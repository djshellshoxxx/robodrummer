// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include "core/DrumEvent.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace robodrummer {

inline constexpr std::size_t DrumInstrumentCount = 16;

// Canonical lowercase instrument names (also the kit file-name stems, see docs/KIT_FORMAT.md).
[[nodiscard]] inline const char* drumInstrumentName(DrumInstrument i) noexcept {
    static constexpr const char* names[DrumInstrumentCount] = {
        "kick", "snare", "closedhat", "openhat", "ride", "crash", "hightom", "midtom",
        "floortom", "rimshot", "sidestick", "china", "splash", "cowbell", "tambourine", "clap"};
    const auto k = static_cast<std::size_t>(i);
    return k < DrumInstrumentCount ? names[k] : "";
}

[[nodiscard]] inline std::optional<DrumInstrument> drumInstrumentFromName(std::string_view name) noexcept {
    for (std::size_t k = 0; k < DrumInstrumentCount; ++k) {
        const std::string_view n = drumInstrumentName(static_cast<DrumInstrument>(k));
        if (n.size() != name.size()) continue;
        bool same = true;
        for (std::size_t j = 0; j < n.size() && same; ++j) {
            char c = name[j];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            same = c == n[j];
        }
        if (same) return static_cast<DrumInstrument>(k);
    }
    return std::nullopt;
}

// MIDI output map (SPEC §27): per-instrument note, output channel 1..16, note length.
// Default is General MIDI (matches the previous RoboDrummerAudioProcessor::midiNoteFor).
class DrumMidiMap {
public:
    static constexpr int DefaultChannel = 10;
    static constexpr float DefaultNoteLengthMs = 100.0f;
    static constexpr float MinNoteLengthMs = 5.0f;
    static constexpr float MaxNoteLengthMs = 2000.0f;
    static constexpr std::size_t MaxSerializedLength = 512;

    struct ParseResult; // defined below the class

    DrumMidiMap() noexcept { notes_ = generalMidiNotes(); }

    [[nodiscard]] static constexpr int generalMidiNote(DrumInstrument i) noexcept {
        switch (i) {
            case DrumInstrument::Kick: return 36;
            case DrumInstrument::Snare: return 38;
            case DrumInstrument::ClosedHat: return 42;
            case DrumInstrument::OpenHat: return 46;
            case DrumInstrument::Ride: return 51;
            case DrumInstrument::Crash: return 49;
            case DrumInstrument::HighTom: return 50;
            case DrumInstrument::MidTom: return 47;
            case DrumInstrument::FloorTom: return 43;
            case DrumInstrument::Rimshot: return 37;
            case DrumInstrument::Sidestick: return 37;
            case DrumInstrument::China: return 52;
            case DrumInstrument::Splash: return 55;
            case DrumInstrument::Cowbell: return 56;
            case DrumInstrument::Tambourine: return 54;
            case DrumInstrument::Clap: return 39;
        }
        return -1;
    }

    // Note 0..127 for the instrument (always valid).
    [[nodiscard]] int noteFor(DrumInstrument i) const noexcept {
        const auto k = static_cast<std::size_t>(i);
        return k < DrumInstrumentCount ? notes_[k] : -1;
    }
    // Returns false (and changes nothing) when note is outside 0..127.
    bool setNote(DrumInstrument i, int note) noexcept {
        const auto k = static_cast<std::size_t>(i);
        if (k >= DrumInstrumentCount || note < 0 || note > 127) return false;
        notes_[k] = note;
        return true;
    }

    [[nodiscard]] int channel() const noexcept { return channel_; } // 1..16
    bool setChannel(int ch) noexcept {
        if (ch < 1 || ch > 16) return false;
        channel_ = ch;
        return true;
    }
    [[nodiscard]] int zeroBasedChannel() const noexcept { return channel_ - 1; }

    [[nodiscard]] float noteLengthMs() const noexcept { return noteLengthMs_; }
    bool setNoteLengthMs(float ms) noexcept {
        if (!(ms >= MinNoteLengthMs && ms <= MaxNoteLengthMs)) return false; // also rejects NaN
        noteLengthMs_ = ms;
        return true;
    }
    [[nodiscard]] int noteLengthSamples(double sampleRate) const noexcept {
        return std::max(1, static_cast<int>(noteLengthMs_ * 0.001 * sampleRate));
    }

