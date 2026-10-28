#include "License.h"
#include <juce_cryptography/juce_cryptography.h>
#include "BinaryData.h"

juce::String tierName (Tier t)
{
    switch (t)
    {
        case Tier::Pro:   return "PRO";
        case Tier::Start: return "Start";
        case Tier::Free:
        default:          return "Bepul";
    }
}

static juce::PropertiesFile::Options makeOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "AZ BETAS";
    o.filenameSuffix      = ".settings";
    o.folderName          = "Azimxodjayev Music/AZ BETAS";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat       = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = 300;
    o.processLock = nullptr;
    return o;
}

LicenseManager::LicenseManager()
{
    props = std::make_unique<juce::PropertiesFile> (makeOptions());

    // public key: "<e hex>,<n hex>" (or "NONE" before the admin created one)
    auto pk = juce::String::fromUTF8 (BinaryData::license_public_key_txt,
                                      BinaryData::license_public_key_txtSize).trim();
    if (pk.containsChar (','))
    {
        publicKeyE = pk.upToFirstOccurrenceOf (",", false, false).trim();
        publicKeyN = pk.fromFirstOccurrenceOf (",", false, false).trim();
    }
}

juce::String LicenseManager::today()
{
    return juce::Time::getCurrentTime().formatted ("%Y-%m-%d");
}

static bool base64UrlDecode (juce::String s, juce::MemoryBlock& out)
{
    s = s.replaceCharacter ('-', '+').replaceCharacter ('_', '/');
    while (s.length() % 4 != 0)
        s << "=";
    juce::MemoryOutputStream mo (out, false);
    return juce::Base64::convertFromBase64 (mo, s);
}

LicenseInfo LicenseManager::verify (const juce::String& keyIn) const
{
    LicenseInfo info;
    auto key = keyIn.removeCharacters (" \t\r\n");

    if (key.isEmpty())                  { info.error = "Kalit kiritilmagan"; return info; }
    if (! key.startsWith ("AZB1."))     { info.error = "Kalit formati noto'g'ri"; return info; }
    if (! hasPublicKey())               { info.error = "Plaginda ochiq kalit yo'q (admin hali sozlamagan)"; return info; }

    auto rest    = key.substring (5);
    auto payload = rest.upToFirstOccurrenceOf (".", false, false);
    auto sigHex  = rest.fromFirstOccurrenceOf (".", false, false);

    juce::MemoryBlock payloadBytes;
    if (payload.isEmpty() || sigHex.isEmpty() || ! base64UrlDecode (payload, payloadBytes))
    {
        info.error = "Kalit buzilgan";
        return info;
    }

    // signature check: sig^e mod n == SHA256(payload) (little-endian integer)
    juce::SHA256 sha (payloadBytes.getData(), payloadBytes.getSize());
    juce::BigInteger expected;
    expected.loadFromMemoryBlock (sha.getRawData());

    juce::BigInteger sig, e, n;
    sig.parseString (sigHex, 16);
    e.parseString (publicKeyE, 16);
    n.parseString (publicKeyN, 16);

    if (sig.isZero() || n.isZero() || sig >= n)
    {
        info.error = "Imzo noto'g'ri";
        return info;
    }

    sig.exponentModulo (e, n);
    if (sig != expected)
    {
        info.error = "Imzo tasdiqlanmadi - kalit haqiqiy emas";
        return info;
    }

    auto json = juce::JSON::parse (payloadBytes.toString());
    if (! json.isObject())
    {
        info.error = "Kalit ma'lumoti o'qilmadi";
        return info;
    }

    info.user    = json["u"].toString();
    info.expires = json["e"].toString();
    auto t       = json["t"].toString().toLowerCase();
    info.tier    = t == "pro" ? Tier::Pro : (t == "start" ? Tier::Start : Tier::Free);

    if (info.expires.isEmpty() || info.expires < today())
    {
        info.error = "Obuna muddati tugagan (" + info.expires + ")";
        info.tier  = Tier::Free;
        return info;
    }

    info.valid = true;
    return info;
}

LicenseInfo LicenseManager::activate (const juce::String& key)
{
    auto info = verify (key);
    if (info.valid)
    {
        props->setValue ("licenseKey", key.removeCharacters (" \t\r\n"));
        props->saveIfNeeded();
    }
    return info;
}

void LicenseManager::deactivate()
{
    props->removeValue ("licenseKey");
    props->saveIfNeeded();
}

LicenseInfo LicenseManager::current() const
{
    auto key = props->getValue ("licenseKey");
    if (key.isEmpty())
        return {};
    const juce::ScopedLock sl (cacheLock);
    auto day = today();
    if (key != cachedKey || day != cachedDay)
    {
        cachedInfo = verify (key);
        cachedKey = key;
        cachedDay = day;
    }
    return cachedInfo;
}

juce::StringArray LicenseManager::getList (const juce::String& key) const
{
    juce::StringArray a;
    a.addTokens (props->getValue (key), "|", "");
    a.removeEmptyStrings();
    return a;
}

void LicenseManager::setList (const juce::String& key, const juce::StringArray& list)
{
    props->setValue (key, list.joinIntoString ("|"));
    props->saveIfNeeded();
}

int LicenseManager::usedFreeCount() const          { return getList ("usedLoops").size(); }
bool LicenseManager::hasUsed (const juce::String& id) const { return getList ("usedLoops").contains (id); }

bool LicenseManager::canUseLoop (const juce::String& id, bool loopIsFree) const
{
    if (tier() != Tier::Free || loopIsFree)
        return true;
    auto used = getList ("usedLoops");
    return used.contains (id) || used.size() < freeLimit;
}

bool LicenseManager::registerLoopUse (const juce::String& id, bool loopIsFree)
{
    if (! canUseLoop (id, loopIsFree))
        return false;

    if (tier() == Tier::Free && ! loopIsFree)
    {
        auto used = getList ("usedLoops");
        if (! used.contains (id))
        {
            used.add (id);
            setList ("usedLoops", used);
        }
    }
    return true;
}

juce::String LicenseManager::monthKey() const
{
    return "modernize_" + juce::Time::getCurrentTime().formatted ("%Y-%m");
}

int LicenseManager::modernizeQuota() const
{
    switch (tier())
    {
        case Tier::Pro:   return -1;
        case Tier::Start: return 5;
        case Tier::Free:
        default:          return 0;
    }
}

int LicenseManager::modernizeUsedThisMonth() const { return getList (monthKey()).size(); }

bool LicenseManager::canModernizeExport (const juce::String& sourceId) const
{
    auto q = modernizeQuota();
    if (q < 0) return true;
    if (q == 0) return false;
    auto used = getList (monthKey());
    return used.contains (sourceId) || used.size() < q;
}

bool LicenseManager::registerModernizeExport (const juce::String& sourceId)
{
    if (! canModernizeExport (sourceId))
        return false;
    if (modernizeQuota() > 0)
    {
        auto used = getList (monthKey());
        if (! used.contains (sourceId))
        {
            used.add (sourceId);
            setList (monthKey(), used);
        }
    }
    return true;
}
