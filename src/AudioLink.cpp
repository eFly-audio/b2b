// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#include "AudioLink.h"
#include "AppCapture.h"
#include <opus.h>
#include <cmath>
#if JUCE_MAC
#include "MacSupport.h"
#endif

namespace {
constexpr int kFrame = 480;           // 10 ms @ 48 kHz
constexpr int kHeader = 8;            // 'S','A',ver=2,stream, seq(u32 LE)
constexpr int kVoiceKbps = 48;

int64_t nowMs() { return juce::Time::currentTimeMillis(); }

float peakOf(const float* const* ch, int nch, int n) {
    float p = 0;
    for (int c = 0; c < nch; ++c)
        for (int i = 0; i < n; ++i) p = juce::jmax(p, std::abs(ch[c][i]));
    return p;
}
} // namespace

// ============================================================== FIFO helpers
void AudioLink::Fifo::write(const float* const* src, int channels, int n) {
    int s1, z1, s2, z2;
    fifo.prepareToWrite(n, s1, z1, s2, z2);
    for (int c = 0; c < buf.getNumChannels(); ++c) {
        const float* in = src[juce::jmin(c, channels - 1)];
        if (z1 > 0) buf.copyFrom(c, s1, in, z1);
        if (z2 > 0) buf.copyFrom(c, s2, in + z1, z2);
    }
    fifo.finishedWrite(z1 + z2);
}

void AudioLink::Fifo::read(float* const* dst, int n, bool add, float gain) {
    int s1, z1, s2, z2;
    fifo.prepareToRead(n, s1, z1, s2, z2);
    for (int c = 0; c < buf.getNumChannels(); ++c) {
        if (add) {
            if (z1 > 0) juce::FloatVectorOperations::addWithMultiply(dst[c], buf.getReadPointer(c, s1), gain, z1);
            if (z2 > 0) juce::FloatVectorOperations::addWithMultiply(dst[c] + z1, buf.getReadPointer(c, s2), gain, z2);
        } else {
            if (z1 > 0) juce::FloatVectorOperations::copyWithMultiply(dst[c], buf.getReadPointer(c, s1), gain, z1);
            if (z2 > 0) juce::FloatVectorOperations::copyWithMultiply(dst[c] + z1, buf.getReadPointer(c, s2), gain, z2);
        }
    }
    fifo.finishedRead(z1 + z2);
}

void AudioLink::Fifo::peek(juce::AudioBuffer<float>& dst, int n) const {
    int s1, z1, s2, z2;
    fifo.prepareToRead(n, s1, z1, s2, z2);
    for (int c = 0; c < buf.getNumChannels(); ++c) {
        if (z1 > 0) dst.copyFrom(c, 0, buf, c, s1, z1);
        if (z2 > 0) dst.copyFrom(c, z1, buf, c, s2, z2);
    }
}

float AudioLink::Fifo::peakOfNext(int n) const {
    int s1, z1, s2, z2;
    fifo.prepareToRead(n, s1, z1, s2, z2);
    float p = 0;
    for (int c = 0; c < buf.getNumChannels(); ++c) {
        if (z1 > 0) p = juce::jmax(p, buf.getMagnitude(c, s1, z1));
        if (z2 > 0) p = juce::jmax(p, buf.getMagnitude(c, s2, z2));
    }
    return p;
}

// ============================================================== lifecycle
AudioLink::AudioLink(smash::LinkEngine& e) : juce::Thread("b2b audio"), engine(e) {
    int err = 0;
    encDaw = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &err);
    if (encDaw) {
        opus_encoder_ctl(encDaw, OPUS_SET_COMPLEXITY(8));
        opus_encoder_ctl(encDaw, OPUS_SET_VBR(1));
        opus_encoder_ctl(encDaw, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    }
    encVoice = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &err);
    if (encVoice) {
        opus_encoder_ctl(encVoice, OPUS_SET_COMPLEXITY(8));
        opus_encoder_ctl(encVoice, OPUS_SET_BITRATE(kVoiceKbps * 1000));
        opus_encoder_ctl(encVoice, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    }
    for (int s = 0; s < NumStreams; ++s) {
        recvGain[s] = 1.0f;
        recvPeak[s] = 0.0f;
        receiving[s] = false;
        for (int p = 0; p < kPeers; ++p) {
            rx[p][s].channels = s == Daw ? 2 : 1;
            rx[p][s].dec = opus_decoder_create(48000, rx[p][s].channels, &err);
            peerPeak[p][s] = 0.0f;
            peerReceiving[p][s] = false;
        }
    }
    for (int p = 0; p < kPeers; ++p) { personGain[p] = 1.0f; personMute[p] = false; resetReq[p] = false; }
    prepare(48000.0, 512);
    micFifo.setSize(1, 48000);
    tmpMic.setSize(1, 4096);
    startThread(juce::Thread::Priority::high);
}

