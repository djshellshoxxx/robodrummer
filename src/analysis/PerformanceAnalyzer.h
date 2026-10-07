#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace robodrummer {

struct PerformanceState {
    float rms{0.0f};
    float peak{0.0f};
    float activity{0.0f};
    float intensity{0.0f};
    float spectralFlux{0.0f};
    float lowBandEnergy{0.0f};
    float midBandEnergy{0.0f};
    float highBandEnergy{0.0f};
    float transientDensity{0.0f};
    float rhythmicRegularity{0.0f};
    float silenceProbability{1.0f};
    float accentProbability{0.0f};
    float activityConfidence{0.0f};
    float phraseBoundaryProbability{0.0f};
    float buildReleaseTendency{0.0f};
};

class PerformanceAnalyzer {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 0.0 && std::isfinite(sampleRate)) ? sampleRate : 48000.0;
        lowCoeff_ = onePoleCoefficient(220.0);
        midCoeff_ = onePoleCoefficient(2200.0);
        reset();
    }

    void reset() noexcept {
        smoothedRms_ = 0.0f;
        smoothedPeak_ = 0.0f;
        smoothedActivity_ = 0.0f;
        previousAbs_ = 0.0f;
        lowState_ = 0.0f;
        midState_ = 0.0f;
        previousLowEnergy_ = 0.0f;
        previousMidEnergy_ = 0.0f;
        previousHighEnergy_ = 0.0f;
        derivativeMean_ = 0.002f;
        derivativeDeviation_ = 0.002f;
        sampleCursor_ = 0;
        lastTransientSample_ = -1;
        intervalMeanSamples_ = 0.0;
        intervalDeviationSamples_ = 0.0;
        intervalEvidence_ = 0;
        slowIntensity_ = 0.0f;
        previousSlowIntensity_ = 0.0f;
        activityHistory_ = 0.0f;
        state_ = {};
    }

    void processBlock(const float* input, int numSamples) noexcept {
        if (input == nullptr || numSamples <= 0) return;

        double energy = 0.0;
        double lowEnergy = 0.0;
        double midEnergy = 0.0;
        double highEnergy = 0.0;
        float peak = 0.0f;
        double positiveMotion = 0.0;
        int transientCount = 0;
        float strongestTransient = 0.0f;

        const float derivativeThreshold = derivativeMean_ + 2.5f * derivativeDeviation_;
        const long long refractorySamples = std::max<long long>(1, static_cast<long long>(std::llround(sampleRate_ * 0.030)));

        for (int i = 0; i < numSamples; ++i) {
            const float x = std::isfinite(input[i]) ? input[i] : 0.0f;
            const float a = std::abs(x);
            energy += static_cast<double>(x) * static_cast<double>(x);
            peak = std::max(peak, a);

            const float derivative = std::max(0.0f, a - previousAbs_);
            positiveMotion += derivative;
            previousAbs_ = a;

            const float low = lowState_ + lowCoeff_ * (x - lowState_);
            lowState_ = low;
            const float lowMid = midState_ + midCoeff_ * (x - midState_);
            midState_ = lowMid;
            const float mid = lowMid - low;
            const float high = x - lowMid;
            lowEnergy += static_cast<double>(low) * low;
            midEnergy += static_cast<double>(mid) * mid;
            highEnergy += static_cast<double>(high) * high;

            const long long absoluteSample = sampleCursor_ + i;
            if (derivative > std::max(0.006f, derivativeThreshold) &&
                (lastTransientSample_ < 0 || absoluteSample - lastTransientSample_ >= refractorySamples)) {
                ++transientCount;
                strongestTransient = std::max(strongestTransient, derivative);
                if (lastTransientSample_ >= 0) {
                    const double interval = static_cast<double>(absoluteSample - lastTransientSample_);
                    if (intervalMeanSamples_ <= 0.0) {
                        intervalMeanSamples_ = interval;
                    } else {
                        const double error = interval - intervalMeanSamples_;
                        intervalMeanSamples_ += error * 0.12;
                        intervalDeviationSamples_ += (std::abs(error) - intervalDeviationSamples_) * 0.12;
                    }
                    intervalEvidence_ = std::min(intervalEvidence_ + 1, 64);
                }
                lastTransientSample_ = absoluteSample;
            }

            const float derivativeError = derivative - derivativeMean_;
            derivativeMean_ += 0.0008f * derivativeError;
            derivativeDeviation_ += 0.0008f * (std::abs(derivativeError) - derivativeDeviation_);
            derivativeDeviation_ = std::max(0.0005f, derivativeDeviation_);
        }

        sampleCursor_ += numSamples;

        const float invSamples = 1.0f / static_cast<float>(numSamples);
        const float rms = static_cast<float>(std::sqrt(energy * invSamples));
        const float lowRms = static_cast<float>(std::sqrt(lowEnergy * invSamples));
        const float midRms = static_cast<float>(std::sqrt(midEnergy * invSamples));
        const float highRms = static_cast<float>(std::sqrt(highEnergy * invSamples));
        const float motionPerSample = static_cast<float>(positiveMotion * invSamples);
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

        const float fluxMagnitude =
            std::max(0.0f, lowRms - previousLowEnergy_) +
            std::max(0.0f, midRms - previousMidEnergy_) +
            std::max(0.0f, highRms - previousHighEnergy_);
        previousLowEnergy_ = lowRms;
        previousMidEnergy_ = midRms;
        previousHighEnergy_ = highRms;
        const float spectralFlux = std::clamp(fluxMagnitude * 3.5f, 0.0f, 1.0f);

        const float eventsPerSecond = blockSeconds > 0.0f
            ? static_cast<float>(transientCount) / blockSeconds
            : 0.0f;
        const float transientDensity = std::clamp(eventsPerSecond / 12.0f, 0.0f, 1.0f);

        float rhythmicRegularity = 0.0f;
        if (intervalEvidence_ >= 3 && intervalMeanSamples_ > 1.0) {
            const double coefficientOfVariation = intervalDeviationSamples_ / intervalMeanSamples_;
            const float evidence = std::clamp(static_cast<float>(intervalEvidence_) / 10.0f, 0.0f, 1.0f);
            rhythmicRegularity = std::clamp(
                (1.0f - static_cast<float>(coefficientOfVariation) * 2.5f) * evidence,
                0.0f,
                1.0f);
        }

        const float loudness = std::clamp(std::sqrt(std::max(0.0f, smoothedRms_)) * 1.35f, 0.0f, 1.0f);
        const float spectralDensity = std::clamp((lowRms + midRms + highRms) * 1.5f, 0.0f, 1.0f);
        const float intensity = std::clamp(
            loudness * 0.48f +
            smoothedActivity_ * 0.24f +
            transientDensity * 0.16f +
            spectralDensity * 0.12f,
            0.0f,
            1.0f);

        const float silenceProbability = std::clamp(
            1.0f - (loudness * 0.65f + smoothedActivity_ * 0.35f),
            0.0f,
            1.0f);
        const float crest = rms > 1.0e-5f ? std::clamp(peak / (rms * 5.0f), 0.0f, 1.0f) : 0.0f;
        const float transientAccent = std::clamp(strongestTransient * 3.0f, 0.0f, 1.0f);
        const float accentProbability = std::clamp(crest * 0.45f + transientAccent * 0.55f, 0.0f, 1.0f);
        const float activityConfidence = std::clamp(
            std::abs(smoothedActivity_ - 0.5f) * 1.6f + std::max(loudness, silenceProbability) * 0.2f,
            0.0f,
            1.0f);

        previousSlowIntensity_ = slowIntensity_;
        const float slowCoeff = std::clamp(blockSeconds * 1.1f, 0.002f, 0.12f);
        slowIntensity_ += (intensity - slowIntensity_) * slowCoeff;
        const float buildRelease = std::clamp(
            (slowIntensity_ - previousSlowIntensity_) / std::max(0.0005f, slowCoeff) * 2.2f,
            -1.0f,
            1.0f);

        const float activityDelta = std::abs(smoothedActivity_ - activityHistory_);
        activityHistory_ += (smoothedActivity_ - activityHistory_) * std::clamp(blockSeconds * 0.7f, 0.002f, 0.10f);
        const float phraseBoundary = std::clamp(
            activityDelta * 1.8f + spectralFlux * 0.20f + silenceProbability * (1.0f - activityHistory_) * 0.35f,
            0.0f,
            1.0f);

        state_ = {
            smoothedRms_,
            smoothedPeak_,
            smoothedActivity_,
            intensity,
            spectralFlux,
            lowRms,
            midRms,
            highRms,
            transientDensity,
            rhythmicRegularity,
            silenceProbability,
            accentProbability,
            activityConfidence,
            phraseBoundary,
            buildRelease
        };
    }

    [[nodiscard]] PerformanceState state() const noexcept { return state_; }

