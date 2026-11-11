// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#include "VideoLink.h"
#include <algorithm>

#include <vpx/vp8cx.h>
#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>
#include <vpx/vpx_encoder.h>

#if JUCE_WINDOWS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#elif JUCE_MAC
#include "MacSupport.h"
#endif

namespace {
constexpr int kHdr = 14;          // 'S','V',ver,type, stream, flags, w(2), h(2), pts(4)
constexpr size_t kMaxQueued = 300 * 1024;

int64_t nowMs() { return juce::Time::currentTimeMillis(); }

inline uint8_t clip8(int v) { return (uint8_t) (v < 0 ? 0 : (v > 255 ? 255 : v)); }

// BGRA (memory order) -> I420, BT.601 limited range. w, h must be even.
void bgraToI420(const uint8_t* src, int stride, int w, int h, uint8_t* Y, uint8_t* U, uint8_t* V) {
    for (int y = 0; y < h; y += 2) {
        const uint8_t* r0 = src + (size_t) y * stride;
        const uint8_t* r1 = r0 + stride;
        uint8_t* y0 = Y + (size_t) y * w;
        uint8_t* y1 = y0 + w;
        uint8_t* u = U + (size_t) (y / 2) * (w / 2);
        uint8_t* v = V + (size_t) (y / 2) * (w / 2);
        for (int x = 0; x < w; x += 2) {
            int sb = 0, sg = 0, sr = 0;
            const uint8_t* px[4] = { r0 + x * 4, r0 + x * 4 + 4, r1 + x * 4, r1 + x * 4 + 4 };
            uint8_t* yo[4] = { y0 + x, y0 + x + 1, y1 + x, y1 + x + 1 };
            for (int k = 0; k < 4; ++k) {
                int b = px[k][0], g = px[k][1], r = px[k][2];
                *yo[k] = (uint8_t) (((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
                sb += b; sg += g; sr += r;
            }
            sb >>= 2; sg >>= 2; sr >>= 2;
            u[x / 2] = (uint8_t) (((-38 * sr - 74 * sg + 112 * sb + 128) >> 8) + 128);
            v[x / 2] = (uint8_t) (((112 * sr - 94 * sg - 18 * sb + 128) >> 8) + 128);
        }
    }
}

juce::Image i420ToImage(const vpx_image_t* im) {
    const int w = (int) im->d_w, h = (int) im->d_h;
    juce::Image out(juce::Image::RGB, w, h, false);
    juce::Image::BitmapData bd(out, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y) {
        const uint8_t* Y = im->planes[0] + (size_t) y * im->stride[0];
        const uint8_t* U = im->planes[1] + (size_t) (y / 2) * im->stride[1];
        const uint8_t* V = im->planes[2] + (size_t) (y / 2) * im->stride[2];
        uint8_t* d = bd.getLinePointer(y);
        for (int x = 0; x < w; ++x) {
            int c = Y[x] - 16, dd = U[x / 2] - 128, e = V[x / 2] - 128;
            int r = (298 * c + 409 * e + 128) >> 8;
            int g = (298 * c - 100 * dd - 208 * e + 128) >> 8;
            int b = (298 * c + 516 * dd + 128) >> 8;
            auto* p = reinterpret_cast<juce::PixelRGB*>(d + x * bd.pixelStride);
            p->setARGB(255, clip8(r), clip8(g), clip8(b));
        }
    }
    return out;
}
} // namespace

struct VideoLink::Enc {
    vpx_codec_ctx_t ctx {};
    vpx_image_t img {};
    std::vector<uint8_t> i420;
    int w = 0, h = 0, kbps = 0, fps = 0;
    bool ok = false;
    ~Enc() { if (ok) vpx_codec_destroy(&ctx); }
};

struct VideoLink::Dec {
    vpx_codec_ctx_t ctx {};
    int w = 0, h = 0;
    bool ok = false, needKey = true;
    int64_t lastKeyReq = 0;
    ~Dec() { if (ok) vpx_codec_destroy(&ctx); }
};

// ============================================================== lifecycle
VideoLink::VideoLink(smash::LinkEngine& e) : juce::Thread("b2b video"), engine(e) {
    for (int s = 0; s < NumStreams; ++s) {
        sending[s] = false; forceKey[s] = true;
        sentFps[s] = sentKbps[s] = skipped[s] = 0;
        for (int p = 0; p < kPeers; ++p) {
            lastRecv[p][s] = 0; recvFps[p][s] = recvW[p][s] = recvH[p][s] = 0;
            peerNeedsKey[p][s] = true;
        }
    }
    for (auto& r : peerReset) r = false;
    startThread(juce::Thread::Priority::normal);
}

VideoLink::~VideoLink() { stopThread(3000); }

void VideoLink::setSending(Stream s, bool on) {
    bool was = sending[s].exchange(on);
    if (on && !was) forceKey[s] = true;
    if (!on && was) {
        std::lock_guard<std::mutex> l(boxMtx);
        box[s].fresh = false;
        sendCtl('X', s);
        std::lock_guard<std::mutex> l2(imgMtx);
        preview[s] = {};
    }
}

void VideoLink::sendCtl(uint8_t type, Stream s) {
    uint8_t p[5] = { 'S', 'V', 1, type, (uint8_t) s };
    engine.sendMedia("video", p, sizeof p);
}

void VideoLink::sendCtlTo(int slot, uint8_t type, Stream s) {
    uint8_t p[5] = { 'S', 'V', 1, type, (uint8_t) s };
    engine.sendMediaTo(slot, "video", p, sizeof p);
}

void VideoLink::peerJoined(int slot) {
    if (slot < 0 || slot >= kPeers) return;
    for (int s = 0; s < NumStreams; ++s) peerNeedsKey[slot][s] = true;
    peerReset[slot] = true;
}

void VideoLink::peerLeft(int slot) {
    if (slot < 0 || slot >= kPeers) return;
    for (int s = 0; s < NumStreams; ++s) peerNeedsKey[slot][s] = true;
    peerReset[slot] = true;
}

void VideoLink::clearSlot(int p) { // worker thread
    for (int s = 0; s < NumStreams; ++s) dec[p][s].reset();
    std::lock_guard<std::mutex> l(imgMtx);
    for (int s = 0; s < NumStreams; ++s) { img[p][s] = {}; ++imgVersion[p][s]; lastRecv[p][s] = 0; }
}

void VideoLink::pushFrame(Stream s, const uint8_t* bgra, int w, int h, int stride) {
    if (!sending[s].load() || w < 16 || h < 16) return;
    w &= ~1; h &= ~1;
    std::lock_guard<std::mutex> l(boxMtx);
    auto& b = box[s];
    b.bgra.resize((size_t) w * h * 4);
    for (int y = 0; y < h; ++y) memcpy(b.bgra.data() + (size_t) y * w * 4, bgra + (size_t) y * stride, (size_t) w * 4);
    b.w = w; b.h = h; b.fresh = true;
}

void VideoLink::pushImage(Stream s, const juce::Image& src, int maxW, int maxH) {
    if (!sending[s].load() || !src.isValid()) return;
    float sc = juce::jmin(1.0f, maxW / (float) src.getWidth(), maxH / (float) src.getHeight());
    int w = juce::jmax(16, juce::roundToInt(src.getWidth() * sc)) & ~1;
    int h = juce::jmax(16, juce::roundToInt(src.getHeight() * sc)) & ~1;
    juce::Image img(juce::Image::ARGB, w, h, false);
    {
        juce::Graphics g(img);
        g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
        g.drawImage(src, 0, 0, w, h, 0, 0, src.getWidth(), src.getHeight());
    }
    juce::Image::BitmapData bd(img, juce::Image::BitmapData::readOnly);
    pushFrame(s, bd.data, w, h, bd.lineStride);
    std::lock_guard<std::mutex> l(imgMtx);
    preview[s] = img;
}

juce::Image VideoLink::localPreview(Stream s) {
    std::lock_guard<std::mutex> l(imgMtx);
    return preview[s];
}

void VideoLink::debugInjectRemote(int p, Stream s, const juce::Image& im) {
    if (p < 0 || p >= kPeers) return;
    std::lock_guard<std::mutex> l(imgMtx);
    img[p][s] = im;
    ++imgVersion[p][s];
    lastRecv[p][s] = nowMs() + 600000;
}

bool VideoLink::latest(int p, Stream s, juce::Image& out, int& version) {
    if (p < 0 || p >= kPeers) return false;
    std::lock_guard<std::mutex> l(imgMtx);
    if (imgVersion[p][s] == version) return false;
    version = imgVersion[p][s];
    out = img[p][s];
    return true;
}

bool VideoLink::remoteActive(int p, Stream s) const {
    return p >= 0 && p < kPeers && linked.load() && nowMs() - lastRecv[p][s].load() < 3000;
}

bool VideoLink::anyRemoteActive(Stream s) const {
    for (int p = 0; p < kPeers; ++p)
        if (remoteActive(p, s)) return true;
    return false;
}

// ============================================================== worker
void VideoLink::run() {
#if JUCE_WINDOWS
    // capture in physical pixels regardless of the host's DPI awareness
    using SetCtx = HANDLE(WINAPI*)(HANDLE);
    if (auto fn = (SetCtx) GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext"))
        fn((HANDLE) -4); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#endif
    bool wasLinked = false;
    while (!threadShouldExit()) {
        const int64_t now = nowMs();
        const bool on = linked.load();
        if (!on && wasLinked) {
            {
                std::lock_guard<std::mutex> l(inMtx);
                inbox.clear();
            }
            for (int p = 0; p < kPeers; ++p) clearSlot(p);
        }
        for (int p = 0; p < kPeers; ++p)
            if (peerReset[p].exchange(false)) {
                {
                    std::lock_guard<std::mutex> l(inMtx);
                    inbox.erase(std::remove_if(inbox.begin(), inbox.end(), [p](auto& e) { return e.first == p; }), inbox.end());
                }
                clearSlot(p);
            }
        if (on && !wasLinked)
            for (auto& f : forceKey) f = true;
        wasLinked = on;

        // your upload is shared by everyone you send to: ease the bitrate as the room fills
        const int others = juce::jmax(1, engine.peerCount());
        const double share = others >= 3 ? 0.6 : (others == 2 ? 0.75 : 1.0);
        fpsTarget[Screen] = juce::jlimit(2, 30, screenFps.load());
        kbpsTarget[Screen] = (int) ((screenHD.load() ? 4500 : 2500) * share);
        kbpsTarget[Cam] = (int) (700 * share);
        if (on) {
            if (sending[Screen].load() && now - lastGrab >= 1000 / fpsTarget[Screen]) {
                lastGrab = now;
                grabScreen();
            }
            for (int s = 0; s < NumStreams; ++s) encodeIfDue((Stream) s, now);
        }
        decodePending();

        if (now - statT0 >= 1000) {
            double dt = (now - statT0) / 1000.0;
            for (int s = 0; s < NumStreams; ++s) {
                sentFps[s] = juce::roundToInt(cntSent[s] / dt);
                for (int p = 0; p < kPeers; ++p) { recvFps[p][s] = juce::roundToInt(cntRecv[p][s] / dt); cntRecv[p][s] = 0; }
                sentKbps[s] = juce::roundToInt(bytesSent[s] * 8 / 1000.0 / dt);
                cntSent[s] = bytesSent[s] = 0;
            }
            statT0 = now;
        }
        wait(4);
    }
}

void VideoLink::encodeIfDue(Stream s, int64_t now) {
    if (!sending[s].load()) {
        enc[s].reset();
        return;
    }
    if (now - lastEncode[s] < 1000 / fpsTarget[s] - 2) return;

    Box frame;
    {
        std::lock_guard<std::mutex> l(boxMtx);
        if (!box[s].fresh) return;
        std::swap(frame.bgra, box[s].bgra);
        frame.w = box[s].w; frame.h = box[s].h;
        box[s].fresh = false;
    }
    lastEncode[s] = now;

    // congestion control, per receiver. Someone whose channel backs up stops getting frames and
    // is marked "needs keyframe"; once drained they get a fresh keyframe. Everyone else keeps going.
    std::vector<int> targets;
    bool anyFresh = false, anyCatchup = false;
    for (const auto& pi : engine.peers()) {
        const int p = pi.slot;
        if (p < 0 || p >= kPeers) continue;
        const size_t q = engine.mediaBufferedTo(p, "video");
        if (q > kMaxQueued) { peerNeedsKey[p][s] = true; continue; }
        if (peerNeedsKey[p][s].load()) {
            if (q < kMaxQueued / 3) { anyCatchup = true; targets.push_back(p); }
            continue;
        }
        anyFresh = true;
        targets.push_back(p);
    }
    if (targets.empty()) { ++skipped[s]; return; }
    if (anyCatchup) {
        // rate-limit catch-up keyframes so one bad link can't flood everyone with them
        if (!anyFresh || now - lastCatchupKey[s] > 1500) { forceKey[s] = true; lastCatchupKey[s] = now; }
        else targets.erase(std::remove_if(targets.begin(), targets.end(), [&](int p) { return peerNeedsKey[p][s].load(); }), targets.end());
    }

    // unchanged screen: don't re-encode it (less shimmer, less bandwidth); still send ~1 fps keep-alive
    if (s == Screen) {
        uint64_t hsh = 1469598103934665603ull;
        const uint32_t* px = reinterpret_cast<const uint32_t*>(frame.bgra.data());
        const size_t count = frame.bgra.size() / 4;
        for (size_t i = 0; i < count; i += 7) hsh = (hsh ^ (px[i] & 0x00ffffffu)) * 1099511628211ull;
        hsh ^= (uint64_t) frame.w << 32 ^ (uint64_t) frame.h;
        if (hsh == lastHash[s] && now - lastSentAt[s] < 1000 && !forceKey[s].load()) return;
        lastHash[s] = hsh;
    }

    auto& e = enc[s];
    if (!e || e->w != frame.w || e->h != frame.h || e->kbps != kbpsTarget[s] || e->fps != fpsTarget[s]) {
        e = std::make_unique<Enc>();
        e->w = frame.w; e->h = frame.h; e->kbps = kbpsTarget[s]; e->fps = fpsTarget[s];
        vpx_codec_enc_cfg_t cfg;
        if (vpx_codec_enc_config_default(vpx_codec_vp8_cx(), &cfg, 0) != VPX_CODEC_OK) { e.reset(); return; }
        cfg.g_w = (unsigned) frame.w;
        cfg.g_h = (unsigned) frame.h;
        cfg.g_timebase.num = 1;
        cfg.g_timebase.den = 1000;
        cfg.rc_target_bitrate = (unsigned) kbpsTarget[s];
        // VBR with a quality ceiling: CBR made busy DAW screens "breathe" (sharp/blurry pumping)
        cfg.rc_end_usage = VPX_VBR;
        cfg.g_lag_in_frames = 0;
        cfg.g_error_resilient = 0;
        cfg.g_threads = 2;
        // the channel is reliable, so no periodic keyframes (they flash); only on start / request
        cfg.kf_mode = VPX_KF_AUTO;
        cfg.kf_max_dist = 100000;
        cfg.rc_min_quantizer = s == Screen ? 4 : 4;
        cfg.rc_max_quantizer = s == Screen ? 36 : 50;
        cfg.rc_undershoot_pct = 100;
        cfg.rc_overshoot_pct = 30;
        cfg.rc_dropframe_thresh = 0;
        cfg.rc_resize_allowed = 0;
        if (vpx_codec_enc_init(&e->ctx, vpx_codec_vp8_cx(), &cfg, 0) != VPX_CODEC_OK) { e.reset(); return; }
        e->ok = true;
        vpx_codec_control(&e->ctx, VP8E_SET_CPUUSED, s == Screen ? 6 : 8);
        vpx_codec_control(&e->ctx, VP8E_SET_SCREEN_CONTENT_MODE, s == Screen ? 1 : 0);
        vpx_codec_control(&e->ctx, VP8E_SET_NOISE_SENSITIVITY, 0);
        vpx_codec_control(&e->ctx, VP8E_SET_STATIC_THRESHOLD, 0);
        e->i420.resize((size_t) frame.w * frame.h * 3 / 2);
        vpx_img_wrap(&e->img, VPX_IMG_FMT_I420, (unsigned) frame.w, (unsigned) frame.h, 1, e->i420.data());
        forceKey[s] = true;
    }

    uint8_t* Y = e->i420.data();
    uint8_t* U = Y + (size_t) frame.w * frame.h;
    uint8_t* V = U + (size_t) frame.w * frame.h / 4;
    bgraToI420(frame.bgra.data(), frame.w * 4, frame.w, frame.h, Y, U, V);

    vpx_enc_frame_flags_t flags = forceKey[s].exchange(false) ? VPX_EFLAG_FORCE_KF : 0;
    pts[s] += (uint32_t) (1000 / fpsTarget[s]);
    if (vpx_codec_encode(&e->ctx, &e->img, pts[s], (unsigned long) (1000 / fpsTarget[s]), flags, VPX_DL_REALTIME) != VPX_CODEC_OK) {
        enc[s].reset();
        return;
    }
    vpx_codec_iter_t it = nullptr;
    while (auto* pkt = vpx_codec_get_cx_data(&e->ctx, &it)) {
        if (pkt->kind != VPX_CODEC_CX_FRAME_PKT) continue;
        std::vector<uint8_t> out((size_t) kHdr + pkt->data.frame.sz);
        bool kf = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
        uint32_t p = pts[s];
        uint8_t h[kHdr] = { 'S', 'V', 1, 'F', (uint8_t) s, (uint8_t) (kf ? 1 : 0),
                            (uint8_t) frame.w, (uint8_t) (frame.w >> 8), (uint8_t) frame.h, (uint8_t) (frame.h >> 8),
                            (uint8_t) p, (uint8_t) (p >> 8), (uint8_t) (p >> 16), (uint8_t) (p >> 24) };
        memcpy(out.data(), h, kHdr);
        memcpy(out.data() + kHdr, pkt->data.frame.buf, pkt->data.frame.sz);
        bool sentAny = false;
        for (int p : targets) {
            if (peerNeedsKey[p][s].load() && !kf) continue;
            if (engine.sendMediaTo(p, "video", out.data(), out.size())) {
                sentAny = true;
                if (kf) peerNeedsKey[p][s] = false;
            } else {
                peerNeedsKey[p][s] = true; // missed a frame: their chain is broken until the next keyframe
            }
        }
        if (sentAny) {
            lastSentAt[s] = now;
            ++cntSent[s];
            bytesSent[s] += (int) out.size();
        }
    }
}

// ============================================================== receive
void VideoLink::onPacket(int slot, const uint8_t* d, size_t n) {
    if (slot < 0 || slot >= kPeers) return;
    if (n < 5 || d[0] != 'S' || d[1] != 'V' || d[2] != 1) return;
    const int s = d[4];
    if (s < 0 || s >= NumStreams) return;
    if (d[3] == 'K') { forceKey[s] = true; return; }
    std::lock_guard<std::mutex> l(inMtx);
    int mine = 0;
    for (auto& e : inbox) mine += e.first == slot;
    if (mine > 60) {
        // way behind on this person: drop their backlog, decoders will ask for a keyframe
        inbox.erase(std::remove_if(inbox.begin(), inbox.end(), [slot](auto& e) { return e.first == slot; }), inbox.end());
        for (int k = 0; k < NumStreams; ++k) {
            uint8_t reset[5] = { 'S', 'V', 1, 'R', (uint8_t) k };
            inbox.emplace_back(slot, std::vector<uint8_t>(reset, reset + 5));
        }
    }
    inbox.emplace_back(slot, std::vector<uint8_t>(d, d + n));
}

void VideoLink::decodePending() {
    for (int guard = 0; guard < 40; ++guard) {
        std::vector<uint8_t> p;
        int slot;
        {
            std::lock_guard<std::mutex> l(inMtx);
            if (inbox.empty()) return;
            slot = inbox.front().first;
            p = std::move(inbox.front().second);
            inbox.pop_front();
        }
        const Stream s = (Stream) p[4];
        auto& dc = dec[slot][s];
        if (p[3] == 'R') { if (dc) dc->needKey = true; continue; }
        if (p[3] == 'X') {
            dc.reset();
            lastRecv[slot][s] = 0;
            std::lock_guard<std::mutex> l(imgMtx);
            img[slot][s] = {};
            ++imgVersion[slot][s];
            continue;
        }
        if (p[3] != 'F' || p.size() <= (size_t) kHdr) continue;
        const bool kf = p[5] & 1;
        const int w = p[6] | (p[7] << 8), h = p[8] | (p[9] << 8);
        lastRecv[slot][s] = nowMs();
        if (!dc || dc->w != w || dc->h != h) {
            dc = std::make_unique<Dec>();
            dc->w = w; dc->h = h;
            vpx_codec_dec_cfg_t cfg {};
            cfg.threads = 2;
            dc->ok = vpx_codec_dec_init(&dc->ctx, vpx_codec_vp8_dx(), &cfg, 0) == VPX_CODEC_OK;
        }
        if (!dc->ok) continue;
        auto requestKey = [&] {
            if (nowMs() - dc->lastKeyReq > 400) { dc->lastKeyReq = nowMs(); sendCtlTo(slot, 'K', s); }
        };
        if (dc->needKey && !kf) { requestKey(); continue; }
        if (vpx_codec_decode(&dc->ctx, p.data() + kHdr, (unsigned) (p.size() - kHdr), nullptr, 0) != VPX_CODEC_OK) {
            dc->needKey = true;
            requestKey();
            continue;
        }
        dc->needKey = false;
        vpx_codec_iter_t it = nullptr;
        while (auto* im = vpx_codec_get_frame(&dc->ctx, &it)) {
            auto frame = i420ToImage(im);
            ++cntRecv[slot][s];
            recvW[slot][s] = (int) im->d_w; recvH[slot][s] = (int) im->d_h;
            std::lock_guard<std::mutex> l(imgMtx);
            img[slot][s] = frame;
            ++imgVersion[slot][s];
        }
    }
}

#if JUCE_WINDOWS
namespace {
struct Mon { RECT r; HMONITOR h; };
BOOL CALLBACK monEnum(HMONITOR hm, HDC, LPRECT r, LPARAM lp) {
    reinterpret_cast<std::vector<Mon>*>(lp)->push_back({ *r, hm });
    return TRUE;
}
std::vector<Mon> monitors() {
    std::vector<Mon> v;
    EnumDisplayMonitors(nullptr, nullptr, monEnum, (LPARAM) &v);
    return v;
}

template <class T> void safeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Desktop Duplication (DXGI): reads the exact composited frame the GPU shows, so GPU-drawn
// windows never come out black/stale like they can with GDI. Falls back to GDI if unavailable.
struct DxgiCapture {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGIOutputDuplication* dup = nullptr;
    ID3D11Texture2D* staging = nullptr;
    HMONITOR monitor = nullptr;
    int W = 0, H = 0;
    std::vector<uint8_t> full; // last full-resolution BGRA frame
    bool haveFrame = false;
    int failures = 0;

    ~DxgiCapture() { reset(); }
    void reset() {
        safeRelease(staging); safeRelease(dup); safeRelease(ctx); safeRelease(dev);
        monitor = nullptr; haveFrame = false; W = H = 0;
    }

    bool init(HMONITOR target) {
        reset();
        IDXGIFactory1* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**) &factory))) return false;
        bool ok = false;
        IDXGIAdapter1* adapter = nullptr;
        for (UINT a = 0; !ok && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
            IDXGIOutput* out = nullptr;
            for (UINT o = 0; !ok && adapter->EnumOutputs(o, &out) != DXGI_ERROR_NOT_FOUND; ++o) {
                DXGI_OUTPUT_DESC d {};
                out->GetDesc(&d);
                if (d.Monitor == target) {
                    IDXGIOutput1* out1 = nullptr;
                    if (SUCCEEDED(out->QueryInterface(__uuidof(IDXGIOutput1), (void**) &out1))) {
                        D3D_FEATURE_LEVEL fl;
                        if (SUCCEEDED(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                                        D3D11_SDK_VERSION, &dev, &fl, &ctx))
                            && SUCCEEDED(out1->DuplicateOutput(dev, &dup))) {
                            DXGI_OUTDUPL_DESC dd {};
                            dup->GetDesc(&dd);
                            W = (int) dd.ModeDesc.Width;
                            H = (int) dd.ModeDesc.Height;
                            monitor = target;
                            ok = W > 0 && H > 0;
                        }
                        safeRelease(out1);
                    }
                }
                safeRelease(out);
            }
            safeRelease(adapter);
        }
        safeRelease(factory);
        if (!ok) reset();
        return ok;
    }

