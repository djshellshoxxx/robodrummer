#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace robodrummer {

struct OnsetEvent {
    int sampleOffset{0};
    float strength{0.0f};
    float probability{1.0f};  // how far the novelty rose above the adaptive threshold (0.5 = just crossed)
};

class AdaptiveOnsetDetector {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 0.0 && std::isfinite(sampleRate)) ? sampleRate : 48000.0;
        frameSize_ = std::max(32, static_cast<int>(std::llround(sampleRate_ * 0.005333333333333333)));
        refractorySamples_ = std::max(frameSize_, static_cast<int>(std::llround(sampleRate_ * 0.045)));
        // Accent reference decays with a ~10 s time constant so strength tracks the current playing level.
        noveltyPeakDecay_ = static_cast<float>(std::exp(-static_cast<double>(frameSize_) / (sampleRate_ * 10.0)));
        // Band split for the spectral-flux feature: ~250 Hz and ~2.5 kHz, kept inside the usable band at low rates.
        const double nyquist = 0.5 * sampleRate_;
        lowCoefficient_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * std::min(250.0, nyquist * 0.30) / sampleRate_));
        highCoefficient_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * std::min(2500.0, nyquist * 0.60) / sampleRate_));
        reset();
    }

    void reset() noexcept {
        frameEnergy_ = 0.0;
        frameSamples_ = 0;
        framePeakMagnitude_ = 0.0f;
        framePeakIndex_ = 0;
        previousLogEnergy_ = 0.0f;
        previousBandLog_ = {};
        bandEnergy_ = {};
        lowState_ = 0.0f;
        highState_ = 0.0f;
        noveltyMean_ = 0.0f;
        noveltyDeviation_ = 0.01f;
        noveltyPeak_ = 0.0f;
        samplesSinceOnset_ = refractorySamples_;
    }

    [[nodiscard]] std::size_t processBlock(const float* input, int numSamples, OnsetEvent* out, std::size_t capacity) noexcept {
        if (input == nullptr || numSamples <= 0 || out == nullptr || capacity == 0) return 0;
        std::size_t count = 0;

        for (int i = 0; i < numSamples; ++i) {
            // Clamp to a sane full-scale range: a single huge finite sample would otherwise make the
            // log energy infinite and poison the novelty statistics permanently.
            const float x = std::isfinite(input[i]) ? std::clamp(input[i], -4.0f, 4.0f) : 0.0f;
            frameEnergy_ += static_cast<double>(x) * static_cast<double>(x);
            lowState_ += lowCoefficient_ * (x - lowState_);
            highState_ += highCoefficient_ * (x - highState_);
            const float lowBand = lowState_;
            const float midBand = highState_ - lowState_;
            const float highBand = x - highState_;
            bandEnergy_[0] += static_cast<double>(lowBand) * lowBand;
            bandEnergy_[1] += static_cast<double>(midBand) * midBand;
            bandEnergy_[2] += static_cast<double>(highBand) * highBand;
            const float magnitude = std::abs(x);
            if (magnitude > framePeakMagnitude_) {
                framePeakMagnitude_ = magnitude;
                framePeakIndex_ = frameSamples_;
            }
            ++frameSamples_;
            ++samplesSinceOnset_;

            if (frameSamples_ < frameSize_) continue;

            const float rms = static_cast<float>(std::sqrt(frameEnergy_ / static_cast<double>(frameSamples_)));
            const float logEnergy = std::log1p(rms * 24.0f);
            const float broadbandNovelty = std::max(0.0f, logEnergy - previousLogEnergy_);
            // Spectral-flux proxy: summed positive change of the three band energies. It catches chord changes and
            // pick attacks whose broadband level barely moves (sustained distortion).
            float flux = 0.0f;
            for (std::size_t band = 0; band < 3; ++band) {
                const float bandLog = std::log1p(static_cast<float>(std::sqrt(bandEnergy_[band] / static_cast<double>(frameSamples_))) * 24.0f);
                flux += std::max(0.0f, bandLog - previousBandLog_[band]);
                previousBandLog_[band] = 0.82f * previousBandLog_[band] + 0.18f * bandLog;
                bandEnergy_[band] = 0.0;
            }
            const float novelty = std::max(broadbandNovelty, 0.45f * flux);
            const float threshold = std::max(0.055f, noveltyMean_ + 2.35f * noveltyDeviation_);
            noveltyPeak_ *= noveltyPeakDecay_;

            if (novelty > threshold && samplesSinceOnset_ >= refractorySamples_ && count < capacity) {
                // Strength is relative to the recent strongest onset, so accents stay distinguishable at any
                // playing level instead of saturating at 1 (which hid downbeat accents from bar tracking).
                noveltyPeak_ = std::max(noveltyPeak_, novelty);
                // Novelty is a log-energy difference, which compresses accents; squaring restores contrast.
                const float ratio = std::clamp(novelty / std::max(noveltyPeak_, 1.0e-4f), 0.0f, 1.0f);
                const float normalized = ratio * ratio;
                const int peakOffset = i - (frameSize_ - 1 - framePeakIndex_);
                const float probability = std::clamp(0.5f + 0.5f * (novelty - threshold) / std::max(0.02f, threshold), 0.0f, 1.0f);
                out[count++] = {std::max(0, peakOffset), normalized, probability};
                samplesSinceOnset_ = 0;
            }

            const float error = novelty - noveltyMean_;
            noveltyMean_ += 0.035f * error;
            noveltyDeviation_ += 0.035f * (std::abs(error) - noveltyDeviation_);
            noveltyDeviation_ = std::max(0.005f, noveltyDeviation_);
            previousLogEnergy_ = 0.82f * previousLogEnergy_ + 0.18f * logEnergy;
            frameEnergy_ = 0.0;
            frameSamples_ = 0;
            framePeakMagnitude_ = 0.0f;
            framePeakIndex_ = 0;
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
    float framePeakMagnitude_{0.0f};
    int framePeakIndex_{0};
    float previousLogEnergy_{0.0f};
    std::array<float, 3> previousBandLog_{};
    std::array<double, 3> bandEnergy_{};
    float lowState_{0.0f};
    float highState_{0.0f};
    float lowCoefficient_{0.03f};
    float highCoefficient_{0.3f};
    float noveltyMean_{0.0f};
    float noveltyDeviation_{0.01f};
    float noveltyPeak_{0.0f};
    float noveltyPeakDecay_{0.9995f};
    int samplesSinceOnset_{2160};
};

} // namespace robodrummer
