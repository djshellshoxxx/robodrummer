// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include "audio/DrumKit.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace robodrummer {

struct KitLoadResult {
    std::unique_ptr<DrumKit> kit;       // null when nothing usable was loaded (keep current/starter kit)
    std::vector<std::string> errors;    // one human-readable line per skipped/invalid file or problem
    int filesLoaded{0};
    int filesSkipped{0};
    std::size_t bytesUsed{0};           // decoded size at plugin rate (floats * 4)
    bool cancelled{false};
};

// Loads a user kit folder (docs/KIT_FORMAT.md) on a background thread. Never touches the audio
// thread. Limits: <= 20 s per sample, <= 64 MB decoded per kit. WAV/AIFF only; corrupt or
// unsupported files are skipped and reported. Samples are resampled to the plugin rate and
// mono is duplicated to stereo.
//
// Threading: loadAsync/status/errors/cancel are message-thread calls. The callback runs on the
// JUCE message thread. Hand the result to the audio thread with KitHandoff::publish(std::move(kit)).
// A new loadAsync cancels a load in progress (its callback is invoked with cancelled = true).
// Destroying the loader cancels and waits for the worker; pending callbacks are dropped.
class KitLoader {
public:
    enum class Status { Idle, Loading, Loaded, Failed };

    static constexpr double MaxSampleSeconds = 20.0;
    static constexpr std::size_t MaxKitBytes = 64u * 1024u * 1024u;

    using Callback = std::function<void(KitLoadResult)>;

    KitLoader();
    ~KitLoader();
    KitLoader(const KitLoader&) = delete;
    KitLoader& operator=(const KitLoader&) = delete;

    void loadAsync(juce::File folder, double sampleRate, Callback callback);
    void cancel();

    [[nodiscard]] Status status() const noexcept { return status_.load(); }
    [[nodiscard]] float progress() const noexcept { return progress_.load(); } // 0..1
    [[nodiscard]] std::vector<std::string> errors() const; // from the last finished load
    [[nodiscard]] juce::String statusText() const;

    // Synchronous worker used by loadAsync (exposed for tooling/tests; do not call on the audio
    // thread). `shouldCancel` is polled between files.
    static KitLoadResult loadKitFolder(const juce::File& folder, double sampleRate,
                                       const std::function<bool()>& shouldCancel = {},
                                       std::atomic<float>* progress = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<Status> status_{Status::Idle};
    std::atomic<float> progress_{0.0f};
};

} // namespace robodrummer
