// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#pragma once
// b2b audio: two independent Opus streams over an unreliable data channel
//  - DAW stream:   plugin input, stereo, music-tuned, user bitrate
//  - Voice stream: Windows/macOS mic, mono, speech-tuned
// Each has its own jitter buffer, drift-corrected resampler and receive gain,
// so the collaborator's voice and DAW can be balanced independently.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <deque>
#include <map>
#include <mutex>
#include "LinkEngine.h"
#include "AppAudio.h"

struct OpusEncoder;
struct OpusDecoder;

class AudioLink : private juce::Thread, private juce::AudioIODeviceCallback {
public:
    enum Stream { Daw = 0, Voice = 1, NumStreams = 2 };
    static constexpr int kPeers = smash::LinkEngine::kMaxPeers; // up to 3 other people

    explicit AudioLink(smash::LinkEngine& engine);
    ~AudioLink() override;

    // host side
    void prepare(double hostSampleRate, int maxBlockSize);
    void process(juce::AudioBuffer<float>& buffer); // audio thread only

    // network side (any thread)
    void onPacket(int slot, const uint8_t* data, size_t size);
    void resetSlot(int slot); // a person left / a new one took the slot

    // settings (any thread)
    std::atomic<bool> linked { false };
    std::atomic<bool> streamInput { false }; // send the plugin's input (DAW audio)
    std::atomic<bool> micOn { false };       // send the mic
    std::atomic<float> micGain { 1.0f }, streamGain { 1.0f };
    std::atomic<float> recvGain[NumStreams];  // everyone's DAW / voice volume (bar sliders)
    std::atomic<float> personGain[kPeers];    // per-person volume (mixer)
    std::atomic<bool> personMute[kPeers];
    std::atomic<int> bitrateKbps { 192 };    // DAW stream

    // ---- APP AUDIO (message thread): also send what the DAW outputs outside plugins (Ableton's
    // browser preview). b2b's own output is subtracted from it, so nobody hears an echo.
    juce::String setAppAudio(bool on); // "" ok, else a message for the user
    bool appAudioOn() const { return appOn.load(); }
    enum class AppState { Off, Searching, Locked, NoSignal, WrongRate };
    AppState appState() const;
    bool appNeedsRestart() const { return appRestart.load(); } // host sample rate changed

    // mic device (message thread)
    juce::StringArray micDevices();
    juce::String openMic(const juce::String& deviceName); // "" on success, else error text
    void closeMic();
    juce::String micDeviceName() const { return micName; }
    void pushMic(const float* mono, int numSamples, double sampleRate); // also used by tests

    // meters / status (read from UI)
    std::atomic<float> sendPeak { 0 }, micPeak { 0 };
    std::atomic<float> recvPeak[NumStreams];          // loudest person, per stream (bar meter)
    std::atomic<float> peerPeak[kPeers][NumStreams];
    std::atomic<bool> receiving[NumStreams];
    std::atomic<bool> peerReceiving[kPeers][NumStreams];
    std::atomic<int> bufferMs { 0 }, lossPct { 0 };
    std::atomic<int> underruns { 0 };
    std::atomic<int> lastBlock { 512 };       // biggest host block of the last few seconds
    std::atomic<int> blockPeak { 0 };
    std::atomic<int> skips { 0 };              // latency catch-ups (jumped ahead after a hiccup)
    bool anyReceiving() const { return receiving[Daw].load() || receiving[Voice].load(); }

