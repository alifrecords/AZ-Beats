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
    void setCategory (int idx);

private:
    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updatePlayer();

    AZBetasProcessor& proc;
    std::function<void (const juce::String&)> notify;
    std::function<void()> goModernize;

    juce::TextEditor search;
    juce::Label catTitle, genreTitle, listTitle, listSub, playerTitle, playerSub, usage;
    // sidebar: fixed filters, sample packs, categories (scrollable)
    struct Entry { int kind; juce::String value, label; };   // kind: 0 fixed, 1 pack, 2 category
    std::vector<Entry> entries;
    juce::OwnedArray<juce::TextButton> catButtons;
    juce::OwnedArray<juce::Label> sideLabels;
    juce::Viewport sideView;
    juce::Component sideContent;
    int categoryIndex = 0;
    juce::String entriesSignature;
    void layoutSide();
    // cover of the selected pack
    juce::Image cover;
    juce::String coverPackId;
    juce::Rectangle<int> coverArea;
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
