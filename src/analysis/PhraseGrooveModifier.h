#pragma once
#include "analysis/JamBrain.h"
#include "core/Style.h"
#include <algorithm>

namespace robodrummer {

class PhraseGrooveModifier {
public:
    [[nodiscard]] static Style apply(Style style, PhraseState state, float amount) noexcept {
        amount = std::clamp(amount, 0.0f, 1.0f);

        switch (state) {
            case PhraseState::Build:
                style.extraKickProbability = std::clamp(style.extraKickProbability + 0.16f * amount, 0.0f, 1.0f);
                style.crashOnSectionStart = std::clamp(style.crashOnSectionStart + 0.15f * amount, 0.0f, 1.0f);
                style.openHatProbability = std::clamp(style.openHatProbability + 0.18f * amount, 0.0f, 1.0f);
                style.rideProbability = std::clamp(style.rideProbability + 0.06f * amount, 0.0f, 1.0f);
                style.ghostSnareProbability = std::clamp(style.ghostSnareProbability + 0.08f * amount, 0.0f, 1.0f);
                break;
            case PhraseState::Release:
                style.extraKickProbability = std::clamp(style.extraKickProbability * (1.0f - 0.65f * amount), 0.0f, 1.0f);
                style.openHatProbability = std::clamp(style.openHatProbability * (1.0f - 0.55f * amount), 0.0f, 1.0f);
                style.rideProbability = std::clamp(style.rideProbability + 0.22f * amount, 0.0f, 1.0f);
                style.crashOnSectionStart = std::clamp(style.crashOnSectionStart * (1.0f - 0.35f * amount), 0.0f, 1.0f);
                break;
            case PhraseState::Break: {
                const float keep = 1.0f - 0.75f * amount;
                style.kickBeat1 *= keep;
                style.kickBeat3 *= keep;
                style.extraKickProbability *= keep * 0.5f;
                style.snareBackbeat *= keep;
                style.closedHatProbability *= std::max(0.12f, keep);
                style.openHatProbability *= keep;
                style.rideProbability *= keep;
                style.ghostSnareProbability *= keep;
                style.crashOnSectionStart = 0.0f;
                break;
            }
            case PhraseState::Stable:
                break;
        }
        return style;
    }
};

} // namespace robodrummer
