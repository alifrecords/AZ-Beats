#include "Modernize.h"
#include <juce_dsp/juce_dsp.h>
#include <deque>

namespace Modernize
{

// ---------------------------------------------------------------- names ---
juce::String bandName (int i)
{
    static const char* n[] = { "< 60 Hz", "60–120 Hz", "120–500 Hz", "500 Hz – 2 kHz", "2–10 kHz", "10–20 kHz" };
    return juce::String::fromUTF8 (n[juce::jlimit (0, numBands - 1, i)]);
}

static const float bandEdges[numBands + 1] = { 20.f, 60.f, 120.f, 500.f, 2000.f, 10000.f, 20000.f };

juce::String styleName (int s)
{
    static const char* n[] = { "Avto", "O'zbek pop", "Trap / Drill", "Afro House", "Lo-fi", "Akustik / jonli" };
    return n[juce::jlimit (0, numStyles - 1, s)];
}

float styleTargetLufs (int s)
{
    static const float t[] = { -12.f, -11.f, -9.f, -10.f, -14.f, -16.f };
    return t[juce::jlimit (0, numStyles - 1, s)];
}

juce::String noteName (int pc)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[((pc % 12) + 12) % 12];
}

// ------------------------------------------------------------- biquads ---
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};

    void set (double B0, double B1, double B2, double A0, double A1, double A2)
    {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    float tick (int ch, float x)
    {
        double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }

    static Biquad highPass (double sr, double f, double q)
    {
        Biquad b; double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        b.set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al); return b;
    }
    static Biquad lowPass (double sr, double f, double q)
    {
        Biquad b; double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        b.set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al); return b;
    }
    static Biquad peak (double sr, double f, double q, double gainDb)
    {
        Biquad b; double A = std::pow (10.0, gainDb / 40), w = juce::MathConstants<double>::twoPi * f / sr;
        double c = std::cos (w), al = std::sin (w) / (2 * q);
        b.set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A); return b;
    }
    static Biquad highShelf (double sr, double f, double q, double gainDb)
    {
        Biquad b; double A = std::pow (10.0, gainDb / 40), w = juce::MathConstants<double>::twoPi * f / sr;
        double c = std::cos (w), al = std::sin (w) / (2 * q), sA = 2 * std::sqrt (A) * al;
        b.set (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
               (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
        return b;
    }
};

static void applyBiquad (juce::AudioBuffer<float>& b, Biquad f)
{
    f.reset();
    for (int c = 0; c < juce::jmin (2, b.getNumChannels()); ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < b.getNumSamples(); ++i)
            d[i] = f.tick (c, d[i]);
    }
}

// ---------------------------------------------------------- loudness ---
float integratedLufs (const juce::AudioBuffer<float>& src, double sr, int start, int len)
{
    if (len < 0) len = src.getNumSamples() - start;
    if (len <= 0) return -70.0f;

    juce::AudioBuffer<float> b (2, len);
    for (int c = 0; c < 2; ++c)
        b.copyFrom (c, 0, src, juce::jmin (c, src.getNumChannels() - 1), start, len);

    applyBiquad (b, Biquad::highShelf (sr, 1500.0, 1.0 / std::sqrt (2.0), 4.0));
    applyBiquad (b, Biquad::highPass (sr, 38.0, 0.5));

    const int block = (int) (0.4 * sr), step = (int) (0.1 * sr);
    std::vector<double> z;
    if (len < block)
    {
        double s = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < len; ++i) s += (double) b.getSample (c, i) * b.getSample (c, i);
        z.push_back (s / len);
    }
    else
    {
        for (int p = 0; p + block <= len; p += step)
        {
            double s = 0;
            for (int c = 0; c < 2; ++c)
            {
                auto* d = b.getReadPointer (c, p);
                for (int i = 0; i < block; ++i) s += (double) d[i] * d[i];
            }
            z.push_back (s / block);
        }
    }

    auto loud = [] (double ms) { return -0.691 + 10.0 * std::log10 (juce::jmax (ms, 1.0e-12)); };

    double sum = 0; int n = 0;
    for (auto v : z) if (loud (v) > -70.0) { sum += v; ++n; }
    if (n == 0) return -70.0f;
    const double rel = loud (sum / n) - 10.0;

    sum = 0; n = 0;
    for (auto v : z) if (loud (v) > -70.0 && loud (v) > rel) { sum += v; ++n; }
    if (n == 0) return -70.0f;
    return (float) loud (sum / n);
}

