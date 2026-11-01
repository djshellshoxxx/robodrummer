#include "plugin/JamEngine.h"
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <tuple>

using namespace robodrummer;

namespace {
std::uint64_t ordinaryTrace(double rate, double bpm, int numerator, int denominator) {
    JamEngine engine;
    engine.prepare(rate);
    engine.setTempo(bpm);
    engine.setMeter(numerator, denominator);
    engine.setHumanize(0.23f);
    engine.setFeelMs(-7.0f);
    engine.setComplexity(0.7f);
    std::array<DrumEvent, 192> events{};
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xff;
            hash *= 1099511628211ULL;
        }
    };
    double ppq = 0.0;
    for (int block = 0; block < 2400; ++block) {
        if (block == 150) engine.apply(MidiCommand::Fill);
        if (block == 300) engine.apply(MidiCommand::Break);
        if (block == 450) engine.apply(MidiCommand::ImmediateFill);
        if (block == 600) engine.apply(MidiCommand::HalfTime);
        if (block == 900) engine.apply(MidiCommand::DoubleTime);
        if (block == 1200) { ppq = 1.25; engine.nudgePhaseSamples(-137); }
        if (block == 1500) engine.setTempoPreservingPosition(bpm * 0.8);
        if (block == 1600) engine.setTempo(bpm);
        if (block == 1800) engine.requestOutro();
        if (block == 2200) engine.apply(MidiCommand::Resume);
        engine.syncToPpq(ppq);
        const int size = block % 3 == 0 ? 64 : (block % 3 == 1 ? 512 : 2048);
        const auto count = engine.processBlock(size, events.data(), events.size());
        // std::sort may order simultaneous hits differently across standard
        // libraries. Canonicalize this historical event-field reference only.
        std::sort(events.begin(), events.begin() + count, [](const auto& a, const auto& b) {
            return std::tie(a.sampleOffset, a.instrument, a.velocity, a.sequence)
                 < std::tie(b.sampleOffset, b.instrument, b.velocity, b.sequence);
        });
        mix(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& event = events[i];
            mix(static_cast<std::uint64_t>(event.instrument));
            mix(static_cast<std::uint64_t>(event.sampleOffset));
            mix(std::bit_cast<std::uint32_t>(event.velocity));
            mix(event.sequence);
        }
        mix(static_cast<std::uint64_t>(engine.currentBarIndex()));
        mix(static_cast<std::uint64_t>(engine.currentBeatInBar()));
        ppq += size * bpm / (rate * 60.0);
    }
    return hash;
}

void checkPosition(const JamEngine& engine) {
    assert(engine.currentBarIndex() >= 0);
    assert(engine.currentBeatInBar() >= 1);
    assert(engine.currentBeatInBar() <= engine.meterNumerator());
    assert(std::isfinite(engine.currentBeatPhase()));
    assert(engine.currentBeatPhase() >= 0.0 && engine.currentBeatPhase() < 1.0);
    for (const auto offset : {0LL, 512LL, std::numeric_limits<long long>::min(), std::numeric_limits<long long>::max()}) {
        long long bar = -1;
        double phase = -1;
        engine.barPhaseAt(offset, bar, phase);
        assert(bar >= 0);
        assert(std::isfinite(phase) && phase >= 0.0 && phase < 1.0);
        if (offset == std::numeric_limits<long long>::min()) assert(bar == 0 && phase == 0.0);
    }
}

std::size_t checkBlock(JamEngine& engine, int size) {
    std::array<DrumEvent, 192> events{};
    const auto count = engine.processBlock(size, events.data(), events.size());
    assert(count <= events.size());
    for (std::size_t i = 0; i < count; ++i) {
        assert(events[i].sampleOffset >= 0 && events[i].sampleOffset < size);
        assert(std::isfinite(events[i].velocity));
        if (i > 0) assert(events[i - 1].sampleOffset <= events[i].sampleOffset);
    }
    int offset = -1;
    long long beat = -1;
    if (engine.nextBeat(0, size, offset, beat)) {
        assert(offset >= 0 && offset < size);
        assert(beat >= 0);
    }
    checkPosition(engine);
    return count;
}
}

int main() {
    // A finite host PPQ can still overflow the sample domain. Previously llround
    // yielded LLONG_MIN and the subsequent backwards seek overflowed subtraction.
    JamEngine repro;
    repro.prepare(48000.0);
    repro.setTempo(120.0);
    repro.syncToPpq(1.0e15);
    const auto clippedBar = repro.currentBarIndex();
    assert(clippedBar > 0);
    repro.syncToPpq(std::numeric_limits<double>::max());
    assert(repro.currentBarIndex() == clippedBar); // both saturate at the same upper bound
    repro.syncToPpq(std::numeric_limits<double>::quiet_NaN());
    repro.syncToPpq(std::numeric_limits<double>::infinity());
    assert(repro.currentBarIndex() == clippedBar); // nonfinite host positions remain ignored
    repro.syncToPpq(0.0);
    assert(repro.currentBarIndex() == 0);
    assert(checkBlock(repro, 512) > 0);

    for (const double rate : {11025.0, 48000.0, 192000.0}) {
        for (const double tempo : {20.0, 120.0, 400.0}) {
            for (const auto meter : {std::array<int, 2>{1, 16}, {4, 4}, {32, 1}}) {
                JamEngine engine;
                engine.prepare(rate);
                engine.setTempo(tempo);
                engine.setMeter(meter[0], meter[1]);
                engine.setTimeScale(2.0);
                for (const double ppq : {1.0e15, std::numeric_limits<double>::max()}) {
                    engine.syncToPpq(ppq);
                    const auto saturatedBar = engine.currentBarIndex();
                    assert(saturatedBar > 0);
                    checkPosition(engine);
                    engine.apply(MidiCommand::ImmediateFill);
                    checkBlock(engine, 1);
                    checkBlock(engine, 512);
                    assert(engine.currentBarIndex() == saturatedBar);
                    engine.requestOutro();
                    checkBlock(engine, 40000);
                    engine.setTempoPreservingPosition(20.0);
                    checkPosition(engine);
                    engine.nudgePhaseSamples(std::numeric_limits<long long>::max());
                    checkBlock(engine, 512);
                    engine.jumpPhaseSamples(std::numeric_limits<long long>::min());
                    assert(engine.currentBarIndex() == 0);
                    engine.apply(MidiCommand::Resume);
                    engine.syncToPpq(0.0);
                    checkBlock(engine, 512);
                    engine.apply(MidiCommand::Stop);
                    engine.nudgePhaseSamples(std::numeric_limits<long long>::max());
                    checkBlock(engine, std::numeric_limits<int>::max()); // stopped path also advances safely
                    engine.syncToPpq(0.0);
                    engine.apply(MidiCommand::Resume);
                    checkBlock(engine, 512);
                }
                engine.syncToPpq(-std::numeric_limits<double>::max());
                assert(engine.currentBarIndex() == 0);
                engine.nudgePhaseSamples(std::numeric_limits<long long>::max());
                const auto saturatedBar = engine.currentBarIndex();
                for (int advance = 0; advance < 5000; ++advance)
                    assert(engine.processBlock(std::numeric_limits<int>::max(), nullptr, 0) == 0);
                assert(engine.currentBarIndex() == saturatedBar);
            }
        }
    }
    // Exact event-field/position traces captured from the previous ordinary
    // production path, including fills, breaks, feel changes and host loops.
    assert(ordinaryTrace(48000.0, 120.0, 4, 4) == 18129464599217574272ULL);
    assert(ordinaryTrace(44100.0, 123.75, 7, 8) == 3999801358167414663ULL);
}
