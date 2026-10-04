#include "core/MusicalClock.h"
#include <cassert>
#include <cmath>
int main() {
    using namespace robodrummer;
    MusicalClock clock;
    assert(std::abs(clock.samplesPerBeat(48000.0) - 24000.0) < 0.001);
    clock.setTempo(0.0); assert(clock.snapshot().bpm == 20.0);
    clock.setTempo(120.0); clock.setMeter(4,4); clock.reset();
    clock.advance(24000,48000.0); assert(clock.snapshot().beat == 2 && clock.snapshot().bar == 1);
    clock.advance(72000,48000.0); assert(clock.snapshot().beat == 1 && clock.snapshot().bar == 2);
    clock.setMeter(7,8); auto s=clock.snapshot(); assert(s.numerator==7 && s.denominator==8);
}
