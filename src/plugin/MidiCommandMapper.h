#pragma once
#include "core/MidiCommand.h"
#include <optional>
namespace robodrummer {
inline std::optional<MidiCommand> mapNoteOn(int note, float velocity) noexcept {
    if (velocity <= 0.0f) return std::nullopt;
    switch (note) {
        case 36: return MidiCommand::Fill;
        case 37: return MidiCommand::NextSection;
        case 38: return MidiCommand::PreviousSection;
        case 39: return MidiCommand::Crash;
        case 40: return MidiCommand::IntensityUp;
        case 41: return MidiCommand::IntensityDown;
        case 42: return MidiCommand::HalfTime;
        case 43: return MidiCommand::DoubleTime;
        case 44: return MidiCommand::Break;
        case 45: return MidiCommand::Stop;
        case 46: return MidiCommand::Resume;
        case 47: return MidiCommand::ResetListening;
        case 48: return MidiCommand::SoloSupport;
        case 49: return MidiCommand::EndJam;
        default: return std::nullopt;
    }
}
}
