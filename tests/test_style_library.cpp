#include "core/GrooveGenerator.h"
#include "core/StyleLibrary.h"
#include <array>
#include <cassert>
#include <cstring>
int main() {
    using namespace robodrummer;
    assert(kJamStyleCount == 22);
    assert(std::strcmp(kJamStyleNames[0], "Rock") == 0);
    assert(static_cast<int>(JamStyle::JazzSwing) == kJamStyleCount - 1);

    GrooveGenerator g;
    GrooveContext c;
    c.sampleRate = 48000.0; c.bpm = 120.0; c.intensity = 0.6f;
    std::array<DrumEvent, 128> out{};
    const int barSamples = 96000;
    for (int i = 0; i < kJamStyleCount; ++i) {
        const auto style = styleFor(static_cast<JamStyle>(i));
        assert(style.tempoMinBpm > 0.0f && style.tempoMaxBpm > style.tempoMinBpm);
        std::size_t total = 0;
        for (unsigned seed = 1; seed <= 16; ++seed) {
            c.seed = seed;
            const auto n = g.generateBar(style, c, out);
            total += n;
            for (std::size_t k = 0; k < n; ++k) {
                assert(out[k].sampleOffset >= 0 && out[k].sampleOffset < barSamples);
                assert(out[k].velocity >= 0.0f && out[k].velocity <= 1.0f);
            }
        }
        assert(total > 0);
        // Every style must also survive odd meters without crashing or overrunning the bar.
        for (const auto [num, den] : { std::pair{3, 4}, std::pair{6, 8}, std::pair{5, 4}, std::pair{7, 8} }) {
            auto m = c; m.numerator = num; m.denominator = den; m.sectionStart = true;
            const double spb = 48000.0 * 60.0 / 120.0 * (4.0 / den);
            const auto n = g.generateBar(style, m, out);
            for (std::size_t k = 0; k < n; ++k) assert(out[k].sampleOffset >= 0 && out[k].sampleOffset < static_cast<int>(spb * num + 0.5));
        }
    }

    auto count = [&](const Style& s, DrumInstrument inst, int offset, unsigned seeds = 32) {
        std::size_t hits = 0;
        for (unsigned seed = 1; seed <= seeds; ++seed) {
            c.seed = seed;
            const auto n = g.generateBar(s, c, out);
            for (std::size_t k = 0; k < n; ++k) hits += out[k].instrument == inst && (offset < 0 || out[k].sampleOffset == offset);
        }
        return hits;
    };
    // Disco: four on the floor (always a kick on every beat).
    const auto disco = styleFor(JamStyle::Disco);
    for (int beat = 0; beat < 4; ++beat) assert(count(disco, DrumInstrument::Kick, beat * 24000) == 32);
    assert(count(disco, DrumInstrument::OpenHat, -1) > 32); // offbeat open hats
    // Reggae one drop: nothing on beat one, kick + cross-stick on three.
    const auto reggae = styleFor(JamStyle::Reggae);
    assert(count(reggae, DrumInstrument::Kick, 0) == 0);
    assert(count(reggae, DrumInstrument::Kick, 48000) > 28);
    assert(count(reggae, DrumInstrument::Rimshot, 48000) > 28);
    assert(count(reggae, DrumInstrument::Snare, -1) == 0);
    // Half-time: single backbeat on beat three, none on two or four.
    const auto half = styleFor(JamStyle::HalfTime);
    assert(count(half, DrumInstrument::Snare, 48000) > 28);
    assert(count(half, DrumInstrument::Snare, 24000) == 0);
    assert(count(half, DrumInstrument::Snare, 72000) == 0);
    // Jazz: ride on every beat, hat foot on two and four.
    const auto jazz = styleFor(JamStyle::JazzSwing);
    for (int beat = 0; beat < 4; ++beat) assert(count(jazz, DrumInstrument::Ride, beat * 24000, 4) == 4);
    assert(count(jazz, DrumInstrument::ClosedHat, 24000, 4) == 4);
    // Breakbeat is busier on ghost snares than plain rock.
    assert(count(styleFor(JamStyle::Breakbeat), DrumInstrument::Snare, -1) > count(Style::basicRock(), DrumInstrument::Snare, -1));
    // Complexity scales syncopation for the same style and seeds.
    GrooveContext lo = c, hi = c; lo.complexity = 0.0f; hi.complexity = 1.0f;
    std::size_t loKick = 0, hiKick = 0;
    const auto funk = Style::funk();
    for (unsigned seed = 1; seed <= 200; ++seed) {
        lo.seed = hi.seed = seed;
        auto n = g.generateBar(funk, lo, out); for (std::size_t k = 0; k < n; ++k) loKick += out[k].instrument == DrumInstrument::Kick;
        n = g.generateBar(funk, hi, out); for (std::size_t k = 0; k < n; ++k) hiKick += out[k].instrument == DrumInstrument::Kick;
    }
    assert(hiKick > loKick);
}
