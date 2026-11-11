// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#include "LinkEngine.h"

#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace smash {

// ---------------------------------------------------------------- utilities
namespace {
constexpr const char* kIdPrefix = "smashlink-";
constexpr const char* kCodeAlphabet = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
constexpr size_t kChunk = 64 * 1024;
constexpr size_t kHighWater = 4 * 1024 * 1024;
constexpr size_t kLowWater = 1 * 1024 * 1024;

std::string randomString(size_t n, const char* alphabet) {
    static thread_local std::mt19937_64 rng { std::random_device {}() ^ (uint64_t) Clock::now().time_since_epoch().count() };
    std::string a(alphabet), s;
    std::uniform_int_distribution<size_t> d(0, a.size() - 1);
    for (size_t i = 0; i < n; ++i) s += a[d(rng)];
    return s;
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

fs::path u8(const std::string& s) { return fs::path(reinterpret_cast<const char8_t*>(s.c_str())); }

std::string toU8(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string sanitizeName(std::string n) {
    for (auto& c : n)
        if ((unsigned char) c < 32 || std::string("/\\:*?\"<>|").find(c) != std::string::npos) c = '_';
    while (!n.empty() && (n[0] == '.' || n[0] == ' ')) n.erase(0, 1);
    while (!n.empty() && (n.back() == '.' || n.back() == ' ')) n.pop_back();
    if (n.size() > 150) n = n.substr(n.size() - 150);
    return n.empty() ? "file" : n;
}

fs::path uniquePath(const fs::path& dir, const std::string& name) {
    fs::path p = dir / u8(name);
    if (!fs::exists(p)) return p;
    auto stem = p.stem().u8string(), ext = p.extension().u8string();
    for (int i = 1; i < 10000; ++i) {
        auto cand = dir / fs::path(stem + u8" (" + std::u8string((const char8_t*) std::to_string(i).c_str()) + u8")" + ext);
        if (!fs::exists(cand)) return cand;
    }
    return p;
}

std::mutex gLogMtx;
std::string gLogPath;
int gEngines = 0;

void fileLog(const std::string& line) {
    std::lock_guard<std::mutex> l(gLogMtx);
    if (gLogPath.empty()) return;
    if (std::getenv("SMASHLINK_DEBUG")) { std::printf("  %s\n", line.c_str()); std::fflush(stdout); }
    std::ofstream f(u8(gLogPath), std::ios::app);
    auto t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%H:%M:%S", std::localtime(&t));
    f << buf << "  " << line << "\n";
}

std::string newConnId() { return "dc_" + randomString(10, "abcdefghijklmnopqrstuvwxyz0123456789"); }
} // namespace

const char* stateName(LinkState s) {
    switch (s) {
        case LinkState::Idle: return "Offline";
        case LinkState::Connecting: return "Connecting";
        case LinkState::Waiting: return "Waiting";
        case LinkState::Linking: return "Linking";
        case LinkState::Connected: return "Connected";
        case LinkState::Error: return "Error";
    }
    return "?";
}

struct LinkEngine::Peer {
    std::string id, name, connId;
    int slot = -1;
    bool initiator = false;
    bool rejected = false;  // host side: room was full
    bool announced = false; // PeerJoined was emitted
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> ctl;
    std::map<std::string, std::shared_ptr<rtc::DataChannel>> media;
    std::atomic<bool> connected { false };
    std::atomic<bool> greeted { false };
    std::atomic<int> pingMs { -1 };
};

struct LinkEngine::Transfer {
    int id = 0, groupId = 0, remoteId = 0, slot = -1;
    std::string peerId;
    bool started = false, incoming = false, finished = false;
    std::string relName, name;
    fs::path path;
    uint64_t size = 0, done = 0;
    std::shared_ptr<rtc::DataChannel> dc;
    std::ifstream in;
    std::ofstream out;
    std::recursive_mutex m; // dc->send() may call back into pumpSend synchronously
    int64_t lastReport = 0;
    std::atomic<bool> pumping { false }, pumpAgain { false };
};

// ---------------------------------------------------------------- lifecycle
LinkEngine::LinkEngine(ServerConfig cfg, std::string downloadDir) : cfg_(std::move(cfg)), downloadDir_(std::move(downloadDir)) {
    {
        std::lock_guard<std::mutex> l(gLogMtx);
        if (gEngines++ == 0) {
            std::error_code ec;
            fs::create_directories(u8(downloadDir_), ec);
            gLogPath = toU8(u8(downloadDir_).parent_path() / "smashlink.log");
            rtc::InitLogger(std::getenv("SMASHLINK_DEBUG") ? rtc::LogLevel::Debug : rtc::LogLevel::Warning,
                            [](rtc::LogLevel, std::string m) { fileLog("[rtc] " + m); });
        }
    }
    hb_ = std::thread([this] { heartbeatLoop(); });
}

LinkEngine::~LinkEngine() {
    {
        std::lock_guard<std::mutex> l(hbMtx_);
        hbStop_ = true;
    }
    hbCv_.notify_all();
    if (hb_.joinable()) hb_.join();
    teardown();
    flushGraveyard();
    std::lock_guard<std::mutex> l(gLogMtx);
    if (--gEngines == 0) {
        auto f = rtc::Cleanup();
        f.wait_for(std::chrono::seconds(3));
    }
}

void LinkEngine::push(Event e) {
    std::lock_guard<std::mutex> l(mtx_);
    if (events_.size() > 5000) events_.pop_front();
    events_.push_back(std::move(e));
}

std::vector<Event> LinkEngine::poll() {
    std::lock_guard<std::mutex> l(mtx_);
    std::vector<Event> v(events_.begin(), events_.end());
    events_.clear();
    return v;
}

void LinkEngine::log(const std::string& s) {
    fileLog(s);
    Event e { Event::Type::Log };
    e.text = s;
    push(e);
}

void LinkEngine::setState(LinkState s, const std::string& msg) {
    state_ = s;
    Event e { Event::Type::State };
    e.state = s;
    e.text = msg;
    push(e);
    fileLog(std::string("state -> ") + stateName(s) + (msg.empty() ? "" : " : " + msg));
}

std::string LinkEngine::roomCode() const {
    std::lock_guard<std::mutex> l(mtx_);
    return code_;
}

bool LinkEngine::isHost() const {
    std::lock_guard<std::mutex> l(mtx_);
    return isHost_;
}

std::string LinkEngine::normalizeCode(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c >= 'a' && c <= 'z') c = char(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) out += c;
    }
    return out;
}

