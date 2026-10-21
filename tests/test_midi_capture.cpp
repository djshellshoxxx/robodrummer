#include "core/MidiCapture.h"
#include "smf_test_parser.h"
#include <atomic>
#include <cassert>
#include <cmath>
#include <limits>
#include <thread>

using namespace robodrummer;

namespace {
constexpr double kSr = 48000.0;
constexpr int kBlock = 500;  // at 120 bpm: 20 ticks exactly

// Drives a capture like the audio thread would: one onBlock per block, one noteAtSample per hit.
template <class Cap>
struct Sim {
    Cap cap;
    double ppq{0.0};
    double bpm{120.0};
    int num{4}, den{4};
    bool playing{true}, hostPpq{true};

    // One block of n samples; hits = sample offsets of kicks inside the block.
    void block(int n, std::initializer_list<int> hits = {}) {
        CaptureBlock b;
        b.hostPpqValid = hostPpq; b.ppq = ppq; b.playing = playing; b.bpm = bpm;
        b.numerator = num; b.denominator = den; b.numSamples = n; b.sampleRate = kSr;
        cap.onBlock(b);
        for (int h : hits) cap.noteAtSample(h, DrumInstrument::Kick, 0.8f);
        ppq += n * bpm / 60.0 / kSr;
    }
    // Plays whole beats (quarter notes * 4/den per meter beat) with a kick on each beat start.
    void playBeats(int beats) {
        const double beatQuarters = 4.0 / den;
        const int samplesPerBeat = static_cast<int>(std::lround(kSr * 60.0 / bpm * beatQuarters));
        for (int i = 0; i < beats; ++i) {
            int done = 0;
            while (done < samplesPerBeat) {
                const int n = std::min(kBlock, samplesPerBeat - done);
                if (done == 0) block(n, {0}); else block(n);
                done += n;
            }
            ppq = std::round(ppq * 960.0) / 960.0;  // keep host PPQ exact like a real host would
        }
    }
    void playBars(int bars) { playBeats(bars * num); }
};
using Cap = MidiCapture<8192>;

smf::File parseExport(Cap& c, const CaptureRequest& r, CaptureResult* out = nullptr) {
    std::vector<std::uint8_t> bytes;
    c.drain();
    const auto res = c.exportSmf(r, bytes);
    if (out) *out = res;
    assert(res.status == CaptureStatus::Ok);
    return smf::parse(bytes);
}
std::vector<smf::Ev> ons(const smf::File& f) {
    std::vector<smf::Ev> v;
    for (auto& e : f.tracks[1].ev) if (e.status == 0x99) v.push_back(e);
    return v;
}
}  // namespace