AudioLink::~AudioLink() {
    appcapture::stop();
    stopRecording();
    recThread.stopThread(2000);
    stopThread(2000);
    closeMic();
    if (encDaw) opus_encoder_destroy(encDaw);
    if (encVoice) opus_encoder_destroy(encVoice);
    for (auto& pr : rx)
        for (auto& r : pr)
            if (r.dec) opus_decoder_destroy(r.dec);
}

void AudioLink::prepare(double rate, int maxBlock) {
    std::lock_guard<std::mutex> l(cfgMtx);
    rxTmp.setSize(2, juce::jmax(8192, maxBlock * 2));
    hostRate = rate;
    lastBlock = 64;
    int sec = (int) std::ceil(rate);
    dawFifo.setSize(2, sec);
    for (auto& pr : rx) for (auto& r : pr) {
        r.outFifo.setSize(2, sec);
        r.pcm48.setSize(2, 48000);
        for (auto& i : r.interp) i.reset();
        r.playing = false;
        r.isPlaying = false;
    }
    tmpIn.setSize(2, 4096);
    tmpApp.setSize(2, 4096);
    appFifo.setSize(2, sec);
    appPlaying = false;
    if (appOn.load() && std::abs(appRate - rate) > 1.0) appRestart = true;
    frame48.setSize(2, kFrame);
    tmpOut.setSize(2, 1024);
    for (auto& i : sendInterp) i.reset();
}

// ============================================================== audio thread
void AudioLink::process(juce::AudioBuffer<float>& buffer) {
    const int n = buffer.getNumSamples();
    const int nch = juce::jmin(2, buffer.getNumChannels());
    if (nch == 0) return;
    if (n > lastBlock.load()) lastBlock = n;  // grow at once...
    if (n > blockPeak.load()) blockPeak = n;  // ...shrink back via the worker (one odd big block mustn't stick)
    const bool on = linked.load();

    const juce::SpinLock::ScopedTryLockType recGuard(recLock);
    const bool rec = recGuard.isLocked() && recording.load();
    if (rec && recMaster) recMaster->write(buffer.getArrayOfReadPointers(), n); // your master / plugin input, pre-mix

    if (on && streamInput.load()) {
        dawFifo.write(buffer.getArrayOfReadPointers(), nch, n);
        sendPeak = peakOf(buffer.getArrayOfReadPointers(), nch, n) * streamGain.load();
    } else {
        sendPeak = 0;
    }

    float top[NumStreams] = {};
    for (int p = 0; p < kPeers; ++p)
        for (int st = 0; st < NumStreams; ++st) {
            mixRx(rx[p][st], p, st, buffer, n, nch, on, rec);
            top[st] = juce::jmax(top[st], peerPeak[p][st].load());
        }
    for (int st = 0; st < NumStreams; ++st) recvPeak[st] = top[st];
    // app audio: remember exactly what b2b output, so it can be removed from the captured app audio
    if (appOn.load()) app.pushPlayed(buffer.getReadPointer(0), nch > 1 ? buffer.getReadPointer(1) : nullptr, n);
}

