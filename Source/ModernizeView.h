#pragma once
#include "PluginProcessor.h"
#include "Components.h"

class ModernizeView : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::ChangeListener
{
public:
    ModernizeView (AZBetasProcessor& p, std::function<void (const juce::String&)> notify);
    ~ModernizeView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dragOver = false; repaint(); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void syncFromSettings();
    void updateState();
    void openFile();

    AZBetasProcessor& proc;
    std::function<void (const juce::String&)> notify;

    juce::TextButton chooseBtn { "Fayl tanlash" }, takeSelBtn { "Tanlangan loopni olish" };
    juce::ComboBox style, subNote;
    juce::Label styleLbl, settingsLbl, lockLbl;
    juce::TextButton autoBtn { "Avto sozlash" }, applyBtn { "Qo'llash" }, abBtn { "A / B" },
                     stopBtn { "Stop" }, exportBtn { "WAV eksport" };
    juce::ToggleButton subOn { "Sub qo'shish" }, seamless { "Loop rejimi (choksiz)" };
    std::unique_ptr<ParamRow> subLevel, hpf, mud, presence, air, transient, parallel, width, target, ceiling;
    juce::Viewport settingsView;
    juce::Component settingsContent;
    DragToDawBox dragBox;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false;

    juce::Rectangle<int> dropArea, infoArea, tableArea;
};
