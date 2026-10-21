// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include "core/DrumEvent.h"
#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace robodrummer {

struct KitFileInfo {
    DrumInstrument instrument{DrumInstrument::Kick};
    int layer{0};        // velocity layer, 0-based (0 when the name has no _vN tag)
    int roundRobin{0};   // round-robin variant, 0-based (0 when the name has no _rrN tag)
};

// Parses user-kit file names such as "kick.wav", "Snare_v2_rr3.aiff" or "closed-hat_V1".
// _vN selects the velocity layer (1-4) and _rrN the round-robin variant (1-4). Returns nullopt for names that
// do not identify a drum instrument.
[[nodiscard]] inline std::optional<KitFileInfo> parseKitFileName(const std::string& stem) {
    std::vector<std::string> tokens;
    std::string current;
    for (const char raw : stem) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(raw)));
        if (c == '_' || c == '-' || c == ' ' || c == '.') {
            if (!current.empty()) tokens.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) tokens.push_back(current);

    KitFileInfo info;
    std::string name;
    for (const auto& token : tokens) {
        const auto numericTail = [&](std::size_t prefixLength) -> int {
            if (token.size() != prefixLength + 1 || !std::isdigit(static_cast<unsigned char>(token.back()))) return -1;
            return token.back() - '0';
        };
        if (token.rfind("rr", 0) == 0 && numericTail(2) >= 1) { info.roundRobin = numericTail(2) - 1; continue; }
        if (token.size() == 2 && token[0] == 'v' && numericTail(1) >= 1) { info.layer = numericTail(1) - 1; continue; }
        name += token;
    }

    struct Alias { const char* name; DrumInstrument instrument; };
    static const Alias aliases[] = {
        {"kick", DrumInstrument::Kick}, {"bd", DrumInstrument::Kick}, {"bassdrum", DrumInstrument::Kick},
        {"snare", DrumInstrument::Snare}, {"sd", DrumInstrument::Snare},
        {"closedhat", DrumInstrument::ClosedHat}, {"hatclosed", DrumInstrument::ClosedHat}, {"hihatclosed", DrumInstrument::ClosedHat},
        {"hihat", DrumInstrument::ClosedHat}, {"hat", DrumInstrument::ClosedHat}, {"hh", DrumInstrument::ClosedHat},
        {"chh", DrumInstrument::ClosedHat}, {"closedhihat", DrumInstrument::ClosedHat},
        {"openhat", DrumInstrument::OpenHat}, {"hatopen", DrumInstrument::OpenHat}, {"hihatopen", DrumInstrument::OpenHat},
        {"ohh", DrumInstrument::OpenHat}, {"openhihat", DrumInstrument::OpenHat},
        {"ride", DrumInstrument::Ride}, {"crash", DrumInstrument::Crash}, {"crash1", DrumInstrument::Crash},
        {"hightom", DrumInstrument::HighTom}, {"tom1", DrumInstrument::HighTom}, {"hitom", DrumInstrument::HighTom}, {"racktom", DrumInstrument::HighTom},
        {"midtom", DrumInstrument::MidTom}, {"tom2", DrumInstrument::MidTom},
        {"floortom", DrumInstrument::FloorTom}, {"tom3", DrumInstrument::FloorTom}, {"lowtom", DrumInstrument::FloorTom}, {"floor", DrumInstrument::FloorTom},
        {"rimshot", DrumInstrument::Rimshot}, {"rim", DrumInstrument::Rimshot},
        {"sidestick", DrumInstrument::Sidestick}, {"stick", DrumInstrument::Sidestick}, {"xstick", DrumInstrument::Sidestick},
        {"china", DrumInstrument::China}, {"splash", DrumInstrument::Splash}, {"cowbell", DrumInstrument::Cowbell},
        {"tambourine", DrumInstrument::Tambourine}, {"tamb", DrumInstrument::Tambourine}, {"clap", DrumInstrument::Clap},
    };
    for (const auto& alias : aliases) {
        if (name == alias.name) { info.instrument = alias.instrument; return info; }
    }
    return std::nullopt;
}

} // namespace robodrummer
