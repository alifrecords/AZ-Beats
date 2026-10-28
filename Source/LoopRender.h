#pragma once
#include <juce_audio_formats/juce_audio_formats.h>

// A decoded audio file
struct AudioClip
{
    juce::AudioBuffer<float> buffer;  // always 2 channels
    double sampleRate = 44100.0;
    juce::String name;

    bool isEmpty() const { return buffer.getNumSamples() == 0; }
    double seconds() const { return buffer.getNumSamples() / sampleRate; }
};

// A loop prepared for playback at the host tempo / pitch
struct RenderedLoop
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 44100.0;
    double beats = 0.0;          // length in beats (0 = one-shot, no sync)
    double bpm = 0.0;            // tempo it was rendered at
    float semitones = 0.0f;
    juce::String id, name, key;
};

namespace LoopRender
{
    bool loadFile (const juce::File& f, AudioClip& out, juce::String& error);

    // Number of beats in a loop: from bars if known, else from duration and bpm.
    double beatsFor (double seconds, double bpm, int bars);

    // Time-stretch + pitch-shift. If 'seamless' the loop is treated as cyclic
    // so the result loops without clicks. Output length = exactly
    // beats * 60 / targetBpm seconds (when bpm > 0).
    juce::AudioBuffer<float> render (const AudioClip& src, double srcBpm, double beats,
                                     double targetBpm, double targetSampleRate,
                                     float semitones, bool seamless);

    // Name for a key transposed by semitones ("G" + 2 -> "A", "Am" -1 -> "G#m")
    juce::String transposeKey (const juce::String& key, int semitones);

    bool writeWav (const juce::AudioBuffer<float>& buffer, double sampleRate,
                   const juce::File& file, int bits = 24);

    // min/max peaks for drawing (numPoints pairs)
    std::vector<float> peaks (const juce::AudioBuffer<float>& buffer, int numPoints);
}