// ============================================================== app audio
juce::String AudioLink::setAppAudio(bool on) {
    appcapture::stop();
    appOn = false;
    appRestart = false;
    if (!on) return {};
    const double rate = hostRate.load();
    {
        std::lock_guard<std::mutex> l(cfgMtx);
        app.reset(rate);
        appFifo.discard(appFifo.ready());
        appPlaying = false;
        appRate = rate;
    }
    auto err = appcapture::start(rate, [this](const float* L, const float* R, int n) {
        float p = 0;
        for (int i = 0; i < n; ++i) p = juce::jmax(p, std::abs(L[i]), std::abs(R[i]));
        appCapPeak = juce::jmax(p, appCapPeak.load() * 0.9f);
        app.pushCaptured(L, R, n);
    });
    if (!err.empty()) return juce::String::fromUTF8(err.c_str());
    appStartMs = juce::Time::currentTimeMillis();
    appOn = true;
    return {};
}

AudioLink::AppState AudioLink::appState() const {
    if (!appOn.load()) return AppState::Off;
    const double ar = appcapture::actualRate();
    if (ar > 0 && std::abs(ar - hostRate.load()) > 1.0) return AppState::WrongRate;
    if (app.state() == AppAudioCanceller::Lock::Locked) return AppState::Locked;
    if (juce::Time::currentTimeMillis() - appStartMs.load() > 4000 && appCapPeak.load() < 1.0e-5f) return AppState::NoSignal;
    return AppState::Searching;
}

void AudioLink::mixRx(Rx& r, int p, int s, juce::AudioBuffer<float>& buffer, int n, int nch, bool on, bool rec) {
    float* tmp[2] = { rxTmp.getWritePointer(0), rxTmp.getWritePointer(1) };
    if (const int sk = r.outSkip.exchange(0); sk > 0) r.outFifo.discard(sk);
    if (on && r.isPlaying.load() && r.outFifo.ready() < n) r.starved = true;
    const int got = on ? juce::jmin(n, r.outFifo.ready(), rxTmp.getNumSamples()) : 0;
    auto& w = recPeer[p][s];
    if (got > 0) {
        const float g = personMute[p].load() ? 0.0f : recvGain[s].load() * personGain[p].load();
        r.outFifo.read(tmp, got, false, 1.0f);
        peerPeak[p][s] = rxTmp.getMagnitude(0, got) * g;
        if (rec && w) w->write(rxTmp.getArrayOfReadPointers(), got); // as received, before your volume
        if (g > 0.0f) {
            if (nch == 1) {
                buffer.addFrom(0, 0, rxTmp, 0, 0, got, g * 0.5f);
                buffer.addFrom(0, 0, rxTmp, 1, 0, got, g * 0.5f);
            } else {
                buffer.addFrom(0, 0, rxTmp, 0, 0, got, g);
                buffer.addFrom(1, 0, rxTmp, 1, 0, got, g);
            }
        }
    } else {
        peerPeak[p][s] = 0;
        if (rec && w) {
            // keep received tracks aligned with your master: silence while nothing arrives
            const int m = juce::jmin(n, rxTmp.getNumSamples());
            rxTmp.clear(0, m);
            w->write(rxTmp.getArrayOfReadPointers(), m);
        }
    }
}

// ============================================================== recording
std::unique_ptr<AudioLink::Writer> AudioLink::makeWriter(const juce::File& f, double rate, int channels) {
    f.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream>(f);
    if (!stream->openedOk()) return {};
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(stream.get(), rate, (unsigned) channels, 24, {}, 0));
    if (!w) return {};
    stream.release(); // owned by the writer now
    return std::make_unique<Writer>(w.release(), recThread, 1 << 18);
}

