// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#include "Editor.h"
#include "LogoData.h"

using smash::LinkState;

// ============================================================== look & feel
LinkLookAndFeel::LinkLookAndFeel() {
    setColour(juce::TextButton::buttonColourId, ui::panel2);
    setColour(juce::TextButton::textColourOffId, ui::text);
    setColour(juce::TextButton::textColourOnId, ui::bg);
    setColour(juce::TextEditor::backgroundColourId, ui::panel2);
    setColour(juce::TextEditor::textColourId, ui::text);
    setColour(juce::TextEditor::highlightColourId, ui::accent.withAlpha(0.3f));
    setColour(juce::TextEditor::highlightedTextColourId, ui::text);
    setColour(juce::TextEditor::outlineColourId, ui::line);
    setColour(juce::TextEditor::focusedOutlineColourId, ui::accent.withAlpha(0.7f));
    setColour(juce::CaretComponent::caretColourId, ui::accent);
    setColour(juce::Label::textColourId, ui::dim);
    setColour(juce::ScrollBar::thumbColourId, ui::line);
    setColour(juce::ComboBox::backgroundColourId, ui::panel2);
    setColour(juce::ComboBox::outlineColourId, ui::line);
    setColour(juce::ComboBox::textColourId, ui::text);
    setColour(juce::ComboBox::arrowColourId, ui::dim);
    setColour(juce::PopupMenu::backgroundColourId, ui::panel2);
    setColour(juce::PopupMenu::textColourId, ui::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, ui::accent.withAlpha(0.18f));
    setColour(juce::PopupMenu::highlightedTextColourId, ui::text);
    setColour(juce::TooltipWindow::backgroundColourId, ui::panel2);
    setColour(juce::TooltipWindow::textColourId, ui::text);
    setColour(juce::TooltipWindow::outlineColourId, ui::line);
    setColour(juce::BubbleComponent::backgroundColourId, ui::panel2);
    setColour(juce::BubbleComponent::outlineColourId, ui::line);
    setColour(juce::ResizableWindow::backgroundColourId, ui::bg);
    setColour(juce::DocumentWindow::textColourId, ui::text);
}

void LinkLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    bool primary = (bool) b.getProperties()["primary"];
    bool danger = (bool) b.getProperties()["danger"];
    auto base = primary ? ui::accent : ui::panel2;
    if (!b.isEnabled()) base = base.withAlpha(0.35f);
    else if (down) base = base.darker(0.15f);
    else if (over) base = primary ? base.brighter(0.1f) : base.brighter(0.08f);
    g.setColour(base);
    g.fillRoundedRectangle(r, 6.0f);
    if (!primary) {
        g.setColour(danger && over ? ui::red.withAlpha(0.7f) : (over ? ui::dim.withAlpha(0.5f) : ui::line));
        g.drawRoundedRectangle(r, 6.0f, 1.0f);
    }
}

juce::Font LinkLookAndFeel::getTextButtonFont(juce::TextButton&, int h) {
    return juce::Font(juce::jmin(12.5f, h * 0.42f), juce::Font::bold).withExtraKerningFactor(0.06f);
}

void LinkLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int w, int h, juce::TextEditor& e) {
    g.setColour(e.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(0, 0, (float) w, (float) h, 6.0f);
}

void LinkLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int w, int h, juce::TextEditor& e) {
    if (e.isReadOnly()) return;
    g.setColour(e.hasKeyboardFocus(true) ? ui::accent.withAlpha(0.7f) : e.findColour(juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle(0.5f, 0.5f, w - 1.0f, h - 1.0f, 6.0f, 1.0f);
}

void LinkLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) {
    auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(2.0f);
    float d = juce::jmin(r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre(d, d);
    float rad = d * 0.5f - 2.0f;
    auto centre = c.getCentre();
    // track
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, rad, rad, 0, a0, a1, true);
    g.setColour(ui::line);
    g.strokePath(track, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // value arc from 0 dB
    double zeroPos = s.valueToProportionOfLength(0.0);
    float aZero = a0 + (float) zeroPos * (a1 - a0);
    float aVal = a0 + pos * (a1 - a0);
    if (std::abs(aVal - aZero) > 0.01f) {
        juce::Path v;
        v.addCentredArc(centre.x, centre.y, rad, rad, 0, juce::jmin(aZero, aVal), juce::jmax(aZero, aVal), true);
        g.setColour(s.getValue() > 0.05 ? ui::amber : ui::accent);
        g.strokePath(v, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // body + pointer
    g.setColour(s.isMouseOverOrDragging() ? ui::panel2.brighter(0.12f) : ui::panel2.brighter(0.04f));
    g.fillEllipse(c.reduced(4.5f));
    juce::Point<float> tip(centre.x + (rad - 4.5f) * std::sin(aVal), centre.y - (rad - 4.5f) * std::cos(aVal));
    g.setColour(ui::text);
    g.drawLine({ centre.getPointOnCircumference(rad * 0.15f, aVal), tip }, 1.6f);
}

void LinkLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                       juce::Slider::SliderStyle, juce::Slider& s) {
    float cy = y + h * 0.5f;
    juce::Rectangle<float> track((float) x, cy - 2.0f, (float) w, 4.0f);
    g.setColour(ui::line);
    g.fillRoundedRectangle(track, 2.0f);
    g.setColour(ui::accent);
    g.fillRoundedRectangle(track.withRight(pos), 2.0f);
    float r = s.isMouseOverOrDragging() ? 7.0f : 6.0f;
    g.setColour(ui::text);
    g.fillEllipse(pos - r, cy - r, r * 2, r * 2);
    g.setColour(ui::bg.withAlpha(0.4f));
    g.drawEllipse(pos - r, cy - r, r * 2, r * 2, 1.0f);
}

void LinkLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& b) {
    auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(0.5f);
    g.setColour(ui::panel2);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(b.isMouseOver(true) ? ui::dim.withAlpha(0.5f) : ui::line);
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
    juce::Path arrow;
    float ax = w - 16.0f, ay = h * 0.5f;
    arrow.startNewSubPath(ax - 4, ay - 2);
    arrow.lineTo(ax, ay + 2);
    arrow.lineTo(ax + 4, ay - 2);
    g.setColour(ui::dim);
    g.strokePath(arrow, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LinkLookAndFeel::positionComboBoxText(juce::ComboBox& b, juce::Label& l) {
    l.setBounds(8, 1, b.getWidth() - 30, b.getHeight() - 2);
    l.setFont(getComboBoxFont(b));
}

void LinkLookAndFeel::drawBubble(juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body) {
    g.setColour(ui::panel2);
    g.fillRoundedRectangle(body, 5.0f);
    g.setColour(ui::line);
    g.drawRoundedRectangle(body, 5.0f, 1.0f);
}

// ============================================================== pill toggle
PillToggle::PillToggle(const juce::String& label, Icon i) : juce::Button(label), icon(i) {
    setClickingTogglesState(icon != Icon::PopOut && icon != Icon::Gear);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void PillToggle::drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> r, juce::Colour c) {
    g.setColour(c);
    juce::PathStrokeType st(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    auto cx = r.getCentreX(), s = r.getWidth();
    juce::Path p;
    switch (icon) {
        case Icon::Mic: {
            auto cap = juce::Rectangle<float>(s * 0.34f, s * 0.54f).withCentre({ cx, r.getY() + s * 0.33f });
            p.addRoundedRectangle(cap, s * 0.17f);
            juce::Path arc;
            arc.addCentredArc(cx, r.getY() + s * 0.42f, s * 0.3f, s * 0.3f, 0, juce::MathConstants<float>::pi * 0.5f,
                              juce::MathConstants<float>::pi * 1.5f, true);
            p.addPath(arc);
            p.startNewSubPath(cx, r.getY() + s * 0.72f);
            p.lineTo(cx, r.getY() + s * 0.88f);
            p.startNewSubPath(cx - s * 0.18f, r.getY() + s * 0.88f);
            p.lineTo(cx + s * 0.18f, r.getY() + s * 0.88f);
            g.strokePath(p, st);
            break;
        }
        case Icon::Stream: {
            auto cy = r.getCentreY();
            g.fillEllipse(cx - s * 0.08f, cy - s * 0.08f, s * 0.16f, s * 0.16f);
            for (float k : { 0.28f, 0.46f }) {
                juce::Path a, b;
                a.addCentredArc(cx, cy, s * k, s * k, 0, -2.3f, -0.85f, true);
                b.addCentredArc(cx, cy, s * k, s * k, 0, 0.85f, 2.3f, true);
                g.strokePath(a, st);
                g.strokePath(b, st);
            }
            break;
        }
        case Icon::Screen: {
            auto mon = juce::Rectangle<float>(s * 0.86f, s * 0.58f).withCentre({ cx, r.getY() + s * 0.4f });
            p.addRoundedRectangle(mon, s * 0.08f);
            p.startNewSubPath(cx, mon.getBottom());
            p.lineTo(cx, r.getY() + s * 0.86f);
            p.startNewSubPath(cx - s * 0.2f, r.getY() + s * 0.86f);
            p.lineTo(cx + s * 0.2f, r.getY() + s * 0.86f);
            g.strokePath(p, st);
            break;
        }
        case Icon::Cam: {
            auto body = juce::Rectangle<float>(r.getX() + s * 0.04f, r.getCentreY() - s * 0.25f, s * 0.6f, s * 0.5f);
            p.addRoundedRectangle(body, s * 0.1f);
            p.startNewSubPath(body.getRight() + s * 0.04f, r.getCentreY() - s * 0.05f);
            p.lineTo(r.getRight() - s * 0.04f, r.getCentreY() - s * 0.2f);
            p.lineTo(r.getRight() - s * 0.04f, r.getCentreY() + s * 0.2f);
            p.lineTo(body.getRight() + s * 0.04f, r.getCentreY() + s * 0.05f);
            g.strokePath(p, st);
            break;
        }
        case Icon::Gear: {
            auto c = r.getCentre();
            const float ro = s * 0.46f, ri = s * 0.34f;
            juce::Path gear;
            for (int i = 0; i < 16; ++i) {
                float a0 = i * juce::MathConstants<float>::twoPi / 16.0f;
                float rr = (i % 2 == 0) ? ro : ri;
                float a1 = a0 + juce::MathConstants<float>::twoPi / 16.0f;
                auto p0 = c.getPointOnCircumference(rr, a0), p1 = c.getPointOnCircumference(rr, a1);
                if (i == 0) gear.startNewSubPath(p0); else gear.lineTo(p0);
                gear.lineTo(p1);
            }
            gear.closeSubPath();
            g.strokePath(gear, st);
            g.drawEllipse(juce::Rectangle<float>(s * 0.3f, s * 0.3f).withCentre(c), 1.6f);
            break;
        }
        case Icon::Rec: {
            g.fillEllipse(juce::Rectangle<float>(s * 0.62f, s * 0.62f).withCentre(r.getCentre()));
            break;
        }
        case Icon::PopOut: {
            auto box = r.reduced(s * 0.12f);
            p.startNewSubPath(box.getX() + box.getWidth() * 0.45f, box.getY());
            p.lineTo(box.getX(), box.getY());
            p.lineTo(box.getX(), box.getBottom());
            p.lineTo(box.getRight(), box.getBottom());
            p.lineTo(box.getRight(), box.getY() + box.getHeight() * 0.55f);
            p.startNewSubPath(box.getCentreX(), box.getCentreY());
            p.lineTo(box.getRight(), box.getY());
            p.startNewSubPath(box.getRight() - box.getWidth() * 0.35f, box.getY());
            p.lineTo(box.getRight(), box.getY());
            p.lineTo(box.getRight(), box.getY() + box.getHeight() * 0.35f);
            g.strokePath(p, st);
            break;
        }
    }
}

void PillToggle::paintButton(juce::Graphics& g, bool over, bool down) {
    auto r = getLocalBounds().toFloat().reduced(1.5f);
    const bool on = getToggleState();
    const float rad = r.getHeight() * 0.5f;
    if (on) {
        // soft glow
        for (int i = 3; i >= 1; --i) {
            g.setColour((icon == Icon::Rec ? ui::red : ui::accent).withAlpha(0.06f * (float) (4 - i)));
            g.fillRoundedRectangle(r.expanded((float) i), rad + (float) i);
        }
        const auto onCol = icon == Icon::Rec ? ui::red : ui::accent;
        juce::ColourGradient grad(onCol.brighter(0.15f), 0, r.getY(), onCol.darker(0.08f), 0, r.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(r, rad);
        if (down) { g.setColour(juce::Colours::black.withAlpha(0.12f)); g.fillRoundedRectangle(r, rad); }
    } else {
        g.setColour(down ? ui::panel2.darker(0.1f) : (over ? ui::panel2.brighter(0.06f) : ui::panel2));
        g.fillRoundedRectangle(r, rad);
        g.setColour(over ? ui::dim.withAlpha(0.6f) : ui::line);
        g.drawRoundedRectangle(r, rad, 1.0f);
    }
    auto fg = on ? (icon == Icon::Rec ? ui::text : ui::bg) : (over ? ui::text : ui::text.withAlpha(0.85f));
    auto content = r.reduced(rad * 0.55f, 0);
    const float is = juce::jmin(16.0f, r.getHeight() - 12.0f);
    auto text = getButtonText();
    if (text.isEmpty()) {
        drawIcon(g, icon, juce::Rectangle<float>(is, is).withCentre(r.getCentre()), fg);
        return;
    }
    juce::Font f(12.0f, juce::Font::bold);
    f.setExtraKerningFactor(0.08f);
    float tw = f.getStringWidthFloat(text);
    float total = is + 7 + tw;
    float x0 = content.getCentreX() - total * 0.5f;
    drawIcon(g, icon, juce::Rectangle<float>(x0, r.getCentreY() - is * 0.5f, is, is), icon == Icon::Rec && !on ? ui::red : fg);
    g.setColour(fg);
    g.setFont(f);
    g.drawText(text, juce::Rectangle<float>(x0 + is + 7, r.getY(), tw + 4, r.getHeight()), juce::Justification::centredLeft);
}

// ============================================================== level meter
void LevelMeter::push(float lin) {
    level = juce::jmax(lin, level * 0.8f);
    float db = juce::Decibels::gainToDecibels(lin, -100.0f);
    auto now = juce::Time::currentTimeMillis();
    if (db >= holdDb || now - holdT > 1500) {
        holdDb = db;
        holdT = now;
    }
    if (lin >= 0.999f) clip = true;
    repaint();
}

void LevelMeter::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    auto textArea = r.removeFromRight(46.0f);
    auto bar = r.withSizeKeepingCentre(r.getWidth() - 4, 6.0f);
    auto toFrac = [](float db) { return juce::jlimit(0.0f, 1.0f, (db + 48.0f) / 48.0f); };

    g.setColour(ui::line);
    g.fillRoundedRectangle(bar, 3.0f);
    float db = juce::Decibels::gainToDecibels(level, -100.0f);
    float w = bar.getWidth() * toFrac(db);
    if (w > 0.5f) {
        juce::ColourGradient grad(ui::accent, bar.getX(), 0, ui::red, bar.getRight(), 0, false);
        grad.addColour(toFrac(-9.0f), ui::accent);
        grad.addColour(toFrac(-4.0f), ui::amber);
        grad.addColour(toFrac(-1.0f), ui::red);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(bar.withWidth(w), 3.0f);
    }
    // ticks at -6 and 0 dB
    g.setColour(ui::dim.withAlpha(0.5f));
    for (float t : { -6.0f, 0.0f }) {
        float x = bar.getX() + bar.getWidth() * toFrac(t);
        g.fillRect(x - 0.5f, bar.getY() - 3, 1.0f, bar.getHeight() + 6);
    }
    // peak hold marker
    if (holdDb > -48.0f) {
        float x = bar.getX() + bar.getWidth() * toFrac(holdDb);
        g.setColour(holdDb > -1.0f ? ui::red : ui::text.withAlpha(0.8f));
        g.fillRect(x - 1.0f, bar.getY() - 1, 2.0f, bar.getHeight() + 2);
    }
    juce::String t;
    juce::Colour c = ui::text;
    if (clip) { t = "CLIP"; c = ui::red; }
    else if (holdDb <= -60.0f) { t = "-inf"; c = ui::dim; }
    else { t = juce::String(holdDb, 1); c = holdDb > -1.0f ? ui::red : (holdDb > -6.0f ? ui::amber : ui::text); }
    g.setColour(c);
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.5f, clip ? juce::Font::bold : juce::Font::plain));
    g.drawText(t, textArea, juce::Justification::centredRight);
}

// ============================================================== trim knob
TrimKnob::TrimKnob() {
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setRange(-24.0, 12.0, 0.1);
    setValue(0.0, juce::dontSendNotification);
    setDoubleClickReturnValue(true, 0.0);
    setTextValueSuffix(" dB");
    setPopupDisplayEnabled(true, true, nullptr, 1200);
    setMouseDragSensitivity(160);
    setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    setTooltip("Gain trim (double-click = 0 dB)");
    onValueChange = [this] { if (onGain) onGain(juce::Decibels::decibelsToGain((float) getValue(), -100.0f)); };
}

// ============================================================== helpers
static juce::String fmtBytes(juce::uint64 b) {
    if (b < 1024) return juce::String((int) b) + " B";
    if (b < 1024 * 1024) return juce::String(b / 1024.0, 1) + " KB";
    if (b < 1024ull * 1024 * 1024) return juce::String(b / (1024.0 * 1024.0), 1) + " MB";
    return juce::String(b / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
}

static juce::Colour stateColour(LinkState s) {
    switch (s) {
        case LinkState::Connected: return ui::accent;
        case LinkState::Error: return ui::red;
        case LinkState::Idle: return ui::dim;
        default: return ui::amber;
    }
}

static void drawLogo(juce::Graphics& g, juce::Rectangle<int> area, float height) {
    static juce::Image logo = juce::ImageCache::getFromMemory(b2b_logo_png, b2b_logo_png_len);
    if (!logo.isValid()) return;
    float h = height, w = h * logo.getWidth() / (float) logo.getHeight();
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(logo, juce::Rectangle<float>((float) area.getX(), area.getCentreY() - h * 0.5f, w, h), juce::RectanglePlacement::centred);
}

// ============================================================== file list
void FileList::paint(juce::Graphics& g) {
    auto& rows = proc.files;
    if (rows.empty()) {
        g.setColour(ui::dim.withAlpha(0.6f));
        g.setFont(juce::Font(12.5f));
        g.drawFittedText("Drop stems, bounces or zips here.\nReceived files drag straight into your DAW.",
                         getLocalBounds().reduced(16), juce::Justification::centred, 3);
        return;
    }
    for (int i = 0; i < (int) rows.size(); ++i) {
        auto& r = rows[(size_t) i];
        auto b = juce::Rectangle<int>(0, i * rowH, getWidth(), rowH).reduced(2, 3);
        g.setColour(i == hover ? ui::panel2.brighter(0.05f) : ui::panel2);
        g.fillRoundedRectangle(b.toFloat(), 5.0f);
        auto badge = b.removeFromLeft(28);
        g.setColour(r.incoming ? ui::person(r.fromSlot) : ui::dim);
        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.drawText(r.incoming ? juce::String::fromUTF8("\xe2\x86\x93") : juce::String::fromUTF8("\xe2\x86\x91"), badge, juce::Justification::centred);

        auto content = b.reduced(4, 6);
        auto top = content.removeFromTop(16);
        juce::String right;
        juce::Colour rc = ui::dim;
        if (r.midi && r.status != FileRow::Status::Failed) { right = r.midi->summary.fromFirstOccurrenceOf("MIDI  ", false, false); rc = ui::accent; }
        else if (r.status == FileRow::Status::Done) {
            rc = ui::accent;
            right = r.incoming ? juce::String("received")
                               : (r.recipients > 1 ? "delivered " + juce::String(r.delivered) + "/" + juce::String(r.recipients) : juce::String("delivered"));
            if (r.failed > 0) rc = ui::amber;
        }
        else if (r.status == FileRow::Status::Failed) { right = "failed"; rc = ui::red; }
        else right = r.total ? juce::String(int(100.0 * (double) r.bytes / (double) r.total)) + "%" : "0%";
        g.setFont(juce::Font(11.5f));
        g.setColour(rc);
        g.drawText(right, top.removeFromRight(r.midi ? 110 : (r.recipients > 1 ? 92 : 62)), juce::Justification::centredRight);
        g.setColour(ui::text);
        g.setFont(juce::Font(12.5f));
        g.drawText(r.name, top, juce::Justification::centredLeft, true);
        if (r.midi && (r.status == FileRow::Status::Done || !r.incoming)) {
            // mini piano roll instead of the progress bar
            auto roll = content.withTrimmedTop(4).toFloat();
            g.setColour(ui::bg.darker(0.2f));
            g.fillRoundedRectangle(roll, 3.0f);
            auto in = roll.reduced(3.0f, 2.0f);
            const int lo = r.midi->lo - 1, hi = r.midi->hi + 1;
            const float rowH = in.getHeight() / (float) (hi - lo + 1);
            g.setColour(ui::line);
            for (int b = 1; b < r.midi->bars; ++b) g.fillRect(in.getX() + in.getWidth() * b / r.midi->bars, in.getY(), 1.0f, in.getHeight());
            g.setColour(ui::accent);
            for (auto& n : r.midi->notes)
                g.fillRect(in.getX() + n.start * in.getWidth(), in.getBottom() - (n.pitch - lo + 1) * rowH,
                           juce::jmax(2.0f, (n.end - n.start) * in.getWidth() - 1.0f), juce::jmax(2.5f, rowH - 0.5f));
            g.setColour(ui::dim);
            g.setFont(juce::Font(10.5f));
            continue;
        }
        content.removeFromTop(6);
        auto bar = content.removeFromTop(3);
        g.setColour(ui::line);
        g.fillRect(bar);
        double frac = r.status == FileRow::Status::Done ? 1.0 : (double) r.bytes / (double) juce::jmax<juce::uint64>(1, r.total);
        g.setColour(r.status == FileRow::Status::Failed ? ui::red : (r.status == FileRow::Status::Done ? ui::accent.withAlpha(0.5f) : ui::accent));
        g.fillRect(bar.withWidth(juce::roundToInt(bar.getWidth() * frac)));
        content.removeFromTop(4);
        g.setColour(ui::dim);
        g.setFont(juce::Font(11.0f));
        juce::String who = r.incoming ? (r.from.isNotEmpty() ? "  -  from " + r.from : juce::String())
                                      : (r.recipients > 1 ? "  -  to everyone" : juce::String());
        g.drawText(fmtBytes(r.total) + who + (r.status == FileRow::Status::Done && r.incoming ? juce::String("  -  drag to DAW") : juce::String()),
                   content, juce::Justification::centredLeft, true);
    }
}

void FileList::mouseMove(const juce::MouseEvent& e) {
    int h = rowAt(e.y);
    if (h >= (int) proc.files.size()) h = -1;
    if (h != hover) { hover = h; repaint(); }
    bool canDrag = h >= 0 && proc.files[(size_t) h].status == FileRow::Status::Done;
    setMouseCursor(canDrag ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void FileList::mouseDown(const juce::MouseEvent& e) { pressed = rowAt(e.y); dragStarted = false; }

void FileList::mouseDrag(const juce::MouseEvent& e) {
    if (dragStarted || pressed < 0 || pressed >= (int) proc.files.size() || e.getDistanceFromDragStart() < 6) return;
    auto& r = proc.files[(size_t) pressed];
    if (r.status != FileRow::Status::Done || !juce::File(r.path).existsAsFile()) return;
    dragStarted = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({ r.path }, false, this);
}

void FileList::mouseDoubleClick(const juce::MouseEvent& e) {
    int i = rowAt(e.y);
    if (i >= 0 && i < (int) proc.files.size()) {
        juce::File f(proc.files[(size_t) i].path);
        if (f.exists()) f.revealToUser();
    }
}

// ============================================================== video view
VideoView::VideoView(Processor& p, bool pop) : proc(p), popOut(pop) {
    for (auto& a : ver) for (auto& v : a) v = -1;
    startTimerHz(30);
    setTooltip("Click a video to make it big, click again for all. Double-click: big window");
}

juce::Rectangle<float> VideoView::pinArea() const {
    return { getWidth() - 112.0f, 10.0f, 100.0f, 26.0f };
}

void VideoView::mouseDown(const juce::MouseEvent& e) {
    if (popOut && pinArea().contains(e.position)) {
        pinned = !pinned;
        if (onPin) onPin(pinned);
        repaint();
        return;
    }
    if (e.getNumberOfClicks() > 1) return;
    for (auto& [slot, r] : tiles)
        if (r.contains(e.position)) {
            focus = focus == slot ? -1 : slot;
            repaint();
            return;
        }
}

void VideoView::timerCallback() {
    bool changed = false;
    for (int p = 0; p < P; ++p)
        for (int s = 0; s < VideoLink::NumStreams; ++s)
            changed |= proc.video().latest(p, (VideoLink::Stream) s, frame[p][s], ver[p][s]);
    if (changed || proc.video().isSending(VideoLink::Cam)) repaint();
}

static void drawChip(juce::Graphics& g, juce::Rectangle<float> at, const juce::String& t, bool live, juce::Colour dot = ui::red) {
    juce::Font f(11.5f, juce::Font::bold);
    float w = f.getStringWidthFloat(t) + (live ? 30.0f : 18.0f);
    auto c = at.withWidth(juce::jmin(w, juce::jmax(40.0f, at.getWidth()))).withHeight(22.0f);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRoundedRectangle(c, 11.0f);
    if (live) { g.setColour(dot); g.fillEllipse(c.getX() + 10, c.getCentreY() - 3.5f, 7, 7); }
    g.setColour(ui::text);
    g.setFont(f);
    g.drawText(t, c.withTrimmedLeft(live ? 22.0f : 9.0f).withTrimmedRight(6.0f), juce::Justification::centredLeft, true);
}

void VideoView::drawTile(juce::Graphics& g, int p, juce::Rectangle<float> r, bool big) {
    auto& v = proc.video();
    const bool scr = frame[p][VideoLink::Screen].isValid() && v.remoteActive(p, VideoLink::Screen);
    const bool cam = frame[p][VideoLink::Cam].isValid() && v.remoteActive(p, VideoLink::Cam);
    g.setColour(juce::Colours::black.withAlpha(popOut ? 1.0f : 0.35f));
    g.fillRoundedRectangle(r, 5.0f);
    auto& main = scr ? frame[p][VideoLink::Screen] : frame[p][VideoLink::Cam];
    if (main.isValid()) {
        auto placed = juce::RectanglePlacement(juce::RectanglePlacement::centred).appliedTo(main.getBounds().toFloat(), r);
        g.drawImage(main, placed);
    }
    if (scr && cam) {
        auto& c = frame[p][VideoLink::Cam];
        float pw = juce::jlimit(70.0f, 300.0f, r.getWidth() * 0.22f), ph = pw * c.getHeight() / (float) c.getWidth();
        auto pip = juce::Rectangle<float>(r.getRight() - pw - 8, r.getBottom() - ph - 8, pw, ph);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRoundedRectangle(pip.expanded(2), 5.0f);
        g.drawImage(c, pip);
    }
    const auto col = ui::person(p);
    g.setColour(col.withAlpha(big || focus < 0 ? 0.55f : 0.35f));
    g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.2f);
    // talking light: their voice level
    const float vp = proc.audio().peerPeak[p][AudioLink::Voice].load();
    if (vp > 0.03f) {
        g.setColour(col.withAlpha(juce::jlimit(0.3f, 1.0f, vp * 3.0f)));
        g.drawRoundedRectangle(r.reduced(1.0f), 5.0f, 2.5f);
    }
    drawChip(g, r.reduced(8).withHeight(22), proc.nameOf(p) + (scr ? "'s screen" : "'s camera"), true, col);
}

void VideoView::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(popOut ? juce::Colours::black : ui::bg.darker(0.2f));
    g.fillRoundedRectangle(r, popOut ? 0.0f : 6.0f);
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    auto& v = proc.video();
    tiles.clear();

    std::vector<int> live;
    for (int p = 0; p < P; ++p)
        if (proc.people[p].present
            && ((frame[p][0].isValid() && v.remoteActive(p, VideoLink::Screen)) || (frame[p][1].isValid() && v.remoteActive(p, VideoLink::Cam))))
            live.push_back(p);
    if (focus >= 0 && std::find(live.begin(), live.end(), focus) == live.end()) focus = -1;

    auto area = r.reduced(popOut ? 0.0f : 4.0f);
    const float gap = 6.0f;
    if (live.empty()) {
        auto c = r.withSizeKeepingCentre(380, 110);
        PillToggle::drawIcon(g, PillToggle::Icon::Screen, juce::Rectangle<float>(34, 34).withCentre({ c.getCentreX(), c.getY() + 18 }),
                             ui::dim.withAlpha(0.6f));
        g.setColour(ui::text.withAlpha(0.8f));
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText(v.isSending(VideoLink::Screen) ? "You're sharing your screen" : "No video yet", c.withTrimmedTop(46).withHeight(20),
                   juce::Justification::centred);
        g.setColour(ui::dim);
        g.setFont(juce::Font(12.5f));
        g.drawFittedText(v.isSending(VideoLink::Screen) ? juce::String("Everyone in the room sees it live. Your own view stays clean.")
                                                        : juce::String("Share your screen or turn on your camera.\nEveryone's video shows up here - up to 3 at once."),
                         c.withTrimmedTop(70).toNearestInt(), juce::Justification::centredTop, 2);
    } else if (focus >= 0 && live.size() > 1) {
        // focused: one big, the rest as a strip along the bottom
        auto strip = area.removeFromBottom(juce::jlimit(70.0f, 170.0f, area.getHeight() * 0.22f));
        area.removeFromBottom(gap);
        tiles.push_back({ focus, area });
        std::vector<int> rest;
        for (int p : live) if (p != focus) rest.push_back(p);
        const float tw = juce::jmin(strip.getHeight() * 16.0f / 9.0f, (strip.getWidth() - gap * (float) (rest.size() - 1)) / (float) rest.size());
        float x = strip.getCentreX() - (tw * (float) rest.size() + gap * (float) (rest.size() - 1)) * 0.5f;
        for (int p : rest) { tiles.push_back({ p, { x, strip.getY(), tw, strip.getHeight() } }); x += tw + gap; }
    } else if (live.size() == 1) {
        tiles.push_back({ live[0], area });
    } else if (live.size() == 2) {
        const bool wide = area.getWidth() / area.getHeight() > 1.2f;
        auto a = area;
        auto b = wide ? a.removeFromRight((a.getWidth() - gap) * 0.5f) : a.removeFromBottom((a.getHeight() - gap) * 0.5f);
        if (wide) a.removeFromRight(gap); else a.removeFromBottom(gap);
        tiles.push_back({ live[0], a });
        tiles.push_back({ live[1], b });
    } else {
        // 3: two on top, one centred below (2x2 without the empty cell)
        auto top = area.removeFromTop((area.getHeight() - gap) * 0.5f);
        area.removeFromTop(gap);
        auto l = top.removeFromLeft((top.getWidth() - gap) * 0.5f);
        top.removeFromLeft(gap);
        tiles.push_back({ live[0], l });
        tiles.push_back({ live[1], top });
        tiles.push_back({ live[2], area.withSizeKeepingCentre(l.getWidth(), area.getHeight()) });
    }
    for (auto& [p, tr] : tiles) drawTile(g, p, tr, p == focus || tiles.size() == 1);

    // own camera self-view
    if (v.isSending(VideoLink::Cam)) {
        auto self = v.localPreview(VideoLink::Cam);
        if (self.isValid()) {
            float pw = juce::jlimit(100.0f, 190.0f, r.getWidth() * 0.15f), ph = pw * self.getHeight() / (float) self.getWidth();
            auto sv = juce::Rectangle<float>(r.getX() + 12, r.getBottom() - ph - 12, pw, ph);
            g.drawImage(self, sv);
            g.setColour(ui::accent.withAlpha(0.7f));
            g.drawRoundedRectangle(sv.expanded(1), 4.0f, 1.0f);
            drawChip(g, sv.withTrimmedTop(-28.0f).withHeight(22.0f), "You", false);
        }
    }
    if (v.isSending(VideoLink::Screen) && !live.empty())
        drawChip(g, juce::Rectangle<float>(r.getRight() - (popOut ? 290.0f : 160.0f), r.getBottom() - 32, 150, 22), "You're sharing", true, ui::accent);

    if (popOut) {
        auto pa = pinArea();
        g.setColour(pinned ? ui::accent : juce::Colours::black.withAlpha(0.6f));
        g.fillRoundedRectangle(pa, 13.0f);
        if (!pinned) { g.setColour(ui::line); g.drawRoundedRectangle(pa.reduced(0.5f), 13.0f, 1.0f); }
        g.setColour(pinned ? ui::bg : ui::text);
        g.setFont(juce::Font(11.5f, juce::Font::bold).withExtraKerningFactor(0.08f));
        g.drawText(pinned ? "ON TOP" : "PIN ON TOP", pa, juce::Justification::centred);
    }
}

// ============================================================== people
namespace {
// popover: one person's volume + mute
class PersonMixer : public juce::Component {
public:
    PersonMixer(Processor& p, int s) : proc(p), slot(s) {
        vol.setSliderStyle(juce::Slider::LinearHorizontal);
        vol.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        vol.setRange(0.0, 1.5, 0.01);
        vol.setDoubleClickReturnValue(true, 1.0);
        vol.setValue(proc.audio().personGain[slot].load(), juce::dontSendNotification);
        vol.onValueChange = [this] { proc.audio().personGain[slot] = (float) vol.getValue(); repaint(); };
        mute.setClickingTogglesState(true);
        mute.setToggleState(proc.audio().personMute[slot].load(), juce::dontSendNotification);
        mute.onClick = [this] { proc.audio().personMute[slot] = mute.getToggleState(); mute.setButtonText(mute.getToggleState() ? "MUTED" : "MUTE"); };
        mute.setButtonText(mute.getToggleState() ? "MUTED" : "MUTE");
        addAndMakeVisible(vol);
        addAndMakeVisible(mute);
        setSize(250, 96);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(12, 10);
        r.removeFromTop(26);
        auto row = r.removeFromTop(28);
        mute.setBounds(row.removeFromRight(70));
        row.removeFromRight(8);
        vol.setBounds(row);
    }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().reduced(12, 10);
        auto top = r.removeFromTop(20);
        g.setColour(ui::person(slot));
        g.fillEllipse((float) top.getX(), top.getCentreY() - 4.0f, 8, 8);
        g.setColour(ui::text);
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(proc.nameOf(slot), top.withTrimmedLeft(14), juce::Justification::centredLeft, true);
        g.setColour(ui::dim);
        g.setFont(juce::Font(11.0f));
        auto& pp = proc.people[slot];
        g.drawText(juce::String(juce::roundToInt(vol.getValue() * 100)) + "%" + (pp.pingMs >= 0 ? "   " + juce::String(pp.pingMs) + " ms" : juce::String()),
                   top, juce::Justification::centredRight);
        g.drawText("their voice + DAW volume, only for you", r.withTrimmedTop(4), juce::Justification::centredLeft);
    }

private:
    Processor& proc;
    int slot;
    juce::Slider vol;
    juce::TextButton mute;
};
} // namespace

int PeopleStrip::preferredWidth() const {
    int w = 0;
    juce::Font f(12.0f, juce::Font::bold);
    for (int p = 0; p < AudioLink::kPeers; ++p)
        if (proc.people[p].present) w += f.getStringWidth(proc.nameOf(p)) + 40;
    return w;
}

void PeopleStrip::paint(juce::Graphics& g) {
    chips.clear();
    float x = 0;
    juce::Font f(12.0f, juce::Font::bold);
    auto& au = proc.audio();
    for (int p = 0; p < AudioLink::kPeers; ++p) {
        if (!proc.people[p].present) continue;
        auto name = proc.nameOf(p);
        float w = juce::jmin((float) f.getStringWidth(name) + 34.0f, (float) getWidth() - x);
        if (w < 30) break;
        juce::Rectangle<float> c(x, getHeight() * 0.5f - 13.0f, w, 26.0f);
        chips.push_back({ p, c });
        const auto col = ui::person(p);
        const bool muted = au.personMute[p].load();
        g.setColour(ui::panel2);
        g.fillRoundedRectangle(c, 13.0f);
        const float talk = au.peerPeak[p][AudioLink::Voice].load();
        g.setColour(talk > 0.03f ? col : ui::line);
        g.drawRoundedRectangle(c.reduced(0.5f), 13.0f, talk > 0.03f ? 1.6f : 1.0f);
        g.setColour(muted ? ui::red : col);
        g.fillEllipse(c.getX() + 10, c.getCentreY() - 4, 8, 8);
        g.setColour(muted ? ui::dim : ui::text);
        g.setFont(f);
        g.drawText(name, c.withTrimmedLeft(24).withTrimmedRight(8), juce::Justification::centredLeft, true);
        x += w + 6;
    }
}

void PeopleStrip::mouseDown(const juce::MouseEvent& e) {
    for (auto& [slot, c] : chips)
        if (c.contains(e.position)) {
            auto* top = getTopLevelComponent();
            auto area = top->getLocalArea(this, c.toNearestInt());
            juce::CallOutBox::launchAsynchronously(std::make_unique<PersonMixer>(proc, slot), area, top);
            return;
        }
}

VideoWindow::VideoWindow(Processor& p) : juce::DocumentWindow("b2b  -  live view", juce::Colours::black, juce::DocumentWindow::allButtons) {
    setUsingNativeTitleBar(true);
    auto* view = new VideoView(p, true);
    view->setTooltip("Click ON TOP to pin or unpin this window above your DAW");
    view->pinned = p.keepOnTop;
    view->onPin = [this, &p](bool on) { setAlwaysOnTop(on); p.keepOnTop = on; };
    setContentOwned(view, false);
    setAlwaysOnTop(p.keepOnTop); // stay visible while you work in the DAW
    setResizable(true, false);
    setResizeLimits(480, 270, 4000, 3000);
    centreWithSize(1280, 760);
}

// ============================================================== settings panel
SettingsPanel::SettingsPanel(Processor& p) : proc(p) {
    qualityBox.addItemList({ "96 kbps (light)", "128 kbps", "192 kbps (default)", "256 kbps", "320 kbps (best)" }, 1);
    fpsBox.addItemList({ "5 fps", "10 fps (default)", "15 fps", "24 fps" }, 1);
    resBox.addItemList({ "720p - smoother on slow internet", "1080p - sharpest text" }, 1);
    qualityBox.onChange = [this] {
        const int k[] = { 96, 128, 192, 256, 320 };
        proc.audio().bitrateKbps = k[juce::jlimit(0, 4, qualityBox.getSelectedItemIndex())];
    };
    fpsBox.onChange = [this] {
        const int f[] = { 5, 10, 15, 24 };
        proc.video().screenFps = f[juce::jlimit(0, 3, fpsBox.getSelectedItemIndex())];
    };
    resBox.onChange = [this] { proc.video().screenHD = resBox.getSelectedItemIndex() == 1; };
    onTopToggle.onClick = [this] { proc.keepOnTop = onTopToggle.getToggleState(); };
    autoSendToggle.onClick = [this] { proc.autoSendRecordings = autoSendToggle.getToggleState(); };
    autoSendToggle.setColour(juce::ToggleButton::textColourId, ui::text);
    autoSendToggle.setColour(juce::ToggleButton::tickColourId, ui::accent);
    autoSendToggle.setColour(juce::ToggleButton::tickDisabledColourId, ui::dim);
    onTopToggle.setColour(juce::ToggleButton::textColourId, ui::text);
    onTopToggle.setColour(juce::ToggleButton::tickColourId, ui::accent);
    onTopToggle.setColour(juce::ToggleButton::tickDisabledColourId, ui::dim);
    openFolderBtn.onClick = [this] { proc.receiveFolder().revealToUser(); };
    changeFolderBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Where should received files go?", proc.receiveFolder(), "");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                             [this](const juce::FileChooser& fc) {
                                 auto f = fc.getResult();
                                 if (f.isDirectory()) { proc.setReceiveFolder(f); repaint(); }
                             });
    };
    doneBtn.getProperties().set("primary", true);
    doneBtn.setColour(juce::TextButton::textColourOffId, ui::bg);
    doneBtn.onClick = [this] { if (onClose) onClose(); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &qualityBox, &fpsBox, &resBox, &changeFolderBtn, &openFolderBtn, &doneBtn, &onTopToggle, &autoSendToggle })
        addAndMakeVisible(c);
    refresh();
}

