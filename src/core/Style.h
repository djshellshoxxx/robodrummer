#pragma once
#include <cstdint>
namespace robodrummer {
struct Style {
    float kickBeat1{0.95f};
    float kickBeat3{0.70f};
    float extraKickProbability{0.08f};
    float snareBackbeat{0.98f};
    float closedHatProbability{0.96f};
    float crashOnSectionStart{0.80f};
    float humanizeMs{4.0f};
    static Style basicRock() noexcept { return {}; }
};
struct GrooveContext {
    double bpm{120.0};
    double sampleRate{48000.0};
    int numerator{4};
    int denominator{4};
    float intensity{0.5f};
    bool sectionStart{false};
    std::uint32_t seed{1};
};
}
