#include "ModernizeView.h"

ModernizeView::ModernizeView (AZBetasProcessor& p, std::function<void (const juce::String&)> notifyFn)
    : proc (p), notify (std::move (notifyFn))
{
    chooseBtn.onClick = [this] { openFile(); };
    takeSelBtn.onClick = [this]
    {
        juce::String err;
        if (! proc.modernizeTakeSelected (err)) notify (err);
        syncFromSettings();
    };
    addAndMakeVisible (chooseBtn);
    addAndMakeVisible (takeSelBtn);

    styleLabel (styleLbl, 11.0f, Theme::muted, true);
    styleLbl.setText ("ZAMONAVIY USLUB", juce::dontSendNotification);
    styleLabel (settingsLbl, 11.0f, Theme::muted, true);
    settingsLbl.setText ("AVTO SOZLAMA  -  QO'LDA BOSHQARISH MUMKIN", juce::dontSendNotification);
    styleLabel (lockLbl, 12.0f, Theme::accentTxt);
    lockLbl.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (styleLbl);
    addAndMakeVisible (settingsLbl);
    addAndMakeVisible (lockLbl);

    for (int i = 0; i < Modernize::numStyles; ++i)
        style.addItem (Modernize::styleName (i), i + 1);
    style.onChange = [this]
    {
        proc.modernizeAuto (style.getSelectedId() - 1);
        syncFromSettings();
        notify ("Uslub: " + style.getText() + " - sozlamalar avtomatik moslandi");
    };
    addAndMakeVisible (style);

    autoBtn.setColour (juce::TextButton::buttonColourId, Theme::tealDim);
    autoBtn.setColour (juce::TextButton::textColourOffId, Theme::tealTxt);
    autoBtn.onClick = [this] { proc.modernizeAuto (proc.modSettings().style); syncFromSettings(); };
    addAndMakeVisible (autoBtn);

    auto& s = proc.modSettings();
    auto make = [this] (std::unique_ptr<ParamRow>& row, const juce::String& name, double mn, double mx,
                        double step, const juce::String& unit, std::function<void (float)> set)
    {
        row = std::make_unique<ParamRow> (name, mn, mx, step, unit);
        row->onChange = std::move (set);
        settingsContent.addAndMakeVisible (*row);
    };
    juce::ignoreUnused (s);
    make (subLevel, "Sub balandligi (peak'ga nisbatan)", -30, 0, 0.5, "dB", [this] (float v) { proc.modSettings().subLevelDb = v; });
    make (hpf,      "Past kesish (HPF)", 20, 80, 1, "Hz",           [this] (float v) { proc.modSettings().hpfHz = v; });
    make (mud,      "Loyqalik - 200 Hz", -8, 4, 0.1, "dB",          [this] (float v) { proc.modSettings().mudDb = v; });
    make (presence, "Aniqlik - 3 kHz", -4, 8, 0.1, "dB",            [this] (float v) { proc.modSettings().presenceDb = v; });
    make (air,      "Havo - 12 kHz", -6, 6, 0.1, "dB",              [this] (float v) { proc.modSettings().airDb = v; });
    make (transient,"Tranzient (zarba aniqligi)", 0, 100, 1, "%",   [this] (float v) { proc.modSettings().transient = v; });
    make (parallel, "Parallel kompressiya", 0, 100, 1, "%",         [this] (float v) { proc.modSettings().parallel = v; });
    make (width,    "Stereo kenglik", 0, 150, 1, "%",               [this] (float v) { proc.modSettings().width = v; });
    make (target,   "Loudness maqsadi", -20, -7, 0.5, "LUFS",       [this] (float v) { proc.modSettings().targetLufs = v; });
    make (ceiling,  "True Peak chegarasi", -3, -0.1, 0.1, "dBTP",   [this] (float v) { proc.modSettings().ceilingDb = v; });

    subOn.onClick = [this] { proc.modSettings().subOn = subOn.getToggleState(); };
    seamless.onClick = [this] { proc.modSettings().seamless = seamless.getToggleState(); };
    settingsContent.addAndMakeVisible (subOn);
    settingsContent.addAndMakeVisible (seamless);
    subNote.addItem ("Sub notasi: avto", 1);
    for (int i = 0; i < 12; ++i) subNote.addItem ("Sub notasi: " + Modernize::noteName (i), i + 2);
    subNote.onChange = [this] { proc.modSettings().subNote = subNote.getSelectedId() - 2; };
    settingsContent.addAndMakeVisible (subNote);

    settingsView.setViewedComponent (&settingsContent, false);
    settingsView.setScrollBarsShown (true, false);
    settingsView.setScrollBarThickness (6);
    addAndMakeVisible (settingsView);

    applyBtn.setColour (juce::TextButton::buttonColourId, Theme::accent);
    applyBtn.setColour (juce::TextButton::textColourOffId, Theme::bg);
    applyBtn.onClick = [this] { proc.modernizeRun(); updateState(); };
    abBtn.onClick = [this]
    {
        if (proc.mod().processed == nullptr) { notify ("Avval \"Qo'llash\"ni bosing"); return; }
        proc.modernizePreview (! proc.isModernizePreviewProcessed());
        notify (proc.isModernizePreviewProcessed() ? "Eshitilmoqda: AZ MODERNIZE (keyin)" : "Eshitilmoqda: ASL (oldin)");
    };
    stopBtn.onClick = [this]
    {
        if (proc.isPreviewPlaying()) proc.setPreviewPlaying (false);
        else proc.modernizePreview (proc.mod().processed != nullptr);
    };
    exportBtn.onClick = [this]
    {
        if (proc.mod().processed == nullptr) { notify ("Avval \"Qo'llash\"ni bosing"); return; }
        chooser = std::make_unique<juce::FileChooser> ("WAV saqlash",
            juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                .getChildFile (juce::File::createLegalFileName (proc.mod().sourceName + " (AZ Modernize)") + ".wav"),
            "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            juce::String err;
            auto out = proc.modernizeExport (f.withFileExtension ("wav"), err);
            notify (out.existsAsFile() ? "Saqlandi: " + out.getFullPathName() : err);
        });
    };
    for (auto* b : { &applyBtn, &abBtn, &stopBtn, &exportBtn })
        addAndMakeVisible (b);

    dragBox.title = "DAW'ga sudrang";
    dragBox.createFile = [this] (juce::String& err) { return proc.modernizeExport ({}, err); };
    dragBox.onMessage = [this] (const juce::String& m) { notify (m); };
    addAndMakeVisible (dragBox);

    proc.addChangeListener (this);
    syncFromSettings();
}

