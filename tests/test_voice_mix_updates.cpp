#include "audio/DrumSamplePlayer.h"
#include "core/Mixer.h"
#include <array>
#include <cassert>
#include <cmath>

int main() {
    using namespace robodrummer;
    for (const double rate : {8000.0, 44100.0, 192000.0}) {
        DrumSamplePlayer ramp;
        ramp.setSampleRate(rate);
        DrumSamplePlayer::Sample held; held.left.assign(4096, 1.0f);
        ramp.setSample(DrumInstrument::Kick, std::move(held));
        ramp.trigger({DrumInstrument::Kick, 0, 1.0f, 0});
        ramp.setInstrumentGainPan(DrumInstrument::Kick, 0.0f, 0.0f);
        float previous = 1.0f;
        const int rampSamples = static_cast<int>(std::ceil(rate * 0.005));
        for (int frame = 0; frame < rampSamples; ++frame) {
            float sample[1]{}; float* output[]{sample};
            ramp.render(output, 1, 1);
            assert(sample[0] >= 0.0f && sample[0] <= previous);
            if (frame + 1 == rampSamples) assert(sample[0] == 0.0f);
            else assert(sample[0] > 0.0f);
            previous = sample[0];
        }
    }
    DrumSamplePlayer player;
    DrumSamplePlayer::Sample sample; sample.left.assign(8192, 1.0f);
    player.setSample(DrumInstrument::Crash, sample);
    player.trigger({DrumInstrument::Crash, 0, 0.5f, 0});
    std::array<float, 512> l{}, r{}, sendL{}, sendR{};
    float* left[]{l.data()}; float* right[]{r.data()};
    player.renderBuses(left, right, 1, 1);
    assert(l[0] == 0.5f && r[0] == 0.5f);

    MixerSettings mix;
    mix.mute[static_cast<std::size_t>(MixerChannel::Cymbals)] = true;
    const auto muted = resolveInstrumentMix(DrumInstrument::Crash, mix);
    player.setInstrumentGainPan(DrumInstrument::Crash, muted.gain, muted.pan);
    l.fill(0); r.fill(0);
    player.renderBuses(left, right, 1, 512, sendL.data(), sendR.data());
    assert(l[0] > 0.0f && l[0] < 0.5f); // click-bounded ramp, not a step
    for (std::size_t n = 1; n < l.size(); ++n) assert(l[n] <= l[n - 1]);
    assert(l.back() == 0.0f && r.back() == 0.0f);
    for (std::size_t n = 0; n < l.size(); ++n) assert(l[n] == sendL[n] && r[n] == sendR[n]);

    // The same ringing hit follows pan/gain and returns with its original velocity.
    player.setInstrumentGainPan(DrumInstrument::Crash, 0.5f, 1.0f);
    l.fill(0); r.fill(0);
    player.renderBuses(left, right, 1, 512);
    assert(l.back() == 0.0f && r.back() == 0.25f);
    player.setInstrumentGainPan(DrumInstrument::Crash, 1.0f, -1.0f);
    l.fill(0); r.fill(0);
    player.renderBuses(left, right, 1, 512);
    assert(l.back() == 0.5f && r.back() == 0.0f);

    // A solo on a different channel suppresses this already-ringing cymbal.
    mix = {}; mix.solo[static_cast<std::size_t>(MixerChannel::Kick)] = true;
    const auto soloed = resolveInstrumentMix(DrumInstrument::Crash, mix);
    player.setInstrumentGainPan(DrumInstrument::Crash, soloed.gain, soloed.pan);
    l.fill(0); r.fill(0);
    player.renderBuses(left, right, 1, 512);
    assert(l.back() == 0.0f && r.back() == 0.0f);

    // Unchanged gain/pan preserves the original sample multiplication bit for bit.
    DrumSamplePlayer exact;
    DrumSamplePlayer::Sample waveform; waveform.left = {0.1234567f, -0.2345678f, 1.25f};
    exact.setSample(DrumInstrument::Snare, waveform);
    exact.setInstrumentGainPan(DrumInstrument::Snare, 0.73f, 0.21f);
    exact.trigger({DrumInstrument::Snare, 0, 0.81f, 0});
    exact.setInstrumentGainPan(DrumInstrument::Snare, 0.73f, 0.21f);
    l.fill(0); r.fill(0);
    exact.renderBuses(left, right, 1, 3);
    for (std::size_t n = 0; n < 3; ++n) {
        const float interpolated = waveform.left[n] + ((n + 1 < 3 ? waveform.left[n + 1] : waveform.left[n]) - waveform.left[n]) * 0.0f;
        assert(l[n] == interpolated * (0.81f * 0.73f * (1.0f - 0.21f)));
        assert(r[n] == interpolated * (0.81f * 0.73f));
    }
}