    // true when `full` holds a valid frame (new or unchanged since last call)
    bool grab() {
        if (!dup) return false;
        DXGI_OUTDUPL_FRAME_INFO info {};
        IDXGIResource* res = nullptr;
        HRESULT hr = dup->AcquireNextFrame(30, &info, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return haveFrame; // nothing changed on screen
        if (FAILED(hr)) { reset(); return false; }           // access lost (mode change, UAC...): re-init next time
        bool ok = false;
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**) &tex))) {
            D3D11_TEXTURE2D_DESC td {};
            tex->GetDesc(&td);
            if (!staging) {
                D3D11_TEXTURE2D_DESC sd = td;
                sd.Usage = D3D11_USAGE_STAGING;
                sd.BindFlags = 0;
                sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                sd.MiscFlags = 0;
                sd.MipLevels = 1;
                sd.ArraySize = 1;
                sd.SampleDesc.Count = 1;
                dev->CreateTexture2D(&sd, nullptr, &staging);
            }
            if (staging && td.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
                ctx->CopyResource(staging, tex);
                D3D11_MAPPED_SUBRESOURCE m {};
                if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
                    W = (int) td.Width; H = (int) td.Height;
                    full.resize((size_t) W * H * 4);
                    for (int y = 0; y < H; ++y)
                        memcpy(full.data() + (size_t) y * W * 4, static_cast<const uint8_t*>(m.pData) + (size_t) y * m.RowPitch, (size_t) W * 4);
                    ctx->Unmap(staging, 0);
                    ok = haveFrame = true;
                }
            }
            safeRelease(tex);
        }
        safeRelease(res);
        dup->ReleaseFrame();
        return ok || haveFrame;
    }
};

