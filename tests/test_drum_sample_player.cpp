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

    // Velocity layers choose different samples without allocating on trigger.
    DrumSamplePlayer layered;
    DrumSamplePlayer::Sample soft; soft.left = {0.25f};
    DrumSamplePlayer::Sample hard; hard.left = {1.0f};
    layered.setVelocityLayer(DrumInstrument::Snare, 0, 0.0f, 0.49f, soft);
    layered.setVelocityLayer(DrumInstrument::Snare, 1, 0.5f, 1.0f, hard);

    float ls[1]{}; float rs[1]{}; float* softOut[2]{ls,rs};
    layered.trigger({DrumInstrument::Snare,0,0.4f,0});
    layered.render(softOut,2,1);
    assert(std::abs(ls[0] - 0.10f) < 0.0001f);

    float lh[1]{}; float rh[1]{}; float* hardOut[2]{lh,rh};
    layered.trigger({DrumInstrument::Snare,0,0.8f,0});
    layered.render(hardOut,2,1);
    assert(std::abs(lh[0] - 0.80f) < 0.0001f);

    // Round robin alternates variants inside the selected layer.
    DrumSamplePlayer rr;
    DrumSamplePlayer::Sample rrA; rrA.left = {0.25f};
    DrumSamplePlayer::Sample rrB; rrB.left = {0.75f};
    rr.setVelocityLayer(DrumInstrument::Kick, 0, 0.0f, 1.0f, rrA);
    assert(rr.addRoundRobinSample(DrumInstrument::Kick, 0, rrB));

    float ra[1]{}; float rar[1]{}; float* rrOutA[2]{ra,rar};
    rr.trigger({DrumInstrument::Kick,0,1.0f,0});
    rr.render(rrOutA,2,1);
    assert(std::abs(ra[0] - 0.25f) < 0.0001f);

    float rb[1]{}; float rbr[1]{}; float* rrOutB[2]{rb,rbr};
    rr.trigger({DrumInstrument::Kick,0,1.0f,1});
    rr.render(rrOutB,2,1);
    assert(std::abs(rb[0] - 0.75f) < 0.0001f);

    // Choke groups stop a ringing voice when another instrument in the group triggers.
    DrumSamplePlayer choke;
    DrumSamplePlayer::Sample open; open.left = {1.0f, 1.0f, 1.0f};
    DrumSamplePlayer::Sample closed; closed.left = {0.5f};
    choke.setSample(DrumInstrument::OpenHat, open);
    choke.setSample(DrumInstrument::ClosedHat, closed);
    choke.setChokeGroup(DrumInstrument::OpenHat, 1);
    choke.setChokeGroup(DrumInstrument::ClosedHat, 1);

    float c1[1]{}; float c1r[1]{}; float* cOut1[2]{c1,c1r};
    choke.trigger({DrumInstrument::OpenHat,0,1.0f,0});
    choke.render(cOut1,2,1);
    assert(std::abs(c1[0] - 1.0f) < 0.0001f);

    choke.trigger({DrumInstrument::ClosedHat,0,1.0f,1});
    float c2[2]{}; float c2r[2]{}; float* cOut2[2]{c2,c2r};
    choke.render(cOut2,2,2);
    assert(std::abs(c2[0] - 0.5f) < 0.0001f);
    assert(std::abs(c2[1]) < 0.0001f);

    // Per-instrument gain/pan applies after velocity.
    DrumSamplePlayer mix;
    DrumSamplePlayer::Sample tone; tone.left = {1.0f};
    mix.setSample(DrumInstrument::Ride, tone);
    mix.setInstrumentGainPan(DrumInstrument::Ride, 0.5f, 1.0f);
    float ml[1]{}; float mr[1]{}; float* mixOut[2]{ml,mr};
    mix.trigger({DrumInstrument::Ride,0,1.0f,0});
    mix.render(mixOut,2,1);
    assert(std::abs(ml[0]) < 0.0001f);
    assert(std::abs(mr[0] - 0.5f) < 0.0001f);
    // Tuning changes playback rate without allocation or destructive sample edits.
    DrumSamplePlayer tuned;
    DrumSamplePlayer::Sample ramp; ramp.left = {1.0f, 0.75f, 0.5f, 0.25f};
    tuned.setSample(DrumInstrument::HighTom, ramp);
    tuned.setInstrumentTuning(DrumInstrument::HighTom, 12.0f);
    float tl[2]{}; float tr[2]{}; float* tuneOut[2]{tl,tr};
    tuned.trigger({DrumInstrument::HighTom,0,1.0f,0});
    tuned.render(tuneOut,2,2);
    assert(std::abs(tl[0] - 1.0f) < 0.0001f);
    assert(std::abs(tl[1] - 0.5f) < 0.0001f);

    // Attack/release envelopes are applied per voice.
    DrumSamplePlayer enveloped;
    DrumSamplePlayer::Sample flat; flat.left = {1.0f, 1.0f, 1.0f, 1.0f};
    enveloped.setSample(DrumInstrument::FloorTom, flat);
    enveloped.setInstrumentEnvelope(DrumInstrument::FloorTom, 2, 2);
    float el[4]{}; float er[4]{}; float* envOut[2]{el,er};
    enveloped.trigger({DrumInstrument::FloorTom,0,1.0f,0});
    enveloped.render(envOut,2,4);
    assert(std::abs(el[0] - 0.5f) < 0.0001f);
    assert(std::abs(el[1] - 1.0f) < 0.0001f);
    assert(std::abs(el[2] - 1.0f) < 0.0001f);
    assert(std::abs(el[3] - 0.5f) < 0.0001f);

    // A full voice pool steals the most decayed voice instead of dropping the new hit.
    DrumSamplePlayer full;
    // Only the first sample is non-zero, so output at the start of a render proves a fresh voice started.
    DrumSamplePlayer::Sample longSample; longSample.left.assign(48000, 0.0f); longSample.left[0] = 1.0f;
    full.setSample(DrumInstrument::Kick, longSample);
    float fl[1]{}; float fr[1]{}; float* fullOut[2]{fl, fr};
    for (int i = 0; i < 200; ++i) {
        full.trigger({DrumInstrument::Kick, 0, 1.0f, 0});
        fl[0] = 0.0f;
        full.render(fullOut, 2, 1);
    }
    assert(fl[0] > 0.5f);
}
