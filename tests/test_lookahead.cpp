#include "core/Lookahead.h"
#include <cassert>
#include <cmath>
int main() {
    using namespace robodrummer;
    assert(std::abs(recommendedLookaheadMs(480, 48000.0, 1.0f) - 12.0) < 1.0e-9);
    assert(recommendedLookaheadMs(480, 48000.0, 0.0f) > recommendedLookaheadMs(480, 48000.0, 1.0f) + 20.0);
    assert(recommendedLookaheadMs(1024, 48000.0, 0.5f) > recommendedLookaheadMs(128, 48000.0, 0.5f));
    assert(recommendedLookaheadMs(0, 48000.0, 1.0f) == 0.0);
    assert(std::isfinite(recommendedLookaheadMs(512, 44100.0, std::nanf(""))));
}
