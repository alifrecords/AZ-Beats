// Renders the AZ BETAS editor tabs to PNG files (for checking the UI headless).
//   AZBetasScreenshot <outDir> [catalogUrl] [licenceKey]
#include "PluginProcessor.h"
#include "PluginEditor.h"

static void pump (int ms)
{
    auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::File out (argc > 1 ? argv[1] : "/tmp");
    out.createDirectory();

    AZBetasProcessor proc;
    if (argc > 2)
    {
        proc.catalog->setUrl (argv[2]);
        for (int i = 0; i < 100 && proc.catalog->isLoading(); ++i) pump (100);
        proc.catalog->refresh();
    }
    if (argc > 3) { auto info = proc.license->activate (argv[3]); std::cout << "licence: " << info.valid << " " << info.error << "\n"; }
    proc.prepareToPlay (48000.0, 512);
    for (int i = 0; i < 100 && proc.catalog->isLoading(); ++i) pump (100);
    pump (300);
    std::cout << proc.catalog->getStatus() << "\n";

    auto items = proc.catalog->getItems();
    if (! items.isEmpty()) proc.selectItem (items[0], true);
    for (int i = 0; i < 100 && ! proc.isSelectedLoaded(); ++i) pump (100);
    pump (1500);

    // simulate a little playback so the playhead moves
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    for (int i = 0; i < 300; ++i) proc.processBlock (buf, midi);
    std::cout << "rms after play: " << buf.getRMSLevel (0, 0, 512) << "\n";

    std::unique_ptr<AZBetasEditor> ed (dynamic_cast<AZBetasEditor*> (proc.createEditor()));
    ed->setSize (1160, 740);
    pump (500);

    auto snap = [&] (const juce::String& name)
    {
        pump (400);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        juce::FileOutputStream os (out.getChildFile (name + ".png"));
        os.setPosition (0); os.truncate();
        png.writeImageToStream (img, os);
    };

    snap ("1_kutubxona");
    if (auto* lib = ed->findChildWithID ("library"))
        if (auto* lv = dynamic_cast<LibraryView*> (lib)) { lv->setCategory (4); snap ("1b_pack"); lv->setCategory (0); }
    juce::String err;
    juce::String padErr;
    proc.assignSelectedToPad (0, padErr);
    ed->showTab (1);
    if (proc.modernizeTakeSelected (err))
    {
        proc.modernizeRun();
        for (int i = 0; i < 200 && proc.isModernizeBusy(); ++i) pump (100);
    }
    else std::cout << "modernize: " << err << "\n";
    snap ("2_modernize");
    ed->showTab (2);
    snap ("3_pad");
    ed->showTab (3);
    snap ("4_akkaunt");
    ed = nullptr;
    pump (200);
    return 0;
}
