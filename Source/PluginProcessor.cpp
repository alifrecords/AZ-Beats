#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

juce::AudioProcessorValueTreeState::ParameterLayout AZBetasProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "volume", 1 }, "Hajm",
                     juce::NormalisableRange<float> (-48.0f, 6.0f, 0.1f), -3.0f,
                     juce::AudioParameterFloatAttributes().withLabel ("dB")));
    p.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "pitch", 1 }, "Pitch", -12, 12, 0,
                     juce::AudioParameterIntAttributes().withLabel ("st")));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sync", 1 }, "Tempo sync", true));
    return { p.begin(), p.end() };
}

AZBetasProcessor::AZBetasProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "PARAMS", createLayout())
{
    // one refresh per session is enough (catalog is shared between instances)
    static std::atomic<juce::int64> lastRefresh { 0 };
    auto now = juce::Time::currentTimeMillis();
    if (now - lastRefresh.load() > 10 * 60 * 1000)
    {
        lastRefresh = now;
        catalog->refresh();
    }
    license->setFreeLimit (catalog->getFreeLimit());
    status = catalog->getStatus();
    startTimer (250);
}

AZBetasProcessor::~AZBetasProcessor()
{
    *alive = false;
    stopTimer();
    worker.removeAllJobs (true, 30000);
}

bool AZBetasProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AZBetasProcessor::prepareToPlay (double sr, int)
{
    const bool changed = std::abs (sr - sampleRate) > 0.5;
    sampleRate = sr;
    gainSmoothed.reset (sr, 0.03);
    if (changed)
    {
        renderedBpm = -1.0; // force re-render at the new sample rate
        requestRender();
        renderPads();
    }
}

// ------------------------------------------------------------------ audio --
void AZBetasProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int n = buffer.getNumSamples();

    double ppq = 0.0; bool playing = false; bool havePpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) hostBpm = *bpm;
            if (auto q = pos->getPpqPosition()) { ppq = *q; havePpq = true; }
            playing = pos->getIsPlaying();
        }
    hostPlaying = playing;
    hostPpq = ppq;

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int idx = m.getNoteNumber() - firstPadNote;
        if (idx < 0 || idx >= numPads) continue;
        if (m.isNoteOn())       { pads[idx].active = true; pads[idx].pos = 0.0; }
        else if (m.isNoteOff()) { pads[idx].active = false; }
    }
    midi.clear();

    const bool sync = params.getRawParameterValue ("sync")->load() > 0.5f;
    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (params.getRawParameterValue ("volume")->load(), -48.0f));

    const juce::SpinLock::ScopedTryLockType lock (playLock);
    if (! lock.isLocked())
        return;

    auto* out0 = buffer.getWritePointer (0);
    auto* out1 = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    // returns false when a non-looping sound reached its end
    auto play = [&] (const RenderedLoop& L, double& pos, bool syncToHost, bool loops) -> bool
    {
        const int len = L.buffer.getNumSamples();
        if (len == 0) return false;
        if (syncToHost && L.beats > 0.0 && playing && havePpq)
        {
            double ph = std::fmod (ppq, L.beats);
            if (ph < 0) ph += L.beats;
            pos = ph / L.beats * len;
        }
        auto* s0 = L.buffer.getReadPointer (0);
        auto* s1 = L.buffer.getReadPointer (1);
        int p = (int) pos;
        for (int i = 0; i < n; ++i)
        {
            if (p >= len)
            {
                if (! loops) { pos = len; return false; }
                p -= len;
            }
            out0[i] += s0[p];
            if (out1 != nullptr) out1[i] += s1[p];
            ++p;
        }
        pos = p >= len && loops ? p - len : p;
        return true;
    };

    if (previewPlaying.load() && previewLoop != nullptr)
    {
        const bool loops = previewLoop->beats > 0.0 || previewLoops;
        if (! play (*previewLoop, previewPos, sync && ! previewIsModernize, loops))
            previewPlaying = false;
        const int len = previewLoop->buffer.getNumSamples();
        previewPosition = len > 0 ? (float) (previewPos / len) : 0.0f;
    }

    for (auto& pad : pads)
        if (pad.active.load() && pad.loop != nullptr)
            if (! play (*pad.loop, pad.pos, sync, true))
                pad.active = false;

    for (int i = 0; i < n; ++i)
    {
        const float g = gainSmoothed.getNextValue();
        out0[i] *= g;
        if (out1 != nullptr) out1[i] *= g;
    }
}