juce::String AudioLink::startRecording(const juce::File& folder, const juce::StringArray& names) {
    stopRecording();
    if (!folder.createDirectory()) return "Can't create " + folder.getFullPathName();
    if (!recThread.isThreadRunning()) recThread.startThread();
    const double hr = hostRate.load();
    auto master = makeWriter(folder.getChildFile("You - master.wav"), hr, 2);
    auto mic = makeWriter(folder.getChildFile("You - mic.wav"), micRate.load(), 1);
    if (!master || !mic) return "Couldn't create the recording files";
    std::unique_ptr<Writer> peers[kPeers][NumStreams];
    juce::StringArray used { "You" };
    for (int p = 0; p < kPeers && p < names.size(); ++p) {
        if (names[p].isEmpty()) continue;
        auto nm = juce::File::createLegalFileName(names[p]);
        if (nm.isEmpty()) nm = "Person " + juce::String(p + 1);
        for (int k = 2; used.contains(nm, true); ++k) nm = juce::File::createLegalFileName(names[p]) + " " + juce::String(k);
        used.add(nm);
        peers[p][Voice] = makeWriter(folder.getChildFile(nm + " - voice.wav"), hr, 2);
        peers[p][Daw] = makeWriter(folder.getChildFile(nm + " - DAW.wav"), hr, 2);
        if (!peers[p][Voice] || !peers[p][Daw]) return "Couldn't create the recording files";
    }
    const juce::SpinLock::ScopedLockType l(recLock);
    recMaster = std::move(master);
    recMic = std::move(mic);
    for (int p = 0; p < kPeers; ++p)
        for (int st = 0; st < NumStreams; ++st) recPeer[p][st] = std::move(peers[p][st]);
    recFolder = folder;
    recStartMs = juce::Time::getMillisecondCounterHiRes();
    recording = true;
    return {};
}

juce::File AudioLink::stopRecording() {
    std::unique_ptr<Writer> a, b, c[kPeers][NumStreams];
    juce::File f;
    {
        const juce::SpinLock::ScopedLockType l(recLock);
        if (!recording.load()) return {};
        recording = false;
        a = std::move(recMaster); b = std::move(recMic);
        for (int p = 0; p < kPeers; ++p)
            for (int st = 0; st < NumStreams; ++st) c[p][st] = std::move(recPeer[p][st]);
        f = recFolder;
    }
    // writers flush and close the files as they are destroyed (outside the lock)
    return f;
}

// ============================================================== mic
juce::StringArray AudioLink::micDevices() {
#if JUCE_WINDOWS
    if (!deviceType) deviceType.reset(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::shared));
#elif JUCE_MAC
    if (!deviceType) deviceType.reset(juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio());
#endif
    if (!deviceType) return {};
    deviceType->scanForDevices();
    return deviceType->getDeviceNames(true);
}

juce::String AudioLink::openMic(const juce::String& name) {
    closeMic();
#if JUCE_MAC
    if (!macsupport::hostDeclares("NSMicrophoneUsageDescription"))
        return "Your DAW doesn't allow microphone access on macOS. Route your mic into a track with b2b on it and use STREAM DAW instead.";
#endif
    if (!deviceType) micDevices();
    if (!deviceType) return "Mic capture isn't available on this system";
    auto names = deviceType->getDeviceNames(true);
    auto use = name.isNotEmpty() && names.contains(name) ? name : names[deviceType->getDefaultDeviceIndex(true)];
    if (use.isEmpty()) return "No microphone found";
    std::unique_ptr<juce::AudioIODevice> d(deviceType->createDevice({}, use));
    if (!d) return "Can't open " + use;
    juce::BigInteger ins;
    ins.setRange(0, juce::jmax(1, juce::jmin(2, d->getInputChannelNames().size())), true);
    auto rates = d->getAvailableSampleRates();
    double rate = rates.contains(48000.0) ? 48000.0 : (rates.isEmpty() ? 48000.0 : rates[0]);
    auto err = d->open(ins, {}, rate, d->getDefaultBufferSize());
    if (err.isNotEmpty()) return "Mic error: " + err;
    micName = use;
    micDevice = std::move(d);
    micDevice->start(this);
    return {};
}

void AudioLink::closeMic() {
    if (micDevice) {
        micDevice->stop();
        micDevice->close();
        micDevice.reset();
    }
}

void AudioLink::audioDeviceAboutToStart(juce::AudioIODevice* d) {
    std::lock_guard<std::mutex> l(cfgMtx);
    micRate = d->getCurrentSampleRate();
    micFifo.setSize(1, (int) std::ceil(micRate.load()));
    micInterp.reset();
    micMonoSize = juce::jmax(4096, d->getCurrentBufferSizeSamples() * 2);
    micMono.allocate((size_t) micMonoSize, true);
}

