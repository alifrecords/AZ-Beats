#include "LibraryView.h"

static const char* fixedCats[] = { "Hammasi", "Yangi", "Bepul namunalar", "Eng yaxshilar" };
static constexpr int numFixedCats = 4;

LibraryView::LibraryView (AZBetasProcessor& p, std::function<void (const juce::String&)> notifyFn,
                          std::function<void()> goMod)
    : proc (p), notify (std::move (notifyFn)), goModernize (std::move (goMod))
{
    search.setTextToShowWhenEmpty ("Qidiruv: nom, janr, teg...", Theme::muted);
    search.setFont (Theme::font (13.0f));
    search.setIndents (10, 9);
    search.onTextChange = [this] { refreshList(); };
    addAndMakeVisible (search);

    styleLabel (catTitle, 11.0f, Theme::muted, true);   catTitle.setText ("KATEGORIYALAR", juce::dontSendNotification);
    styleLabel (genreTitle, 11.0f, Theme::muted, true); genreTitle.setText ("JANR", juce::dontSendNotification);
    addAndMakeVisible (catTitle); addAndMakeVisible (genreTitle);

    genre.onChange = [this] { refreshList(); };
    addAndMakeVisible (genre);
    onlyFree.onClick = [this] { refreshList(); };
    onlyMatchKey.onClick = [this] { refreshList(); };
    addAndMakeVisible (onlyFree); addAndMakeVisible (onlyMatchKey);
    refreshBtn.onClick = [this] { proc.catalog->refresh(); };
    addAndMakeVisible (refreshBtn);

    styleLabel (listTitle, 20.0f, Theme::text, true);
    styleLabel (listSub, 12.0f, Theme::muted);
    styleLabel (usage, 12.0f, Theme::blue);
    usage.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (listTitle); addAndMakeVisible (listSub); addAndMakeVisible (usage);

    list.setRowHeight (52);
    list.setColour (juce::ListBox::backgroundColourId, Theme::bg);
    addAndMakeVisible (list);

    // ---- player
    styleLabel (playerTitle, 15.0f, Theme::text, true);
    styleLabel (playerSub, 12.0f, Theme::muted);
    addAndMakeVisible (playerTitle); addAndMakeVisible (playerSub);

    playBtn.onClick = [this] { proc.setPreviewPlaying (! proc.isPreviewPlaying()); updatePlayer(); };
    addAndMakeVisible (playBtn);
    syncBtn.setClickingTogglesState (true);
    syncBtn.setColour (juce::TextButton::buttonOnColourId, Theme::teal);
    syncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "sync", syncBtn);
    addAndMakeVisible (syncBtn);

    for (auto* s : { &pitch, &volume })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        addAndMakeVisible (s);
    }
    pitchAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "pitch", pitch);
    volAtt   = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "volume", volume);
    pitch.setTextValueSuffix (" st");
    volume.setTextValueSuffix (" dB");
    styleLabel (pitchLbl, 11.0f, Theme::muted, true); pitchLbl.setText ("PITCH", juce::dontSendNotification);
    styleLabel (volLbl, 11.0f, Theme::muted, true);   volLbl.setText ("HAJM", juce::dontSendNotification);
    addAndMakeVisible (pitchLbl); addAndMakeVisible (volLbl);

    toModernize.setColour (juce::TextButton::buttonColourId, Theme::accentDim);
    toModernize.setColour (juce::TextButton::textColourOffId, Theme::accentTxt);
    toModernize.onClick = [this]
    {
        juce::String err;
        if (proc.modernizeTakeSelected (err)) { if (goModernize) goModernize(); }
        else notify (err);
    };
    addAndMakeVisible (toModernize);

    toPad.onClick = [this]
    {
        for (int i = 0; i < AZBetasProcessor::numPads; ++i)
            if (proc.getPad (i).id.isEmpty())
            {
                juce::String err;
                if (proc.assignSelectedToPad (i, err))
                    notify ("Pad " + juce::String (i + 1) + " ga qo'yildi (MIDI nota "
                            + juce::MidiMessage::getMidiNoteName (AZBetasProcessor::firstPadNote + i, true, true, 3) + ")");
                else notify (err);
                return;
            }
        notify ("Hamma padlar band. Pad bo'limida birortasini tozalang.");
    };
    addAndMakeVisible (toPad);

    wave.getPeaks = [this] (int n) { return proc.getPreviewPeaks (n); };
    wave.getPosition = [this] { return proc.isPreviewPlaying() ? proc.getPreviewPosition() : -1.0f; };
    wave.onClick = [this] { proc.setPreviewPlaying (! proc.isPreviewPlaying()); };
    addAndMakeVisible (wave);

    dragBox.createFile = [this] (juce::String& err) { return proc.exportSelectedForDrag (err); };
    dragBox.onMessage = [this] (const juce::String& m) { notify (m); };
    addAndMakeVisible (dragBox);

    proc.catalog->addChangeListener (this);
    proc.addChangeListener (this);
    refreshList();
    updatePlayer();
}

