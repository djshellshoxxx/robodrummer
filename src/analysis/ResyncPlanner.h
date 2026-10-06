#pragma once
#include "analysis/RhythmAnalyzer.h"
#include <algorithm>

namespace robodrummer {

enum class ResyncAction {
    None,
    Wait,
    BreakAndRealign
};

class ResyncPlanner {
public:
    void reset() noexcept { persistentErrorSeconds_ = 0.0; }

    [[nodiscard]] ResyncAction update(bool hardResyncRecommended,
                                      const RhythmState& rhythm,
                                      double deltaSeconds) noexcept {
        deltaSeconds = std::clamp(deltaSeconds, 0.0, 0.5);

        if (!hardResyncRecommended) {
            persistentErrorSeconds_ = 0.0;
            return ResyncAction::None;
        }

        const bool reliableTiming = rhythm.locked &&
                                    rhythm.tempoConfidence >= 0.60f &&
                                    rhythm.beatConfidence >= 0.55f;
        if (!reliableTiming) {
            persistentErrorSeconds_ = 0.0;
            return ResyncAction::Wait;
        }

        persistentErrorSeconds_ += deltaSeconds;

        const bool reliableDownbeat = rhythm.downbeatConfidence >= 0.60f;
        const bool onBeatOne = rhythm.beatInBar == 1;
        const bool nearBoundary = rhythm.beatPhase <= 0.08 || rhythm.beatPhase >= 0.92;
        const bool persistent = persistentErrorSeconds_ >= 0.40;

        if (reliableDownbeat && onBeatOne && nearBoundary && persistent) {
            persistentErrorSeconds_ = 0.0;
            return ResyncAction::BreakAndRealign;
        }

        return ResyncAction::Wait;
    }

private:
    double persistentErrorSeconds_{0.0};
};

} // namespace robodrummer