std::string LinkEngine::prettyCode(const std::string& c) { return c.size() == 6 ? c.substr(0, 3) + "-" + c.substr(3) : c; }

std::vector<PeerInfo> LinkEngine::peers() const {
    std::lock_guard<std::mutex> l(mtx_);
    std::vector<PeerInfo> v;
    for (auto& [id, p] : peers_)
        if (p->connected.load() && !p->rejected && p->slot >= 0) v.push_back({ id, p->name, p->slot, p->pingMs.load(), true });
    std::sort(v.begin(), v.end(), [](const PeerInfo& a, const PeerInfo& b) { return a.slot < b.slot; });
    return v;
}

int LinkEngine::peerCount() const { return (int) peers().size(); }

std::string LinkEngine::peerNameForSlot(int slot) const {
    std::lock_guard<std::mutex> l(mtx_);
    auto p = peerBySlot(slot);
    return p ? p->name : std::string();
}

int LinkEngine::freeSlot() const {
    std::set<int> used;
    for (auto& [id, p] : peers_)
        if (p->slot >= 0) used.insert(p->slot);
    for (int s = 0; s < kMaxPeers; ++s)
        if (!used.count(s)) return s;
    return -1;
}

std::shared_ptr<LinkEngine::Peer> LinkEngine::peerBySlot(int slot) const {
    for (auto& [id, p] : peers_)
        if (p->slot == slot && slot >= 0) return p;
    return nullptr;
}

// ---------------------------------------------------------------- session control
void LinkEngine::host(const std::string& name) {
    teardown();
    std::string id;
    {
        std::lock_guard<std::mutex> l(mtx_);
        isHost_ = true;
        myName_ = name;
        code_ = randomString(6, kCodeAlphabet);
        myId_ = hostId_ = id = kIdPrefix + code_;
    }
    setState(LinkState::Connecting, "Connecting to signaling server");
    connectSignaling(id);
}

void LinkEngine::join(const std::string& rawCode, const std::string& name) {
    auto code = normalizeCode(rawCode);
    if (code.size() != 6) {
        setState(LinkState::Error, "Code must be 6 characters");
        return;
    }
    teardown();
    std::string id;
    {
        std::lock_guard<std::mutex> l(mtx_);
        isHost_ = false;
        myName_ = name;
        code_ = code;
        hostId_ = kIdPrefix + code;
        myId_ = id = std::string(kIdPrefix) + "g-" + randomString(10, "abcdefghijkmnpqrstuvwxyz23456789");
    }
    setState(LinkState::Connecting, "Connecting to signaling server");
    connectSignaling(id);
}

void LinkEngine::leave() {
    teardown();
    setState(LinkState::Idle);
}

void LinkEngine::teardown() {
    std::shared_ptr<rtc::WebSocket> ws;
    std::map<std::string, std::shared_ptr<Peer>> ps;
    std::map<int, std::shared_ptr<Transfer>> tr;
    {
        std::lock_guard<std::mutex> l(mtx_);
        ++generation_;
        ws.swap(ws_);
        ps.swap(peers_);
        tr.swap(transfers_);
        sendQueue_.clear();
        pendingFull_ = false;
    }
    {
        std::lock_guard<std::mutex> l(hbMtx_);
        if (std::this_thread::get_id() != hb_.get_id()) tasks_.clear();
    }
    for (auto& [id, t] : tr) {
        std::lock_guard<std::recursive_mutex> l(t->m);
        bool wasActive = !t->finished;
        t->finished = true;
        if (t->dc) { t->dc->resetCallbacks(); t->dc->close(); }
        if (t->out.is_open()) t->out.close();
        if (wasActive && t->incoming) { std::error_code ec; fs::remove(t->path, ec); }
        if (wasActive) {
            Event e { Event::Type::FileFailed };
            e.fileId = t->incoming ? t->id : t->groupId;
            e.text = t->name; e.incoming = t->incoming; e.peer = t->slot;
            push(e);
        }
    }
    try {
        for (auto& [id, p] : ps) {
            for (auto& [n, m] : p->media) { m->resetCallbacks(); m->close(); }
            if (p->ctl) { p->ctl->resetCallbacks(); p->ctl->close(); }
            if (p->pc) { p->pc->resetCallbacks(); p->pc->close(); }
        }
        if (ws) { ws->resetCallbacks(); ws->close(); }
    } catch (...) {}
}