// --------------------------------------------------------------- helpers --
void AZBetasProcessor::setStatus (const juce::String& s)
{
    status = s;
    sendChangeMessage();
}

double AZBetasProcessor::targetBpmFor (double srcBpm) const
{
    const bool sync = params.getRawParameterValue ("sync")->load() > 0.5f;
    const double h = hostBpm.load();
    return (sync && h > 20.0 && srcBpm > 0.0) ? h : srcBpm;
}

double AZBetasProcessor::effectiveBpm() const
{
    return targetBpmFor (selected.bpm);
}

juce::String AZBetasProcessor::currentKeyName() const
{
    const int pitch = (int) params.getRawParameterValue ("pitch")->load();
    return LoopRender::transposeKey (selected.key, pitch);
}

std::shared_ptr<RenderedLoop> AZBetasProcessor::renderClip (const std::shared_ptr<AudioClip>& clip,
                                                            const juce::String& id, const juce::String& name,
                                                            const juce::String& key, double srcBpm, double beats) const
{
    auto r = std::make_shared<RenderedLoop>();
    const double target = targetBpmFor (srcBpm);
    const float pitch = params.getRawParameterValue ("pitch")->load();
    r->buffer = LoopRender::render (*clip, srcBpm, beats, target, sampleRate, pitch, srcBpm > 0.0);
    r->sampleRate = sampleRate;
    r->beats = srcBpm > 0.0 ? beats : 0.0;
    r->bpm = target;
    r->semitones = pitch;
    r->id = id;
    r->name = name;
    r->key = LoopRender::transposeKey (key, (int) pitch);
    return r;
}

void AZBetasProcessor::requestRender()
{
    if (previewIsModernize || selectedClip == nullptr)
        return;

    renderedBpm   = effectiveBpm();
    renderedPitch = params.getRawParameterValue ("pitch")->load();
    renderedSync  = params.getRawParameterValue ("sync")->load() > 0.5f;

    const int gen = ++renderGeneration;
    auto clip = selectedClip;
    auto item = selected;
    std::weak_ptr<bool> weak = alive;

    worker.addJob ([this, gen, clip, item, weak]
    {
        auto beats = LoopRender::beatsFor (clip->seconds(), item.bpm, item.bars);
        auto r = renderClip (clip, item.id, item.name, item.key, item.bpm, beats);
        if (gen != renderGeneration.load())
            return;

        std::shared_ptr<RenderedLoop> old;
        {
            const juce::SpinLock::ScopedLockType l (playLock);
            const double frac = (previewLoop != nullptr && previewLoop->buffer.getNumSamples() > 0)
                                    ? previewPos / previewLoop->buffer.getNumSamples() : 0.0;
            old = previewLoop;
            previewLoop = r;
            previewLoops = false;
            previewPos = frac * r->buffer.getNumSamples();
        }
        juce::MessageManager::callAsync ([this, weak] { if (auto a = weak.lock(); a && *a) sendChangeMessage(); });
    });
}

void AZBetasProcessor::renderPads()
{
    for (int i = 0; i < numPads; ++i)
    {
        auto& pad = pads[i];
        if (pad.clip == nullptr) continue;
        auto clip = pad.clip;
        auto id = pad.id, name = pad.name, key = pad.key;
        auto bpm = pad.srcBpm, beats = pad.beats;
        worker.addJob ([this, i, clip, id, name, key, bpm, beats]
        {
            auto r = renderClip (clip, id, name, key, bpm, beats);
            std::shared_ptr<RenderedLoop> old;
            const juce::SpinLock::ScopedLockType l (playLock);
            if (pads[i].id != id) return;
            old = pads[i].loop;
            pads[i].loop = r;
        });
    }
}

