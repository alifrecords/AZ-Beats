#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "License.h"
#include "Catalog.h"
#include "LoopRender.h"
#include "Modernize.h"

class AZBetasProcessor : public juce::AudioProcessor,
                         public juce::ChangeBroadcaster,
                         private juce::Timer
{
public:
    static constexpr int numPads = 8;
    static constexpr int firstPadNote = 36;   // C1

    AZBetasProcessor();
    ~AZBetasProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }
    const juce::String getName() const override              { return "AZ BETAS"; }
    bool acceptsMidi() const override                        { return true; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.0; }
    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- shared services
    juce::SharedResourcePointer<LicenseManager> license;
    juce::SharedResourcePointer<LibraryCatalog> catalog;
    juce::AudioProcessorValueTreeState params;

    // ---- library preview (message thread)
    void selectItem (const CatalogItem& item, bool autoplay);
    juce::String getSelectedId() const                       { return selected.id; }
    const CatalogItem& getSelected() const                   { return selected; }
    bool isSelectedLoaded() const                            { return selectedClip != nullptr; }
    void setPreviewPlaying (bool shouldPlay);
    bool isPreviewPlaying() const                            { return previewPlaying.load(); }
    float getPreviewPosition() const                         { return previewPosition.load(); }
    std::vector<float> getPreviewPeaks (int points);
    juce::String getStatus() const                           { return status; }
    juce::String currentKeyName() const;                     // after pitch shift
    double effectiveBpm() const;                             // tempo the preview plays at

    // Render the selected loop (at the current tempo / pitch) into a WAV for
    // dragging into the DAW. Applies licence rules. Empty file = refused (see error).
    juce::File exportSelectedForDrag (juce::String& error);

    // ---- pads
    struct PadInfo { juce::String id, name; bool loaded = false, active = false; };
    PadInfo getPad (int i) const;
    bool assignSelectedToPad (int i, juce::String& error);
    void clearPad (int i);
    void togglePad (int i);

    // ---- AZ Modernize (message thread)
    struct ModState
    {
        std::shared_ptr<AudioClip> source, processed;
        juce::String sourceId, sourceName, sourceKey;
        double sourceBpm = 0.0;
        Modernize::Analysis before, after;
        bool hasBefore = false, hasAfter = false;
        Modernize::Settings settings;
    };
    const ModState& mod() const                              { return modState; }
    Modernize::Settings& modSettings()                       { return modState.settings; }
    bool modernizeLoadFile (const juce::File& f, juce::String& error);
    bool modernizeTakeSelected (juce::String& error);
    void modernizeAuto (int style);
    void modernizeRun();                                     // async
    bool isModernizeBusy() const                             { return modBusy.load(); }
    void modernizePreview (bool processed);                  // plays through the preview player
    bool isModernizePreviewProcessed() const                 { return modPreviewProcessed; }
    juce::File modernizeExport (const juce::File& target, juce::String& error); // target empty -> temp file

    double getHostBpm() const                                { return hostBpm.load(); }
    bool isHostPlaying() const                               { return hostPlaying.load(); }

private:
    void timerCallback() override;
    void requestRender();
    void renderPads();
    void setStatus (const juce::String& s);
    double targetBpmFor (double srcBpm) const;
    std::shared_ptr<RenderedLoop> renderClip (const std::shared_ptr<AudioClip>& clip, const juce::String& id,
                                              const juce::String& name, const juce::String& key,
                                              double srcBpm, double beats) const;
    void loadPadFromId (int i, const juce::String& id);

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // selected library item
    CatalogItem selected;
    std::shared_ptr<AudioClip> selectedClip;
    juce::String status;

    // what the audio thread plays
    juce::SpinLock playLock;
    std::shared_ptr<RenderedLoop> previewLoop;
    bool previewLoops = true;            // modernize preview of one-shots still loops
    double previewPos = 0.0;
    std::atomic<bool> previewPlaying { false };
    std::atomic<float> previewPosition { 0.0f };
    bool previewIsModernize = false;

    struct Pad
    {
        std::shared_ptr<AudioClip> clip;
        std::shared_ptr<RenderedLoop> loop;
        juce::String id, name, key;
        double srcBpm = 0.0, beats = 0.0;
        std::atomic<bool> active { false };
        double pos = 0.0;
    };
    Pad pads[numPads];

    // modernize
    ModState modState;
    std::atomic<bool> modBusy { false };
    bool modPreviewProcessed = false;

    // host info
    std::atomic<double> hostBpm { 0.0 }, hostPpq { 0.0 };
    std::atomic<bool> hostPlaying { false };
    double sampleRate = 44100.0;
    double renderedBpm = -1.0, renderedPitch = 1000.0;
    bool renderedSync = true;
    std::atomic<int> renderGeneration { 0 };
    juce::LinearSmoothedValue<float> gainSmoothed;

    juce::ThreadPool worker { 1 };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AZBetasProcessor)
};
