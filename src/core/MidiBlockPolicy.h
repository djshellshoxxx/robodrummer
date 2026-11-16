#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace robodrummer {

// Non-owning view into the host's current MIDI event. Do not retain it after
// the host buffer changes. Long SysEx is never copied merely to inspect status.
class MidiMessageView {
public:
    MidiMessageView(const std::uint8_t* bytes, int count) noexcept
        : bytes_(bytes), count_(bytes != nullptr && count > 0 ? static_cast<std::size_t>(count) : 0) {}

    [[nodiscard]] const std::uint8_t* data() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] int getChannel() const noexcept {
        const auto status = byte(0);
        return status >= 0x80 && status < 0xf0 ? (status & 0x0f) + 1 : 0;
    }
    [[nodiscard]] bool isMidiClock() const noexcept { return byte(0) == 0xf8; }
    [[nodiscard]] bool isMidiStart() const noexcept { return byte(0) == 0xfa; }
    [[nodiscard]] bool isMidiContinue() const noexcept { return byte(0) == 0xfb; }
    [[nodiscard]] bool isMidiStop() const noexcept { return byte(0) == 0xfc; }
    [[nodiscard]] bool isController() const noexcept { return count_ >= 3 && (byte(0) & 0xf0) == 0xb0; }
    [[nodiscard]] int getControllerNumber() const noexcept { return byte(1); }
    [[nodiscard]] int getControllerValue() const noexcept { return byte(2); }
    [[nodiscard]] bool isAllNotesOff() const noexcept { return isController() && byte(1) == 123; }
    [[nodiscard]] bool isAllSoundOff() const noexcept { return isController() && byte(1) == 120; }
    [[nodiscard]] bool isNoteOn(bool includeVelocityZero = false) const noexcept {
        return count_ >= 3 && (byte(0) & 0xf0) == 0x90 && (includeVelocityZero || byte(2) != 0);
    }
    [[nodiscard]] bool isNoteOff(bool includeVelocityZero = true) const noexcept {
        return count_ >= 3 && ((byte(0) & 0xf0) == 0x80 || (includeVelocityZero && isNoteOn(true) && byte(2) == 0));
    }
    [[nodiscard]] int getNoteNumber() const noexcept { return byte(1); }
    [[nodiscard]] std::uint8_t getVelocity() const noexcept {
        return isNoteOn(true) || isNoteOff(false) ? byte(2) : 0;
    }
    [[nodiscard]] bool requiresCriticalOutput() const noexcept {
        return isMidiStop() || isAllSoundOff() || isAllNotesOff() || isNoteOff();
    }

private:
    [[nodiscard]] std::uint8_t byte(std::size_t index) const noexcept { return index < count_ ? bytes_[index] : 0; }
    const std::uint8_t* bytes_;
    std::size_t count_;
};

enum class MidiOutputClass : std::uint8_t { Ordinary, Generated, Critical };

// Per-callback accounting only; backing storage belongs to the framework
// adapter and is reserved before playback. Separate quotas prevent large input
// messages from taking the generated-drum or note-off/stop reserve.
class MidiBlockPolicy {
public:
    static constexpr std::size_t OutputCapacityBytes = 8192;
    static constexpr std::size_t MaxInputEvents = 4096;
    static constexpr std::size_t MaxGeneratedEvents = 128 * 2; // processor's fixed hit array, on + off
    // Conservatively exceeds JUCE 9's six-byte event header, without exposing
    // that framework layout to callers. The adapter must reserve at least this
    // class's OutputCapacityBytes before it calls addEvent.
    static constexpr std::size_t EventOverheadBytes = 8;
    static constexpr std::size_t GeneratedBytes = MaxGeneratedEvents * (3 + EventOverheadBytes);
    static constexpr std::size_t CriticalBytes = 1024;
    static constexpr std::size_t OrdinaryBytes = OutputCapacityBytes - GeneratedBytes - CriticalBytes;

    [[nodiscard]] bool scanNext() noexcept {
        if (scanned_ < MaxInputEvents) { ++scanned_; return true; }
        inputOverflowed_ = true;
        return false;
    }
    [[nodiscard]] bool admitOutput(std::size_t messageBytes, MidiOutputClass kind) noexcept {
        const auto index = static_cast<std::size_t>(kind);
        const auto quota = budgetFor(kind);
        if (index >= used_.size() || messageBytes == 0 || messageBytes > quota ||
            quota - messageBytes < EventOverheadBytes || used_[index] > quota - messageBytes - EventOverheadBytes) {
            ++outputDropped_;
            if (kind == MidiOutputClass::Critical) criticalOverflowed_ = true;
            return false;
        }
        used_[index] += messageBytes + EventOverheadBytes;
        return true;
    }
    [[nodiscard]] static constexpr std::size_t budgetFor(MidiOutputClass kind) noexcept {
        switch (kind) {
            case MidiOutputClass::Ordinary: return OrdinaryBytes;
            case MidiOutputClass::Generated: return GeneratedBytes;
            case MidiOutputClass::Critical: return CriticalBytes;
        }
        return 0;
    }
    // Panic first discards queued output; clear its accounting too, retaining
    // the overflow evidence and scan limit for diagnostics and control flow.
    void resetOutput() noexcept { used_ = {}; }
    [[nodiscard]] std::size_t outputBytes() const noexcept { return used_[0] + used_[1] + used_[2]; }
    [[nodiscard]] std::size_t outputDropped() const noexcept { return outputDropped_; }
    [[nodiscard]] bool inputOverflowed() const noexcept { return inputOverflowed_; }
    [[nodiscard]] bool criticalOutputOverflowed() const noexcept { return criticalOverflowed_; }
    [[nodiscard]] bool needsPanic() const noexcept { return inputOverflowed_ || criticalOverflowed_; }

private:
    std::array<std::size_t, 3> used_{};
    std::size_t scanned_{0}, outputDropped_{0};
    bool inputOverflowed_{false}, criticalOverflowed_{false};
};

} // namespace robodrummer