void AZBetasProcessor::timerCallback()
{
    license->setFreeLimit (catalog->getFreeLimit());

    const double bpm = effectiveBpm();
    const float pitch = params.getRawParameterValue ("pitch")->load();
    const bool sync = params.getRawParameterValue ("sync")->load() > 0.5f;
    if (std::abs (bpm - renderedBpm) > 0.01 || std::abs (pitch - renderedPitch) > 0.01f || sync != renderedSync)
    {
        requestRender();
        renderPads();
        renderedBpm = bpm; renderedPitch = pitch; renderedSync = sync;
        sendChangeMessage();
    }
}

// --------------------------------------------------------------- library --
void AZBetasProcessor::selectItem (const CatalogItem& item, bool autoplay)
{
    selected = item;
    selectedClip = nullptr;
    previewIsModernize = false;
    setStatus ("Yuklanmoqda: " + item.name);

    std::weak_ptr<bool> weak = alive;
    const auto id = item.id;
    catalog->ensureLocal (item, [this, weak, id, autoplay] (juce::File f)
    {
        auto a = weak.lock();
        if (! a || ! *a || selected.id != id) return;
        if (! f.existsAsFile())
        {
            setStatus ("Yuklab bo'lmadi (internetni tekshiring): " + selected.name);
            return;
        }
        auto clip = std::make_shared<AudioClip>();
        juce::String err;
        if (! LoopRender::loadFile (f, *clip, err))
        {
            f.deleteFile();
            setStatus (err);
            return;
        }
        selectedClip = clip;
        {
            const juce::SpinLock::ScopedLockType l (playLock);
            previewPos = 0.0;
        }
        requestRender();
        if (autoplay) previewPlaying = true;
        setStatus (selected.name + " - tayyor");
    });
}

void AZBetasProcessor::setPreviewPlaying (bool shouldPlay)
{
    if (shouldPlay && previewIsModernize && selectedClip != nullptr && modState.source == nullptr)
        previewIsModernize = false;
    if (shouldPlay)
    {
        const juce::SpinLock::ScopedLockType l (playLock);
        if (previewLoop != nullptr && previewPos >= previewLoop->buffer.getNumSamples())
            previewPos = 0.0;
    }
    previewPlaying = shouldPlay;
    sendChangeMessage();
}

std::vector<float> AZBetasProcessor::getPreviewPeaks (int points)
{
    std::shared_ptr<RenderedLoop> l;
    {
        const juce::SpinLock::ScopedLockType lk (playLock);
        l = previewLoop;
    }
    if (l == nullptr) return {};
    return LoopRender::peaks (l->buffer, points);
}

static juce::File dragDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AZ BETAS");
    d.createDirectory();
    return d;
}

juce::File AZBetasProcessor::exportSelectedForDrag (juce::String& error)
{
    if (selectedClip == nullptr || selected.id.isEmpty())
    {
        error = "Avval kutubxonadan loop tanlang";
        return {};
    }
    if (selected.isEarly && license->tier() != Tier::Pro)
    {
        error = "Bu yangilik birinchi 3 kun faqat PRO uchun. Tinglash bepul.";
        return {};
    }
    if (! license->registerLoopUse (selected.id, selected.isFree))
    {
        error = "Bepul limit tugadi (" + juce::String (license->getFreeLimit()) + " ta). "
                "Start yoki PRO obuna bilan cheksiz foydalaning.";
        return {};
    }

    std::shared_ptr<RenderedLoop> l;
    {
        const juce::SpinLock::ScopedLockType lk (playLock);
        l = previewLoop;
    }
    if (l == nullptr || l->id != selected.id || previewIsModernize)
    {
        auto beats = LoopRender::beatsFor (selectedClip->seconds(), selected.bpm, selected.bars);
        l = renderClip (selectedClip, selected.id, selected.name, selected.key, selected.bpm, beats);
    }

    juce::String name = selected.name;
    if (l->bpm > 0) name << " " << juce::String (juce::roundToInt (l->bpm)) << "bpm";
    if (l->key.isNotEmpty()) name << " " << l->key;
    auto f = dragDir().getChildFile (juce::File::createLegalFileName (name) + ".wav");
    if (! LoopRender::writeWav (l->buffer, l->sampleRate, f))
    {
        error = "WAV yozib bo'lmadi";
        return {};
    }
    return f;
}

