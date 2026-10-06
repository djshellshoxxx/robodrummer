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
    const auto disco = Style::disco();
    const auto reggae = Style::reggae();
    const auto halfTime = Style::halfTime();
    const auto breakbeat = Style::breakbeat();

    assert(funk.extraKickProbability > blues.extraKickProbability);
    assert(punk.crashOnSectionStart > blues.crashOnSectionStart);
    assert(shuffle.swing > rock.swing);
    assert(disco.fourOnFloorKick);
    assert(reggae.oneDrop);
    assert(halfTime.halfTimeBackbeat);
    assert(breakbeat.hatHitsPerBeat == 4);

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
    assert(secondHat > 12000);

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

    // Disco supplies a deterministic kick anchor on all four quarter-note beats.
    c.seed = 42;
    n = g.generateBar(disco, c, out);
    bool discoKick[4]{};
    for (std::size_t i = 0; i < n; ++i) {
        if (out[i].instrument != DrumInstrument::Kick)
            continue;
        const int beat = out[i].sampleOffset / 24000;
        if (beat >= 0 && beat < 4 && out[i].sampleOffset % 24000 == 0)
            discoKick[beat] = true;
    }
    assert(discoKick[0] && discoKick[1] && discoKick[2] && discoKick[3]);

    // Half-time moves the backbeat to beat 3 (zero-based beat index 2).
    c.seed = 11;
    n = g.generateBar(halfTime, c, out);
    bool snareBeat2 = false;
    bool snareBeat3 = false;
    bool snareBeat4 = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (out[i].instrument != DrumInstrument::Snare)
            continue;
        if (out[i].sampleOffset == 24000) snareBeat2 = true;
        if (out[i].sampleOffset == 48000) snareBeat3 = true;
        if (out[i].sampleOffset == 72000) snareBeat4 = true;
    }
    assert(!snareBeat2 && snareBeat3 && !snareBeat4);

    // Reggae one-drop anchors kick/snare together on beat 3.
    c.seed = 23;
    n = g.generateBar(reggae, c, out);
    bool oneDropKick = false;
    bool oneDropSnare = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (out[i].sampleOffset != 48000)
            continue;
        oneDropKick |= out[i].instrument == DrumInstrument::Kick;
        oneDropSnare |= out[i].instrument == DrumInstrument::Snare;
    }
    assert(oneDropKick && oneDropSnare);
}