LibraryView::~LibraryView()
{
    proc.catalog->removeChangeListener (this);
    proc.removeChangeListener (this);
}

void LibraryView::changeListenerCallback (juce::ChangeBroadcaster* b)
{
    if (b == &proc.catalog.get())
        refreshList();
    updatePlayer();
}

void LibraryView::setCategory (int idx)
{
    categoryIndex = idx;
    for (int i = 0; i < catButtons.size(); ++i)
        catButtons[i]->setToggleState (i == idx, juce::dontSendNotification);
    refreshList();
}

void LibraryView::refreshList()
{
    auto items = proc.catalog->getItems();

    // categories (rebuild buttons when the set changes)
    juce::StringArray cats;
    for (int i = 0; i < numFixedCats; ++i) cats.add (fixedCats[i]);
    cats.addArray (proc.catalog->getCategories());
    if (cats != categories)
    {
        categories = cats;
        catButtons.clear();
        for (int i = 0; i < categories.size(); ++i)
        {
            auto* b = catButtons.add (new juce::TextButton (categories[i]));
            b->setColour (juce::TextButton::buttonColourId, Theme::bg);
            b->setColour (juce::TextButton::buttonOnColourId, Theme::selected);
            b->setColour (juce::TextButton::textColourOffId, Theme::text2);
            b->setColour (juce::TextButton::textColourOnId, Theme::text);
            b->onClick = [this, i] { setCategory (i); };
            addAndMakeVisible (b);
        }
        categoryIndex = juce::jmin (categoryIndex, categories.size() - 1);
        for (int i = 0; i < catButtons.size(); ++i)
            catButtons[i]->setToggleState (i == categoryIndex, juce::dontSendNotification);
        resized();
    }

    // genres
    auto genres = proc.catalog->getGenres();
    auto current = genre.getText();
    genre.clear (juce::dontSendNotification);
    genre.addItem ("Barcha janrlar", 1);
    for (int i = 0; i < genres.size(); ++i) genre.addItem (genres[i], i + 2);
    auto gi = genres.indexOf (current);
    genre.setSelectedId (gi >= 0 ? gi + 2 : 1, juce::dontSendNotification);

    // filter
    const auto q = search.getText().trim().toLowerCase();
    const auto cat = categories[categoryIndex];
    const auto selKey = proc.getSelected().key;
    filtered.clearQuick();
    for (auto& it : items)
    {
        if (categoryIndex == 1 && ! it.isNew) continue;
        if (categoryIndex == 2 && ! it.isFree) continue;
        if (categoryIndex == 3 && ! it.isDemo) continue;
        if (categoryIndex >= numFixedCats && it.category != cat) continue;
        if (genre.getSelectedId() > 1 && it.genre != genre.getText()) continue;
        if (onlyFree.getToggleState() && ! it.isFree) continue;
        if (onlyMatchKey.getToggleState() && selKey.isNotEmpty() && it.key.isNotEmpty() && it.key != selKey) continue;
        if (q.isNotEmpty())
        {
            auto hay = (it.name + " " + it.genre + " " + it.category + " " + it.key + " " + it.tags.joinIntoString (" ")).toLowerCase();
            if (! hay.contains (q)) continue;
        }
        filtered.add (it);
    }

    listTitle.setText (categoryIndex == 0 ? "Kutubxona" : cat, juce::dontSendNotification);
    listSub.setText (juce::String (filtered.size()) + " ta  |  " + proc.catalog->getStatus(), juce::dontSendNotification);
    list.updateContent();
    list.repaint();
    updatePlayer();
}

