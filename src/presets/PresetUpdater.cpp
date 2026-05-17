#include "PresetUpdater.h"
#include "PresetFormat.h"
#include <thread>

namespace
{
constexpr const char* kManifestUrl =
    "https://raw.githubusercontent.com/joeriben/T5ynth-Presets/main/manifest.json";

constexpr const char* kBankName = "UCDCAE AI Lab";

juce::String toHex(const juce::uint8* bytes, size_t len)
{
    juce::String s;
    s.preallocateBytes(len * 2);
    static const char* hex = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i)
    {
        s += hex[(bytes[i] >> 4) & 0x0f];
        s += hex[bytes[i] & 0x0f];
    }
    return s;
}
}

PresetUpdater::PresetUpdater() = default;

PresetUpdater::~PresetUpdater()
{
    // Signal cancel and walk away. The background thread holds its own
    // shared_ptr<State>, so the state object survives this destructor and
    // the worker can finish its current file boundary, release the URL
    // stream, and exit cleanly. No thread join → the plugin window closes
    // immediately even if a slow HTTP read is in progress.
    cancel();
}

juce::File PresetUpdater::getBankDirectory()
{
    auto dir = PresetFormat::getUserPresetsDirectory().getChildFile(kBankName);
    dir.createDirectory();
    return dir;
}

juce::String PresetUpdater::getBankName() { return kBankName; }
juce::String PresetUpdater::getManifestUrl() { return kManifestUrl; }

void PresetUpdater::cancel()
{
    if (activeState_)
        activeState_->cancel.store(true);
}

juce::String PresetUpdater::sha256OfFile(const juce::File& file)
{
    juce::FileInputStream in(file);
    if (! in.openedOk()) return {};
    juce::SHA256 hash(in);
    auto raw = hash.getRawData();
    return toHex(reinterpret_cast<const juce::uint8*>(raw.getData()), raw.getSize());
}

bool PresetUpdater::fetchManifest(ParsedManifest& out, juce::String& error)
{
    juce::URL url(kManifestUrl);
    auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(15000);
    auto stream = url.createInputStream(opts);
    if (! stream)
    {
        error = "Could not reach " + juce::String(kManifestUrl)
              + "\n\nCheck your network connection.";
        return false;
    }

    const auto body = stream->readEntireStreamAsString();
    if (body.isEmpty())
    {
        error = "Manifest was empty.";
        return false;
    }

    const auto parsed = juce::JSON::parse(body);
    auto* root = parsed.getDynamicObject();
    if (root == nullptr)
    {
        error = "Manifest is not valid JSON.";
        return false;
    }

    out.baseUrl = root->getProperty("base_url").toString();
    if (out.baseUrl.isEmpty())
    {
        // Default: sibling files in the repo (.t5p at the repo root, same
        // dir as manifest.json). The manifest itself usually specifies
        // base_url explicitly — this is a safety net.
        out.baseUrl = "https://raw.githubusercontent.com/joeriben/T5ynth-Presets/main/";
    }
    if (! out.baseUrl.endsWithChar('/'))
        out.baseUrl += "/";

    auto* arr = root->getProperty("presets").getArray();
    if (arr == nullptr)
    {
        error = "Manifest has no \"presets\" array.";
        return false;
    }

    for (auto& v : *arr)
    {
        auto* obj = v.getDynamicObject();
        if (obj == nullptr) continue;
        ManifestEntry e;
        e.name   = obj->getProperty("name").toString();
        e.path   = obj->getProperty("path").toString();
        e.size   = static_cast<int64_t>(static_cast<double>(obj->getProperty("size")));
        e.sha256 = obj->getProperty("sha256").toString().toLowerCase();
        if (e.path.isEmpty()) continue;
        if (e.name.isEmpty()) e.name = juce::File(e.path).getFileNameWithoutExtension();
        out.entries.push_back(std::move(e));
    }

    if (out.entries.empty())
    {
        error = "Manifest lists zero presets.";
        return false;
    }
    return true;
}

bool PresetUpdater::downloadOne(const juce::URL& url,
                                const juce::File& target,
                                int64_t expectedSize,
                                const std::shared_ptr<State>& state,
                                juce::String& error)
{
    auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(15000);
    auto stream = url.createInputStream(opts);
    if (! stream)
    {
        error = "Could not open " + url.toString(false);
        return false;
    }

    target.getParentDirectory().createDirectory();
    juce::TemporaryFile temp(target);
    {
        juce::FileOutputStream out(temp.getFile());
        if (out.failedToOpen())
        {
            error = "Cannot write " + target.getFullPathName();
            return false;
        }

        char buffer[65536];
        int64_t written = 0;
        while (! state->cancel.load())
        {
            const auto n = stream->read(buffer, sizeof(buffer));
            if (n <= 0) break;
            if (! out.write(buffer, static_cast<size_t>(n)))
            {
                error = "Write failed for " + target.getFileName();
                return false;
            }
            written += n;
        }
        out.flush();
        if (state->cancel.load())
        {
            error = "Cancelled";
            return false;
        }

        // Sanity-check the byte count when the manifest declared one.
        // A truncated download for a binary preset would be silently
        // corrupted otherwise (.t5p has no internal length redundancy
        // past the JSON header).
        if (expectedSize > 0 && written < expectedSize)
        {
            error = "Truncated transfer: expected " + juce::String(expectedSize)
                  + " bytes, got " + juce::String(written);
            return false;
        }
    }

    return temp.overwriteTargetFileWithTemporary();
}

