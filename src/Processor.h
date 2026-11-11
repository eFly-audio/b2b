// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LinkEngine.h"
#include "AudioLink.h"
#include "VideoLink.h"
#if JUCE_USE_CAMERA
 #include <juce_video/juce_video.h>
#endif

struct ChatLine {
    int slot = -1; // sender's slot (-1: you / system)
    juce::String from, text;
    bool mine = false, system = false;
    juce::String time;
};

struct MidiPreview {
    struct N { float start, end; int pitch; };
    std::vector<N> notes; // start/end normalised to 0..1
    int lo = 60, hi = 72, bars = 1;
    juce::String summary;
    static std::shared_ptr<MidiPreview> load(const juce::File& f);
};

struct FileRow {
    enum class Status { Active, Done, Failed };
    std::shared_ptr<MidiPreview> midi;
    int id = 0;
    juce::String name, path;
    bool incoming = false;
    juce::String from;  // incoming: who sent it
    int fromSlot = -1;
    int recipients = 1, delivered = 0, failed = 0; // outgoing: one row for everyone
    juce::uint64 sentTo[3] {};                     // outgoing: bytes per person
    juce::uint64 bytes = 0, total = 0;
    Status status = Status::Active;
};

// The processor owns the network engine and the UI model so the session
// survives closing/reopening the plugin window.
class Processor : public juce::AudioProcessor, private juce::Timer {
public:
    Processor();
    ~Processor() override;

    void prepareToPlay(double sr, int block) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b2b"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- UI API (message thread)
    void host();
    void join(const juce::String& code);
    void leave();
    void sendChat(const juce::String& text);
    void sendFiles(const juce::StringArray& paths);
    juce::File receiveFolder() const { return receiveDir; }
    void setReceiveFolder(const juce::File& dir);
    bool keepOnTop = true; // pop-out video window
    void toggleRecording();
    void sendRecording();          // send the last recording to the collaborator
    juce::File lastRecording;      // folder of the last finished recording
    bool lastRecordingSent = false;
    bool autoSendRecordings = false;

    // audio
    AudioLink& audio() { return *audioLink; }
    void setMicEnabled(bool on);
    void setAppAudio(bool on);
    void selectMic(const juce::String& name);
    juce::String micName;

    // video
    VideoLink& video() { return *videoLink; }
    void setScreenShare(bool on);
    void setCam(bool on);
    void selectCam(const juce::String& name);
    juce::StringArray camNames();
    juce::String camName;
    bool camOn = false;
    int editorW = 1000, editorH = 660;

    juce::String myName;
    smash::LinkState state = smash::LinkState::Idle;
    juce::String statusText, code;
    struct Person { bool present = false; juce::String name; int pingMs = -1; };
    Person people[AudioLink::kPeers];
    int peopleCount() const;
    juce::String peopleSummary() const; // "Kolega, Trečias"
    juce::String nameOf(int slot) const;
    bool isHost() const { return engine->isHost(); }
    int pingMs = -1; // worst ping in the room
    std::vector<ChatLine> chat;
    std::vector<FileRow> files;
    int chatVersion = 0, fileVersion = 0, stateVersion = 0;

private:
    void timerCallback() override;
    void checkMidiOutbox();
    int outboxTick = 0;
    void addSystem(const juce::String& s);
    FileRow* findFile(int id, bool incoming);

    juce::File receiveDir;
    std::unique_ptr<smash::LinkEngine> engine;
    std::unique_ptr<AudioLink> audioLink;
    std::unique_ptr<VideoLink> videoLink;
    AudioLink::AppState lastAppState = AudioLink::AppState::Off;
    bool remoteWasActive[AudioLink::kPeers][VideoLink::NumStreams] {};
#if JUCE_USE_CAMERA
    struct CamListener : juce::CameraDevice::Listener {
        VideoLink* v = nullptr;
        std::atomic<juce::int64> last { 0 };
        void imageReceived(const juce::Image& img) override {
            auto now = juce::Time::currentTimeMillis();
            if (now - last.load() < 60) return; // ~15 fps
            last = now;
            v->pushImage(VideoLink::Cam, img, 640, 360);
        }
    } camListener;
    std::unique_ptr<juce::CameraDevice> camera;
#endif
    JUCE_DECLARE_WEAK_REFERENCEABLE(Processor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};
