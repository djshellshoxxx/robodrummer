// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include "core/MidiCommand.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace robodrummer {

// Lock-free CC -> parameter-target and note -> command bindings with MIDI Learn. The audio thread reads and
// learns; the UI thread arms, unbinds and displays. Targets are indices into a table owned by the processor.
class MidiLearnMap {
public:
    static constexpr int NoTarget = -1;
    static constexpr int NoCommand = -1;
    static constexpr int DefaultChannel = 16;
    static constexpr int CommandCount = static_cast<int>(MidiCommand::CountInToggle) + 1;
    struct NoteBinding {
        int command{NoCommand};
        int channel{DefaultChannel};
    };

    MidiLearnMap() noexcept { clearAll(); resetNoteDefaults(); }

    void clearAll() noexcept {
        for (auto& v : ccToTarget_) v.store(NoTarget, std::memory_order_relaxed);
        armedTarget_.store(NoTarget, std::memory_order_relaxed);
        armedCommand_.store(NoCommand, std::memory_order_relaxed);
    }

    void resetNoteDefaults() noexcept {
        for (std::size_t note = 0; note < noteBindings_.size(); ++note) {
            const int command = note >= 36 && note < 36 + CommandCount ? static_cast<int>(note) - 36 : NoCommand;
            noteBindings_[note].store(packBinding(command, DefaultChannel), std::memory_order_relaxed);
        }
    }

    // ---- controller bindings ----
    void bindCc(int cc, int target) noexcept {
        if (cc < 0 || cc > 127 || target < 0) return;
        unbindTarget(target);
        ccToTarget_[static_cast<std::size_t>(cc)].store(target, std::memory_order_relaxed);
    }
    void unbindTarget(int target) noexcept {
        for (auto& v : ccToTarget_) if (v.load(std::memory_order_relaxed) == target) v.store(NoTarget, std::memory_order_relaxed);
    }
    [[nodiscard]] int targetForCc(int cc) const noexcept {
        return cc < 0 || cc > 127 ? NoTarget : ccToTarget_[static_cast<std::size_t>(cc)].load(std::memory_order_relaxed);
    }
    [[nodiscard]] int ccForTarget(int target) const noexcept {
        for (int cc = 0; cc < 128; ++cc)
            if (ccToTarget_[static_cast<std::size_t>(cc)].load(std::memory_order_relaxed) == target) return cc;
        return -1;
    }

    // ---- note -> command bindings ----
    void bindNote(int note, int command, int channel = DefaultChannel) noexcept {
        if (note < 0 || note > 127 || command < 0 || command >= CommandCount) return;
        unbindCommand(command);
        noteBindings_[static_cast<std::size_t>(note)].store(
            packBinding(command, channel < 1 || channel > 16 ? DefaultChannel : channel), std::memory_order_relaxed);
    }
    void unbindCommand(int command) noexcept {
        for (auto& value : noteBindings_) {
            auto expected = value.load(std::memory_order_relaxed);
            if (unpackBinding(expected).command == command)
                value.compare_exchange_strong(expected, expected & ~0xffu, std::memory_order_relaxed);
        }
    }
    // One load owns the pair. State capture must use this snapshot rather than
    // reading channelForNote() and commandForNote() in separate operations.
    [[nodiscard]] NoteBinding noteBindingForNote(int note) const noexcept {
        return note < 0 || note > 127 ? NoteBinding{} :
            unpackBinding(noteBindings_[static_cast<std::size_t>(note)].load(std::memory_order_relaxed));
    }
    // A note only triggers its command on the MIDI channel it was bound to (channel 16 for the defaults).
    [[nodiscard]] int commandForNote(int note, int channel) const noexcept {
        const auto binding = noteBindingForNote(note);
        return binding.channel == channel ? binding.command : NoCommand;
    }
    [[nodiscard]] int channelForNote(int note) const noexcept {
        return noteBindingForNote(note).channel;
    }
    [[nodiscard]] int noteForCommand(int command) const noexcept {
        for (int note = 0; note < 128; ++note)
            if (noteBindingForNote(note).command == command) return note;
        return -1;
    }

    // ---- learn mode ----
    void armTarget(int target) noexcept { armedCommand_.store(NoCommand, std::memory_order_relaxed); armedTarget_.store(target, std::memory_order_release); }
    void armCommand(int command) noexcept { armedTarget_.store(NoTarget, std::memory_order_relaxed); armedCommand_.store(command, std::memory_order_release); }
    void cancelLearn() noexcept { armedTarget_.store(NoTarget, std::memory_order_relaxed); armedCommand_.store(NoCommand, std::memory_order_relaxed); }
    [[nodiscard]] int armedTarget() const noexcept { return armedTarget_.load(std::memory_order_acquire); }
    [[nodiscard]] int armedCommand() const noexcept { return armedCommand_.load(std::memory_order_acquire); }

    // Audio thread: a controller message arrived. Returns the target to drive (learning it first if armed).
    [[nodiscard]] int handleCc(int cc) noexcept {
        const int armed = armedTarget_.load(std::memory_order_acquire);
        if (armed != NoTarget) {
            bindCc(cc, armed);
            armedTarget_.store(NoTarget, std::memory_order_release);
            return armed;
        }
        return targetForCc(cc);
    }

    // Audio thread: a note-on arrived. Returns the command it triggers (learning it first if armed).
    [[nodiscard]] int handleNote(int note, int channel) noexcept {
        const int armed = armedCommand_.load(std::memory_order_acquire);
        if (armed != NoCommand) {
            bindNote(note, armed, channel);
            armedCommand_.store(NoCommand, std::memory_order_release);
            return NoCommand; // the learning press itself should not also fire the command
        }
        return commandForNote(note, channel);
    }

private:
    static_assert(CommandCount < 255);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    // Low byte: command + 1 (zero means unbound). High byte: MIDI channel.
    static constexpr std::uint32_t packBinding(int command, int channel) noexcept {
        return static_cast<std::uint32_t>(command + 1) | (static_cast<std::uint32_t>(channel) << 8);
    }
    static constexpr NoteBinding unpackBinding(std::uint32_t value) noexcept {
        return { static_cast<int>(value & 0xffu) - 1, static_cast<int>((value >> 8) & 0xffu) };
    }
    std::array<std::atomic<int>, 128> ccToTarget_{};
    std::array<std::atomic<std::uint32_t>, 128> noteBindings_{};
    std::atomic<int> armedTarget_{NoTarget};
    std::atomic<int> armedCommand_{NoCommand};
};

} // namespace robodrummer
