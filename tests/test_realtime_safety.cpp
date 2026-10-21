// Verifies the audio-thread paths never touch the heap once prepared (SPEC 33).
#include "analysis/InputConditioner.h"
#include "analysis/RhythmAnalyzer.h"
#include "analysis/PerformanceAnalyzer.h"
#include "audio/DrumSamplePlayer.h"
#include "core/StyleLibrary.h"
#include "core/MidiLearn.h"
#include "plugin/JamEngine.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <new>

static std::atomic<long long> gAllocations{0};
static std::atomic<bool> gCounting{false};
void* operator new(std::size_t n) { if (gCounting) ++gAllocations; if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (gCounting) ++gAllocations; if (void* p = std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main() {
    using namespace robodrummer;
    JamEngine engine; engine.prepare(48000.0); engine.setTempo(128.0); engine.setMeter(4, 4);
    LiveRhythmAnalyzer rhythm; rhythm.prepare(48000.0);
    PerformanceAnalyzer perf; perf.prepare(48000.0);
    InputConditioner conditioner; conditioner.prepare(48000.0); conditioner.setGateThresholdDb(-60.0f); conditioner.setAutoGain(true);
    DrumSamplePlayer player;
    DrumSamplePlayer::Sample sample; sample.left.assign(24000, 0.1f);
    for (int i = 0; i < 16; ++i) player.setSample(static_cast<DrumInstrument>(i), sample);
    MidiLearnMap learn;
    std::array<float, 512> in{}, mono{}, l{}, r{};
    std::array<DrumEvent, 128> events{};
    float* outs[2] { l.data(), r.data() };

    gCounting = true;
    for (int block = 0; block < 4000; ++block) {
        for (std::size_t i = 0; i < in.size(); ++i)
            in[i] = ((block * 512 + static_cast<int>(i)) % 12000 < 300) ? 0.5f * std::sin(static_cast<float>(i)) : 0.0f;
        conditioner.process(in.data(), nullptr, mono.data(), 512);
        rhythm.processBlock(mono.data(), 512);
        perf.processBlock(mono.data(), 512);
        engine.setStyle(styleFor(static_cast<JamStyle>(block % kJamStyleCount)));
        engine.setHumanize(0.7f);
        if (block % 97 == 0) engine.apply(MidiCommand::Fill);
        if (block % 211 == 0) engine.apply(MidiCommand::ImmediateFill);
        if (block % 401 == 0) engine.apply(MidiCommand::Break);
        if (block % 503 == 0) engine.apply(MidiCommand::HalfTime);
        const auto n = engine.processBlock(512, events.data(), events.size());
        for (std::size_t i = 0; i < n; ++i) player.trigger(events[i]);
        player.render(outs, 2, 512);
        (void) learn.handleCc(block % 128);
    }
    gCounting = false;
    assert(gAllocations.load() == 0);
}
