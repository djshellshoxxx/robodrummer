#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace robodrummer {

struct SessionBarObservation {
    double tempoBpm{120.0};
    float intensity{0.5f};
    float activity{0.5f};
    bool phraseBoundary{false};
    bool manualFillRequested{false};
    bool fillOccurred{false};
};

struct SessionMemorySnapshot {
    int barCount{0};
    double averageTempoBpm{0.0};
    double averageTempoDriftPerBar{0.0};
    float averageIntensity{0.0f};
    float intensityVolatility{0.0f};
    float averagePhraseBars{0.0f};
    float averagePauseBars{0.0f};
    int longestPauseBars{0};
    float manualFillRatePerBar{0.0f};
    float fillRatePerBar{0.0f};
};

struct SessionMemoryRecommendations {
    float confidence{0.0f};
    int suggestedPhraseBars{0};
    float fillBiasAdjustment{0.0f};
    float dynamicSensitivity{1.0f};
    float tempoResponsiveness{1.0f};
};

class SessionMemory {
public:
    void reset() noexcept {
        barCount_ = 0;
        tempoSum_ = 0.0;
        tempoDriftSum_ = 0.0;
        tempoDriftSamples_ = 0;
        previousTempo_ = 0.0;
        hasPreviousTempo_ = false;

        intensitySum_ = 0.0f;
        intensityMotionSum_ = 0.0f;
        intensityMotionSamples_ = 0;
        previousIntensity_ = 0.0f;
        hasPreviousIntensity_ = false;

        phraseLengthSum_ = 0;
        phraseSamples_ = 0;
        lastPhraseBoundaryBar_ = 0;

        currentPauseBars_ = 0;
        pauseLengthSum_ = 0;
        pauseSamples_ = 0;
        longestPauseBars_ = 0;

        manualFillCount_ = 0;
        fillCount_ = 0;
    }

    void observeBar(const SessionBarObservation& input) noexcept {
        const double tempo = std::clamp(std::isfinite(input.tempoBpm) ? input.tempoBpm : 120.0, 20.0, 400.0);
        const float intensity = std::clamp(input.intensity, 0.0f, 1.0f);
        const float activity = std::clamp(input.activity, 0.0f, 1.0f);

        ++barCount_;
        tempoSum_ += tempo;
        intensitySum_ += intensity;

        if (hasPreviousTempo_) {
            tempoDriftSum_ += std::abs(tempo - previousTempo_);
            ++tempoDriftSamples_;
        }
        previousTempo_ = tempo;
        hasPreviousTempo_ = true;

        if (hasPreviousIntensity_) {
            intensityMotionSum_ += std::abs(intensity - previousIntensity_);
            ++intensityMotionSamples_;
        }
        previousIntensity_ = intensity;
        hasPreviousIntensity_ = true;

        if (input.phraseBoundary) {
            const int phraseLength = barCount_ - lastPhraseBoundaryBar_;
            if (phraseLength > 0) {
                phraseLengthSum_ += phraseLength;
                ++phraseSamples_;
            }
            lastPhraseBoundaryBar_ = barCount_;
        }

        if (activity <= 0.08f) {
            ++currentPauseBars_;
            longestPauseBars_ = std::max(longestPauseBars_, currentPauseBars_);
        } else if (currentPauseBars_ > 0) {
            pauseLengthSum_ += currentPauseBars_;
            ++pauseSamples_;
            currentPauseBars_ = 0;
        }

        if (input.manualFillRequested)
            ++manualFillCount_;
        if (input.fillOccurred)
            ++fillCount_;
    }

    [[nodiscard]] SessionMemorySnapshot snapshot() const noexcept {
        SessionMemorySnapshot result;
        result.barCount = barCount_;
        if (barCount_ <= 0)
            return result;

        result.averageTempoBpm = tempoSum_ / static_cast<double>(barCount_);
        result.averageTempoDriftPerBar = tempoDriftSamples_ > 0
            ? tempoDriftSum_ / static_cast<double>(tempoDriftSamples_)
            : 0.0;
        result.averageIntensity = intensitySum_ / static_cast<float>(barCount_);
        result.intensityVolatility = intensityMotionSamples_ > 0
            ? intensityMotionSum_ / static_cast<float>(intensityMotionSamples_)
            : 0.0f;
        result.averagePhraseBars = phraseSamples_ > 0
            ? static_cast<float>(phraseLengthSum_) / static_cast<float>(phraseSamples_)
            : 0.0f;

        const int pauseSum = pauseLengthSum_ + (currentPauseBars_ > 0 ? currentPauseBars_ : 0);
        const int pauseCount = pauseSamples_ + (currentPauseBars_ > 0 ? 1 : 0);
        result.averagePauseBars = pauseCount > 0
            ? static_cast<float>(pauseSum) / static_cast<float>(pauseCount)
            : 0.0f;
        result.longestPauseBars = longestPauseBars_;
        result.manualFillRatePerBar = static_cast<float>(manualFillCount_) / static_cast<float>(barCount_);
        result.fillRatePerBar = static_cast<float>(fillCount_) / static_cast<float>(barCount_);
        return result;
    }

    [[nodiscard]] SessionMemoryRecommendations recommendations() const noexcept {
        const auto stats = snapshot();
        SessionMemoryRecommendations result;
        if (stats.barCount <= 0)
            return result;

        const float barEvidence = std::clamp(static_cast<float>(stats.barCount) / 16.0f, 0.0f, 1.0f);
        const float phraseEvidence = std::clamp(static_cast<float>(phraseSamples_) / 4.0f, 0.0f, 1.0f);
        result.confidence = std::clamp(barEvidence * 0.70f + phraseEvidence * 0.30f, 0.0f, 1.0f);

        if (phraseSamples_ >= 2 && stats.averagePhraseBars > 0.0f)
            result.suggestedPhraseBars = std::clamp(static_cast<int>(std::lround(stats.averagePhraseBars)), 2, 16);

        // Manual fills are the clearest direct signal of the player's preferred fill frequency.
        // Around one manual request per 20 bars is neutral; more raises the bias gently.
        result.fillBiasAdjustment = std::clamp((stats.manualFillRatePerBar - 0.05f) * 1.4f, -0.08f, 0.18f);

        // Dynamic response adapts only modestly. A highly variable player can receive a little
        // more response; a very steady player gets slightly more restraint.
        result.dynamicSensitivity = std::clamp(0.85f + stats.intensityVolatility * 1.5f, 0.75f, 1.35f);

        // Tempo drift is interpreted as a preference for looser push/pull following, never as
        // permission for abrupt phase changes.
        result.tempoResponsiveness = std::clamp(
            0.85f + static_cast<float>(stats.averageTempoDriftPerBar / 4.0), 0.80f, 1.25f);

        return result;
    }

private:
    int barCount_{0};

    double tempoSum_{0.0};
    double tempoDriftSum_{0.0};
    int tempoDriftSamples_{0};
    double previousTempo_{0.0};
    bool hasPreviousTempo_{false};

    float intensitySum_{0.0f};
    float intensityMotionSum_{0.0f};
    int intensityMotionSamples_{0};
    float previousIntensity_{0.0f};
    bool hasPreviousIntensity_{false};

    int phraseLengthSum_{0};
    int phraseSamples_{0};
    int lastPhraseBoundaryBar_{0};

    int currentPauseBars_{0};
    int pauseLengthSum_{0};
    int pauseSamples_{0};
    int longestPauseBars_{0};

    int manualFillCount_{0};
    int fillCount_{0};
};

} // namespace robodrummer
