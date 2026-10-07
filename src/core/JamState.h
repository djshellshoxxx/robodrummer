#pragma once
#include "MidiCommand.h"
#include <algorithm>
namespace robodrummer {
struct JamState {
    float intensity{0.5f};
    int queuedSectionDelta{0};
    bool fillRequested{false};
    float fillStrength{0.75f};
    bool crashRequested{false};
    bool breakRequested{false};
    bool stopped{false};
    bool resetListeningRequested{false};
    double timeScale{1.0};
};
inline void applyMidiCommand(JamState& state, MidiCommand command) noexcept {
    switch (command) {
        case MidiCommand::Fill: state.fillRequested = true; state.fillStrength = 0.75f; break;
        case MidiCommand::NextSection: ++state.queuedSectionDelta; break;
        case MidiCommand::PreviousSection: --state.queuedSectionDelta; break;
        case MidiCommand::Crash: state.crashRequested = true; break;
        case MidiCommand::IntensityUp: state.intensity = std::clamp(state.intensity + 0.1f, 0.0f, 1.0f); break;
        case MidiCommand::IntensityDown: state.intensity = std::clamp(state.intensity - 0.1f, 0.0f, 1.0f); break;
        case MidiCommand::HalfTime: state.timeScale = state.timeScale < 0.75 ? 1.0 : 0.5; break;
        case MidiCommand::DoubleTime: state.timeScale = state.timeScale > 1.5 ? 1.0 : 2.0; break;
        case MidiCommand::Break: state.breakRequested = true; break;
        case MidiCommand::Stop: state.stopped = true; break;
        case MidiCommand::Resume: state.stopped = false; break;
        case MidiCommand::ResetListening: state.resetListeningRequested = true; break;
        case MidiCommand::SoloSupport: break;
        case MidiCommand::EndJam: break;
    }
}
}
