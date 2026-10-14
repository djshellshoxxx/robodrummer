#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace robodrummer {

// The slower performance layer (SPEC 6, 17, 18). The first four members predate the extended feature set.
struct PerformanceState {
    float rms{0.0f};
    float peak{0.0f};
    float activity{0.0f};
    float intensity{0.0f};
    float bandLow{0.0f};            // normalised band energy (< ~250 Hz)
    float bandMid{0.0f};            // ~250 Hz - 2.5 kHz
    float bandHigh{0.0f};           // > ~2.5 kHz
    float spectralFlux{0.0f};       // how fast the spectrum is changing
    float transientDensity{0.0f};   // onsets per second, mapped so 8/s = 1
    float transientStrength{0.0f};  // smoothed mean onset strength
    float regularity{0.0f};         // 1 = metronomic inter-onset intervals
    float accentProbability{0.0f};  // likelihood the most recent onset was an accent
    float silenceProbability{1.0f}; // likelihood the guitar is currently silent
};

class PerformanceAnalyzer {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 0.0 && std::isfinite(sampleRate)) ? sampleRate : 48000.0;
        const double nyquist = 0.5 * sampleRate_;
        lowCoefficient_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * std::min(250.0, nyquist * 0.30) / sampleRate_));
        highCoefficient_ = static_cast<float>(1.0 - std::exp(-6.283185307179586 * std::min(2500.0, nyquist * 0.60) / sampleRate_));
        reset();
    }

    void reset() noexcept {
        smoothedRms_ = 0.0f;
        smoothedPeak_ = 0.0f;
        smoothedActivity_ = 0.0f;
        previousAbs_ = 0.0f;
        lowState_ = highState_ = 0.0f;
        smoothedBands_ = {};
        previousBandLog_ = {};
        smoothedFlux_ = 0.0f;
        smoothedSilence_ = 1.0f;
        elapsedSeconds_ = 0.0;
        onsetTimes_.fill(-1.0e9);
        onsetStrengths_.fill(0.0f);
        onsetHead_ = 0;
        onsetCount_ = 0;
        strengthMean_ = 0.0f;
        accent_ = 0.0f;
        state_ = {};
    }

    // Registers an onset found by the tempo analyser, so density, regularity and accents come from the same events.
    void noteOnset(double seconds, float strength) noexcept {
        if (!std::isfinite(seconds)) return;
        strength = std::clamp(std::isfinite(strength) ? strength : 0.0f, 0.0f, 1.0f);
        onsetTimes_[onsetHead_] = seconds;
        onsetStrengths_[onsetHead_] = strength;
        onsetHead_ = (onsetHead_ + 1) % onsetTimes_.size();
        onsetCount_ = std::min(onsetCount_ + 1, onsetTimes_.size());
        strengthMean_ += (strength - strengthMean_) * 0.2f;
        // An accent stands out against the recent average level of onsets.
        const float reference = std::max(0.08f, meanRecentStrength());
        accent_ = std::clamp((strength / reference - 1.0f) / 0.8f + 0.25f, 0.0f, 1.0f);
    }

    void processBlock(const float* input, int numSamples) noexcept {
        if (input == nullptr || numSamples <= 0) return;

        double energy = 0.0;
        float peak = 0.0f;
        double positiveMotion = 0.0;
        std::array<double, 3> bandEnergy{};
        for (int i = 0; i < numSamples; ++i) {
            const float x = std::isfinite(input[i]) ? input[i] : 0.0f;
            const float a = std::abs(x);
            energy += static_cast<double>(x) * static_cast<double>(x);
            peak = std::max(peak, a);
            positiveMotion += std::max(0.0f, a - previousAbs_);
            previousAbs_ = a;
            lowState_ += lowCoefficient_ * (x - lowState_);
            highState_ += highCoefficient_ * (x - highState_);
            const float mid = highState_ - lowState_;
            const float high = x - highState_;
            bandEnergy[0] += static_cast<double>(lowState_) * lowState_;
            bandEnergy[1] += static_cast<double>(mid) * mid;
            bandEnergy[2] += static_cast<double>(high) * high;
        }

        const float rms = static_cast<float>(std::sqrt(energy / static_cast<double>(numSamples)));
        const float motionPerSample = static_cast<float>(positiveMotion / static_cast<double>(numSamples));
        const float blockSeconds = static_cast<float>(static_cast<double>(numSamples) / sampleRate_);
        elapsedSeconds_ += static_cast<double>(blockSeconds);
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

        // Band energies and spectral flux (positive change of the log band energies).
        float flux = 0.0f;
        for (std::size_t band = 0; band < 3; ++band) {
            const float bandRms = static_cast<float>(std::sqrt(bandEnergy[band] / static_cast<double>(numSamples)));
            const float bandLog = std::log1p(bandRms * 24.0f);
            flux += std::max(0.0f, bandLog - previousBandLog_[band]);
            previousBandLog_[band] = 0.7f * previousBandLog_[band] + 0.3f * bandLog;
            smoothedBands_[band] = smooth(smoothedBands_[band], std::clamp(std::sqrt(bandRms) * 1.35f, 0.0f, 1.0f));
        }
        smoothedFlux_ = smooth(smoothedFlux_, std::clamp(flux * 1.5f, 0.0f, 1.0f));

        const float rmsDb = rms > 1.0e-6f ? 20.0f * std::log10(rms) : -120.0f;
        smoothedSilence_ = smooth(smoothedSilence_, std::clamp((-38.0f - rmsDb) / 14.0f + 0.5f, 0.0f, 1.0f));

        // Onset-derived features over the last few seconds.
        const double now = elapsedSeconds_;
        int recentOnsets = 0;
        for (std::size_t i = 0; i < onsetCount_; ++i) recentOnsets += (now - onsetTimes_[i]) <= 4.0 ? 1 : 0;
        const float density = std::clamp(static_cast<float>(recentOnsets) / 4.0f / 8.0f, 0.0f, 1.0f);

        const float loudness = std::clamp(std::sqrt(std::max(0.0f, smoothedRms_)) * 1.35f, 0.0f, 1.0f);
        const float spectralDensity = std::clamp(smoothedBands_[1] * 0.5f + smoothedBands_[2] * 0.8f, 0.0f, 1.0f);
        // SPEC 18: intensity is more than loudness - it also reflects playing density, attack strength and spectrum.
        const float intensity = std::clamp(loudness * 0.58f + smoothedActivity_ * 0.20f + density * 0.10f +
                                           std::clamp(strengthMean_, 0.0f, 1.0f) * 0.05f + spectralDensity * 0.07f, 0.0f, 1.0f);

        state_.rms = smoothedRms_;
        state_.peak = smoothedPeak_;
        state_.activity = smoothedActivity_;
        state_.intensity = intensity;
        state_.bandLow = smoothedBands_[0];
        state_.bandMid = smoothedBands_[1];
        state_.bandHigh = smoothedBands_[2];
        state_.spectralFlux = smoothedFlux_;
        state_.transientDensity = density;
        state_.transientStrength = std::clamp(strengthMean_, 0.0f, 1.0f);
        state_.regularity = computeRegularity();
        state_.accentProbability = accent_;
        state_.silenceProbability = smoothedSilence_;
    }

    [[nodiscard]] PerformanceState state() const noexcept { return state_; }

