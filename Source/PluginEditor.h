#pragma once
#include "PluginProcessor.h"
#include "Theme.h"
#include "LibraryView.h"
#include "ModernizeView.h"
#include "PadView.h"
#include "AccountView.h"

class AZBetasEditor : public juce::AudioProcessorEditor,
                      public juce::DragAndDropContainer,
                      private juce::Timer,
                      private juce::ChangeListener
{
public:
    explicit AZBetasEditor (AZBetasProcessor&);
    ~AZBetasEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showTab (int index);
    void showMessage (const juce::String& text);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    AZBetasProcessor& proc;
    AZLookAndFeel lnf;

    juce::TextButton tabs[4] { juce::TextButton ("Kutubxona"), juce::TextButton ("AZ Modernize"),
                                juce::TextButton ("Pad rejimi"), juce::TextButton ("Akkaunt") };
    int currentTab = 0;

    std::unique_ptr<LibraryView> library;
    std::unique_ptr<ModernizeView> modernize;
    std::unique_ptr<PadView> padsView;
    std::unique_ptr<AccountView> account;

    juce::String message;
    juce::uint32 messageTime = 0;
    juce::Rectangle<int> hostArea, tierArea, statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AZBetasEditor)
};
