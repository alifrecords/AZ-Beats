#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme
{
    const juce::Colour bg        { 0xff0f1113 };
    const juce::Colour panel     { 0xff15181b };
    const juce::Colour panel2    { 0xff181b1f };
    const juce::Colour raised    { 0xff1c2024 };
    const juce::Colour selected  { 0xff23272c };
    const juce::Colour line      { 0xff262a30 };
    const juce::Colour line2     { 0xff2e333a };
    const juce::Colour text      { 0xffe8eaed };
    const juce::Colour text2     { 0xffc9cfd6 };
    const juce::Colour muted     { 0xff9aa3ad };
    const juce::Colour accent    { 0xfff2a33a };
    const juce::Colour accentDim { 0xff2b2418 };
    const juce::Colour accentTxt { 0xfff2c27a };
    const juce::Colour teal      { 0xff3bc9b0 };
    const juce::Colour tealDim   { 0xff13342e };
    const juce::Colour tealTxt   { 0xff5fe0c6 };
    const juce::Colour blue      { 0xff8fb6ff };
    const juce::Colour blueDim   { 0xff1e2a3f };
    const juce::Colour red       { 0xffe86f6f };
    const juce::Colour wave      { 0xff4a525c };

    inline juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    // Small rounded badge ("YANGI", "BEPUL", ...)
    inline void badge (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t,
                       juce::Colour bgC, juce::Colour fg)
    {
        g.setColour (bgC);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (fg);
        g.setFont (font (10.0f, true));
        g.drawText (t, r, juce::Justification::centred);
    }

    inline void card (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill = panel,
                      juce::Colour border = line, float radius = 10.0f)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r, radius);
        g.setColour (border);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }
}

class AZLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AZLookAndFeel()
    {
        using namespace Theme;
        setColour (juce::ResizableWindow::backgroundColourId, bg);
        setColour (juce::TextButton::buttonColourId, raised);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, bg);
        setColour (juce::ComboBox::backgroundColourId, panel2);
        setColour (juce::ComboBox::outlineColourId, line2);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, muted);
        setColour (juce::PopupMenu::backgroundColourId, panel2);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, selected);
        setColour (juce::PopupMenu::highlightedTextColourId, accent);
        setColour (juce::TextEditor::backgroundColourId, panel2);
        setColour (juce::TextEditor::outlineColourId, line2);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
        setColour (juce::CaretComponent::caretColourId, accent);
        setColour (juce::ListBox::backgroundColourId, bg);
        setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, text);
        setColour (juce::Slider::thumbColourId, text);
        setColour (juce::Slider::trackColourId, accent);
        setColour (juce::Slider::backgroundColourId, line);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ToggleButton::textColourId, text2);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::ToggleButton::tickDisabledColourId, muted);
        setColour (juce::ScrollBar::thumbColourId, line2);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return Theme::font (juce::jmin (13.5f, h * 0.42f), true);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgC,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bgC;
        if (! b.isEnabled()) c = c.withMultipliedAlpha (0.4f);
        else if (down) c = c.brighter (0.15f);
        else if (over) c = c.brighter (0.07f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 8.0f);
        if (! b.getToggleState() && c.getBrightness() < 0.3f)
        {
            g.setColour (Theme::line2);
            g.drawRoundedRectangle (r, 8.0f, 1.0f);
        }
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal)
            return LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        auto cy = (float) y + h * 0.5f;
        juce::Rectangle<float> track ((float) x, cy - 3.0f, (float) w, 6.0f);
        g.setColour (Theme::line);
        g.fillRoundedRectangle (track, 3.0f);

        // fill from the "zero" point for bipolar ranges
        auto zero = (float) s.getPositionOfValue (juce::jlimit (s.getMinimum(), s.getMaximum(), 0.0));
        if (s.getMinimum() >= 0.0) zero = (float) x;
        auto a = juce::jmin (zero, pos), bb = juce::jmax (zero, pos);
        g.setColour (s.isEnabled() ? Theme::accent : Theme::muted);
        g.fillRoundedRectangle ({ a, cy - 3.0f, juce::jmax (2.0f, bb - a), 6.0f }, 3.0f);
        g.setColour (s.isEnabled() ? Theme::text : Theme::muted);
        g.fillEllipse (pos - 8.0f, cy - 8.0f, 16.0f, 16.0f);
    }

    juce::Slider::SliderLayout getSliderLayout (juce::Slider& s) override
    {
        auto l = LookAndFeel_V4::getSliderLayout (s);
        return l;
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        juce::Path p;
        auto ax = (float) w - 18.0f, ay = h * 0.5f;
        p.startNewSubPath (ax - 4, ay - 2); p.lineTo (ax, ay + 2); p.lineTo (ax + 4, ay - 2);
        g.setColour (Theme::muted);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::font (13.0f); }
    juce::Font getPopupMenuFont() override               { return Theme::font (13.5f); }
    juce::Font getLabelFont (juce::Label& l) override    { return l.getFont(); }

    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& te) override
    {
        g.setColour (te.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (0, 0, (float) w, (float) h, 8.0f);
    }
    void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& te) override
    {
        g.setColour (te.hasKeyboardFocus (true) ? Theme::accent : Theme::line2);
        g.drawRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 8.0f, 1.0f);
    }
};

// Helper: a label with theme font
inline void styleLabel (juce::Label& l, float size, juce::Colour c = Theme::text, bool bold = false)
{
    l.setFont (Theme::font (size, bold));
    l.setColour (juce::Label::textColourId, c);
    l.setInterceptsMouseClicks (false, false);
}