void AudioLink::audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut, int n,
                                                 const juce::AudioIODeviceCallbackContext&) {
    for (int c = 0; c < numOut; ++c)
        if (out[c]) juce::FloatVectorOperations::clear(out[c], n);
    if (numIn <= 0 || !in[0] || n > micMonoSize) return;
    if (numIn > 1 && in[1]) {
        juce::FloatVectorOperations::copy(micMono.get(), in[0], n);
        juce::FloatVectorOperations::add(micMono.get(), in[1], n);
        juce::FloatVectorOperations::multiply(micMono.get(), 0.5f, n);
        pushMic(micMono.get(), n, micRate.load());
    } else {
        pushMic(in[0], n, micRate.load());
    }
}

void AudioLink::pushMic(const float* mono, int n, double rate) {
    if (rate != micRate.load()) {
        std::lock_guard<std::mutex> l(cfgMtx);
        micRate = rate;
        micFifo.setSize(1, (int) std::ceil(rate));
        micInterp.reset();
    }
    float p = 0;
    for (int i = 0; i < n; ++i) p = juce::jmax(p, std::abs(mono[i]));
    micPeak = micOn.load() ? p * micGain.load() : 0.0f;
    {
        const juce::SpinLock::ScopedTryLockType recGuard(recLock);
        if (recGuard.isLocked() && recording.load() && recMic && micOn.load()) {
            const float* m[1] = { mono };
            recMic->write(m, n);
        }
    }
    if (!micOn.load() || !linked.load()) return;
    const float* src[1] = { mono };
    micFifo.write(src, 1, n);
}

// ============================================================== worker
void AudioLink::run() {
    while (!threadShouldExit()) {
        {
            std::lock_guard<std::mutex> l(cfgMtx);
            doSendDaw();
            doSendVoice();
            const bool on = linked.load();
            if (!on && wasLinked)
                for (auto& pr : rx) for (auto& r : pr) resetReceive(r);
            {
                const auto t = nowMs();
                if (t - blockWindowStart > 3000) {
                    blockWindowStart = t;
                    const int pk = blockPeak.exchange(0);
                    if (pk > 0 && !std::getenv("B2B_OLDJB")) lastBlock = juce::jmax(32, pk);
                }
            }
            wasLinked = on;
            for (int p = 0; p < kPeers; ++p)
                if (resetReq[p].exchange(false))
                    for (auto& r : rx[p]) resetReceive(r);
            int buf = 0, loss = 0;
            const auto now = nowMs();
            for (int s = 0; s < NumStreams; ++s) {
                bool any = false;
                for (int p = 0; p < kPeers; ++p) {
                    auto& r = rx[p][s];
                    if (on) doReceive(r);
                    const bool rcv = on && now - r.lastPacketMs < 500;
                    peerReceiving[p][s] = rcv;
                    any |= rcv;
                    if (rcv) {
                        const double hr = hostRate.load();
                        buf = juce::jmax(buf, (int) (r.jitter.size() * 10 + r.pcm48.ready() / 48 + r.outFifo.ready() * 1000.0 / hr));
                        loss = juce::jmax(loss, r.lossPct);
                    }
                }
                receiving[s] = any;
            }
            bufferMs = buf;
            lossPct = loss;
        }
        wait(2);
    }
}

void AudioLink::sendPacket(Stream s, const uint8_t* opus, int bytes) {
    uint8_t pkt[1500];
    if (bytes <= 0 || bytes > (int) sizeof(pkt) - kHeader) return;
    uint32_t q = sendSeq[s]++;
    pkt[0] = 'S'; pkt[1] = 'A'; pkt[2] = 2; pkt[3] = (uint8_t) s;
    pkt[4] = uint8_t(q); pkt[5] = uint8_t(q >> 8); pkt[6] = uint8_t(q >> 16); pkt[7] = uint8_t(q >> 24);
    memcpy(pkt + kHeader, opus, (size_t) bytes);
    engine.sendMedia("audio", pkt, (size_t) (bytes + kHeader));
}

