#pragma once
#include "core/MidiCommand.h"
#include <array>
#include <optional>

namespace robodrummer {

inline constexpr int MidiCommandCount = 14;

[[nodiscard]] constexpr int midiCommandIndex(MidiCommand command) noexcept {
    return static_cast<int>(command);
}

class MidiLearnMap {
public:
    MidiLearnMap() noexcept { resetDefaults(); }

    void resetDefaults() noexcept {
        for (int i = 0; i < MidiCommandCount; ++i)
            notes_[static_cast<std::size_t>(i)] = 36 + i;
    }

    [[nodiscard]] bool assign(MidiCommand command, int note) noexcept {
        if (note < 0 || note > 127)
            return false;

        const int commandIndex = midiCommandIndex(command);
        if (commandIndex < 0 || commandIndex >= MidiCommandCount)
            return false;

        for (auto& mappedNote : notes_) {
            if (mappedNote == note)
                mappedNote = -1;
        }
        notes_[static_cast<std::size_t>(commandIndex)] = note;
        return true;
    }

    [[nodiscard]] int noteForCommand(MidiCommand command) const noexcept {
        const int commandIndex = midiCommandIndex(command);
        if (commandIndex < 0 || commandIndex >= MidiCommandCount)
            return -1;
        return notes_[static_cast<std::size_t>(commandIndex)];
    }

    [[nodiscard]] std::optional<MidiCommand> commandForNote(int note) const noexcept {
        if (note < 0 || note > 127)
            return std::nullopt;
        for (int i = 0; i < MidiCommandCount; ++i) {
            if (notes_[static_cast<std::size_t>(i)] == note)
                return static_cast<MidiCommand>(i);
        }
        return std::nullopt;
    }

private:
    std::array<int, MidiCommandCount> notes_{};
};

} // namespace robodrummer
