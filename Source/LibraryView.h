#pragma once
#include "PluginProcessor.h"
#include "Components.h"

class LibraryView : public juce::Component,
                    private juce::ListBoxModel,
                    private juce::ChangeListener
{
public:
    LibraryView (AZBetasProcessor& p, std::function<void (const juce::String&)> notify,
                 std::function<void()> goModernize);
    ~LibraryView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();              // called by the editor timer (~30 fps)
    void refreshList();

private:
    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updatePlayer();
    void setCategory (int idx);

    AZBetasProcessor& proc;
    std::function<void (const juce::String&)> notify;
    std::function<void()> goModernize;

    juce::TextEditor search;
    juce::Label catTitle, genreTitle, listTitle, listSub, playerTitle, playerSub, usage;
    juce::OwnedArray<juce::TextButton> catButtons;
    juce::StringArray categories;
    int categoryIndex = 0;
    juce::ComboBox genre;
    juce::ToggleButton onlyFree { "Faqat bepullar" }, onlyMatchKey { "Tonallik mos (tanlanganga)" };
    juce::TextButton refreshBtn { "Yangilash" };
    juce::ListBox list { "loops", this };
    juce::Array<CatalogItem> filtered;

    // player
    juce::TextButton playBtn { "Play" }, syncBtn { "SYNC" }, toModernize { "AZ Modernize'ga" }, toPad { "Padga" };
    juce::Slider pitch, volume;
    juce::Label pitchLbl, volLbl;
    WaveformView wave;
    DragToDawBox dragBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchAtt, volAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAtt;
    juce::String lastSelectedId;
    bool lastPlaying = false;
};
