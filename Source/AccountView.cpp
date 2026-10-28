#include "AccountView.h"

AccountView::AccountView (AZBetasProcessor& p, std::function<void (const juce::String&)> notifyFn,
                          std::function<void()> tierChanged)
    : proc (p), notify (std::move (notifyFn)), onTierChanged (std::move (tierChanged))
{
    keyEdit.setTextToShowWhenEmpty ("AZB1.xxxxx.xxxxx - admin bergan kalitni shu yerga qo'ying", Theme::muted);
    keyEdit.setFont (Theme::font (13.0f));
    keyEdit.setIndents (10, 10);
    addAndMakeVisible (keyEdit);

    activateBtn.setColour (juce::TextButton::buttonColourId, Theme::accent);
    activateBtn.setColour (juce::TextButton::textColourOffId, Theme::bg);
    activateBtn.onClick = [this]
    {
        auto info = proc.license->activate (keyEdit.getText());
        if (info.valid)
        {
            notify ("Faollashtirildi: " + tierName (info.tier) + ", " + info.expires + " gacha");
            keyEdit.clear();
        }
        else notify ("Kalit qabul qilinmadi: " + info.error);
        refresh();
        if (onTierChanged) onTierChanged();
    };
    logoutBtn.onClick = [this]
    {
        proc.license->deactivate();
        notify ("Kalit o'chirildi. Bepul tarif.");
        refresh();
        if (onTierChanged) onTierChanged();
    };
    buyBtn.setColour (juce::TextButton::buttonColourId, Theme::tealDim);
    buyBtn.setColour (juce::TextButton::textColourOffId, Theme::tealTxt);
    buyBtn.onClick = [] { juce::URL ("https://t.me/Azimxodjayev_music_bot").launchInDefaultBrowser(); };

    urlEdit.setFont (Theme::font (12.0f));
    urlEdit.setIndents (10, 9);
    urlEdit.setText (proc.catalog->getUrl(), false);
    saveUrlBtn.onClick = [this]
    {
        proc.catalog->setUrl (urlEdit.getText());
        proc.catalog->refresh();
        notify ("Kutubxona manzili saqlandi, yangilanmoqda...");
    };
    clearCacheBtn.onClick = [this]
    {
        auto dir = LibraryCatalog::getCacheDir();
        int n = 0;
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false))
            if (f.deleteFile()) ++n;
        notify (juce::String (n) + " ta keshdagi fayl o'chirildi");
    };

    for (auto* b : { &activateBtn, &logoutBtn, &buyBtn, &saveUrlBtn, &clearCacheBtn })
        addAndMakeVisible (b);
    addAndMakeVisible (urlEdit);
    refresh();
}

void AccountView::refresh()
{
    logoutBtn.setVisible (proc.license->current().valid);
    repaint();
}