void AudioLink::doSendDaw() {
    const bool on = linked.load();
    const bool useDaw = on && streamInput.load();
    const bool useApp = on && appOn.load();

    // app audio residual (the extras, with b2b's own output removed) -> appFifo
    if (appOn.load()) {
        app.process();
        float il[2 * 512];
        int got;
        while ((got = app.popResidual(il, 512)) > 0) {
            float L[512], R[512];
            for (int i = 0; i < got; ++i) { L[i] = il[2 * i]; R[i] = il[2 * i + 1]; }
            const float* src[2] = { L, R };
            if (useApp && appFifo.space() > got) appFifo.write(src, 2, got);
        }
        appCapPeak = appCapPeak.load() * 0.999f;
    }
    if (!useApp) { appFifo.discard(appFifo.ready()); appPlaying = false; }
    if ((!useDaw && !useApp) || !encDaw) { dawFifo.discard(dawFifo.ready()); return; }
    if (!useDaw) dawFifo.discard(dawFifo.ready());

    const int kbps = juce::jlimit(32, 510, bitrateKbps.load());
    if (kbps != encBitrate) {
        opus_encoder_ctl(encDaw, OPUS_SET_BITRATE(kbps * 1000));
        encBitrate = kbps;
    }
    const double hr = hostRate.load();
    const double rIn = hr / 48000.0;
    const int need = (int) std::ceil(kFrame * rIn) + 4;
    if (dawFifo.ready() > (int) (hr * 0.2)) dawFifo.discard(dawFifo.ready() - need * 2);
    // the residual arrives in capture-sized bursts: keep a small cushion, trim if it piles up
    const int appCushion = (int) (hr * 0.03);
    if (appFifo.ready() > (int) (hr * 0.25)) appFifo.discard(appFifo.ready() - appCushion);
    if (!appPlaying && appFifo.ready() >= appCushion + need) appPlaying = true;

    for (int guard = 0; guard < 30; ++guard) {
        if (useDaw) {
            if (dawFifo.ready() < need) break;
            dawFifo.peek(tmpIn, need);
        } else {
            if (!appPlaying || appFifo.ready() < need) break;
            tmpIn.clear(0, need);
        }
        bool mixApp = false;
        if (useApp && appPlaying) {
            if (appFifo.ready() >= need) {
                appFifo.peek(tmpApp, need);
                tmpIn.addFrom(0, 0, tmpApp, 0, 0, need);
                tmpIn.addFrom(1, 0, tmpApp, 1, 0, need);
                mixApp = true;
            } else {
                appPlaying = false; // ran dry: rebuild the cushion
            }
        }
        int used = sendInterp[0].process(rIn, tmpIn.getReadPointer(0), frame48.getWritePointer(0), kFrame);
        sendInterp[1].process(rIn, tmpIn.getReadPointer(1), frame48.getWritePointer(1), kFrame);
        if (useDaw) dawFifo.discard(used);
        if (mixApp) appFifo.discard(used);
        frame48.applyGain(streamGain.load());
        float inter[kFrame * 2];
        for (int i = 0; i < kFrame; ++i) {
            inter[2 * i] = frame48.getSample(0, i);
            inter[2 * i + 1] = frame48.getSample(1, i);
        }
        uint8_t opus[1400];
        int bytes = opus_encode_float(encDaw, inter, kFrame, opus, (opus_int32) sizeof(opus));
        sendPacket(Daw, opus, bytes);
    }
}

void AudioLink::doSendVoice() {
    const bool mic = linked.load() && micOn.load();
    if (!mic || !encVoice) { micFifo.discard(micFifo.ready()); return; }
    const double rMic = micRate.load() / 48000.0;
    const int need = (int) std::ceil(kFrame * rMic) + 4;
    if (micFifo.ready() > (int) (micRate.load() * 0.15)) micFifo.discard(micFifo.ready() - need * 2);

    for (int guard = 0; guard < 30 && micFifo.ready() >= need; ++guard) {
        micFifo.peek(tmpMic, need);
        float m[kFrame];
        int used = micInterp.process(rMic, tmpMic.getReadPointer(0), m, kFrame);
        micFifo.discard(used);
        const float g = micGain.load();
        for (auto& v : m) v *= g;
        uint8_t opus[1400];
        int bytes = opus_encode_float(encVoice, m, kFrame, opus, (opus_int32) sizeof(opus));
        sendPacket(Voice, opus, bytes);
    }
}

// ============================================================== receive
void AudioLink::resetSlot(int slot) {
    if (slot >= 0 && slot < kPeers) resetReq[slot] = true;
}

