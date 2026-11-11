// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#include "MidiCapture.h"
#include <cmath>

namespace {
double nowSec() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }
constexpr int kTicks = 960;
} // namespace

int MidiCapture::Take::bars() const {
    const double beatsPerBar = num * 4.0 / den;
    return juce::jmax(1, (int) std::ceil(lengthPpq / beatsPerBar - 1e-6));
}

MidiCapture::MidiCapture() : ring((size_t) fifo.getTotalSize()) { openStart.fill(-1); }

void MidiCapture::push(const Ev& e) {
    int s1, z1, s2, z2;
    fifo.prepareToWrite(1, s1, z1, s2, z2);
    if (z1 > 0) ring[(size_t) s1] = e;
    else if (z2 > 0) ring[(size_t) s2] = e;
    fifo.finishedWrite(z1 + z2);
}

// ---------------------------------------------------------------- audio thread
void MidiCapture::process(const juce::MidiBuffer& midi, int numSamples, double sampleRate, juce::AudioPlayHead* ph) {
    juce::Optional<juce::AudioPlayHead::PositionInfo> pos;
    if (ph) pos = ph->getPosition();
    const bool playing = pos.hasValue() && pos->getIsPlaying() && pos->getPpqPosition().hasValue();
    const double bpm = pos.hasValue() && pos->getBpm().hasValue() ? *pos->getBpm() : liveBpm;
    if (pos.hasValue() && pos->getBpm().hasValue()) liveBpm = *pos->getBpm();
    int num = 4, den = 4;
    if (pos.hasValue() && pos->getTimeSignature().hasValue()) { num = pos->getTimeSignature()->numerator; den = pos->getTimeSignature()->denominator; }
    const double ppq0 = playing ? *pos->getPpqPosition() : 0.0;

    Ev base;
    base.bpm = bpm; base.num = num; base.den = den; base.wall = nowSec(); base.transport = playing;

    if (playing && !wasPlaying) {
        Ev e = base;
        e.type = Ev::Start;
        e.ppq = ppq0;
        double barLen = num * 4.0 / den;
        e.barStart = pos->getPpqPositionOfLastBarStart().hasValue() ? *pos->getPpqPositionOfLastBarStart() : std::floor(ppq0 / barLen) * barLen;
        push(e);
        loopedThisRun = false;
    } else if (!playing && wasPlaying) {
        Ev e = base;
        e.type = Ev::Stop;
        e.ppq = lastPpq;
        push(e);
    } else if (playing && ppq0 < lastPpq - 0.05 && !loopedThisRun) {
        // pattern / loop wrapped: one pass is captured, close it at the loop point
        Ev e = base;
        e.type = Ev::Loop;
        e.ppq = lastPpq;
        push(e);
        loopedThisRun = true;
    }

    const double ppqPerSample = bpm / 60.0 / sampleRate;
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        if (!m.isNoteOnOrOff() && !m.isAllNotesOff()) continue;
        Ev e = base;
        e.type = Ev::Midi;
        e.ppq = ppq0 + meta.samplePosition * ppqPerSample;
        e.wall = base.wall + meta.samplePosition / sampleRate;
        auto raw = m.getRawData();
        e.d0 = raw[0];
        e.d1 = m.getRawDataSize() > 1 ? raw[1] : 0;
        e.d2 = m.getRawDataSize() > 2 ? raw[2] : 0;
        push(e);
    }
    wasPlaying = playing;
    if (playing) lastPpq = ppq0 + numSamples * ppqPerSample;
}

// ---------------------------------------------------------------- message thread
void MidiCapture::beginTake(const Ev& e) {
    take = {};
    take.startPpq = e.barStart;
    take.bpm = e.bpm;
    take.num = e.num;
    take.den = e.den;
    take.live = !e.transport;
    openStart.fill(-1);
    capturing = true;
}