void LinkEngine::flushGraveyard() {
    std::vector<std::shared_ptr<Peer>> g;
    {
        std::lock_guard<std::mutex> l(graveMtx_);
        g.swap(graveyard_);
    }
    try {
        for (auto& p : g) {
            for (auto& [n, m] : p->media) { m->resetCallbacks(); m->close(); }
            if (p->ctl) { p->ctl->resetCallbacks(); p->ctl->close(); }
            if (p->pc) { p->pc->resetCallbacks(); p->pc->close(); }
        }
    } catch (...) {}
}

// ---------------------------------------------------------------- signaling (PeerJS protocol)
void LinkEngine::connectSignaling(const std::string& myId) {
    rtc::WebSocketConfiguration wc;
    if (!cfg_.caFile.empty()) wc.caCertificatePemFile = cfg_.caFile;
    wc.connectionTimeout = std::chrono::milliseconds(10000);
    auto ws = std::make_shared<rtc::WebSocket>(wc);
    uint64_t gen;
    {
        std::lock_guard<std::mutex> l(mtx_);
        ws_ = ws;
        gen = generation_;
    }
    auto alive = [this, gen] { std::lock_guard<std::mutex> l(mtx_); return gen == generation_; };
    ws->onOpen([this, alive] { if (alive()) log("signaling socket open"); });
    ws->onError([this, alive](std::string err) {
        if (alive() && state_ != LinkState::Connected) setState(LinkState::Error, "Signaling error: " + err);
    });
    ws->onClosed([this, alive] {
        if (!alive()) return;
        auto s = state_.load();
        if (s == LinkState::Connecting || s == LinkState::Waiting || s == LinkState::Linking)
            setState(LinkState::Error, "Signaling server disconnected");
        else
            log("signaling closed (people already linked stay linked)");
    });
    ws->onMessage([this, alive](rtc::message_variant m) {
        if (!alive()) return;
        // handled on our own thread: creating PeerConnections inside an RTC callback can starve
        // libdatachannel's worker pool (every worker ends up waiting for another one)
        if (auto s = std::get_if<std::string>(&m)) post([this, alive, msg = *s] { if (alive()) onSignal(msg); });
    });
    std::string url = std::string(cfg_.secure ? "wss://" : "ws://") + cfg_.host + ":" + std::to_string(cfg_.port) + "/peerjs?key=" + cfg_.key
                      + "&id=" + myId + "&token=" + randomString(12, "abcdefghijklmnopqrstuvwxyz0123456789") + "&version=1.5.4";
    log("connecting " + url.substr(0, url.find("&token")));
    try {
        ws->open(url);
    } catch (const std::exception& e) {
        setState(LinkState::Error, std::string("Cannot open socket: ") + e.what());
    }
}

void LinkEngine::sendSignal(const std::string& type, const std::string& dst, const std::string& payloadJson) {
    std::shared_ptr<rtc::WebSocket> ws;
    {
        std::lock_guard<std::mutex> l(mtx_);
        ws = ws_;
    }
    if (!ws || !ws->isOpen()) return;
    json j;
    j["type"] = type;
    j["dst"] = dst;
    j["payload"] = json::parse(payloadJson);
    fileLog("signal -> " + type + " " + dst);
    try { ws->send(j.dump()); } catch (const std::exception& e) { fileLog(std::string("signal send failed: ") + e.what()); }
}

void LinkEngine::onSignal(const std::string& msg) {
    json j;
    try { j = json::parse(msg); } catch (...) { return; }
    const std::string type = j.value("type", "");
    const std::string src = j.value("src", "");
    fileLog("signal <- " + type + (src.empty() ? "" : " " + src));

    bool host;
    std::string hostId, myId;
    {
        std::lock_guard<std::mutex> l(mtx_);
        host = isHost_;
        hostId = hostId_;
        myId = myId_;
    }
    try {
        if (type == "OPEN") {
            if (host) {
                setState(LinkState::Waiting, "Share the code - up to 3 people can join");
            } else {
                setState(LinkState::Linking, "Looking for the room");
                createPeer(hostId, true, newConnId());
            }
        } else if (type == "ID-TAKEN") {
            setState(LinkState::Error, "That code is busy, press HOST again");
        } else if (type == "ERROR" || type == "INVALID-KEY") {
            setState(LinkState::Error, "Server: " + j["payload"].value("msg", type));
        } else if (type == "OFFER") {
            std::shared_ptr<Peer> existing;
            int count = 0;
            {
                std::lock_guard<std::mutex> l(mtx_);
                auto it = peers_.find(src);
                if (it != peers_.end()) existing = it->second;
                count = (int) peers_.size();
            }
            if (existing) {
                if (existing->connected.load()) return;
                // glare (both sides offered at once): the lower id keeps its own offer
                if (existing->initiator && myId < src) return;
                removePeer(src, "", false);
                count--;
            }
            if (!host && count >= kMaxPeers) return;
            auto p = createPeer(src, false, j["payload"].value("connectionId", newConnId()));
            auto& sdp = j["payload"]["sdp"];
            p->pc->setRemoteDescription(rtc::Description(sdp.value("sdp", ""), sdp.value("type", "offer")));
            if (host && state_ == LinkState::Waiting) setState(LinkState::Linking, "Someone is joining");
        } else if (type == "ANSWER" || type == "CANDIDATE") {
            std::shared_ptr<Peer> p;
            {
                std::lock_guard<std::mutex> l(mtx_);
                auto it = peers_.find(src);
                if (it != peers_.end()) p = it->second;
            }
            if (!p) return;
            if (type == "ANSWER") {
                auto& sdp = j["payload"]["sdp"];
                p->pc->setRemoteDescription(rtc::Description(sdp.value("sdp", ""), sdp.value("type", "answer")));
            } else {
                auto& c = j["payload"]["candidate"];
                p->pc->addRemoteCandidate(rtc::Candidate(c.value("candidate", ""), c.value("sdpMid", "0")));
            }
        } else if (type == "EXPIRE") {
            if (!host && src == hostId && peerCount() == 0) setState(LinkState::Error, "Room not found - check the code");
            else removePeer(src, "", true);
        }
    } catch (const std::exception& e) {
        log(std::string("signal handling error: ") + e.what());
    }
}

