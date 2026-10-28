// Offline test tool for AZ BETAS core (no DAW needed)
//   AZBetasTest analyse   <file>
//   AZBetasTest modernize <in> <out.wav> [style 0-5]
//   AZBetasTest render    <in> <srcBpm> <targetBpm> <semitones> <out.wav>
//   AZBetasTest verify    <licence key>
//   AZBetasTest catalog   [url]
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "License.h"
#include "Catalog.h"
#include "LoopRender.h"
#include "Modernize.h"

static void printAnalysis (const Modernize::Analysis& a)
{
    std::cout << "Davomiylik: " << a.seconds << " s\n";
    for (int i = 0; i < Modernize::numBands; ++i)
        std::cout << "  " << Modernize::bandName (i) << ": " << juce::String (a.bands[i], 1) << "%\n";
    std::cout << "LUFS " << a.lufs << "  TP " << a.truePeakDb << " dBTP  crest " << a.crestDb
              << " dB  corr " << a.correlation << "  bass " << a.bassPeakHz << " Hz  kicks " << a.kicks << "\n";
    for (auto& r : a.diagnosis)
        std::cout << "  - " << r.problem << "  ->  " << r.fix << "\n";
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    if (args.isEmpty()) { std::cout << "usage: see source\n"; return 1; }

    const auto cmd = args[0];
    if (cmd == "analyse" && args.size() >= 2)
    {
        AudioClip clip; juce::String err;
        if (! LoopRender::loadFile (juce::File (args[1]), clip, err)) { std::cerr << err << "\n"; return 2; }
        printAnalysis (Modernize::analyse (clip.buffer, clip.sampleRate, true));
        return 0;
    }
    if (cmd == "modernize" && args.size() >= 3)
    {
        AudioClip clip; juce::String err;
        if (! LoopRender::loadFile (juce::File (args[1]), clip, err)) { std::cerr << err << "\n"; return 2; }
        auto a = Modernize::analyse (clip.buffer, clip.sampleRate, true);
        std::cout << "== OLDIN\n"; printAnalysis (a);
        auto s = Modernize::autoSettings (a, args.size() > 3 ? args[3].getIntValue() : 0, true);
        std::cout << "Sozlama: sub " << (s.subOn ? "ON " : "off ") << s.subLevelDb << " dB, mud " << s.mudDb
                  << ", presence " << s.presenceDb << ", air " << s.airDb << ", transient " << s.transient
                  << ", parallel " << s.parallel << ", target " << s.targetLufs << "\n";
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto out = Modernize::process (clip, s);
        std::cout << "Ishlov vaqti: " << (juce::Time::getMillisecondCounterHiRes() - t0) << " ms\n";
        LoopRender::writeWav (out, clip.sampleRate, juce::File (args[2]));
        std::cout << "== KEYIN\n"; printAnalysis (Modernize::analyse (out, clip.sampleRate, true));
        std::cout << "uzunlik oldin " << clip.buffer.getNumSamples() << " keyin " << out.getNumSamples() << "\n";
        return 0;
    }
    if (cmd == "render" && args.size() >= 6)
    {
        AudioClip clip; juce::String err;
        if (! LoopRender::loadFile (juce::File (args[1]), clip, err)) { std::cerr << err << "\n"; return 2; }
        double src = args[2].getDoubleValue(), dst = args[3].getDoubleValue();
        auto beats = LoopRender::beatsFor (clip.seconds(), src, 0);
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto out = LoopRender::render (clip, src, beats, dst, 48000.0, (float) args[4].getDoubleValue(), true);
        std::cout << "beats " << beats << " out samples " << out.getNumSamples() << " expected "
                  << beats * 60.0 / dst * 48000.0 << " time " << (juce::Time::getMillisecondCounterHiRes() - t0) << " ms\n";
        LoopRender::writeWav (out, 48000.0, juce::File (args[5]));
        std::cout << "key G +2 = " << LoopRender::transposeKey ("G", 2) << ", Am -1 = " << LoopRender::transposeKey ("Am", -1)
                  << ", Bb +1 = " << LoopRender::transposeKey ("Bb", 1) << "\n";
        return 0;
    }
    if (cmd == "verify" && args.size() >= 2)
    {
        LicenseManager lm;
        auto info = lm.verify (args[1]);
        std::cout << "valid=" << info.valid << " tier=" << tierName (info.tier) << " user=" << info.user
                  << " expires=" << info.expires << " error=" << info.error << "\n";
        return info.valid ? 0 : 3;
    }
    if (cmd == "catalog")
    {
        LibraryCatalog cat;
        if (args.size() > 1) cat.setUrl (args[1]);
        cat.refresh();
        for (int i = 0; i < 300 && cat.isLoading(); ++i)
            juce::Thread::sleep (100);
        std::cout << cat.getStatus() << "\n";
        for (auto& it : cat.getItems())
            std::cout << "  " << it.id << " | " << it.name << " | " << it.bpm << " BPM | " << it.key
                      << " | free=" << it.isFree << " demo=" << it.isDemo << " new=" << it.isNew << "\n";
        return 0;
    }
    std::cout << "unknown command\n";
    return 1;
}
