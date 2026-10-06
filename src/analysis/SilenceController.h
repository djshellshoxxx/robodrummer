#pragma once
#include <algorithm>
#include <cmath>

namespace robodrummer {

enum class SilenceMode {
    KeepPlaying,
    ReduceIntensity,
    HoldGroove,
    FillDuringSilence,
    StopAfterBars,
    WaitForResume
};

struct SilenceSettings {
    SilenceMode mode{SilenceMode::KeepPlaying};
    float silenceThreshold{0.08f};
    int stopAfterBars{2};
};

struct SilenceDecision {
    int silentBars{0};
    float intensityMultiplier{1.0f};
    bool suppressAdaptiveChanges{false};
    bool requestFill{false};
    bool stopDrums{false};
    bool resumeDrums{false};
    bool waitingForResume{false};
    bool markResumeWithCrash{false};
};

class SilenceController {
public:
    void reset() noexcept {
        silentBars_ = 0;
        fillIssued_ = false;
        stoppedForSilence_ = false;
    }

    [[nodiscard]] SilenceDecision update(float activity, const SilenceSettings& settings) noexcept {
        SilenceDecision result;
        const float safeActivity = std::clamp(std::isfinite(activity) ? activity : 0.0f, 0.0f, 1.0f);
        const float threshold = std::clamp(settings.silenceThreshold, 0.0f, 1.0f);
        const bool silent = safeActivity <= threshold;

        if (!silent) {
            if (stoppedForSilence_) {
                result.resumeDrums = true;
                result.markResumeWithCrash = true;
            }
            silentBars_ = 0;
            fillIssued_ = false;
            stoppedForSilence_ = false;
            result.silentBars = 0;
            result.intensityMultiplier = 1.0f;
            return result;
        }

        ++silentBars_;
        result.silentBars = silentBars_;

        switch (settings.mode) {
            case SilenceMode::KeepPlaying:
                break;

            case SilenceMode::ReduceIntensity:
                result.intensityMultiplier = std::clamp(1.0f - 0.20f * static_cast<float>(silentBars_), 0.25f, 1.0f);
                break;

            case SilenceMode::HoldGroove:
                result.suppressAdaptiveChanges = true;
                break;

            case SilenceMode::FillDuringSilence:
                if (!fillIssued_) {
                    result.requestFill = true;
                    fillIssued_ = true;
                }
                break;

            case SilenceMode::StopAfterBars:
                if (silentBars_ >= std::max(1, settings.stopAfterBars)) {
                    result.stopDrums = true;
                    result.waitingForResume = true;
                    stoppedForSilence_ = true;
                }
                break;

            case SilenceMode::WaitForResume:
                result.stopDrums = true;
                result.waitingForResume = true;
                stoppedForSilence_ = true;
                break;
        }

        return result;
    }

private:
    int silentBars_{0};
    bool fillIssued_{false};
    bool stoppedForSilence_{false};
};

} // namespace robodrummer
