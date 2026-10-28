#pragma once
#include "Theme.h"

// Waveform with a moving playhead
class WaveformView : public juce::Component
{
public:
    std::function<std::vector<float>(int)> getPeaks;   // pairs min/max
    std::function<float()> getPosition;                 // 0..1, <0 = hidden
    std::function<void()> onClick;
    juce::String emptyText { "Loop tanlang" };

    void refresh()
    {
        peaks = getPeaks ? getPeaks (juce::jmax (8, getWidth() / 3)) : std::vector<float>();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::bg);
        g.fillRoundedRectangle (r, 8.0f);
        if (peaks.empty())
        {
            g.setColour (Theme::muted);
            g.setFont (Theme::font (12.0f));
            g.drawText (emptyText, r, juce::Justification::centred);
            return;
        }
        const int n = (int) peaks.size() / 2;
        const float pos = getPosition ? getPosition() : -1.0f;
        const float barW = (r.getWidth() - 16.0f) / (float) n;
        const float mid = r.getCentreY(), half = r.getHeight() * 0.44f;
        float mx = 0.0001f;
        for (auto v : peaks) mx = juce::jmax (mx, std::abs (v));
        for (int i = 0; i < n; ++i)
        {
            float lo = peaks[(size_t) i * 2] / mx, hi = peaks[(size_t) i * 2 + 1] / mx;
            float x = 8.0f + i * barW;
            bool played = pos >= 0.0f && (float) i / (float) n <= pos;
            g.setColour (played ? Theme::accent : Theme::wave);
            g.fillRect (x, mid - hi * half, juce::jmax (1.0f, barW - 1.0f), juce::jmax (1.0f, (hi - lo) * half));
        }
        if (pos >= 0.0f)
        {
            g.setColour (Theme::text);
            g.fillRect (8.0f + pos * (r.getWidth() - 16.0f), r.getY() + 4.0f, 1.5f, r.getHeight() - 8.0f);
        }
    }

    void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }

private:
    std::vector<float> peaks;
};

// "Drag me into the DAW" box. createFile() renders the audio and returns the
// file (or an empty File + error message).
class DragToDawBox : public juce::Component
{
public:
    std::function<juce::File (juce::String&)> createFile;
    std::function<void (const juce::String&)> onMessage;
    juce::String title { "DAW'ga sudrang" }, subtitle { "trekka tashlang" };
    bool locked = false;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (locked ? Theme::raised : (hover ? Theme::accent.brighter (0.1f) : Theme::accent));
        g.fillRoundedRectangle (r, 8.0f);
        auto fg = locked ? Theme::muted : Theme::bg;
        g.setColour (fg);
        // arrow icon
        juce::Path p;
        float cx = r.getX() + 22.0f, cy = r.getCentreY();
        p.startNewSubPath (cx, cy - 8); p.lineTo (cx, cy + 5);
        p.startNewSubPath (cx - 5, cy); p.lineTo (cx, cy + 5); p.lineTo (cx + 5, cy);
        p.startNewSubPath (cx - 7, cy + 9); p.lineTo (cx + 7, cy + 9);
        g.strokePath (p, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        auto tr = r.withTrimmedLeft (40.0f);
        g.setFont (Theme::font (13.0f, true));
        g.drawText (title, tr.removeFromTop (r.getHeight() * 0.55f).toNearestInt(), juce::Justification::bottomLeft);
        g.setFont (Theme::font (10.5f));
        g.drawText (subtitle, tr.toNearestInt(), juce::Justification::topLeft);
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || ! createFile) return;
        dragging = true;
        juce::String err;
        auto f = createFile (err);
        if (! f.existsAsFile())
        {
            if (onMessage) onMessage (err);
            return;
        }
        if (onMessage) onMessage ("Sudrab DAW trekiga tashlang: " + f.getFileName());
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging && ! e.mouseWasDraggedSinceMouseDown() && onMessage)
            onMessage ("Bu qutichani bosib turib DAW trekiga sudrang (yoki faylni saqlash uchun Eksport)");
        dragging = false;
    }

private:
    bool hover = false, dragging = false;
};

// Labelled horizontal slider row used in AZ Modernize
class ParamRow : public juce::Component
{
public:
    ParamRow (const juce::String& name, double mn, double mx, double step, const juce::String& unit)
        : unitText (unit)
    {
        styleLabel (label, 12.5f, Theme::text2);
        label.setText (name, juce::dontSendNotification);
        styleLabel (value, 12.5f, Theme::text);
        value.setJustificationType (juce::Justification::centredRight);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (mn, mx, step);
        slider.onValueChange = [this] { updateText(); if (onChange) onChange ((float) slider.getValue()); };
        addAndMakeVisible (label); addAndMakeVisible (value); addAndMakeVisible (slider);
    }

    std::function<void (float)> onChange;

    void set (float v)
    {
        slider.setValue (v, juce::dontSendNotification);
        updateText();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (18);
        value.setBounds (top.removeFromRight (90));
        label.setBounds (top);
        slider.setBounds (r.reduced (0, 1));
    }

    juce::Slider slider;

private:
    void updateText()
    {
        auto v = slider.getValue();
        auto s = (v > 0 && (unitText == "dB" || unitText == " dB")) ? "+" + juce::String (v, 1) : juce::String (v, slider.getInterval() >= 1.0 ? 0 : 1);
        value.setText (s + " " + unitText, juce::dontSendNotification);
    }
    juce::Label label, value;
    juce::String unitText;
};