// ---------------------------------------------------------------- peers
std::shared_ptr<LinkEngine::Peer> LinkEngine::createPeer(const std::string& remoteId, bool initiator, const std::string& connId) {
    rtc::Configuration c;
    c.iceServers.emplace_back("stun:stun.l.google.com:19302");
    c.iceServers.emplace_back("stun:stun.cloudflare.com:3478");
    c.maxMessageSize = 2 * 1024 * 1024;

    auto p = std::make_shared<Peer>();
    p->id = remoteId;
    p->connId = connId;
    p->initiator = initiator;
    p->pc = std::make_shared<rtc::PeerConnection>(c);
    uint64_t gen;
    {
        std::lock_guard<std::mutex> l(mtx_);
        p->slot = freeSlot();
        peers_[remoteId] = p;
        gen = generation_;
    }
    std::weak_ptr<Peer> wp = p;
    auto alive = [this, gen, wp, remoteId] {
        std::lock_guard<std::mutex> l(mtx_);
        auto it = peers_.find(remoteId);
        return gen == generation_ && it != peers_.end() && it->second == wp.lock();
    };

    p->pc->onLocalDescription([this, alive, wp, remoteId](rtc::Description d) {
        auto s = wp.lock();
        if (!s || !alive()) return;
        json pl; // mirror the official PeerJS client payload shape (the public server checks it)
        pl["sdp"] = { { "type", d.typeString() }, { "sdp", std::string(d) } };
        pl["type"] = "data";
        pl["connectionId"] = s->connId;
        pl["browser"] = "chrome";
        if (d.type() == rtc::Description::Type::Offer) {
            pl["label"] = s->connId;
            pl["reliable"] = true;
            pl["serialization"] = "binary";
            pl["metadata"] = nullptr;
        }
        sendSignal(d.type() == rtc::Description::Type::Offer ? "OFFER" : "ANSWER", remoteId, pl.dump());
    });
    p->pc->onLocalCandidate([this, alive, wp, remoteId](rtc::Candidate cand) {
        auto s = wp.lock();
        if (!s || !alive()) return;
        json pl;
        pl["candidate"] = { { "candidate", std::string(cand) }, { "sdpMid", cand.mid() }, { "sdpMLineIndex", 0 }, { "usernameFragment", nullptr } };
        pl["type"] = "data";
        pl["connectionId"] = s->connId;
        sendSignal("CANDIDATE", remoteId, pl.dump());
    });
    p->pc->onStateChange([this, alive, remoteId](rtc::PeerConnection::State st) {
        if (!alive()) return;
        using S = rtc::PeerConnection::State;
        if (st == S::Failed) removePeer(remoteId, "Could not link (firewall/NAT)", true);
        else if (st == S::Disconnected || st == S::Closed) removePeer(remoteId, "left", true);
    });
    p->pc->onDataChannel([this, alive, wp](std::shared_ptr<rtc::DataChannel> dc) {
        auto s = wp.lock();
        if (!s || !alive()) return;
        const auto label = dc->label();
        if (label == "ctl") setupControl(s, dc);
        else if (label.rfind("m:", 0) == 0) setupMedia(s, dc);
        else if (label.rfind("f|", 0) == 0) setupIncomingFile(s, dc);
    });

    if (initiator) {
        setupControl(p, p->pc->createDataChannel("ctl"));
        rtc::DataChannelInit mi; // audio: unreliable, unordered (late audio is useless)
        mi.reliability.unordered = true;
        mi.reliability.maxRetransmits = 0;
        setupMedia(p, p->pc->createDataChannel("m:audio", mi));
        setupMedia(p, p->pc->createDataChannel("m:video")); // reliable + ordered (VP8 reference chain)
    }
    return p;
}

void LinkEngine::removePeer(const std::string& remoteId, const std::string& reason, bool notify) {
    std::shared_ptr<Peer> p;
    {
        std::lock_guard<std::mutex> l(mtx_);
        auto it = peers_.find(remoteId);
        if (it == peers_.end()) return;
        p = it->second;
        peers_.erase(it);
    }
    p->connected = false;
    {
        // closing a connection from inside its own callback isn't safe: the heartbeat thread does it
        std::lock_guard<std::mutex> l(graveMtx_);
        graveyard_.push_back(p);
    }
    failTransfersOf(remoteId);
    if (notify && p->announced) {
        Event e { Event::Type::PeerLeft };
        e.peer = p->slot;
        e.text = p->name;
        e.from = reason;
        push(e);
    } else if (notify && reason.rfind("Could not", 0) == 0) {
        log(reason + " with " + remoteId);
    }
    updateConnectedState();
}

