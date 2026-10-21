// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include "core/JamStyleProfile.h"
#include "core/Style.h"

namespace robodrummer {

[[nodiscard]] inline Style styleFor(JamStyle style) noexcept {
    switch (style) {
        case JamStyle::Rock: return Style::basicRock();
        case JamStyle::Blues: return Style::blues();
        case JamStyle::Funk: return Style::funk();
        case JamStyle::Punk: return Style::punk();
        case JamStyle::Metal: return Style::metal();
        case JamStyle::Shuffle: return Style::shuffle();
        case JamStyle::ClassicRock: return Style::classicRock();
        case JamStyle::HardRock: return Style::hardRock();
        case JamStyle::Alternative: return Style::alternative();
        case JamStyle::Grunge: return Style::grunge();
        case JamStyle::Soul: return Style::soul();
        case JamStyle::Pop: return Style::pop();
        case JamStyle::Indie: return Style::indie();
        case JamStyle::GarageRock: return Style::garageRock();
        case JamStyle::Country: return Style::country();
        case JamStyle::Reggae: return Style::reggae();
        case JamStyle::Disco: return Style::disco();
        case JamStyle::ElectronicRock: return Style::electronicRock();
        case JamStyle::Breakbeat: return Style::breakbeat();
        case JamStyle::HalfTime: return Style::halfTime();
        case JamStyle::Experimental: return Style::experimental();
        case JamStyle::JazzSwing: return Style::jazzSwing();
    }
    return Style::basicRock();
}

} // namespace robodrummer