ModernizeView::~ModernizeView()
{
    proc.removeChangeListener (this);
}

void ModernizeView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateState();
    repaint();
}

void ModernizeView::syncFromSettings()
{
    const auto& s = proc.modSettings();
    style.setSelectedId (s.style + 1, juce::dontSendNotification);
    subOn.setToggleState (s.subOn, juce::dontSendNotification);
    seamless.setToggleState (s.seamless, juce::dontSendNotification);
    subNote.setSelectedId (s.subNote + 2, juce::dontSendNotification);
    subLevel->set (s.subLevelDb);  hpf->set (s.hpfHz);  mud->set (s.mudDb);
    presence->set (s.presenceDb);  air->set (s.airDb);  transient->set (s.transient);
    parallel->set (s.parallel);    width->set (s.width); target->set (s.targetLufs);
    ceiling->set (s.ceilingDb);
    updateState();
    repaint();
}

void ModernizeView::updateState()
{
    const bool has = proc.mod().source != nullptr;
    const auto tier = proc.license->tier();
    const bool canProcess = tier != Tier::Free;
    const bool busy = proc.isModernizeBusy();

    applyBtn.setEnabled (has && canProcess && ! busy);
    applyBtn.setButtonText (busy ? "Ishlanmoqda..." : "Qo'llash");
    autoBtn.setEnabled (has);
    abBtn.setEnabled (proc.mod().processed != nullptr);
    exportBtn.setEnabled (proc.mod().processed != nullptr);
    stopBtn.setButtonText (proc.isPreviewPlaying() ? "Stop" : "Tinglash");
    stopBtn.setEnabled (has);
    dragBox.locked = ! canProcess || proc.mod().processed == nullptr;
    dragBox.subtitle = ! canProcess ? "Start / PRO kerak" : (proc.mod().processed == nullptr ? "avval Qo'llash" : "trekka tashlang");
    dragBox.repaint();

    if (tier == Tier::Free)
        lockLbl.setText ("Bepul tarif: faqat tashxis. Ishlov va eksport - Start / PRO", juce::dontSendNotification);
    else if (tier == Tier::Start)
        lockLbl.setText ("Start: bu oy " + juce::String (proc.license->modernizeUsedThisMonth()) + " / 5 fayl", juce::dontSendNotification);
    else
        lockLbl.setText ("PRO: cheksiz", juce::dontSendNotification);
}

