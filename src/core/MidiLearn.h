// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace robodrummer {

// MIDI Learn (SPEC §26, CDL baseline §5): maps (MIDI channel 1..16, CC 0..127) -> parameter
// slot index 0..MaxSlots-1. The integrator maps slot <-> parameter ID.
//
// Conflict policy: newest mapping wins and the relation is one-to-one. Learning a CC that is
// already mapped re-points it to the new slot (the old slot becomes unlinked); learning a slot
// that already has a CC replaces that CC. The result reports what was displaced.
//
// Threading: all methods are intended for ONE thread (the audio thread). The UI should request
// arm/cancel/unlink through an atomic/message queue that the audio thread drains, and read
// state()/lastResult() via a snapshot. Nothing allocates except serialize()/parse()
// (message thread only). Storage is fixed; lookup is a table index.
class MidiLearn {
public:
    static constexpr int MaxSlots = 64;
    static constexpr int ExpressionCc = 11;

    enum class State : std::uint8_t { Idle, Armed };

    struct LearnResult {
        bool learned{false};
        int slot{-1};
        int channel{0};           // 1..16
        int cc{0};                // 0..127
        int displacedSlot{-1};    // slot that lost its link due to the conflict policy, if any
        int replacedCc{-1};       // CC previously mapped to `slot`, if any
    };

    struct CcResult {
        bool handled{false};      // true if the CC was consumed (learned or mapped)
        int slot{-1};
        float value{0.0f};        // 0..1 (cc/127)
        bool learned{false};
        LearnResult learn;
    };

    [[nodiscard]] static constexpr float normalize(int ccValue) noexcept {
        return ccValue <= 0 ? 0.0f : (ccValue >= 127 ? 1.0f : static_cast<float>(ccValue) / 127.0f);
    }

    MidiLearn() noexcept { clearAll(); }

