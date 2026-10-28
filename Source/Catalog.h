#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <set>

// One loop / sample in the online library.
struct CatalogItem
{
    juce::String id, name, file, category, genre, key, type, added;
    juce::StringArray tags;
    double bpm = 0.0;      // 0 = one-shot / no tempo
    int bars = 0;          // 0 = unknown
    bool isFree = false;   // admin marked "bepul"
    bool isDemo = false;   // admin marked "namuna" (showcase)
    bool isNew = false;    // added within the last 14 days
    bool isEarly = false;  // added within the last 3 days: PRO early access
    juce::int64 size = 0;

    bool isLoop() const    { return bpm > 0.0; }
};

// Downloads catalog.json and the audio files, keeps an offline cache.
class LibraryCatalog : public juce::ChangeBroadcaster
{
public:
    static constexpr const char* defaultUrl =
        "https://raw.githubusercontent.com/alifrecords/AZ-Beats/main/library/catalog.json";

    LibraryCatalog();
    ~LibraryCatalog() override;

    juce::String getUrl() const;
    void setUrl (const juce::String& url);

    void refresh();                         // async; broadcasts change when done
    bool isLoading() const                  { return loading.load(); }
    juce::String getStatus() const;

    juce::Array<CatalogItem> getItems() const;
    const CatalogItem* find (const juce::String& id) const; // pointer valid until next refresh (message thread)
    juce::StringArray getCategories() const;
    juce::StringArray getGenres() const;
    int getFreeLimit() const                { return freeLimit; }

    // Local cache for audio files
    static juce::File getCacheDir();
    juce::File localFileFor (const CatalogItem& item) const;
    bool isCached (const CatalogItem& item) const;

    // Download (if needed) and call back on the message thread with the file
    // (or File() on failure).
    void ensureLocal (const CatalogItem& item, std::function<void (juce::File)> done);

    bool parse (const juce::String& jsonText); // public for tests

private:
    void loadFromCache();
    juce::URL resolve (const juce::String& relative) const;

    mutable juce::CriticalSection lock;
    juce::Array<CatalogItem> items;
    juce::String status;
    int freeLimit = 10;
    std::atomic<bool> loading { false };
    juce::ThreadPool pool { 2 };
    std::set<juce::String> inFlight;
};
