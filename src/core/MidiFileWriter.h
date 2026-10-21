#pragma once
// Pure Standard MIDI File (type 1) builder. Message-thread use only (allocates). See docs/specs/05-midi-capture.md.
#include "DrumEvent.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace robodrummer {

inline constexpr int kMidiPpq = 960;

// DrumInstrument -> MIDI note (0..127), -1 = do not export that instrument.
struct DrumMap {
    std::array<std::int16_t, 16> note{};
    [[nodiscard]] static DrumMap gm() noexcept {  // identical to RoboDrummerAudioProcessor::midiNoteFor
        DrumMap m;
        m.note = {36, 38, 42, 46, 51, 49, 50, 47, 43, 37, 37, 52, 55, 56, 54, 39};
        return m;
    }
    [[nodiscard]] int noteFor(DrumInstrument i) const noexcept {
        const auto idx = static_cast<std::size_t>(i);
        if (idx >= note.size()) return -1;
        const int n = note[idx];
        return (n >= 0 && n <= 127) ? n : -1;
    }
    void set(DrumInstrument i, int n) noexcept {
        const auto idx = static_cast<std::size_t>(i);
        if (idx < note.size()) note[idx] = static_cast<std::int16_t>((n >= 0 && n <= 127) ? n : -1);
    }
};

[[nodiscard]] inline const char* drumInstrumentName(DrumInstrument i) noexcept {
    switch (i) {
        case DrumInstrument::Kick: return "Kick";
        case DrumInstrument::Snare: return "Snare";
        case DrumInstrument::ClosedHat: return "Closed Hat";
        case DrumInstrument::OpenHat: return "Open Hat";
        case DrumInstrument::Ride: return "Ride";
        case DrumInstrument::Crash: return "Crash";
        case DrumInstrument::HighTom: return "High Tom";
        case DrumInstrument::MidTom: return "Mid Tom";
        case DrumInstrument::FloorTom: return "Floor Tom";
        case DrumInstrument::Rimshot: return "Rimshot";
        case DrumInstrument::Sidestick: return "Sidestick";
        case DrumInstrument::China: return "China";
        case DrumInstrument::Splash: return "Splash";
        case DrumInstrument::Cowbell: return "Cowbell";
        case DrumInstrument::Tambourine: return "Tambourine";
        case DrumInstrument::Clap: return "Clap";
    }
    return "?";
}

struct ExportNote {
    std::int64_t tick{0};       // onset, >= 0, relative to file start
    std::uint32_t duration{1};  // ticks, 0 treated as 1
    std::uint8_t note{36};      // 0..127
    std::uint8_t velocity{100}; // clamped to 1..127 on write
    std::int8_t  instrument{-1}; // optional DrumInstrument (for the "Drum map" text); -1 unknown
};
struct ExportTempo { std::int64_t tick{0}; double bpm{120.0}; };
struct ExportMeter { std::int64_t tick{0}; int numerator{4}; int denominator{4}; };

struct MidiExportData {
    std::vector<ExportNote> notes;
    std::vector<ExportTempo> tempos;
    std::vector<ExportMeter> meters;
    std::int64_t lengthTicks{0};  // end-of-track is placed at max(lengthTicks, last note-off)
    std::string drumMapText;      // optional, written as Text meta on the drum track
};

enum class MidiWriteResult { Ok, InvalidTempo, InvalidMeter, TickOutOfRange };