private:
    [[nodiscard]] float meanRecentStrength() const noexcept {
        const std::size_t n = std::min<std::size_t>(onsetCount_, 8);
        if (n == 0) return 0.0f;
        float sum = 0.0f;
        for (std::size_t i = 0; i < n; ++i) sum += onsetStrengths_[(onsetHead_ + onsetTimes_.size() - 1 - i) % onsetTimes_.size()];
        return sum / static_cast<float>(n);
    }

    // 1 - coefficient of variation of the recent inter-onset intervals (restricted to a plausible range).
    [[nodiscard]] float computeRegularity() const noexcept {
        const std::size_t n = std::min<std::size_t>(onsetCount_, 9);
        if (n < 5) return 0.0f;
        std::array<double, 8> intervals{};
        std::size_t count = 0;
        for (std::size_t i = 0; i + 1 < n; ++i) {
            const double later = onsetTimes_[(onsetHead_ + onsetTimes_.size() - 1 - i) % onsetTimes_.size()];
            const double earlier = onsetTimes_[(onsetHead_ + onsetTimes_.size() - 2 - i) % onsetTimes_.size()];
            const double dt = later - earlier;
            if (dt > 0.08 && dt < 2.5) intervals[count++] = dt;
        }
        if (count < 3) return 0.0f;
        double mean = 0.0;
        for (std::size_t i = 0; i < count; ++i) mean += intervals[i];
        mean /= static_cast<double>(count);
        double variance = 0.0;
        for (std::size_t i = 0; i < count; ++i) variance += (intervals[i] - mean) * (intervals[i] - mean);
        const double cv = std::sqrt(variance / static_cast<double>(count)) / std::max(1.0e-6, mean);
        return static_cast<float>(std::clamp(1.0 - cv * 2.5, 0.0, 1.0));
    }

    double sampleRate_{48000.0};
    float lowCoefficient_{0.03f};
    float highCoefficient_{0.3f};
    float smoothedRms_{0.0f};
    float smoothedPeak_{0.0f};
    float smoothedActivity_{0.0f};
    float previousAbs_{0.0f};
    float lowState_{0.0f};
    float highState_{0.0f};
    std::array<float, 3> smoothedBands_{};
    std::array<float, 3> previousBandLog_{};
    float smoothedFlux_{0.0f};
    float smoothedSilence_{1.0f};
    double elapsedSeconds_{0.0};
    std::array<double, 24> onsetTimes_{};
    std::array<float, 24> onsetStrengths_{};
    std::size_t onsetHead_{0};
    std::size_t onsetCount_{0};
    float strengthMean_{0.0f};
    float accent_{0.0f};
    PerformanceState state_{};
};

} // namespace robodrummer