void LinkEngine::updateConnectedState() {
    int n = 0;
    bool host;
    {
        std::lock_guard<std::mutex> l(mtx_);
        host = isHost_;
        for (auto& [id, p] : peers_)
            if (p->connected.load() && !p->rejected) ++n;
    }
    const auto s = state_.load();
    if (n > 0) {
        if (s != LinkState::Connected) setState(LinkState::Connected, "Linked");
    } else if (host) {
        if (s == LinkState::Connected || s == LinkState::Linking) setState(LinkState::Waiting, "Everyone left - waiting");
    } else if (s == LinkState::Connected) {
        setState(LinkState::Error, "Connection lost");
    }
}

void LinkEngine::setupControl(std::shared_ptr<Peer> p, std::shared_ptr<rtc::DataChannel> dc) {
    uint64_t gen;
    {
        std::lock_guard<std::mutex> l(mtx_);
        p->ctl = dc;
        gen = generation_;
    }
    std::weak_ptr<Peer> wp = p;
    std::weak_ptr<rtc::DataChannel> wdc = dc;
    auto alive = [this, gen, wp] {
        auto s = wp.lock();
        std::lock_guard<std::mutex> l(mtx_);
        if (!s || gen != generation_) return false;
        auto it = peers_.find(s->id);
        return it != peers_.end() && it->second == s;
    };
    auto greet = [this, alive, wp, wdc] {
        auto s = wp.lock();
        auto d = wdc.lock();
        if (!s || !d || !alive() || s->greeted.exchange(true)) return;
        std::string name;
        bool host;
        std::vector<std::string> roster;
        int others = 0;
        {
            std::lock_guard<std::mutex> l(mtx_);
            name = myName_;
            host = isHost_;
            for (auto& [id, q] : peers_)
                if (q != s && q->connected.load() && !q->rejected) { roster.push_back(id); ++others; }
        }
        d->send(json { { "t", "hello" }, { "name", name }, { "ver", 3 } }.dump());
        if (host) {
            if (others >= kMaxPeers || s->slot < 0) {
                s->rejected = true;
                d->send(json { { "t", "full" } }.dump());
                return;
            }
            d->send(json { { "t", "roster" }, { "peers", roster } }.dump());
        }
        s->connected = true;
        updateConnectedState();
    };
    dc->onOpen(greet);
    dc->onMessage([this, alive, wp](rtc::message_variant m) {
        auto s = wp.lock();
        if (!s || !alive()) return;
        if (auto str = std::get_if<std::string>(&m)) onControl(s, *str);
    });
    if (dc->isOpen()) greet();
}

void LinkEngine::onControl(std::shared_ptr<Peer> p, const std::string& msg) {
    json j;
    try { j = json::parse(msg); } catch (...) { return; }
    const auto t = j.value("t", "");
    if (t == "hello") {
        p->name = j.value("name", "Collaborator");
        if (!p->rejected && !p->announced) {
            p->announced = true;
            Event e { Event::Type::PeerJoined };
            e.text = p->name;
            e.peer = p->slot;
            push(e);
        }
    } else if (t == "roster") {
        // from the host: connect to everyone else in the room
        std::string myId;
        {
            std::lock_guard<std::mutex> l(mtx_);
            myId = myId_;
        }
        for (auto& id : j.value("peers", std::vector<std::string>())) {
            bool known;
            {
                std::lock_guard<std::mutex> l(mtx_);
                known = peers_.count(id) > 0;
            }
            if (!known && id != myId && id.rfind(kIdPrefix, 0) == 0) {
                uint64_t gen;
                {
                    std::lock_guard<std::mutex> l(mtx_);
                    gen = generation_;
                }
                post([this, id, gen] {
                    {
                        std::lock_guard<std::mutex> l(mtx_);
                        if (gen != generation_ || peers_.count(id)) return;
                    }
                    createPeer(id, true, newConnId());
                });
            }
        }
    } else if (t == "full") {
        pendingFull_ = true; // the heartbeat thread leaves the room
    } else if (t == "chat") {
        Event e { Event::Type::Chat };
        e.text = j.value("text", "");
        e.from = p->name;
        e.peer = p->slot;
        e.incoming = true;
        push(e);
    } else if (t == "ping") {
        if (p->ctl && p->ctl->isOpen()) p->ctl->send(json { { "t", "pong" }, { "ts", j.value("ts", (int64_t) 0) } }.dump());
    } else if (t == "pong") {
        p->pingMs = (int) (nowMs() - j.value("ts", nowMs()));
        Event e { Event::Type::Ping };
        e.pingMs = p->pingMs.load();
        e.peer = p->slot;
        push(e);
    } else if (t == "file_ok" || t == "file_fail") {
        const int id = j.value("id", 0);
        std::shared_ptr<Transfer> tr;
        {
            std::lock_guard<std::mutex> l(mtx_);
            auto it = transfers_.find(id);
            if (it != transfers_.end() && it->second->peerId == p->id) { tr = it->second; transfers_.erase(it); }
        }
        if (!tr) return;
        {
            std::lock_guard<std::recursive_mutex> l(tr->m);
            tr->finished = true;
            if (tr->in.is_open()) tr->in.close();
            Event e { t == "file_ok" ? Event::Type::FileDone : Event::Type::FileFailed };
            e.fileId = tr->groupId; e.text = tr->name; e.bytes = e.total = tr->size; e.path = toU8(tr->path); e.peer = tr->slot;
            push(e);
            if (tr->dc) tr->dc->close();
        }
        post([this] { startQueued(); }); // creates data channels: not inside an RTC callback
    }
}