void PresetUpdater::start(ProgressCallback onProgress, FinishCallback onFinish)
{
    // Refuse re-entry while a previous run is still going. isRunning()
    // observes activeState_->running, so a previously-finished run leaves
    // activeState_ pointing at a "done" state (running=false) and a new
    // start() proceeds.
    if (isRunning())
        return;

    auto state = std::make_shared<State>();
    activeState_ = state;
    auto bankDir = getBankDirectory();

    std::thread([state, bankDir,
                 onProgress = std::move(onProgress),
                 onFinish   = std::move(onFinish)]()
    {
        // All shared with the message thread purely via the shared_ptr
        // `state` and the std::function callbacks captured by value — the
        // worker NEVER dereferences the originating PresetUpdater.

        auto reportProgress = [&onProgress](double frac, juce::String msg)
        {
            if (onProgress)
                juce::MessageManager::callAsync([onProgress, frac, msg]
                {
                    onProgress(frac, msg);
                });
        };

        auto finish = [&onFinish, &state](bool ok, Stats stats, juce::String err)
        {
            // Mark done BEFORE handing the result off so a callback that
            // immediately re-checks isRunning() sees the worker as idle.
            state->running.store(false);
            if (onFinish)
                juce::MessageManager::callAsync([onFinish, ok, stats, err]
                {
                    onFinish(ok, stats, err);
                });
        };

        reportProgress(0.0, "Fetching manifest...");

        ParsedManifest manifest;
        juce::String err;
        if (! fetchManifest(manifest, err))
        {
            finish(false, {}, err);
            return;
        }

        if (state->cancel.load()) { finish(false, {}, "Cancelled"); return; }

        Stats stats;
        const auto total = static_cast<int>(manifest.entries.size());

        // Names that appear in the manifest — used at the end to prune
        // orphaned local files (presets the maintainer removed upstream).
        std::set<juce::String> manifestPaths;

        for (int i = 0; i < total; ++i)
        {
            if (state->cancel.load()) { finish(false, stats, "Cancelled"); return; }

            const auto& entry = manifest.entries[static_cast<size_t>(i)];
            manifestPaths.insert(entry.path);

            const auto target = bankDir.getChildFile(entry.path);
            const bool exists = target.existsAsFile();

            // Hash-match skip: file present and SHA matches manifest →
            // no work needed. The hash check covers two cases the size
            // alone can't: (a) silent corruption of a previously good
            // file, (b) an upstream content change that kept the byte
            // count identical.
            bool needsDownload = ! exists;
            if (exists && entry.sha256.isNotEmpty())
            {
                if (sha256OfFile(target) != entry.sha256)
                    needsDownload = true;
            }
            else if (exists && entry.size > 0 && target.getSize() != entry.size)
            {
                needsDownload = true;
            }

            if (! needsDownload)
            {
                stats.unchanged++;
                reportProgress(static_cast<double>(i + 1) / total,
                               "Up to date: " + entry.name
                               + " (" + juce::String(i + 1) + "/" + juce::String(total) + ")");
                continue;
            }

            reportProgress(static_cast<double>(i) / total,
                           "Downloading " + entry.name
                           + " (" + juce::String(i + 1) + "/" + juce::String(total) + ")");

            // URL-encode each path segment separately. juce::URL::addEscapeChars
            // with isParameter=false would otherwise escape '/' to %2F, breaking
            // any future subdir layout. Most preset filenames contain spaces,
            // which raw.githubusercontent.com rejects without %20.
            juce::StringArray segments;
            segments.addTokens(entry.path, "/", {});
            for (auto& seg : segments)
                seg = juce::URL::addEscapeChars(seg, false);
            const juce::URL fileUrl(manifest.baseUrl + segments.joinIntoString("/"));
            juce::String dlErr;
            const bool wasNew = ! exists;
            if (downloadOne(fileUrl, target, entry.size, state, dlErr))
            {
                if (wasNew) stats.added++;
                else        stats.updated++;
            }
            else
            {
                stats.failed++;
                juce::Logger::writeToLog("PresetUpdater: failed " + entry.path + ": " + dlErr);
                if (state->cancel.load()) { finish(false, stats, "Cancelled"); return; }
            }
        }

        // Prune orphans: any .t5p in the bank dir that isn't in the manifest.
        // This keeps the bank consistent with upstream when presets are
        // renamed or retired. Subdirectories are left alone (a user
        // shouldn't be putting files here anyway — the bank is read-only
        // from the UI side — but just in case).
        for (auto& f : bankDir.findChildFiles(juce::File::findFiles, false, "*.t5p"))
        {
            if (manifestPaths.find(f.getFileName()) == manifestPaths.end())
            {
                if (f.deleteFile())
                    stats.removed++;
            }
        }

        reportProgress(1.0, "Done");
        finish(true, stats, {});
    }).detach();
}
