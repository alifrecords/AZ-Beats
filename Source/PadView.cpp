#include "PadView.h"

PadView::PadView (AZBetasProcessor& p, std::function<void (const juce::String&)> notifyFn)
    : proc (p), notify (std::move (notifyFn))
{
    proc.addChangeListener (this);
}

PadView::~PadView() { proc.removeChangeListener (this); }

void PadView::resized()
{
    auto r = getLocalBounds().reduced (24).withTrimmedTop (70).withTrimmedBottom (60);
    const int cols = 4, rows = 2, gap = 14;
    const int w = (r.getWidth() - gap * (cols - 1)) / cols;
    const int h = (r.getHeight() - gap * (rows - 1)) / rows;
    for (int i = 0; i < AZBetasProcessor::numPads; ++i)
    {
        const int row = 1 - i / cols, col = i % cols;   // bottom row = first notes (like hardware pads)
        padRects[i] = { r.getX() + col * (w + gap), r.getY() + row * (h + gap), w, h };
    }
}

void PadView::paint (juce::Graphics& g)
{
    auto top = getLocalBounds().reduced (24).removeFromTop (60);
    g.setColour (Theme::text);
    g.setFont (Theme::font (20.0f, true));
    g.drawText ("Pad rejimi", top.removeFromTop (28), juce::Justification::left);
    g.setColour (Theme::muted);
    g.setFont (Theme::font (12.5f));
    g.drawText ("Bo'sh padni bosing - tanlangan loop qo'yiladi. To'la padni bosing - yoqish/o'chirish. "
                "O'ng tugma - tozalash. MIDI: C1-G1 (klaviatura yoki kontroller).",
                top, juce::Justification::topLeft, true);

    for (int i = 0; i < AZBetasProcessor::numPads; ++i)
    {
        auto info = proc.getPad (i);
        auto r = padRects[i].toFloat();
        auto fill = info.active ? Theme::accent : (info.id.isNotEmpty() ? Theme::raised : Theme::panel.darker (0.2f));
        g.setColour (fill);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (info.active ? Theme::accent.brighter (0.3f) : Theme::line2);
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

        auto inner = padRects[i].reduced (14, 12);
        auto fg = info.active ? Theme::bg : Theme::text;
        g.setColour (info.active ? Theme::bg : Theme::muted);
        g.setFont (Theme::font (11.0f, true));
        auto hdr = inner.removeFromTop (16);
        g.drawText ("PAD " + juce::String (i + 1), hdr, juce::Justification::left);
        g.drawText (juce::MidiMessage::getMidiNoteName (AZBetasProcessor::firstPadNote + i, true, true, 3), hdr, juce::Justification::right);

        g.setColour (fg);
        g.setFont (Theme::font (15.0f, true));
        if (info.id.isEmpty())
        {
            g.setColour (Theme::muted);
            g.setFont (Theme::font (13.0f));
            g.drawText ("+ loop qo'yish", inner, juce::Justification::centred);
        }
        else
        {
            g.drawFittedText (info.name, inner.withTrimmedBottom (20), juce::Justification::centredLeft, 3);
            g.setFont (Theme::font (11.5f));
            g.setColour (info.active ? Theme::bg : Theme::text2);
            g.drawText (info.loaded ? (info.active ? "chalinmoqda" : "tayyor") : "yuklanmoqda...", inner.removeFromBottom (18), juce::Justification::left);
        }
    }

    auto foot = getLocalBounds().reduced (24).removeFromBottom (40);
    g.setColour (Theme::muted);
    g.setFont (Theme::font (12.0f));
    g.drawText ("Padlar DAW tempo'si bilan sinxron chalinadi. DAW ijro etayotganda loop doim to'g'ri taktdan eshitiladi.",
                foot, juce::Justification::centredLeft, true);
}

int PadView::padAt (juce::Point<int> p) const
{
    for (int i = 0; i < AZBetasProcessor::numPads; ++i)
        if (padRects[i].contains (p)) return i;
    return -1;
}

void PadView::mouseUp (const juce::MouseEvent& e)
{
    const int i = padAt (e.getPosition());
    if (i < 0) return;
    auto info = proc.getPad (i);
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Tanlangan loopni qo'yish");
        m.addItem (2, "Tozalash", info.id.isNotEmpty());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, i] (int r)
        {
            juce::String err;
            if (r == 1 && ! proc.assignSelectedToPad (i, err)) notify (err);
            if (r == 2) proc.clearPad (i);
            repaint();
        });
        return;
    }
    if (info.id.isEmpty())
    {
        juce::String err;
        if (proc.assignSelectedToPad (i, err)) notify ("Pad " + juce::String (i + 1) + " tayyor");
        else notify (err);
    }
    else proc.togglePad (i);
    repaint();
}

void PadView::tick()
{
    bool changed = false;
    for (int i = 0; i < AZBetasProcessor::numPads; ++i)
    {
        auto a = proc.getPad (i).active;
        if (a != lastActive[i]) { lastActive[i] = a; changed = true; }
    }
    if (changed) repaint();
}