int LibraryView::getNumRows() { return filtered.size(); }

void LibraryView::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool isSel)
{
    if (! juce::isPositiveAndBelow (row, filtered.size())) return;
    const auto& it = filtered.getReference (row);
    const bool current = it.id == proc.getSelectedId();
    const bool playing = current && proc.isPreviewPlaying();
    const bool usable = proc.license->canUseLoop (it.id, it.isFree)
                        && ! (it.isEarly && proc.license->tier() != Tier::Pro);

    g.fillAll (current ? Theme::raised : (isSel ? Theme::panel : Theme::bg));
    g.setColour (Theme::line.withAlpha (0.6f));
    g.fillRect (0, h - 1, w, 1);

    // play circle
    juce::Rectangle<float> c (14.0f, h * 0.5f - 16.0f, 32.0f, 32.0f);
    g.setColour (playing ? Theme::accent : Theme::panel2);
    g.fillEllipse (c);
    g.setColour (Theme::line2);
    if (! playing) g.drawEllipse (c, 1.0f);
    g.setColour (playing ? Theme::bg : Theme::text);
    juce::Path p;
    if (playing) { p.addRectangle (c.getCentreX() - 5, c.getCentreY() - 6, 3.5f, 12); p.addRectangle (c.getCentreX() + 1.5f, c.getCentreY() - 6, 3.5f, 12); }
    else p.addTriangle (c.getCentreX() - 4, c.getCentreY() - 7, c.getCentreX() - 4, c.getCentreY() + 7, c.getCentreX() + 7, c.getCentreY());
    g.fillPath (p);

    // columns
    const int colBpm = w - 330, colKey = w - 270, colGenre = w - 220, colBadges = w - 110;
    auto nameArea = juce::Rectangle<int> (60, 6, colBpm - 70, h - 12);
    g.setColour (Theme::text);
    g.setFont (Theme::font (14.0f, true));
    g.drawText (it.name, nameArea.removeFromTop (nameArea.getHeight() / 2 + 2), juce::Justification::bottomLeft, true);
    g.setColour (Theme::muted);
    g.setFont (Theme::font (11.5f));
    juce::String meta = it.category;
    if (it.bars > 0) meta << "  |  " << it.bars << " takt";
    if (proc.catalog->isCached (it)) meta << "  |  yuklangan";
    g.drawText (meta, nameArea, juce::Justification::topLeft, true);

    g.setFont (Theme::font (13.0f));
    g.setColour (Theme::text);
    g.drawText (it.bpm > 0 ? juce::String (it.bpm, std::abs (it.bpm - std::floor (it.bpm)) < 0.01 ? 0 : 1) : "-", colBpm, 0, 56, h, juce::Justification::centredLeft);
    g.drawText (it.key.isNotEmpty() ? it.key : "-", colKey, 0, 46, h, juce::Justification::centredLeft);
    g.setColour (Theme::text2);
    g.setFont (Theme::font (12.0f));
    g.drawText (it.genre, colGenre, 0, 104, h, juce::Justification::centredLeft, true);

    float bx = (float) colBadges;
    auto by = h * 0.5f - 9.0f;
    if (it.isFree) { Theme::badge (g, { bx, by, 46, 18 }, "BEPUL", Theme::blueDim, Theme::blue); bx += 50; }
    if (it.isEarly) { Theme::badge (g, { bx, by, 36, 18 }, "PRO", Theme::accentDim, Theme::accentTxt); bx += 40; }
    if (it.isNew && ! it.isEarly) { Theme::badge (g, { bx, by, 46, 18 }, "YANGI", Theme::tealDim, Theme::tealTxt); bx += 50; }
    if (! usable)
    {
        // lock icon
        g.setColour (Theme::accentTxt);
        juce::Rectangle<float> lk ((float) w - 26.0f, h * 0.5f - 2.0f, 12.0f, 9.0f);
        g.fillRoundedRectangle (lk, 1.5f);
        juce::Path arc; arc.addCentredArc (lk.getCentreX(), lk.getY(), 4.0f, 4.5f, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
        g.strokePath (arc, juce::PathStrokeType (1.6f));
    }
}

void LibraryView::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (! juce::isPositiveAndBelow (row, filtered.size())) return;
    const auto& it = filtered.getReference (row);
    if (it.id == proc.getSelectedId())
        proc.setPreviewPlaying (! proc.isPreviewPlaying());
    else
        proc.selectItem (it, true);   // listening is always free
    list.repaint();
}