private:
    [[nodiscard]] float onePoleCoefficient(double cutoffHz) const noexcept {
        constexpr double twoPi = 6.28318530717958647692;
        const double normalized = std::clamp(cutoffHz, 1.0, sampleRate_ * 0.45);
        return static_cast<float>(1.0 - std::exp(-twoPi * normalized / sampleRate_));
    }

    double sampleRate_{48000.0};
    float lowCoeff_{0.03f};
    float midCoeff_{0.25f};
    float smoothedRms_{0.0f};
    float smoothedPeak_{0.0f};
    float smoothedActivity_{0.0f};
    float previousAbs_{0.0f};
    float lowState_{0.0f};
    float midState_{0.0f};
    float previousLowEnergy_{0.0f};
    float previousMidEnergy_{0.0f};
    float previousHighEnergy_{0.0f};
    float derivativeMean_{0.002f};
    float derivativeDeviation_{0.002f};
    long long sampleCursor_{0};
    long long lastTransientSample_{-1};
    double intervalMeanSamples_{0.0};
    double intervalDeviationSamples_{0.0};
    int intervalEvidence_{0};
    float slowIntensity_{0.0f};
    float previousSlowIntensity_{0.0f};
    float activityHistory_{0.0f};
    PerformanceState state_{};
};

} // namespace robodrummer