void AccountView::paint (juce::Graphics& g)
{
    auto info = proc.license->current();
    const auto tier = info.valid ? info.tier : Tier::Free;

    // status card
    Theme::card (g, statusArea.toFloat());
    auto s = statusArea.reduced (18, 14);
    g.setColour (Theme::muted);
    g.setFont (Theme::font (11.0f, true));
    g.drawText ("SIZNING TARIFINGIZ", s.removeFromTop (16), juce::Justification::left);
    g.setColour (tier == Tier::Pro ? Theme::accent : (tier == Tier::Start ? Theme::blue : Theme::text));
    g.setFont (Theme::font (28.0f, true));
    g.drawText (tierName (tier), s.removeFromTop (38), juce::Justification::left);
    g.setColour (Theme::text2);
    g.setFont (Theme::font (13.0f));
    if (info.valid)
        g.drawText ("Foydalanuvchi: " + info.user + "   |   Amal qiladi: " + info.expires + " gacha", s.removeFromTop (20), juce::Justification::left);
    else
        g.drawText (info.error.isNotEmpty() ? info.error : "Kalit kiritilmagan - bepul tarif", s.removeFromTop (20), juce::Justification::left);
    s.removeFromTop (6);
    g.setColour (Theme::muted);
    juce::String u;
    if (tier == Tier::Free)
        u << "Bepul loop'lar: " << proc.license->usedFreeCount() << " / " << proc.license->getFreeLimit()
          << " ishlatildi  |  AZ Modernize: faqat tashxis";
    else if (tier == Tier::Start)
        u << "Kutubxona cheksiz  |  AZ Modernize: bu oy " << proc.license->modernizeUsedThisMonth() << " / 5";
    else
        u << "Hammasi cheksiz";
    g.drawText (u, s.removeFromTop (20), juce::Justification::left);

    // plans
    struct Plan { const char* name; const char* price; const char* lines[4]; juce::Colour c; };
    const Plan plans[] = {
        { "BEPUL", "0 so'm", { "Hamma narsani tinglash", "10 tagacha loop DAW'da", "Bepul namunalar cheksiz", "Modernize: tashxis" }, Theme::muted },
        { "START", "25 000 so'm / oy", { "Butun kutubxona", "Har haftalik yangiliklar", "Pad rejimi, pitch, sync", "Modernize: oyiga 5 fayl" }, Theme::blue },
        { "PRO", "50 000 so'm / oy", { "Start'dagi hammasi", "Modernize cheksiz", "Yangiliklarga 3 kun oldin", "Yillik: 300 000 so'm" }, Theme::accent } };
    auto pa = plansArea;
    const int pw = (pa.getWidth() - 24) / 3;
    for (int i = 0; i < 3; ++i)
    {
        auto r = pa.removeFromLeft (pw);
        pa.removeFromLeft (12);
        const bool cur = (int) tier == i;
        Theme::card (g, r.toFloat(), cur ? Theme::accentDim : Theme::panel, cur ? Theme::accent : Theme::line);
        auto c = r.reduced (16, 14);
        g.setColour (plans[i].c);
        g.setFont (Theme::font (12.0f, true));
        g.drawText (juce::String (plans[i].name) + (cur ? "  (joriy)" : ""), c.removeFromTop (18), juce::Justification::left);
        g.setColour (Theme::text);
        g.setFont (Theme::font (19.0f, true));
        g.drawText (plans[i].price, c.removeFromTop (30), juce::Justification::left);
        g.setFont (Theme::font (12.5f));
        g.setColour (Theme::text2);
        for (auto* l : plans[i].lines)
            g.drawText (juce::String ("- ") + l, c.removeFromTop (20), juce::Justification::left, true);
    }

    // key area title
    g.setColour (Theme::muted);
    g.setFont (Theme::font (11.0f, true));
    g.drawText ("LITSENZIYA KALITI (to'lovdan keyin admin yuboradi, 30 kun amal qiladi)", keyArea.withHeight (16), juce::Justification::left);
    g.drawText ("KUTUBXONA MANZILI (catalog.json)", libArea.withHeight (16), juce::Justification::left);

    g.setColour (Theme::muted);
    g.setFont (Theme::font (11.0f));
    g.drawText ("AZ BETAS v" + juce::String (JucePlugin_VersionString) + "  |  Azimxodjayev Music",
                getLocalBounds().reduced (24, 10).removeFromBottom (16), juce::Justification::right);
}

void AccountView::resized()
{
    auto r = getLocalBounds().reduced (24, 18);
    statusArea = r.removeFromTop (128);
    r.removeFromTop (14);
    plansArea = r.removeFromTop (170);
    r.removeFromTop (16);

    keyArea = r.removeFromTop (60);
    auto kr = keyArea.withTrimmedTop (20);
    logoutBtn.setBounds (kr.removeFromRight (150));
    kr.removeFromRight (8);
    activateBtn.setBounds (kr.removeFromRight (140));
    kr.removeFromRight (8);
    keyEdit.setBounds (kr);
    r.removeFromTop (10);
    buyBtn.setBounds (r.removeFromTop (38).removeFromLeft (300));
    r.removeFromTop (16);
    libArea = r.removeFromTop (58);
    auto lr = libArea.withTrimmedTop (20);
    clearCacheBtn.setBounds (lr.removeFromRight (150));
    lr.removeFromRight (8);
    saveUrlBtn.setBounds (lr.removeFromRight (170));
    lr.removeFromRight (8);
    urlEdit.setBounds (lr);
}