float truePeakDb (const juce::AudioBuffer<float>& src, double)
{
    juce::AudioBuffer<float> b (2, src.getNumSamples());
    for (int c = 0; c < 2; ++c)
        b.copyFrom (c, 0, src, juce::jmin (c, src.getNumChannels() - 1), 0, src.getNumSamples());

    juce::dsp::Oversampling<float> os (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
    const int blockSize = 4096;
    os.initProcessing ((size_t) blockSize);
    float mx = 0;
    for (int p = 0; p < b.getNumSamples(); p += blockSize)
    {
        const int n = juce::jmin (blockSize, b.getNumSamples() - p);
        juce::dsp::AudioBlock<float> blk (b.getArrayOfWritePointers(), 2, (size_t) p, (size_t) n);
        auto up = os.processSamplesUp (blk);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* d = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                mx = juce::jmax (mx, std::abs (d[i]));
        }
    }
    mx = juce::jmax (mx, b.getMagnitude (0, b.getNumSamples()));
    return juce::Decibels::gainToDecibels (mx, -100.0f);
}

// ------------------------------------------------------------ spectrum ---
static std::vector<double> powerSpectrum (const juce::AudioBuffer<float>& b, double, int order)
{
    const int N = 1 << order;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> win ((size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<double> acc ((size_t) N / 2 + 1, 0.0);
    std::vector<float> buf ((size_t) N * 2);
    const int len = b.getNumSamples();
    const int hop = N / 2;
    int frames = 0;
    for (int p = 0; p < juce::jmax (1, len - N + 1); p += hop)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N && p + i < len; ++i)
        {
            float m = 0;
            for (int c = 0; c < b.getNumChannels(); ++c) m += b.getSample (c, p + i);
            buf[(size_t) i] = m / (float) b.getNumChannels();
        }
        win.multiplyWithWindowingTable (buf.data(), (size_t) N);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        for (int k = 0; k <= N / 2; ++k)
            acc[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
        ++frames;
    }
    return acc;
}

// ------------------------------------------------------------- kicks ---
struct Hit { int pos; float strength; };

static std::vector<Hit> detectKicks (const juce::AudioBuffer<float>& b, double sr)
{
    const int len = b.getNumSamples();
    std::vector<float> mono ((size_t) len);
    for (int i = 0; i < len; ++i)
        mono[(size_t) i] = 0.5f * (b.getSample (0, i) + b.getSample (b.getNumChannels() > 1 ? 1 : 0, i));

    auto hp = Biquad::highPass (sr, 40.0, 0.707), lp1 = Biquad::lowPass (sr, 130.0, 0.707), lp2 = Biquad::lowPass (sr, 130.0, 0.707);
    for (auto& v : mono) v = lp2.tick (0, lp1.tick (0, hp.tick (0, v)));

    const int hop = 128;
    const int frames = len / hop;
    if (frames < 4) return {};
    std::vector<float> e ((size_t) frames, 0.0f);
    for (int f = 0; f < frames; ++f)
    {
        double s = 0;
        for (int i = 0; i < hop; ++i) { float v = mono[(size_t) (f * hop + i)]; s += v * v; }
        e[(size_t) f] = (float) std::sqrt (s / hop);
    }
    const float emax = *std::max_element (e.begin(), e.end());
    if (emax <= 1.0e-6f) return {};

    std::vector<float> o ((size_t) frames, 0.0f);
    for (int f = 1; f < frames; ++f) o[(size_t) f] = juce::jmax (0.0f, e[(size_t) f] - e[(size_t) f - 1]);
    const float omax = *std::max_element (o.begin(), o.end());

    std::vector<Hit> hits;
    const int minGap = (int) (0.08 * sr / hop);
    int last = -1000000;
    for (int f = 1; f < frames - 1; ++f)
    {
        if (o[(size_t) f] < 0.15f * omax || o[(size_t) f] < o[(size_t) f - 1] || o[(size_t) f] < o[(size_t) f + 1])
            continue;
        float pk = 0;
        for (int k = f; k < juce::jmin (frames, f + 20); ++k) pk = juce::jmax (pk, e[(size_t) k]);
        float strength = pk / emax;
        if (strength < 0.5f || f - last < minGap) continue;
        hits.push_back ({ f * hop, strength });
        last = f;
    }
    return hits;
}

// ----------------------------------------------------------- analyse ---
Analysis analyse (const juce::AudioBuffer<float>& b, double sr, bool seamless)
{
    Analysis a;
    const int len = b.getNumSamples();
    a.seconds = len / sr;
    if (len == 0) return a;

    auto ps = powerSpectrum (b, sr, 13);
    const double binHz = sr / 8192.0;
    double total = 0;
    double bandE[numBands] {};
    for (size_t k = 1; k < ps.size(); ++k)
    {
        double f = k * binHz;
        if (f < bandEdges[0] || f >= bandEdges[numBands]) continue;
        total += ps[k];
        for (int i = 0; i < numBands; ++i)
            if (f >= bandEdges[i] && f < bandEdges[i + 1]) bandE[i] += ps[k];
    }
    for (int i = 0; i < numBands; ++i)
        a.bands[i] = total > 0 ? (float) (100.0 * bandE[i] / total) : 0.0f;

    // bass peak 40-250 Hz
    double best = 0;
    for (size_t k = 1; k < ps.size(); ++k)
    {
        double f = k * binHz;
        if (f >= 40 && f <= 250 && ps[k] > best) { best = ps[k]; a.bassPeakHz = (float) f; }
    }

    a.lufs = integratedLufs (b, sr);
    a.truePeakDb = truePeakDb (b, sr);
    a.peakDb = juce::Decibels::gainToDecibels (b.getMagnitude (0, len), -100.0f);
    double ms = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        auto r = b.getRMSLevel (c, 0, len);
        ms += r * r;
    }
    a.rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (ms / b.getNumChannels()), -100.0f);
    a.crestDb = a.peakDb - a.rmsDb;

    if (b.getNumChannels() > 1)
    {
        double sl = 0, sr2 = 0, slr = 0;
        auto* L = b.getReadPointer (0); auto* R = b.getReadPointer (1);
        for (int i = 0; i < len; ++i) { sl += L[i] * L[i]; sr2 += R[i] * R[i]; slr += L[i] * R[i]; }
        a.correlation = (sl > 0 && sr2 > 0) ? (float) (slr / std::sqrt (sl * sr2)) : 1.0f;
    }

    a.kicks = (int) detectKicks (b, sr).size();

    // ---- diagnosis rows (simple table: problem -> fix)
    auto add = [&a] (juce::String p, juce::String f) { a.diagnosis.add ({ p, f }); };
    if (a.bands[0] < 5.0f)
        add ("Sub yo'q (" + juce::String (a.bands[0], 1) + "%)", "Kick ostiga sub qo'shish");
    if (a.bands[2] > 45.0f)
        add ("120-500 Hz loyqa (" + juce::String (a.bands[2], 0) + "%)", "200 Hz atrofini kesish");
    {
        // 2-10 kHz share is low when below ~8 %
        if (a.bands[4] < 5.0f)
            add ("Aniqlik kam (2-10 kHz " + juce::String (a.bands[4], 1) + "%)", "3 kHz'ni ko'tarish");
    }
    if (a.bands[5] < 4.0f)
        add ("Havo kam (10 kHz+)", "12 kHz shelf ko'tarish");
    if (a.lufs < -16.0f)
        add ("Balandlik past (" + juce::String (a.lufs, 1) + " LUFS)", "Loudness'ni zamonaviy darajaga chiqarish");
    if (a.truePeakDb > -0.5f)
        add ("True Peak xavfli (" + juce::String (a.truePeakDb, 1) + " dBTP)", "Limiter bilan -1 dBTP");
    if (a.crestDb > 20.0f)
        add ("Dinamika juda keng (crest " + juce::String (a.crestDb, 0) + " dB)", "Parallel kompressiya");
    if (a.correlation < 0.2f)
        add ("Stereo faza muammosi (" + juce::String (a.correlation, 2) + ")", "Kenglikni kamaytirish");
    if (seamless && len > 64)
    {
        float jump = 0;
        for (int c = 0; c < b.getNumChannels(); ++c)
            jump = juce::jmax (jump, std::abs (b.getSample (c, 0) - b.getSample (c, len - 1)));
        if (jump > 0.3f)
            add ("Loop chokida klik bo'lishi mumkin", "Wrap-around ishlov (avtomatik)");
    }
    if (a.diagnosis.isEmpty())
        add ("Jiddiy muammo topilmadi", "Faqat yengil sayqal");
    return a;
}