    // --- learn mode -------------------------------------------------------------------------
    // Arm learn for a slot; the next CC received is mapped. timeoutSamples <= 0 means no timeout.
    bool arm(int slot, long long timeoutSamples) noexcept {
        if (!validSlot(slot)) return false;
        state_ = State::Armed;
        armedSlot_ = slot;
        remaining_ = timeoutSamples;
        timeoutEnabled_ = timeoutSamples > 0;
        timedOut_ = false;
        return true;
    }
    void cancel() noexcept { state_ = State::Idle; armedSlot_ = -1; timedOut_ = false; }
    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] int armedSlot() const noexcept { return armedSlot_; }
    // True once after a timeout expired (cleared by arm/cancel/clearTimedOut). For UI feedback.
    [[nodiscard]] bool timedOut() const noexcept { return timedOut_; }
    void clearTimedOut() noexcept { timedOut_ = false; }
    // Last learn outcome (for "show the mapping result").
    [[nodiscard]] const LearnResult& lastResult() const noexcept { return last_; }

    // Advance the timeout by numSamples. Returns true if the armed state just timed out.
    bool tick(int numSamples) noexcept {
        if (state_ != State::Armed || !timeoutEnabled_) return false;
        remaining_ -= numSamples;
        if (remaining_ > 0) return false;
        state_ = State::Idle;
        armedSlot_ = -1;
        timedOut_ = true;
        return true;
    }

    // --- mapping ----------------------------------------------------------------------------
    // Explicit link (newest wins). Returns false for out-of-range arguments.
    bool link(int channel, int cc, int slot) noexcept {
        LearnResult r;
        return linkInternal(channel, cc, slot, r);
    }
    void unlinkSlot(int slot) noexcept {
        if (!validSlot(slot)) return;
        const auto s = static_cast<std::size_t>(slot);
        if (slotKey_[s] < 0) return;
        table_[static_cast<std::size_t>(slotKey_[s])] = -1;
        slotKey_[s] = -1;
        --count_;
    }
    void unlinkCc(int channel, int cc) noexcept {
        if (!validKey(channel, cc)) return;
        const int slot = table_[key(channel, cc)];
        if (slot >= 0) unlinkSlot(slot);
    }
    void clearAll() noexcept {
        table_.fill(-1);
        slotKey_.fill(-1);
        count_ = 0;
        state_ = State::Idle;
        armedSlot_ = -1;
        timedOut_ = false;
        last_ = {};
    }
    [[nodiscard]] int mappingCount() const noexcept { return count_; }
    // Slot for the CC or -1. Realtime-safe, O(1). Ignores expression default.
    [[nodiscard]] int slotFor(int channel, int cc) const noexcept {
        return validKey(channel, cc) ? table_[key(channel, cc)] : -1;
    }
    // CC mapped to the slot (0..127) or -1; channel via channelForSlot.
    [[nodiscard]] int ccForSlot(int slot) const noexcept {
        return validSlot(slot) && slotKey_[static_cast<std::size_t>(slot)] >= 0 ? slotKey_[static_cast<std::size_t>(slot)] % 128 : -1;
    }
    [[nodiscard]] int channelForSlot(int slot) const noexcept {
        return validSlot(slot) && slotKey_[static_cast<std::size_t>(slot)] >= 0 ? slotKey_[static_cast<std::size_t>(slot)] / 128 + 1 : -1;
    }

    // --- expression pedal default (off by default) --------------------------------------------
    // When enabled, CC 11 on any channel drives `intensitySlot` unless the user mapped CC 11
    // (explicit mappings take precedence). Not part of the mapping table; not learned/unlinked.
    void setExpressionDefault(bool enabled, int intensitySlot) noexcept {
        expressionEnabled_ = enabled && validSlot(intensitySlot);
        expressionSlot_ = expressionEnabled_ ? intensitySlot : -1;
    }
    [[nodiscard]] bool expressionDefaultEnabled() const noexcept { return expressionEnabled_; }
    [[nodiscard]] int expressionSlot() const noexcept { return expressionSlot_; }

    // --- input ------------------------------------------------------------------------------
    // Feed one incoming CC. channel 1..16, cc 0..127, value 0..127. Invalid input is ignored.
    // While armed, the CC is learned (and consumed); otherwise it is looked up.
    CcResult handleCc(int channel, int cc, int value) noexcept {
        CcResult out;
        if (!validKey(channel, cc) || value < 0 || value > 127) return out;
        out.value = normalize(value);
        if (state_ == State::Armed) {
            LearnResult r;
            r.slot = armedSlot_;
            if (linkInternal(channel, cc, armedSlot_, r)) {
                r.learned = true;
                last_ = r;
                out.learned = true;
                out.learn = r;
                out.slot = r.slot;
                out.handled = true;
            }
            state_ = State::Idle;
            armedSlot_ = -1;
            return out;
        }
        int slot = table_[key(channel, cc)];
        if (slot < 0 && expressionEnabled_ && cc == ExpressionCc) slot = expressionSlot_;
        if (slot >= 0) {
            out.slot = slot;
            out.handled = true;
        }
        return out;
    }

    // --- serialization (message thread) -------------------------------------------------------
    // "ch:cc>slot;ch:cc>slot;...;expr=<slot>" (expr only when the default is enabled).
    [[nodiscard]] std::string serialize() const {
        std::string s;
        for (int slot = 0; slot < MaxSlots; ++slot) {
            const int k = slotKey_[static_cast<std::size_t>(slot)];
            if (k < 0) continue;
            if (!s.empty()) s += ';';
            s += std::to_string(k / 128 + 1) + ":" + std::to_string(k % 128) + ">" + std::to_string(slot);
        }
        if (expressionEnabled_) {
            if (!s.empty()) s += ';';
            s += "expr=" + std::to_string(expressionSlot_);
        }
        return s;
    }

    // Replaces this object's mappings from text. Returns false (leaving *this unchanged) on any
    // malformed, out-of-range or conflicting entry. Learn state is reset on success.
    [[nodiscard]] bool deserialize(std::string_view text) {
        if (text.size() > 4096) return false;
        MidiLearn tmp;
        std::size_t pos = 0;
        while (pos <= text.size()) {
            auto end = text.find(';', pos);
            if (end == std::string_view::npos) end = text.size();
            const auto tok = text.substr(pos, end - pos);
            pos = end + 1;
            if (tok.empty()) continue;
            if (tok.substr(0, 5) == "expr=") {
                int slot = 0;
                if (!parseInt(tok.substr(5), slot) || !validSlot(slot) || tmp.expressionEnabled_) return false;
                tmp.setExpressionDefault(true, slot);
                continue;
            }
            const auto colon = tok.find(':');
            const auto gt = tok.find('>');
            if (colon == std::string_view::npos || gt == std::string_view::npos || gt < colon) return false;
            int ch = 0, cc = 0, slot = 0;
            if (!parseInt(tok.substr(0, colon), ch) || !parseInt(tok.substr(colon + 1, gt - colon - 1), cc)
                || !parseInt(tok.substr(gt + 1), slot))
                return false;
            if (!validKey(ch, cc) || !validSlot(slot)) return false;
            if (tmp.slotFor(ch, cc) >= 0 || tmp.ccForSlot(slot) >= 0) return false; // duplicate
            tmp.link(ch, cc, slot);
        }
        *this = tmp;
        return true;
    }