void LinkEngine::sendChat(const std::string& text) {
    if (text.empty()) return;
    std::vector<std::shared_ptr<rtc::DataChannel>> chans;
    std::string me;
    {
        std::lock_guard<std::mutex> l(mtx_);
        me = myName_;
        for (auto& [id, p] : peers_)
            if (p->connected.load() && !p->rejected && p->ctl) chans.push_back(p->ctl);
    }
    if (chans.empty()) return;
    auto t = text.substr(0, 4000);
    const auto payload = json { { "t", "chat" }, { "text", t } }.dump();
    for (auto& c : chans)
        try { if (c->isOpen()) c->send(payload); } catch (...) {}
    Event e { Event::Type::Chat };
    e.text = t;
    e.from = me;
    push(e);
}

// ---------------------------------------------------------------- media
void LinkEngine::setupMedia(std::shared_ptr<Peer> p, std::shared_ptr<rtc::DataChannel> dc) {
    auto name = dc->label().substr(2);
    uint64_t gen;
    {
        std::lock_guard<std::mutex> l(mtx_);
        p->media[name] = dc;
        gen = generation_;
    }
    std::weak_ptr<Peer> wp = p;
    dc->onMessage([this, name, gen, wp](rtc::message_variant m) {
        auto b = std::get_if<rtc::binary>(&m);
        auto s = wp.lock();
        if (!b || !s) return;
        std::shared_ptr<MediaHandler> h;
        {
            std::lock_guard<std::mutex> l(mtx_);
            if (gen != generation_ || s->rejected || s->slot < 0) return;
            auto it = mediaHandlers_.find(name);
            if (it != mediaHandlers_.end()) h = it->second;
        }
        if (h && *h) (*h)(s->slot, reinterpret_cast<const uint8_t*>(b->data()), b->size());
    });
}

void LinkEngine::setMediaHandler(const std::string& name, MediaHandler h) {
    std::lock_guard<std::mutex> l(mtx_);
    mediaHandlers_[name] = std::make_shared<MediaHandler>(std::move(h));
}

bool LinkEngine::mediaReady(const std::string& name) const {
    std::lock_guard<std::mutex> l(mtx_);
    for (auto& [id, p] : peers_) {
        auto it = p->media.find(name);
        if (p->connected.load() && !p->rejected && it != p->media.end() && it->second->isOpen()) return true;
    }
    return false;
}

size_t LinkEngine::mediaBufferedTo(int slot, const std::string& name) const {
    std::lock_guard<std::mutex> l(mtx_);
    auto p = peerBySlot(slot);
    if (!p) return 0;
    auto it = p->media.find(name);
    if (it == p->media.end()) return 0;
    try { return it->second->bufferedAmount(); } catch (...) { return 0; }
}

bool LinkEngine::sendMediaTo(int slot, const std::string& name, const void* data, size_t size) {
    std::shared_ptr<rtc::DataChannel> dc;
    {
        std::lock_guard<std::mutex> l(mtx_);
        auto p = peerBySlot(slot);
        if (!p || !p->connected.load() || p->rejected) return false;
        auto it = p->media.find(name);
        if (it == p->media.end()) return false;
        dc = it->second;
    }
    try {
        if (!dc->isOpen()) return false;
        // congestion: drop instead of queueing (real-time data is useless when late)
        if (dc->bufferedAmount() > (name == "video" ? 4u * 1024 * 1024 : 64u * 1024)) return false;
        return dc->send(reinterpret_cast<const std::byte*>(data), size);
    } catch (...) {
        return false;
    }
}

bool LinkEngine::sendMedia(const std::string& name, const void* data, size_t size) {
    std::vector<int> slots;
    {
        std::lock_guard<std::mutex> l(mtx_);
        for (auto& [id, p] : peers_)
            if (p->connected.load() && !p->rejected && p->slot >= 0) slots.push_back(p->slot);
    }
    bool any = false;
    for (int s : slots) any |= sendMediaTo(s, name, data, size);
    return any;
}

// ---------------------------------------------------------------- file transfer
void LinkEngine::setDownloadDir(const std::string& dir) {
    std::lock_guard<std::mutex> l(mtx_);
    downloadDir_ = dir;
}

int LinkEngine::sendFile(const std::string& utf8Path, const std::string& relName, int toSlot) {
    if (state_ != LinkState::Connected) return 0;
    std::error_code ec;
    const fs::path path = u8(utf8Path);
    if (!fs::is_regular_file(path, ec)) return 0;
    const uint64_t size = fs::file_size(path, ec);
    if (ec) return 0;
    const std::string rel = relName.empty() ? toU8(path.filename()) : relName;
    const int group = nextFileId_++;
    int n = 0;
    {
        std::lock_guard<std::mutex> l(mtx_);
        for (auto& [id, p] : peers_) {
            if (!p->connected.load() || p->rejected || p->slot < 0) continue;
            if (toSlot >= 0 && p->slot != toSlot) continue;
            auto t = std::make_shared<Transfer>();
            t->path = path; t->size = size; t->relName = t->name = rel;
            t->id = nextFileId_++; t->groupId = group; t->peerId = id; t->slot = p->slot;
            transfers_[t->id] = t;
            sendQueue_.push_back(t);
            ++n;
        }
    }
    if (n == 0) return 0;
    Event e { Event::Type::FileStart };
    e.fileId = group; e.text = rel; e.total = size; e.incoming = false; e.path = utf8Path; e.recipients = n; e.peer = toSlot;
    push(e);
    startQueued();
    return group;
}