void LibraryView::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e) { listBoxItemClicked (row, e); }

void LibraryView::updatePlayer()
{
    const auto& s = proc.getSelected();
    playerTitle.setText (s.id.isEmpty() ? "Loop tanlanmagan" : s.name, juce::dontSendNotification);
    juce::String sub;
    if (s.id.isNotEmpty())
    {
        if (s.bpm > 0)
        {
            sub << juce::String (s.bpm, 0) << " BPM";
            auto eb = proc.effectiveBpm();
            if (std::abs (eb - s.bpm) > 0.01) sub << " -> " << juce::String (eb, 1) << " (DAW)";
        }
        else sub << "One-shot";
        auto k = proc.currentKeyName();
        if (k.isNotEmpty()) sub << "  |  " << k;
        if (! proc.isSelectedLoaded()) sub << "  |  yuklanmoqda...";
    }
    playerSub.setText (sub, juce::dontSendNotification);
    playBtn.setButtonText (proc.isPreviewPlaying() ? "Stop" : "Play");

    if (proc.license->tier() == Tier::Free)
        usage.setText ("Bepul: " + juce::String (proc.license->usedFreeCount()) + " / "
                       + juce::String (proc.license->getFreeLimit()) + " loop ishlatildi", juce::dontSendNotification);
    else
        usage.setText (tierName (proc.license->tier()) + " - cheksiz", juce::dontSendNotification);

    dragBox.locked = s.id.isNotEmpty() && (! proc.license->canUseLoop (s.id, s.isFree)
                                           || (s.isEarly && proc.license->tier() != Tier::Pro));
    dragBox.subtitle = dragBox.locked ? "limit tugadi - obuna oling" : "trekka tashlang";
    dragBox.repaint();

    if (s.id != lastSelectedId || proc.isPreviewPlaying() != lastPlaying)
    {
        lastSelectedId = s.id;
        lastPlaying = proc.isPreviewPlaying();
        list.repaint();
    }
    wave.refresh();
}

void LibraryView::tick()
{
    if (proc.isPreviewPlaying() != lastPlaying)
        updatePlayer();
    wave.repaint();
}