void AudioLink::onPacket(int slot, const uint8_t* d, size_t n) {
    if (slot < 0 || slot >= kPeers) return;
    if (n <= (size_t) kHeader || d[0] != 'S' || d[1] != 'A' || d[2] != 2 || d[3] >= NumStreams) return;
    auto& r = rx[slot][d[3]];
    uint32_t seq = uint32_t(d[4]) | (uint32_t(d[5]) << 8) | (uint32_t(d[6]) << 16) | (uint32_t(d[7]) << 24);
    std::lock_guard<std::mutex> l(jbMtx);
    r.lastPacketMs = nowMs();
    {
        const double arr = juce::Time::getMillisecondCounterHiRes();
        const double transit = arr - 10.0 * (double) seq;
        r.transits.push_back({ arr, transit });
        while (!r.transits.empty() && arr - r.transits.front().first > 4000.0) r.transits.pop_front();
        if (r.transits.size() > 600) r.transits.pop_front();
    }
    if (r.playing && (int32_t) (seq - r.expected) < 0) return; // too late
    if (r.jitter.size() > 150) r.jitter.erase(r.jitter.begin());
    r.jitter[seq].assign(d + kHeader, d + n);
}

void AudioLink::resetReceive(Rx& r) {
    std::lock_guard<std::mutex> l(jbMtx);
    r.jitter.clear();
    r.transits.clear();
    r.lastPacketMs = 0;
    r.playing = false;
    r.isPlaying = false;
    r.pcm48.discard(r.pcm48.ready());
    if (r.dec) opus_decoder_ctl(r.dec, OPUS_RESET_STATE);
    for (auto& i : r.interp) i.reset();
}

