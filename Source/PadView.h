#pragma once
#include "PluginProcessor.h"
#include "Theme.h"

// 8 pads (MIDI C1..G1) that play loops in sync with the DAW
class PadView : public juce::Component, private juce::ChangeListener
{
public:
    PadView (AZBetasProcessor& p, std::function<void (const juce::String&)> notify);
    ~PadView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void tick();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    int padAt (juce::Point<int> p) const;

    AZBetasProcessor& proc;
    std::function<void (const juce::String&)> notify;
    juce::Rectangle<int> padRects[AZBetasProcessor::numPads];
    bool lastActive[AZBetasProcessor::numPads] {};
};
