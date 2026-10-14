#include "core/FillGenerator.h"
#include "plugin/JamEngine.h"
#include <array>
#include <cassert>
int main() {
    using namespace robodrummer;
    assert(FillGenerator::lengthBeats(FillLength::OneBeat, 4) == 1);
    assert(FillGenerator::lengthBeats(FillLength::TwoBeats, 4) == 2);
    assert(FillGenerator::lengthBeats(FillLength::OneBar, 4) == 4);
    assert(FillGenerator::lengthBeats(FillLength::OneBar, 7) == 7);
    assert(FillGenerator::lengthBeats(FillLength::TwoBars, 3) == 6);
    assert(!FillGenerator::endsWithCrash(1) && FillGenerator::endsWithCrash(4));

    std::array<FillHit, FillGenerator::MaxHits> hits{};
    FillSettings simple; simple.complexity = 0.0f;
    FillSettings dense; dense.complexity = 1.0f;
    const auto nSimple = FillGenerator::generate(simple, 0.8f, 4, 1, hits);
    for (std::size_t i = 0; i < nSimple; ++i) assert(hits[i].beatOffset >= 0.0 && hits[i].beatOffset < 4.0);
    const auto nDense = FillGenerator::generate(dense, 0.8f, 4, 1, hits);
    assert(nDense > nSimple);
    bool kickDouble = false;
    for (std::size_t i = 0; i < nDense; ++i) kickDouble |= hits[i].instrument == DrumInstrument::Kick;
    assert(kickDouble);
    // Longer fills never overflow the table, even at maximum settings.
    assert(FillGenerator::generate(dense, 1.0f, 24, 7, hits) <= FillGenerator::MaxHits);
    // Surprise changes the voicing but not the number of strikes for the same seed.
    FillSettings wild; wild.surprise = 1.0f;
    std::array<FillHit, FillGenerator::MaxHits> a{}, b{};
    const auto n0 = FillGenerator::generate(FillSettings{}, 0.8f, 2, 5, a);
    const auto n1 = FillGenerator::generate(wild, 0.8f, 2, 5, b);
    assert(n0 == n1);
    int different = 0; for (std::size_t i = 0; i < n0; ++i) different += a[i].instrument != b[i].instrument;
    assert(different > 0);

    auto tomsIn = [](const std::array<DrumEvent, 512>& e, std::size_t n, int from, int to) {
        int count = 0;
        for (std::size_t i = 0; i < n; ++i)
            if (e[i].sampleOffset >= from && e[i].sampleOffset < to &&
                (e[i].instrument == DrumInstrument::HighTom || e[i].instrument == DrumInstrument::MidTom || e[i].instrument == DrumInstrument::FloorTom)) ++count;
        return count;
    };
    std::array<DrumEvent, 512> events{};

    // A one-bar fill requested on a bar line occupies that bar, replaces its groove hats and ends with a crash.
    {
        JamEngine e; e.prepare(48000.0); e.setTempo(120.0); e.setMeter(4, 4);
        FillSettings s; s.length = FillLength::OneBar; e.setFillSettings(s);
        e.requestFill(0.8f);
        std::size_t n = e.processBlock(96000 * 2, events.data(), events.size()); // bars 0 and 1
        assert(tomsIn(events, n, 0, 96000) > 0);
        assert(tomsIn(events, n, 96000, 192000) == 0);
        int hatsInFill = 0, hatsAfter = 0; bool crashAtEnd = false;
        for (std::size_t i = 0; i < n; ++i) {
            hatsInFill += events[i].sampleOffset < 96000 && events[i].instrument == DrumInstrument::ClosedHat;
            hatsAfter += events[i].sampleOffset >= 96000 && events[i].instrument == DrumInstrument::ClosedHat;
            crashAtEnd |= events[i].instrument == DrumInstrument::Crash && events[i].sampleOffset == 96000;
        }
        assert(hatsInFill == 0 && hatsAfter > 0);
        assert(crashAtEnd);
        assert(!e.state().fillRequested);
    }
    // Two-bar fill: tom activity across two bars, then the groove returns.
    {
        JamEngine e; e.prepare(48000.0); e.setTempo(120.0); e.setMeter(4, 4);
        FillSettings s; s.length = FillLength::TwoBars; e.setFillSettings(s);
        e.requestFill(0.8f);
        std::array<DrumEvent, 512> big{};
        const auto n = e.processBlock(96000 * 4, big.data(), big.size());
        assert(tomsIn(big, n, 0, 96000) > 0 && tomsIn(big, n, 96000, 192000) > 0);
        assert(tomsIn(big, n, 192000, 384000) == 0);
        int hatsAfter = 0;
        for (std::size_t i = 0; i < n; ++i) hatsAfter += big[i].sampleOffset >= 192000 && big[i].instrument == DrumInstrument::ClosedHat;
        assert(hatsAfter > 0);
    }
    // Immediate fill starts on the next beat rather than waiting for the bar.
    {
        JamEngine e; e.prepare(48000.0); e.setTempo(120.0); e.setMeter(4, 4);
        (void) e.processBlock(1000, events.data(), events.size());
        e.apply(MidiCommand::ImmediateFill);
        const auto n = e.processBlock(96000, events.data(), events.size());
        assert(tomsIn(events, n, 0, 30000) > 0); // first beat boundary after the request is at 24000 - 1000 = 23000
    }
    // Quantised fill requested mid-bar waits for the final beat of that bar.
    {
        JamEngine e; e.prepare(48000.0); e.setTempo(120.0); e.setMeter(4, 4);
        (void) e.processBlock(1000, events.data(), events.size());
        e.apply(MidiCommand::Fill);
        const auto n = e.processBlock(95000, events.data(), events.size());
        assert(tomsIn(events, n, 0, 70000) == 0);
        assert(tomsIn(events, n, 71000, 95000) > 0);
    }

    // Outro: groove -> two-beat fill -> final crash + kick on a downbeat -> silence until Resume.
    {
        JamEngine e; e.prepare(48000.0); e.setTempo(120.0); e.setMeter(4, 4);
        (void) e.processBlock(10000, events.data(), events.size());      // inside bar 0
        e.apply(MidiCommand::EndJam);
        assert(e.outroPending());
        std::array<DrumEvent, 512> big{};
        const auto n = e.processBlock(96000 * 4, big.data(), big.size()); // 4 s from sample 10000
        // final downbeat is the start of bar 2 = sample 192000 -> offset 182000 in this block
        bool kickAtFinal = false, crashAtFinal = false;
        int hitsAfter = 0, tomsInFill = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (big[i].sampleOffset == 182000 && big[i].instrument == DrumInstrument::Kick) kickAtFinal = true;
            if (big[i].sampleOffset == 182000 && big[i].instrument == DrumInstrument::Crash) crashAtFinal = true;
            if (big[i].sampleOffset > 182000) ++hitsAfter;
            if (big[i].sampleOffset >= 182000 - 48000 && big[i].sampleOffset < 182000 &&
                (big[i].instrument == DrumInstrument::HighTom || big[i].instrument == DrumInstrument::MidTom || big[i].instrument == DrumInstrument::FloorTom)) ++tomsInFill;
        }
        assert(kickAtFinal && crashAtFinal);
        assert(tomsInFill > 0);
        assert(hitsAfter == 0);
        assert(e.state().stopped && !e.outroPending());
        e.apply(MidiCommand::Resume);
        assert(e.processBlock(96000, events.data(), events.size()) > 0);
    }
}