void AudioLink::doReceive(Rx& r) {
    if (!r.dec) return;
    std::unique_lock<std::mutex> jl(jbMtx);
    const double hr = hostRate.load();

    // output side: two host blocks (of the last few seconds) - the worker can wake late (Windows timer ~15 ms)
    const int targetOut = juce::jmin(r.outFifo.fifo.getTotalSize() / 2, juce::jmax((int) (hr * 0.02), lastBlock.load() * 2 + 64));
    const double targetOutMs = targetOut * 1000.0 / hr;
    // network side: as much as the arrival times actually spread (measured), not a fixed guess
    double cushion = 40.0;
    static const bool oldJb = std::getenv("B2B_OLDJB") != nullptr; // A/B against v0.7 behaviour (tests)
    if (r.transits.size() >= 50 && !oldJb) {
        double lo = 1e18, hi = -1e18;
        for (auto& t : r.transits) { lo = juce::jmin(lo, t.second); hi = juce::jmax(hi, t.second); }
        cushion = juce::jlimit(30.0, 400.0, hi - lo + 15.0);
    }
    r.cushionMs = (float) cushion;
    const int prebuffer = juce::jmax(2, (int) std::ceil((targetOutMs + cushion) / 10.0));

    // way behind (a stall, a burst after a network hiccup): jump ahead instead of creeping back at 0.5 %
    if (r.playing && !oldJb) {
        const double buffered48 = (double) r.jitter.size() * kFrame + r.pcm48.ready() + r.outFifo.ready() * 48000.0 / hr;
        const double excessMs = buffered48 / 48.0 - (targetOutMs + cushion);
        if (excessMs > 60.0) {
            int drop = juce::jmax(0, juce::jmin((int) r.jitter.size() - 2, (int) ((excessMs - 5.0) / 10.0)));
            for (int i = 0; i < drop; ++i) r.jitter.erase(r.jitter.begin());
            if (drop > 0) r.expected = r.jitter.begin()->first;
            // excess sitting in the output FIFO (e.g. after one huge host block): the audio thread drops it
            const int outExcess = r.outFifo.ready() - targetOut;
            if (outExcess > (int) (hr * 0.03)) r.outSkip = outExcess;
            if (drop > 0 || outExcess > (int) (hr * 0.03)) {
                ++skips;
                if (std::getenv("B2B_AUDIO_DEBUG")) std::printf("    [rx] catch-up: excess %.0f ms, dropped %d, cushion %.0f, outMs %.0f\n", excessMs, drop, cushion, targetOutMs);
            }
        }
    }

    if (r.starved.exchange(false) && r.playing) {
        r.playing = false; // the DAW ran dry: rebuild the cushion
        ++underruns;
        if (std::getenv("B2B_AUDIO_DEBUG")) std::printf("    [rx] underrun: jitter %d pkts, out %d, cushion %.0f, outMs %.0f\n", (int) r.jitter.size(), r.outFifo.ready(), cushion, targetOutMs);
    }
    if (!r.playing && (int) r.jitter.size() >= prebuffer) {
        r.playing = true;
        r.expected = r.jitter.begin()->first;
        r.pcm48.discard(r.pcm48.ready());
    }
    r.isPlaying = r.playing;

    const int chunk = 128;
    float pcm[kFrame * 2];
    bool dry = false;
    while (r.playing && !dry && r.outFifo.ready() < targetOut && r.outFifo.space() > chunk) {
        const double buffered48 = (double) r.jitter.size() * kFrame + r.pcm48.ready() + r.outFifo.ready() * 48000.0 / hr;
        const double target48 = (targetOutMs + cushion) * 48.0;
        const double errSec = (buffered48 - target48) / 48000.0;
        const double factor = 1.0 + juce::jlimit(-0.005, 0.005, errSec * 0.05);
        const double ratio = 48000.0 / hr * factor;
        const int need48 = (int) std::ceil(chunk * ratio) + 4;

        while (r.pcm48.ready() < need48) {
            while (!r.jitter.empty() && (int32_t) (r.jitter.begin()->first - r.expected) < 0) r.jitter.erase(r.jitter.begin());
            int got = 0;
            auto it = r.jitter.find(r.expected);
            if (it != r.jitter.end()) {
                got = opus_decode_float(r.dec, it->second.data(), (opus_int32) it->second.size(), pcm, kFrame, 0);
                r.jitter.erase(it);
                ++r.decoded;
            } else if (!r.jitter.empty()) {
                int32_t gap = (int32_t) (r.jitter.begin()->first - r.expected);
                if (gap > 50) { r.expected = r.jitter.begin()->first; continue; } // sender restarted
                got = opus_decode_float(r.dec, nullptr, 0, pcm, kFrame, 0);     // packet loss concealment
                ++r.lost;
            } else {
                dry = true; // nothing new yet; the audio thread still has outFifo to play
                break;
            }
            ++r.expected;
            if (got <= 0) continue;
            float L[kFrame], R[kFrame];
            for (int i = 0; i < got; ++i) {
                if (r.channels == 2) { L[i] = pcm[2 * i]; R[i] = pcm[2 * i + 1]; }
                else { L[i] = R[i] = pcm[i]; }
            }
            const float* src[2] = { L, R };
            r.pcm48.write(src, 2, got);
        }
        if (dry) break;

        juce::AudioBuffer<float> peekBuf(tmpIn.getArrayOfWritePointers(), 2, need48);
        r.pcm48.peek(peekBuf, need48);
        int used = r.interp[0].process(ratio, tmpIn.getReadPointer(0), tmpOut.getWritePointer(0), chunk);
        r.interp[1].process(ratio, tmpIn.getReadPointer(1), tmpOut.getWritePointer(1), chunk);
        r.pcm48.discard(used);
        r.outFifo.write(tmpOut.getArrayOfReadPointers(), 2, chunk);
    }

    if (std::getenv("B2B_AUDIO_DEBUG")) {
        static thread_local juce::int64 lastDbg = 0;
        if (nowMs() - lastDbg > 300 && r.playing) {
            lastDbg = nowMs();
            std::printf("    [rx] jit %d pk  pcm48 %d  out %d  cush %.0f  outMs %.0f  exp %u  first %u\n", (int) r.jitter.size(), r.pcm48.ready(), r.outFifo.ready(), cushion, targetOutMs,
                        r.expected, r.jitter.empty() ? 0u : r.jitter.begin()->first);
        }
    }
    if (r.decoded + r.lost >= 200) {
        r.lossPct = juce::roundToInt(100.0 * r.lost / (r.decoded + r.lost));
        r.decoded = r.lost = 0;
    }
}
