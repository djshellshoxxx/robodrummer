// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#include "core/GrooveGenerator.h"
#include "core/JamStyleProfile.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>

using namespace robodrummer;

static int countAt(const std::array<DrumEvent, 128>& ev, std::size_t n, DrumInstrument inst, double beat, double spb) {
    int c = 0;
    for (std::size_t i = 0; i < n; ++i)
        if (ev[i].instrument == inst && std::abs(ev[i].sampleOffset - beat * spb) < 2.0) ++c;
    return c;
}
static int total(const std::array<DrumEvent, 128>& ev, std::size_t n, DrumInstrument inst) {
    int c = 0;
    for (std::size_t i = 0; i < n; ++i) c += ev[i].instrument == inst;
    return c;
}

int main() {
    // Existing indices are frozen (host parameter indices depend on them).
    assert(static_cast<int>(JamStyle::Rock) == 0 && static_cast<int>(JamStyle::Shuffle) == 5);
    assert(static_cast<int>(JamStyle::HardRock) == 6 && JamStyleCount == 22);
    assert(std::strcmp(jamStyleName(JamStyle::HalfTime), "Half-Time") == 0);
    for (int i = 0; i < JamStyleCount; ++i) {
        assert(jamStyleName(static_cast<JamStyle>(i))[0] != '\0');
        for (int j = i + 1; j < JamStyleCount; ++j)
            assert(std::strcmp(jamStyleName(static_cast<JamStyle>(i)), jamStyleName(static_cast<JamStyle>(j))) != 0);
        const auto profile = JamStyleProfile::forStyle(static_cast<JamStyle>(i));
        assert(profile.style == static_cast<JamStyle>(i));
        assert(profile.phraseBars >= 4);
    }
    // Every style differs from every other in at least one field the generator reads.
    auto sig = [](const Style& s) {
        return std::array<float, 14>{s.kickBeat1, s.kickBeat3, s.extraKickProbability, s.snareBackbeat, s.closedHatProbability,
            s.humanizeMs, s.openHatProbability, s.rideProbability, s.ghostSnareProbability, s.swing,
            s.syncopation, s.offbeatOpenHat, s.snareLaybackMs,
            static_cast<float>(s.fourOnFloor + 2 * s.oneDrop + 4 * s.halfTimeSnare + 8 * s.jazzRide + 16 * s.trainBeat)};
    };
    for (int i = 0; i < JamStyleCount; ++i)
        for (int j = i + 1; j < JamStyleCount; ++j)
            assert(sig(styleForJamStyle(static_cast<JamStyle>(i))) != sig(styleForJamStyle(static_cast<JamStyle>(j))));

    GrooveGenerator g;
    GrooveContext c;
    c.intensity = 0.6f;
    const double spb = 24000.0;
    std::array<DrumEvent, 128> ev{};

    // Reggae one-drop: nothing on beat 2, kick + backbeat together on beat 3.
    c.seed = 5;
    auto n = g.generateBar(Style::reggae(), c, ev);
    assert(countAt(ev, n, DrumInstrument::Snare, 1, spb) + countAt(ev, n, DrumInstrument::Sidestick, 1, spb) == 0);
    assert(countAt(ev, n, DrumInstrument::Snare, 2, spb) + countAt(ev, n, DrumInstrument::Sidestick, 2, spb) == 1);
    assert(countAt(ev, n, DrumInstrument::Kick, 2, spb) == 1);

    // Disco: four on the floor plus open hats on the offbeats.
    int openHats = 0;
    for (unsigned seed = 1; seed <= 16; ++seed) {
        c.seed = seed;
        n = g.generateBar(Style::disco(), c, ev);
        for (int b = 0; b < 4; ++b) assert(countAt(ev, n, DrumInstrument::Kick, b, spb) == 1);
        openHats += total(ev, n, DrumInstrument::OpenHat);
    }
    assert(openHats > 16 * 4 * 0.6);

    // HalfTime style: single snare on beat 3, none on 2 and 4.
    c.seed = 9;
    n = g.generateBar(Style::halfTime(), c, ev);
    assert(countAt(ev, n, DrumInstrument::Snare, 2, spb) == 1);
    assert(countAt(ev, n, DrumInstrument::Snare, 1, spb) + countAt(ev, n, DrumInstrument::Snare, 3, spb) == 0);

    // Jazz: swung ride on every beat with late skip notes, feathered quiet kick.
    n = g.generateBar(Style::jazzSwing(), c, ev);
    for (int b = 0; b < 4; ++b) assert(countAt(ev, n, DrumInstrument::Ride, b, spb) == 1);
    int skips = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (ev[i].instrument == DrumInstrument::Ride && ev[i].sampleOffset % 24000 != 0) {
            ++skips;
            assert(ev[i].sampleOffset % 24000 > 15000); // swung, not straight 12000
        }
        if (ev[i].instrument == DrumInstrument::Kick) assert(ev[i].velocity < 0.5f);
    }
    assert(skips == 2);

    // Breakbeat is more syncopated than rock.
    auto offbeatKicks = [&](const Style& s) {
        int count = 0;
        for (unsigned seed = 1; seed <= 64; ++seed) {
            c.seed = seed;
            const auto m = g.generateBar(s, c, ev);
            for (std::size_t i = 0; i < m; ++i)
                if (ev[i].instrument == DrumInstrument::Kick && ev[i].sampleOffset % 24000 != 0) ++count;
        }
        return count;
    };
    assert(offbeatKicks(Style::breakbeat()) > offbeatKicks(Style::basicRock()));

    // Complexity: low thins, high enriches; 0.5 is identical to the default context.
    auto eventTotal = [&](float complexity) {
        int count = 0;
        c.complexity = complexity;
        for (unsigned seed = 1; seed <= 64; ++seed) { c.seed = seed; count += static_cast<int>(g.generateBar(Style::basicRock(), c, ev)); }
        return count;
    };
    assert(eventTotal(0.0f) < eventTotal(0.5f));
    assert(eventTotal(1.0f) > eventTotal(0.5f));
    c.complexity = 0.5f; c.seed = 3;
    const auto base = g.generateBar(Style::basicRock(), c, ev);
    GrooveContext c2; c2.intensity = 0.6f; c2.seed = 3;
    std::array<DrumEvent, 128> ev2{};
    assert(g.generateBar(Style::basicRock(), c2, ev2) == base);
    for (std::size_t i = 0; i < base; ++i)
        assert(ev[i].sampleOffset == ev2[i].sampleOffset && ev[i].instrument == ev2[i].instrument);

    // Brain settings scale with fill frequency.
    const auto p = JamStyleProfile::forStyle(JamStyle::Rock);
    assert(p.toBrainSettings(1.0f).minBarsBeforeFill == p.toBrainSettings().minBarsBeforeFill);
    assert(p.toBrainSettings(2.0f).minBarsBeforeFill < p.toBrainSettings(1.0f).minBarsBeforeFill);
    assert(p.toBrainSettings(0.0f).minBarsBeforeFill > 1000);
}