// area-average downscale BGRA (sharp text, no shimmer from nearest-neighbour sampling)
void boxScale(const uint8_t* src, int sw, int sh, uint8_t* dst, int dw, int dh) {
    std::vector<int> x0((size_t) dw + 1);
    for (int x = 0; x <= dw; ++x) x0[(size_t) x] = (int) ((int64_t) x * sw / dw);
    for (int y = 0; y < dh; ++y) {
        int ya = (int) ((int64_t) y * sh / dh), yb = juce::jmax(ya + 1, (int) ((int64_t) (y + 1) * sh / dh));
        uint8_t* out = dst + (size_t) y * dw * 4;
        for (int x = 0; x < dw; ++x) {
            int xa = x0[(size_t) x], xb = juce::jmax(xa + 1, x0[(size_t) x + 1]);
            uint32_t b = 0, g = 0, r = 0, n = 0;
            for (int yy = ya; yy < yb; ++yy) {
                const uint8_t* p = src + ((size_t) yy * sw + xa) * 4;
                for (int xx = xa; xx < xb; ++xx, p += 4) { b += p[0]; g += p[1]; r += p[2]; ++n; }
            }
            out[x * 4 + 0] = (uint8_t) (b / n);
            out[x * 4 + 1] = (uint8_t) (g / n);
            out[x * 4 + 2] = (uint8_t) (r / n);
            out[x * 4 + 3] = 255;
        }
    }
}