// ------------------------------------------------------------------ pads --
AZBetasProcessor::PadInfo AZBetasProcessor::getPad (int i) const
{
    PadInfo info;
    if (i < 0 || i >= numPads) return info;
    const juce::SpinLock::ScopedLockType l (const_cast<juce::SpinLock&> (playLock));
    info.id = pads[i].id;
    info.name = pads[i].name;
    info.loaded = pads[i].loop != nullptr;
    info.active = pads[i].active.load();
    return info;
}

bool AZBetasProcessor::assignSelectedToPad (int i, juce::String& error)
{
    if (i < 0 || i >= numPads) return false;
    if (selectedClip == nullptr) { error = "Avval loop tanlang va yuklanishini kuting"; return false; }
    if (selected.isEarly && license->tier() != Tier::Pro) { error = "Bu yangilik birinchi 3 kun faqat PRO uchun"; return false; }
    if (! license->registerLoopUse (selected.id, selected.isFree))
    {
        error = "Bepul limit tugadi. Start yoki PRO obuna oling.";
        return false;
    }
    {
        const juce::SpinLock::ScopedLockType l (playLock);
        pads[i].active = false;
        pads[i].loop = nullptr;
        pads[i].clip = selectedClip;
        pads[i].id = selected.id;
        pads[i].name = selected.name;
        pads[i].key = selected.key;
        pads[i].srcBpm = selected.bpm;
        pads[i].beats = LoopRender::beatsFor (selectedClip->seconds(), selected.bpm, selected.bars);
    }
    renderPads();
    sendChangeMessage();
    return true;
}

void AZBetasProcessor::clearPad (int i)
{
    if (i < 0 || i >= numPads) return;
    std::shared_ptr<RenderedLoop> old;
    std::shared_ptr<AudioClip> oldClip;
    {
        const juce::SpinLock::ScopedLockType l (playLock);
        pads[i].active = false;
        old = pads[i].loop; oldClip = pads[i].clip;
        pads[i].loop = nullptr; pads[i].clip = nullptr;
        pads[i].id = {}; pads[i].name = {};
    }
    sendChangeMessage();
}

void AZBetasProcessor::togglePad (int i)
{
    if (i < 0 || i >= numPads || pads[i].loop == nullptr) return;
    pads[i].pos = 0.0;
    pads[i].active = ! pads[i].active.load();
    sendChangeMessage();
}

void AZBetasProcessor::loadPadFromId (int i, const juce::String& id)
{
    auto* item = catalog->find (id);
    if (item == nullptr) return;
    auto copy = *item;
    std::weak_ptr<bool> weak = alive;
    catalog->ensureLocal (copy, [this, weak, i, copy] (juce::File f)
    {
        auto a = weak.lock();
        if (! a || ! *a || ! f.existsAsFile()) return;
        auto clip = std::make_shared<AudioClip>();
        juce::String err;
        if (! LoopRender::loadFile (f, *clip, err)) return;
        {
            const juce::SpinLock::ScopedLockType l (playLock);
            pads[i].clip = clip; pads[i].id = copy.id; pads[i].name = copy.name; pads[i].key = copy.key;
            pads[i].srcBpm = copy.bpm;
            pads[i].beats = LoopRender::beatsFor (clip->seconds(), copy.bpm, copy.bars);
        }
        renderPads();
        sendChangeMessage();
    });
}