void MidiCapture::finishTake(double endPpq) {
    if (!capturing) return;
    for (int i = 0; i < 128 * 16; ++i)
        if (openStart[(size_t) i] >= 0) {
            take.notes.push_back({ openStart[(size_t) i], juce::jmax(openStart[(size_t) i] + 0.01, endPpq), i % 128, openVel[(size_t) i], i / 128 + 1 });
            openStart[(size_t) i] = -1;
        }
    capturing = false;
    if (take.notes.empty()) return;
    std::sort(take.notes.begin(), take.notes.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    double last = take.startPpq;
    for (auto& n : take.notes) last = juce::jmax(last, n.end);
    const double barLen = take.num * 4.0 / take.den;
    take.lengthPpq = juce::jmax(barLen, std::ceil((last - take.startPpq) / barLen - 1e-6) * barLen);
    finished = take;
}

int MidiCapture::heldNotes() const {
    int n = 0;
    for (double v : openStart) n += v >= 0 ? 1 : 0;
    return n;
}

void MidiCapture::clear() {
    take = {};
    finished = {};
    capturing = false;
    openStart.fill(-1);
}

bool MidiCapture::update() {
    bool done = false;
    int s1, z1, s2, z2;
    const int n = fifo.getNumReady();
    fifo.prepareToRead(n, s1, z1, s2, z2);
    auto handle = [&](const Ev& e) {
        switch (e.type) {
            case Ev::Start:
                ignoreUntilStop = false;
                beginTake(e);
                break;
            case Ev::Loop:
                if (capturing) { finishTake(e.ppq); done = !finished.empty(); }
                ignoreUntilStop = true; // don't capture the repeats
                break;
            case Ev::Stop:
                if (capturing && !take.live) { finishTake(e.ppq); done = !finished.empty(); }
                ignoreUntilStop = false;
                break;
            case Ev::Midi: {
                if (ignoreUntilStop) break;
                double ppq = e.ppq;
                if (!e.transport) {
                    // live playing without the transport: time from the first note at the current tempo
                    if (!capturing || !take.live) {
                        Ev st = e;
                        st.barStart = 0;
                        beginTake(st);
                        liveStartWall = e.wall;
                    }
                    ppq = (e.wall - liveStartWall) * take.bpm / 60.0;
                    lastLiveEventWall = e.wall;
                } else if (!capturing) {
                    break;
                }
                const int status = e.d0 & 0xf0, ch = e.d0 & 0x0f;
                if (status == 0xb0) { finishTake(ppq); done = !finished.empty(); break; } // all notes off
                const int idx = ch * 128 + (e.d1 & 0x7f);
                if (status == 0x90 && e.d2 > 0) {
                    if (openStart[(size_t) idx] >= 0)
                        take.notes.push_back({ openStart[(size_t) idx], ppq, e.d1 & 0x7f, openVel[(size_t) idx], ch + 1 });
                    openStart[(size_t) idx] = ppq;
                    openVel[(size_t) idx] = e.d2;
                } else if (openStart[(size_t) idx] >= 0) {
                    take.notes.push_back({ openStart[(size_t) idx], juce::jmax(openStart[(size_t) idx] + 0.01, ppq), e.d1 & 0x7f, openVel[(size_t) idx], ch + 1 });
                    openStart[(size_t) idx] = -1;
                }
                break;
            }
        }
    };
    for (int i = 0; i < z1; ++i) handle(ring[(size_t) (s1 + i)]);
    for (int i = 0; i < z2; ++i) handle(ring[(size_t) (s2 + i)]);
    fifo.finishedRead(z1 + z2);

    // live take ends after 3 s without playing (and nothing held)
    if (capturing && take.live && heldNotes() == 0 && !take.notes.empty() && nowSec() - lastLiveEventWall > 3.0) {
        finishTake((lastLiveEventWall - liveStartWall) * take.bpm / 60.0);
        done = !finished.empty();
    }
    return done;
}

// ---------------------------------------------------------------- MIDI file
bool MidiCapture::writeMidiFile(const Take& t, const juce::File& f, const juce::String& trackName) {
    if (t.empty()) return false;
    juce::MidiMessageSequence seq;
    auto meta = [&](juce::MidiMessage m) { m.setTimeStamp(0); seq.addEvent(m); };
    meta(juce::MidiMessage::textMetaEvent(3, trackName));
    meta(juce::MidiMessage::tempoMetaEvent(juce::roundToInt(60000000.0 / t.bpm)));
    meta(juce::MidiMessage::timeSignatureMetaEvent(t.num, t.den));
    for (auto& n : t.notes) {
        const double on = juce::jmax(0.0, (n.start - t.startPpq) * kTicks);
        const double off = juce::jmax(on + 1.0, (n.end - t.startPpq) * kTicks);
        seq.addEvent(juce::MidiMessage::noteOn(n.channel, n.pitch, (juce::uint8) juce::jlimit(1, 127, n.velocity)), on);
        seq.addEvent(juce::MidiMessage::noteOff(n.channel, n.pitch), off);
    }
    auto eot = juce::MidiMessage::endOfTrack();
    eot.setTimeStamp(t.lengthPpq * kTicks);
    seq.addEvent(eot);
    seq.sort();
    seq.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(kTicks);
    mf.addTrack(seq);
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::FileOutputStream os(f);
    if (!os.openedOk()) return false;
    return mf.writeTo(os, 1);
}

juce::String MidiCapture::suggestName(const Take& t, const juce::String& base) {
    auto b = base.trim().isEmpty() ? juce::String("b2b MIDI") : base.trim();
    return juce::File::createLegalFileName(b + " - " + juce::String(juce::roundToInt(t.bpm)) + " BPM " + juce::String(t.bars())
                                           + (t.bars() == 1 ? " bar" : " bars") + juce::Time::getCurrentTime().formatted(" - %H%M%S"))
         + ".mid";
}
