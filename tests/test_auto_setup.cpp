#include "analysis/AutoSetup.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using namespace robodrummer;

namespace {
constexpr double kSr = 48000.0;
constexpr double kTwoPi = 6.283185307179586;

struct Lcg {
    std::uint32_t s;
    explicit Lcg(std::uint32_t seed) : s(seed) {}
    float next() { s = s * 1664525u + 1013904223u; return static_cast<float>((s >> 8) & 0xFFFFFF) / 16777216.0f; } // 0..1
    float bipolar() { return next() * 2.0f - 1.0f; }
};

struct Pluck { double t; float amp; float freq; };

std::vector<float> render(double seconds, float noiseAmp, const std::vector<Pluck>& plucks, float decay = 0.12f, std::uint32_t seed = 7) {
    const auto n = static_cast<std::size_t>(seconds * kSr);
    std::vector<float> out(n, 0.0f);
    Lcg rng(seed);
    for (auto& v : out) v = noiseAmp * rng.bipolar() * 1.7f; // uniform noise, rms ~ noiseAmp
    for (const auto& p : plucks) {
        const auto start = static_cast<std::size_t>(p.t * kSr);
        const auto len = static_cast<std::size_t>(decay * 6.0 * kSr);
        for (std::size_t i = 0; i < len && start + i < n; ++i) {
            const double t = static_cast<double>(i) / kSr;
            const double env = std::exp(-t / decay) * (i < 24 ? static_cast<double>(i) / 24.0 : 1.0);
            const double tone = std::sin(kTwoPi * p.freq * t) + 0.4 * std::sin(kTwoPi * p.freq * 3.0 * t);
            out[start + i] += static_cast<float>(p.amp * env * tone / 1.4);
        }
    }
    return out;
}

std::vector<Pluck> steadyEighths(double bpm, double startT, double endT, float amp, float freq = 196.0f) {
    std::vector<Pluck> v;
    const double step = 60.0 / bpm / 2.0;
    int k = 0;
    for (double t = startT; t < endT; t += step, ++k) v.push_back({t, (k % 2 == 0) ? amp : amp * 0.8f, freq});
    return v;
}

std::vector<Pluck> jittered(double bpm, double startT, double endT, float amp, double jitterSec, std::uint32_t seed) {
    Lcg rng(seed);
    std::vector<Pluck> v;
    const double step = 60.0 / bpm / 2.0;
    for (double t = startT; t < endT; t += step) v.push_back({t + jitterSec * rng.bipolar(), amp * (0.7f + 0.3f * rng.next()), 196.0f});
    return v;
}

// Feeds the whole signal in blocks of `block` samples until the run leaves Listening; then finalizes.
AutoSetupResult run(AutoSetup& a, const std::vector<float>& sig, int block = 480, float seconds = 10.0f) {
    a.prepare(kSr);
    a.start(seconds);
    std::size_t pos = 0;
    while (pos < sig.size() && a.state() == AutoSetupState::Listening) {
        const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), sig.size() - pos));
        a.process(sig.data() + pos, n);
        pos += static_cast<std::size_t>(n);
    }
    if (a.state() == AutoSetupState::Analyzing) a.finalize();
    return a.result();
}

bool inRanges(const AutoSetupProposal& p) {
    const auto ok = [](const auto& f, float lo, float hi) { return !f.apply || (std::isfinite(f.value) && f.value >= lo && f.value <= hi); };
    return ok(p.inputGainDb, -24, 36) && ok(p.gateThresholdDb, -90, -20) && ok(p.followStrength, 0, 1) && ok(p.tempoInertia, 0, 1) &&
           ok(p.confidenceSensitivity, 0, 1) && ok(p.leadership, 0, 1) && ok(p.bpm, 40, 240) && ok(p.intensity, 0, 1);
}

bool hasLine(const AutoSetupResult& r, const char* needle) {
    for (int i = 0; i < r.lineCount; ++i) if (std::strstr(r.line(i), needle) != nullptr) return true;
    return false;
}

