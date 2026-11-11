// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#pragma once
// STREAM APP AUDIO: what the DAW itself outputs (e.g. Ableton's browser preview, which goes to the
// Cue out and never passes through plugins on the master).
//
// The captured app audio also contains everything b2b played (your mix + everyone else's voice/DAW).
// Sending that would echo people back to themselves, so we subtract it: b2b knows exactly what it
// output (y). Captured c = g*y(delayed by D) + extra. We find D (integer: capture runs at the host
// rate, so the samples are bit-identical copies) and g, and keep only e = c - g*y(t-D): the extras.
// Until it is locked, nothing is passed while b2b is playing anything (no echo risk).
//
// Pure C++ (no JUCE) so it can be unit tested. Threads: pushPlayed = audio thread,
// pushCaptured = capture thread, process/popResidual = one worker thread.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

class AppAudioCanceller {
public:
    enum class Lock { Off = 0, Searching, Locked };

    void reset(double rate) {
        std::lock_guard<std::mutex> l(m);
        sr = rate;
        ring.assign(2, std::vector<float>(kRing, 0.0f));
        P = C = 0;
        playedIn.clear(); capIn.clear();
        playedIn.reserve(kRing * 2); capIn.reserve(kRing * 2); // the audio thread never allocates
        capHist.assign(2, std::vector<float>(kRing, 0.0f));
        out.clear();
        locked = false; lockState = Lock::Searching; off = 0; badChecks = 0; lastCheckC = 0; lastSearchC = 0;
        g[0] = g[1] = 1.0f;
    }

    // audio thread: what b2b just output (host rate). Cheap: append under a short lock.
    void pushPlayed(const float* L, const float* R, int n) {
        std::unique_lock<std::mutex> l(inMtx, std::try_to_lock);
        if (!l.owns_lock()) { droppedPlayed += n; return; }
        if (playedIn.size() + (size_t) n * 2 > playedIn.capacity()) { droppedPlayed += n; return; }
        for (int i = 0; i < n; ++i) { playedIn.push_back(L[i]); playedIn.push_back(R ? R[i] : L[i]); }
    }
    // capture thread: what the app output (must be at the same rate as the host)
    void pushCaptured(const float* L, const float* R, int n) {
        std::lock_guard<std::mutex> l(inMtx);
        if (capIn.size() + (size_t) n * 2 > capIn.capacity()) return;
        for (int i = 0; i < n; ++i) { capIn.push_back(L[i]); capIn.push_back(R ? R[i] : L[i]); }
    }

    // worker: consume inputs, update alignment, produce residual
    void process() {
        std::vector<float> pin, cin;
        {
            std::lock_guard<std::mutex> l(inMtx);
            pin.assign(playedIn.begin(), playedIn.end()); // copy + clear keeps the reserved capacity
            playedIn.clear();
            cin.assign(capIn.begin(), capIn.end());
            capIn.clear();
        }
        std::lock_guard<std::mutex> l(m);
        if (ring.empty()) return;
        // a gap in the played stream (dropped push) shifts the alignment: force a re-search
        if (droppedPlayed.exchange(0) > 0) { locked = false; lockState = Lock::Searching; }
        for (size_t i = 0; i + 1 < pin.size(); i += 2) {
            ring[0][P & kMask] = pin[i];
            ring[1][P & kMask] = pin[i + 1];
            ++P;
        }
        for (size_t i = 0; i + 1 < cin.size(); i += 2) pendingCap.push_back({ cin[i], cin[i + 1] });

        // captured samples go into history right away (alignment search needs them)
        for (auto& s : pendingCap) {
            capHist[0][C & kMask] = s.l;
            capHist[1][C & kMask] = s.r;
            ++C;
            ++unprocessed;
        }
        pendingCap.clear();

        const int64_t hop = (int64_t) (sr * (locked ? 0.25 : 0.5));
        if (!locked && C - lastSearchC >= hop && C > kSearchW) { lastSearchC = C; search(); }
        else if (locked && C - lastCheckC >= hop) { lastCheckC = C; track(); }

        // residual for every captured sample we can match
        const bool silent = playedSilent();
        while (unprocessed > 0) {
            const int64_t k = C - unprocessed;
            float eL, eR;
            const float cl = capHist[0][k & kMask], cr = capHist[1][k & kMask];
            if (locked) {
                const int64_t p = k + off;
                if (p >= P) break;            // played sample not here yet: wait
                if (p < P - kRing) { --unprocessed; continue; }
                eL = cl - g[0] * ring[0][p & kMask];
                eR = cr - g[1] * ring[1][p & kMask];
            } else if (silent) {
                eL = cl; eR = cr;             // b2b plays nothing: nothing to cancel, pass it all
            } else {
                eL = eR = 0.0f;               // not locked while b2b plays: mute (no echo)
            }
            out.push_back(eL);
            out.push_back(eR);
            --unprocessed;
        }
        if (out.size() > (size_t) (sr * 2 * 2)) out.erase(out.begin(), out.end() - (ptrdiff_t) (sr * 2));
    }

