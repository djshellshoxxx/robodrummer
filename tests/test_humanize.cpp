#include "core/GrooveGenerator.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
int main() {
    using namespace robodrummer;
    GrooveGenerator g;
    const auto style = Style::basicRock();
    GrooveContext exact; exact.seed = 9; exact.intensity = 0.6f;
    std::array<DrumEvent, 128> a{}, b{};
    const auto na = g.generateBar(style, exact, a);

    // Humanize 0 / feel 0 is bit-exact with the plain groove.
    GrooveContext zero = exact; zero.humanize = 0.0f; zero.feelMs = 0.0f;
    assert(g.generateBar(style, zero, b) == na);
    for (std::size_t i = 0; i < na; ++i) {
        if (!(a[i].sampleOffset == b[i].sampleOffset && a[i].velocity == b[i].velocity)) {
            std::fprintf(stderr, "DIAG na=%zu i=%zu a=(%d,%d,%.9g,%u) b=(%d,%d,%.9g,%u)\n", na, i,
                         static_cast<int>(a[i].instrument), a[i].sampleOffset, static_cast<double>(a[i].velocity), a[i].sequence,
                         static_cast<int>(b[i].instrument), b[i].sampleOffset, static_cast<double>(b[i].velocity), b[i].sequence);
            for (std::size_t j = 0; j < na; ++j) std::fprintf(stderr, "  %zu a=(%d,%d,%.9g) b=(%d,%d,%.9g)\n", j, static_cast<int>(a[j].instrument), a[j].sampleOffset, static_cast<double>(a[j].velocity), static_cast<int>(b[j].instrument), b[j].sampleOffset, static_cast<double>(b[j].velocity));
        }
        assert(a[i].sampleOffset == b[i].sampleOffset && a[i].velocity == b[i].velocity);
    }

    // Humanize moves hits, deterministically per seed, never out of the bar, and never the event count.
    GrooveContext loose = exact; loose.humanize = 1.0f;
    const auto nl = g.generateBar(style, loose, b);
    assert(nl == na);
    std::array<DrumEvent, 128> c{};
    assert(g.generateBar(style, loose, c) == nl);
    int moved = 0; int maxShift = 0;
    for (std::size_t i = 0; i < na; ++i) {
        assert(b[i].sampleOffset == c[i].sampleOffset);
        assert(b[i].sampleOffset >= 0 && b[i].sampleOffset < 96000);
        const int shift = std::abs(b[i].sampleOffset - a[i].sampleOffset);
        moved += shift > 0; maxShift = std::max(maxShift, shift);
    }
    assert(moved > static_cast<int>(na) / 2);
    // Style humanizeMs = 4 ms, so at full humanize the worst shift stays below ~ (2*4*1.0 + bias) ms.
    assert(maxShift < static_cast<int>(48000 * 0.013));

    // Looser styles move further than tight ones.
    long long rockSum = 0, punkSum = 0, bluesSum = 0;
    for (unsigned seed = 1; seed <= 64; ++seed) {
        exact.seed = seed; loose.seed = seed;
        for (const auto& pair : { std::pair{Style::punk(), &punkSum}, std::pair{Style::blues(), &bluesSum} }) {
            const auto n0 = g.generateBar(pair.first, exact, a);
            const auto n1 = g.generateBar(pair.first, loose, b);
            assert(n0 == n1);
            for (std::size_t i = 0; i < n0; ++i) *pair.second += std::abs(a[i].sampleOffset - b[i].sampleOffset);
        }
    }
    assert(bluesSum > punkSum * 2);

    // Laid-back feel delays the snare, pushing feel moves it earlier; hats move at half weight.
    GrooveContext lay = exact; lay.seed = 3; lay.feelMs = 10.0f;
    GrooveContext push = lay; push.feelMs = -10.0f;
    GrooveContext base = lay; base.feelMs = 0.0f;
    g.generateBar(style, base, a); g.generateBar(style, lay, b); g.generateBar(style, push, c);
    auto snareAt = [&](const std::array<DrumEvent, 128>& e) {
        for (const auto& ev : e) if (ev.instrument == DrumInstrument::Snare) return ev.sampleOffset;
        return -1;
    };
    assert(snareAt(b) > snareAt(a));
    assert(snareAt(c) < snareAt(a));
    assert(snareAt(b) - snareAt(a) == 480);
}
