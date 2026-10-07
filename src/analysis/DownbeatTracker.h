#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace robodrummer {

struct DownbeatState {
    int meterNumerator{4};
    int currentBeatInBar{1};
    int nextBeatInBar{2};
    float downbeatConfidence{0.0f};
};

class DownbeatTracker {
public:
    void reset(int meterNumerator = 4) noexcept {
        meter_ = std::clamp(meterNumerator, 2, MaxMeter);
        slotEnergy_.fill(0.0f);
        slotCount_.fill(0.0f);
        beatIndex_ = 0;
        inferredDownbeatSlot_ = 0;
        confidence_ = 0.0f;
    }

    void observeBeat(float accentStrength) noexcept {
        accentStrength = std::clamp(std::isfinite(accentStrength) ? accentStrength : 0.0f, 0.0f, 1.0f);

        // Keep a long-enough memory for phrase stability while allowing gradual adaptation.
        for (int i = 0; i < meter_; ++i) {
            slotEnergy_[static_cast<std::size_t>(i)] *= 0.985f;
            slotCount_[static_cast<std::size_t>(i)] *= 0.985f;
        }

        const int slot = beatIndex_ % meter_;
        slotEnergy_[static_cast<std::size_t>(slot)] += accentStrength;
        slotCount_[static_cast<std::size_t>(slot)] += 1.0f;
        ++beatIndex_;
        updateInference();
    }

    [[nodiscard]] DownbeatState state() const noexcept {
        if (beatIndex_ <= 0)
            return {meter_, 1, meter_ > 1 ? 2 : 1, 0.0f};

        const int lastSlot = (beatIndex_ - 1) % meter_;
        const int current = ((lastSlot - inferredDownbeatSlot_ + meter_) % meter_) + 1;
        const int nextSlot = beatIndex_ % meter_;
        const int next = ((nextSlot - inferredDownbeatSlot_ + meter_) % meter_) + 1;
        return {meter_, current, next, confidence_};
    }

private:
    static constexpr int MaxMeter = 12;

    void updateInference() noexcept {
        float bestMean = -1.0f;
        float secondMean = -1.0f;
        int bestSlot = inferredDownbeatSlot_;
        float totalEvidence = 0.0f;

        for (int i = 0; i < meter_; ++i) {
            const auto index = static_cast<std::size_t>(i);
            const float count = slotCount_[index];
            const float mean = count > 1.0e-5f ? slotEnergy_[index] / count : 0.0f;
            totalEvidence += count;
            if (mean > bestMean) {
                secondMean = bestMean;
                bestMean = mean;
                bestSlot = i;
            } else if (mean > secondMean) {
                secondMean = mean;
            }
        }

        if (secondMean < 0.0f) secondMean = 0.0f;
        // Normalize contrast against a musically meaningful fraction of the leading accent.
        // Using the full peak as the denominator systematically under-reported confidence
        // even after many perfectly repeated bars (and kept recovery logic needlessly timid).
        const float separation = std::clamp(
            (bestMean - secondMean) / std::max(0.15f, bestMean * 0.75f),
            0.0f,
            1.0f);
        const float evidence = std::clamp(totalEvidence / static_cast<float>(meter_ * 4), 0.0f, 1.0f);
        const float proposedConfidence = separation * evidence;

        // Hysteresis: only rotate the inferred bar when the competing phase is convincingly better.
        if (bestSlot == inferredDownbeatSlot_ || proposedConfidence >= std::max(0.45f, confidence_ + 0.12f))
            inferredDownbeatSlot_ = bestSlot;

        confidence_ += (proposedConfidence - confidence_) * 0.20f;
        confidence_ = std::clamp(confidence_, 0.0f, 1.0f);
    }

    int meter_{4};
    int beatIndex_{0};
    int inferredDownbeatSlot_{0};
    float confidence_{0.0f};
    std::array<float, MaxMeter> slotEnergy_{};
    std::array<float, MaxMeter> slotCount_{};
};

} // namespace robodrummer