Settings autoSettings (const Analysis& a, int style, bool seamless)
{
    Settings s;
    s.style = style;
    s.seamless = seamless;
    s.targetLufs = styleTargetLufs (style);

    s.subOn = a.bands[0] < 5.0f && a.kicks >= 1;
    s.subLevelDb = a.bands[0] < 2.0f ? -16.0f : -19.0f;
    if (style == Trap) s.subLevelDb += 3.0f;
    if (style == Acoustic || style == LoFi) s.subOn = s.subOn && a.bands[0] < 1.0f;

    s.mudDb = a.bands[2] > 45.0f ? -juce::jlimit (1.0f, 4.0f, (a.bands[2] - 40.0f) / 8.0f) : 0.0f;
    s.presenceDb = a.bands[4] < 4.0f ? 3.0f : (a.bands[4] < 8.0f ? 2.0f : 0.0f);
    s.airDb = a.bands[5] < 4.0f ? 2.0f : (a.bands[5] < 8.0f ? 0.5f : 0.0f);
    if (style == LoFi) { s.airDb = juce::jmin (s.airDb, 0.0f) - 1.5f; s.presenceDb = juce::jmin (s.presenceDb, 1.0f); }

    s.transient = a.crestDb < 12.0f ? 25.0f : 10.0f;
    s.parallel  = a.crestDb > 18.0f ? 35.0f : 20.0f;
    if (style == Acoustic) { s.parallel *= 0.5f; s.transient = 0.0f; }
    s.width = a.correlation < 0.2f ? 70.0f : 100.0f;
    s.ceilingDb = -1.0f;
    return s;
}