void drawCursor(HDC dc, RECT r, float sc) {
    CURSORINFO ci {};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor) return;
    ICONINFO ii {};
    int hx = 0, hy = 0;
    if (GetIconInfo(ci.hCursor, &ii)) {
        hx = (int) ii.xHotspot; hy = (int) ii.yHotspot;
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
    }
    const float cs = juce::jmax(0.6f, sc);
    int cx = juce::roundToInt((ci.ptScreenPos.x - r.left) * sc - hx * cs);
    int cy = juce::roundToInt((ci.ptScreenPos.y - r.top) * sc - hy * cs);
    int csz = juce::roundToInt(GetSystemMetrics(SM_CXCURSOR) * cs);
    DrawIconEx(dc, cx, cy, ci.hCursor, csz, csz, 0, nullptr, DI_NORMAL);
}

DxgiCapture gDxgi; // used only from the video thread
bool gDxgiDisabled = false;
} // namespace

juce::StringArray VideoLink::screenNames() {
    juce::StringArray names;
    auto m = monitors();
    for (size_t i = 0; i < m.size(); ++i)
        names.add("Screen " + juce::String((int) i + 1) + "  (" + juce::String(m[i].r.right - m[i].r.left) + "x"
                  + juce::String(m[i].r.bottom - m[i].r.top) + ")");
    if (names.isEmpty()) names.add("Screen 1");
    return names;
}

