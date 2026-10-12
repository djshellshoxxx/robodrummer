// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#include "plugin/KitLoader.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace robodrummer {

namespace {

std::string errLine(const juce::File& f, const std::string& msg) {
    return f.getFileName().toStdString() + ": " + msg;
}

} // namespace

struct KitLoader::Impl {
    juce::ThreadPool pool{juce::ThreadPoolOptions{}.withNumberOfThreads(1)};
    std::shared_ptr<std::atomic<bool>> cancelFlag{std::make_shared<std::atomic<bool>>(false)};
    std::shared_ptr<std::atomic<bool>> alive{std::make_shared<std::atomic<bool>>(true)};
    std::vector<std::string> lastErrors;
    std::atomic<int> generation{0};
};

KitLoader::KitLoader() : impl_(std::make_unique<Impl>()) {}

KitLoader::~KitLoader() {
    impl_->alive->store(false);
    impl_->cancelFlag->store(true);
    impl_->pool.removeAllJobs(true, 20000);
}

void KitLoader::cancel() { impl_->cancelFlag->store(true); }

std::vector<std::string> KitLoader::errors() const { return impl_->lastErrors; }

juce::String KitLoader::statusText() const {
    switch (status_.load()) {
        case Status::Idle: return "No custom kit loaded";
        case Status::Loading: return "Loading kit... " + juce::String(juce::roundToInt(progress_.load() * 100.0f)) + "%";
        case Status::Loaded: {
            const auto n = impl_->lastErrors.size();
            return n == 0 ? juce::String("Kit loaded")
                          : "Kit loaded, " + juce::String(static_cast<int>(n)) + " file(s) skipped";
        }
        case Status::Failed: return "Kit failed to load (using starter kit)";
    }
    return {};
}

void KitLoader::loadAsync(juce::File folder, double sampleRate, Callback callback) {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    impl_->cancelFlag->store(true); // cancel a load in progress
    auto flag = std::make_shared<std::atomic<bool>>(false);
    impl_->cancelFlag = flag;
    const int gen = ++impl_->generation;
    status_.store(Status::Loading);
    progress_.store(0.0f);

    auto alive = impl_->alive;
    Impl* impl = impl_.get();
    impl_->pool.addJob([this, impl, alive, flag, gen, folder, sampleRate, callback = std::move(callback)]() mutable {
        auto result = std::make_shared<KitLoadResult>(
            loadKitFolder(folder, sampleRate, [flag] { return flag->load(); }, &progress_));
        juce::MessageManager::callAsync([this, impl, alive, gen, result, callback = std::move(callback)]() mutable {
            if (!alive->load()) return; // loader destroyed
            if (gen == impl->generation.load()) {
                impl->lastErrors = result->errors;
                progress_.store(1.0f);
                status_.store(result->cancelled ? Status::Idle
                              : result->kit ? Status::Loaded : Status::Failed);
            }
            if (callback) callback(std::move(*result));
        });
    });
}

