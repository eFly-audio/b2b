// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#pragma once
#include "Processor.h"

namespace ui {
const juce::Colour bg { 0xff0e0f11 }, panel { 0xff15171b }, panel2 { 0xff1b1e23 }, line { 0xff262a31 },
    text { 0xffe9eaec }, dim { 0xff7d838d }, accent { 0xffc8ff3d }, amber { 0xffffb547 }, red { 0xffff5d5d };
// one colour per person in the room (slot 0..2)
inline juce::Colour person(int slot) {
    static const juce::Colour c[] = { juce::Colour(0xff5ee0ff), juce::Colour(0xffff7ad9), juce::Colour(0xffffb547) };
    return slot >= 0 && slot < 3 ? c[slot] : accent;
}
}

class LinkLookAndFeel : public juce::LookAndFeel_V4 {
public:
    LinkLookAndFeel();
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawTextEditorOutline(juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void fillTextEditorBackground(juce::Graphics&, int w, int h, juce::TextEditor&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int h) override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::Font(13.0f); }
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void drawBubble(juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>&) override;
};

// Pill-shaped toggle with an icon; glows when on.
class PillToggle : public juce::Button {
public:
    enum class Icon { Mic, Stream, Screen, Cam, PopOut, Gear, Rec };
    PillToggle(const juce::String& label, Icon icon);
    void paintButton(juce::Graphics&, bool over, bool down) override;
    static void drawIcon(juce::Graphics&, Icon, juce::Rectangle<float>, juce::Colour);

private:
    Icon icon;
};

// Peak meter with dBFS readout, 1.5 s peak hold and a click-to-reset clip latch.
class LevelMeter : public juce::Component, public juce::SettableTooltipClient {
public:
    void push(float linear);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override { clip = false; holdDb = -100.0f; repaint(); }

private:
    float level = 0, holdDb = -100.0f;
    juce::int64 holdT = 0;
    bool clip = false;
};

// Gain trim in dB (-24..+12), double-click resets to 0 dB.
class TrimKnob : public juce::Slider {
public:
    TrimKnob();
    std::function<void(float gain)> onGain;
    void setGain(float g) { setValue(juce::Decibels::gainToDecibels(g, -24.0f), juce::dontSendNotification); }
};

class FileList : public juce::Component {
public:
    explicit FileList(Processor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { hover = -1; repaint(); }
    static constexpr int rowH = 54;
    int rowAt(int y) const { return y / rowH; }

private:
    Processor& proc;
    int hover = -1, pressed = -1;
    bool dragStarted = false;
};

// Everyone's screens / cameras: a grid of up to 3 tiles; click a tile to focus it, click again for the grid.
class VideoView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    explicit VideoView(Processor& p, bool popOut = false);
    void paint(juce::Graphics&) override;
    std::function<void()> onPopOut;
    std::function<void(bool)> onPin; // pop-out window only
    bool pinned = true;
    void mouseDoubleClick(const juce::MouseEvent&) override { if (onPopOut) onPopOut(); }
    void mouseDown(const juce::MouseEvent& e) override;
    int focus = -1; // slot shown big, -1 = grid

private:
    void timerCallback() override;
    juce::Rectangle<float> pinArea() const;
    void drawTile(juce::Graphics&, int slot, juce::Rectangle<float> r, bool big);
    Processor& proc;
    bool popOut;
    static constexpr int P = AudioLink::kPeers;
    juce::Image frame[P][VideoLink::NumStreams];
    int ver[P][VideoLink::NumStreams];
    std::vector<std::pair<int, juce::Rectangle<float>>> tiles;
};

// People in the room: one chip per person (colour, name, talking light, ping). Click = their volume.
class PeopleStrip : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit PeopleStrip(Processor& p) : proc(p) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    int preferredWidth() const;

private:
    std::vector<std::pair<int, juce::Rectangle<float>>> chips;
    Processor& proc;
};

// Settings overlay (gear button): stream quality, screen share, files, window.
class SettingsPanel : public juce::Component {
public:
    explicit SettingsPanel(Processor& p);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    std::function<void()> onClose;
    void refresh();

private:
    juce::Rectangle<int> card() const;
    Processor& proc;
    juce::ComboBox qualityBox, fpsBox, resBox;
    juce::TextButton changeFolderBtn { "CHANGE" }, openFolderBtn { "OPEN" }, doneBtn { "DONE" };
    juce::ToggleButton onTopToggle { "Video window stays on top of the DAW" };
    juce::ToggleButton autoSendToggle { "Send recordings to everyone automatically" };
    std::unique_ptr<juce::FileChooser> chooser;
};

class VideoWindow : public juce::DocumentWindow {
public:
    explicit VideoWindow(Processor& p);
    void closeButtonPressed() override { setVisible(false); }
};

class Editor : public juce::AudioProcessorEditor,
               public juce::FileDragAndDropTarget,
               public juce::DragAndDropContainer,
               private juce::Timer {
public:
    explicit Editor(Processor&);
    ~Editor() override;
    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void refreshState();
    void refreshChat();
    void refreshFiles();
    void sendChat();
    void doJoin();
    void popOutVideo();
    void paintPanel(juce::Graphics&, juce::Rectangle<int>, const juce::String& title);

    Processor& proc;
    LinkLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    // connect view
    juce::Label nameLabel, orLabel;
    juce::TextEditor nameEdit, codeEdit;
    juce::TextButton hostBtn { "HOST SESSION" }, joinBtn { "JOIN" };
    // waiting view
    juce::TextButton copyBtn { "COPY CODE" }, cancelBtn { "CANCEL" };
    // header
    juce::TextButton folderBtn { "OPEN FOLDER" }, leaveBtn { "LEAVE" };
    PillToggle gearBtn { "", PillToggle::Icon::Gear };
    PillToggle recBtn { "REC", PillToggle::Icon::Rec };
    juce::TextButton sendRecBtn { "SEND REC" };
    juce::TextButton codeChip;  // room code in the header: click to copy, others can join mid-session
    SettingsPanel settings;
    // audio bar
    PillToggle micBtn { "MIC", PillToggle::Icon::Mic }, streamBtn { "STREAM DAW", PillToggle::Icon::Stream };
    PillToggle appBtn { "PREVIEW", PillToggle::Icon::Stream };
    juce::ComboBox micBox;
    LevelMeter micMeter, streamMeter, inMeter;
    TrimKnob micTrim, streamTrim;
    juce::Slider voiceSlider, dawSlider;
    // stage
    PillToggle screenBtn { "SCREEN", PillToggle::Icon::Screen }, camBtn { "CAM", PillToggle::Icon::Cam };
    juce::ComboBox screenBox, camBox;
    PillToggle popBtn { "", PillToggle::Icon::PopOut };
    VideoView stage;
    PeopleStrip people;
    std::unique_ptr<VideoWindow> videoWindow;
    // files + chat
    juce::TextButton attachBtn { "+ ADD" };
    FileList fileList;
    juce::Viewport fileView;
    juce::TextEditor chatLog, chatInput;
    juce::TextButton sendBtn { "SEND" };

    std::unique_ptr<juce::FileChooser> chooser;
    int seenState = -1, seenChat = -1, seenFiles = -1;
    bool dragOver = false;
    juce::Rectangle<int> header, codeBox, audioBar, stageArea, filesArea, chatArea, inLabelArea, statusArea, connectCard, voiceLabel, dawLabel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};
