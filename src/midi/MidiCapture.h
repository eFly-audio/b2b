// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#pragma once
// b2b MIDI: captures what the host plays into the plugin (piano roll / clip / live keys)
// as tempo-synced takes and writes them as standard MIDI files.
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

class MidiCapture {
public:
    struct Note { double start = 0, end = 0; int pitch = 60, velocity = 100, channel = 1; };
    struct Take {
        std::vector<Note> notes;
        double startPpq = 0, lengthPpq = 0; // start = bar line where playback began
        double bpm = 120.0;
        int num = 4, den = 4;
        bool live = false;                  // captured without the host transport
        int bars() const;
        bool empty() const { return notes.empty(); }
    };

    MidiCapture();

    // audio thread
    void process(const juce::MidiBuffer& midi, int numSamples, double sampleRate, juce::AudioPlayHead* playHead);

    // message thread: drain events; returns true when a take was just finished
    bool update();
    const Take& current() const { return take; }   // in progress or last finished
    const Take& lastFinished() const { return finished; }
    bool isCapturing() const { return capturing; }
    int heldNotes() const;
    void clear();
    void debugSetFinished(const Take& t) { finished = t; } // UI previews / tests

    // write `t` as a type-1 MIDI file (960 ppq, tempo + time signature, notes from bar 1)
    static bool writeMidiFile(const Take& t, const juce::File& f, const juce::String& trackName);
    static juce::String suggestName(const Take& t, const juce::String& base);

private:
    struct Ev {
        enum Type : uint8_t { Midi, Start, Stop, Loop } type = Midi;
        uint8_t d0 = 0, d1 = 0, d2 = 0;
        double ppq = 0, barStart = 0, bpm = 120, wall = 0;
        int num = 4, den = 4;
        bool transport = true;
    };
    void push(const Ev& e);
    void beginTake(const Ev& e);
    void finishTake(double endPpq);

    juce::AbstractFifo fifo { 16384 };
    std::vector<Ev> ring;

    // audio-thread state
    bool wasPlaying = false;
    double lastPpq = -1;
    bool loopedThisRun = false;
    double liveBpm = 120;

    // message-thread state
    Take take, finished;
    bool capturing = false;
    bool ignoreUntilStop = false;
    std::array<double, 128 * 16> openStart {};
    std::array<int, 128 * 16> openVel {};
    double lastLiveEventWall = 0, liveStartWall = 0;
};