// ----------------------------------------------------- value tree I/O ---
juce::ValueTree Settings::toValueTree() const
{
    juce::ValueTree v ("MODERNIZE");
    v.setProperty ("style", style, nullptr);       v.setProperty ("subOn", subOn, nullptr);
    v.setProperty ("subLevel", subLevelDb, nullptr); v.setProperty ("subNote", subNote, nullptr);
    v.setProperty ("hpf", hpfHz, nullptr);         v.setProperty ("mud", mudDb, nullptr);
    v.setProperty ("presence", presenceDb, nullptr); v.setProperty ("air", airDb, nullptr);
    v.setProperty ("transient", transient, nullptr); v.setProperty ("parallel", parallel, nullptr);
    v.setProperty ("width", width, nullptr);       v.setProperty ("target", targetLufs, nullptr);
    v.setProperty ("ceiling", ceilingDb, nullptr); v.setProperty ("seamless", seamless, nullptr);
    return v;
}

void Settings::fromValueTree (const juce::ValueTree& v)
{
    if (! v.isValid()) return;
    style = v.getProperty ("style", style);           subOn = v.getProperty ("subOn", subOn);
    subLevelDb = v.getProperty ("subLevel", subLevelDb); subNote = v.getProperty ("subNote", subNote);
    hpfHz = v.getProperty ("hpf", hpfHz);             mudDb = v.getProperty ("mud", mudDb);
    presenceDb = v.getProperty ("presence", presenceDb); airDb = v.getProperty ("air", airDb);
    transient = v.getProperty ("transient", transient); parallel = v.getProperty ("parallel", parallel);
    width = v.getProperty ("width", width);           targetLufs = v.getProperty ("target", targetLufs);
    ceilingDb = v.getProperty ("ceiling", ceilingDb); seamless = v.getProperty ("seamless", seamless);
}

