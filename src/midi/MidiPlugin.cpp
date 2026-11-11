// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#include "MidiPlugin.h"
#include "../B2BPaths.h"
#include "../LogoData.h"

namespace col {
const juce::Colour bg { 0xff0e0f11 }, panel { 0xff15171b }, panel2 { 0xff1b1e23 }, line { 0xff262a31 },
    text { 0xffe9eaec }, dim { 0xff8b919b }, accent { 0xffc8ff3d };
}

// ============================================================== processor
MidiProcessor::MidiProcessor()
    : AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo(), true)) {
    status = "Play your pattern or clip - one pass gets captured";
    startTimerHz(30);
}

void MidiProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    capture.process(midi, buffer.getNumSamples(), getSampleRate() > 0 ? getSampleRate() : 48000.0, getPlayHead());
    buffer.clear(); // silent; MIDI passes through
}

void MidiProcessor::timerCallback() {
    const bool wasCapturing = capture.isCapturing();
    if (capture.update()) {
        auto& t = capture.lastFinished();
        status = "Captured " + juce::String(t.bars()) + (t.bars() == 1 ? " bar, " : " bars, ") + juce::String((int) t.notes.size())
                 + " notes at " + juce::String(juce::roundToInt(t.bpm)) + " BPM";
        lastTakeFile = juce::File();
        if (autoSend) sendTake();
        ++version;
    } else if (capture.isCapturing() != wasCapturing || capture.isCapturing()) {
        if (capture.isCapturing()) status = "Capturing... " + juce::String((int) capture.current().notes.size() + capture.heldNotes()) + " notes";
        ++version;
    }
    // the outbox file disappears once b2b has picked it up
    if (pendingOutbox != juce::File() && !pendingOutbox.existsAsFile()) {
        status = "Sent - it shows up in your collaborator's FILES";
        pendingOutbox = juce::File();
        ++version;
    }
}

juce::File MidiProcessor::writeTake() {
    auto& t = capture.lastFinished();
    if (t.empty()) return {};
    if (lastTakeFile.existsAsFile()) return lastTakeFile;
    auto name = MidiCapture::suggestName(t, trackName.isNotEmpty() ? trackName : juce::String("b2b MIDI"));
    auto f = b2bpaths::midiTakes().getChildFile(name);
    if (!MidiCapture::writeMidiFile(t, f, trackName.isNotEmpty() ? trackName : juce::String("b2b MIDI"))) return {};
    lastTakeFile = f;
    return f;
}

void MidiProcessor::sendTake() {
    auto f = writeTake();
    if (!f.existsAsFile()) { status = "Nothing captured yet"; ++version; return; }
    auto out = b2bpaths::midiOutbox();
    out.createDirectory();
    auto dst = out.getChildFile(f.getFileName());
    if (f.copyFileTo(dst)) {
        pendingOutbox = dst;
        status = "Handed to b2b - sends as soon as b2b is linked";
    } else {
        status = "Couldn't write to " + out.getFullPathName();
    }
    ++version;
}

void MidiProcessor::clearTake() {
    capture.clear();
    lastTakeFile = juce::File();
    status = "Play your pattern or clip - one pass gets captured";
    ++version;
}

void MidiProcessor::getStateInformation(juce::MemoryBlock& mb) {
    juce::ValueTree t("B2BMIDI");
    t.setProperty("autoSend", autoSend, nullptr);
    juce::MemoryOutputStream os(mb, false);
    t.writeToStream(os);
}

void MidiProcessor::setStateInformation(const void* d, int n) {
    auto t = juce::ValueTree::readFromData(d, (size_t) n);
    if (t.isValid()) autoSend = (bool) t.getProperty("autoSend", false);
    ++version;
}

juce::AudioProcessorEditor* MidiProcessor::createEditor() { return new MidiEditor(*this); }

#ifndef B2B_MIDI_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MidiProcessor(); }
#endif

// ============================================================== editor
MidiEditor::Lnf::Lnf() {
    setColour(juce::TextButton::buttonColourId, col::panel2);
    setColour(juce::TextButton::textColourOffId, col::text);
    setColour(juce::ToggleButton::textColourId, col::text);
    setColour(juce::ToggleButton::tickColourId, col::accent);
    setColour(juce::ToggleButton::tickDisabledColourId, col::dim);
}

void MidiEditor::Lnf::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    bool primary = (bool) b.getProperties()["primary"];
    auto base = primary ? col::accent : col::panel2;
    if (down) base = base.darker(0.15f);
    else if (over) base = base.brighter(0.08f);
    g.setColour(base);
    g.fillRoundedRectangle(r, 6.0f);
    if (!primary) { g.setColour(over ? col::dim.withAlpha(0.5f) : col::line); g.drawRoundedRectangle(r, 6.0f, 1.0f); }
}

juce::Font MidiEditor::Lnf::getTextButtonFont(juce::TextButton&, int h) {
    return juce::Font(juce::jmin(12.5f, h * 0.4f), juce::Font::bold).withExtraKerningFactor(0.06f);
}

MidiEditor::MidiEditor(MidiProcessor& p) : AudioProcessorEditor(p), proc(p) {
    setLookAndFeel(&lnf);
    logo = juce::ImageCache::getFromMemory(b2b_logo_png, b2b_logo_png_len);
    sendBtn.getProperties().set("primary", true);
    sendBtn.setColour(juce::TextButton::textColourOffId, col::bg);
    sendBtn.onClick = [this] { proc.sendTake(); };
    clearBtn.onClick = [this] { proc.clearTake(); };
    autoToggle.setToggleState(proc.autoSend, juce::dontSendNotification);
    autoToggle.onClick = [this] { proc.autoSend = autoToggle.getToggleState(); };
    addAndMakeVisible(sendBtn);
    addAndMakeVisible(clearBtn);
    addAndMakeVisible(autoToggle);
    setSize(560, 340);
    startTimerHz(30);
}