KitLoadResult KitLoader::loadKitFolder(const juce::File& folder, double sampleRate,
                                       const std::function<bool()>& shouldCancel,
                                       std::atomic<float>* progress) {
    KitLoadResult out;
    const auto cancelled = [&] { return shouldCancel && shouldCancel(); };
    if (!(sampleRate >= 8000.0 && sampleRate <= 768000.0)) {
        out.errors.push_back("invalid sample rate");
        return out;
    }
    if (!folder.isDirectory()) {
        out.errors.push_back("kit folder not found: " + folder.getFullPathName().toStdString());
        return out;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    auto files = folder.findChildFiles(juce::File::findFiles, false);
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareNatural(b.getFileName()) < 0;
    });

    KitConfig config{};
    DrumKitBuilder builder;
    std::vector<juce::File> audioFiles;

    for (const auto& f : files) {
        const auto name = f.getFileName();
        if (name.startsWithChar('.')) continue; // hidden / macOS resource forks
        if (name.equalsIgnoreCase("kit.txt")) {
            if (f.getSize() > 64 * 1024) { out.errors.push_back(errLine(f, "kit.txt too large (max 64 KB)")); continue; }
            parseKitConfig(f.loadFileAsString().toStdString(), config, out.errors);
            continue;
        }
        const auto ext = f.getFileExtension().trimCharactersAtStart(".").toStdString();
        if (!isKitAudioExtension(ext)) continue; // not a kit file
        audioFiles.push_back(f);
    }

    for (std::size_t idx = 0; idx < audioFiles.size(); ++idx) {
        if (cancelled()) { out.cancelled = true; out.errors.push_back("load cancelled"); out.kit.reset(); return out; }
        if (progress) progress->store(static_cast<float>(idx) / static_cast<float>(audioFiles.size()));
        const auto& f = audioFiles[idx];

        const auto parsed = parseKitFileName(f.getFileName().toStdString());
        if (!parsed) {
            out.errors.push_back(errLine(f, "unrecognised file name (see KIT_FORMAT.md)"));
            ++out.filesSkipped;
            continue;
        }

        try {
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(f));
            if (!reader) { out.errors.push_back(errLine(f, "unsupported or corrupt audio file")); ++out.filesSkipped; continue; }
            if (reader->lengthInSamples <= 0 || reader->sampleRate < 1000.0) { out.errors.push_back(errLine(f, "empty or invalid audio")); ++out.filesSkipped; continue; }
            if (reader->numChannels < 1 || reader->numChannels > 2) { out.errors.push_back(errLine(f, "only mono or stereo files are supported")); ++out.filesSkipped; continue; }
            const double seconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
            if (seconds > MaxSampleSeconds) { out.errors.push_back(errLine(f, "longer than 20 seconds")); ++out.filesSkipped; continue; }

            const auto srcLen = static_cast<int>(reader->lengthInSamples);
            const double ratio = reader->sampleRate / sampleRate; // input samples per output sample
            const bool resample = std::abs(ratio - 1.0) > 1e-4;
            const auto outLen = resample ? static_cast<std::size_t>(std::ceil(static_cast<double>(srcLen) / ratio))
                                         : static_cast<std::size_t>(srcLen);
            const std::size_t bytes = outLen * 2 * sizeof(float);
            if (out.bytesUsed + bytes > MaxKitBytes) { out.errors.push_back(errLine(f, "kit size limit (64 MB) exceeded")); ++out.filesSkipped; continue; }

            const int numCh = static_cast<int>(reader->numChannels);
            juce::AudioBuffer<float> in(2, srcLen + 16);
            in.clear();
            if (!reader->read(&in, 0, srcLen, 0, true, numCh > 1)) { out.errors.push_back(errLine(f, "read error (truncated or corrupt)")); ++out.filesSkipped; continue; }
            if (numCh == 1) in.copyFrom(1, 0, in, 0, 0, srcLen);

            DrumSample s;
            s.left.assign(outLen, 0.0f);
            s.right.assign(outLen, 0.0f);
            for (int ch = 0; ch < 2; ++ch) {
                auto& dst = ch == 0 ? s.left : s.right;
                if (resample) {
                    juce::LagrangeInterpolator interp;
                    interp.process(ratio, in.getReadPointer(ch), dst.data(), static_cast<int>(outLen));
                } else {
                    std::copy_n(in.getReadPointer(ch), outLen, dst.begin());
                }
                for (auto& v : dst)
                    if (!std::isfinite(v)) v = 0.0f;
            }
            if (!builder.add(*parsed, std::move(s))) {
                out.errors.push_back(errLine(f, "duplicate of another file for the same instrument/velocity/round-robin"));
                ++out.filesSkipped;
                continue;
            }
            out.bytesUsed += bytes;
            ++out.filesLoaded;
        } catch (const std::bad_alloc&) {
            out.errors.push_back(errLine(f, "out of memory"));
            ++out.filesSkipped;
        } catch (...) {
            out.errors.push_back(errLine(f, "unexpected error while decoding"));
            ++out.filesSkipped;
        }
    }

    if (cancelled()) { out.cancelled = true; return out; }
    if (builder.empty()) {
        out.errors.push_back("no usable samples found in folder");
        return out;
    }
    out.kit = builder.build(config, folder.getFileName().toStdString());
    if (progress) progress->store(1.0f);
    return out;
}

} // namespace robodrummer