// ------------------------------------------------------------- modernize --
static void resetAnalysis (AZBetasProcessor::ModState& m)
{
    m.before = Modernize::analyse (m.source->buffer, m.source->sampleRate, m.settings.seamless);
    m.hasBefore = true;
    m.hasAfter = false;
    m.processed = nullptr;
    m.settings = Modernize::autoSettings (m.before, m.settings.style, m.settings.seamless);
}

bool AZBetasProcessor::modernizeLoadFile (const juce::File& f, juce::String& error)
{
    auto clip = std::make_shared<AudioClip>();
    if (! LoopRender::loadFile (f, *clip, error))
        return false;
    modState.source = clip;
    modState.sourceId = "file:" + f.getFileName() + ":" + juce::String (f.getSize());
    modState.sourceName = f.getFileNameWithoutExtension();
    modState.sourceKey = {};
    modState.sourceBpm = 0.0;
    modState.settings.seamless = clip->seconds() <= 60.0;
    resetAnalysis (modState);
    setStatus ("AZ Modernize: " + modState.sourceName + " tahlil qilindi");
    return true;
}

bool AZBetasProcessor::modernizeTakeSelected (juce::String& error)
{
    if (selectedClip == nullptr) { error = "Avval kutubxonadan loop tanlang"; return false; }
    modState.source = selectedClip;
    modState.sourceId = "lib:" + selected.id;
    modState.sourceName = selected.name;
    modState.sourceKey = selected.key;
    modState.sourceBpm = selected.bpm;
    modState.settings.seamless = selected.isLoop();
    resetAnalysis (modState);
    setStatus ("AZ Modernize: " + modState.sourceName + " tahlil qilindi");
    return true;
}

void AZBetasProcessor::modernizeAuto (int style)
{
    if (! modState.hasBefore) { modState.settings.style = style; return; }
    modState.settings = Modernize::autoSettings (modState.before, style, modState.settings.seamless);
    sendChangeMessage();
}

void AZBetasProcessor::modernizeRun()
{
    if (modState.source == nullptr || modBusy.load()) return;
    if (license->tier() == Tier::Free)
    {
        setStatus ("AZ Modernize ishlovi Start va PRO obunada ochiladi (Bepul: faqat tashxis)");
        return;
    }
    modBusy = true;
    setStatus ("AZ Modernize ishlamoqda...");
    auto src = modState.source;
    auto settings = modState.settings;
    auto key = modState.sourceKey;
    std::weak_ptr<bool> weak = alive;
    worker.addJob ([this, src, settings, key, weak]
    {
        auto out = std::make_shared<AudioClip>();
        out->buffer = Modernize::process (*src, settings, key);
        out->sampleRate = src->sampleRate;
        out->name = src->name;
        auto after = Modernize::analyse (out->buffer, out->sampleRate, settings.seamless);
        juce::MessageManager::callAsync ([this, weak, out, after, src]
        {
            auto a = weak.lock();
            if (! a || ! *a) return;
            modBusy = false;
            if (modState.source != src) return;
            modState.processed = out;
            modState.after = after;
            modState.hasAfter = true;
            setStatus ("Tayyor: " + juce::String (after.lufs, 1) + " LUFS, " + juce::String (after.truePeakDb, 1) + " dBTP");
            modernizePreview (true);
        });
    });
}

