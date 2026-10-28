#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

// ---------------------------------------------------------------------------
// AZ BETAS licence / subscription model
//
//  Bepul (Free) : listen to everything, use up to N loops in the DAW
//                 (N comes from the catalog, default 10), admin-marked free
//                 loops are unlimited, AZ Modernize = diagnosis only.
//  Start        : whole library, AZ Modernize 5 files / month.
//  PRO          : everything unlimited.
//
// A licence key is issued by the admin (admin/admin.html) after payment:
//    AZB1.<base64url(payload json)>.<hex signature>
// payload: {"u":"<user>","t":"start|pro","e":"YYYY-MM-DD","i":"YYYY-MM-DD"}
// signature = SHA256(payload)^d mod n  (RSA, verified here with the public key)
// ---------------------------------------------------------------------------

enum class Tier { Free = 0, Start = 1, Pro = 2 };

juce::String tierName (Tier t);

struct LicenseInfo
{
    bool valid = false;
    Tier tier = Tier::Free;
    juce::String user;
    juce::String expires;   // YYYY-MM-DD
    juce::String error;     // human readable (Uzbek) if invalid
};

class LicenseManager
{
public:
    LicenseManager();

    // Parse + verify a key string. Does not store it.
    LicenseInfo verify (const juce::String& key) const;

    // Verify and, if valid, store. Returns the result.
    LicenseInfo activate (const juce::String& key);
    void deactivate();

    LicenseInfo current() const;           // re-checks expiry every call
    Tier tier() const                      { return current().tier; }

    // --- free tier loop allowance --------------------------------------
    void setFreeLimit (int n)              { freeLimit = juce::jmax (0, n); }
    int  getFreeLimit() const              { return freeLimit; }
    int  usedFreeCount() const;
    bool hasUsed (const juce::String& loopId) const;
    // Can this loop be placed in the DAW (drag / export / pad)?
    bool canUseLoop (const juce::String& loopId, bool loopIsFree) const;
    // Call when a loop is actually used. Returns false if not allowed.
    bool registerLoopUse (const juce::String& loopId, bool loopIsFree);

    // --- AZ Modernize allowance ----------------------------------------
    int  modernizeQuota() const;           // -1 = unlimited, 0 = none
    int  modernizeUsedThisMonth() const;
    bool canModernizeExport (const juce::String& sourceId) const;
    bool registerModernizeExport (const juce::String& sourceId);

    // settings shared by the plug-in
    juce::PropertiesFile& settings()       { return *props; }
    bool hasPublicKey() const              { return publicKeyN.isNotEmpty(); }

    static juce::String today();

private:
    juce::StringArray getList (const juce::String& key) const;
    void setList (const juce::String& key, const juce::StringArray& list);
    juce::String monthKey() const;

    std::unique_ptr<juce::PropertiesFile> props;
    juce::String publicKeyE, publicKeyN;   // hex
    int freeLimit = 10;

    juce::CriticalSection cacheLock;
    mutable juce::String cachedKey, cachedDay;
    mutable LicenseInfo cachedInfo;
};
