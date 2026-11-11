// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#pragma once
// b2b video: screen share + webcam, VP8 over a reliable ordered data channel.
// Frames are skipped *before* encoding when the channel backs up, so the VP8
// reference chain never breaks and latency stays bounded.
#include <juce_graphics/juce_graphics.h>
#include <juce_events/juce_events.h>
#include <deque>
#include <mutex>
#include "LinkEngine.h"

class VideoLink : private juce::Thread {
public:
    enum Stream { Screen = 0, Cam = 1, NumStreams = 2 };
    static constexpr int kPeers = smash::LinkEngine::kMaxPeers;

    explicit VideoLink(smash::LinkEngine& engine);
    ~VideoLink() override;

    std::atomic<bool> linked { false };

    // ---- sending
    void setSending(Stream s, bool on);
    bool isSending(Stream s) const { return sending[s].load(); }
    // Hand over a BGRA frame (already scaled to the size you want to send). Any thread.
    void pushFrame(Stream s, const uint8_t* bgra, int w, int h, int strideBytes);
    void pushImage(Stream s, const juce::Image& img, int maxW, int maxH);

    // screen capture source (Windows: GDI; elsewhere: test pattern)
    juce::StringArray screenNames();
    std::atomic<int> screenIndex { 0 };
    // screen share quality settings
    std::atomic<int> screenFps { 10 };
    std::atomic<bool> screenHD { false }; // false: up to 1280x800, true: up to 1920x1200

    // ---- receiving
    void onPacket(int slot, const uint8_t* data, size_t size); // network thread
    void peerJoined(int slot); // they get a keyframe before any delta frame
    void peerLeft(int slot);   // drop their decoders / pictures
    // copy of the latest decoded frame from a person; returns false if nothing new since `version`
    bool latest(int slot, Stream s, juce::Image& out, int& version);
    bool remoteActive(int slot, Stream s) const;
    bool anyRemoteActive(Stream s) const;
    juce::Image localPreview(Stream s);
    void debugInjectRemote(int slot, Stream s, const juce::Image& im); // tests / UI previews only

    // ---- stats
    std::atomic<int> sentFps[NumStreams], sentKbps[NumStreams], skipped[NumStreams];
    std::atomic<int> recvFps[kPeers][NumStreams], recvW[kPeers][NumStreams], recvH[kPeers][NumStreams];

private:
    struct Enc;
    struct Dec;
    void run() override;
    void encodeIfDue(Stream s, int64_t now);
    void decodePending();
    void grabScreen();
    void sendCtl(uint8_t type, Stream s);
    void sendCtlTo(int slot, uint8_t type, Stream s);
    void clearSlot(int slot);

    smash::LinkEngine& engine;
    std::atomic<bool> sending[NumStreams];
    std::atomic<bool> forceKey[NumStreams];
    // per receiver: must get a keyframe before deltas (just joined / fell behind)
    std::atomic<bool> peerNeedsKey[kPeers][NumStreams];
    std::atomic<bool> peerReset[kPeers];
    int64_t lastCatchupKey[NumStreams] {};

    // latest-frame mailbox per stream (sender side)
    std::mutex boxMtx;
    struct Box { std::vector<uint8_t> bgra; int w = 0, h = 0; bool fresh = false; } box[NumStreams];
    int64_t lastEncode[NumStreams] {};
    int fpsTarget[NumStreams] { 10, 15 };
    int kbpsTarget[NumStreams] { 2500, 700 };
    uint64_t lastHash[NumStreams] {};
    int64_t lastSentAt[NumStreams] {};
    int screenMaxW() const { return screenHD.load() ? 1920 : 1280; }
    int screenMaxH() const { return screenHD.load() ? 1200 : 800; }
    std::unique_ptr<Enc> enc[NumStreams];
    uint32_t pts[NumStreams] {};

    // receive
    std::mutex inMtx;
    std::deque<std::pair<int, std::vector<uint8_t>>> inbox;
    std::unique_ptr<Dec> dec[kPeers][NumStreams];
    std::mutex imgMtx;
    juce::Image img[kPeers][NumStreams], preview[NumStreams];
    int imgVersion[kPeers][NumStreams] {};
    std::atomic<int64_t> lastRecv[kPeers][NumStreams];

    // stats windows
    int64_t statT0 = 0;
    int cntSent[NumStreams] {}, cntRecv[kPeers][NumStreams] {}, bytesSent[NumStreams] {};

    // screen grab state
    int64_t lastGrab = 0;
    std::vector<uint8_t> grabBuf;
    int testPhase = 0;
};