void AZBetasProcessor::modernizePreview (bool processed)
{
    auto clip = processed && modState.processed != nullptr ? modState.processed : modState.source;
    if (clip == nullptr) return;
    modPreviewProcessed = processed && modState.processed != nullptr;
    previewIsModernize = true;
    ++renderGeneration;
    const double sr = sampleRate;
    const auto name = modState.sourceName;
    std::weak_ptr<bool> weak = alive;
    worker.addJob ([this, clip, sr, name, weak]
    {
        auto r = std::make_shared<RenderedLoop>();
        r->buffer = LoopRender::render (*clip, 0.0, 0.0, 0.0, sr, 0.0f, false);
        r->sampleRate = sr;
        r->name = name;
        std::shared_ptr<RenderedLoop> old;
        {
            const juce::SpinLock::ScopedLockType l (playLock);
            const double frac = (previewLoop != nullptr && previewLoop->buffer.getNumSamples() > 0)
                                    ? previewPos / previewLoop->buffer.getNumSamples() : 0.0;
            old = previewLoop;
            previewLoop = r;
            previewLoops = true;
            previewPos = juce::jlimit (0.0, 1.0, frac) * r->buffer.getNumSamples();
        }
        previewPlaying = true;
        juce::MessageManager::callAsync ([this, weak] { if (auto a = weak.lock(); a && *a) sendChangeMessage(); });
    });
}

juce::File AZBetasProcessor::modernizeExport (const juce::File& target, juce::String& error)
{
    if (modState.processed == nullptr) { error = "Avval \"Qo'llash\" tugmasini bosing"; return {}; }
    if (! license->registerModernizeExport (modState.sourceId))
    {
        error = license->tier() == Tier::Free ? "Bepul tarifda faqat tashxis. Start yoki PRO oling."
                                              : "Oylik limit tugadi (Start: 5 ta). PRO bilan cheksiz.";
        return {};
    }
    auto f = target != juce::File() ? target
                                    : dragDir().getChildFile (juce::File::createLegalFileName (modState.sourceName + " (AZ Modernize)") + ".wav");
    if (! LoopRender::writeWav (modState.processed->buffer, modState.processed->sampleRate, f))
    {
        error = "WAV yozib bo'lmadi";
        return {};
    }
    return f;
}

// ----------------------------------------------------------------- state --
void AZBetasProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = params.copyState();
    juce::ValueTree az ("AZB");
    az.setProperty ("selected", selected.id, nullptr);
    for (int i = 0; i < numPads; ++i)
        az.setProperty ("pad" + juce::String (i), pads[i].id, nullptr);
    az.appendChild (modState.settings.toValueTree(), nullptr);
    state.removeChild (state.getChildWithName ("AZB"), nullptr);
    state.appendChild (az, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AZBetasProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml (*xml);
    auto az = state.getChildWithName ("AZB");
    if (state.hasType (params.state.getType()))
        params.replaceState (state);
    if (! az.isValid()) return;

    modState.settings.fromValueTree (az.getChildWithName ("MODERNIZE"));
    const juce::String sel = az.getProperty ("selected");
    juce::StringArray padIds;
    for (int i = 0; i < numPads; ++i)
        padIds.add (az.getProperty ("pad" + juce::String (i)).toString());

    std::weak_ptr<bool> weak = alive;
    // the catalog may still be loading: retry a few times
    auto attempt = std::make_shared<std::function<void (int)>>();
    *attempt = [this, weak, sel, padIds, attempt] (int tries)
    {
        auto a = weak.lock();
        if (! a || ! *a) return;
        bool missing = false;
        if (sel.isNotEmpty() && selected.id != sel)
        {
            if (auto* it = catalog->find (sel)) selectItem (*it, false);
            else missing = true;
        }
        for (int i = 0; i < numPads; ++i)
            if (padIds[i].isNotEmpty() && pads[i].id != padIds[i])
            {
                if (catalog->find (padIds[i]) != nullptr) loadPadFromId (i, padIds[i]);
                else missing = true;
            }
        if (missing && tries > 0)
            juce::Timer::callAfterDelay (1500, [attempt, tries] { (*attempt) (tries - 1); });
    };
    juce::MessageManager::callAsync ([attempt] { (*attempt) (20); });
}

juce::AudioProcessorEditor* AZBetasProcessor::createEditor()
{
    return new AZBetasEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AZBetasProcessor();
}
