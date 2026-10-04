#pragma once
#include <cstdint>
namespace robodrummer {
enum class DrumInstrument : std::uint8_t { Kick, Snare, ClosedHat, OpenHat, Ride, Crash, HighTom, MidTom, FloorTom, Rimshot, Sidestick, China, Splash, Cowbell, Tambourine, Clap };
struct DrumEvent {
    DrumInstrument instrument{DrumInstrument::Kick};
    int sampleOffset{0};
    float velocity{1.0f};
    std::uint32_t sequence{0};
};
}
