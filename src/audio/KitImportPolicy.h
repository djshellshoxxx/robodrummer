#pragma once
// User-kit import resource policy. JUCE-free; all calls run off the audio thread.
#include "audio/DrumSamplePlayer.h"
#include "core/KitFileName.h"
#include <cstdint>
#include <optional>

namespace robodrummer {

// Covers retained decoded samples AND the simultaneous source/resampling buffers.
// 256 MiB supports substantial layered kits without letting a selected folder
// consume the DAW's address space. Each individual file is limited to 20 seconds.
inline constexpr std::size_t kMaxKitDecodedBytes = 256u * 1024u * 1024u;
inline constexpr double kMaxKitFileSeconds = 20.0;
inline constexpr std::size_t kKitSlotCount = DrumSamplePlayer::InstrumentCount
    * DrumSamplePlayer::MaxVelocityLayers * DrumSamplePlayer::MaxRoundRobinVariants;

[[nodiscard]] inline std::optional<std::size_t> kitSlotIndex(const KitFileInfo& info) noexcept {
    const auto instrument = static_cast<std::size_t>(info.instrument);
    if (instrument >= DrumSamplePlayer::InstrumentCount || info.layer < 0 || info.roundRobin < 0
        || info.layer >= static_cast<int>(DrumSamplePlayer::MaxVelocityLayers)
        || info.roundRobin >= static_cast<int>(DrumSamplePlayer::MaxRoundRobinVariants)) return {};
    return (instrument * DrumSamplePlayer::MaxVelocityLayers + static_cast<std::size_t>(info.layer))
        * DrumSamplePlayer::MaxRoundRobinVariants + static_cast<std::size_t>(info.roundRobin);
}

struct KitReadPlan {
    int sourceFrames{}, targetFrames{};
    std::size_t sourceBytes{}, targetBytes{};
};

[[nodiscard]] inline std::optional<KitReadPlan> planKitRead(std::int64_t frames, double sourceRate,
                                                          double targetRate, unsigned channels) noexcept {
    if (frames <= 0 || !std::isfinite(sourceRate) || sourceRate < 1.0 || sourceRate > 768000.0
        || !std::isfinite(targetRate) || targetRate < 8000.0 || targetRate > 768000.0
        || channels < 1 || channels > 2) return {};
    KitReadPlan plan;
    plan.sourceFrames = static_cast<int>(std::min(frames, static_cast<std::int64_t>(sourceRate * kMaxKitFileSeconds)));
    plan.targetFrames = std::abs(sourceRate - targetRate) < 0.5 ? plan.sourceFrames
        : std::max(1, static_cast<int>(std::floor(static_cast<double>(plan.sourceFrames) / (sourceRate / targetRate))));
    plan.sourceBytes = static_cast<std::size_t>(plan.sourceFrames) * channels * sizeof(float);
    plan.targetBytes = static_cast<std::size_t>(plan.targetFrames) * channels * sizeof(float);
    return plan;
}

class KitImportBudget {
public:
    explicit KitImportBudget(std::size_t maximumBytes, std::size_t fallbackBytes) noexcept
        : maximumBytes_(std::max(maximumBytes, fallbackBytes)), retainedBytes_(fallbackBytes) {}
    [[nodiscard]] bool fits(std::size_t sourceBytes, std::size_t targetBytes) const noexcept {
        const auto remaining = maximumBytes_ - retainedBytes_;
        return sourceBytes <= remaining && targetBytes <= remaining - sourceBytes;
    }
    void retain(std::size_t bytes) noexcept { retainedBytes_ += bytes; }
    [[nodiscard]] std::size_t retainedBytes() const noexcept { return retainedBytes_; }
private:
    std::size_t maximumBytes_, retainedBytes_;
};

} // namespace robodrummer
