#include "core/EventScheduler.h"
#include <cassert>
int main() {
    using namespace robodrummer;
    EventScheduler<3> s;
    assert(s.push({DrumInstrument::Snare, 20, 1.0f,0}));
    assert(s.push({DrumInstrument::Kick, 10, 1.0f,0}));
    assert(s.push({DrumInstrument::Crash, 20, 1.0f,0}));
    assert(!s.push({DrumInstrument::Ride, 30,1.0f,0}));
    auto it=s.begin();
    assert(it[0].sampleOffset==10);
    assert(it[1].instrument==DrumInstrument::Snare);
    assert(it[2].instrument==DrumInstrument::Crash);
    s.clear();
    assert(s.size()==0);
}