void SettingsPanel::refresh() {
    const int k = proc.audio().bitrateKbps.load();
    qualityBox.setSelectedItemIndex(k <= 96 ? 0 : k <= 128 ? 1 : k <= 192 ? 2 : k <= 256 ? 3 : 4, juce::dontSendNotification);
    const int f = proc.video().screenFps.load();
    fpsBox.setSelectedItemIndex(f <= 5 ? 0 : f <= 10 ? 1 : f <= 15 ? 2 : 3, juce::dontSendNotification);
    resBox.setSelectedItemIndex(proc.video().screenHD.load() ? 1 : 0, juce::dontSendNotification);
    onTopToggle.setToggleState(proc.keepOnTop, juce::dontSendNotification);
    autoSendToggle.setToggleState(proc.autoSendRecordings, juce::dontSendNotification);
    repaint();
}

juce::Rectangle<int> SettingsPanel::card() const { return getLocalBounds().withSizeKeepingCentre(560, 500); }

void SettingsPanel::mouseDown(const juce::MouseEvent& e) {
    if (!card().contains(e.getPosition()) && onClose) onClose(); // click outside closes
}

void SettingsPanel::resized() {
    auto c = card().reduced(28, 24);
    c.removeFromTop(44);
    auto row = [&](int h) { auto r = c.removeFromTop(h); c.removeFromTop(6); return r; };
    row(18); // AUDIO label
    qualityBox.setBounds(row(34).withTrimmedLeft(190));
    c.removeFromTop(14);
    row(18); // SCREEN label
    fpsBox.setBounds(row(34).withTrimmedLeft(190));
    resBox.setBounds(row(34).withTrimmedLeft(190));
    c.removeFromTop(14);
    row(18); // FILES label
    auto fr = row(34);
    openFolderBtn.setBounds(fr.removeFromRight(70));
    fr.removeFromRight(6);
    changeFolderBtn.setBounds(fr.removeFromRight(86));
    c.removeFromTop(14);
    row(18); // OPTIONS label
    onTopToggle.setBounds(row(26));
    autoSendToggle.setBounds(row(26));
    doneBtn.setBounds(card().reduced(28, 22).removeFromBottom(38).removeFromRight(110));
}

void SettingsPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black.withAlpha(0.55f));
    auto cr = card();
    g.setColour(ui::panel);
    g.fillRoundedRectangle(cr.toFloat(), 12.0f);
    g.setColour(ui::line);
    g.drawRoundedRectangle(cr.toFloat().reduced(0.5f), 12.0f, 1.0f);
    auto c = cr.reduced(28, 24);
    g.setColour(ui::text);
    g.setFont(juce::Font(18.0f, juce::Font::bold));
    g.drawText("Settings", c.removeFromTop(30), juce::Justification::centredLeft);
    c.removeFromTop(14);
    auto section = [&](const juce::String& t) {
        g.setColour(ui::dim);
        g.setFont(juce::Font(11.0f, juce::Font::bold).withExtraKerningFactor(0.12f));
        g.drawText(t, c.removeFromTop(18), juce::Justification::centredLeft);
        c.removeFromTop(6);
    };
    auto label = [&](const juce::String& t) {
        auto r = c.removeFromTop(34);
        c.removeFromTop(6);
        g.setColour(ui::text);
        g.setFont(juce::Font(13.0f));
        g.drawText(t, r.withWidth(185), juce::Justification::centredLeft);
        return r;
    };
    section("AUDIO");
    label("DAW stream quality");
    c.removeFromTop(14);
    section("SCREEN SHARE");
    label("Frame rate");
    label("Resolution");
    c.removeFromTop(14);
    section("RECEIVED FILES");
    auto fr = label("");
    g.setColour(ui::text.withAlpha(0.85f));
    g.setFont(juce::Font(12.0f));
    g.drawText(proc.receiveFolder().getFullPathName(), fr.withTrimmedRight(172), juce::Justification::centredLeft, true);
    c.removeFromTop(14);
    section("OPTIONS");
}

