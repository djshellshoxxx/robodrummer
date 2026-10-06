// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace robodrummer {

struct TempoHypothesis {
    double bpm{120.0};
    double beatPhase{0.0};
    float confidence{0.0f};
    std::size_t supportingEventCount{0};
    float recentConsistency{0.0f};
    double ageSeconds{0.0};
    double predictedNextBeatSeconds{0.0};
    double halfTimeBpm{60.0};
    double doubleTimeBpm{240.0};
};

class TempoTracker {
public:
    static constexpr std::size_t HistorySize = 24;
    static constexpr double MinBpm = 40.0;
    static constexpr double MaxBpm = 240.0;
    static constexpr double BinSize = 0.5;
    static constexpr std::size_t BinCount = static_cast<std::size_t>((MaxBpm - MinBpm) / BinSize) + 1;

    void reset() noexcept {
        histogram_.fill(0.0f);
        firstEvidenceSeconds_.fill(std::numeric_limits<double>::quiet_NaN());
        onsetTimes_.fill(0.0);
        onsetStrengths_.fill(0.0f);
        onsetCount_ = 0;
        writeIndex_ = 0;
        lastOnsetSeconds_ = std::numeric_limits<double>::quiet_NaN();
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
            if (!(dt > 0.08) || dt > 3.2) continue;
            const float pairStrength = std::sqrt(strength * onsetStrengths_[idx]);
            const float lagWeight = 1.0f / static_cast<float>(lag * lag);
            voteFamily(60.0 / dt, pairStrength * lagWeight, seconds);
        }

        onsetTimes_[writeIndex_] = seconds;
        onsetStrengths_[writeIndex_] = strength;
        writeIndex_ = (writeIndex_ + 1) % onsetTimes_.size();
        ++onsetCount_;
        lastOnsetSeconds_ = seconds;
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
            result[rank] = hypothesisForBin(idx);
            const int radius = static_cast<int>(std::llround(3.0 / BinSize));
            const int center = static_cast<int>(idx);
            for (int j = std::max(0, center - radius); j <= std::min(static_cast<int>(BinCount) - 1, center + radius); ++j)
                scratch[static_cast<std::size_t>(j)] = 0.0f;
        }
        return result;
    }

private:
    void vote(double bpm, float weight, double seconds) noexcept {
        if (!std::isfinite(bpm) || bpm < MinBpm || bpm > MaxBpm || weight <= 0.0f) return;
        const int center = static_cast<int>(std::llround((bpm - MinBpm) / BinSize));
        for (int offset = -2; offset <= 2; ++offset) {
            const int bin = center + offset;
            if (bin < 0 || bin >= static_cast<int>(BinCount)) continue;
            const float kernel = (offset == 0 ? 1.0f : (std::abs(offset) == 1 ? 0.60f : 0.22f));
            const auto index = static_cast<std::size_t>(bin);
            histogram_[index] += weight * kernel;
            if (!std::isfinite(firstEvidenceSeconds_[index]))
                firstEvidenceSeconds_[index] = seconds;
        }
    }

    void voteFamily(double directBpm, float weight, double seconds) noexcept {
        vote(directBpm, weight, seconds);
        vote(directBpm * 0.5, weight * 0.82f, seconds);
        vote(directBpm * 2.0, weight * 0.55f, seconds);
        vote(directBpm / 3.0, weight * 0.28f, seconds);
        vote(directBpm * 1.5, weight * 0.22f, seconds);
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

    [[nodiscard]] std::size_t supportForBpm(double bpm) const noexcept {
        const std::size_t count = std::min<std::size_t>(onsetCount_, onsetTimes_.size());
        if (count < 2 || !(bpm > 0.0)) return 0;

        std::array<double, HistorySize> ordered{};
        const std::size_t start = onsetCount_ <= onsetTimes_.size() ? 0 : writeIndex_;
        for (std::size_t i = 0; i < count; ++i)
            ordered[i] = onsetTimes_[(start + i) % onsetTimes_.size()];

        const double period = 60.0 / bpm;
        std::size_t support = 0;
        for (std::size_t i = 1; i < count; ++i) {
            const double dt = ordered[i] - ordered[i - 1];
            if (!(dt > 0.0)) continue;
            const double ratio = dt / period;
            const double nearest = std::max(1.0, std::round(ratio));
            const double error = std::abs(ratio - nearest) / nearest;
            if (error <= 0.12)
                ++support;
        }
        return support;
    }

    [[nodiscard]] TempoHypothesis hypothesisForBin(std::size_t index) const noexcept {
        TempoHypothesis result;
        result.bpm = MinBpm + static_cast<double>(index) * BinSize;
        result.confidence = confidenceForBin(index);
        result.supportingEventCount = supportForBpm(result.bpm);
        const std::size_t evidenceTarget = std::min<std::size_t>(8, std::max<std::size_t>(1, onsetCount_ > 0 ? onsetCount_ - 1 : 0));
        result.recentConsistency = std::clamp(
            static_cast<float>(result.supportingEventCount) / static_cast<float>(evidenceTarget),
            0.0f,
            1.0f);
        const double firstEvidence = firstEvidenceSeconds_[index];
        result.ageSeconds = std::isfinite(firstEvidence) && std::isfinite(lastOnsetSeconds_)
            ? std::max(0.0, lastOnsetSeconds_ - firstEvidence)
            : 0.0;
        result.beatPhase = 0.0;
        const double period = 60.0 / std::max(MinBpm, result.bpm);
        result.predictedNextBeatSeconds = std::isfinite(lastOnsetSeconds_)
            ? lastOnsetSeconds_ + period
            : 0.0;
        result.halfTimeBpm = result.bpm * 0.5;
        result.doubleTimeBpm = result.bpm * 2.0;
        return result;
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

        best_ = hypothesisForBin(idx);
        hasBest_ = true;
    }

    std::array<float, BinCount> histogram_{};
    std::array<double, BinCount> firstEvidenceSeconds_{};
    std::array<double, HistorySize> onsetTimes_{};
    std::array<float, HistorySize> onsetStrengths_{};
    std::size_t onsetCount_{0};
    std::size_t writeIndex_{0};
    double lastOnsetSeconds_{std::numeric_limits<double>::quiet_NaN()};
    TempoHypothesis best_{};
    bool hasBest_{false};
};

} // namespace robodrummer