private:
    static constexpr std::size_t TableSize = 16 * 128;

    [[nodiscard]] static constexpr bool validSlot(int slot) noexcept { return slot >= 0 && slot < MaxSlots; }
    [[nodiscard]] static constexpr bool validKey(int ch, int cc) noexcept { return ch >= 1 && ch <= 16 && cc >= 0 && cc <= 127; }
    [[nodiscard]] static constexpr std::size_t key(int ch, int cc) noexcept { return static_cast<std::size_t>((ch - 1) * 128 + cc); }

    static bool parseInt(std::string_view s, int& out) noexcept {
        if (s.empty() || s.size() > 4) return false;
        int v = 0;
        for (const char c : s) {
            if (c < '0' || c > '9') return false;
            v = v * 10 + (c - '0');
        }
        out = v;
        return true;
    }

    bool linkInternal(int channel, int cc, int slot, LearnResult& r) noexcept {
        if (!validKey(channel, cc) || !validSlot(slot)) return false;
        const auto k = key(channel, cc);
        const auto s = static_cast<std::size_t>(slot);
        const int existingSlot = table_[k];
        if (existingSlot == slot) {
            r.channel = channel; r.cc = cc; r.slot = slot;
            return true;
        }
        if (existingSlot >= 0) { // CC was mapped elsewhere: newest wins
            r.displacedSlot = existingSlot;
            slotKey_[static_cast<std::size_t>(existingSlot)] = -1;
            table_[k] = -1;
            --count_;
        }
        if (slotKey_[s] >= 0) { // slot had a different CC: replace it
            r.replacedCc = slotKey_[s] % 128;
            table_[static_cast<std::size_t>(slotKey_[s])] = -1;
            slotKey_[s] = -1;
            --count_;
        }
        table_[k] = static_cast<std::int16_t>(slot);
        slotKey_[s] = static_cast<std::int16_t>(k);
        ++count_;
        r.channel = channel; r.cc = cc; r.slot = slot;
        return true;
    }

    std::array<std::int16_t, TableSize> table_{};      // key -> slot or -1
    std::array<std::int16_t, MaxSlots> slotKey_{};     // slot -> key or -1
    int count_{0};
    State state_{State::Idle};
    int armedSlot_{-1};
    long long remaining_{0};
    bool timeoutEnabled_{false};
    bool timedOut_{false};
    bool expressionEnabled_{false};
    int expressionSlot_{-1};
    LearnResult last_{};
};

} // namespace robodrummer
