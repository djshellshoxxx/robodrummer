#pragma once
#include "analysis/TimingAuthorityController.h"
#include <algorithm>

namespace robodrummer {

class DynamicsFollower {
public:
    [[nodiscard]] static float blend(float manualIntensity,
                                     float guitarIntensity,
                                     float dynamicFollow,
                                     float effectiveGuitarAuthority,
                                     LeadershipMode mode) noexcept {
        manualIntensity = std::clamp(manualIntensity, 0.0f, 1.0f);
        guitarIntensity = std::clamp(guitarIntensity, 0.0f, 1.0f);
        dynamicFollow = std::clamp(dynamicFollow, 0.0f, 1.0f);
        effectiveGuitarAuthority = std::clamp(effectiveGuitarAuthority, 0.0f, 1.0f);
        if (mode == LeadershipMode::DrummerLeads) return manualIntensity;
        const float amount = dynamicFollow * effectiveGuitarAuthority;
        return std::clamp(manualIntensity + (guitarIntensity - manualIntensity) * amount, 0.0f, 1.0f);
    }
};

} // namespace robodrummer
