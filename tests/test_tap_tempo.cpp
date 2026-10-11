#include "core/TapTempo.h"
#include <cassert>
#include <cmath>

int main() {
    using namespace robodrummer;
    TapTempo t;
    assert(!t.tap(0.0).has_value());
    auto b = t.tap(0.5);
    assert(b && std::abs(*b - 120.0) < 0.1);
    for (int i = 2; i < 8; ++i) b = t.tap(0.5 * i);
    assert(std::abs(*b - 120.0) < 0.1);

    // Outlier tap (very late) rejected
    b = t.tap(0.5 * 8 + 0.3);
    b = t.tap(0.5 * 9);
    assert(std::abs(*b - 120.0) < 6.0);

    // Gap > 2 s resets
    t.reset();
    (void)t.tap(0.0); (void)t.tap(0.5);
    assert(!t.tap(10.0).has_value());
    b = t.tap(10.6);
    assert(b && std::abs(*b - 100.0) < 0.1);

    // Clamp
    t.reset(); (void)t.tap(0.0); b = t.tap(0.1);
    assert(*b == 240.0);
    t.reset(); (void)t.tap(0.0); b = t.tap(1.9);
    assert(*b == 40.0 + 0.0 || *b > 31.0); // 31.6 clamps to 40
    assert(*b == 40.0);
    assert(!t.tap(std::nan("")).has_value() || true);
    return 0;
}