// ============================================================== editor
Editor::Editor(Processor& p) : AudioProcessorEditor(p), proc(p), settings(p), stage(p), people(p), fileList(p) {
    setLookAndFeel(&lnf);
    setResizable(true, true);
    setResizeLimits(920, 600, 2600, 1700);
    setSize(juce::jlimit(920, 2600, proc.editorW), juce::jlimit(600, 1700, proc.editorH));

    auto setupLabel = [this](juce::Label& l, const juce::String& t) {
        l.setText(t, juce::dontSendNotification);
        l.setFont(juce::Font(11.0f, juce::Font::bold).withExtraKerningFactor(0.12f));
        addChildComponent(l);
    };
    setupLabel(nameLabel, "YOUR NAME");
    setupLabel(orLabel, "OR JOIN A SESSION");

    auto setupEdit = [this](juce::TextEditor& t, float size) {
        t.setFont(juce::Font(size));
        t.setIndents(10, 0);
        t.setJustification(juce::Justification::centredLeft);
        addChildComponent(t);
    };
    setupEdit(nameEdit, 15.0f);
    nameEdit.setText(proc.myName, false);
    nameEdit.setInputRestrictions(32);
    nameEdit.onTextChange = [this] { proc.myName = nameEdit.getText().trim().isEmpty() ? juce::String("Producer") : nameEdit.getText().trim(); };
    setupEdit(codeEdit, 20.0f);
    codeEdit.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 20.0f, juce::Font::bold));
    codeEdit.setTextToShowWhenEmpty("ABC-123", ui::dim.withAlpha(0.5f));
    codeEdit.setInputRestrictions(9, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789- ");
    codeEdit.onReturnKey = [this] { doJoin(); };

    hostBtn.getProperties().set("primary", true);
    sendBtn.getProperties().set("primary", true);
    leaveBtn.getProperties().set("danger", true);
    hostBtn.setColour(juce::TextButton::textColourOffId, ui::bg);
    sendBtn.setColour(juce::TextButton::textColourOffId, ui::bg);
    for (auto* b : { &hostBtn, &joinBtn, &copyBtn, &cancelBtn, &sendBtn, &attachBtn, &folderBtn, &leaveBtn }) addChildComponent(b);

    hostBtn.onClick = [this] { proc.host(); };
    joinBtn.onClick = [this] { doJoin(); };
    copyBtn.onClick = [this] {
        juce::SystemClipboard::copyTextToClipboard(proc.code);
        copyBtn.setButtonText("COPIED");
        juce::Timer::callAfterDelay(1200, [safe = juce::Component::SafePointer<Editor>(this)] { if (safe) safe->copyBtn.setButtonText("COPY CODE"); });
    };
    cancelBtn.onClick = [this] { proc.leave(); };
    leaveBtn.onClick = [this] { proc.leave(); };
    folderBtn.onClick = [this] { proc.receiveFolder().revealToUser(); };
    attachBtn.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser>("Send files or a folder", juce::File(), "*");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::canSelectDirectories | juce::FileBrowserComponent::canSelectMultipleItems,
                             [this](const juce::FileChooser& fc) {
                                 juce::StringArray paths;
                                 for (auto& f : fc.getResults()) paths.add(f.getFullPathName());
                                 if (!paths.isEmpty()) proc.sendFiles(paths);
                             });
    };

    // ---- audio bar
    auto& au = proc.audio();
    micBtn.setToggleState(au.micOn.load(), juce::dontSendNotification);
    streamBtn.setToggleState(au.streamInput.load(), juce::dontSendNotification);
    micBtn.setTooltip("Talk to the room (use headphones)");
    streamBtn.setTooltip("Send what comes into this plugin. Put b2b on the master to stream your mix.");
    micBtn.onClick = [this] { proc.setMicEnabled(micBtn.getToggleState()); micBtn.setToggleState(proc.audio().micOn.load(), juce::dontSendNotification); };
    streamBtn.onClick = [this] { proc.audio().streamInput = streamBtn.getToggleState(); };
    appBtn.setTooltip("APP AUDIO for Ableton: also send your browser / sample preview (it skips plugins on the master). "
                      "b2b subtracts its own output, so nobody hears an echo. Mac: needs Screen Recording permission + 48 kHz. Not needed in FL.");
    appBtn.onClick = [this] { proc.setAppAudio(appBtn.getToggleState()); appBtn.setToggleState(proc.audio().appAudioOn(), juce::dontSendNotification); };
    auto mics = au.micDevices();
    micBox.addItemList(mics.isEmpty() ? juce::StringArray { "No mic found" } : mics, 1);
    int sel = mics.indexOf(proc.micName);
    micBox.setSelectedItemIndex(sel >= 0 ? sel : 0, juce::dontSendNotification);
    micBox.onChange = [this] { if (micBox.getText() != "No mic found") proc.selectMic(micBox.getText()); };
    micTrim.setGain(au.micGain.load());
    streamTrim.setGain(au.streamGain.load());
    micTrim.onGain = [this](float gn) { proc.audio().micGain = gn; };
    streamTrim.onGain = [this](float gn) { proc.audio().streamGain = gn; };
    micMeter.setTooltip("Mic level sent (dBFS). Click to reset the clip warning.");
    streamMeter.setTooltip("DAW level sent (dBFS). Click to reset the clip warning.");
    inMeter.setTooltip("Incoming level (dBFS)");
    auto setupRecv = [this, &au](juce::Slider& sl, AudioLink::Stream st, const juce::String& tip) {
        sl.setSliderStyle(juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        sl.setRange(0.0, 1.5, 0.01);
        sl.setValue(au.recvGain[st].load(), juce::dontSendNotification);
        sl.setDoubleClickReturnValue(true, 1.0);
        sl.onValueChange = [this, &sl, st] { proc.audio().recvGain[st] = (float) sl.getValue(); };
        sl.setTooltip(tip);
    };
    setupRecv(voiceSlider, AudioLink::Voice, "Everyone's voice volume (double-click = 100%). Per person: click their name.");
    setupRecv(dawSlider, AudioLink::Daw, "Everyone's DAW stream volume (double-click = 100%). Per person: click their name.");

    codeChip.setTooltip("Room code - click to copy. Others can join any time (4 people max)");
    codeChip.onClick = [this] {
        juce::SystemClipboard::copyTextToClipboard(proc.code);
        codeChip.setButtonText("COPIED");
        juce::Timer::callAfterDelay(1200, [safe = juce::Component::SafePointer<Editor>(this)] { if (safe) safe->codeChip.setButtonText(safe->proc.code); });
    };
    addChildComponent(codeChip);
    people.setTooltip("Who's in the room. Click someone for their volume / mute");

    recBtn.setTooltip("Record your master + mic and everyone's voice + DAW as separate WAVs");
    recBtn.onClick = [this] { proc.toggleRecording(); recBtn.setToggleState(proc.audio().isRecording(), juce::dontSendNotification); };
    addChildComponent(recBtn);
    sendRecBtn.setTooltip("Send the last recording to everyone in the room (arrives in their FILES)");
    sendRecBtn.getProperties().set("primary", true);
    sendRecBtn.setColour(juce::TextButton::textColourOffId, ui::bg);
    sendRecBtn.onClick = [this] { proc.sendRecording(); };
    addChildComponent(sendRecBtn);
    gearBtn.setTooltip("Settings");
    gearBtn.setComponentID("gear");
    gearBtn.onClick = [this] {
        settings.refresh();
        settings.setVisible(true);
        settings.toFront(true);
    };
    settings.onClose = [this] { settings.setVisible(false); };

    // ---- stage
    screenBtn.setTooltip("Share your screen");
    camBtn.setTooltip("Turn on your camera");
    popBtn.setTooltip("Open video in a big window");
    screenBtn.onClick = [this] { proc.setScreenShare(screenBtn.getToggleState()); };
    camBtn.onClick = [this] { proc.setCam(camBtn.getToggleState()); camBtn.setToggleState(proc.camOn, juce::dontSendNotification); };
    popBtn.onClick = [this] { popOutVideo(); };
    stage.onPopOut = [this] { popOutVideo(); };
    screenBox.addItemList(proc.video().screenNames(), 1);
    screenBox.setSelectedItemIndex(juce::jlimit(0, juce::jmax(0, screenBox.getNumItems() - 1), proc.video().screenIndex.load()), juce::dontSendNotification);
    screenBox.onChange = [this] { proc.video().screenIndex = screenBox.getSelectedItemIndex(); };
    auto cams = proc.camNames();
    camBox.addItemList(cams.isEmpty() ? juce::StringArray { "No camera" } : cams, 1);
    int cs = cams.indexOf(proc.camName);
    camBox.setSelectedItemIndex(cs >= 0 ? cs : 0, juce::dontSendNotification);
    camBox.onChange = [this] { if (camBox.getText() != "No camera") proc.selectCam(camBox.getText()); };

    for (juce::Component* c : std::initializer_list<juce::Component*> { &micBtn, &streamBtn, &micBox, &micMeter, &streamMeter, &inMeter, &micTrim,
                                                                         &streamTrim, &appBtn, &voiceSlider, &dawSlider, &screenBtn, &camBtn, &screenBox, &camBox, &popBtn, &stage, &people })
        addChildComponent(c);
    addAndMakeVisible(gearBtn);
    addChildComponent(settings);

    // ---- chat + files
    chatLog.setMultiLine(true, true);
    chatLog.setReadOnly(true);
    chatLog.setCaretVisible(false);
    chatLog.setScrollbarsShown(true);
    chatLog.setColour(juce::TextEditor::backgroundColourId, ui::panel);
    chatLog.setIndents(10, 4);
    addChildComponent(chatLog);
    setupEdit(chatInput, 14.0f);
    chatInput.setTextToShowWhenEmpty("Message...", ui::dim.withAlpha(0.6f));
    chatInput.onReturnKey = [this] { sendChat(); };
    sendBtn.onClick = [this] { sendChat(); };
    fileView.setViewedComponent(&fileList, false);
    fileView.setScrollBarsShown(true, false);
    fileView.setScrollBarThickness(6);
    addChildComponent(fileView);

    refreshState();
    startTimerHz(30);
}