namespace smf_detail {
inline constexpr std::int64_t kMaxVlq = 0x0FFFFFFF;
inline void put16(std::vector<std::uint8_t>& o, unsigned v) { o.push_back(static_cast<std::uint8_t>(v >> 8)); o.push_back(static_cast<std::uint8_t>(v)); }
inline void put32(std::vector<std::uint8_t>& o, std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) o.push_back(static_cast<std::uint8_t>(v >> s)); }
inline void putVlq(std::vector<std::uint8_t>& o, std::uint32_t v) {  // v <= 0x0FFFFFFF
    std::uint8_t b[4]; int n = 0;
    b[n++] = v & 0x7F;
    while ((v >>= 7) != 0) b[n++] = static_cast<std::uint8_t>((v & 0x7F) | 0x80);
    while (n > 0) o.push_back(b[--n]);
}
inline void putMetaText(std::vector<std::uint8_t>& o, std::uint32_t delta, std::uint8_t type, const std::string& s) {
    const auto len = std::min<std::size_t>(s.size(), 255);
    putVlq(o, delta); o.push_back(0xFF); o.push_back(type); putVlq(o, static_cast<std::uint32_t>(len));
    for (std::size_t i = 0; i < len; ++i) o.push_back(static_cast<std::uint8_t>(s[i] & 0x7F));  // keep ASCII
}
inline void endTrack(std::vector<std::uint8_t>& file, const std::vector<std::uint8_t>& body) {
    file.insert(file.end(), {'M', 'T', 'r', 'k'});
    put32(file, static_cast<std::uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
}
struct Ev { std::int64_t tick; int order; std::uint8_t a, b; };  // order: 0 = note-off, 1 = note-on
}  // namespace smf_detail