    // ---- recording (message thread): separate WAVs, sample-aligned start
    //  You - master.wav (plugin input), You - mic.wav, <person> - voice.wav, <person> - DAW.wav per person
    //  names: index = slot, empty = nobody there
    juce::String startRecording(const juce::File& folder, const juce::StringArray& names);
    juce::File stopRecording();
    bool isRecording() const { return recording.load(); }
    double recordSeconds() const { return recording.load() ? (juce::Time::getMillisecondCounterHiRes() - recStartMs) / 1000.0 : 0.0; }

private:
    struct Fifo {
        juce::AbstractFifo fifo { 1 };
        juce::AudioBuffer<float> buf;
        void setSize(int channels, int size) { buf.setSize(channels, size); fifo.setTotalSize(size); fifo.reset(); }
        int ready() const { return fifo.getNumReady(); }
        int space() const { return fifo.getFreeSpace(); }
        void write(const float* const* src, int channels, int n);   // channels: 1 duplicates to all
        void read(float* const* dst, int n, bool add, float gain); // reads min(n, ready)
        void peek(juce::AudioBuffer<float>& dst, int n) const;
        void discard(int n) { fifo.finishedRead(juce::jmin(n, fifo.getNumReady())); }
        float peakOfNext(int n) const;
    };

    struct Rx {
        std::map<uint32_t, std::vector<uint8_t>> jitter;
        OpusDecoder* dec = nullptr;
        int channels = 2;
        Fifo pcm48, outFifo;
        juce::LagrangeInterpolator interp[2];
        bool playing = false;
        uint32_t expected = 0;
        int64_t lastPacketMs = 0;
        // arrival jitter: transit = arrival - seq*10ms; the spread over the last 4 s is the cushion we need
        std::deque<std::pair<double, double>> transits; // (arrival ms, transit ms)
        std::atomic<float> cushionMs { 40.0f };
        std::atomic<int> outSkip { 0 }; // samples the audio thread should drop from outFifo (catch-up)
        int decoded = 0, lost = 0, lossPct = 0;
        std::atomic<bool> starved { false }, isPlaying { false };
    };

    void run() override;
    void doSendDaw();
    void doSendVoice();
    void doReceive(Rx& rx);
    void mixRx(Rx& r, int slot, int s, juce::AudioBuffer<float>& buffer, int n, int nch, bool on, bool rec);
    void resetReceive(Rx& rx);
    void sendPacket(Stream s, const uint8_t* opus, int bytes);

    void audioDeviceIOCallbackWithContext(const float* const* in, int numIn, float* const* out, int numOut,
                                          int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override;
    void audioDeviceStopped() override {}

    smash::LinkEngine& engine;
    std::mutex cfgMtx, jbMtx;
    std::atomic<double> hostRate { 48000.0 };
    std::atomic<double> micRate { 48000.0 };

    Fifo dawFifo, micFifo;
    juce::LagrangeInterpolator sendInterp[2], micInterp;
    juce::AudioBuffer<float> tmpIn, tmpMic, frame48, tmpOut;

    OpusEncoder* encDaw = nullptr;
    OpusEncoder* encVoice = nullptr;
    int encBitrate = 0;
    uint32_t sendSeq[NumStreams] {};

    Rx rx[kPeers][NumStreams];

    // app audio
    AppAudioCanceller app;
    std::atomic<bool> appOn { false }, appRestart { false };
    std::atomic<float> appCapPeak { 0 };
    std::atomic<juce::int64> appStartMs { 0 };
    double appRate = 0;
    Fifo appFifo;
    bool appPlaying = false;
    juce::AudioBuffer<float> tmpApp;
    std::atomic<bool> resetReq[kPeers];
    bool wasLinked = false;
    juce::int64 blockWindowStart = 0;

    // recording
    using Writer = juce::AudioFormatWriter::ThreadedWriter;
    std::unique_ptr<Writer> makeWriter(const juce::File& f, double rate, int channels);
    juce::TimeSliceThread recThread { "b2b recorder" };
    std::unique_ptr<Writer> recMaster, recMic, recPeer[kPeers][NumStreams];
    std::atomic<bool> recording { false };
    juce::SpinLock recLock;
    double recStartMs = 0;
    juce::File recFolder;
    juce::AudioBuffer<float> rxTmp;

    std::unique_ptr<juce::AudioIODeviceType> deviceType;
    std::unique_ptr<juce::AudioIODevice> micDevice;
    juce::String micName;
    juce::HeapBlock<float> micMono;
    int micMonoSize = 0;
};
