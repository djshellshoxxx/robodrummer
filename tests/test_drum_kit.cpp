#include "audio/DrumSamplePlayer.h"
#include <cassert>
#include <cmath>

using namespace robodrummer;

static DrumSample dc(float v, std::size_t n) { DrumSample s; s.left.assign(n, v); return s; }

int main() {
    // File names.
    auto n = parseKitFileName("Snare_v2_rr3.WAV");
    assert(n && n->instrument == DrumInstrument::Snare && n->velocity == 2 && n->roundRobin == 3);
    n = parseKitFileName("kick.aiff");
    assert(n && n->velocity == 0 && n->roundRobin == 0);
    n = parseKitFileName("closedhat_rr2.aif");
    assert(n && n->instrument == DrumInstrument::ClosedHat && n->roundRobin == 2 && n->velocity == 0);
    assert(!parseKitFileName("kick.mp3"));
    assert(!parseKitFileName("gong.wav"));
    assert(!parseKitFileName("kick_v5.wav"));
    assert(!parseKitFileName("kick_v0.wav"));
    assert(!parseKitFileName("kick_x.wav"));
    assert(!parseKitFileName("kick"));

    // kit.txt.
    KitConfig cfg{};
    std::vector<std::string> errs;
    parseKitConfig("# comment\nsnare tune=2 gain=-6 pan=-0.5 choke=2\nbogus tune=1\nkick gain=40\n\nride pan=1 # x\n", cfg, errs);
    assert(errs.size() == 2);
    assert(cfg[1].tune && *cfg[1].tune == 2.0f && *cfg[1].choke == 2 && *cfg[1].pan == -0.5f);
    assert(!cfg[0].gainDb);
    assert(cfg[4].pan && *cfg[4].pan == 1.0f);

    // Builder: velocity ranges and round robins.
    DrumKitBuilder b;
    assert(b.add(*parseKitFileName("snare_v1.wav"), dc(0.1f, 4)));
    assert(b.add(*parseKitFileName("snare_v1_rr2.wav"), dc(0.15f, 4)));
    assert(b.add(*parseKitFileName("snare_v2.wav"), dc(0.9f, 4)));
    assert(!b.add(*parseKitFileName("snare_v2_rr1.wav"), dc(0.9f, 4))); // duplicate slot
    assert(b.add(*parseKitFileName("closedhat.wav"), dc(0.3f, 4)));
    auto kit = b.build(cfg);
    const auto& sn = kit->instruments[1];
    assert(sn.layerCount == 2 && sn.layers[0].maxVelocity == 0.5f && sn.layers[1].minVelocity == 0.5f);
    assert(std::abs(sn.gain - 0.5012f) < 1e-3f && sn.tuneSemitones == 2.0f && sn.chokeGroup == 2);
    assert(kit->instruments[2].chokeGroup == 1 && kit->instruments[2].layerCount == 1);
    assert(kit->instrumentCount() == 2);

    // Player uses kit, falls back to starter for missing instruments.
    DrumSamplePlayer p;
    p.setSample(DrumInstrument::Kick, dc(0.5f, 100));
    DrumKitBuilder b2;
    b2.add(*parseKitFileName("snare.wav"), dc(1.0f, 100));
    auto kit2 = b2.build({});
    p.useKit(kit2.get());
    p.trigger({DrumInstrument::Snare, 0, 1.0f, 0});
    p.trigger({DrumInstrument::Kick, 0, 1.0f, 1});
    float l[8]{}, r[8]{}; float* out[2] = {l, r};
    p.render(out, 2, 8);
    assert(std::abs(l[4] - 1.5f) < 1e-4f);
    p.useKit(nullptr); // voices from the kit stop
    float l2[8]{}, r2[8]{}; float* out2[2] = {l2, r2};
    p.render(out2, 2, 8);
    assert(std::abs(l2[4] - 0.5f) < 1e-4f);

    // Handoff.
    KitHandoff h;
    assert(h.acquire() == nullptr && h.current() == nullptr);
    auto k1 = std::make_unique<DrumKit>(); auto* k1p = k1.get();
    h.publish(std::move(k1));
    assert(h.acquire() == k1p && h.current() == k1p && h.acquire() == nullptr);
    auto k2 = std::make_unique<DrumKit>(); auto* k2p = k2.get();
    h.publish(std::move(k2));
    h.publish(std::make_unique<DrumKit>()); // supersedes unacquired k2
    (void)k2p;
    auto* got = h.acquire();
    assert(got != nullptr && got != k1p && h.current() == got);
    h.publish(nullptr); // empty kit
    assert(h.acquire() != nullptr && h.acquire() == nullptr);
    h.collectGarbage();
    return 0;
}