void VideoLink::grabScreen() {
    auto mons = monitors();
    if (mons.empty()) return;
    const Mon mon = mons[(size_t) juce::jlimit(0, (int) mons.size() - 1, screenIndex.load())];
    const RECT r = mon.r;
    const int sw = r.right - r.left, sh = r.bottom - r.top;
    if (sw < 16 || sh < 16) return;
    const float sc = juce::jmin(1.0f, (float) screenMaxW() / sw, (float) screenMaxH() / sh);
    const int dw = juce::roundToInt(sw * sc) & ~1, dh = juce::roundToInt(sh * sc) & ~1;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = dw;
    bi.bmiHeader.biHeight = -dh; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(mem, bmp);
        bool done = false;
        if (!gDxgiDisabled) {
            if (gDxgi.monitor != mon.h && !gDxgi.init(mon.h)) {
                if (++gDxgi.failures > 5) gDxgiDisabled = true; // e.g. remote desktop: stay on GDI
            }
            if (gDxgi.monitor == mon.h && gDxgi.grab() && gDxgi.W > 0) {
                boxScale(gDxgi.full.data(), gDxgi.W, gDxgi.H, static_cast<uint8_t*>(bits), dw, dh);
                done = true;
            }
        }
        if (!done) {
            SetStretchBltMode(mem, HALFTONE);
            SetBrushOrgEx(mem, 0, 0, nullptr);
            StretchBlt(mem, 0, 0, dw, dh, screen, r.left, r.top, sw, sh, SRCCOPY);
        }
        drawCursor(mem, r, sc);
        GdiFlush();
        pushFrame(Screen, static_cast<const uint8_t*>(bits), dw, dh, dw * 4);
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}
#elif JUCE_MAC
juce::StringArray VideoLink::screenNames() {
    juce::StringArray n;
    for (auto& s : macsupport::screenNames()) n.add(juce::String::fromUTF8(s.c_str()));
    return n;
}