void LinkEngine::startQueued() {
    for (;;) {
        std::shared_ptr<Transfer> t;
        std::shared_ptr<rtc::PeerConnection> pc;
        {
            std::lock_guard<std::mutex> l(mtx_);
            std::map<std::string, int> active;
            for (auto& [id, tr] : transfers_)
                if (!tr->incoming && tr->started && !tr->finished) active[tr->peerId]++;
            for (auto it = sendQueue_.begin(); it != sendQueue_.end(); ++it) {
                if (active[(*it)->peerId] >= kMaxActiveSendsPerPeer) continue;
                auto pit = peers_.find((*it)->peerId);
                if (pit == peers_.end() || !pit->second->pc) continue;
                t = *it;
                pc = pit->second->pc;
                sendQueue_.erase(it);
                break;
            }
            if (!t) return;
        }
        std::lock_guard<std::recursive_mutex> tl(t->m);
        if (t->finished) continue;
        t->started = true;
        t->in.open(t->path, std::ios::binary);
        bool ok = (bool) t->in;
        if (ok) {
            std::string label = "f|" + std::to_string(t->id) + "|" + std::to_string(t->size) + "|" + t->relName;
            rtc::DataChannelInit init;
            init.reliability.unordered = false;
            try {
                t->dc = pc->createDataChannel(label, init);
            } catch (const std::exception& ex) {
                log(std::string("createDataChannel failed: ") + ex.what());
                ok = false;
            }
        }
        if (!ok) {
            t->finished = true;
            {
                std::lock_guard<std::mutex> l(mtx_);
                transfers_.erase(t->id);
            }
            Event e { Event::Type::FileFailed };
            e.fileId = t->groupId; e.text = t->name; e.peer = t->slot;
            push(e);
            continue;
        }
        std::weak_ptr<Transfer> wt = t;
        t->dc->setBufferedAmountLowThreshold(kLowWater);
        t->dc->onOpen([this, wt] { if (auto s = wt.lock()) pumpSend(s); });
        t->dc->onBufferedAmountLow([this, wt] { if (auto s = wt.lock()) pumpSend(s); });
    }
}

void LinkEngine::pumpSend(std::shared_ptr<Transfer> t) {
    // dc->send() can fire onBufferedAmountLow synchronously: never pump re-entrantly (it would
    // reorder chunks), just ask the running pump to go round once more
    t->pumpAgain = true;
    if (t->pumping.exchange(true)) return;
    do {
        t->pumpAgain = false;
        pumpOnce(t);
        t->pumping = false;
    } while (t->pumpAgain.load() && !t->pumping.exchange(true));
}

void LinkEngine::pumpOnce(std::shared_ptr<Transfer> t) {
    std::lock_guard<std::recursive_mutex> l(t->m);
    if (t->finished || !t->dc || !t->dc->isOpen()) return;
    try {
        while (t->done < t->size && t->dc->bufferedAmount() < kHighWater) {
            rtc::binary buf(std::min<uint64_t>(kChunk, t->size - t->done));
            t->in.read(reinterpret_cast<char*>(buf.data()), (std::streamsize) buf.size());
            auto got = (size_t) t->in.gcount();
            if (got == 0) break;
            buf.resize(got);
            t->dc->send(std::move(buf));
            t->done += got;
        }
    } catch (const std::exception& e) {
        log(std::string("send error: ") + e.what());
    }
    auto now = nowMs();
    if (now - t->lastReport > 100 || t->done >= t->size) {
        t->lastReport = now;
        Event e { Event::Type::FileProgress };
        e.fileId = t->groupId; e.bytes = t->done; e.total = t->size; e.incoming = false; e.peer = t->slot;
        push(e);
    }
}

void LinkEngine::failTransfersOf(const std::string& peerId) {
    std::vector<std::shared_ptr<Transfer>> dead;
    {
        std::lock_guard<std::mutex> l(mtx_);
        for (auto it = transfers_.begin(); it != transfers_.end();) {
            if (it->second->peerId == peerId) { dead.push_back(it->second); it = transfers_.erase(it); }
            else ++it;
        }
        sendQueue_.erase(std::remove_if(sendQueue_.begin(), sendQueue_.end(), [&](auto& t) { return t->peerId == peerId; }), sendQueue_.end());
    }
    for (auto& t : dead) {
        std::lock_guard<std::recursive_mutex> l(t->m);
        if (t->finished) continue;
        t->finished = true;
        if (t->out.is_open()) { t->out.close(); std::error_code ec; fs::remove(t->path, ec); }
        Event e { Event::Type::FileFailed };
        e.fileId = t->incoming ? t->id : t->groupId; e.text = t->name; e.incoming = t->incoming; e.peer = t->slot;
        push(e);
    }
}