    void resetToGeneralMidi() noexcept { *this = DrumMidiMap(); }

    bool operator==(const DrumMidiMap& o) const noexcept {
        return notes_ == o.notes_ && channel_ == o.channel_ && noteLengthMs_ == o.noteLengthMs_;
    }

    // Compact form: "kick=36;snare=38;...;tambourine=54;clap=39;ch=10;len=100"
    [[nodiscard]] std::string serialize() const {
        std::string s;
        for (std::size_t k = 0; k < DrumInstrumentCount; ++k) {
            s += drumInstrumentName(static_cast<DrumInstrument>(k));
            s += '=';
            s += std::to_string(notes_[k]);
            s += ';';
        }
        s += "ch=" + std::to_string(channel_);
        s += ";len=" + std::to_string(static_cast<int>(noteLengthMs_ + 0.5f));
        return s;
    }

    // Parses and validates; missing keys keep GM defaults. Unknown/duplicate keys, bad numbers
    // and out-of-range values (note 0..127, ch 1..16, len 5..2000 ms) fail with a message.
    // Atomic: the result map is only meaningful when ok.
    [[nodiscard]] static ParseResult parse(std::string_view text);


private:
    static std::array<int, DrumInstrumentCount> generalMidiNotes() noexcept {
        std::array<int, DrumInstrumentCount> n{};
        for (std::size_t k = 0; k < DrumInstrumentCount; ++k) n[k] = generalMidiNote(static_cast<DrumInstrument>(k));
        return n;
    }
    static std::string_view trim(std::string_view s) noexcept {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
        return s;
    }
    static bool iequals(std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            char x = a[i], y = b[i];
            if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
            if (x != y) return false;
        }
        return true;
    }
    static bool parseInt(std::string_view s, int& out) noexcept {
        if (s.empty() || s.size() > 6) return false;
        int v = 0;
        for (const char c : s) {
            if (c < '0' || c > '9') return false;
            v = v * 10 + (c - '0');
        }
        out = v;
        return true;
    }

    std::array<int, DrumInstrumentCount> notes_{};
    int channel_{DefaultChannel};
    float noteLengthMs_{DefaultNoteLengthMs};
};

struct DrumMidiMap::ParseResult {
    bool ok{false};
    std::string error; // empty when ok
    DrumMidiMap map;   // valid only when ok
};

inline DrumMidiMap::ParseResult DrumMidiMap::parse(std::string_view text)
{
    ParseResult res;
    if (text.size() > MaxSerializedLength) { res.error = "map string too long"; return res; }
    DrumMidiMap m;
    std::array<bool, DrumInstrumentCount> seen{};
    bool seenCh = false, seenLen = false;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        auto end = text.find(';', pos);
        if (end == std::string_view::npos) end = text.size();
        const auto token = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (token.empty()) continue;
        const auto eq = token.find('=');
        if (eq == std::string_view::npos) { res.error = "missing '=' in '" + std::string(token) + "'"; return res; }
        const auto key = trim(token.substr(0, eq));
        const auto val = trim(token.substr(eq + 1));
        int v = 0;
        if (!parseInt(val, v)) { res.error = "bad number for '" + std::string(key) + "'"; return res; }
        if (iequals(key, "ch")) {
            if (seenCh) { res.error = "duplicate key 'ch'"; return res; }
            seenCh = true;
            if (!m.setChannel(v)) { res.error = "channel must be 1-16"; return res; }
        } else if (iequals(key, "len")) {
            if (seenLen) { res.error = "duplicate key 'len'"; return res; }
            seenLen = true;
            if (!m.setNoteLengthMs(static_cast<float>(v))) { res.error = "len must be 5-2000 ms"; return res; }
        } else if (const auto inst = drumInstrumentFromName(key)) {
            const auto k = static_cast<std::size_t>(*inst);
            if (seen[k]) { res.error = "duplicate key '" + std::string(key) + "'"; return res; }
            seen[k] = true;
            if (!m.setNote(*inst, v)) { res.error = "note for '" + std::string(key) + "' must be 0-127"; return res; }
        } else {
            res.error = "unknown key '" + std::string(key) + "'";
            return res;
        }
    }
    res.ok = true;
    res.map = m;
    return res;
}


} // namespace robodrummer