MidiEditor::~MidiEditor() { setLookAndFeel(nullptr); }

void MidiEditor::timerCallback() {
    if (seen != proc.version) {
        seen = proc.version;
        sendBtn.setEnabled(!proc.capture.lastFinished().empty());
        autoToggle.setToggleState(proc.autoSend, juce::dontSendNotification);
        repaint();
    }
}

void MidiEditor::resized() {
    auto r = getLocalBounds().reduced(16, 12);
    r.removeFromTop(48);
    auto bottom = r.removeFromBottom(38);
    r.removeFromBottom(10);
    rollArea = r;
    sendBtn.setBounds(bottom.removeFromRight(190));
    bottom.removeFromRight(8);
    clearBtn.setBounds(bottom.removeFromRight(74));
    bottom.removeFromRight(8);
    dragArea = bottom.removeFromRight(110);
    autoToggle.setBounds(bottom);
}

void MidiEditor::paint(juce::Graphics& g) {
    g.fillAll(col::bg);
    auto hdr = getLocalBounds().removeFromTop(60).reduced(16, 8);
    if (logo.isValid()) {
        float h = 40.0f, w = h * logo.getWidth() / (float) logo.getHeight();
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(logo, juce::Rectangle<float>((float) hdr.getX() - 4, hdr.getCentreY() - h * 0.5f, w, h), juce::RectanglePlacement::centred);
        hdr.removeFromLeft((int) w + 4);
    }
    g.setColour(col::accent);
    g.setFont(juce::Font(20.0f, juce::Font::bold).withExtraKerningFactor(0.1f));
    g.drawText("MIDI", hdr.removeFromLeft(70), juce::Justification::centredLeft);
    g.setColour(col::dim);
    g.setFont(juce::Font(12.0f));
    g.drawText(proc.status, hdr, juce::Justification::centredRight, true);

    // piano roll preview
    g.setColour(col::panel);
    g.fillRoundedRectangle(rollArea.toFloat(), 8.0f);
    const auto& t = proc.capture.isCapturing() ? proc.capture.current() : proc.capture.lastFinished();
    auto roll = rollArea.reduced(12, 12).toFloat();
    if (t.notes.empty()) {
        g.setColour(col::dim);
        g.setFont(juce::Font(13.0f));
        g.drawFittedText("Put b2b MIDI on a channel / MIDI track, paste your notes into its piano roll and press play.\n"
                         "One pass gets captured, in time with your project. Live playing works too.",
                         rollArea.reduced(30), juce::Justification::centred, 3);
    } else {
        int lo = 127, hi = 0;
        double end = t.startPpq + juce::jmax(t.lengthPpq, 1.0);
        for (auto& n : t.notes) { lo = juce::jmin(lo, n.pitch); hi = juce::jmax(hi, n.pitch); end = juce::jmax(end, n.end); }
        lo -= 2; hi += 2;
        const double span = end - t.startPpq;
        const double barLen = t.num * 4.0 / t.den;
        g.setColour(col::line);
        for (double b = 0; b <= span + 1e-6; b += barLen) {
            float x = roll.getX() + (float) (b / span) * roll.getWidth();
            g.fillRect(x, roll.getY(), 1.0f, roll.getHeight());
        }
        const float rowH = roll.getHeight() / (float) (hi - lo + 1);
        for (auto& n : t.notes) {
            float x0 = roll.getX() + (float) ((n.start - t.startPpq) / span) * roll.getWidth();
            float x1 = roll.getX() + (float) ((n.end - t.startPpq) / span) * roll.getWidth();
            float y = roll.getBottom() - (n.pitch - lo + 1) * rowH;
            g.setColour(col::accent.withAlpha(0.45f + 0.55f * n.velocity / 127.0f));
            g.fillRoundedRectangle(x0, y + 1, juce::jmax(2.0f, x1 - x0 - 1), juce::jmax(2.0f, rowH - 2), 2.0f);
        }
    }

    // drag handle: drop the take straight into your own DAW
    const bool can = !proc.capture.lastFinished().empty();
    g.setColour(can ? col::panel2 : col::panel2.withAlpha(0.4f));
    g.fillRoundedRectangle(dragArea.toFloat().reduced(0.5f), 6.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(dragArea.toFloat().reduced(0.5f), 6.0f, 1.0f);
    g.setColour(can ? col::text : col::dim);
    g.setFont(juce::Font(12.0f, juce::Font::bold).withExtraKerningFactor(0.06f));
    auto da = dragArea.toFloat();
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            g.fillEllipse(da.getX() + 14.0f + j * 5.0f, da.getCentreY() - 7.0f + i * 5.5f, 2.6f, 2.6f);
    g.drawText("DRAG .MID", dragArea.withTrimmedLeft(16), juce::Justification::centred);
}

void MidiEditor::mouseMove(const juce::MouseEvent& e) {
    setMouseCursor(dragArea.contains(e.getPosition()) && !proc.capture.lastFinished().empty() ? juce::MouseCursor::DraggingHandCursor
                                                                                            : juce::MouseCursor::NormalCursor);
}

void MidiEditor::mouseDown(const juce::MouseEvent&) { dragStarted = false; }

void MidiEditor::mouseDrag(const juce::MouseEvent& e) {
    if (dragStarted || !dragArea.contains(e.getMouseDownPosition()) || e.getDistanceFromDragStart() < 5) return;
    auto f = proc.writeTake();
    if (!f.existsAsFile()) return;
    dragStarted = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({ f.getFullPathName() }, false, this);
}
