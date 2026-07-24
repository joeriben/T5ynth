#include "UpdateChecker.h"

namespace
{
// The LIST endpoint, not /releases/latest: every release is a GitHub
// pre-release, and /releases/latest excludes pre-releases (returns 404 here),
// so it would never surface an update. /releases returns all non-draft releases
// newest-first and DOES include pre-releases.
constexpr const char* kReleasesApiUrl = "https://api.github.com/repos/joeriben/akroasys/releases?per_page=30";

// A parsed semantic version: {major, minor, patch} plus the raw pre-release
// identifiers after '-' ("beta", "0"). Empty prerelease == a final release,
// which by semver ranks ABOVE any pre-release of the same triplet.
struct SemVer
{
    int major = 0, minor = 0, patch = 0;
    juce::StringArray prerelease;   // e.g. {"beta", "1"}; empty for a final release
};

SemVer parseSemVer(juce::String s)
{
    s = s.trim();
    if (s.startsWithIgnoreCase("v"))
        s = s.substring(1);
    s = s.upToFirstOccurrenceOf("+", false, false);   // drop build metadata

    SemVer v;
    const juce::String core = s.upToFirstOccurrenceOf("-", false, false);
    const juce::String pre  = s.fromFirstOccurrenceOf("-", false, false);

    auto nums = juce::StringArray::fromTokens(core, ".", "");
    if (nums.size() > 0) v.major = nums[0].getIntValue();
    if (nums.size() > 1) v.minor = nums[1].getIntValue();
    if (nums.size() > 2) v.patch = nums[2].getIntValue();

    if (pre.isNotEmpty())
        v.prerelease = juce::StringArray::fromTokens(pre, ".", "");
    return v;
}

// Returns <0, 0, >0 like strcmp — full semver precedence including pre-release
// ordering (SemVer §11): numeric identifiers compare numerically, a longer set
// of pre-release fields wins ties, and a final release outranks any pre-release.
int compareSemVer(const SemVer& a, const SemVer& b)
{
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;

    const bool aPre = ! a.prerelease.isEmpty();
    const bool bPre = ! b.prerelease.isEmpty();
    if (! aPre && ! bPre) return 0;
    if (! aPre) return 1;    // a is final, b is pre-release → a is newer
    if (! bPre) return -1;

    const int n = juce::jmin(a.prerelease.size(), b.prerelease.size());
    for (int i = 0; i < n; ++i)
    {
        const juce::String& ai = a.prerelease[i];
        const juce::String& bi = b.prerelease[i];
        const bool aNum = ai.containsOnly("0123456789") && ai.isNotEmpty();
        const bool bNum = bi.containsOnly("0123456789") && bi.isNotEmpty();

        if (aNum && bNum)
        {
            const int an = ai.getIntValue(), bn = bi.getIntValue();
            if (an != bn) return an < bn ? -1 : 1;
        }
        else if (aNum != bNum)
        {
            return aNum ? -1 : 1;   // numeric identifiers rank below alphanumeric
        }
        else if (ai != bi)
        {
            return ai < bi ? -1 : 1;
        }
    }
    if (a.prerelease.size() != b.prerelease.size())
        return a.prerelease.size() < b.prerelease.size() ? -1 : 1;
    return 0;
}
} // namespace

UpdateChecker::UpdateChecker(juce::String currentVersion)
    : juce::Thread("akroasys Update Check"), currentVersion_(std::move(currentVersion))
{
}

UpdateChecker::~UpdateChecker()
{
    stopThread(4000);
}

bool UpdateChecker::isNewer(const juce::String& latestTag, const juce::String& current)
{
    return compareSemVer(parseSemVer(latestTag), parseSemVer(current)) > 0;
}

void UpdateChecker::run()
{
    juce::URL url(kReleasesApiUrl);
    auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(5000)
                    .withExtraHeaders("User-Agent: akroasys-UpdateChecker\r\nAccept: application/vnd.github+json");

    auto stream = url.createInputStream(opts);
    if (stream == nullptr || threadShouldExit())
        return;

    const auto body = stream->readEntireStreamAsString();
    if (threadShouldExit() || body.isEmpty())
        return;

    auto parsed = juce::JSON::parse(body);
    auto* releases = parsed.getArray();   // null if GitHub returned an error object
    if (releases == nullptr || threadShouldExit())
        return;

    // Pick the highest-precedence non-draft release across the page (the API sorts
    // by created_at, but a re-tagged patch could land out of version order, so we
    // compare rather than trusting position).
    juce::String bestTag, bestUrl;
    SemVer bestVer;
    bool haveBest = false;
    for (const auto& item : *releases)
    {
        auto* obj = item.getDynamicObject();
        if (obj == nullptr)
            continue;
        if (static_cast<bool>(obj->getProperty("draft")))
            continue;
        const juce::String tag = obj->getProperty("tag_name").toString();
        if (tag.isEmpty())
            continue;
        const SemVer v = parseSemVer(tag);
        if (! haveBest || compareSemVer(v, bestVer) > 0)
        {
            haveBest = true;
            bestVer  = v;
            bestTag  = tag;
            bestUrl  = obj->getProperty("html_url").toString();
        }
    }

    if (! haveBest || threadShouldExit())
        return;

    if (compareSemVer(bestVer, parseSemVer(currentVersion_)) > 0)
    {
        auto cb = onUpdateAvailable;
        juce::MessageManager::callAsync([cb, bestTag, bestUrl]
        {
            if (cb)
                cb(bestTag, bestUrl);
        });
    }
}