    // worker: take up to n residual frames (interleaved stereo). Returns frames written.
    int popResidual(float* interleaved, int n) {
        std::lock_guard<std::mutex> l(m);
        const int got = (int) std::min<size_t>((size_t) n, out.size() / 2);
        std::copy(out.begin(), out.begin() + got * 2, interleaved);
        out.erase(out.begin(), out.begin() + got * 2);
        return got;
    }
    int residualReady() {
        std::lock_guard<std::mutex> l(m);
        return (int) (out.size() / 2);
    }

    Lock state() const { return lockState.load(); }
    int delaySamples() const { return lastDelay.load(); }
    float cancelDb() const { return lastCancelDb.load(); } // how much of b2b's own output was removed

private:
    static constexpr int kRing = 1 << 17, kMask = kRing - 1;
    static constexpr int kSearchW = 16384, kDecim = 8;
    struct Fr { float l, r; };

    float monoPlayed(int64_t p) const { return ring[0][p & kMask] + ring[1][p & kMask]; }
    float monoCap(int64_t k) const { return capHist[0][k & kMask] + capHist[1][k & kMask]; }

    bool playedSilent() const {
        // b2b output nothing for longer than any possible capture delay
        const int n = (int) std::min<int64_t>(P, (int64_t) (sr * 0.75));
        float pk = 0;
        for (int64_t p = P - n; p < P; ++p) pk = std::max(pk, std::abs(monoPlayed(p)));
        return pk < 1.0e-4f;
    }

    // normalised correlation of captured [k0, k0+W) against played shifted by o
    double corrAt(int64_t k0, int W, int64_t o, double* gain = nullptr, int ch = -1) const {
        double xy = 0, xx = 0, yy = 0;
        for (int i = 0; i < W; ++i) {
            const int64_t k = k0 + i, p = k + o;
            const double c = ch < 0 ? monoCap(k) : capHist[(size_t) ch][k & kMask];
            const double y = ch < 0 ? monoPlayed(p) : ring[(size_t) ch][p & kMask];
            xy += c * y; xx += c * c; yy += y * y;
        }
        if (gain) *gain = yy > 1e-12 ? xy / yy : 1.0;
        return (xx > 1e-12 && yy > 1e-12) ? xy / std::sqrt(xx * yy) : 0.0;
    }

    bool windowOk(int64_t k0, int W, int64_t o) const {
        const int64_t p0 = k0 + o, p1 = k0 + W + o;
        return p0 >= std::max<int64_t>(0, P - kRing + 16) && p1 <= P && k0 >= C - kRing + 16 && k0 >= 0;
    }

    double playedEnergy(int64_t p0, int W) const {
        double e = 0;
        for (int i = 0; i < W; ++i) { const double v = monoPlayed(p0 + i); e += v * v; }
        return e / W;
    }

    void adopt(int64_t o, int64_t k0, int W) {
        off = o;
        for (int ch = 0; ch < 2; ++ch) {
            double gg = 1.0;
            corrAt(k0, W, o, &gg, ch);
            g[ch] = (float) std::clamp(gg, 0.0, 4.0);
        }
        locked = true;
        lockState = Lock::Locked;
        badChecks = 0;
        lastDelay = (int) (P - C - o);
        measureCancel(k0, W);
    }

    void measureCancel(int64_t k0, int W) {
        double ce = 0, ee = 0;
        for (int i = 0; i < W; ++i) {
            const int64_t k = k0 + i, p = k + off;
            for (int ch = 0; ch < 2; ++ch) {
                const double c = capHist[(size_t) ch][k & kMask], y = ring[(size_t) ch][p & kMask];
                const double e = c - g[ch] * y;
                ce += c * c; ee += e * e;
            }
        }
        lastCancelDb = ce > 1e-12 ? (float) (10.0 * std::log10(std::max(ee, 1e-20) / ce)) : 0.0f;
    }

