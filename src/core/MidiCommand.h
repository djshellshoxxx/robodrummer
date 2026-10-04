#pragma once
#include <cstdint>
namespace robodrummer {
enum class MidiCommand : std::uint8_t { Fill, NextSection, PreviousSection, Crash, IntensityUp, IntensityDown, HalfTime, DoubleTime, Break, Stop, Resume, ResetListening };
}