// ----------------------------------------------------------- limiter ---
// Brick-wall look-ahead limiter at 4x oversampling.
static void limit (juce::AudioBuffer<float>& b, double sr, float ceilingDb)
{
    using OS = juce::dsp::Oversampling<float>;
    const float ceiling = juce::Decibels::decibelsToGain (ceilingDb);
    const int len = b.getNumSamples();
    const int blockSize = 2048;

    // pass 1: oversampled required gain
    OS os (2, 2, OS::filterHalfBandFIREquiripple, true);
    os.initProcessing ((size_t) blockSize);
    const int lat = (int) std::ceil (os.getLatencyInSamples());

    juce::AudioBuffer<float> padded (2, len + lat + blockSize);
    padded.clear();
    for (int c = 0; c < 2; ++c) padded.copyFrom (c, 0, b, c, 0, len);
    const int plen = padded.getNumSamples();

    std::vector<float> req;
    req.reserve ((size_t) plen * 4);
    juce::AudioBuffer<float> work (2, blockSize);
    for (int p = 0; p < plen; p += blockSize)
    {
        const int n = juce::jmin (blockSize, plen - p);
        for (int c = 0; c < 2; ++c) work.copyFrom (c, 0, padded, c, p, n);
        juce::dsp::AudioBlock<float> blk (work.getArrayOfWritePointers(), 2, 0, (size_t) n);
        auto up = os.processSamplesUp (blk);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            float m = juce::jmax (std::abs (up.getSample (0, (int) i)), std::abs (up.getSample (1, (int) i)));
            req.push_back (m > ceiling ? ceiling / m : 1.0f);
        }
    }

    // gain curve: min-filter (look-ahead), release, moving average
    const double osr = sr * 4.0;
    const int L = juce::jmax (1, (int) (0.0015 * osr));
    const float rel = (float) (1.0 - std::exp (-1.0 / (0.08 * osr)));
    const size_t N = req.size();
    std::vector<float> h (N), g (N);
    std::deque<size_t> dq;
    float prev = 1.0f;
    for (size_t n = 0; n < N; ++n)
    {
        while (! dq.empty() && req[dq.back()] >= req[n]) dq.pop_back();
        dq.push_back (n);
        while (dq.front() + (size_t) L < n) dq.pop_front();
        float m = req[dq.front()];
        prev = juce::jmin (m, prev + (1.0f - prev) * rel);
        h[n] = prev;
    }
    double acc = 0;
    for (size_t n = 0; n < N; ++n)
    {
        acc += h[n];
        if (n >= (size_t) L + 1) acc -= h[n - (size_t) L - 1];
        g[n] = (float) (acc / (double) juce::jmin<size_t> (n + 1, (size_t) L + 1));
    }

    // pass 2: apply gain (advanced by L so it lines up with the peaks), downsample
    OS os2 (2, 2, OS::filterHalfBandFIREquiripple, true);
    os2.initProcessing ((size_t) blockSize);
    juce::AudioBuffer<float> out (2, plen);
    size_t gi = 0;
    for (int p = 0; p < plen; p += blockSize)
    {
        const int n = juce::jmin (blockSize, plen - p);
        for (int c = 0; c < 2; ++c) work.copyFrom (c, 0, padded, c, p, n);
        juce::dsp::AudioBlock<float> blk (work.getArrayOfWritePointers(), 2, 0, (size_t) n);
        auto up = os2.processSamplesUp (blk);
        for (size_t i = 0; i < up.getNumSamples(); ++i, ++gi)
        {
            float gain = g[juce::jmin (N - 1, gi + (size_t) L)];
            up.setSample (0, (int) i, up.getSample (0, (int) i) * gain);
            up.setSample (1, (int) i, up.getSample (1, (int) i) * gain);
        }
        os2.processSamplesDown (blk);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, p, work, c, 0, n);
    }

    for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, out, c, lat, len);
}

