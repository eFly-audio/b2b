// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "MidiCapture.h"

// b2b MIDI: load it as an instrument (FL channel / Ableton MIDI track). It captures what the
// piano roll / clip plays into it and hands finished takes to b2b, which sends them to your
// collaborator. Silent: it makes no sound itself.
class MidiProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    MidiProcessor();
    ~MidiProcessor() override { stopTimer(); }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "b2b MIDI"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    void updateTrackProperties(const TrackProperties& p) override { if (p.name.isNotEmpty()) trackName = p.name; }

    // UI (message thread)
    MidiCapture capture;
    bool autoSend = false;
    juce::String trackName;
    juce::String status;
    juce::File lastTakeFile, pendingOutbox;
    int version = 0;
    juce::File writeTake();  // writes the last finished take to MIDI Takes, returns it
    void sendTake();         // writeTake + hand it to b2b via the outbox
    void clearTake();

private:
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiProcessor)
};

class MidiEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit MidiEditor(MidiProcessor&);
    ~MidiEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    MidiProcessor& proc;
    struct Lnf : juce::LookAndFeel_V4 {
        Lnf();
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        juce::Font getTextButtonFont(juce::TextButton&, int h) override;
    } lnf;
    juce::TextButton sendBtn { "SEND TO COLLABORATOR" }, clearBtn { "CLEAR" };
    juce::ToggleButton autoToggle { "Auto-send every take" };
    juce::Rectangle<int> rollArea, dragArea;
    juce::Image logo;
    int seen = -1;
    bool dragStarted = false;
};
