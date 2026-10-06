#pragma once
#include "core/MidiCommand.h"
#include <algorithm>

namespace robodrummer {

[[nodiscard]] inline float applyIntensityCommand(float current, MidiCommand command) noexcept {
    current = std::clamp(current, 0.0f, 1.0f);
    switch (command) {
        case MidiCommand::IntensityUp: return std::clamp(current + 0.1f, 0.0f, 1.0f);
        case MidiCommand::IntensityDown: return std::clamp(current - 0.1f, 0.0f, 1.0f);
        default: return current;
    }
}

} // namespace robodrummer