void dump(const char* name, const AutoSetupResult& r) {
    std::printf("[%s] status=%d bpm=%.2f reg=%.2f sd=%.1fms L=%.2f sub=%d tc=%.2f trk=%.1f dens=%.2f low=%.2f str=%.2f swing=%.2f noise=%.1f peak=%.1f\n", name,
                static_cast<int>(r.status), r.measured.bpm, r.measured.regularity, r.measured.timingSdMs, r.measured.looseness, r.measured.subdivision,
                r.measured.tempoConfidence, r.measured.trackedBpm, r.measured.onsetDensity, r.measured.lowShare, r.measured.transientStrength,
                r.measured.swing, r.measured.noiseFloorDb, r.measured.peakDb);
    for (int i = 0; i < r.lineCount; ++i) std::printf("   %s\n", r.line(i));
}

bool sameResult(const AutoSetupResult& a, const AutoSetupResult& b) {
    if (a.status != b.status || a.lineCount != b.lineCount) return false;
    for (int i = 0; i < a.lineCount; ++i) if (std::strcmp(a.line(i), b.line(i)) != 0) return false;
    return std::memcmp(&a.measured, &b.measured, sizeof(a.measured)) == 0;
}
} // namespace

int main() {
    const float noiseAmp = static_cast<float>(std::pow(10.0, -63.0 / 20.0));

    // 1. Quiet noise + steady 8ths at 100 BPM -> BPM within 2, Hybrid, gate above noise.
    AutoSetupResult steady;
    {
        AutoSetup a;
        const auto sig = render(20.0, noiseAmp, steadyEighths(100.0, 1.4, 19.0, 0.22f));
        steady = run(a, sig);
        dump("steady", steady);
        assert(a.state() == AutoSetupState::Done);
        assert(steady.status == AutoSetupStatus::Ok && steady.levelsValid && steady.rhythmValid);
        assert(std::abs(steady.proposal.bpm.value - 100.0f) <= 2.0f);
        assert(steady.proposal.timingMode.apply && steady.proposal.timingMode.value == LeadershipMode::Hybrid);
        assert(steady.proposal.leadership.apply && std::abs(steady.proposal.leadership.value - 0.70f) < 1e-6f);
        assert(steady.proposal.gateThresholdDb.apply);
        const float noisePost = steady.measured.noiseFloorDb + steady.proposal.inputGainDb.value;
        assert(steady.proposal.gateThresholdDb.value > noisePost + 6.0f);
        assert(steady.proposal.gateThresholdDb.value < steady.measured.loudRmsDb + steady.proposal.inputGainDb.value - 10.0f);
        assert(steady.measured.noiseFloorDb < -58.0f && steady.measured.noiseFloorDb > -68.0f);
        assert(steady.proposal.inputGainDb.value > 0.0f); // plucks peak ~ -13 dBFS
        assert(inRanges(steady.proposal));
        assert(hasLine(steady, "Gate set to") && hasLine(steady, "noise floor measured"));
        assert(hasLine(steady, "Hybrid"));
        assert(a.progress() == 1.0f);
    }

    // 2. Irregular timing -> Drummer Leads with more inertia, less follow than the steady case.
    {
        AutoSetup a;
        const auto sig = render(20.0, noiseAmp, jittered(100.0, 1.4, 19.0, 0.22f, 0.075, 99));
        const auto r = run(a, sig);
        dump("irregular", r);
        assert(r.status == AutoSetupStatus::Ok);
        assert(r.proposal.timingMode.apply && r.proposal.timingMode.value == LeadershipMode::DrummerLeads);
        assert(!r.proposal.leadership.apply);
        assert(r.proposal.tempoInertia.value >= steady.proposal.tempoInertia.value + 0.2f);
        assert(r.proposal.followStrength.value < steady.proposal.followStrength.value);
        assert(r.proposal.confidenceSensitivity.value < steady.proposal.confidenceSensitivity.value);
        assert(inRanges(r.proposal));
        assert(hasLine(r, "Drummer Leads"));
    }

    // 3. Silence -> graceful "not enough signal", no proposals.
    {
        AutoSetup a;
        const std::vector<float> silence(static_cast<std::size_t>(20 * kSr), 0.0f);
        const auto r = run(a, silence);
        assert(a.state() == AutoSetupState::Done);
        assert(r.status == AutoSetupStatus::NotEnoughSignal);
        assert(!r.proposal.any());
        assert(!r.levelsValid && !r.rhythmValid);
        assert(r.lineCount >= 1 && hasLine(r, "could not hear"));
        // Only noise (no playing) is equally "not enough".
        const auto noiseOnly = run(a, render(20.0, noiseAmp, {}));
        assert(noiseOnly.status == AutoSetupStatus::NotEnoughSignal && !noiseOnly.proposal.any());
    }

    // 4. Clipping -> lower gain advice.
    {
        AutoSetup a;
        auto sig = render(20.0, noiseAmp, steadyEighths(100.0, 1.4, 19.0, 1.8f));
        for (auto& v : sig) v = std::clamp(v, -1.0f, 1.0f);
        const auto r = run(a, sig);
        dump("clipping", r);
        assert(r.status == AutoSetupStatus::Ok && r.measured.clipping);
        assert(r.proposal.inputGainDb.apply && r.proposal.inputGainDb.value <= -6.0f);
        assert(hasLine(r, "clipping") && hasLine(r, "Warning"));
        assert(steady.proposal.inputGainDb.value > r.proposal.inputGainDb.value);
    }

    // 5. Cancel mid-run.
    {
        AutoSetup a;
        a.prepare(kSr);
        const auto sig = render(6.0, noiseAmp, steadyEighths(100.0, 1.4, 6.0, 0.22f));
        a.start();
        assert(a.state() == AutoSetupState::Listening && a.prompt() == AutoSetupPrompt::GetReady);
        a.process(sig.data(), static_cast<int>(2 * kSr));
        assert(a.state() == AutoSetupState::Listening && a.progress() > 0.1f && a.progress() < 0.3f);
        a.cancel();
        a.process(sig.data(), 480);
        assert(a.state() == AutoSetupState::Cancelled);
        a.process(sig.data(), 480); // no-op afterwards
        a.finalize();
        assert(a.state() == AutoSetupState::Cancelled);
        assert(!a.result().proposal.any());
        // restartable after cancel
        const auto r = run(a, render(20.0, noiseAmp, steadyEighths(100.0, 1.4, 19.0, 0.22f)));
        assert(r.status == AutoSetupStatus::Ok);
        // cancel after completion discards the result
        a.cancel();
        assert(a.state() == AutoSetupState::Cancelled && a.result().status == AutoSetupStatus::Cancelled && !a.result().proposal.any());
    }

    // 6. Timeout: only sparse short blips for the whole hard cap.
    {
        AutoSetup a;
        std::vector<Pluck> sparse;
        for (double t = 2.0; t < 19.0; t += 3.0) sparse.push_back({t, 0.2f, 196.0f});
        const auto r = run(a, render(20.0, noiseAmp, sparse, 0.015f));
        dump("timeout", r);
        assert(a.state() == AutoSetupState::Done);
        assert(r.status == AutoSetupStatus::TimedOut);
        assert(!r.proposal.any());
        assert(hasLine(r, "sparse"));
        assert(a.progress() == 1.0f);
        // The input must have lasted the full cap (16 s) before giving up.
    }

    // 7. Determinism and block-size independence.
    {
        const auto sig = render(20.0, noiseAmp, jittered(110.0, 1.4, 19.0, 0.3f, 0.02, 5));
        AutoSetup a, b, c, d;
        const auto r1 = run(a, sig, 480);
        const auto r2 = run(b, sig, 480);
        const auto r3 = run(c, sig, 37);
        const auto r4 = run(d, sig, 4096);
        assert(sameResult(r1, r2));
        assert(sameResult(r1, r3));
        assert(sameResult(r1, r4));
        assert(r1.proposal.bpm.value == r3.proposal.bpm.value && r1.proposal.jamStyle.value == r4.proposal.jamStyle.value);
        // Re-running the same object also repeats.
        const auto r5 = run(a, sig, 480);
        assert(sameResult(r1, r5));
    }

    // 8. NaN / Inf / null input.
    {
        AutoSetup a;
        auto sig = render(20.0, noiseAmp, steadyEighths(100.0, 1.4, 19.0, 0.22f));
        const float inf = std::numeric_limits<float>::infinity();
        for (std::size_t i = 0; i < sig.size(); i += 997) sig[i] = (i % 2) ? std::numeric_limits<float>::quiet_NaN() : (i % 3 ? inf : -inf);
        const auto r = run(a, sig);
        dump("nan", r);
        assert(r.status == AutoSetupStatus::Ok && inRanges(r.proposal));
        assert(a.badSampleCount() > 100 && hasLine(r, "invalid input samples"));
        assert(std::abs(r.proposal.bpm.value - 100.0f) <= 3.0f);

        const std::vector<float> allNan(static_cast<std::size_t>(20 * kSr), std::numeric_limits<float>::quiet_NaN());
        const auto rn = run(a, allNan);
        assert(rn.status == AutoSetupStatus::NotEnoughSignal && !rn.proposal.any());

        // Null pointers and absurd sizes never crash; null counts as silence.
        a.start();
        a.process(nullptr, 480);
        a.process(sig.data(), 0);
        a.process(sig.data(), -5);
        for (int i = 0; i < 2000 && a.state() == AutoSetupState::Listening; ++i) a.process(nullptr, 480);
        assert(a.state() == AutoSetupState::Analyzing);
        a.finalize();
        assert(a.result().status == AutoSetupStatus::NotEnoughSignal);

        // Huge finite values are clamped, not propagated.
        std::vector<float> huge(static_cast<std::size_t>(20 * kSr), 1.0e30f);
        const auto rh = run(a, huge);
        assert(inRanges(rh.proposal));
        for (int i = 0; i < rh.lineCount; ++i) assert(std::strstr(rh.line(i), "nan") == nullptr && std::strstr(rh.line(i), "inf") == nullptr);
    }

    // 9. Progress is monotone, prompts sensible, process is idle-safe.
    {
        AutoSetup a;
        a.prepare(kSr);
        std::vector<float> tmp(480, 0.1f);
        a.process(tmp.data(), 480); // Idle: no-op
        assert(a.state() == AutoSetupState::Idle);
        const auto sig = render(14.0, noiseAmp, steadyEighths(100.0, 1.4, 13.0, 0.22f));
        a.start(99.0f); // clamped to 12 s
        float last = 0.0f;
        bool sawPlay = false;
        for (std::size_t pos = 0; pos + 480 <= sig.size() && a.state() == AutoSetupState::Listening; pos += 480) {
            a.process(sig.data() + pos, 480);
            assert(a.progress() >= last - 1e-6f && a.progress() <= 1.0f);
            last = a.progress();
            sawPlay = sawPlay || a.prompt() == AutoSetupPrompt::PlayNaturally;
        }
        assert(sawPlay && std::strlen(autoSetupPromptText(AutoSetupPrompt::PlayNaturally)) > 5);
        assert(a.state() == AutoSetupState::Analyzing);
        assert(a.inputPeakDb() < -9.0f && a.inputPeakDb() > -20.0f);
    }

    // 10. Snapshot / apply / undo.
    {
        AutoSetupProposal p;
        p.inputGainDb = {true, 6.0f};
        p.gateThresholdDb = {true, -50.0f};
        p.bpm = {false, 100.0f}; // user unticked BPM
        p.jamStyle = {true, JamStyle::Blues};
        p.timingMode = {true, LeadershipMode::Hybrid};
        p.leadership = {true, 0.7f};
        p.tempoInertia = {true, std::numeric_limits<float>::quiet_NaN()}; // NaN never passes through
        AutoSetupSnapshot before;
        before.bpm = 120.0f; before.tempoInertia = 0.5f;
        const auto after = applyProposal(p, before);
        assert(after.inputGainDb == 6.0f && after.gateThresholdDb == -50.0f && after.bpm == 120.0f);
        assert(after.jamStyle == JamStyle::Blues && after.timingMode == LeadershipMode::Hybrid && after.tempoInertia == 0.5f);
        AutoSetupUndo undo;
        assert(!undo.available());
        undo.capture(before, p);
        assert(undo.available());
        // The user tweaks BPM and intensity after Apply; undo must not touch them.
        auto edited = after;
        edited.bpm = 133.0f; edited.intensity = 0.9f;
        const auto restored = undo.restore(edited);
        assert(restored.inputGainDb == 0.0f && restored.gateThresholdDb == -90.0f && restored.jamStyle == JamStyle::Rock);
        assert(restored.timingMode == LeadershipMode::DrummerLeads && restored.leadership == 0.5f);
        assert(restored.bpm == 133.0f && restored.intensity == 0.9f);
        assert(!undo.available());
        assert(undo.restore(edited).bpm == 133.0f); // second undo is a no-op
        AutoSetupUndo empty;
        empty.capture(before, AutoSetupProposal{});
        assert(!empty.available());
        // Out-of-range proposal values are clamped on apply.
        AutoSetupProposal wild;
        wild.inputGainDb = {true, 500.0f}; wild.bpm = {true, 5.0f}; wild.gateThresholdDb = {true, 10.0f};
        const auto w = applyProposal(wild, before);
        assert(w.inputGainDb == 36.0f && w.bpm == 40.0f && w.gateThresholdDb == -20.0f);
    }

    // 11. Style choice is transparent and sensible.
    {
        const auto slowSwung = chooseAutoStyle(80.0f, 2.5f, 0.3f, 0.4f, 0.9f);
        assert(slowSwung.best == JamStyle::Blues || slowSwung.best == JamStyle::Shuffle || slowSwung.best == JamStyle::JazzSwing);
        const auto fastHeavy = chooseAutoStyle(175.0f, 6.5f, 0.5f, 0.6f, 0.0f);
        assert(fastHeavy.best == JamStyle::Punk || fastHeavy.best == JamStyle::Metal || fastHeavy.best == JamStyle::HardRock);
        const auto nothing = chooseAutoStyle(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f, -1.0f);
        assert(static_cast<int>(nothing.best) >= 0 && static_cast<int>(nothing.best) < kJamStyleCount);
        const auto again = chooseAutoStyle(80.0f, 2.5f, 0.3f, 0.4f, 0.9f);
        assert(again.best == slowSwung.best && again.second == slowSwung.second);
        // The analyser reports its style choice in plain words.
        assert(hasLine(steady, "Jam Style:"));
    }

    // 12. Swung playing is detected as swung and picks a swing-capable style at a slow tempo.
    {
        AutoSetup a;
        std::vector<Pluck> v;
        const double beat = 60.0 / 90.0;
        int k = 0;
        for (double t0 = 1.4; t0 < 19.0; t0 += beat, ++k) {
            v.push_back({t0, 0.25f, 110.0f});
            v.push_back({t0 + beat * 2.0 / 3.0, 0.15f, 110.0f});
        }
        const auto r = run(a, render(20.0, noiseAmp, v));
        dump("swung", r);
        assert(r.status == AutoSetupStatus::Ok && r.rhythmValid);
        assert(r.measured.swing > 0.6f);
        assert(std::abs(r.proposal.bpm.value - 90.0f) <= 3.0f);
    }

    std::printf("test_auto_setup: all passed\n");
    return 0;
}
