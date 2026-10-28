#include "Catalog.h"
#include <set>
#include <juce_data_structures/juce_data_structures.h>

static juce::File settingsDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Azimxodjayev Music").getChildFile ("AZ BETAS");
}

juce::File LibraryCatalog::getCacheDir()
{
    auto d = settingsDir().getChildFile ("Library");
    d.createDirectory();
    return d;
}

LibraryCatalog::LibraryCatalog()
{
    loadFromCache();
}

LibraryCatalog::~LibraryCatalog()
{
    pool.removeAllJobs (true, 20000);
}

juce::String LibraryCatalog::getUrl() const
{
    auto f = settingsDir().getChildFile ("catalog_url.txt");
    auto u = f.existsAsFile() ? f.loadFileAsString().trim() : juce::String();
    return u.isNotEmpty() ? u : juce::String (defaultUrl);
}

void LibraryCatalog::setUrl (const juce::String& url)
{
    settingsDir().createDirectory();
    settingsDir().getChildFile ("catalog_url.txt").replaceWithText (url.trim());
}

juce::String LibraryCatalog::getStatus() const
{
    const juce::ScopedLock sl (lock);
    return status;
}

juce::Array<CatalogItem> LibraryCatalog::getItems() const
{
    const juce::ScopedLock sl (lock);
    return items;
}

const CatalogItem* LibraryCatalog::find (const juce::String& id) const
{
    const juce::ScopedLock sl (lock);
    for (auto& it : items)
        if (it.id == id)
            return &it;
    return nullptr;
}

juce::StringArray LibraryCatalog::getCategories() const
{
    const juce::ScopedLock sl (lock);
    juce::StringArray c;
    for (auto& it : items)
        if (it.category.isNotEmpty())
            c.addIfNotAlreadyThere (it.category);
    return c;
}

juce::Array<CatalogPack> LibraryCatalog::getPacks() const
{
    const juce::ScopedLock sl (lock);
    return packs;
}

juce::StringArray LibraryCatalog::getGenres() const
{
    const juce::ScopedLock sl (lock);
    juce::StringArray c;
    for (auto& it : items)
        if (it.genre.isNotEmpty())
            c.addIfNotAlreadyThere (it.genre);
    c.sort (true);
    return c;
}

static bool isRecent (const juce::String& ymd, double days = 14.0)
{
    if (ymd.length() < 10) return false;
    juce::Time t (ymd.substring (0, 4).getIntValue(), ymd.substring (5, 7).getIntValue() - 1,
                  ymd.substring (8, 10).getIntValue(), 0, 0);
    return (juce::Time::getCurrentTime() - t).inDays() <= days;
}

bool LibraryCatalog::parse (const juce::String& jsonText)
{
    auto json = juce::JSON::parse (jsonText);
    auto* arr = json["items"].getArray();
    if (arr == nullptr)
        return false;

    juce::Array<CatalogItem> parsed;
    for (auto& v : *arr)
    {
        CatalogItem it;
        it.id       = v["id"].toString();
        it.name     = v["name"].toString();
        it.file     = v["file"].toString();
        it.category = v["category"].toString();
        it.genre    = v["genre"].toString();
        it.key      = v["key"].toString();
        it.type     = v["type"].toString();
        it.added    = v["added"].toString();
        it.bpm      = (double) v["bpm"];
        it.bars     = (int) v["bars"];
        it.isFree   = (bool) v["free"];
        it.isDemo   = (bool) v["demo"];
        it.size     = (juce::int64) v["size"];
        it.pack     = v["pack"].toString();
        it.order    = (int) v["order"];
        it.isNew    = isRecent (it.added);
        it.isEarly  = ! it.isFree && isRecent (it.added, 3.0);
        if (auto* tags = v["tags"].getArray())
            for (auto& t : *tags)
                it.tags.add (t.toString());
        if (it.id.isNotEmpty() && it.file.isNotEmpty())
            parsed.add (it);
    }

    juce::Array<CatalogPack> parsedPacks;
    if (auto* parr = json["packs"].getArray())
        for (auto& v : *parr)
        {
            CatalogPack pk;
            pk.id = v["id"].toString();
            pk.name = v["name"].toString();
            pk.description = v["description"].toString();
            pk.genre = v["genre"].toString();
            pk.cover = v["cover"].toString();
            pk.isFree = (bool) v["free"];
            pk.count = (int) v["count"];
            if (pk.id.isNotEmpty()) parsedPacks.add (pk);
        }

    // catalog v2 is already in the admin's order; older catalogs: newest first
    bool hasOrder = ! parsed.isEmpty();
    for (auto& it : parsed) hasOrder = hasOrder && it.order > 0;
    std::stable_sort (parsed.begin(), parsed.end(), [hasOrder] (const CatalogItem& a, const CatalogItem& b)
    {
        if (hasOrder) return a.order < b.order;
        if (a.added != b.added) return a.added > b.added;
        return a.name < b.name;
    });

    const juce::ScopedLock sl (lock);
    items = parsed;
    packs = parsedPacks;
    freeLimit = json.hasProperty ("freeLimit") ? (int) json["freeLimit"] : 10;
    return true;
}

