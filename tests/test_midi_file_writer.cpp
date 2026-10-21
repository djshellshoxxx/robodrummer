#include "core/MidiFileWriter.h"
#include "smf_test_parser.h"
#include <cassert>
#include <cmath>
#include <limits>

using namespace robodrummer;

static std::vector<std::uint8_t> write(const MidiExportData& d, MidiWriteResult expect = MidiWriteResult::Ok) {
    std::vector<std::uint8_t> b;
    const auto r = writeStandardMidiFile(d, b);
    assert(r == expect);
    if (expect != MidiWriteResult::Ok) assert(b.empty());
    return b;
}
static std::uint32_t usOf(const smf::Ev& e) { return (e.meta[0] << 16) | (e.meta[1] << 8) | e.meta[2]; }

int main() {
    // Empty export: valid file, 2 tracks, defaults 120 bpm 4/4, only end-of-track.
    {
        const auto f = smf::parse(write({}));
        assert(f.format == 1 && f.ntrks == 2 && f.division == 960);
        int tempo = 0, ts = 0, notes = 0;
        for (auto& e : f.tracks[0].ev) { tempo += e.metaType == 0x51; ts += e.metaType == 0x58; if (e.metaType == 0x51) assert(usOf(e) == 500000); }
        for (auto& e : f.tracks[1].ev) notes += (e.status & 0xF0) == 0x90;
        assert(tempo == 1 && ts == 1 && notes == 0);
        assert(f.tracks[0].eotTick == 0 && f.tracks[1].eotTick == 0);
    }
    // Round trip with exact non-grid (humanized) ticks, GM channel 10, no running status, off-before-on ordering.
    {
        MidiExportData d;
        d.lengthTicks = 3840;
        d.notes = {{0, 120, 36, 100}, {481, 60, 38, 90}, {481, 60, 42, 80}, {1919, 120, 36, 127}, {960, 120, 36, 70}};
        d.drumMapText = "Drum map: 36=Kick";
        const auto f = smf::parse(write(d));
        const auto& t = f.tracks[1];
        std::vector<smf::Ev> ns;
        for (auto& e : t.ev) if (e.status == 0x99 || e.status == 0x89) ns.push_back(e);
        assert(ns.size() == 10);
        for (std::size_t i = 1; i < ns.size(); ++i) assert(ns[i].tick >= ns[i-1].tick);
        int ons = 0; bool saw481 = false, saw1919 = false;
        for (auto& e : ns) {
            if (e.status == 0x99) { ++ons; assert(e.d2 >= 1 && e.d2 <= 127); }
            if (e.status == 0x99 && e.tick == 481 && e.d1 == 38 && e.d2 == 90) saw481 = true;
            if (e.status == 0x99 && e.tick == 1919 && e.d1 == 36 && e.d2 == 127) saw1919 = true;
        }
        assert(ons == 5 && saw481 && saw1919);
        assert(t.eotTick == 3840 && f.tracks[0].eotTick == 3840);
        bool named = false;
        for (auto& e : t.ev) if (e.metaType == 0x03) named = std::string(e.meta.begin(), e.meta.end()) == "RoboDrummer Drums";
        assert(named);
    }
    // Same tick: note-off of a previous note precedes the note-on; same-pitch overlap shortened; duplicates merged.
    {
        MidiExportData d;
        d.notes = {{0, 960, 36, 100}, {100, 50, 36, 90}, {100, 50, 36, 120}, {500, 0, 38, 50}};
        d.lengthTicks = 1000;
        const auto f = smf::parse(write(d));
        std::vector<smf::Ev> ns;
        for (auto& e : f.tracks[1].ev) if ((e.status & 0xF0) == 0x80 || (e.status & 0xF0) == 0x90) ns.push_back(e);
        // 36 on@0, 36 off@100, 36 on@100 (vel 120, merged), 36 off@150, 38 on@500, 38 off@501
        assert(ns.size() == 6);
        assert(ns[0].tick == 0 && ns[0].status == 0x99);
        assert(ns[1].tick == 100 && ns[1].status == 0x89);
        assert(ns[2].tick == 100 && ns[2].status == 0x99 && ns[2].d2 == 120);
        assert(ns[3].tick == 150 && ns[3].status == 0x89);
        assert(ns[5].tick == 501);
    }
    // Velocity clamp to 1..127.
    {
        MidiExportData d;
        d.notes = {{0, 10, 36, 0}, {100, 10, 38, 255}, {200, 10, 40, 1}};
        const auto f = smf::parse(write(d));
        std::vector<int> v;
        for (auto& e : f.tracks[1].ev) if (e.status == 0x99) v.push_back(e.d2);
        assert((v == std::vector<int>{1, 127, 1}));
    }
    // Tempo map + odd meters, delta times correct.
    {
        MidiExportData d;
        d.notes = {{0, 10, 36, 100}};
        d.tempos = {{0, 120.0}, {1920, 90.0}, {5000, 200.5}};
        d.meters = {{0, 7, 8}, {1920, 5, 4}, {5000, 3, 16}, {6000, 12, 8}};
        d.lengthTicks = 8000;
        const auto f = smf::parse(write(d));
        std::vector<smf::Ev> tm, ts;
        for (auto& e : f.tracks[0].ev) { if (e.metaType == 0x51) tm.push_back(e); if (e.metaType == 0x58) ts.push_back(e); }
        assert(tm.size() == 3 && tm[0].tick == 0 && tm[1].tick == 1920 && tm[2].tick == 5000);
        assert(usOf(tm[0]) == 500000 && usOf(tm[1]) == 666667);
        assert(usOf(tm[2]) == static_cast<std::uint32_t>(std::lround(60000000.0 / 200.5)));
        assert(ts.size() == 4);
        const int nn[] = {7, 5, 3, 12}, dd[] = {3, 2, 4, 3};  // dd = log2(denominator)
        for (int i = 0; i < 4; ++i) assert(ts[i].meta.size() == 4 && ts[i].meta[0] == nn[i] && ts[i].meta[1] == dd[i] && ts[i].meta[2] == 24 && ts[i].meta[3] == 8);
        assert(ts[3].tick == 6000 && f.tracks[0].eotTick == 8000);
    }
    // Tempo/meter entries past the end are dropped; same-tick duplicates collapse to the last.
    {
        MidiExportData d;
        d.lengthTicks = 1000;
        d.tempos = {{0, 100.0}, {0, 150.0}, {2000, 60.0}};
        const auto f = smf::parse(write(d));
        int n = 0;
        for (auto& e : f.tracks[0].ev) if (e.metaType == 0x51) { ++n; assert(usOf(e) == 400000); }
        assert(n == 1);
    }
    // NaN / Inf / zero / negative tempo rejected; bad meters rejected.
    {
        const double bad[] = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), 0.0, -120.0};
        for (double b : bad) { MidiExportData d; d.tempos = {{0, b}}; write(d, MidiWriteResult::InvalidTempo); }
        MidiExportData m; m.meters = {{0, 4, 3}}; write(m, MidiWriteResult::InvalidMeter);
        m.meters = {{0, 0, 4}}; write(m, MidiWriteResult::InvalidMeter);
    }
    // Extreme finite tempos are clamped to a representable value.
    {
        MidiExportData d; d.tempos = {{0, 1e-9}, {10, 1e9}};
        const auto f = smf::parse(write(d));
        for (auto& e : f.tracks[0].ev) if (e.metaType == 0x51) assert(usOf(e) >= 1 && usOf(e) <= 0xFFFFFF);
    }
    // Huge ticks: variable-length quantity boundaries round trip; overflow / negative rejected.
    {
        for (std::int64_t t : {127LL, 128LL, 16383LL, 16384LL, 2097151LL, 2097152LL, 0x0FFFFFFELL}) {
            MidiExportData d; d.notes = {{t, 1, 36, 100}}; d.lengthTicks = t + 1;
            const auto f = smf::parse(write(d));
            bool ok = false;
            for (auto& e : f.tracks[1].ev) if (e.status == 0x99) ok = e.tick == static_cast<std::uint64_t>(t);
            assert(ok);
        }
        MidiExportData d; d.notes = {{0x10000000LL, 1, 36, 100}}; write(d, MidiWriteResult::TickOutOfRange);
        d.notes = {{-1, 1, 36, 100}}; write(d, MidiWriteResult::TickOutOfRange);
        d.notes = {{0, 1, 36, 100}}; d.lengthTicks = 0x7FFFFFFFFFFFFFFFLL; write(d, MidiWriteResult::TickOutOfRange);
        d.lengthTicks = 0; d.notes = {{1LL << 62, 1, 36, 100}}; write(d, MidiWriteResult::TickOutOfRange);
    }
    // Output buffer is cleared on every call; DrumMap behavior.
    {
        std::vector<std::uint8_t> b(100, 7);
        MidiExportData d; d.tempos = {{0, std::numeric_limits<double>::quiet_NaN()}};
        assert(writeStandardMidiFile(d, b) == MidiWriteResult::InvalidTempo && b.empty());
        auto m = DrumMap::gm();
        assert(m.noteFor(DrumInstrument::Kick) == 36 && m.noteFor(DrumInstrument::Clap) == 39 && m.noteFor(DrumInstrument::Crash) == 49);
        m.set(DrumInstrument::Kick, 35); m.set(DrumInstrument::Snare, -1); m.set(DrumInstrument::Ride, 300);
        assert(m.noteFor(DrumInstrument::Kick) == 35 && m.noteFor(DrumInstrument::Snare) == -1 && m.noteFor(DrumInstrument::Ride) == -1);
        assert(m.noteFor(static_cast<DrumInstrument>(200)) == -1);
    }
    // Large file stress: parse back many notes.
    {
        MidiExportData d;
        for (int i = 0; i < 20000; ++i) d.notes.push_back({i * 37LL, 20, static_cast<std::uint8_t>(36 + i % 8), static_cast<std::uint8_t>(1 + i % 127)});
        d.lengthTicks = 20000 * 37;
        const auto f = smf::parse(write(d));
        std::size_t ons = 0; for (auto& e : f.tracks[1].ev) ons += e.status == 0x99;
        assert(ons == 20000);
    }
    return 0;
}
