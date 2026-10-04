#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace robodrummer {

struct TempoHypothesis {
    double bpm{120.0};
    float confidence{0.0f};
};

class TempoTracker {
public:
    static constexpr double MinBpm = 45.0;
    static constexpr double MaxBpm = 220.0;
    static constexpr double BinSize = 0.5;
    static constexpr std::size_t BinCount = static_cast<std::size_t>((MaxBpm - MinBpm) / BinSize) + 1;

    void reset() noexcept {
        histogram_.fill(0.0f);
        onsetTimes_.fill(0.0);
        onsetStrengths_.fill(0.0f);
        onsetCount_ = 0;
        writeIndex_ = 0;
        best_ = {};
        hasBest_ = false;
    }

    void addOnset(double seconds, float strength = 1.0f) noexcept {
        if (!std::isfinite(seconds) || strength <= 0.0f) return;
        strength = std::clamp(strength, 0.05f, 1.0f);
        for (auto& score : histogram_) score *= 0.965f;

        const std::size_t available = std::min<std::size_t>(onsetCount_, onsetTimes_.size());
        for (std::size_t lag = 1; lag <= available; ++lag) {
            const std::size_t idx = (writeIndex_ + onsetTimes_.size() - lag) % onsetTimes_.size();
            const double dt = seconds - onsetTimes_[idx];
            if (!(dt > 0.08) || dt > 2.8) continue;
            const float pairStrength = std::sqrt(strength * onsetStrengths_[idx]);
            const float lagWeight = 1.0f / static_cast<float>(lag * lag);
            voteFamily(60.0 / dt, pairStrength * lagWeight);
        }

        onsetTimes_[writeIndex_] = seconds;
        onsetStrengths_[writeIndex_] = strength;
        writeIndex_ = (writeIndex_ + 1) % onsetTimes_.size();
        ++onsetCount_;
        updateBest();
    }

    [[nodiscard]] TempoHypothesis best() const noexcept { return best_; }

    [[nodiscard]] std::array<TempoHypothesis, 3> topHypotheses() const noexcept {
        std::array<TempoHypothesis, 3> result{};
        std::array<float, BinCount> scratch = histogram_;
        for (std::size_t rank = 0; rank < result.size(); ++rank) {
            const auto it = std::max_element(scratch.begin(), scratch.end());
            if (it == scratch.end() || *it <= 0.0f) break;
            const std::size_t idx = static_cast<std::size_t>(std::distance(scratch.begin(), it));
            result[rank] = {MinBpm + static_cast<double>(idx) * BinSize, confidenceForBin(idx)};
            const int radius = static_cast<int>(std::llround(3.0 / BinSize));
            const int center = static_cast<int>(idx);
            for (int j = std::max(0, center - radius); j <= std::min(static_cast<int>(BinCount) - 1, center + radius); ++j)
                scratch[static_cast<std::size_t>(j)] = 0.0f;
        }
        return result;
    }

private:
    void vote(double bpm, float weight) noexcept {
        if (!std::isfinite(bpm) || bpm < MinBpm || bpm > MaxBpm || weight <= 0.0f) return;
        const int center = static_cast<int>(std::llround((bpm - MinBpm) / BinSize));
        for (int offset = -2; offset <= 2; ++offset) {
            const int bin = center + offset;
            if (bin < 0 || bin >= static_cast<int>(BinCount)) continue;
            const float kernel = (offset == 0 ? 1.0f : (std::abs(offset) == 1 ? 0.60f : 0.22f));
            histogram_[static_cast<std::size_t>(bin)] += weight * kernel;
        }
    }

    void voteFamily(double directBpm, float weight) noexcept {
        vote(directBpm, weight);
        vote(directBpm * 0.5, weight * 0.82f);
        vote(directBpm * 2.0, weight * 0.55f);
        vote(directBpm / 3.0, weight * 0.28f);
        vote(directBpm * 1.5, weight * 0.22f);
    }

    [[nodiscard]] float confidenceForBin(std::size_t index) const noexcept {
        float local = 0.0f;
        float total = 0.0f;
        for (std::size_t i = 0; i < histogram_.size(); ++i) {
            total += histogram_[i];
            const auto distance = static_cast<long long>(i) - static_cast<long long>(index);
            if (std::llabs(distance) <= 3) local += histogram_[i];
        }
        if (total <= 1.0e-6f) return 0.0f;
        const float concentration = local / total;
        const float evidence = std::clamp(static_cast<float>(onsetCount_) / 10.0f, 0.0f, 1.0f);
        return std::clamp(concentration * 2.8f * evidence, 0.0f, 1.0f);
    }

    void updateBest() noexcept {
        const auto it = std::max_element(histogram_.begin(), histogram_.end());
        if (it == histogram_.end() || *it <= 0.0f) {
            best_ = {};
            hasBest_ = false;
            return;
        }

        std::size_t idx = static_cast<std::size_t>(std::distance(histogram_.begin(), it));
        const double candidateBpm = MinBpm + static_cast<double>(idx) * BinSize;

        if (hasBest_) {
            const int currentIndex = std::clamp(
                static_cast<int>(std::llround((best_.bpm - MinBpm) / BinSize)),
                0, static_cast<int>(BinCount) - 1);
            const float currentScore = histogram_[static_cast<std::size_t>(currentIndex)];
            const float candidateScore = histogram_[idx];
            const bool nearbyDrift = std::abs(candidateBpm - best_.bpm) <= 8.0;
            const bool decisiveSwitch = currentScore <= 1.0e-6f || candidateScore > currentScore * 1.18f;
            if (!nearbyDrift && !decisiveSwitch) idx = static_cast<std::size_t>(currentIndex);
        }

        best_.bpm = MinBpm + static_cast<double>(idx) * BinSize;
        best_.confidence = confidenceForBin(idx);
        hasBest_ = true;
    }

    std::array<float, BinCount> histogram_{};
    std::array<double, 24> onsetTimes_{};
    std::array<float, 24> onsetStrengths_{};
    std::size_t onsetCount_{0};
    std::size_t writeIndex_{0};
    TempoHypothesis best_{};
    bool hasBest_{false};
};

} // namespace robodrummer