// ----------------------------------------------------------- process ---
static int noteFromKey (const juce::String& key)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* f[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    auto k = key.trim();
    if (k.endsWithIgnoreCase ("m")) k = k.dropLastCharacters (1);
    for (int i = 0; i < 12; ++i)
        if (k == n[i] || k == f[i]) return i;
    return -1;
}

juce::AudioBuffer<float> process (const AudioClip& clip, const Settings& s, const juce::String& key)
{
    const double sr = clip.sampleRate;
    const int len = clip.buffer.getNumSamples();
    if (len == 0) return {};

    // ---- pad: circular for loops (seamless), silence otherwise
    const int pad = s.seamless ? juce::jmin (len, (int) (2.0 * sr)) : (int) (0.5 * sr);
    const int total = len + 2 * pad;
    juce::AudioBuffer<float> x (2, total);
    x.clear();
    for (int c = 0; c < 2; ++c)
    {
        const int sc = juce::jmin (c, clip.buffer.getNumChannels() - 1);
        x.copyFrom (c, pad, clip.buffer, sc, 0, len);
        if (s.seamless)
            for (int i = 0; i < pad; ++i)
            {
                x.setSample (c, pad - 1 - i, clip.buffer.getSample (sc, ((len - 1 - i) % len + len) % len));
                x.setSample (c, pad + len + i, clip.buffer.getSample (sc, i % len));
            }
    }

    const float origPeak = clip.buffer.getMagnitude (0, len);

    // ---- EQ
    applyBiquad (x, Biquad::highPass (sr, s.hpfHz, 0.707));
    if (std::abs (s.mudDb) > 0.05f)      applyBiquad (x, Biquad::peak (sr, 200.0, 1.4, s.mudDb));
    if (std::abs (s.presenceDb) > 0.05f) applyBiquad (x, Biquad::peak (sr, 3000.0, 0.7, s.presenceDb));
    if (std::abs (s.airDb) > 0.05f)      applyBiquad (x, Biquad::highShelf (sr, 12000.0, 0.7, s.airDb));

    // ---- transient shaper
    if (s.transient > 0.5f)
    {
        const float amt = s.transient / 100.0f;
        auto coef = [sr] (double ms) { return (float) (1.0 - std::exp (-1.0 / (ms * 0.001 * sr))); };
        const float fa = coef (0.5), fr = coef (15), sa = coef (15), sr2 = coef (150);
        float fast = 0, slow = 0;
        for (int i = 0; i < total; ++i)
        {
            float m = 0.5f * (std::abs (x.getSample (0, i)) + std::abs (x.getSample (1, i)));
            fast += (m > fast ? fa : fr) * (m - fast);
            slow += (m > slow ? sa : sr2) * (m - slow);
            float g = 1.0f + amt * juce::jlimit (0.0f, 1.0f, (fast - slow) / (slow + 1.0e-5f));
            x.setSample (0, i, x.getSample (0, i) * g);
            x.setSample (1, i, x.getSample (1, i) * g);
        }
    }

    // ---- stereo width (lows stay as they are, they are mostly mono anyway)
    if (std::abs (s.width - 100.0f) > 0.5f)
    {
        const float w = s.width / 100.0f;
        for (int i = 0; i < total; ++i)
        {
            float l = x.getSample (0, i), r = x.getSample (1, i);
            float m = 0.5f * (l + r), sd = 0.5f * (l - r) * w;
            x.setSample (0, i, m + sd);
            x.setSample (1, i, m - sd);
        }
    }

    // ---- sub under the kicks
    if (s.subOn)
    {
        // kicks detected on the original (circular) material
        auto hits = detectKicks (x, sr);
        int pc = s.subNote >= 0 ? s.subNote : noteFromKey (key);
        if (pc < 0)
        {
            auto a = analyse (clip.buffer, sr, false);
            if (a.bassPeakHz > 0)
                pc = (int) std::round (69 + 12 * std::log2 (a.bassPeakHz / 440.0));
            pc = pc < 0 ? 7 : pc;   // default G
        }
        pc = ((pc % 12) + 12) % 12;
        // choose octave so that f0 is within ~40-62 Hz
        double f0 = 440.0 * std::pow (2.0, (pc + 12 * 1 - 69 + 12) / 12.0); // octave 1
        while (f0 > 62.0) f0 *= 0.5;
        while (f0 < 40.0) f0 *= 2.0;

        juce::AudioBuffer<float> sub (1, total);
        sub.clear();
        const int L = (int) (0.42 * sr);
        std::vector<float> note ((size_t) L);
        double ph = 0;
        for (int i = 0; i < L; ++i)
        {
            double t = i / sr;
            double fq = f0 + (120.0 - f0) * std::exp (-t / 0.025);
            ph += juce::MathConstants<double>::twoPi * fq / sr;
            double amp = std::exp (-t / 0.20) * juce::jmin (1.0, t / 0.002);
            note[(size_t) i] = (float) (std::tanh (1.6 * std::sin (ph)) * amp);
        }
        for (size_t h = 0; h < hits.size(); ++h)
        {
            const int st = hits[h].pos;
            const int next = h + 1 < hits.size() ? hits[h + 1].pos : total;
            const int n = juce::jmin (L, next - st, total - st);
            const int fade = (int) (0.004 * sr);
            const float gain = 0.6f + 0.4f * hits[h].strength;
            for (int i = 0; i < n; ++i)
            {
                float v = note[(size_t) i] * gain;
                if (n < L && i > n - fade) v *= (float) (n - i) / (float) fade;
                sub.addSample (0, st + i, v);
            }
        }
        applyBiquad (sub, Biquad::highPass (sr, 28.0, 0.707));
        const float subPeak = sub.getMagnitude (0, total);
        if (subPeak > 0)
        {
            const float g = origPeak * juce::Decibels::decibelsToGain (s.subLevelDb) / subPeak;
            for (int c = 0; c < 2; ++c) x.addFrom (c, 0, sub, 0, 0, total, g);
        }
    }

    // ---- parallel compression (NY style)
    if (s.parallel > 0.5f)
    {
        juce::AudioBuffer<float> par (x);
        juce::dsp::Compressor<float> comp;
        comp.prepare ({ sr, (juce::uint32) total, 2 });
        comp.setThreshold (-30.0f); comp.setRatio (6.0f); comp.setAttack (1.0f); comp.setRelease (80.0f);
        juce::dsp::AudioBlock<float> blk (par);
        comp.process (juce::dsp::ProcessContextReplacing<float> (blk));
        const float mix = s.parallel / 100.0f * juce::Decibels::decibelsToGain (10.0f);
        for (int c = 0; c < 2; ++c) x.addFrom (c, 0, par, c, 0, total, mix);
    }

    // ---- loudness + true-peak limiting (measured on the core region only)
    juce::AudioBuffer<float> result (2, total);
    float gainDb = s.targetLufs - integratedLufs (x, sr, pad, len);
    gainDb = juce::jlimit (-30.0f, 36.0f, gainDb);
    for (int iter = 0; iter < 4; ++iter)
    {
        result.makeCopyOf (x);
        result.applyGain (juce::Decibels::decibelsToGain (gainDb));
        limit (result, sr, s.ceilingDb - 0.3f);
        const float got = integratedLufs (result, sr, pad, len);
        const float diff = s.targetLufs - got;
        if (std::abs (diff) < 0.2f) break;
        gainDb = juce::jlimit (-30.0f, 40.0f, gainDb + diff);
    }

    juce::AudioBuffer<float> out (2, len);
    for (int c = 0; c < 2; ++c) out.copyFrom (c, 0, result, c, pad, len);

    // final true-peak safety
    const float tp = truePeakDb (out, sr);
    if (tp > s.ceilingDb)
        out.applyGain (juce::Decibels::decibelsToGain (s.ceilingDb - tp - 0.05f));
    return out;
}

} // namespace Modernize
