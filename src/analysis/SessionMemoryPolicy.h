#pragma once
#include "analysis/JamBrain.h"
#include "analysis/SessionMemory.h"
#include <algorithm>
#include <cmath>

namespace robodrummer {

struct SessionMemoryPolicyResult {
    JamBrainSettings brainSettings{};
    float dynamicFollow{0.0f};
};

class SessionMemoryPolicy {
public:
    [[nodiscard]] static SessionMemoryPolicyResult apply(
        JamBrainSettings base,
        float baseDynamicFollow,
        const SessionMemoryRecommendations& memory,
        bool enabled) noexcept {

        SessionMemoryPolicyResult result;
        result.brainSettings = base;
        result.dynamicFollow = std::clamp(baseDynamicFollow, 0.0f, 1.0f);

        if (!enabled || memory.confidence < 0.50f)
            return result;

        const float trust = std::clamp((memory.confidence - 0.50f) / 0.50f, 0.0f, 1.0f);

        if (memory.suggestedPhraseBars > 0) {
            const int target = std::clamp(memory.suggestedPhraseBars, 2, 16);
            const int maxShift = std::max(1, static_cast<int>(std::lround(4.0f * trust)));
            if (target < base.phraseBars)
                result.brainSettings.phraseBars = std::max(target, base.phraseBars - maxShift);
            else if (target > base.phraseBars)
                result.brainSettings.phraseBars = std::min(target, base.phraseBars + maxShift);
        }

        result.brainSettings.fillBias = std::clamp(
            base.fillBias + memory.fillBiasAdjustment * trust,
            -0.25f,
            0.35f);

        const float dynamicScale = 1.0f + (memory.dynamicSensitivity - 1.0f) * trust;
        result.dynamicFollow = std::clamp(result.dynamicFollow * dynamicScale, 0.0f, 1.0f);

        if (memory.dynamicSensitivity > 1.0f) {
            const float thresholdScale = 1.0f / (1.0f + (memory.dynamicSensitivity - 1.0f) * trust);
            result.brainSettings.buildThreshold = std::clamp(base.buildThreshold * thresholdScale, 0.04f, 0.18f);
            result.brainSettings.releaseThreshold = std::clamp(base.releaseThreshold * thresholdScale, 0.04f, 0.18f);
        }

        return result;
    }
};

} // namespace robodrummer