Editor::~Editor() {
    stopTimer();
    videoWindow.reset();
    setLookAndFeel(nullptr);
}

void Editor::popOutVideo() {
    if (!videoWindow) videoWindow = std::make_unique<VideoWindow>(proc);
    videoWindow->setAlwaysOnTop(proc.keepOnTop);
    if (auto* vv = dynamic_cast<VideoView*>(videoWindow->getContentComponent())) { vv->pinned = proc.keepOnTop; vv->repaint(); }
    videoWindow->setVisible(true);
    videoWindow->toFront(true);
}

void Editor::doJoin() {
    auto c = codeEdit.getText();
    if (smash::LinkEngine::normalizeCode(c.toStdString()).size() != 6) {
        codeEdit.setColour(juce::TextEditor::outlineColourId, ui::red);
        codeEdit.repaint();
        return;
    }
    codeEdit.setColour(juce::TextEditor::outlineColourId, ui::line);
    proc.join(c);
}

void Editor::sendChat() {
    auto t = chatInput.getText();
    if (t.trim().isEmpty()) return;
    proc.sendChat(t);
    chatInput.clear();
}

void Editor::timerCallback() {
    if (seenState != proc.stateVersion) refreshState();
    if (seenChat != proc.chatVersion) refreshChat();
    if (seenFiles != proc.fileVersion) refreshFiles();
    if (proc.state == LinkState::Connected) {
        auto& au = proc.audio();
        micMeter.push(au.micOn.load() ? au.micPeak.load() : 0.0f);
        streamMeter.push(au.streamInput.load() ? au.sendPeak.load() : 0.0f);
        inMeter.push(juce::jmax(au.recvPeak[AudioLink::Daw].load(), au.recvPeak[AudioLink::Voice].load()));
        repaint(header);
        people.repaint();
        if (screenBtn.getToggleState() != proc.video().isSending(VideoLink::Screen))
            screenBtn.setToggleState(proc.video().isSending(VideoLink::Screen), juce::dontSendNotification);
    }
    {
        const bool r = proc.audio().isRecording();
        juce::String t = "REC";
        if (r) {
            int sec = (int) proc.audio().recordSeconds();
            t = juce::String::formatted("%02d:%02d", sec / 60, sec % 60);
        }
        if (recBtn.getButtonText() != t) recBtn.setButtonText(t);
        if (recBtn.getToggleState() != r) recBtn.setToggleState(r, juce::dontSendNotification);
    }
    if (getWidth() != proc.editorW || getHeight() != proc.editorH) { proc.editorW = getWidth(); proc.editorH = getHeight(); }
}

