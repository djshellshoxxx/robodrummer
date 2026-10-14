#include "audio/DrumSamplePlayer.h"
#include "core/KitFileName.h"
#include "core/Mixer.h"
#include <cassert>
#include <cmath>
int main() {
    using namespace robodrummer;
    // Instrument -> channel routing covers every instrument.
    assert(mixerChannelFor(DrumInstrument::Kick) == MixerChannel::Kick);
    assert(mixerChannelFor(DrumInstrument::Rimshot) == MixerChannel::Snare);
    assert(mixerChannelFor(DrumInstrument::FloorTom) == MixerChannel::Toms);
    assert(mixerChannelFor(DrumInstrument::OpenHat) == MixerChannel::HiHat);
    assert(mixerChannelFor(DrumInstrument::Splash) == MixerChannel::Cymbals);
    for (int i = 0; i <= static_cast<int>(DrumInstrument::Clap); ++i) {
        const auto ch = mixerChannelFor(static_cast<DrumInstrument>(i));
        assert(static_cast<std::size_t>(ch) < kMixerSourceChannelCount);
        const int bus = auxBusFor(ch);
        assert(bus >= 1 && bus <= kAuxBusCount);
    }
    MixerSettings m;
    assert(std::abs(resolveInstrumentMix(DrumInstrument::Kick, m).gain - 1.0f) < 1.0e-6f);
    m.gainDb[static_cast<std::size_t>(MixerChannel::Kick)] = -6.0f;
    assert(std::abs(resolveInstrumentMix(DrumInstrument::Kick, m).gain - 0.5012f) < 0.001f);
    m.gainDb[static_cast<std::size_t>(MixerChannel::Kick)] = -60.0f;
    assert(resolveInstrumentMix(DrumInstrument::Kick, m).gain == 0.0f);
    m = {};
    m.mute[static_cast<std::size_t>(MixerChannel::Snare)] = true;
    assert(resolveInstrumentMix(DrumInstrument::Snare, m).gain == 0.0f && resolveInstrumentMix(DrumInstrument::Kick, m).gain > 0.9f);
    m.solo[static_cast<std::size_t>(MixerChannel::Kick)] = true;
    assert(m.anySolo());
    assert(resolveInstrumentMix(DrumInstrument::Kick, m).gain > 0.9f);
    assert(resolveInstrumentMix(DrumInstrument::ClosedHat, m).gain == 0.0f);
    m.solo[static_cast<std::size_t>(MixerChannel::Snare)] = true; // solo wins over mute only when mute is cleared
    assert(resolveInstrumentMix(DrumInstrument::Snare, m).gain > 0.9f);
    m.pan[static_cast<std::size_t>(MixerChannel::Kick)] = -2.0f;
    assert(resolveInstrumentMix(DrumInstrument::Kick, m).pan == -1.0f);

    // Kit file names.
    auto p = [](const char* s) { return parseKitFileName(s); };
    assert(p("kick") && p("kick")->instrument == DrumInstrument::Kick);
    assert(p("Snare_v2_rr3") && p("Snare_v2_rr3")->layer == 1 && p("Snare_v2_rr3")->roundRobin == 2);
    assert(p("closed-hat_V1") && p("closed-hat_V1")->instrument == DrumInstrument::ClosedHat);
    assert(p("Open Hat") && p("Open Hat")->instrument == DrumInstrument::OpenHat);
    assert(p("tom1") && p("tom1")->instrument == DrumInstrument::HighTom);
    assert(p("Floor_Tom_rr2") && p("Floor_Tom_rr2")->instrument == DrumInstrument::FloorTom);
    assert(p("RIDE.v3") && p("RIDE.v3")->layer == 2);
    assert(p("hihat") && p("hihat")->instrument == DrumInstrument::ClosedHat);
    assert(!p("readme") && !p("") && !p("v1_rr1"));

    // Aux bus routing in the sampler: bus voices land on their own pair, or fall back to main.
    DrumSamplePlayer player;
    DrumSamplePlayer::Sample s; s.left.assign(64, 0.5f);
    player.setSample(DrumInstrument::Kick, s);
    player.setSample(DrumInstrument::Snare, s);
    player.setInstrumentBus(DrumInstrument::Kick, 1);
    player.setInstrumentBus(DrumInstrument::Snare, 2);
    float mainL[64]{}, mainR[64]{}, kickL[64]{}, kickR[64]{};
    float* lefts[3] { mainL, kickL, nullptr };
    float* rights[3] { mainR, kickR, nullptr };
    player.trigger({DrumInstrument::Kick, 0, 1.0f, 0});
    player.trigger({DrumInstrument::Snare, 0, 1.0f, 1});
    player.renderBuses(lefts, rights, 3, 64);
    assert(kickL[10] > 0.4f && kickR[10] > 0.4f);   // kick on its aux bus
    assert(mainL[10] > 0.4f);                       // snare bus disabled -> main
    assert(std::abs(mainL[10] - 0.5f) < 1.0e-5f);   // ...and kick did not leak into main
}