void VideoLink::grabScreen() {
    int w = 0, h = 0;
    if (macsupport::grabScreen(screenIndex.load(), screenMaxW(), screenMaxH(), grabBuf, w, h))
        pushFrame(Screen, grabBuf.data(), w, h, w * 4);
}
#else
juce::StringArray VideoLink::screenNames() { return { "Test pattern" }; }

void VideoLink::grabScreen() {
    if (std::getenv("B2B_NO_TESTPATTERN")) return; // tests push their own frames
    // synthetic "screen": dark background, grid, moving accent block (used by tests)
    const int w = 1280, h = 720;
    grabBuf.resize((size_t) w * h * 4);
    ++testPhase;
    const int bx = (testPhase * 16) % (w - 160);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = grabBuf.data() + (size_t) y * w * 4;
        for (int x = 0; x < w; ++x) {
            uint8_t v = ((x % 64) == 0 || (y % 64) == 0) ? 60 : 20;
            bool block = x >= bx && x < bx + 160 && y >= 280 && y < 440;
            row[x * 4 + 0] = block ? 61 : v;   // B
            row[x * 4 + 1] = block ? 255 : v;  // G
            row[x * 4 + 2] = block ? 200 : v;  // R
            row[x * 4 + 3] = 255;
        }
    }
    pushFrame(Screen, grabBuf.data(), w, h, w * 4);
}
#endif
