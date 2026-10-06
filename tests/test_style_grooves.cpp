#include "core/GrooveGenerator.h"
#include <array>
#include <cassert>

int main() {
    using namespace robodrummer;
    GrooveGenerator g;
    GrooveContext c;
    c.seed = 17;
    c.intensity = 0.7f;
    std::array<DrumEvent, 128> out{};

    const auto rock = Style::basicRock();
    const auto funk = Style::funk();
    const auto blues = Style::blues();
    const auto punk = Style::punk();
    const auto shuffle = Style::shuffle();

    assert(funk.extraKickProbability > blues.extraKickProbability);
    assert(punk.crashOnSectionStart > blues.crashOnSectionStart);
    assert(shuffle.swing > rock.swing);

    // Shuffle's second hat subdivision should land later than straight eighths.
    auto n = g.generateBar(shuffle, c, out);
    int firstHat = -1;
    int secondHat = -1;
    for (std::size_t i = 0; i < n; ++i) {
        if (out[i].instrument == DrumInstrument::ClosedHat) {
            if (firstHat < 0) firstHat = out[i].sampleOffset;
            else { secondHat = out[i].sampleOffset; break; }
        }
    }
    assert(firstHat == 0);
    assert(secondHat > 12000); // straight eighth at 120 BPM/48 kHz would be 12000 samples

    // Across deterministic seeds, funk should produce more syncopated kick activity than blues.
    std::size_t funkKicks = 0;
    std::size_t bluesKicks = 0;
    for (unsigned seed = 1; seed <= 128; ++seed) {
        c.seed = seed;
        n = g.generateBar(funk, c, out);
        for (std::size_t i = 0; i < n; ++i)
            funkKicks += out[i].instrument == DrumInstrument::Kick;

        n = g.generateBar(blues, c, out);
        for (std::size_t i = 0; i < n; ++i)
            bluesKicks += out[i].instrument == DrumInstrument::Kick;
    }
    assert(funkKicks > bluesKicks);
}