// Builds a complete SMF into `out` (cleared first; left empty on error).
[[nodiscard]] inline MidiWriteResult writeStandardMidiFile(const MidiExportData& in, std::vector<std::uint8_t>& out) {
    using namespace smf_detail;
    out.clear();

    // ---- validate + normalize tempo map ----
    std::vector<ExportTempo> tempos = in.tempos;
    for (const auto& t : tempos) {
        if (!std::isfinite(t.bpm) || t.bpm <= 0.0) return MidiWriteResult::InvalidTempo;
        if (t.tick < 0 || t.tick > kMaxVlq * 4) return MidiWriteResult::TickOutOfRange;
    }
    std::stable_sort(tempos.begin(), tempos.end(), [](auto& a, auto& b) { return a.tick < b.tick; });
    std::vector<ExportMeter> meters = in.meters;
    for (const auto& m : meters) {
        const int d = m.denominator;
        if (m.numerator < 1 || m.numerator > 255 || d < 1 || d > 128 || (d & (d - 1)) != 0) return MidiWriteResult::InvalidMeter;
        if (m.tick < 0 || m.tick > kMaxVlq * 4) return MidiWriteResult::TickOutOfRange;
    }
    std::stable_sort(meters.begin(), meters.end(), [](auto& a, auto& b) { return a.tick < b.tick; });
    if (tempos.empty() || tempos.front().tick > 0) tempos.insert(tempos.begin(), ExportTempo{0, 120.0});
    if (meters.empty() || meters.front().tick > 0) meters.insert(meters.begin(), ExportMeter{0, 4, 4});

    // ---- notes: validate, sort, resolve same-pitch overlaps ----
    std::vector<ExportNote> notes;
    notes.reserve(in.notes.size());
    for (const auto& n : in.notes) {
        if (n.note > 127) continue;
        if (n.tick < 0 || n.tick > kMaxVlq * 4) return MidiWriteResult::TickOutOfRange;
        notes.push_back(n);
    }
    std::stable_sort(notes.begin(), notes.end(), [](auto& a, auto& b) { return a.tick != b.tick ? a.tick < b.tick : a.note < b.note; });
    std::vector<Ev> evs;
    evs.reserve(notes.size() * 2);
    std::array<std::int64_t, 128> nextOn{};  // scan backwards: next onset of the same pitch
    nextOn.fill(-1);
    std::vector<bool> keep(notes.size(), true);
    std::vector<std::int64_t> endTick(notes.size(), 0);
    for (std::size_t k = notes.size(); k-- > 0;) {
        auto& n = notes[k];
        std::int64_t dur = std::max<std::int64_t>(1, n.duration);
        const auto nx = nextOn[n.note];
        if (nx == n.tick) { keep[k] = false; continue; }  // duplicate onset: the later (louder-or-equal handled below)
        if (nx > n.tick) dur = std::min(dur, nx - n.tick);
        endTick[k] = n.tick + dur;
        nextOn[n.note] = n.tick;
    }
    // Duplicates at the same tick/pitch: keep the louder one (scan forward).
    for (std::size_t k = 0; k + 1 < notes.size(); ++k)
        if (!keep[k] && notes[k].tick == notes[k + 1].tick && notes[k].note == notes[k + 1].note)
            notes[k + 1].velocity = std::max(notes[k].velocity, notes[k + 1].velocity);
    std::int64_t lastEnd = std::max<std::int64_t>(0, in.lengthTicks);
    for (std::size_t k = 0; k < notes.size(); ++k) {
        if (!keep[k]) continue;
        const auto v = static_cast<std::uint8_t>(std::clamp<int>(notes[k].velocity, 1, 127));
        evs.push_back({notes[k].tick, 1, notes[k].note, v});
        evs.push_back({endTick[k], 0, notes[k].note, 64});
        lastEnd = std::max(lastEnd, endTick[k]);
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.order != b.order) return a.order < b.order;  // note-offs first
        return a.a < b.a;
    });
    const std::int64_t eot = lastEnd;

    // ---- track 0: conductor ----
    std::vector<std::uint8_t> t0;
    std::int64_t prev = 0;
    auto delta = [&](std::int64_t tick, std::vector<std::uint8_t>& body) -> bool {
        const std::int64_t d = tick - prev;
        if (d < 0 || d > kMaxVlq) return false;
        putVlq(body, static_cast<std::uint32_t>(d));
        prev = tick;
        return true;
    };
    putMetaText(t0, 0, 0x03, "RoboDrummer Tempo Map");
    putMetaText(t0, 0, 0x01, "RoboDrummer capture");
    {
        // Merge meters and tempos in tick order; same-tick duplicates collapse to the last one.
        std::size_t ti = 0, mi = 0;
        while (ti < tempos.size() || mi < meters.size()) {
            const bool takeMeter = mi < meters.size() && (ti >= tempos.size() || meters[mi].tick <= tempos[ti].tick);
            if (takeMeter) {
                const auto& m = meters[mi++];
                if (mi < meters.size() && meters[mi].tick == m.tick) continue;
                if (m.tick > eot) continue;
                if (!delta(m.tick, t0)) return MidiWriteResult::TickOutOfRange;
                int dd = 0; for (int d = m.denominator; d > 1; d >>= 1) ++dd;
                t0.insert(t0.end(), {0xFF, 0x58, 0x04, static_cast<std::uint8_t>(m.numerator), static_cast<std::uint8_t>(dd), 24, 8});
            } else {
                const auto& t = tempos[ti++];
                if (ti < tempos.size() && tempos[ti].tick == t.tick) continue;
                if (t.tick > eot) continue;
                if (!delta(t.tick, t0)) return MidiWriteResult::TickOutOfRange;
                const double us = std::clamp(std::round(60000000.0 / std::clamp(t.bpm, 4.0, 960.0)), 1.0, 16777215.0);
                const auto u = static_cast<std::uint32_t>(us);
                t0.insert(t0.end(), {0xFF, 0x51, 0x03, static_cast<std::uint8_t>(u >> 16), static_cast<std::uint8_t>(u >> 8), static_cast<std::uint8_t>(u)});
            }
        }
        if (!delta(eot, t0)) return MidiWriteResult::TickOutOfRange;
        t0.insert(t0.end(), {0xFF, 0x2F, 0x00});
    }

    // ---- track 1: drums, channel 10, explicit status bytes (no running status) ----
    std::vector<std::uint8_t> t1;
    putMetaText(t1, 0, 0x03, "RoboDrummer Drums");
    putMetaText(t1, 0, 0x04, "Drums");
    if (!in.drumMapText.empty()) putMetaText(t1, 0, 0x01, in.drumMapText);
    prev = 0;
    for (const auto& e : evs) {
        if (!delta(e.tick, t1)) return MidiWriteResult::TickOutOfRange;
        t1.push_back(e.order == 1 ? 0x99 : 0x89);
        t1.push_back(e.a);
        t1.push_back(e.b);
    }
    if (!delta(eot, t1)) return MidiWriteResult::TickOutOfRange;
    t1.insert(t1.end(), {0xFF, 0x2F, 0x00});

    out.insert(out.end(), {'M', 'T', 'h', 'd'});
    put32(out, 6); put16(out, 1); put16(out, 2); put16(out, kMidiPpq);
    endTrack(out, t0);
    endTrack(out, t1);
    return MidiWriteResult::Ok;
}

}  // namespace robodrummer
