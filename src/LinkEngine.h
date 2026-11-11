// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#pragma once
// b2b network engine (no JUCE dependency so it can be tested natively).
// Up to 4 people per room, full mesh: everyone has a direct WebRTC connection to everyone.
// Signaling: PeerJS-compatible WebSocket server (0.peerjs.com by default).
//  - host registers "smashlink-<CODE>"; joiners connect to the host first
//  - the host sends each newcomer the roster; the newcomer connects to everyone on it
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rtc { class WebSocket; class PeerConnection; class DataChannel; }

namespace smash {

struct ServerConfig {
    std::string host = "0.peerjs.com";
    int port = 443;
    bool secure = true;
    std::string key = "peerjs";
    std::string caFile;
};

enum class LinkState { Idle, Connecting, Waiting, Linking, Connected, Error };

struct PeerInfo {
    std::string id, name;
    int slot = -1;     // stable 0..2 while the person is in the room
    int pingMs = -1;
    bool connected = false;
};

struct Event {
    enum class Type { State, Chat, PeerJoined, PeerLeft, Ping, FileStart, FileProgress, FileDone, FileFailed, Log };
    Type type;
    std::string text;      // chat text / log / error / file name / peer name
    std::string from;      // chat author / file sender name
    bool incoming = false; // file direction
    int fileId = 0;        // outgoing: group id (one per file, all recipients); incoming: transfer id
    int peer = -1;         // slot of the person this is about (-1: none / me)
    int recipients = 0;    // outgoing FileStart: how many people the file goes to
    uint64_t bytes = 0, total = 0;
    std::string path;
    LinkState state = LinkState::Idle;
    int pingMs = 0;
};

class LinkEngine {
public:
    static constexpr int kMaxPeers = 3; // 4 people including you

    LinkEngine(ServerConfig cfg, std::string downloadDir);
    ~LinkEngine();

    void host(const std::string& displayName);
    void join(const std::string& code, const std::string& displayName);
    void leave();

    void sendChat(const std::string& text); // to everyone
    // Queue a file for everyone (toSlot = -1) or one person. `relName` = name/path the receiver sees.
    // Returns the group id (0 on error).
    int sendFile(const std::string& utf8Path, const std::string& relName = {}, int toSlot = -1);
    void setDownloadDir(const std::string& dir);

    // Real-time media ("audio": unreliable, "video": reliable+ordered), per person.
    using MediaHandler = std::function<void(int slot, const uint8_t* data, size_t size)>;
    void setMediaHandler(const std::string& name, MediaHandler h);
    bool sendMedia(const std::string& name, const void* data, size_t size);              // everyone
    bool sendMediaTo(int slot, const std::string& name, const void* data, size_t size);  // one person
    size_t mediaBufferedTo(int slot, const std::string& name) const;
    bool mediaReady(const std::string& name) const; // open to at least one person

    std::vector<PeerInfo> peers() const;   // connected people (not you)
    int peerCount() const;
    std::string peerNameForSlot(int slot) const;

    std::vector<Event> poll();
    LinkState state() const { return state_.load(); }
    std::string roomCode() const;
    bool isHost() const;

    static std::string normalizeCode(const std::string& in);
    static std::string prettyCode(const std::string& code);

private:
    struct Peer;
    struct Transfer;
    void push(Event e);
    void setState(LinkState s, const std::string& msg = {});
    void log(const std::string& s);
    void connectSignaling(const std::string& myId);
    void onSignal(const std::string& msg);
    void sendSignal(const std::string& type, const std::string& dst, const std::string& payloadJson);
    std::shared_ptr<Peer> createPeer(const std::string& remoteId, bool initiator, const std::string& connId);
    void removePeer(const std::string& remoteId, const std::string& reason, bool notify);
    void flushGraveyard();
    void setupControl(std::shared_ptr<Peer> p, std::shared_ptr<rtc::DataChannel> dc);
    void onControl(std::shared_ptr<Peer> p, const std::string& msg);
    void setupMedia(std::shared_ptr<Peer> p, std::shared_ptr<rtc::DataChannel> dc);
    void setupIncomingFile(std::shared_ptr<Peer> p, std::shared_ptr<rtc::DataChannel> dc);
    void pumpSend(std::shared_ptr<Transfer> t); void pumpOnce(std::shared_ptr<Transfer> t);
    void startQueued();
    void failTransfersOf(const std::string& peerId);
    void teardown();
    void heartbeatLoop();
    void updateConnectedState();
    int freeSlot() const; // call with mtx_ held
    std::shared_ptr<Peer> peerBySlot(int slot) const; // call with mtx_ held

    ServerConfig cfg_;
    std::string downloadDir_;
    std::atomic<LinkState> state_ { LinkState::Idle };

    mutable std::mutex mtx_;
    std::deque<Event> events_;
    std::string code_, myId_, hostId_, myName_;
    bool isHost_ = false;
    std::shared_ptr<rtc::WebSocket> ws_;
    std::map<std::string, std::shared_ptr<Peer>> peers_;
    std::map<int, std::shared_ptr<Transfer>> transfers_;
    std::deque<std::shared_ptr<Transfer>> sendQueue_;
    std::map<std::string, std::shared_ptr<MediaHandler>> mediaHandlers_;
    std::atomic<int> nextFileId_ { 1 };
    uint64_t generation_ = 0;
    static constexpr int kMaxActiveSendsPerPeer = 4;

    std::atomic<bool> pendingFull_ { false };
    std::mutex graveMtx_; std::vector<std::shared_ptr<Peer>> graveyard_;
    std::deque<std::function<void()>> tasks_;
    void post(std::function<void()> fn);
    std::thread hb_;
    std::mutex hbMtx_;
    std::condition_variable hbCv_;
    bool hbStop_ = false;
};

const char* stateName(LinkState s);

} // namespace smash
