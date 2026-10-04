#pragma once
#include <algorithm>
#include <cmath>

namespace robodrummer {

struct PerformanceState {
    float rms{0.0f};
    float peak{0.0f};
    float activity{0.0f};
    float intensity{0.0f};
};

class PerformanceAnalyzer {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 0.0 && std::isfinite(sampleRate)) ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept {
        smoothedRms_ = 0.0f;
        smoothedPeak_ = 0.0f;
        smoothedActivity_ = 0.0f;
        previousAbs_ = 0.0f;
        state_ = {};
    }

    void processBlock(const float* input, int numSamples) noexcept {
        if (input == nullptr || numSamples <= 0) return;

        double energy = 0.0;
        float peak = 0.0f;
        double positiveMotion = 0.0;
        for (int i = 0; i < numSamples; ++i) {
            const float x = std::isfinite(input[i]) ? input[i] : 0.0f;
            const float a = std::abs(x);
            energy += static_cast<double>(x) * static_cast<double>(x);
            peak = std::max(peak, a);
            positiveMotion += std::max(0.0f, a - previousAbs_);
            previousAbs_ = a;
        }

        const float rms = static_cast<float>(std::sqrt(energy / static_cast<double>(numSamples)));
        const float motionPerSample = static_cast<float>(positiveMotion / static_cast<double>(numSamples));
        const float blockSeconds = static_cast<float>(static_cast<double>(numSamples) / sampleRate_);
        const float activityRaw = std::clamp(motionPerSample * 140.0f + rms * 1.8f, 0.0f, 1.0f);

        const float attack = std::clamp(blockSeconds * 18.0f, 0.02f, 1.0f);
        const float release = std::clamp(blockSeconds * 5.0f, 0.01f, 1.0f);
        const auto smooth = [attack, release](float current, float target) {
            const float coefficient = target > current ? attack : release;
            return current + (target - current) * coefficient;
        };

        smoothedRms_ = smooth(smoothedRms_, rms);
        smoothedPeak_ = smooth(smoothedPeak_, peak);
        smoothedActivity_ = smooth(smoothedActivity_, activityRaw);

        const float loudness = std::clamp(std::sqrt(std::max(0.0f, smoothedRms_)) * 1.35f, 0.0f, 1.0f);
        const float intensity = std::clamp(loudness * 0.72f + smoothedActivity_ * 0.28f, 0.0f, 1.0f);
        state_ = {smoothedRms_, smoothedPeak_, smoothedActivity_, intensity};
    }

    [[nodiscard]] PerformanceState state() const noexcept { return state_; }

private:
    double sampleRate_{48000.0};
    float smoothedRms_{0.0f};
    float smoothedPeak_{0.0f};
    float smoothedActivity_{0.0f};
    float previousAbs_{0.0f};
    PerformanceState state_{};
};

} // namespace robodrummer
