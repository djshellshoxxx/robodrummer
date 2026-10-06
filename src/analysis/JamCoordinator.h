#pragma once
#include "analysis/JamBrain.h"
#include <algorithm>
#include <cmath>

namespace robodrummer {

enum class JamMode {
    FreeJam,
    SemiStructured,
    ProgrammedSong
};

enum class JamCoordinationState {
    EstablishingGroove,
    StableJam,
    Building,
    Releasing,
    AwaitingCue,
    TransitionLikely,
    Break,
    SoloSupport,
    EndingLikely
};

struct JamCoordinatorObservation {
    PhraseState phraseState{PhraseState::Stable};
    float activity{0.5f};
    float intensity{0.5f};
    float trackingConfidence{0.0f};
    bool phraseBoundary{false};
    bool nextSectionCue{false};
    bool soloCue{false};
    bool endCue{false};
    bool programmedBoundaryDue{false};
};

struct JamCoordinatorSettings {
    JamMode mode{JamMode::FreeJam};
    int establishingBars{2};
    int breakBars{2};
    float trustedTrackingConfidence{0.45f};
    float breakActivityThreshold{0.08f};
};

struct JamCoordinatorDecision {
    JamCoordinationState state{JamCoordinationState::EstablishingGroove};
    float transitionProbability{0.0f};
    float endingProbability{0.0f};
    float intensityBias{0.0f};
    bool executeTransition{false};
    bool suppressBusyFills{false};
};

class JamCoordinator {
public:
    void reset() noexcept {
        barCount_ = 0;
        lowActivityBars_ = 0;
        awaitingCue_ = false;
        state_ = JamCoordinationState::EstablishingGroove;
    }

    [[nodiscard]] JamCoordinatorDecision update(const JamCoordinatorObservation& input,
                                                const JamCoordinatorSettings& settings) noexcept {
        ++barCount_;

        const float activity = std::clamp(std::isfinite(input.activity) ? input.activity : 0.0f, 0.0f, 1.0f);
        const float confidence = std::clamp(
            std::isfinite(input.trackingConfidence) ? input.trackingConfidence : 0.0f,
            0.0f,
            1.0f);

        if (activity <= std::clamp(settings.breakActivityThreshold, 0.0f, 1.0f))
            ++lowActivityBars_;
        else
            lowActivityBars_ = 0;

        JamCoordinatorDecision decision;

        // Explicit musical intent always wins over statistical inference.
        if (input.endCue) {
            state_ = JamCoordinationState::EndingLikely;
            awaitingCue_ = false;
            decision.state = state_;
            decision.endingProbability = 1.0f;
            decision.suppressBusyFills = false;
            return decision;
        }

        if (input.soloCue) {
            state_ = JamCoordinationState::SoloSupport;
            decision.state = state_;
            decision.intensityBias = -0.18f;
            decision.suppressBusyFills = true;
            return decision;
        }

        // Programmed boundaries are deterministic and do not require tracker confidence.
        if (settings.mode == JamMode::ProgrammedSong && input.programmedBoundaryDue) {
            state_ = JamCoordinationState::TransitionLikely;
            awaitingCue_ = false;
            decision.state = state_;
            decision.transitionProbability = 1.0f;
            decision.executeTransition = true;
            return decision;
        }

        if (lowActivityBars_ >= std::max(1, settings.breakBars)) {
            state_ = JamCoordinationState::Break;
            decision.state = state_;
            decision.intensityBias = -0.25f;
            decision.suppressBusyFills = true;
            return decision;
        }

        const bool trusted = confidence >= std::clamp(settings.trustedTrackingConfidence, 0.0f, 1.0f);

        // Explicit next-section cues are authoritative in semi-structured mode.
        if (settings.mode == JamMode::SemiStructured && input.nextSectionCue) {
            state_ = JamCoordinationState::TransitionLikely;
            awaitingCue_ = false;
            decision.state = state_;
            decision.transitionProbability = 1.0f;
            decision.executeTransition = true;
            return decision;
        }

        // During acquisition, avoid inventing structure from sparse early evidence.
        if (barCount_ <= std::max(0, settings.establishingBars)) {
            state_ = JamCoordinationState::EstablishingGroove;
            decision.state = state_;
            decision.suppressBusyFills = true;
            return decision;
        }

        if (!trusted) {
            awaitingCue_ = false;
            state_ = JamCoordinationState::StableJam;
            decision.state = state_;
            return decision;
        }

        if (settings.mode == JamMode::SemiStructured && awaitingCue_) {
            state_ = JamCoordinationState::AwaitingCue;
            decision.state = state_;
            decision.transitionProbability = 0.70f;
            return decision;
        }

        switch (input.phraseState) {
            case PhraseState::Break:
                state_ = JamCoordinationState::Break;
                decision.intensityBias = -0.25f;
                decision.suppressBusyFills = true;
                break;

            case PhraseState::Build:
                if (input.phraseBoundary) {
                    if (settings.mode == JamMode::SemiStructured) {
                        awaitingCue_ = true;
                        state_ = JamCoordinationState::AwaitingCue;
                        decision.transitionProbability = 0.72f;
                    } else {
                        state_ = JamCoordinationState::TransitionLikely;
                        decision.transitionProbability = 0.72f;
                    }
                } else {
                    state_ = JamCoordinationState::Building;
                    decision.intensityBias = 0.08f;
                }
                break;

            case PhraseState::Release:
                if (input.phraseBoundary && settings.mode == JamMode::SemiStructured) {
                    awaitingCue_ = true;
                    state_ = JamCoordinationState::AwaitingCue;
                    decision.transitionProbability = 0.62f;
                } else {
                    state_ = JamCoordinationState::Releasing;
                    decision.intensityBias = -0.08f;
                }
                break;

            case PhraseState::Stable:
                state_ = JamCoordinationState::StableJam;
                break;
        }

        decision.state = state_;
        return decision;
    }

    [[nodiscard]] JamCoordinationState state() const noexcept { return state_; }

private:
    int barCount_{0};
    int lowActivityBars_{0};
    bool awaitingCue_{false};
    JamCoordinationState state_{JamCoordinationState::EstablishingGroove};
};

} // namespace robodrummer
