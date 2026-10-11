// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#include "core/JamStyleProfile.h"
#include "plugin/JamEngine.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <vector>

using namespace robodrummer;

static std::vector<DrumEvent> run(JamEngine& e, int blocks, int blockSize, std::vector<long long>* abs = nullptr) {
    std::vector<DrumEvent> all;
    std::array<DrumEvent, 256> buf{};
    for (int b = 0; b < blocks; ++b) {
        const auto n = e.processBlock(blockSize, buf.data(), buf.size());
        for (std::size_t i = 0; i < n; ++i) {
            assert(buf[i].sampleOffset >= 0 && buf[i].sampleOffset < blockSize);
            all.push_back(buf[i]);
            if (abs) abs->push_back(static_cast<long long>(b) * blockSize + buf[i].sampleOffset);
        }
    }
    return all;
}

static bool isTom(DrumInstrument i) {
    return i == DrumInstrument::HighTom || i == DrumInstrument::MidTom || i == DrumInstrument::FloorTom;
}
static int countToms(const std::vector<DrumEvent>& ev) {
    int c = 0;
    for (auto& e : ev) c += isTom(e.instrument);
    return c;
}

int main() {
    // Humanizer: deterministic, bounded, off at amount 0, tighter at fast tempo.
    Humanizer h;
    h.amount = 1.0f; h.seed = 11;
    const Style sloppy = Style::garageRock();
    int maxSlow = 0, maxFast = 0;
    for (std::uint32_t i = 0; i < 200; ++i) {
        DrumEvent a{DrumInstrument::ClosedHat, 5000, 0.6f, 0}, b = a;
        h.bpm = 80.0;
        h.apply(sloppy, a, i, 0, 47999);
        DrumEvent a2{DrumInstrument::ClosedHat, 5000, 0.6f, 0};
        h.apply(sloppy, a2, i, 0, 47999);
        assert(a.sampleOffset == a2.sampleOffset && a.velocity == a2.velocity);
        maxSlow = std::max(maxSlow, std::abs(a.sampleOffset - 5000));
        h.bpm = 220.0;
        h.apply(sloppy, b, i, 0, 47999);
        maxFast = std::max(maxFast, std::abs(b.sampleOffset - 5000));
        assert(a.velocity > 0.0f && a.velocity <= 1.0f);
    }
    assert(maxFast < maxSlow);
    h.bpm = 120.0;
    for (std::uint32_t i = 0; i < 200; ++i) {
        DrumEvent e{DrumInstrument::Snare, 0, 0.9f, 0};
        h.apply(sloppy, e, i, 0, 100);
        assert(e.sampleOffset >= 0 && e.sampleOffset <= 100);
    }
    h.amount = 0.0f;
    DrumEvent same{DrumInstrument::Kick, 777, 0.5f, 0};
    h.apply(sloppy, same, 1, 0, 9999);
    assert(same.sampleOffset == 777 && same.velocity == 0.5f);
    // Reggae snare lays back on average.
    h.amount = 1.0f; h.bpm = 90.0;
    long long shift = 0;
    for (std::uint32_t i = 0; i < 400; ++i) {
        DrumEvent e{DrumInstrument::Snare, 20000, 0.8f, 0};
        h.apply(Style::reggae(), e, i, 0, 47999);
        shift += e.sampleOffset - 20000;
    }
    assert(shift > 0);

    // Engine: humanize is deterministic, in-bounds and actually moves events off the grid.
    JamEngine a;
    a.prepare(48000.0); a.setTempo(120.0); a.setMeter(4, 4); a.setStyle(Style::classicRock());
    a.setHumanize(1.0f);
    std::vector<long long> absB, absC;
    JamEngine b = a;
    JamEngine c = a;
    (void)run(b, 40, 4800, &absB);
    (void)run(c, 40, 4800, &absC);
    assert(!absB.empty() && absB == absC);
    bool anyMoved = false;
    for (long long x : absB) { assert(x >= 0 && x < 40 * 4800); anyMoved |= (x % 6000) != 0; }
    assert(anyMoved);
    JamEngine dry = a;
    dry.setHumanize(0.0f);
    std::vector<long long> absDry;
    (void)run(dry, 40, 4800, &absDry);
    for (long long x : absDry) assert(x % 6000 == 0);

    // Fills: length and complexity.
    auto fillRun = [&](FillLength len, float complexity, bool crash, const Style& style,
                       std::vector<long long>* absOut = nullptr) {
        JamEngine e;
        e.prepare(48000.0); e.setTempo(120.0); e.setMeter(4, 4); e.setStyle(style);
        FillSettings fs;
        fs.length = len; fs.complexity = complexity; fs.crashLanding = crash;
        e.setFillSettings(fs);
        e.requestFill(0.8f);
        return run(e, 40, 4800, absOut); // two 4/4 bars at 120 BPM
    };
    const auto one = fillRun(FillLength::OneBeat, 0.8f, false, Style::basicRock());
    const auto two = fillRun(FillLength::TwoBeats, 0.8f, false, Style::basicRock());
    const auto bar = fillRun(FillLength::OneBar, 0.8f, false, Style::basicRock());
    assert(countToms(one) > 0 && countToms(two) > countToms(one) && countToms(bar) > countToms(two));
    assert(countToms(fillRun(FillLength::OneBar, 0.2f, false, Style::basicRock())) < countToms(bar));

    {
        JamEngine e;
        e.prepare(48000.0); e.setTempo(120.0); e.setMeter(4, 4);
        FillSettings fs;
        fs.length = FillLength::TwoBars;
        e.setFillSettings(fs);
        e.requestFill(0.8f);
        const auto ev = run(e, 120, 4800);
        assert(countToms(ev) > countToms(bar));
        assert(!e.state().fillRequested);
    }

    // Punk fills use kick and snare rather than toms.
    assert(countToms(fillRun(FillLength::OneBar, 0.8f, false, Style::punk())) == 0);

    // Crash landing: a crash on the downbeat after the fill only when enabled.
    Style crashy = Style::basicRock();
    crashy.fillCrashProbability = 1.0f;
    std::vector<long long> ca;
    auto ce = fillRun(FillLength::OneBeat, -1.0f, true, crashy, &ca);
    bool landed = false;
    for (std::size_t i = 0; i < ce.size(); ++i)
        if (ce[i].instrument == DrumInstrument::Crash && ca[i] == 96000) landed = true;
    assert(landed);
    ca.clear();
    ce = fillRun(FillLength::OneBeat, -1.0f, false, crashy, &ca);
    for (std::size_t i = 0; i < ce.size(); ++i)
        assert(!(ce[i].instrument == DrumInstrument::Crash && ca[i] == 96000));

    // The half-time command (time scale) is independent of the HalfTime style.
    JamEngine ht;
    ht.prepare(48000.0); ht.setTempo(120.0); ht.setMeter(4, 4); ht.setStyle(Style::halfTime());
    assert(ht.state().timeScale == 1.0);
    ht.apply(MidiCommand::HalfTime);
    assert(ht.state().timeScale == 0.5);
    ht.apply(MidiCommand::HalfTime);
    assert(ht.state().timeScale == 1.0);

    // Frequency scale combines user and style multipliers.
    JamEngine f;
    f.setStyle(Style::reggae());
    f.setFillFrequency(1.0f);
    assert(f.fillFrequencyScale() < 1.0f);
}