void LinkEngine::setupIncomingFile(std::shared_ptr<Peer> p, std::shared_ptr<rtc::DataChannel> dc) {
    // label: f|<senderTransferId>|<size>|<relative path>
    auto label = dc->label();
    auto p1 = label.find('|', 2), p2 = label.find('|', p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos) { dc->close(); return; }
    auto t = std::make_shared<Transfer>();
    try {
        t->remoteId = std::stoi(label.substr(2, p1 - 2));
        t->size = std::stoull(label.substr(p1 + 1, p2 - p1 - 1));
    } catch (...) { dc->close(); return; }
    // sanitize every path component; drop "." / ".." / empty; cap depth: nothing escapes the folder
    std::vector<std::string> parts;
    {
        std::string rel = label.substr(p2 + 1), cur;
        for (char c : rel + "/") {
            if (c == '/' || c == '\\') {
                if (!cur.empty() && cur != "." && cur != "..") parts.push_back(sanitizeName(cur));
                cur.clear();
            } else cur += c;
        }
        if (parts.empty()) parts.push_back("file");
        if (parts.size() > 16) parts.erase(parts.begin(), parts.end() - 16);
    }
    t->incoming = true;
    t->id = nextFileId_++;
    t->dc = dc;
    t->peerId = p->id;
    t->slot = p->slot;
    std::error_code ec;
    fs::path dir;
    {
        std::lock_guard<std::mutex> l(mtx_);
        dir = u8(downloadDir_);
    }
    for (size_t i = 0; i + 1 < parts.size(); ++i) dir /= u8(parts[i]);
    fs::create_directories(dir, ec);
    t->path = uniquePath(dir, parts.back());
    for (size_t i = 0; i + 1 < parts.size(); ++i) t->name += parts[i] + "/";
    t->name += toU8(t->path.filename());
    t->out.open(t->path, std::ios::binary | std::ios::trunc);
    if (!t->out) {
        log("cannot write " + toU8(t->path));
        dc->close();
        return;
    }
    {
        std::lock_guard<std::mutex> l(mtx_);
        transfers_[t->id] = t;
    }
    Event e { Event::Type::FileStart };
    e.fileId = t->id; e.text = t->name; e.total = t->size; e.incoming = true; e.path = toU8(t->path); e.from = p->name; e.peer = p->slot;
    push(e);

    std::weak_ptr<Transfer> wt = t;
    std::weak_ptr<Peer> wp = p;
    auto finish = [this, wp](std::shared_ptr<Transfer> s) { // called with s->m held
        s->finished = true;
        s->out.close();
        {
            std::lock_guard<std::mutex> l(mtx_);
            transfers_.erase(s->id);
        }
        if (auto pp = wp.lock())
            if (pp->ctl && pp->ctl->isOpen()) pp->ctl->send(json { { "t", "file_ok" }, { "id", s->remoteId } }.dump());
        Event d { Event::Type::FileDone };
        d.fileId = s->id; d.text = s->name; d.bytes = d.total = s->size; d.incoming = true; d.path = toU8(s->path); d.peer = s->slot;
        if (auto pp = wp.lock()) d.from = pp->name;
        push(d);
    };
    if (t->size == 0) {
        std::lock_guard<std::recursive_mutex> l(t->m);
        finish(t);
        return;
    }
    dc->onMessage([this, wt, finish](rtc::message_variant m) {
        auto s = wt.lock();
        if (!s) return;
        std::lock_guard<std::recursive_mutex> l(s->m);
        if (s->finished) return;
        if (auto b = std::get_if<rtc::binary>(&m)) {
            s->out.write(reinterpret_cast<const char*>(b->data()), (std::streamsize) b->size());
            s->done += b->size();
            auto now = nowMs();
            if (now - s->lastReport > 100) {
                s->lastReport = now;
                Event e { Event::Type::FileProgress };
                e.fileId = s->id; e.bytes = s->done; e.total = s->size; e.incoming = true; e.peer = s->slot;
                push(e);
            }
            if (s->done >= s->size) finish(s);
        }
    });
    dc->onClosed([this, wt] {
        auto s = wt.lock();
        if (!s) return;
        std::lock_guard<std::recursive_mutex> l(s->m);
        if (s->finished) return;
        s->finished = true;
        s->out.close();
        std::error_code ec2;
        fs::remove(s->path, ec2);
        {
            std::lock_guard<std::mutex> l2(mtx_);
            transfers_.erase(s->id);
        }
        Event e { Event::Type::FileFailed };
        e.fileId = s->id; e.text = s->name; e.incoming = true; e.peer = s->slot;
        push(e);
    });
}

// ---------------------------------------------------------------- keepalive + housekeeping
void LinkEngine::post(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> l(hbMtx_);
        tasks_.push_back(std::move(fn));
    }
    hbCv_.notify_all();
}

void LinkEngine::heartbeatLoop() {
    int tick = 0;
    auto nextTick = Clock::now() + std::chrono::milliseconds(500);
    std::unique_lock<std::mutex> lk(hbMtx_);
    while (!hbStop_) {
        hbCv_.wait_until(lk, nextTick, [this] { return hbStop_ || !tasks_.empty(); });
        if (hbStop_) break;
        while (!tasks_.empty()) {
            auto fn = std::move(tasks_.front());
            tasks_.pop_front();
            lk.unlock();
            try { fn(); } catch (const std::exception& e) { fileLog(std::string("task error: ") + e.what()); }
            lk.lock();
            if (hbStop_) break;
        }
        if (hbStop_ || Clock::now() < nextTick) continue;
        nextTick = Clock::now() + std::chrono::milliseconds(500);
        ++tick;
        lk.unlock();
        flushGraveyard();
        if (pendingFull_.exchange(false)) {
            teardown();
            setState(LinkState::Error, "Room is full (4 people max)");
        }
        std::shared_ptr<rtc::WebSocket> ws;
        std::vector<std::shared_ptr<rtc::DataChannel>> ctls;
        {
            std::lock_guard<std::mutex> l(mtx_);
            ws = ws_;
            for (auto& [id, p] : peers_)
                if (p->connected.load() && p->ctl) ctls.push_back(p->ctl);
        }
        try {
            if (ws && ws->isOpen() && tick % 10 == 0) ws->send(std::string(R"({"type":"HEARTBEAT"})"));
            if (tick % 4 == 0)
                for (auto& c : ctls)
                    if (c->isOpen()) c->send(json { { "t", "ping" }, { "ts", nowMs() } }.dump());
        } catch (...) {}
        lk.lock();
    }
}

} // namespace smash