void Editor::refreshState() {
    seenState = proc.stateVersion;
    auto s = proc.state;
    bool connectView = s == LinkState::Idle || s == LinkState::Error || (s == LinkState::Connecting && proc.code.isEmpty());
    bool waitingView = !connectView && s != LinkState::Connected;
    bool sessionView = s == LinkState::Connected;

    for (juce::Component* c : std::initializer_list<juce::Component*> { &nameLabel, &nameEdit, &hostBtn, &orLabel, &codeEdit, &joinBtn })
        c->setVisible(connectView);
    if (connectView && nameEdit.getText() != proc.myName && !nameEdit.hasKeyboardFocus(false)) nameEdit.setText(proc.myName, false);
    copyBtn.setVisible(waitingView && s == LinkState::Waiting);
    cancelBtn.setVisible(waitingView);
    micBtn.setToggleState(proc.audio().micOn.load(), juce::dontSendNotification);
    camBtn.setToggleState(proc.camOn, juce::dontSendNotification);
    appBtn.setToggleState(proc.audio().appAudioOn(), juce::dontSendNotification);
    screenBtn.setToggleState(proc.video().isSending(VideoLink::Screen), juce::dontSendNotification);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &chatLog, &chatInput, &sendBtn, &attachBtn, &leaveBtn, &fileView, &micBtn,
                                                                         &streamBtn, &micBox, &micMeter, &streamMeter, &inMeter, &micTrim, &streamTrim, &appBtn,
                                                                         &voiceSlider, &dawSlider, &screenBtn, &camBtn, &screenBox, &camBox, &popBtn, &stage, &people })
        c->setVisible(sessionView);
    codeChip.setVisible(sessionView && proc.code.isNotEmpty());
    if (codeChip.getButtonText() != "COPIED") codeChip.setButtonText(proc.code);
    folderBtn.setVisible(true);
    recBtn.setVisible(sessionView || proc.audio().isRecording());
    sendRecBtn.setVisible(sessionView && !proc.audio().isRecording() && proc.lastRecording.isDirectory() && !proc.lastRecordingSent);
    recBtn.setToggleState(proc.audio().isRecording(), juce::dontSendNotification);
    if (!sessionView && videoWindow) videoWindow->setVisible(false);
    resized();
    repaint();
}

void Editor::refreshChat() {
    seenChat = proc.chatVersion;
    chatLog.clear();
    juce::Font nameFont(12.5f, juce::Font::bold), textFont(13.5f), sysFont(11.5f, juce::Font::italic);
    for (auto& l : proc.chat) {
        chatLog.moveCaretToEnd();
        if (l.system) {
            chatLog.setFont(sysFont);
            chatLog.setColour(juce::TextEditor::textColourId, ui::dim);
            chatLog.insertTextAtCaret(l.time + "  " + l.text + "\n\n");
            continue;
        }
        chatLog.setFont(nameFont);
        chatLog.setColour(juce::TextEditor::textColourId, l.mine ? ui::dim : ui::person(l.slot));
        chatLog.insertTextAtCaret((l.mine ? juce::String("You") : l.from) + "  ");
        chatLog.setFont(juce::Font(10.5f));
        chatLog.setColour(juce::TextEditor::textColourId, ui::dim.withAlpha(0.6f));
        chatLog.insertTextAtCaret(l.time + "\n");
        chatLog.setFont(textFont);
        chatLog.setColour(juce::TextEditor::textColourId, ui::text);
        chatLog.insertTextAtCaret(l.text + "\n\n");
    }
    chatLog.moveCaretToEnd();
}

void Editor::refreshFiles() {
    seenFiles = proc.fileVersion;
    fileList.setSize(juce::jmax(1, fileView.getWidth() - 8), juce::jmax(fileView.getHeight(), (int) proc.files.size() * FileList::rowH));
    fileList.repaint();
}

bool Editor::isInterestedInFileDrag(const juce::StringArray&) { return proc.state == LinkState::Connected; }

void Editor::filesDropped(const juce::StringArray& files, int, int) {
    dragOver = false;
    repaint();
    proc.sendFiles(files);
}

void Editor::resized() {
    auto r = getLocalBounds();
    header = r.removeFromTop(52);
    auto body = r.reduced(16, 12);
    auto hb = header.reduced(16, 11);
    folderBtn.setBounds(hb.removeFromRight(108));
    hb.removeFromRight(8);
    gearBtn.setBounds(hb.removeFromRight(30));
    hb.removeFromRight(8);
    leaveBtn.setBounds(hb.removeFromRight(78));
    hb.removeFromRight(8);
    recBtn.setBounds(hb.removeFromRight(112));
    hb.removeFromRight(8);
    if (sendRecBtn.isVisible()) { sendRecBtn.setBounds(hb.removeFromRight(96)); hb.removeFromRight(8); }
    codeChip.setBounds(hb.removeFromRight(96));
    settings.setBounds(getLocalBounds());

    if (nameEdit.isVisible()) {
        connectCard = body.withSizeKeepingCentre(360, 260);
        auto card = connectCard;
        nameLabel.setBounds(card.removeFromTop(18));
        card.removeFromTop(4);
        nameEdit.setBounds(card.removeFromTop(38));
        card.removeFromTop(12);
        hostBtn.setBounds(card.removeFromTop(44));
        card.removeFromTop(28);
        orLabel.setBounds(card.removeFromTop(18));
        card.removeFromTop(4);
        auto row = card.removeFromTop(44);
        joinBtn.setBounds(row.removeFromRight(96));
        row.removeFromRight(8);
        codeEdit.setBounds(row);
    }

    if (cancelBtn.isVisible()) {
        codeBox = body.withSizeKeepingCentre(420, 230);
        auto btns = codeBox.withTrimmedTop(170).withSizeKeepingCentre(260, 38);
        if (copyBtn.isVisible()) {
            copyBtn.setBounds(btns.removeFromLeft(126));
            btns.removeFromLeft(8);
            cancelBtn.setBounds(btns);
        } else {
            cancelBtn.setBounds(btns.withSizeKeepingCentre(126, 38));
        }
    }

    if (proc.state == LinkState::Connected) {
        // ---- audio bar
        audioBar = body.removeFromTop(50);
        body.removeFromTop(12);
        auto a = audioBar.reduced(10, 9);
        micBtn.setBounds(a.removeFromLeft(78));
        const int meterW = juce::jlimit(72, 150, audioBar.getWidth() / 11);
        a.removeFromLeft(6);
        micBox.setBounds(a.removeFromLeft(juce::jlimit(110, 220, audioBar.getWidth() / 8)).reduced(0, 2));
        a.removeFromLeft(8);
        micMeter.setBounds(a.removeFromLeft(meterW));
        a.removeFromLeft(4);
        micTrim.setBounds(a.removeFromLeft(32));
        a.removeFromLeft(20);
        streamBtn.setBounds(a.removeFromLeft(118));
        a.removeFromLeft(8);
        streamMeter.setBounds(a.removeFromLeft(meterW));
        a.removeFromLeft(4);
        streamTrim.setBounds(a.removeFromLeft(32));
        a.removeFromLeft(8);
        appBtn.setBounds(a.removeFromLeft(98));
        a.removeFromLeft(16);
        inMeter.setBounds(a.removeFromRight(meterW));
        a.removeFromRight(10);
        // two receive volumes stacked: VOICE on top, DAW below
        auto grp = a.removeFromRight(juce::jmin(200, a.getWidth()));
        inLabelArea = grp.removeFromLeft(0);
        auto labels = grp.removeFromLeft(44);
        voiceLabel = labels.removeFromTop(labels.getHeight() / 2);
        dawLabel = labels;
        voiceSlider.setBounds(grp.removeFromTop(grp.getHeight() / 2).expanded(0, 2));
        dawSlider.setBounds(grp.expanded(0, 2));
        statusArea = {};

        // ---- right column: files + chat
        auto right = body.removeFromRight(juce::jlimit(300, 420, body.getWidth() / 3));
        body.removeFromRight(12);
        filesArea = right.removeFromTop(juce::roundToInt(right.getHeight() * 0.42f));
        right.removeFromTop(12);
        chatArea = right;
        attachBtn.setBounds(filesArea.getRight() - 70, filesArea.getY() + 7, 60, 24);
        fileView.setBounds(filesArea.withTrimmedTop(36).reduced(8, 6));
        auto c = chatArea.withTrimmedTop(32).reduced(6, 6);
        auto in = c.removeFromBottom(36);
        c.removeFromBottom(6);
        chatLog.setBounds(c);
        sendBtn.setBounds(in.removeFromRight(70));
        in.removeFromRight(6);
        chatInput.setBounds(in);

        // ---- stage
        stageArea = body;
        auto sh = stageArea.withHeight(44).reduced(10, 8);
        popBtn.setBounds(sh.removeFromRight(34));
        sh.removeFromRight(10);
        camBox.setBounds(sh.removeFromRight(juce::jlimit(100, 180, stageArea.getWidth() / 6)).reduced(0, 1));
        sh.removeFromRight(6);
        camBtn.setBounds(sh.removeFromRight(82));
        sh.removeFromRight(14);
        screenBox.setBounds(sh.removeFromRight(juce::jlimit(110, 190, stageArea.getWidth() / 5)).reduced(0, 1));
        sh.removeFromRight(6);
        screenBtn.setBounds(sh.removeFromRight(104));
        sh.removeFromRight(12);
        sh.removeFromLeft(54); // "STAGE" title
        people.setBounds(sh.removeFromLeft(juce::jmin(sh.getWidth(), people.preferredWidth())));
        stage.setBounds(stageArea.withTrimmedTop(44).reduced(8, 8).withTrimmedTop(-2));
        refreshFiles();
    }
}

