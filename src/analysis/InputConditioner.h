#pragma once
#include <algorithm>
#include <cmath>

namespace robodrummer {

enum class AnalysisInputMode {
    Mono = 0,
    StereoSum,
    LeftOnly,
    RightOnly
};

struct InputConditionerSettings {
    AnalysisInputMode mode{AnalysisInputMode::Mono};
    float gain{1.0f};
    float gateThreshold{0.0f};
    float highPassHz{35.0f};
};

class InputConditioner {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 0.0 && std::isfinite(sampleRate)) ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept {
        previousInput_ = 0.0f;
        previousHighPass_ = 0.0f;
        gateEnvelope_ = 0.0f;
    }

    void process(const float* left,
                 const float* right,
                 float* output,
                 int numSamples,
                 const InputConditionerSettings& settings) noexcept {
        if (output == nullptr || numSamples <= 0)
            return;

        const float gain = std::clamp(
            std::isfinite(settings.gain) ? settings.gain : 1.0f,
            0.0f,
            8.0f);
        const float gateThreshold = std::clamp(
            std::isfinite(settings.gateThreshold) ? settings.gateThreshold : 0.0f,
            0.0f,
            1.0f);
        const float cutoff = std::clamp(
            std::isfinite(settings.highPassHz) ? settings.highPassHz : 0.0f,
            0.0f,
            static_cast<float>(sampleRate_ * 0.45));
        const float highPassAlpha = cutoff > 0.0f
            ? static_cast<float>(std::exp(-6.28318530717958647692 * cutoff / sampleRate_))
            : 0.0f;

        for (int i = 0; i < numSamples; ++i) {
            const float l = sanitize(left != nullptr ? left[i] : 0.0f);
            const float r = sanitize(right != nullptr ? right[i] : l);

            float x = 0.0f;
            switch (settings.mode) {
                case AnalysisInputMode::Mono:
                case AnalysisInputMode::LeftOnly:
                    x = l;
                    break;
                case AnalysisInputMode::StereoSum:
                    x = right != nullptr ? 0.5f * (l + r) : l;
                    break;
                case AnalysisInputMode::RightOnly:
                    x = right != nullptr ? r : l;
                    break;
            }

            x = std::clamp(x * gain, -8.0f, 8.0f);

            if (cutoff > 0.0f) {
                const float hp = highPassAlpha * (previousHighPass_ + x - previousInput_);
                previousInput_ = x;
                previousHighPass_ = hp;
                x = hp;
            } else {
                previousInput_ = x;
                previousHighPass_ = x;
            }

            const float magnitude = std::abs(x);
            if (gateThreshold > 0.0f) {
                const float target = magnitude >= gateThreshold ? 1.0f : 0.0f;
                const float coefficient = target > gateEnvelope_ ? 0.35f : 0.035f;
                gateEnvelope_ += (target - gateEnvelope_) * coefficient;
                // Preserve intentional transients even while the soft gate is opening.
                const float transientFloor = target > 0.0f ? 0.92f : gateEnvelope_;
                x *= std::max(gateEnvelope_, transientFloor);
            } else {
                gateEnvelope_ = 1.0f;
            }

            output[i] = std::isfinite(x) ? x : 0.0f;
        }
    }

private:
    static float sanitize(float value) noexcept {
        return std::isfinite(value) ? value : 0.0f;
    }

    double sampleRate_{48000.0};
    float previousInput_{0.0f};
    float previousHighPass_{0.0f};
    float gateEnvelope_{0.0f};
};

} // namespace robodrummer