void ModernizeView::tick()
{
    if (proc.isModernizeBusy())
        repaint (tableArea);
}

void ModernizeView::openFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Audio tanlang", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        juce::String err;
        if (! proc.modernizeLoadFile (f, err)) notify (err);
        syncFromSettings();
    });
}

bool ModernizeView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"))
            return true;
    return false;
}

void ModernizeView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
    {
        juce::String err;
        if (proc.modernizeLoadFile (juce::File (f), err)) break;
        notify (err);
    }
    syncFromSettings();
}

// -------------------------------------------------------------------- paint --
void ModernizeView::paint (juce::Graphics& g)
{
    // drop zone
    auto dz = dropArea.toFloat();
    g.setColour (dragOver ? Theme::accentDim : Theme::panel.darker (0.2f));
    g.fillRoundedRectangle (dz, 10.0f);
    juce::Path dash;
    dash.addRoundedRectangle (dz.reduced (1.0f), 10.0f);
    juce::PathStrokeType st (1.5f);
    float d[] = { 6.0f, 4.0f };
    juce::Path dashed;
    st.createDashedStroke (dashed, dash, d, 2);
    g.setColour (dragOver ? Theme::accent : Theme::line2.brighter (0.2f));
    g.fillPath (dashed);
    g.setColour (Theme::text);
    g.setFont (Theme::font (14.0f, true));
    auto tz = dropArea.reduced (18, 0).withTrimmedRight (330);
    g.drawText ("O'z audiongizni shu yerga tashlang", tz.withTrimmedBottom (dropArea.getHeight() / 2), juce::Justification::bottomLeft);
    g.setColour (Theme::muted);
    g.setFont (Theme::font (12.0f));
    g.drawText ("WAV, AIFF, FLAC, MP3 - eski loop, perkussiya, sample yoki stem", tz.withTrimmedTop (dropArea.getHeight() / 2 + 2), juce::Justification::topLeft);

    const auto& m = proc.mod();

    // comparison table
    auto t = tableArea;
    Theme::card (g, t.toFloat());
    t = t.reduced (16, 12);
    g.setColour (Theme::text);
    g.setFont (Theme::font (16.0f, true));
    auto head = t.removeFromTop (24);
    g.drawText ("Tashxis: oldin va keyin", head, juce::Justification::left);
    auto legend = head.removeFromRight (150);
    g.setColour (Theme::wave.brighter (0.2f)); g.fillRoundedRectangle (legend.getX() + 0.0f, legend.getCentreY() - 5.0f, 10, 10, 2);
    g.setColour (Theme::text2); g.setFont (Theme::font (12.0f)); g.drawText ("Oldin", legend.withTrimmedLeft (14), juce::Justification::left);
    g.setColour (Theme::accent); g.fillRoundedRectangle (legend.getX() + 70.0f, legend.getCentreY() - 5.0f, 10, 10, 2);
    g.setColour (Theme::text2); g.drawText ("Keyin", legend.withTrimmedLeft (84), juce::Justification::left);
    {
        juce::String info = "Fayl: ";
        if (m.source)
        {
            info << m.sourceName << "  |  " << juce::String (m.source->seconds(), 1) << " s  |  "
                 << juce::String (m.source->sampleRate / 1000.0, 1) << " kHz";
            if (m.sourceBpm > 0) info << "  |  " << juce::String (m.sourceBpm, 0) << " BPM";
            if (m.sourceKey.isNotEmpty()) info << "  |  " << m.sourceKey;
        }
        else info << "hali yo'q";
        g.setColour (Theme::muted);
        g.setFont (Theme::font (12.0f));
        g.drawText (info, t.removeFromTop (20), juce::Justification::left, true);
    }
    t.removeFromTop (8);

    if (! m.hasBefore)
    {
        g.setColour (Theme::muted);
        g.setFont (Theme::font (13.0f));
        g.drawText ("Fayl tashlang yoki kutubxonadagi loopni oling - tahlil darhol chiqadi (bepul).", t, juce::Justification::centred);
        return;
    }

    float maxShare = 10.0f;
    for (int i = 0; i < Modernize::numBands; ++i)
        maxShare = juce::jmax (maxShare, m.before.bands[i], m.hasAfter ? m.after.bands[i] : 0.0f);

    for (int i = 0; i < Modernize::numBands; ++i)
    {
        auto row = t.removeFromTop (30);
        g.setColour (Theme::text2);
        g.setFont (Theme::font (12.0f));
        g.drawText (Modernize::bandName (i), row.removeFromLeft (120), juce::Justification::centredLeft);
        auto nums = row.removeFromRight (110);
        juce::String txt = juce::String (m.before.bands[i], 1) + "%";
        if (m.hasAfter) txt << "  >  " << juce::String (m.after.bands[i], 1) << "%";
        g.setColour (m.hasAfter ? Theme::text : Theme::muted);
        g.drawText (txt, nums, juce::Justification::centredRight);
        row.removeFromRight (10);
        const float w = (float) row.getWidth();
        g.setColour (Theme::wave.brighter (0.2f));
        g.fillRoundedRectangle ((float) row.getX(), row.getY() + 6.0f, juce::jmax (2.0f, w * m.before.bands[i] / maxShare), 8.0f, 3.0f);
        if (m.hasAfter)
        {
            g.setColour (Theme::accent);
            g.fillRoundedRectangle ((float) row.getX(), row.getY() + 17.0f, juce::jmax (2.0f, w * m.after.bands[i] / maxShare), 8.0f, 3.0f);
        }
    }

    t.removeFromTop (8);
    auto metrics = t.removeFromTop (54);
    const int mw = (metrics.getWidth() - 20) / 3;
    auto metric = [&] (juce::Rectangle<int> r, const juce::String& name, const juce::String& before, const juce::String& after)
    {
        g.setColour (Theme::raised);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        r = r.reduced (10, 7);
        g.setColour (Theme::muted);
        g.setFont (Theme::font (10.5f, true));
        g.drawText (name, r.removeFromTop (14), juce::Justification::left);
        g.setFont (Theme::font (14.0f, true));
        g.setColour (Theme::text);
        juce::String s = before;
        g.drawText (s, r, juce::Justification::left);
        if (after.isNotEmpty())
        {
            auto tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), s + "  >  ");
            g.setColour (Theme::teal);
            g.drawText ("  >  " + after, r.withTrimmedLeft (tw - juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), "  >  ")), juce::Justification::left);
        }
    };
    metric (metrics.removeFromLeft (mw), "LOUDNESS (LUFS)", juce::String (m.before.lufs, 1), m.hasAfter ? juce::String (m.after.lufs, 1) : juce::String());
    metrics.removeFromLeft (10);
    metric (metrics.removeFromLeft (mw), "TRUE PEAK (dBTP)", juce::String (m.before.truePeakDb, 1), m.hasAfter ? juce::String (m.after.truePeakDb, 1) : juce::String());
    metrics.removeFromLeft (10);
    metric (metrics, "DINAMIKA (crest dB)", juce::String (m.before.crestDb, 1), m.hasAfter ? juce::String (m.after.crestDb, 1) : juce::String());

    // diagnosis table
    t.removeFromTop (10);
    g.setColour (Theme::muted);
    g.setFont (Theme::font (11.0f, true));
    auto hdr = t.removeFromTop (18);
    g.drawText ("MUAMMO", hdr.removeFromLeft (hdr.getWidth() / 2), juce::Justification::left);
    g.drawText ("YECHIM", hdr, juce::Justification::left);
    g.setColour (Theme::line);
    g.fillRect (t.removeFromTop (1));
    for (auto& r : m.before.diagnosis)
    {
        if (t.getHeight() < 20) break;
        auto row = t.removeFromTop (22);
        g.setFont (Theme::font (12.0f));
        g.setColour (Theme::text);
        g.drawText (r.problem, row.removeFromLeft (row.getWidth() / 2).reduced (0, 1), juce::Justification::centredLeft, true);
        g.setColour (Theme::tealTxt);
        g.drawText (r.fix, row, juce::Justification::centredLeft, true);
    }

    if (proc.isModernizeBusy())
    {
        g.setColour (Theme::bg.withAlpha (0.6f));
        g.fillRoundedRectangle (tableArea.toFloat(), 10.0f);
        g.setColour (Theme::accent);
        g.setFont (Theme::font (16.0f, true));
        g.drawText ("AZ Modernize ishlamoqda...", tableArea, juce::Justification::centred);
    }
}

