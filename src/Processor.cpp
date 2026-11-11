// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#include "Processor.h"
#include "Editor.h"
#include "B2BPaths.h"
#if JUCE_MAC
#include "MacSupport.h"
#endif

using smash::Event;
using smash::LinkState;

std::shared_ptr<MidiPreview> MidiPreview::load(const juce::File& f) {
    if (!f.hasFileExtension("mid;midi") || !f.existsAsFile() || f.getSize() > 4 * 1024 * 1024) return {};
    juce::FileInputStream in(f);
    juce::MidiFile mf;
    if (!in.openedOk() || !mf.readFrom(in)) return {};
    const int tpq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 960;
    int num = 4, den = 4;
    struct Raw { double s, e; int p; };
    std::vector<Raw> raw;
    double end = 0;
    for (int t = 0; t < mf.getNumTracks(); ++t) {
        juce::MidiMessageSequence seq(*mf.getTrack(t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i) {
            auto* ev = seq.getEventPointer(i);
            auto& m = ev->message;
            if (m.isTimeSignatureMetaEvent()) m.getTimeSignatureInfo(num, den);
            if (m.isEndOfTrackMetaEvent()) end = juce::jmax(end, m.getTimeStamp());
            if (m.isNoteOn() && ev->noteOffObject) {
                raw.push_back({ m.getTimeStamp(), ev->noteOffObject->message.getTimeStamp(), m.getNoteNumber() });
                end = juce::jmax(end, ev->noteOffObject->message.getTimeStamp());
            }
        }
    }
    if (raw.empty() || end <= 0) return {};
    auto p = std::make_shared<MidiPreview>();
    const double barTicks = tpq * num * 4.0 / den;
    p->bars = juce::jmax(1, (int) std::ceil(end / barTicks - 1e-6));
    const double span = p->bars * barTicks;
    p->lo = 127; p->hi = 0;
    for (auto& r : raw) {
        p->notes.push_back({ (float) (r.s / span), (float) (r.e / span), r.p });
        p->lo = juce::jmin(p->lo, r.p);
        p->hi = juce::jmax(p->hi, r.p);
    }
    p->summary = "MIDI  " + juce::String(p->bars) + (p->bars == 1 ? " bar  " : " bars  ") + juce::String((int) raw.size()) + " notes";
    return p;
}

Processor::Processor()
    : AudioProcessor(BusesProperties()
                         .withInput("In", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Out", juce::AudioChannelSet::stereo(), true)) {
    myName = juce::SystemStats::getFullUserName().upToFirstOccurrenceOf(" ", false, false);
    if (myName.isEmpty()) myName = juce::SystemStats::getLogonName();
    if (myName.isEmpty()) myName = "Producer";

    receiveDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                     .getChildFile("b2b")
                     .getChildFile("Received");
    receiveDir.createDirectory();

    smash::ServerConfig sc;
    // optional override, e.g. SMASHLINK_SERVER=127.0.0.1:9000:ws (tests / self-hosted server)
    auto ov = juce::SystemStats::getEnvironmentVariable("SMASHLINK_SERVER", {});
    if (ov.isNotEmpty()) {
        auto parts = juce::StringArray::fromTokens(ov, ":", "");
        if (parts.size() >= 2) {
            sc.host = parts[0].toStdString();
            sc.port = parts[1].getIntValue();
            sc.secure = parts.size() < 3 || parts[2] != "ws";
        }
    }
    engine = std::make_unique<smash::LinkEngine>(sc, receiveDir.getFullPathName().toStdString());
    audioLink = std::make_unique<AudioLink>(*engine);
    engine->setMediaHandler("audio", [a = audioLink.get()](int slot, const uint8_t* d, size_t n) { a->onPacket(slot, d, n); });
    videoLink = std::make_unique<VideoLink>(*engine);
    engine->setMediaHandler("video", [v = videoLink.get()](int slot, const uint8_t* d, size_t n) { v->onPacket(slot, d, n); });
#if JUCE_USE_CAMERA
    camListener.v = videoLink.get();
#endif
    statusText = "Offline";
    startTimerHz(20);
}

Processor::~Processor() {
    stopTimer();
    engine->setMediaHandler("audio", nullptr);
    engine->setMediaHandler("video", nullptr);
    setCam(false);
    engine->leave();
    videoLink.reset();
    audioLink.reset();
    engine.reset();
}

void Processor::prepareToPlay(double sr, int block) { audioLink->prepare(sr, block); }

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals nd;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear(c, 0, buffer.getNumSamples());
    audioLink->process(buffer);
}

void Processor::setMicEnabled(bool on) {
    if (on) {
        auto err = audioLink->openMic(micName);
        if (err.isNotEmpty()) {
            audioLink->micOn = false;
            addSystem(err);
            ++stateVersion;
            return;
        }
        micName = audioLink->micDeviceName();
        audioLink->micOn = true;
    } else {
        audioLink->micOn = false;
        audioLink->closeMic();
    }
    ++stateVersion;
}

void Processor::setAppAudio(bool on) {
    auto err = audioLink->setAppAudio(on);
    if (err.isNotEmpty()) addSystem(err);
    else if (on) addSystem("APP AUDIO on: Ableton's browser preview goes out with your DAW stream (b2b must be on the master)");
    lastAppState = audioLink->appState();
    ++stateVersion;
}

void Processor::toggleRecording() {
    if (audioLink->isRecording()) {
        auto f = audioLink->stopRecording();
        lastRecording = f;
        lastRecordingSent = false;
        addSystem("Recording saved: " + f.getFileName() + " (your master + mic, everyone's voice + DAW)");
        if (autoSendRecordings && state == LinkState::Connected) {
            // give the writers a moment to flush and close the files
            juce::Timer::callAfterDelay(600, [sp = juce::WeakReference<Processor>(this)] { if (auto* p = sp.get()) p->sendRecording(); });
        } else {
            f.revealToUser();
        }
    } else {
        auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("b2b").getChildFile("Recordings")
                       .getChildFile(juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"));
        juce::StringArray names;
        for (auto& p : people) names.add(p.present ? p.name : juce::String());
        auto err = audioLink->startRecording(dir, names);
        addSystem(err.isEmpty() ? juce::String("Recording started") : err);
    }
    ++stateVersion;
}

void Processor::sendRecording() {
    if (!lastRecording.isDirectory() || state != LinkState::Connected) return;
    auto folderName = "b2b rec " + lastRecording.getFileName() + " from " + juce::File::createLegalFileName(myName);
    int n = 0;
    for (const auto& entry : juce::RangedDirectoryIterator(lastRecording, false, "*.wav", juce::File::findFiles)) {
        auto f = entry.getFile();
        auto name = f.getFileName();
        if (name.startsWith("You - ")) name = juce::File::createLegalFileName(myName) + " - " + name.substring(6);
        if (engine->sendFile(f.getFullPathName().toStdString(), (folderName + "/" + name).toStdString()) > 0) ++n;
    }
    lastRecordingSent = n > 0;
    addSystem(n > 0 ? "Sending recording (" + juce::String(n) + " WAVs)" : juce::String("Nothing to send"));
    ++stateVersion;
}

void Processor::setReceiveFolder(const juce::File& dir) {
    if (!dir.isDirectory() && !dir.createDirectory()) return;
    receiveDir = dir;
    engine->setDownloadDir(dir.getFullPathName().toStdString());
}

void Processor::setScreenShare(bool on) {
#if JUCE_MAC
    if (on && !macsupport::hasScreenAccess()) {
        macsupport::requestScreenAccess();
        addSystem("macOS: allow Screen Recording for your DAW (System Settings > Privacy & Security > Screen Recording), then restart the DAW.");
    }
#endif
    videoLink->setSending(VideoLink::Screen, on);
    ++stateVersion;
}

juce::StringArray Processor::camNames() {
#if JUCE_USE_CAMERA
    return juce::CameraDevice::getAvailableDevices();
#else
    return {};
#endif
}

void Processor::setCam(bool on) {
#if JUCE_USE_CAMERA
    if (camera) {
        camera->removeListener(&camListener);
        camera.reset();
    }
#if JUCE_MAC
    if (on && !macsupport::hostDeclares("NSCameraUsageDescription")) {
        addSystem("Your DAW doesn't allow camera access on macOS, so CAM can't work inside it.");
        on = false;
    }
#endif
    if (on) {
        auto names = camNames();
        int idx = juce::jmax(0, names.indexOf(camName));
        if (names.isEmpty()) {
            addSystem("No camera found");
            on = false;
        } else {
            camera.reset(juce::CameraDevice::openDevice(idx, 320, 240, 1280, 720, false));
            if (!camera) {
                addSystem("Could not open " + names[idx] + " (in use by another app?)");
                on = false;
            } else {
                camName = names[idx];
                camera->addListener(&camListener);
            }
        }
    }
#else
    if (on) addSystem("Camera isn't supported in this build");
    on = false;
#endif
    camOn = on;
    videoLink->setSending(VideoLink::Cam, on);
    ++stateVersion;
}

void Processor::selectCam(const juce::String& name) {
    camName = name;
    if (camOn) setCam(true);
}

void Processor::selectMic(const juce::String& name) {
    micName = name;
    if (audioLink->micOn.load()) setMicEnabled(true);
}

bool Processor::isBusesLayoutSupported(const BusesLayout& l) const {
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           && l.getMainInputChannelSet() == l.getMainOutputChannelSet();
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

void Processor::getStateInformation(juce::MemoryBlock& mb) {
    juce::ValueTree t("B2B");
    t.setProperty("name", myName, nullptr);
    t.setProperty("stream", audioLink->streamInput.load(), nullptr);
    t.setProperty("mic", audioLink->micOn.load(), nullptr);
    t.setProperty("micName", micName, nullptr);
    t.setProperty("kbps", audioLink->bitrateKbps.load(), nullptr);
    t.setProperty("recvDaw", audioLink->recvGain[AudioLink::Daw].load(), nullptr);
    t.setProperty("recvVoice", audioLink->recvGain[AudioLink::Voice].load(), nullptr);
    t.setProperty("screenFps", videoLink->screenFps.load(), nullptr);
    t.setProperty("screenHD", videoLink->screenHD.load(), nullptr);
    t.setProperty("onTop", keepOnTop, nullptr);
    t.setProperty("autoSendRec", autoSendRecordings, nullptr);
    t.setProperty("recvDir", receiveDir.getFullPathName(), nullptr);
    t.setProperty("micGain", audioLink->micGain.load(), nullptr);
    t.setProperty("streamGain", audioLink->streamGain.load(), nullptr);
    t.setProperty("camName", camName, nullptr);
    t.setProperty("screen", videoLink->screenIndex.load(), nullptr);
    t.setProperty("w", editorW, nullptr);
    t.setProperty("h", editorH, nullptr);
    juce::MemoryOutputStream os(mb, false);
    t.writeToStream(os);
}

void Processor::setStateInformation(const void* data, int size) {
    auto t = juce::ValueTree::readFromData(data, (size_t) size);
    if (!t.isValid()) return;
    if (t.hasProperty("name")) myName = t["name"].toString();
    audioLink->streamInput = (bool) t.getProperty("stream", false);
    audioLink->bitrateKbps = (int) t.getProperty("kbps", 192);
    audioLink->recvGain[AudioLink::Daw] = (float) t.getProperty("recvDaw", 1.0f);
    audioLink->recvGain[AudioLink::Voice] = (float) t.getProperty("recvVoice", 1.0f);
    videoLink->screenFps = (int) t.getProperty("screenFps", 10);
    videoLink->screenHD = (bool) t.getProperty("screenHD", false);
    keepOnTop = (bool) t.getProperty("onTop", true);
    autoSendRecordings = (bool) t.getProperty("autoSendRec", false);
    {
        juce::File d(t.getProperty("recvDir", "").toString());
        if (d.getFullPathName().isNotEmpty() && (d.isDirectory() || d.createDirectory())) setReceiveFolder(d);
    }
    audioLink->micGain = (float) t.getProperty("micGain", 1.0f);
    micName = t.getProperty("micName", "").toString();
    audioLink->streamGain = (float) t.getProperty("streamGain", 1.0f);
    camName = t.getProperty("camName", "").toString();
    videoLink->screenIndex = (int) t.getProperty("screen", 0);
    editorW = (int) t.getProperty("w", 1000);
    editorH = (int) t.getProperty("h", 660);
    if ((bool) t.getProperty("mic", false))
        juce::MessageManager::callAsync([sp = juce::WeakReference<Processor>(this)] { if (auto* p = sp.get()) p->setMicEnabled(true); });
    ++stateVersion;
}

void Processor::host() { engine->host(myName.toStdString()); }

void Processor::join(const juce::String& c) { engine->join(c.toStdString(), myName.toStdString()); }

void Processor::leave() {
    engine->leave();
    addSystem("You left the session");
}

void Processor::sendChat(const juce::String& text) {
    auto t = text.trim();
    if (t.isNotEmpty()) engine->sendChat(t.toStdString());
}

void Processor::sendFiles(const juce::StringArray& paths) {
    for (auto& p : paths) {
        juce::File f(p);
        if (f.isDirectory()) {
            // whole folder: keep its structure ("Folder/sub/file.wav") on the other side
            int count = 0;
            for (const auto& entry : juce::RangedDirectoryIterator(f, true, "*", juce::File::findFiles)) {
                auto file = entry.getFile();
                auto n = file.getFileName();
                if (n.startsWithChar('.') || n.equalsIgnoreCase("Thumbs.db") || n.equalsIgnoreCase("desktop.ini")) continue;
                if (++count > 2000) {
                    addSystem("Stopped at 2000 files - zip the rest of " + f.getFileName());
                    break;
                }
                auto rel = f.getFileName() + "/" + file.getRelativePathFrom(f).replaceCharacter('\\', '/');
                engine->sendFile(file.getFullPathName().toStdString(), rel.toStdString());
            }
            if (count > 0) addSystem("Sending folder " + f.getFileName() + " (" + juce::String(juce::jmin(count, 2000)) + " files)");
            else addSystem("Folder " + f.getFileName() + " is empty");
            continue;
        }
        if (engine->sendFile(p.toStdString()) == 0) addSystem("Could not send " + f.getFileName());
    }
}

void Processor::addSystem(const juce::String& s) {
    ChatLine l;
    l.system = true;
    l.text = s;
    l.time = juce::Time::getCurrentTime().formatted("%H:%M");
    chat.push_back(l);
    if (chat.size() > 500) chat.erase(chat.begin());
    ++chatVersion;
}

FileRow* Processor::findFile(int id, bool incoming) {
    for (auto& f : files)
        if (f.id == id && f.incoming == incoming) return &f;
    return nullptr;
}

int Processor::peopleCount() const {
    int n = 0;
    for (auto& p : people) n += p.present ? 1 : 0;
    return n;
}

juce::String Processor::nameOf(int slot) const {
    if (slot >= 0 && slot < AudioLink::kPeers && people[slot].name.isNotEmpty()) return people[slot].name;
    return "Someone";
}

juce::String Processor::peopleSummary() const {
    juce::StringArray a;
    for (auto& p : people)
        if (p.present) a.add(p.name.isNotEmpty() ? p.name : juce::String("Someone"));
    return a.joinIntoString(", ");
}

void Processor::checkMidiOutbox() {
    // takes handed over by b2b MIDI (another plugin in this DAW)
    if (state != LinkState::Connected) return;
    auto box = b2bpaths::midiOutbox();
    if (!box.isDirectory()) return;
    auto sent = box.getChildFile("sent");
    for (const auto& entry : juce::RangedDirectoryIterator(box, false, "*.mid", juce::File::findFiles)) {
        auto f = entry.getFile();
        if (f.getLastModificationTime() > juce::Time::getCurrentTime() - juce::RelativeTime::milliseconds(300)) continue; // still being written
        sent.createDirectory();
        auto dst = sent.getNonexistentChildFile(f.getFileNameWithoutExtension(), ".mid");
        if (!f.moveFileTo(dst)) continue; // another b2b instance took it
        if (engine->sendFile(dst.getFullPathName().toStdString(), ("MIDI/" + f.getFileName()).toStdString()) > 0)
            addSystem("Sending MIDI " + f.getFileNameWithoutExtension());
    }
}

void Processor::timerCallback() {
    if (++outboxTick % 20 == 0) checkMidiOutbox();
    audioLink->linked = state == LinkState::Connected;
    for (auto& e : engine->poll()) {
        switch (e.type) {
            case Event::Type::State: {
                auto prev = state;
                state = e.state;
                statusText = juce::String::fromUTF8(e.text.c_str());
                code = juce::String(smash::LinkEngine::prettyCode(engine->roomCode()));
                if (state != LinkState::Connected && state != LinkState::Waiting) {
                    for (int i = 0; i < AudioLink::kPeers; ++i) {
                        if (people[i].present) { audioLink->resetSlot(i); videoLink->peerLeft(i); }
                        people[i] = {};
                    }
                    pingMs = -1;
                }
                if (state == LinkState::Error) addSystem(statusText);
                if (prev == LinkState::Connected && state == LinkState::Waiting) addSystem(statusText);
                ++stateVersion;
                break;
            }
            case Event::Type::PeerJoined:
                if (e.peer >= 0 && e.peer < AudioLink::kPeers) {
                    auto& p = people[e.peer];
                    p.present = true;
                    p.name = juce::String::fromUTF8(e.text.c_str());
                    p.pingMs = -1;
                    audioLink->resetSlot(e.peer);
                    audioLink->personGain[e.peer] = 1.0f;
                    audioLink->personMute[e.peer] = false;
                    videoLink->peerJoined(e.peer);
                    addSystem(p.name + " joined");
                    ++stateVersion;
                }
                break;
            case Event::Type::PeerLeft:
                if (e.peer >= 0 && e.peer < AudioLink::kPeers) {
                    auto& p = people[e.peer];
                    if (p.present) addSystem(nameOf(e.peer) + " left");
                    p = {};
                    audioLink->resetSlot(e.peer);
                    videoLink->peerLeft(e.peer);
                    for (auto& f : remoteWasActive[e.peer]) f = false;
                    ++stateVersion;
                }
                break;
            case Event::Type::Ping: {
                if (e.peer >= 0 && e.peer < AudioLink::kPeers) people[e.peer].pingMs = e.pingMs;
                int worst = -1;
                for (auto& p : people)
                    if (p.present) worst = juce::jmax(worst, p.pingMs);
                pingMs = worst;
                ++stateVersion;
                break;
            }
            case Event::Type::Chat: {
                ChatLine l;
                l.slot = e.incoming ? e.peer : -1;
                l.from = juce::String::fromUTF8(e.from.c_str());
                l.text = juce::String::fromUTF8(e.text.c_str());
                l.mine = !e.incoming;
                l.time = juce::Time::getCurrentTime().formatted("%H:%M");
                chat.push_back(l);
                if (chat.size() > 500) chat.erase(chat.begin());
                ++chatVersion;
                break;
            }
            case Event::Type::FileStart: {
                FileRow r;
                r.id = e.fileId;
                r.name = juce::String::fromUTF8(e.text.c_str());
                r.path = juce::String::fromUTF8(e.path.c_str());
                r.incoming = e.incoming;
                r.total = e.total;
                if (r.incoming) { r.fromSlot = e.peer; r.from = juce::String::fromUTF8(e.from.c_str()); }
                else r.recipients = juce::jmax(1, e.recipients);
                if (!r.incoming) r.midi = MidiPreview::load(juce::File(r.path));
                files.insert(files.begin(), r);
                if (files.size() > 300) files.pop_back();
                ++fileVersion;
                break;
            }
            case Event::Type::FileProgress:
                if (auto* r = findFile(e.fileId, e.incoming)) {
                    if (r->incoming) r->bytes = e.bytes;
                    else if (e.peer >= 0 && e.peer < 3) {
                        r->sentTo[e.peer] = e.bytes;
                        juce::uint64 sum = 0;
                        for (auto b : r->sentTo) sum += b;
                        r->bytes = sum / (juce::uint64) r->recipients; // average over everyone
                    }
                    ++fileVersion;
                }
                break;
            case Event::Type::FileDone:
            case Event::Type::FileFailed: {
                const bool ok = e.type == Event::Type::FileDone;
                auto* r = findFile(e.fileId, e.incoming);
                if (!r && !e.incoming) r = findFile(e.fileId, false);
                if (!r) break;
                if (r->incoming) {
                    r->status = ok ? FileRow::Status::Done : FileRow::Status::Failed;
                    if (ok) {
                        r->bytes = r->total;
                        if (!e.path.empty()) r->path = juce::String::fromUTF8(e.path.c_str());
                        if (!r->midi) r->midi = MidiPreview::load(juce::File(r->path));
                    }
                } else {
                    (ok ? r->delivered : r->failed)++;
                    if (ok && e.peer >= 0 && e.peer < 3) r->sentTo[e.peer] = r->total;
                    if (r->delivered + r->failed >= r->recipients) {
                        r->status = r->delivered > 0 ? FileRow::Status::Done : FileRow::Status::Failed;
                        r->bytes = r->total;
                    }
                }
                ++fileVersion;
                break;
            }
            case Event::Type::Log:
                break;
        }
    }
    audioLink->linked = state == LinkState::Connected;
    videoLink->linked = state == LinkState::Connected;
    if (audioLink->appNeedsRestart()) setAppAudio(true); // sample rate changed
    {
        const auto st = audioLink->appState();
        if (st != lastAppState) {
            using AS = AudioLink::AppState;
            if (st == AS::Locked && lastAppState != AS::Off) addSystem("APP AUDIO locked - echo-free");
            if (st == AS::WrongRate) addSystem("APP AUDIO: set your project to 48 kHz (Ableton: Settings > Audio > Sample Rate) - macOS only captures at 48k");
            if (st == AS::NoSignal) addSystem("APP AUDIO: no sound from your DAW yet. Windows: ASIO drivers can't be captured, use the WASAPI/MME driver.");
            lastAppState = st;
            ++stateVersion;
        }
    }
    if (state != LinkState::Connected) {
        if (audioLink->appAudioOn()) setAppAudio(false);
        if (videoLink->isSending(VideoLink::Screen)) setScreenShare(false);
        if (camOn) setCam(false);
    }
    for (int p = 0; p < AudioLink::kPeers; ++p)
        for (int s = 0; s < VideoLink::NumStreams; ++s) {
            bool act = people[p].present && videoLink->remoteActive(p, (VideoLink::Stream) s);
            if (act != remoteWasActive[p][s]) {
                remoteWasActive[p][s] = act;
                if (people[p].present)
                    addSystem(nameOf(p) + (act ? (s == VideoLink::Screen ? " started sharing their screen" : " turned on their camera")
                                               : (s == VideoLink::Screen ? " stopped sharing" : " turned off their camera")));
                ++stateVersion;
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Processor(); }
