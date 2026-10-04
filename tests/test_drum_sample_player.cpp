#include "audio/DrumSamplePlayer.h"
#include <cassert>
#include <cmath>
int main() {
    using namespace robodrummer;
    DrumSamplePlayer p;
    DrumSamplePlayer::Sample kick;
    kick.left = {1.0f, 0.5f, 0.25f};
    p.setSample(DrumInstrument::Kick, kick);
    float l[4]{}; float r[4]{}; float* out[2]{l,r};
    p.trigger({DrumInstrument::Kick,0,0.5f,0});
    p.render(out,2,4);
    assert(std::abs(l[0]-0.5f) < 0.0001f);
    assert(std::abs(l[1]-0.25f) < 0.0001f);
    assert(std::abs(r[0]-0.5f) < 0.0001f);
    float l2[2]{}; float r2[2]{}; float* out2[2]{l2,r2};
    p.trigger({DrumInstrument::Snare,0,1.0f,1});
    p.render(out2,2,2);
    assert(l2[0] == 0.0f && r2[0] == 0.0f);
}