int main() {
    // ---- empty capture ----
    {
        Sim<Cap> s;
        std::vector<std::uint8_t> b;
        s.cap.drain();
        assert(s.cap.exportSmf({}, b).status == CaptureStatus::Empty && b.empty());
        s.block(kBlock);  // blocks but no hits
        s.cap.drain();
        assert(s.cap.exportSmf({}, b).status == CaptureStatus::Empty && b.empty());
        CaptureRequest bad; bad.bars = -1;
        assert(s.cap.exportSmf(bad, b).status == CaptureStatus::InvalidRequest);
        bad.bars = 4; bad.takeIndex = 5;
        assert(s.cap.exportSmf(bad, b).status == CaptureStatus::InvalidRequest);
    }
    // ---- last N bars, completed-only vs partial, all-since-start ----
    {
        Sim<Cap> s;
        s.playBars(12);
        CaptureRequest r; r.bars = 4;
        CaptureResult res;
        auto f = parseExport(s.cap, r, &res);
        assert(res.bars == 4 && std::fabs(res.seconds - 8.0) < 1e-6);
        assert(f.tracks[0].eotTick == 4 * 3840 && f.tracks[1].eotTick == 4 * 3840);
        auto n = ons(f);
        assert(n.size() == 16);
        for (std::size_t i = 0; i < n.size(); ++i) assert(n[i].tick == i * 960 && n[i].d1 == 36 && n[i].d2 == 102);
        r.bars = 32;  // more than exist -> everything (12 bars)
        f = parseExport(s.cap, r, &res);
        assert(res.bars == 12 && ons(f).size() == 48);
        r.bars = 0;
        f = parseExport(s.cap, r, &res);
        assert(res.bars == 12 && f.tracks[0].eotTick == 12 * 3840);
        // 2 extra beats = partial bar 13
        s.playBeats(2);
        r.bars = 4;
        f = parseExport(s.cap, r, &res);
        assert(res.bars == 4 && ons(f).size() == 16);  // completed bars 9..12
        assert(ons(f)[0].tick == 0);
        r.completedBarsOnly = false;
        f = parseExport(s.cap, r, &res);  // bars 10..13 (13 partial but padded to a full bar)
        assert(res.bars == 4 && f.tracks[0].eotTick == 4 * 3840);
        auto m = ons(f);
        assert(m.size() == 14 && m.back().tick == 3 * 3840 + 960);
    }
    // ---- exact humanized timing preserved (no quantize), velocity mapping/clamping ----
    {
        Sim<Cap> s;
        const double tps = 960.0 * 120.0 / 60.0 / kSr;
        const int offs[] = {0, 1, 7, 33, 101, 499};
        for (int bar = 0; bar < 4; ++bar)
            for (int beat = 0; beat < 4; ++beat) {
                s.block(kBlock, {offs[(bar * 4 + beat) % 6]});
                for (int k = 1; k < 48; ++k) s.block(kBlock);
                s.ppq = std::round(s.ppq * 960.0) / 960.0;
            }
        const auto f = parseExport(s.cap, {});
        const auto n = ons(f);
        assert(n.size() == 16);
        for (std::size_t i = 0; i < n.size(); ++i) {
            const auto expect = static_cast<std::uint64_t>(i * 960 + std::llround(offs[i % 6] * tps));
            assert(n[i].tick == expect);
        }
        // velocity edge cases
        Sim<Cap> v;
        v.block(kBlock);
        v.cap.noteAtSample(0, DrumInstrument::Snare, 0.0f);
        v.cap.noteAtSample(100, DrumInstrument::Snare, 5.0f);
        v.cap.noteAtSample(200, DrumInstrument::Snare, -3.0f);
        v.cap.noteAtSample(300, DrumInstrument::Snare, std::numeric_limits<float>::quiet_NaN());
        v.cap.noteAtSample(-50, DrumInstrument::Clap, 1.0f);   // negative offset -> 0
        assert(!v.cap.noteAtSample(0, static_cast<DrumInstrument>(99), 1.0f));
        v.playBars(1);
        const auto vn = ons(parseExport(v.cap, {}));
        std::vector<int> vel; for (auto& e : vn) if (e.d1 == 38) vel.push_back(e.d2);
        assert((vel == std::vector<int>{1, 127, 1, 64}));
        for (auto& e : vn) if (e.d1 == 39) assert(e.tick == 0 && e.d2 == 127);
    }
    // ---- tempo change map ----
    {
        Sim<Cap> s;
        s.playBars(4);
        s.bpm = 90.0;
        s.playBars(4);
        CaptureRequest r; r.bars = 0;
        CaptureResult res;
        const auto f = parseExport(s.cap, r, &res);
        std::vector<smf::Ev> tm;
        for (auto& e : f.tracks[0].ev) if (e.metaType == 0x51) tm.push_back(e);
        assert(tm.size() == 2 && tm[0].tick == 0 && tm[1].tick == 4 * 3840);
        assert(((tm[0].meta[0] << 16) | (tm[0].meta[1] << 8) | tm[0].meta[2]) == 500000);
        assert(((tm[1].meta[0] << 16) | (tm[1].meta[1] << 8) | tm[1].meta[2]) == 666667);
        assert(res.bars == 8 && std::fabs(res.seconds - (8.0 + 4 * 8.0 / 3.0)) < 1e-6);
        assert(s.cap.takes().size() == 1);  // tempo change does not start a new take
        // Last 4 bars only: starts at 90 bpm, no tempo events inside.
        r.bars = 4;
        const auto g = parseExport(s.cap, r);
        int cnt = 0; for (auto& e : g.tracks[0].ev) if (e.metaType == 0x51) { ++cnt; assert(e.tick == 0 && e.meta[0] == 0x0A); }
        assert(cnt == 1);
    }
    // ---- NaN / garbage tempo and meter inputs are ignored safely ----
    {
        Sim<Cap> s;
        s.playBars(1);
        s.bpm = std::numeric_limits<double>::quiet_NaN();
        s.playBars(1);
        s.bpm = -5.0; s.den = 3; s.num = 1000;
        s.block(kBlock); s.block(0); s.block(-4);
        s.bpm = 120.0; s.den = 4; s.num = 4;
        s.ppq = std::numeric_limits<double>::quiet_NaN();
        s.block(kBlock, {0});
        s.ppq = 2.0 * 3840 / 960;
        s.cap.onBlock({true, 0.0, true, 120.0, 4, 4, 100, 0.0});  // sample rate 0 ignored
        s.cap.onBlock({true, 0.0, true, 120.0, 4, 4, 100, std::numeric_limits<double>::quiet_NaN()});
        s.cap.drain();
        for (auto& t : s.cap.takes()) {
            for (auto& tp : t.tempos) assert(std::isfinite(tp.bpm) && tp.bpm >= 4.0 && tp.bpm <= 960.0);
            for (auto& m : t.meters) assert(m.numerator >= 1 && m.numerator <= 64 && (m.denominator == 4 || m.denominator == 1 || m.denominator == 2 || m.denominator == 8 || m.denominator == 16));
        }
        std::vector<std::uint8_t> b;
        const auto res = s.cap.exportSmf({}, b);
        assert(res.status == CaptureStatus::Ok);
        smf::parse(b);
    }
    // ---- odd meters and meter changes restart the bar grid ----
    {
        Sim<Cap> s;
        s.playBars(2);
        s.num = 7; s.den = 8;
        s.playBars(3);
        CaptureRequest r; r.bars = 3;
        CaptureResult res;
        auto f = parseExport(s.cap, r, &res);
        assert(res.bars == 3 && f.tracks[0].eotTick == 3 * 7 * 480);
        std::vector<smf::Ev> ts;
        for (auto& e : f.tracks[0].ev) if (e.metaType == 0x58) ts.push_back(e);
        assert(ts.size() == 1 && ts[0].tick == 0 && ts[0].meta[0] == 7 && ts[0].meta[1] == 3);
        assert(ons(f).size() == 21);
        r.bars = 0;
        f = parseExport(s.cap, r, &res);
        assert(res.bars == 5 && f.tracks[0].eotTick == 2 * 3840 + 3 * 3360);
        ts.clear();
        for (auto& e : f.tracks[0].ev) if (e.metaType == 0x58) ts.push_back(e);
        assert(ts.size() == 2 && ts[1].tick == 2 * 3840 && ts[1].meta[0] == 7);
        // 5/4, 3/16, 12/8->(8 denominators) variants
        for (auto [nn, dd, len] : {std::tuple{5, 4, 5 * 960}, std::tuple{3, 16, 3 * 240}, std::tuple{12, 8, 12 * 480}, std::tuple{1, 1, 3840}}) {
            Sim<Cap> o; o.num = nn; o.den = dd; o.playBars(3);
            CaptureRequest rr; rr.bars = 2; CaptureResult rs;
            const auto ff = parseExport(o.cap, rr, &rs);
            assert(rs.bars == 2 && ff.tracks[0].eotTick == static_cast<std::uint64_t>(2 * len));
        }
    }
    // ---- take segmentation: stop/start, seek, loop ----
    {
        Sim<Cap> s;
        s.playBars(2);
        s.playing = false; s.block(kBlock); s.block(kBlock);
        s.playing = true; s.ppq = 0.0; s.playBars(2);           // stop/start
        s.cap.drain();
        assert(s.cap.takes().size() == 2);
        s.ppq = 100.0; s.playBars(1);                            // seek forward
        s.ppq = 4.0; s.playBars(1);                              // loop wrap / seek back
        s.cap.drain();
        assert(s.cap.takes().size() == 4);
        assert(s.cap.takes()[2].origin == 100 * 960 && s.cap.takes()[3].origin == 4 * 960);
        CaptureRequest r; r.bars = 0; r.takeIndex = 0;
        CaptureResult res; auto f = parseExport(s.cap, r, &res);
        assert(res.bars == 2);
        r.takeIndex = -1; f = parseExport(s.cap, r, &res);
        assert(res.bars == 1);
        // Seek lands mid-bar: the first bar is anchored on the grid (ticksIntoBar) so the file starts on a bar line.
        s.ppq = 8.0 + 1.0; s.playBars(2); s.playBeats(0);
        s.cap.drain();
        assert(s.cap.takes().back().origin - s.cap.takes().back().anchor == 960);
        // Negative ppq (count-in) is not recorded
        Sim<Cap> p; p.ppq = -4.0; p.block(kBlock, {0}); p.cap.drain();
        assert(p.cap.takes().empty());
        // Free-running (no host ppq): ticks advance with samples
        Sim<Cap> q; q.hostPpq = false; q.playBars(2);
        auto qf = parseExport(q.cap, {});
        assert(ons(qf).size() == 8 && ons(qf)[7].tick == 7 * 960);
    }
    // ---- ring wrap-around (tiny ring, indices wrap many times) ----
    {
        Sim<MidiCapture<8>> s;
        std::size_t produced = 0;
        for (int i = 0; i < 20000; ++i) {
            s.block(kBlock);
            for (int k = 0; k < 3; ++k) { if (s.cap.noteAtSample(k, DrumInstrument::Snare, 0.5f)) ++produced; }
            if (i % 2 == 0) s.cap.drain();
        }
        s.cap.drain();
        std::size_t seen = 0; for (auto& t : s.cap.takes()) seen += t.notes.size();
        assert(produced == seen && s.cap.droppedCount() == 0 && produced == 60000);
        assert(s.cap.takes().size() == 1);
        for (auto& t : s.cap.takes()) for (std::size_t i = 1; i < t.notes.size(); ++i) assert(t.notes[i].tick >= t.notes[i-1].tick);
    }
    // ---- overflow: newest dropped, counted, fresh take started, maps stay consistent ----
    {
        Sim<MidiCapture<16>> s;
        s.block(kBlock);
        bool refused = false;
        for (int i = 0; i < 40; ++i) refused |= !s.cap.noteAtSample(i, DrumInstrument::Kick, 1.0f);
        assert(refused && s.cap.droppedCount() > 0);
        s.cap.drain();
        assert(s.cap.takes().size() == 1 && s.cap.takes()[0].notes.size() == 13);  // 16 slots - segment - tempo - meter
        s.block(kBlock, {0});  // resync -> new take with its own tempo/meter
        s.cap.drain();
        assert(s.cap.takes().size() == 2 && s.cap.takes()[1].tempos.size() == 1 && s.cap.takes()[1].meters.size() == 1);
        std::vector<std::uint8_t> b;
        assert(s.cap.exportSmf({}, b).status == CaptureStatus::Ok);
        smf::parse(b);
        // Ring so small the maps themselves do not fit: never crashes, keeps retrying.
        Sim<MidiCapture<8>> t;
        for (int i = 0; i < 100; ++i) { t.block(kBlock); for (int k = 0; k < 20; ++k) t.cap.noteAtSample(k, DrumInstrument::Kick, 1.f); if (i % 7 == 0) t.cap.drain(); }
        t.cap.drain();
        std::vector<std::uint8_t> bb; (void)t.cap.exportSmf({}, bb);
    }
    // ---- 10 minute cap: oldest bars dropped on a bar line, tempo carried ----
    {
        Sim<Cap> s;
        s.bpm = 120.0;
        // 4 beats per block, one kick per bar. 100 bars at 120 + 700 bars at 240 = 900 s.
        for (int bar = 0; bar < 800; ++bar) {
            if (bar == 100) s.bpm = 240.0;  // tempo change inside the retained region
            const int n = static_cast<int>(kSr * 60.0 / s.bpm * 4.0);
            s.block(n, {0});
            s.ppq = std::round(s.ppq * 960.0) / 960.0;
            if (bar % 10 == 0) s.cap.drain();
        }
        s.cap.drain();
        const auto& t = s.cap.takes().back();
        assert(t.truncated && t.origin == t.anchor && t.origin % 3840 == 0);
        CaptureRequest r; r.bars = 0; CaptureResult res;
        const auto f = parseExport(s.cap, r, &res);
        assert(res.seconds <= kMaxCaptureSeconds + 1e-6 && res.seconds > kMaxCaptureSeconds - 5.0);
        assert(f.tracks[0].ev.size() > 0);
        // Retained region is entirely at 240 bpm after the cut + carried tempo at tick 0
        int tempos = 0; for (auto& e : f.tracks[0].ev) if (e.metaType == 0x51) { ++tempos; assert(e.tick == 0 && e.meta[0] == 0x03 && e.meta[1] == 0xD0 && e.meta[2] == 0x90); }
        assert(tempos == 1);
    }
    // ---- clear ----
    {
        Sim<Cap> s;
        s.playBars(2);
        s.cap.drain();
        assert(!s.cap.takes().empty());
        const auto v0 = s.cap.version();
        s.cap.requestClear();
        assert(s.cap.takes().empty() && s.cap.version() != v0);
        s.playBars(1);   // producer sees the clear -> fresh take
        s.cap.drain();
        assert(s.cap.takes().size() == 1 && s.cap.takes()[0].notes.size() == 4);
        std::vector<std::uint8_t> b;
        assert(s.cap.exportSmf({}, b).status == CaptureStatus::Ok);
    }
    // ---- custom drum map ----
    {
        Sim<Cap> s;
        s.playBars(1);
        s.cap.noteAtSample(5, DrumInstrument::Snare, 0.5f);
        CaptureRequest r; r.map.set(DrumInstrument::Kick, 60); r.map.set(DrumInstrument::Snare, -1);
        const auto f = parseExport(s.cap, r);
        for (auto& e : ons(f)) assert(e.d1 == 60);
        MidiExportData d; assert(s.cap.buildExport(r, d).status == CaptureStatus::Ok);
        assert(d.drumMapText == "Drum map: 60=Kick");
        r.map.set(DrumInstrument::Kick, -1);
        assert(s.cap.buildExport(r, d).status == CaptureStatus::Empty);  // everything unmapped
    }
    // ---- two-thread SPSC stress (tiny ring forces constant contention) ----
    {
        MidiCapture<64> cap;
        std::atomic<bool> done{false};
        std::atomic<std::size_t> accepted{0};
        std::thread prod([&] {
            double ppq = 0.0; std::size_t acc = 0;
            for (int i = 0; i < 100000; ++i) {
                CaptureBlock b{true, ppq, true, 120.0, 4, 4, kBlock, kSr};
                cap.onBlock(b);
                for (int k = 0; k < 3; ++k) acc += cap.noteAtSample(k * 10, DrumInstrument::Kick, 0.7f) ? 1 : 0;
                ppq += kBlock * 120.0 / 60.0 / kSr;
            }
            accepted = acc; done = true;
        });
        while (!done.load()) cap.drain();
        prod.join();
        cap.drain();
        std::size_t seen = 0;
        for (auto& t : cap.takes()) {
            seen += t.notes.size();
            for (std::size_t i = 1; i < t.notes.size(); ++i) assert(t.notes[i].tick >= t.notes[i-1].tick);
        }
        // Takes beyond kMaxTakes are evicted, so seen <= accepted; every accepted note is in a take or evicted.
        assert(seen <= accepted.load() && cap.takes().size() <= kMaxTakes);
    }
    // ---- soak: 300k hits through a normal ring with periodic drain (also exercises the note-count cap) ----
    {
        Sim<Cap> s;
        for (int i = 0; i < 100000; ++i) {
            s.block(kBlock, {0, 100, 200});
            if (i % 50 == 0) s.cap.drain();
        }
        s.cap.drain();
        assert(s.cap.droppedCount() == 0);
        for (auto& t : s.cap.takes()) assert(t.notes.size() <= kMaxNotesPerTake);
        std::vector<std::uint8_t> b;
        assert(s.cap.exportSmf({}, b).status == CaptureStatus::Ok);
        smf::parse(b);
    }
    // ---- spec 05 acceptance: 1M-event soak (334k blocks x 3 hits), bounded memory, no drops, still exports ----
    {
        Sim<Cap> s;
        std::size_t events = 0;
        for (int i = 0; i < 334000; ++i) {
            s.block(kBlock, {0, 100, 200});
            events += 3;
            if (i % 50 == 0) s.cap.drain();
        }
        s.cap.drain();
        assert(events >= 1000000);
        assert(s.cap.droppedCount() == 0);
        assert(s.cap.takes().size() <= kMaxTakes);
        for (auto& t : s.cap.takes()) assert(t.notes.size() <= kMaxNotesPerTake);
        std::vector<std::uint8_t> b;
        assert(s.cap.exportSmf({}, b).status == CaptureStatus::Ok);
        smf::parse(b);
    }
    return 0;
}
