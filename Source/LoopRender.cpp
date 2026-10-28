#include "LoopRender.h"
#include "signalsmith-stretch/signalsmith-stretch.h"

namespace LoopRender
{

bool loadFile (const juce::File& f, AudioClip& out, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr)
    {
        error = "Faylni o'qib bo'lmadi: " + f.getFileName();
        return false;
    }

    const auto len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60 * 12));
    if (len <= 0)
    {
        error = "Fayl bo'sh";
        return false;
    }

    juce::AudioBuffer<float> tmp ((int) juce::jmax (1u, reader->numChannels), len);
    reader->read (&tmp, 0, len, 0, true, true);

    out.buffer.setSize (2, len);
    out.buffer.copyFrom (0, 0, tmp, 0, 0, len);
    out.buffer.copyFrom (1, 0, tmp, tmp.getNumChannels() > 1 ? 1 : 0, 0, len);
    out.sampleRate = reader->sampleRate;
    out.name = f.getFileNameWithoutExtension();
    return true;
}

double beatsFor (double seconds, double bpm, int bars)
{
    if (bpm <= 0.0) return 0.0;
    if (bars > 0)   return bars * 4.0;
    auto b = seconds * bpm / 60.0;
    return juce::jmax (1.0, std::round (b));
}

juce::AudioBuffer<float> render (const AudioClip& src, double srcBpm, double beats,
                                 double targetBpm, double targetSr, float semitones, bool seamless)
{
    const int inLen = src.buffer.getNumSamples();
    if (inLen == 0)
        return {};

    // output length
    int outLen;
    if (srcBpm > 0.0 && targetBpm > 0.0 && beats > 0.0)
        outLen = (int) std::round (beats * 60.0 / targetBpm * targetSr);
    else
        outLen = (int) std::round (inLen * targetSr / src.sampleRate);

    outLen = juce::jmax (1, outLen);

    const double timeRatio = (double) outLen / (inLen * targetSr / src.sampleRate);
    const bool sameRate  = std::abs (targetSr - src.sampleRate) < 0.5;
    const bool noStretch = std::abs (timeRatio - 1.0) < 1.0e-4;

    juce::AudioBuffer<float> out (2, outLen);
    out.clear();

    if (sameRate && noStretch && std::abs (semitones) < 0.01f)
    {
        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, 0, src.buffer, c, 0, juce::jmin (inLen, outLen));
        return out;
    }

    // The stretcher treats input as if it were at targetSr, so compensate the
    // pitch for the sample-rate difference.
    const double transpose = std::pow (2.0, semitones / 12.0) * (src.sampleRate / targetSr);

    signalsmith::stretch::SignalsmithStretch<float> stretch;
    stretch.presetDefault (2, (float) targetSr);
    stretch.setTransposeFactor ((float) transpose);

    if (seamless)
    {
        // Process three copies and keep the middle one -> click-free loop.
        juce::AudioBuffer<float> in3 (2, inLen * 3);
        juce::AudioBuffer<float> out3 (2, outLen * 3);
        for (int k = 0; k < 3; ++k)
            for (int c = 0; c < 2; ++c)
                in3.copyFrom (c, k * inLen, src.buffer, c, 0, inLen);

        stretch.exact (in3.getArrayOfReadPointers(), in3.getNumSamples(),
                       out3.getArrayOfWritePointers(), out3.getNumSamples());

        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, 0, out3, c, outLen, outLen);
    }
    else
    {
        stretch.exact (src.buffer.getArrayOfReadPointers(), inLen,
                       out.getArrayOfWritePointers(), outLen);
    }
    return out;
}

juce::String transposeKey (const juce::String& key, int semitones)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (key.isEmpty() || semitones == 0)
        return key;

    auto k = key.trim();
    bool minor = k.endsWithIgnoreCase ("m") && ! k.endsWithIgnoreCase ("maj");
    auto root = minor ? k.dropLastCharacters (1) : k;
    root = root.replace ("b", "-flat");

    int idx = -1;
    for (int i = 0; i < 12; ++i)
        if (root.equalsIgnoreCase (names[i]))
            idx = i;

    static const char* flats[] = { "C", "D-flat", "D", "E-flat", "E", "F", "G-flat", "G", "A-flat", "A", "B-flat", "B" };
    if (idx < 0)
        for (int i = 0; i < 12; ++i)
            if (root.equalsIgnoreCase (flats[i]))
                idx = i;

    if (idx < 0)
        return key;

    idx = ((idx + semitones) % 12 + 12) % 12;
    return juce::String (names[idx]) + (minor ? "m" : "");
}

bool writeWav (const juce::AudioBuffer<float>& buffer, double sampleRate, const juce::File& file, int bits)
{
    file.deleteFile();
    file.getParentDirectory().createDirectory();
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), sampleRate, (unsigned int) buffer.getNumChannels(), bits, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();
    return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

std::vector<float> peaks (const juce::AudioBuffer<float>& buffer, int numPoints)
{
    std::vector<float> p ((size_t) numPoints * 2, 0.0f);
    const int n = buffer.getNumSamples();
    if (n == 0 || numPoints <= 0)
        return p;

    for (int i = 0; i < numPoints; ++i)
    {
        int a = (int) ((juce::int64) i * n / numPoints);
        int b = juce::jmax (a + 1, (int) ((juce::int64) (i + 1) * n / numPoints));
        float lo = 0, hi = 0;
        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            auto r = buffer.findMinMax (c, a, b - a);
            lo = juce::jmin (lo, r.getStart());
            hi = juce::jmax (hi, r.getEnd());
        }
        p[(size_t) i * 2] = lo;
        p[(size_t) i * 2 + 1] = hi;
    }
    return p;
}

} // namespace LoopRender
