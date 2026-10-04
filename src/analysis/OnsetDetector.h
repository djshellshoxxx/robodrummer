#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace robodrummer {

struct OnsetEvent {
    int sampleOffset{0};
    float strength{0.0f};
};

class AdaptiveOnsetDetector {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 0.0 && std::isfinite(sampleRate)) ? sampleRate : 48000.0;
        frameSize_ = std::max(32, static_cast<int>(std::llround(sampleRate_ * 0.005333333333333333)));
        refractorySamples_ = std::max(frameSize_, static_cast<int>(std::llround(sampleRate_ * 0.045)));
        reset();
    }

    void reset() noexcept {
        frameEnergy_ = 0.0;
        frameSamples_ = 0;
        previousLogEnergy_ = 0.0f;
        noveltyMean_ = 0.0f;
        noveltyDeviation_ = 0.01f;
        samplesSinceOnset_ = refractorySamples_;
    }

    [[nodiscard]] std::size_t processBlock(const float* input, int numSamples, OnsetEvent* out, std::size_t capacity) noexcept {
        if (input == nullptr || numSamples <= 0 || out == nullptr || capacity == 0) return 0;
        std::size_t count = 0;

        for (int i = 0; i < numSamples; ++i) {
            const float x = std::isfinite(input[i]) ? input[i] : 0.0f;
            frameEnergy_ += static_cast<double>(x) * static_cast<double>(x);
            ++frameSamples_;
            ++samplesSinceOnset_;

            if (frameSamples_ < frameSize_) continue;

            const float rms = static_cast<float>(std::sqrt(frameEnergy_ / static_cast<double>(frameSamples_)));
            const float logEnergy = std::log1p(rms * 24.0f);
            const float novelty = std::max(0.0f, logEnergy - previousLogEnergy_);
            const float threshold = std::max(0.055f, noveltyMean_ + 2.35f * noveltyDeviation_);

            if (novelty > threshold && samplesSinceOnset_ >= refractorySamples_ && count < capacity) {
                const float normalized = std::clamp((novelty - threshold) / std::max(0.05f, threshold * 2.0f), 0.0f, 1.0f);
                out[count++] = {i, normalized};
                samplesSinceOnset_ = 0;
            }

            const float error = novelty - noveltyMean_;
            noveltyMean_ += 0.035f * error;
            noveltyDeviation_ += 0.035f * (std::abs(error) - noveltyDeviation_);
            noveltyDeviation_ = std::max(0.005f, noveltyDeviation_);
            previousLogEnergy_ = 0.82f * previousLogEnergy_ + 0.18f * logEnergy;
            frameEnergy_ = 0.0;
            frameSamples_ = 0;
        }

        return count;
    }

    [[nodiscard]] int frameSize() const noexcept { return frameSize_; }

private:
    double sampleRate_{48000.0};
    int frameSize_{256};
    int refractorySamples_{2160};
    double frameEnergy_{0.0};
    int frameSamples_{0};
    float previousLogEnergy_{0.0f};
    float noveltyMean_{0.0f};
    float noveltyDeviation_{0.01f};
    int samplesSinceOnset_{2160};
};

} // namespace robodrummer
