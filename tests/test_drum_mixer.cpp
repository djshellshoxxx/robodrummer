#include "audio/DrumSamplePlayer.h"
#include <cassert>
#include <cmath>
#include <vector>

using namespace robodrummer;

static DrumSamplePlayer::Sample dc(float v, int n) {
    DrumSamplePlayer::Sample s; s.left.assign(static_cast<std::size_t>(n), v); return s;
}

int main() {
    // Mapping.
    assert(DrumMixer::channelFor(DrumInstrument::Kick) == MixerChannel::Kick);
    assert(DrumMixer::channelFor(DrumInstrument::Rimshot) == MixerChannel::Snare);
    assert(DrumMixer::channelFor(DrumInstrument::FloorTom) == MixerChannel::Toms);
    assert(DrumMixer::channelFor(DrumInstrument::OpenHat) == MixerChannel::HiHat);
    assert(DrumMixer::channelFor(DrumInstrument::Ride) == MixerChannel::Ride);
    assert(DrumMixer::channelFor(DrumInstrument::China) == MixerChannel::Cymbals);
    assert(DrumMixer::channelFor(DrumInstrument::Cowbell) == MixerChannel::Cymbals);

    auto run = [](DrumSamplePlayer& p, int n, std::vector<float>& l, std::vector<float>& r) {
        l.assign(static_cast<std::size_t>(n), 0.0f); r = l;
        float* out[2] = {l.data(), r.data()};
        p.render(out, 2, n);
    };
    std::vector<float> l, r;

    // Gain -6 dB (settled), smoothed start.
    {
        DrumSamplePlayer p; p.prepare(48000.0, 512);
        p.setSample(DrumInstrument::Kick, dc(1.0f, 20000));
        p.mixer().setGainDb(MixerChannel::Kick, -6.0f);
        p.trigger({DrumInstrument::Kick, 0, 1.0f, 0});
        run(p, 8000, l, r);
        assert(std::abs(l[7999] - 0.5012f) < 0.002f);
        assert(l[0] > 0.9f && l[0] < 1.0f);
        assert(std::abs(l[1] - l[0]) < 0.01f);
    }
    // Mute and solo.
    {
        DrumSamplePlayer p;
        p.setSample(DrumInstrument::Kick, dc(1.0f, 20000));
        p.setSample(DrumInstrument::Snare, dc(0.5f, 20000));
        p.mixer().setSolo(MixerChannel::Snare, true);
        p.mixer().snap();
        p.trigger({DrumInstrument::Kick, 0, 1.0f, 0});
        p.trigger({DrumInstrument::Snare, 0, 1.0f, 1});
        run(p, 100, l, r);
        assert(std::abs(l[50] - 0.5f) < 1e-4f);
        p.mixer().setSolo(MixerChannel::Snare, false);
        p.mixer().setMute(MixerChannel::Snare, true);
        run(p, 8000, l, r);
        assert(std::abs(l[7999] - 1.0f) < 1e-3f);
    }
    // Pan and master.
    {
        DrumSamplePlayer p;
        p.setSample(DrumInstrument::HighTom, dc(1.0f, 20000));
        p.mixer().setPan(MixerChannel::Toms, 1.0f);
        p.mixer().setGainDb(MixerChannel::Master, -60.0f);
        p.mixer().snap();
        p.trigger({DrumInstrument::HighTom, 0, 1.0f, 0});
        run(p, 100, l, r);
        assert(std::abs(l[50]) < 1e-6f && std::abs(r[50]) < 1e-2f);
        p.mixer().setGainDb(MixerChannel::Master, 0.0f);
        p.mixer().snap();
        run(p, 100, l, r);
        assert(std::abs(l[50]) < 1e-6f && std::abs(r[50] - 1.0f) < 1e-4f);
    }
    // Room: finite, silent when no send, tail decays, large blocks chunked.
    {
        DrumSamplePlayer p; p.prepare(44100.0, 256);
        p.setSample(DrumInstrument::Snare, dc(1.0f, 200));
        p.trigger({DrumInstrument::Snare, 0, 1.0f, 0});
        run(p, 3000, l, r);
        for (float v : l) assert(std::isfinite(v));
        assert(l[2999] == 0.0f);

        DrumSamplePlayer q; q.prepare(44100.0, 256);
        q.setSample(DrumInstrument::Snare, dc(1.0f, 200));
        q.mixer().setRoomSend(MixerChannel::Snare, 1.0f);
        q.mixer().setMute(MixerChannel::Snare, true);
        q.mixer().snap();
        q.trigger({DrumInstrument::Snare, 0, 1.0f, 0});
        run(q, 3000, l, r);
        for (float v : l) assert(v == 0.0f);

        q.mixer().setMute(MixerChannel::Snare, false);
        q.mixer().snap();
        q.trigger({DrumInstrument::Snare, 0, 1.0f, 0});
        run(q, 1000, l, r);
        float wet = 0;
        for (int i = 400; i < 1000; ++i) wet += std::abs(l[static_cast<std::size_t>(i)]);
        assert(wet > 0.001f);
        for (float v : l) assert(std::isfinite(v));
        run(q, 44100 * 5, l, r);
        for (float v : l) assert(std::isfinite(v) && std::abs(v) < 4.0f);
        assert(std::abs(l.back()) < 1e-4f);
    }
    // Stems.
    {
        DrumSamplePlayer p;
        p.setSample(DrumInstrument::Ride, dc(0.25f, 1000));
        p.trigger({DrumInstrument::Ride, 0, 1.0f, 0});
        std::vector<std::vector<float>> bufs(14, std::vector<float>(64, 0.0f));
        float* ptrs[14];
        for (int i = 0; i < 14; ++i) ptrs[i] = bufs[static_cast<std::size_t>(i)].data();
        p.renderStems(ptrs, 64);
        assert(std::abs(bufs[8][10] - 0.25f) < 1e-5f && bufs[0][10] == 0.0f);
    }
    return 0;
}