void Editor::paintPanel(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title) {
    g.setColour(ui::panel);
    g.fillRoundedRectangle(r.toFloat(), 8.0f);
    if (title.isNotEmpty()) {
        g.setColour(ui::dim);
        g.setFont(juce::Font(11.0f, juce::Font::bold).withExtraKerningFactor(0.12f));
        g.drawText(title, r.reduced(12, 0).removeFromTop(36), juce::Justification::centredLeft);
    }
}

void Editor::paint(juce::Graphics& g) {
    g.fillAll(ui::bg);
    g.setColour(ui::panel);
    g.fillRect(header);
    g.setColour(ui::line);
    g.fillRect(header.withTop(header.getBottom() - 1));
    auto hb = header.reduced(18, 0);
    drawLogo(g, hb.withTrimmedLeft(-6), 46.0f);
    hb.removeFromLeft(92);
    g.setColour(ui::dim);
    g.setFont(juce::Font(11.0f).withExtraKerningFactor(0.04f));
    g.drawText("by eFly Audio", hb.removeFromLeft(84).withTrimmedTop(6), juce::Justification::centredLeft);

    auto s = proc.state;
    const int n = proc.peopleCount();
    juce::String st = s == LinkState::Connected ? (n <= 1 ? (n == 1 ? "Linked with " + proc.peopleSummary() : juce::String("Linked"))
                                                          : juce::String(n + 1) + " in the room")
                                                : juce::String(smash::stateName(s));
    if (s == LinkState::Connected && proc.pingMs >= 0) st += "   " + juce::String(proc.pingMs) + " ms ping";
    if (s == LinkState::Connected && proc.audio().anyReceiving())
        st += "   audio " + juce::String(proc.audio().bufferMs.load()) + " ms" + (proc.audio().lossPct.load() > 0 ? "  " + juce::String(proc.audio().lossPct.load()) + "% loss" : juce::String());
    g.setFont(juce::Font(12.0f));
    int rightLimit = folderBtn.getX();
    for (juce::Component* c : std::initializer_list<juce::Component*> { &codeChip, &sendRecBtn, &recBtn, &leaveBtn, &gearBtn })
        if (c->isVisible()) rightLimit = juce::jmin(rightLimit, c->getX());
    int pw = juce::jmin(g.getCurrentFont().getStringWidth(st) + 34, rightLimit - (hb.getX() + 16) - 12);
    auto pill = juce::Rectangle<int>(hb.getX() + 16, header.getCentreY() - 13, juce::jmax(60, pw), 26);
    g.setColour(ui::panel2);
    g.fillRoundedRectangle(pill.toFloat(), 13.0f);
    g.setColour(ui::line);
    g.drawRoundedRectangle(pill.toFloat().reduced(0.5f), 13.0f, 1.0f);
    g.setColour(stateColour(s));
    g.fillEllipse((float) pill.getX() + 11, (float) pill.getCentreY() - 4, 8, 8);
    g.setColour(ui::text);
    g.drawText(st, pill.withTrimmedLeft(25).withTrimmedRight(10), juce::Justification::centredLeft, true);

    if (nameEdit.isVisible()) {
        g.setColour(ui::line);
        int y = connectCard.getY() + 18 + 4 + 38 + 12 + 44 + 14;
        g.fillRect(connectCard.getX(), y, connectCard.getWidth(), 1);
        if (s == LinkState::Error && proc.statusText.isNotEmpty()) {
            g.setColour(ui::red);
            g.setFont(juce::Font(13.0f));
            g.drawFittedText(proc.statusText, connectCard.withTop(connectCard.getY() + 228).withHeight(40), juce::Justification::centredTop, 2);
        }
    }

    if (cancelBtn.isVisible()) {
        g.setColour(ui::panel);
        g.fillRoundedRectangle(codeBox.toFloat(), 10.0f);
        g.setColour(ui::line);
        g.drawRoundedRectangle(codeBox.toFloat().reduced(0.5f), 10.0f, 1.0f);
        auto cb = codeBox.reduced(20, 18);
        g.setColour(ui::dim);
        g.setFont(juce::Font(11.0f, juce::Font::bold).withExtraKerningFactor(0.12f));
        bool hosting = s == LinkState::Waiting;
        g.drawText(hosting ? "YOUR ROOM CODE" : "LINKING", cb.removeFromTop(16), juce::Justification::centred);
        cb.removeFromTop(10);
        g.setColour(hosting ? ui::accent : ui::text);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 54.0f, juce::Font::bold));
        g.drawText(proc.code.isNotEmpty() ? proc.code : juce::String("..."), cb.removeFromTop(70), juce::Justification::centred);
        g.setColour(ui::dim);
        g.setFont(juce::Font(13.0f));
        g.drawText(proc.statusText, cb.removeFromTop(30), juce::Justification::centred);
    }

    if (s == LinkState::Connected) {
        paintPanel(g, audioBar, {});
        // group separators
        g.setColour(ui::line);
        g.fillRect(streamBtn.getX() - 10, audioBar.getY() + 12, 1, audioBar.getHeight() - 24);
        g.fillRect(voiceLabel.getX() - 10, audioBar.getY() + 12, 1, audioBar.getHeight() - 24);
        g.setColour(ui::dim);
        g.setFont(juce::Font(10.5f, juce::Font::bold).withExtraKerningFactor(0.1f));
        g.setFont(juce::Font(9.5f, juce::Font::bold).withExtraKerningFactor(0.08f));
        g.drawText("VOICE", voiceLabel, juce::Justification::centredLeft);
        g.drawText("DAW", dawLabel, juce::Justification::centredLeft);

        paintPanel(g, stageArea, "STAGE");
        g.setColour(dragOver ? ui::accent.withAlpha(0.08f) : ui::panel);
        g.fillRoundedRectangle(filesArea.toFloat(), 8.0f);
        g.setColour(ui::dim);
        g.setFont(juce::Font(11.0f, juce::Font::bold).withExtraKerningFactor(0.12f));
        g.drawText("FILES", filesArea.reduced(12, 0).removeFromTop(36), juce::Justification::centredLeft);
        paintPanel(g, chatArea, "CHAT");
    }
}

void Editor::paintOverChildren(juce::Graphics& g) {
    if (dragOver && proc.state == LinkState::Connected) {
        auto r = filesArea.toFloat().reduced(1.0f);
        g.setColour(ui::accent);
        juce::Path p;
        p.addRoundedRectangle(r, 8.0f);
        float dash[] = { 6.0f, 4.0f };
        juce::PathStrokeType(1.5f).createDashedStroke(p, p, dash, 2);
        g.fillPath(p);
        g.setColour(ui::bg.withAlpha(0.85f));
        g.fillRoundedRectangle(r.withSizeKeepingCentre(170, 34), 6.0f);
        g.setColour(ui::accent);
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText("DROP TO SEND", r.toNearestInt(), juce::Justification::centred);
    }
}
