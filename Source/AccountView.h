#pragma once
#include "PluginProcessor.h"
#include "Theme.h"

class AccountView : public juce::Component
{
public:
    AccountView (AZBetasProcessor& p, std::function<void (const juce::String&)> notify,
                 std::function<void()> onTierChanged);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    AZBetasProcessor& proc;
    std::function<void (const juce::String&)> notify;
    std::function<void()> onTierChanged;

    juce::TextEditor keyEdit, urlEdit;
    juce::TextButton activateBtn { "Faollashtirish" }, logoutBtn { "Kalitni o'chirish" },
                     buyBtn { "Obuna sotib olish (Telegram)" }, saveUrlBtn { "Saqlash va yangilash" },
                     clearCacheBtn { "Keshni tozalash" };
    juce::Rectangle<int> statusArea, plansArea, keyArea, libArea;
};
