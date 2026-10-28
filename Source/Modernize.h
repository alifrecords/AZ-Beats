#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include "LoopRender.h"

// ---------------------------------------------------------------------------
// AZ Modernize: brings old loops / percussion / samples to today's sound.
// analyse() -> diagnosis, autoSettings() -> suggested fix, process() -> result
// ---------------------------------------------------------------------------

namespace Modernize
{
    static constexpr int numBands = 6;
    juce::String bandName (int i);   // "< 60 Hz", "60–120 Hz", ...

    enum Style { Auto = 0, UzPop, Trap, AfroHouse, LoFi, Acoustic, numStyles };
    juce::String styleName (int s);
    float styleTargetLufs (int s);

    struct Analysis
    {
        float bands[numBands] {};   // energy share, %
        float lufs = -70.0f;
        float truePeakDb = -100.0f;
        float peakDb = -100.0f;
        float rmsDb = -100.0f;
        float crestDb = 0.0f;
        float correlation = 1.0f;
        float bassPeakHz = 0.0f;
        int   kicks = 0;
        double seconds = 0.0;

        struct Row { juce::String problem, fix; };
        juce::Array<Row> diagnosis;
    };

    struct Settings
    {
        int   style        = Auto;
        bool  subOn        = false;
        float subLevelDb   = -12.0f;   // sub peak relative to the loop peak
        int   subNote      = -1;       // -1 auto, 0 = C ... 11 = B
        float hpfHz        = 32.0f;
        float mudDb        = 0.0f;     // bell @ 200 Hz
        float presenceDb   = 0.0f;     // bell @ 3 kHz
        float airDb        = 0.0f;     // shelf @ 12 kHz
        float transient    = 0.0f;     // 0..100 %
        float parallel     = 0.0f;     // 0..100 %
        float width        = 100.0f;   // 0..150 %
        float targetLufs   = -12.0f;
        float ceilingDb    = -1.0f;    // true-peak ceiling
        bool  seamless     = true;     // treat as a loop (wrap-around processing)

        juce::ValueTree toValueTree() const;
        void fromValueTree (const juce::ValueTree& v);
    };

    Analysis analyse (const juce::AudioBuffer<float>& buffer, double sampleRate, bool seamless);
    Settings autoSettings (const Analysis& a, int style, bool seamless);

    // key: optional musical key of the loop ("G", "Am") to tune the sub
    juce::AudioBuffer<float> process (const AudioClip& clip, const Settings& s,
                                      const juce::String& key = {});

    // helpers (exposed for tests)
    float integratedLufs (const juce::AudioBuffer<float>& b, double sr, int start = 0, int len = -1);
    float truePeakDb (const juce::AudioBuffer<float>& b, double sr);
    juce::String noteName (int pc);
}