void LibraryView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (Theme::panel.darker (0.15f));
    g.fillRect (r.removeFromLeft (230));
    g.setColour (Theme::line);
    g.fillRect (230, 0, 1, getHeight());

    // list header
    auto main = getLocalBounds().withTrimmedLeft (231).withTrimmedBottom (150);
    g.setColour (Theme::muted);
    g.setFont (Theme::font (11.0f, true));
    const int w = main.getWidth(), y = 64;
    const int x0 = main.getX();
    g.drawText ("NOMI", x0 + 60, y, 200, 22, juce::Justification::centredLeft);
    g.drawText ("BPM", x0 + w - 330, y, 50, 22, juce::Justification::centredLeft);
    g.drawText ("TON", x0 + w - 270, y, 50, 22, juce::Justification::centredLeft);
    g.drawText ("JANR", x0 + w - 220, y, 80, 22, juce::Justification::centredLeft);
    g.setColour (Theme::line);
    g.fillRect (x0, y + 24, w, 1);

    // player panel
    auto pl = getLocalBounds().withTrimmedLeft (231).removeFromBottom (150);
    g.setColour (Theme::panel);
    g.fillRect (pl);
    g.setColour (Theme::line);
    g.fillRect (pl.removeFromTop (1));

    if (filtered.isEmpty())
    {
        g.setColour (Theme::muted);
        g.setFont (Theme::font (13.0f));
        auto msg = proc.catalog->isLoading() ? "Kutubxona yuklanmoqda..."
                 : proc.catalog->getItems().isEmpty() ? "Kutubxona bo'sh yoki internet yo'q. \"Yangilash\"ni bosing."
                 : "Bu filtr bo'yicha hech narsa topilmadi";
        g.drawText (msg, main.withTrimmedTop (90).withHeight (60), juce::Justification::centred);
    }
}

void LibraryView::resized()
{
    auto r = getLocalBounds();
    auto side = r.removeFromLeft (230).reduced (14, 14);
    r.removeFromLeft (1);

    search.setBounds (side.removeFromTop (36));
    side.removeFromTop (14);
    catTitle.setBounds (side.removeFromTop (18));
    for (auto* b : catButtons)
    {
        b->setBounds (side.removeFromTop (30));
        side.removeFromTop (2);
    }
    side.removeFromTop (12);
    genreTitle.setBounds (side.removeFromTop (18));
    genre.setBounds (side.removeFromTop (32));
    side.removeFromTop (8);
    onlyFree.setBounds (side.removeFromTop (26));
    onlyMatchKey.setBounds (side.removeFromTop (26));
    refreshBtn.setBounds (side.removeFromBottom (34));

    auto player = r.removeFromBottom (150).reduced (18, 14);
    auto head = r.removeFromTop (64).reduced (18, 10);
    usage.setBounds (head.removeFromRight (260));
    listTitle.setBounds (head.removeFromTop (26));
    listSub.setBounds (head);
    r.removeFromTop (26);
    list.setBounds (r);

    auto right = player.removeFromRight (330);
    player.removeFromRight (16);
    auto titleRow = player.removeFromTop (22);
    playerTitle.setBounds (titleRow.removeFromLeft (titleRow.getWidth() / 2));
    playerSub.setBounds (titleRow);
    player.removeFromTop (6);
    auto ctl = player.removeFromBottom (30);
    playBtn.setBounds (ctl.removeFromLeft (72));
    ctl.removeFromLeft (8);
    syncBtn.setBounds (ctl.removeFromLeft (64));
    ctl.removeFromLeft (14);
    pitchLbl.setBounds (ctl.removeFromLeft (44));
    pitch.setBounds (ctl.removeFromLeft (juce::jmax (120, ctl.getWidth() / 2 - 50)));
    ctl.removeFromLeft (10);
    volLbl.setBounds (ctl.removeFromLeft (40));
    volume.setBounds (ctl);
    player.removeFromBottom (8);
    wave.setBounds (player);

    auto row1 = right.removeFromTop (right.getHeight() / 2 - 4);
    right.removeFromTop (8);
    toModernize.setBounds (row1.removeFromLeft (row1.getWidth() * 2 / 3 - 4));
    row1.removeFromLeft (8);
    toPad.setBounds (row1);
    dragBox.setBounds (right);
}
