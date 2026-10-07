#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace robodrummer {

struct OnsetEvent {
    int sampleOffset{0};
    float strength{0.0f};
    float probability{0.0f};
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
        frameDerivativeEnergy_ = 0.0;
        frameSamples_ = 0;
        framePeakMagnitude_ = 0.0f;
        framePeakIndex_ = 0;
        previousSample_ = 0.0f;
        previousLogEnergy_ = 0.0f;
        previousDerivativeRms_ = 0.0f;
        noveltyMean_ = 0.0f;
        noveltyDeviation_ = 0.01f;
        noiseFloor_ = 0.005f;
        samplesSinceOnset_ = refractorySamples_;
    }

    [[nodiscard]] std::size_t processBlock(const float* input,
                                           int numSamples,
                                           OnsetEvent* out,
                                           std::size_t capacity) noexcept {
        if (input == nullptr || numSamples <= 0 || out == nullptr || capacity == 0) return 0;
        std::size_t count = 0;

        for (int i = 0; i < numSamples; ++i) {
            const float x = std::isfinite(input[i]) ? input[i] : 0.0f;
            frameEnergy_ += static_cast<double>(x) * static_cast<double>(x);
            const float derivative = x - previousSample_;
            frameDerivativeEnergy_ += static_cast<double>(derivative) * static_cast<double>(derivative);
            previousSample_ = x;

            const float magnitude = std::abs(x);
            if (magnitude > framePeakMagnitude_) {
                framePeakMagnitude_ = magnitude;
                framePeakIndex_ = frameSamples_;
            }
            ++frameSamples_;
            ++samplesSinceOnset_;

            if (frameSamples_ < frameSize_) continue;

            const float invFrame = 1.0f / static_cast<float>(frameSamples_);
            const float rms = static_cast<float>(std::sqrt(frameEnergy_ * invFrame));
            const float derivativeRms = static_cast<float>(std::sqrt(frameDerivativeEnergy_ * invFrame));
            const float logEnergy = std::log1p(rms * 24.0f);

            const float energyRise = std::max(0.0f, logEnergy - previousLogEnergy_);
            const float fluxRise = std::max(0.0f, derivativeRms - previousDerivativeRms_) * 6.0f;
            const float crest = rms > 1.0e-6f
                ? std::clamp(framePeakMagnitude_ / (rms * 5.0f), 0.0f, 1.0f)
                : 0.0f;
            const float transientNovelty = std::max(0.0f, crest - 0.18f) * std::clamp(derivativeRms * 3.0f, 0.0f, 1.0f);

            // Weighted multi-feature novelty: envelope rise catches strums/chords,
            // derivative flux catches sharp pick/palm-muted attacks, and crest-weighted
            // novelty reinforces isolated transients without turning stationary hiss
            // into a beat stream.
            const float novelty =
                energyRise * 0.55f +
                fluxRise * 0.30f +
                transientNovelty * 0.15f;
            const float threshold = std::max(0.045f, noveltyMean_ + 2.20f * noveltyDeviation_);

            const float signalGate = noiseFloor_ * 2.2f + 0.008f;
            const bool aboveNoise = rms >= signalGate;
            if (aboveNoise &&
                novelty > threshold &&
                samplesSinceOnset_ >= refractorySamples_ &&
                count < capacity) {
                const float probability = std::clamp(
                    (novelty - threshold) / std::max(0.035f, threshold * 1.8f),
                    0.0f,
                    1.0f);
                const float amplitudeEvidence = std::clamp(
                    (framePeakMagnitude_ - signalGate) / std::max(0.08f, 1.0f - signalGate),
                    0.0f,
                    1.0f);
                const float strength = std::clamp(
                    probability * 0.62f + amplitudeEvidence * 0.38f,
                    0.0f,
                    1.0f);
                const int peakOffset = i - (frameSize_ - 1 - framePeakIndex_);
                out[count++] = {std::max(0, peakOffset), strength, probability};
                samplesSinceOnset_ = 0;
            }

            const float error = novelty - noveltyMean_;
            noveltyMean_ += 0.035f * error;
            noveltyDeviation_ += 0.035f * (std::abs(error) - noveltyDeviation_);
            noveltyDeviation_ = std::max(0.004f, noveltyDeviation_);

            // Only quiet frames update the noise-floor estimate quickly. Louder
            // material can raise it slowly, preventing sustained chords from
            // teaching the detector that musical signal is "noise".
            const float noiseRate = rms < noiseFloor_ * 2.0f ? 0.025f : 0.0015f;
            noiseFloor_ += (rms - noiseFloor_) * noiseRate;
            noiseFloor_ = std::clamp(noiseFloor_, 0.0005f, 0.20f);

            previousLogEnergy_ = 0.80f * previousLogEnergy_ + 0.20f * logEnergy;
            previousDerivativeRms_ = 0.78f * previousDerivativeRms_ + 0.22f * derivativeRms;
            frameEnergy_ = 0.0;
            frameDerivativeEnergy_ = 0.0;
            frameSamples_ = 0;
            framePeakMagnitude_ = 0.0f;
            framePeakIndex_ = 0;
        }

        return count;
    }

    [[nodiscard]] int frameSize() const noexcept { return frameSize_; }
    [[nodiscard]] float noiseFloor() const noexcept { return noiseFloor_; }

private:
    double sampleRate_{48000.0};
    int frameSize_{256};
    int refractorySamples_{2160};
    double frameEnergy_{0.0};
    double frameDerivativeEnergy_{0.0};
    int frameSamples_{0};
    float framePeakMagnitude_{0.0f};
    int framePeakIndex_{0};
    float previousSample_{0.0f};
    float previousLogEnergy_{0.0f};
    float previousDerivativeRms_{0.0f};
    float noveltyMean_{0.0f};
    float noveltyDeviation_{0.01f};
    float noiseFloor_{0.005f};
    int samplesSinceOnset_{2160};
};

} // namespace robodrummer
