#pragma once
// Tiny strict SMF parser for tests: asserts on malformed structure, rejects running status.
#include <cassert>
#include <cstdint>
#include <vector>

namespace smf {
struct Ev { std::uint64_t tick; int status; int d1; int d2; int metaType; std::vector<std::uint8_t> meta; };
struct Track { std::vector<Ev> ev; bool eot{false}; std::uint64_t eotTick{0}; };
struct File { int format{-1}, ntrks{0}, division{0}; std::vector<Track> tracks; };

inline std::uint32_t be32(const std::vector<std::uint8_t>& d, std::size_t p) { return (d[p] << 24) | (d[p+1] << 16) | (d[p+2] << 8) | d[p+3]; }

inline File parse(const std::vector<std::uint8_t>& d) {
    File f;
    assert(d.size() >= 14);
    assert(d[0]=='M' && d[1]=='T' && d[2]=='h' && d[3]=='d' && be32(d, 4) == 6);
    f.format = (d[8] << 8) | d[9]; f.ntrks = (d[10] << 8) | d[11]; f.division = (d[12] << 8) | d[13];
    std::size_t p = 14;
    for (int t = 0; t < f.ntrks; ++t) {
        assert(p + 8 <= d.size() && d[p]=='M' && d[p+1]=='T' && d[p+2]=='r' && d[p+3]=='k');
        const std::size_t len = be32(d, p + 4); p += 8;
        const std::size_t end = p + len; assert(end <= d.size());
        Track tr; std::uint64_t tick = 0;
        while (p < end) {
            std::uint32_t delta = 0; int n = 0;
            for (;;) { assert(p < end && n < 4); const auto b = d[p++]; ++n; delta = (delta << 7) | (b & 0x7F); if (!(b & 0x80)) break; }
            tick += delta;
            assert(!tr.eot && "events after end-of-track");
            assert(p < end);
            const int st = d[p++];
            assert(st >= 0x80 && "running status is not allowed");
            Ev e{tick, st, 0, 0, -1, {}};
            if (st == 0xFF) {
                e.metaType = d[p++];
                std::uint32_t l = 0; n = 0;
                for (;;) { const auto b = d[p++]; ++n; assert(n <= 4); l = (l << 7) | (b & 0x7F); if (!(b & 0x80)) break; }
                e.meta.assign(d.begin() + p, d.begin() + p + l); p += l;
                if (e.metaType == 0x2F) { assert(l == 0); tr.eot = true; tr.eotTick = tick; }
            } else {
                const int hi = st & 0xF0;
                assert(hi != 0xF0);
                e.d1 = d[p++];
                if (hi != 0xC0 && hi != 0xD0) e.d2 = d[p++];
            }
            tr.ev.push_back(std::move(e));
        }
        assert(p == end && tr.eot);
        f.tracks.push_back(std::move(tr));
    }
    assert(p == d.size());
    return f;
}
}  // namespace smf
