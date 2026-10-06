#pragma once
#include <cstdint>

namespace robodrummer {

enum class OutputMode : std::uint8_t {
    InternalDrums,
    MidiOnly,
    InternalAndMidi
};

[[nodiscard]] constexpr bool rendersInternalAudio(OutputMode mode) noexcept {
    return mode == OutputMode::InternalDrums || mode == OutputMode::InternalAndMidi;
}

[[nodiscard]] constexpr bool writesGeneratedMidi(OutputMode mode) noexcept {
    return mode == OutputMode::MidiOnly || mode == OutputMode::InternalAndMidi;
}

} // namespace robodrummer
