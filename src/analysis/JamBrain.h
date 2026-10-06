#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace robodrummer {

enum class PhraseState {
    Stable,
    Build,
    Release,
    Break
};

struct JamBarObservation {
    float intensity{0.5f};
    float activity{0.5f};
    float beatConfidence{0.0f};
    float downbeatConfidence{0.0f};
};

struct JamBrainSettings {
    int phraseBars{4};
    int minBarsBeforeFill{3};
    float trustedBeatConfidence{0.45f};
    float trustedDownbeatConfidence{0.40f};
    float breakActivityThreshold{0.08f};
    float fillBias{0.0f};
    float buildThreshold{0.08f};
    float releaseThreshold{0.08f};
};

struct JamBrainDecision {
    PhraseState phraseState{PhraseState::Stable};
    bool phraseBoundary{false};
    bool requestFill{false};
    bool breakLikely{false};
    float fillStrength{0.0f};
    float energyTrend{0.0f};
};

class JamBrain {
public:
    void reset() noexcept {
        barIndex_ = 0;
        historyCount_ = 0;
        lowActivityBars_ = 0;
        intensityHistory_.fill(0.5f);
    }

    [[nodiscard]] JamBrainDecision update(const JamBarObservation& observation,
                                          const JamBrainSettings& settings) noexcept {
        JamBrainDecision decision;
        const float intensity = std::clamp(observation.intensity, 0.0f, 1.0f);
        const float activity = std::clamp(observation.activity, 0.0f, 1.0f);

        pushIntensity(intensity);
        decision.energyTrend = trend();

        if (activity <= settings.breakActivityThreshold)
            ++lowActivityBars_;
        else
            lowActivityBars_ = 0;

        if (lowActivityBars_ >= 3) {
            decision.phraseState = PhraseState::Break;
            decision.breakLikely = true;
        } else if (decision.energyTrend >= std::max(0.0f, settings.buildThreshold)) {
            decision.phraseState = PhraseState::Build;
        } else if (decision.energyTrend <= -std::max(0.0f, settings.releaseThreshold)) {
            decision.phraseState = PhraseState::Release;
        } else {
            decision.phraseState = PhraseState::Stable;
        }

        const bool trusted = observation.beatConfidence >= settings.trustedBeatConfidence &&
                             observation.downbeatConfidence >= settings.trustedDownbeatConfidence;
        const int phraseBars = std::max(1, settings.phraseBars);
        const int nextBarNumber = barIndex_ + 1;
        decision.phraseBoundary = trusted && (nextBarNumber % phraseBars == 0);

        if (decision.phraseBoundary &&
            nextBarNumber >= std::max(1, settings.minBarsBeforeFill) &&
            !decision.breakLikely) {
            float base = 0.35f + settings.fillBias;
            if (decision.phraseState == PhraseState::Build)
                base += 0.35f;
            else if (decision.phraseState == PhraseState::Release)
                base += 0.15f;

            base += std::clamp(intensity - 0.5f, -0.25f, 0.25f);
            decision.fillStrength = std::clamp(base, 0.0f, 1.0f);
            decision.requestFill = decision.fillStrength >= 0.50f;
        }

        ++barIndex_;
        return decision;
    }

private:
    void pushIntensity(float value) noexcept {
        if (historyCount_ < static_cast<int>(intensityHistory_.size())) {
            intensityHistory_[static_cast<std::size_t>(historyCount_++)] = value;
        } else {
            for (std::size_t i = 1; i < intensityHistory_.size(); ++i)
                intensityHistory_[i - 1] = intensityHistory_[i];
            intensityHistory_.back() = value;
        }
    }

    [[nodiscard]] float trend() const noexcept {
        if (historyCount_ < 2) return 0.0f;
        const int latestIndex = historyCount_ - 1;
        const float latest = intensityHistory_[static_cast<std::size_t>(latestIndex)];
        const int baselineCount = std::min(2, latestIndex);
        if (baselineCount <= 0)
            return latest - intensityHistory_[0];

        float baseline = 0.0f;
        for (int i = 1; i <= baselineCount; ++i)
            baseline += intensityHistory_[static_cast<std::size_t>(latestIndex - i)];
        baseline /= static_cast<float>(baselineCount);
        return latest - baseline;
    }

    int barIndex_{0};
    int historyCount_{0};
    int lowActivityBars_{0};
    std::array<float, 8> intensityHistory_{};
};

} // namespace robodrummer