    // full search: coarse (decimated) over 0..~0.7 s, refine the best few peaks at full rate
    void search() {
        lockState = Lock::Searching;
        const int W = kSearchW;
        const int64_t k0 = C - W;
        const int64_t base = P - C; // offset if the delay were 0
        const int maxLag = (int) std::min<int64_t>((int64_t) (sr * 0.7), kRing - W - 64);
        if (playedEnergy(P - W - maxLag / 2, W) < 1e-8) return; // nothing to align with yet

        const int Wd = W / kDecim, Ld = maxLag / kDecim;
        std::vector<float> cd((size_t) Wd), pd((size_t) (Wd + Ld + 2));
        for (int i = 0; i < Wd; ++i) {
            float s = 0;
            for (int j = 0; j < kDecim; ++j) s += monoCap(k0 + i * kDecim + j);
            cd[(size_t) i] = s;
        }
        // played window covering all lags: p = k + base - lag
        const int64_t pStart = k0 + base - (int64_t) Ld * kDecim;
        for (int i = 0; i < Wd + Ld; ++i) {
            float s = 0;
            for (int j = 0; j < kDecim; ++j) {
                const int64_t p = pStart + (int64_t) i * kDecim + j;
                s += (p >= P - kRing && p < P) ? monoPlayed(p) : 0.0f;
            }
            pd[(size_t) i] = s;
        }
        double cc = 0;
        for (float v : cd) cc += (double) v * v;
        if (cc < 1e-10) return;
        std::vector<std::pair<double, int>> peaks;
        for (int lag = 0; lag <= Ld; ++lag) {
            const int startIdx = Ld - lag;
            double xy = 0, yy = 0;
            for (int i = 0; i < Wd; ++i) {
                const double y = pd[(size_t) (startIdx + i)];
                xy += cd[(size_t) i] * y; yy += y * y;
            }
            const double r = yy > 1e-12 ? xy / std::sqrt(cc * yy) : 0.0;
            peaks.push_back({ r, lag });
        }
        std::sort(peaks.begin(), peaks.end(), [](auto& a, auto& b) { return a.first > b.first; });
        std::vector<int> cand;
        for (auto& [r, lag] : peaks) {
            if (r < 0.3) break;
            bool near = false;
            for (int c : cand) near |= std::abs(c - lag) < 4;
            if (!near) cand.push_back(lag);
            if (cand.size() >= 4) break;
        }
        const int Wf = 8192;
        const int64_t kf = C - Wf;
        double best = 0;
        int64_t bestO = 0;
        for (int lagD : cand)
            for (int d = -kDecim * 2; d <= kDecim * 2; ++d) {
                const int64_t o = base - (int64_t) lagD * kDecim + d;
                if (!windowOk(kf, Wf, o)) continue;
                const double r = corrAt(kf, Wf, o);
                if (r > best) { best = r; bestO = o; }
            }
        if (best > 0.95) adopt(bestO, kf, Wf);
    }

    // locked: small search around the current offset (buffer size changes, glitches)
    void track() {
        const int W = 4096;
        const int64_t k0 = C - W - 64;
        if (k0 < 0 || !windowOk(k0, W, off)) return;
        if (playedEnergy(k0 + off, W) < 1e-8) return; // silence: nothing to check against
        double best = -1;
        int64_t bestO = off;
        for (int d = -64; d <= 64; ++d) {
            const int64_t o = off + d;
            if (!windowOk(k0, W, o)) continue;
            const double r = corrAt(k0, W, o);
            if (r > best) { best = r; bestO = o; }
        }
        // loud extras (a preview over the music) lower the correlation everywhere, so judge by
        // *where* the peak is, not how high: keep the lock while the peak stays on our offset
        const double cur = corrAt(k0, W, off);
        if (best > 0.25) {
            badChecks = 0;
            if (bestO != off && best > cur + 0.02) adopt(bestO, k0, W);
            else {
                for (int ch = 0; ch < 2; ++ch) { // slow gain follow (master fader moves); LS gain is unbiased by extras
                    double gg = 1.0;
                    if (corrAt(k0, W, off, &gg, ch) > 0.25) g[ch] = 0.8f * g[ch] + 0.2f * (float) std::clamp(gg, 0.0, 4.0);
                }
                measureCancel(k0, W);
            }
        } else if (++badChecks >= 3) { // b2b's output isn't in the app audio any more (moved off the master?)
            locked = false;
            lockState = Lock::Searching;
            lastSearchC = 0;
        }
    }

    std::mutex inMtx, m;
    std::vector<float> playedIn, capIn;
    std::vector<Fr> pendingCap;
    std::vector<std::vector<float>> ring, capHist;
    std::vector<float> out;
    double sr = 48000.0;
    int64_t P = 0, C = 0, off = 0, unprocessed = 0, lastCheckC = 0, lastSearchC = 0;
    bool locked = false;
    int badChecks = 0;
    float g[2] { 1.0f, 1.0f };
    std::atomic<Lock> lockState { Lock::Off };
    std::atomic<int> lastDelay { 0 }, droppedPlayed { 0 };
    std::atomic<float> lastCancelDb { 0.0f };
};
