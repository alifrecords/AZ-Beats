#include "PluginEditor.h"

AZBetasEditor::AZBetasEditor (AZBetasProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);

    auto notify = [this] (const juce::String& m) { showMessage (m); };
    library   = std::make_unique<LibraryView> (proc, notify, [this] { showTab (1); });
    modernize = std::make_unique<ModernizeView> (proc, notify);
    padsView  = std::make_unique<PadView> (proc, notify);
    account   = std::make_unique<AccountView> (proc, notify, [this]
    {
        library->refreshList();
        repaint();
    });
    library->setComponentID ("library");
    addChildComponent (*library);
    addChildComponent (*modernize);
    addChildComponent (*padsView);
    addChildComponent (*account);

    for (int i = 0; i < 4; ++i)
    {
        tabs[i].setClickingTogglesState (false);
        tabs[i].setColour (juce::TextButton::buttonColourId, Theme::panel);
        tabs[i].setColour (juce::TextButton::buttonOnColourId, Theme::selected);
        tabs[i].setColour (juce::TextButton::textColourOffId, Theme::muted);
        tabs[i].setColour (juce::TextButton::textColourOnId, Theme::text);
        tabs[i].onClick = [this, i] { showTab (i); };
        addAndMakeVisible (tabs[i]);
    }

    proc.addChangeListener (this);
    setResizable (true, true);
    setResizeLimits (980, 640, 1800, 1200);
    setSize (1160, 740);
    showTab (0);
    startTimerHz (30);
}

AZBetasEditor::~AZBetasEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void AZBetasEditor::showTab (int index)
{
    currentTab = index;
    for (int i = 0; i < 4; ++i)
        tabs[i].setToggleState (i == index, juce::dontSendNotification);
    library->setVisible (index == 0);
    modernize->setVisible (index == 1);
    padsView->setVisible (index == 2);
    account->setVisible (index == 3);
    if (index == 3) account->refresh();
    if (index == 0) library->refreshList();
}

void AZBetasEditor::showMessage (const juce::String& text)
{
    message = text;
    messageTime = juce::Time::getMillisecondCounter();
    repaint (statusArea);
}

void AZBetasEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint (hostArea);
    repaint (statusArea);
}

void AZBetasEditor::timerCallback()
{
    if (library->isVisible()) library->tick();
    if (modernize->isVisible()) modernize->tick();
    if (padsView->isVisible()) padsView->tick();

    static double lastBpm = -1; static bool lastPlay = false;
    if (std::abs (proc.getHostBpm() - lastBpm) > 0.01 || proc.isHostPlaying() != lastPlay)
    {
        lastBpm = proc.getHostBpm(); lastPlay = proc.isHostPlaying();
        repaint (hostArea);
    }
    if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime > 7000)
    {
        message.clear();
        repaint (statusArea);
    }
}

void AZBetasEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);

    // header
    auto header = getLocalBounds().removeFromTop (64);
    g.setColour (Theme::panel);
    g.fillRect (header);
    g.setColour (Theme::line);
    g.fillRect (header.removeFromBottom (1));

    auto logo = juce::Rectangle<float> (20.0f, 14.0f, 36.0f, 36.0f);
    g.setColour (Theme::accent);
    g.fillRoundedRectangle (logo, 8.0f);
    g.setColour (Theme::bg);
    g.setFont (Theme::font (15.0f, true));
    g.drawText ("AZ", logo, juce::Justification::centred);
    g.setColour (Theme::text);
    g.setFont (Theme::font (18.0f, true));
    g.drawText ("AZ BETAS", 68, 14, 160, 22, juce::Justification::left);
    g.setColour (Theme::muted);
    g.setFont (Theme::font (9.5f, true));
    g.drawText ("AZIMXODJAYEV MUSIC", 68, 36, 160, 14, juce::Justification::left);

    // host info
    Theme::card (g, hostArea.toFloat(), Theme::raised, Theme::raised, 8.0f);
    const bool sync = proc.params.getRawParameterValue ("sync")->load() > 0.5f;
    const auto bpm = proc.getHostBpm();
    g.setColour (proc.isHostPlaying() ? Theme::teal : Theme::muted);
    g.fillEllipse ((float) hostArea.getX() + 12.0f, hostArea.getCentreY() - 4.0f, 8.0f, 8.0f);
    g.setColour (Theme::text2);
    g.setFont (Theme::font (12.5f));
    juce::String h = bpm > 0 ? "DAW: " + juce::String (bpm, 1) + " BPM" : "DAW tempo yo'q";
    h << (sync ? "  |  Sync" : "  |  Sync o'chiq");
    g.drawText (h, hostArea.withTrimmedLeft (28), juce::Justification::centredLeft);

    // tier badge
    auto info = proc.license->current();
    auto tier = info.valid ? info.tier : Tier::Free;
    auto tb = tierArea.toFloat();
    auto badgeR = tb.removeFromTop (22.0f).withSizeKeepingCentre (tb.getWidth(), 22.0f);
    Theme::badge (g, badgeR.withTrimmedLeft (badgeR.getWidth() - 120.0f),
                  tier == Tier::Free ? "BEPUL" : tierName (tier).toUpperCase() + " - FAOL",
                  tier == Tier::Pro ? Theme::accent : (tier == Tier::Start ? Theme::blue : Theme::raised),
                  tier == Tier::Free ? Theme::text2 : Theme::bg);
    g.setColour (Theme::muted);
    g.setFont (Theme::font (11.0f));
    g.drawText (info.valid ? info.expires + " gacha" : "obuna yo'q", tierArea.withTrimmedTop (24), juce::Justification::topRight);

    // status bar
    g.setColour (Theme::panel);
    g.fillRect (statusArea);
    g.setColour (Theme::line);
    g.fillRect (statusArea.getX(), statusArea.getY(), statusArea.getWidth(), 1);
    g.setFont (Theme::font (12.0f));
    g.setColour (message.isNotEmpty() ? Theme::accentTxt : Theme::muted);
    g.drawText (message.isNotEmpty() ? message : proc.getStatus(), statusArea.reduced (16, 0), juce::Justification::centredLeft, true);
}

void AZBetasEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (64);
    statusArea = r.removeFromBottom (28);

    auto hr = header.reduced (16, 10);
    hr.removeFromLeft (220);
    for (auto& t : tabs)
    {
        t.setBounds (hr.removeFromLeft (t.getButtonText() == "AZ Modernize" ? 124 : 108).reduced (2, 0));
    }
    tierArea = hr.removeFromRight (140);
    hr.removeFromRight (12);
    const int hw = juce::jmin (250, hr.getWidth());
    hostArea = hr.removeFromRight (hw).withSizeKeepingCentre (hw, 32);

    for (juce::Component* c : { (juce::Component*) library.get(), (juce::Component*) modernize.get(),
                                (juce::Component*) padsView.get(), (juce::Component*) account.get() })
        c->setBounds (r);
}