void LibraryCatalog::loadFromCache()
{
    auto f = settingsDir().getChildFile ("catalog_cache.json");
    if (f.existsAsFile() && parse (f.loadFileAsString()))
    {
        const juce::ScopedLock sl (lock);
        status = "Oflayn kesh: " + juce::String (items.size()) + " ta";
    }
}

juce::URL LibraryCatalog::resolve (const juce::String& relative) const
{
    if (relative.startsWith ("http://") || relative.startsWith ("https://"))
        return juce::URL (relative);
    auto base = getUrl().upToLastOccurrenceOf ("/", true, false);
    return juce::URL (base + juce::URL::addEscapeChars (relative, false).replace ("%2F", "/"));
}

void LibraryCatalog::refresh()
{
    if (loading.exchange (true))
        return;

    {
        const juce::ScopedLock sl (lock);
        status = "Yuklanmoqda...";
    }
    sendChangeMessage();

    auto url = getUrl();
    pool.addJob ([this, url]
    {
        // cache-buster so a freshly committed catalog shows immediately
        juce::URL u (url + (url.containsChar ('?') ? "&" : "?") + "t="
                     + juce::String (juce::Time::currentTimeMillis() / 60000));
        int code = 0;
        auto stream = u.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                               .withConnectionTimeoutMs (15000)
                                               .withStatusCode (&code));
        juce::String text;
        if (stream != nullptr)
            text = stream->readEntireStreamAsString();

        bool ok = (code == 0 || code == 200) && text.isNotEmpty() && parse (text);
        if (ok)
        {
            settingsDir().createDirectory();
            settingsDir().getChildFile ("catalog_cache.json").replaceWithText (text);
        }

        {
            const juce::ScopedLock sl (lock);
            if (ok)
                status = "Onlayn: " + juce::String (items.size()) + " ta loop va sample";
            else
                status = items.isEmpty() ? "Internet yo'q yoki kutubxona topilmadi"
                                         : "Oflayn rejim: " + juce::String (items.size()) + " ta (keshdan)";
        }
        loading = false;
        juce::MessageManager::callAsync ([this] { sendChangeMessage(); });
    });
}

juce::File LibraryCatalog::localFileFor (const CatalogItem& item) const
{
    auto ext = item.file.fromLastOccurrenceOf (".", true, false);
    if (ext.isEmpty()) ext = ".wav";
    return getCacheDir().getChildFile (juce::File::createLegalFileName (item.id) + ext);
}

bool LibraryCatalog::isCached (const CatalogItem& item) const
{
    auto f = localFileFor (item);
    return f.existsAsFile() && (item.size <= 0 || f.getSize() == item.size);
}

void LibraryCatalog::ensureLocal (const CatalogItem& item, std::function<void (juce::File)> done)
{
    if (isCached (item))
    {
        done (localFileFor (item));
        return;
    }
    download (resolve (item.file), localFileFor (item), std::move (done));
}

void LibraryCatalog::ensureFile (const juce::String& relativePath, std::function<void (juce::File)> done)
{
    if (relativePath.isEmpty()) { done ({}); return; }
    auto target = getCacheDir().getChildFile ("files")
                    .getChildFile (juce::File::createLegalFileName (relativePath.replaceCharacter ('/', '_')));
    target.getParentDirectory().createDirectory();
    if (target.existsAsFile() && target.getSize() > 0) { done (target); return; }
    download (resolve (relativePath), target, std::move (done));
}

void LibraryCatalog::download (const juce::URL& url, const juce::File& target, std::function<void (juce::File)> done)
{
    pool.addJob ([url, target, done]
    {
        auto tmp = target.getSiblingFile (target.getFileName() + ".part");
        bool ok = false;
        int code = 0;
        if (auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                     .withConnectionTimeoutMs (20000)
                                                     .withStatusCode (&code)))
        {
            if (code == 0 || code == 200)
            {
                tmp.deleteFile();
                juce::FileOutputStream out (tmp);
                if (out.openedOk())
                {
                    out.writeFromInputStream (*stream, -1);
                    out.flush();
                    ok = out.getStatus().wasOk() && tmp.getSize() > 0;
                }
            }
        }
        if (ok)
            ok = tmp.moveFileTo (target);
        else
            tmp.deleteFile();

        auto result = ok ? target : juce::File();
        juce::MessageManager::callAsync ([done, result] { done (result); });
    });
}