void ModernizeView::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto right = r.removeFromRight (370);
    r.removeFromRight (16);

    dropArea = r.removeFromTop (84);
    auto btns = dropArea.reduced (14, 0).removeFromRight (310);
    auto b1 = btns.withSizeKeepingCentre (btns.getWidth(), 34);
    chooseBtn.setBounds (b1.removeFromLeft (120));
    b1.removeFromLeft (8);
    takeSelBtn.setBounds (b1);
    r.removeFromTop (14);
    tableArea = r;
    infoArea = {};

    // right panel
    auto pr = right;
    styleLbl.setBounds (pr.removeFromTop (18));
    auto srow = pr.removeFromTop (34);
    autoBtn.setBounds (srow.removeFromRight (120));
    srow.removeFromRight (8);
    style.setBounds (srow);
    pr.removeFromTop (12);
    settingsLbl.setBounds (pr.removeFromTop (18));

    auto bottom = pr.removeFromBottom (166);
    lockLbl.setBounds (bottom.removeFromTop (22));
    bottom.removeFromTop (6);
    auto rowA = bottom.removeFromTop (38);
    applyBtn.setBounds (rowA.removeFromLeft (rowA.getWidth() / 2 - 4));
    rowA.removeFromLeft (8);
    abBtn.setBounds (rowA);
    bottom.removeFromTop (8);
    auto rowB = bottom.removeFromTop (38);
    stopBtn.setBounds (rowB.removeFromLeft (rowB.getWidth() / 2 - 4));
    rowB.removeFromLeft (8);
    exportBtn.setBounds (rowB);
    bottom.removeFromTop (8);
    dragBox.setBounds (bottom);

    pr.removeFromBottom (8);
    settingsView.setBounds (pr);

    // settings content
    const int w = pr.getWidth() - 10;
    int y = 4;
    auto place = [&] (juce::Component& c, int h) { c.setBounds (0, y, w, h); y += h + 8; };
    place (subOn, 24);
    place (*subLevel, 38);
    place (subNote, 30);
    place (*hpf, 38);
    place (*mud, 38);
    place (*presence, 38);
    place (*air, 38);
    place (*transient, 38);
    place (*parallel, 38);
    place (*width, 38);
    place (*target, 38);
    place (*ceiling, 38);
    place (seamless, 24);
    settingsContent.setSize (w, y);
}
