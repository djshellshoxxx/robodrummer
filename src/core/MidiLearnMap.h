#pragma once
#include "core/MidiCommand.h"
#include <array>
#include <atomic>
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
            notes_[static_cast<std::size_t>(i)].store(36 + i, std::memory_order_relaxed);
    }

    [[nodiscard]] bool assign(MidiCommand command, int note) noexcept {
        if (note < 0 || note > 127)
            return false;

        const int commandIndex = midiCommandIndex(command);
        if (commandIndex < 0 || commandIndex >= MidiCommandCount)
            return false;

        for (auto& mappedNote : notes_) {
            if (mappedNote.load(std::memory_order_relaxed) == note)
                mappedNote.store(-1, std::memory_order_relaxed);
        }
        notes_[static_cast<std::size_t>(commandIndex)].store(note, std::memory_order_release);
        return true;
    }

    [[nodiscard]] int noteForCommand(MidiCommand command) const noexcept {
        const int commandIndex = midiCommandIndex(command);
        if (commandIndex < 0 || commandIndex >= MidiCommandCount)
            return -1;
        return notes_[static_cast<std::size_t>(commandIndex)].load(std::memory_order_acquire);
    }

    [[nodiscard]] std::optional<MidiCommand> commandForNote(int note) const noexcept {
        if (note < 0 || note > 127)
            return std::nullopt;
        for (int i = 0; i < MidiCommandCount; ++i) {
            if (notes_[static_cast<std::size_t>(i)].load(std::memory_order_acquire) == note)
                return static_cast<MidiCommand>(i);
        }
        return std::nullopt;
    }

private:
    std::array<std::atomic<int>, MidiCommandCount> notes_{};
};

} // namespace robodrummer
