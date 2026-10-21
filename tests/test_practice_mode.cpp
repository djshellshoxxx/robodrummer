#include "core/PracticeMode.h"
#include <cassert>
#include <limits>
#include <vector>

using namespace robodrummer;

namespace {
struct Sim {
    PracticeMode p;
    long long beat{0};
    int num{4};
    float conf{1.0f};
    PracticeAction step() {
        PracticeBeat b;
        b.barIndex = beat / num;
        b.beatInBar = static_cast<int>(beat % num);
        b.numerator = num;
        b.trackerConfidence = conf;
        ++beat;
        return p.onBeat(b);
    }
    void bars(int n) { for (int i = 0; i < n * num; ++i) (void)step(); }
};

TrainerSettings trainer(double s, double t, double st, int every, TrainerDirection d = TrainerDirection::Up) {
    TrainerSettings ts;
    ts.enabled = true; ts.startBpm = s; ts.targetBpm = t; ts.stepBpm = st; ts.every = every; ts.direction = d;
    return ts;
}

// Runs whole bars and records the BPM override seen on each bar's first beat.
std::vector<double> bpmPerBar(Sim& s, int nBars) {
    std::vector<double> v;
    for (int bar = 0; bar < nBars; ++bar)
        for (int i = 0; i < s.num; ++i) {
            const auto a = s.step();
            if (i == 0) v.push_back(a.bpmOverrideValid ? a.bpmOverride : -1.0);
        }
    return v;
}
}

int main() {
    // Inactive passthrough.
    {
        Sim s;
        const auto a = s.step();
        assert(!a.active && !a.muteGroove && !a.click.active && !a.bpmOverrideValid && a.contentBar == 0);
    }

    // Tempo ramps exactly step by step, every 2 bars, stops at target and holds.
    {
        Sim s;
        s.p.setTrainer(trainer(80, 100, 5, 2));
        s.p.start();
        const auto v = bpmPerBar(s, 14);
        const double expect[] = {80, 80, 85, 85, 90, 90, 95, 95, 100, 100, 100, 100, 100, 100};
        for (int i = 0; i < 14; ++i) assert(v[i] == expect[i]);
        assert(s.p.trainerFinished() && s.p.trainerStepCount() == 4);
    }

    // Non-multiple step lands exactly on target; tempoStepped/finished flags fire once.
    {
        Sim s;
        s.p.setTrainer(trainer(100, 112, 5, 1));
        s.p.start();
        int stepped = 0, finished = 0;
        double last = 0;
        for (int i = 0; i < 40; ++i) {
            const auto a = s.step();
            stepped += a.tempoStepped; finished += a.trainerFinished;
            if (a.bpmOverrideValid) last = a.bpmOverride;
        }
        assert(stepped == 3 && finished == 1 && last == 112.0);
    }

    // Down direction ignores which of start/target is larger; no FP drift over many 0.1 steps.
    {
        Sim s;
        s.p.setTrainer(trainer(60, 120, 0.1, 1, TrainerDirection::Down));
        s.p.start();
        assert(s.step().bpmOverride == 120.0);
        s.bars(700);
        assert(s.p.trainerFinished() && s.p.trainerBpm() == 60.0);
    }

    // Ping-pong: up, hold at target, down, hold at start, repeat; cycle limit stops it.
    {
        Sim s;
        auto t = trainer(90, 100, 5, 1, TrainerDirection::PingPong);
        t.pingPongCycles = 2;
        s.p.setTrainer(t);
        s.p.start();
        const auto v = bpmPerBar(s, 14);
        const double expect[] = {90, 95, 100, 95, 90, 95, 100, 95, 90, 90, 90, 90, 90, 90};
        for (int i = 0; i < 14; ++i) assert(v[i] == expect[i]);
        assert(s.p.trainerFinished());
    }

    // Endless ping-pong never finishes.
    {
        Sim s;
        s.p.setTrainer(trainer(90, 100, 5, 1, TrainerDirection::PingPong));
        s.p.start();
        s.bars(100);
        assert(!s.p.trainerFinished());
    }

    // stopAtTarget requests a stop exactly once; equal start/target is finished without stepping.
    {
        Sim s;
        auto t = trainer(100, 110, 5, 1);
        t.stopAtTarget = true;
        s.p.setTrainer(t);
        s.p.start();
        int stops = 0;
        for (int i = 0; i < 40; ++i) stops += s.step().stopRequested;
        assert(stops == 1);
        Sim e;
        e.p.setTrainer(trainer(100, 100, 5, 1));
        e.p.start();
        e.bars(5);
        assert(e.p.trainerStepCount() == 0 && e.p.trainerBpm() == 100.0);
    }

    // Gate: held while confidence is low, window restarts, steps once confidence is high; down steps are ungated.
    {
        Sim s;
        auto t = trainer(80, 90, 5, 2);
        t.gate = true;
        s.p.setTrainer(t);
        s.p.start();
        s.conf = 0.3f;
        int held = 0;
        for (int i = 0; i < 8 * 4; ++i) held += s.step().gateHeld;
        assert(held == 3 && s.p.trainerBpm() == 80.0);
        s.conf = 0.9f;
        s.bars(2);
        assert(s.p.trainerBpm() == 80.0); // window at bar 8 still holds the old low-confidence bars
        s.bars(1);
        assert(s.p.trainerBpm() == 85.0);
        s.conf = std::numeric_limits<float>::quiet_NaN(); // unknown counts as 0
        int h2 = 0;
        for (int i = 0; i < 4 * 4; ++i) h2 += s.step().gateHeld;
        assert(h2 >= 1 && s.p.trainerBpm() == 85.0);

        Sim d;
        auto td = trainer(80, 90, 5, 1, TrainerDirection::Down);
        td.gate = true;
        d.p.setTrainer(td);
        d.p.start();
        d.conf = 0.0f;
        d.bars(4);
        assert(d.p.trainerBpm() == 80.0);
    }

    // Rest bar after each step: muted, content does not advance.
    {
        Sim s;
        auto t = trainer(80, 90, 5, 1);
        t.restBar = true;
        s.p.setTrainer(t);
        s.p.start();
        s.bars(1);
        std::vector<PracticeAction> bar;
        for (int i = 0; i < 4; ++i) bar.push_back(s.step()); // timeline bar 1: step boundary -> rest
        assert(bar[0].tempoStepped && bar[0].restBar && bar[0].muteGroove && bar[3].restBar);
        assert(bar[0].contentBar == 1);
        bar.clear();
        for (int i = 0; i < 4; ++i) bar.push_back(s.step()); // timeline bar 2: content bar 1 plays, jump flagged
        assert(!bar[0].muteGroove && bar[0].contentJump && bar[0].contentBar == 1 && bar[0].contentBarDelta == -1);
        assert(!bar[1].contentJump);
    }

    // Loop wrap math across meters: N bars always elapse N*numerator beats.
    for (int num : {4, 3, 7}) {
        for (int n : {1, 2, 4, 8}) {
            Sim s;
            s.num = num;
            LoopSettings l; l.bars = n;
            s.p.setLoop(l);
            s.p.start();
            s.bars(1);
            s.p.armLoop();
            long long armedAtBeat = s.beat;
            int wraps = 0;
            long long firstWrapBeat = -1;
            for (long long i = 0; i < static_cast<long long>(num) * n * 3 + num; ++i) {
                const long long beatNow = s.beat;
                const auto a = s.step();
                if (i == 0) { assert(a.loopStarted && a.contentBar == 1); }
                if (a.loopWrapped) {
                    ++wraps;
                    if (firstWrapBeat < 0) firstWrapBeat = beatNow;
                    assert(a.loopBackToBar == 1 && a.contentBar == 1 && a.contentJump);
                    assert(a.contentBarDelta == 1 - (beatNow / num));
                }
            }
            assert(firstWrapBeat - armedAtBeat == static_cast<long long>(n) * num);
            assert(wraps == 3);
        }
    }

    // Arming mid-bar waits for the next bar boundary.
    {
        Sim s;
        s.p.start();
        for (int i = 0; i < 6; ++i) (void)s.step(); // bar 1 beat 1 next
        s.p.armLoop();
        assert(!s.p.loopActive());
        (void)s.step(); (void)s.step();
        const auto a = s.step(); // bar 2 beat 0
        assert(a.loopStarted && a.contentBar == 2 && s.p.loopActive());
    }

    // Range loop (from a section sequencer) and disarm keeps content continuous.
    {
        SectionSequencer<8> seq;
        assert(seq.add({JamStyle::Rock, 2, 0.5f, true}));
        assert(seq.add({JamStyle::Funk, 4, 0.5f, true}));
        assert(seq.add({JamStyle::Blues, 3, 0.5f, true}));
        seq.restorePosition(1, 1); // content bar 3 is section 1, 1 bar in
        auto r = sectionLoopRange(seq, 1, 2, 3);
        assert(r.valid && r.startBar == 2 && r.lengthBars == 7);
        r = sectionLoopRange(seq, 0, 0, 3);
        assert(r.startBar == 0 && r.lengthBars == 2);
        r = sectionLoopRange(seq, 2, 9, 3);
        assert(r.startBar == 6 && r.lengthBars == 3);

        Sim s;
        LoopSettings l; l.mode = LoopMode::Range; l.rangeStartBar = 2; l.rangeBars = 2;
        s.p.setLoop(l);
        s.p.start();
        s.p.armLoop();
        s.bars(2); // plays bars 0,1 through
        std::vector<long long> content;
        for (int b = 0; b < 6; ++b) { content.push_back(s.step().contentBar); s.bars(0); for (int i = 1; i < 4; ++i) (void)s.step(); }
        const long long expect[] = {2, 3, 2, 3, 2, 3};
        for (int i = 0; i < 6; ++i) assert(content[i] == expect[i]);
        s.p.disarmLoop();
        const auto a = s.step();
        assert(!a.loopWrapped);
        long long prev = a.contentBar;
        for (int i = 0; i < 12; ++i) { const auto b = s.step(); assert(b.contentBar >= prev); prev = b.contentBar; }
    }

    // Loop-pass unit: tempo steps once per pass.
    {
        Sim s;
        LoopSettings l; l.bars = 2;
        s.p.setLoop(l);
        auto t = trainer(80, 100, 5, 2, TrainerDirection::Up);
        t.unit = TrainerUnit::LoopPasses;
        s.p.setTrainer(t);
        s.p.start();
        s.p.armLoop();
        s.bars(2 * 2 * 3); // 6 passes of 2 bars
        assert(s.p.loopPass() == 5 || s.p.loopPass() == 6);
        assert(s.p.trainerBpm() == 90.0 || s.p.trainerBpm() == 85.0 || s.p.trainerBpm() == 95.0);
        const double b = s.p.trainerBpm();
        s.bars(4); // two more passes -> one more step
        assert(s.p.trainerBpm() == b + 5.0);
    }

    // Count-in beat counts and bar alignment.
    {
        struct Case { CountInLength len; int beats; int num; };
        const Case cases[] = {{CountInLength::OneBeat, 1, 4}, {CountInLength::TwoBeats, 2, 4}, {CountInLength::FourBeats, 4, 4},
                              {CountInLength::OneBar, 4, 4}, {CountInLength::OneBar, 3, 3}, {CountInLength::OneBar, 7, 7},
                              {CountInLength::FourBeats, 4, 3}, {CountInLength::TwoBeats, 2, 3}};
        for (const auto& c : cases)
            for (int startBeat = 0; startBeat < c.num; ++startBeat) {
                Sim s;
                s.num = c.num;
                s.beat = startBeat + 40 * c.num;
                CountInSettings ci; ci.length = c.len;
                s.p.setCountIn(ci);
                s.p.start();
                int clicks = 0, firstPlay = -1;
                bool lastClickBeforeBoundary = false;
                for (int i = 0; i < c.num * 6; ++i) {
                    const long long beatNow = s.beat;
                    const auto a = s.step();
                    if (a.click.active) {
                        ++clicks;
                        assert(a.muteGroove && a.click.totalBeats == c.beats && a.click.beatNumber == clicks);
                        if (clicks == 1) assert(a.click.accent);
                        lastClickBeforeBoundary = ((beatNow + 1) % c.num) == 0;
                    } else if (!a.muteGroove && firstPlay < 0) {
                        firstPlay = static_cast<int>(beatNow % c.num);
                    }
                    if (a.phase == PracticePhase::Playing) assert(!a.muteGroove);
                }
                assert(clicks == c.beats && lastClickBeforeBoundary && firstPlay == 0);
            }
        // Off: plays immediately.
        Sim s;
        s.p.start();
        const auto a = s.step();
        assert(!a.muteGroove && !a.click.active && a.phase == PracticePhase::Playing);
    }

    // Count-in uses trainer start BPM, and resume re-counts without resetting the trainer.
    {
        Sim s;
        CountInSettings ci; ci.length = CountInLength::OneBar; ci.sound = CountInSound::Sticks;
        s.p.setCountIn(ci);
        s.p.setTrainer(trainer(80, 100, 5, 1));
        s.p.start();
        const auto a = s.step();
        assert(a.click.active && a.click.sound == CountInSound::Sticks && a.bpmOverride == 80.0);
        s.bars(4);
        const int stepsBefore = s.p.trainerStepCount();
        assert(stepsBefore >= 1);
        s.p.resume();
        const auto r = s.step();
        assert(r.muteGroove && s.p.trainerStepCount() == stepsBefore);
    }

    // Loop lead-in overlay when count-in is not start-only: clicks, groove audible.
    {
        Sim s;
        CountInSettings ci; ci.length = CountInLength::TwoBeats; ci.onlyOnStart = false;
        s.p.setCountIn(ci);
        LoopSettings l; l.bars = 2;
        s.p.setLoop(l);
        s.p.start();
        s.bars(4); // finish the count-in wait and settle at a bar boundary
        s.p.armLoop();
        int clicks = 0;
        for (int i = 0; i < 4 * 2; ++i) { const auto a = s.step(); if (a.click.active) { ++clicks; assert(!a.muteGroove); } }
        assert(clicks == 2);
    }

    // Authority composition rule.
    {
        Sim s;
        s.p.setTrainer(trainer(80, 100, 5, 1));
        s.p.start();
        s.p.setAuthority(LeadershipMode::DrummerLeads, 0.9f);
        auto a = s.step();
        assert(a.bpmOverrideValid && !a.tempoHintValid);
        s.p.setAuthority(LeadershipMode::Hybrid, 0.5f);
        a = s.step();
        assert(a.bpmOverrideValid);
        s.p.setAuthority(LeadershipMode::Hybrid, 0.51f);
        a = s.step();
        assert(!a.bpmOverrideValid && a.tempoHintValid && a.tempoHintBpm == 80.0);
        s.p.setAuthority(LeadershipMode::GuitaristLeads, 0.0f);
        a = s.step();
        assert(!a.bpmOverrideValid && a.tempoHintValid);
        s.p.setAuthority(LeadershipMode::DrummerLeads, 0.0f);
        s.p.setExternalTempoSource(true);
        a = s.step();
        assert(!a.bpmOverrideValid && a.tempoHintValid);
        s.p.setAuthority(LeadershipMode::Hybrid, std::numeric_limits<float>::quiet_NaN());
        s.p.setExternalTempoSource(false);
        assert(s.p.trainerDrivesBase());
    }

    // Host seek keeps content continuity; stop and reset neutralize everything.
    {
        Sim s;
        s.p.start();
        s.bars(5);
        const auto before = s.step();
        s.beat = 0; // host jumps back to bar 0
        const auto after = s.step();
        assert(after.active && after.contentBar >= before.contentBar);
        s.p.stop();
        const auto st = s.step();
        assert(!st.active && !st.muteGroove && !st.bpmOverrideValid && st.phase == PracticePhase::Idle);
        s.p.start();
        assert(s.p.isActive());
        s.p.reset();
        assert(!s.p.isActive() && !s.p.loopActive() && s.p.trainerStepCount() == 0);
        // settings survive reset
        TrainerSettings t = trainer(70, 90, 5, 3);
        s.p.setTrainer(t);
        s.p.reset();
        assert(s.p.trainerSettings().every == 3);
    }

    // NaN/Inf/out-of-range setters never leak non-finite output.
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        Sim s;
        TrainerSettings t;
        t.enabled = true; t.startBpm = nan; t.targetBpm = inf; t.stepBpm = nan; t.every = -5;
        t.gateMinConfidence = std::numeric_limits<float>::quiet_NaN();
        t.direction = static_cast<TrainerDirection>(99); t.unit = static_cast<TrainerUnit>(77);
        s.p.setTrainer(t);
        LoopSettings l; l.bars = 0; l.rangeBars = 100000; l.rangeStartBar = -9; l.mode = static_cast<LoopMode>(9);
        s.p.setLoop(l);
        CountInSettings ci; ci.length = static_cast<CountInLength>(200); ci.sound = static_cast<CountInSound>(200);
        s.p.setCountIn(ci);
        assert(s.p.trainerSettings().startBpm == 80.0 && s.p.trainerSettings().targetBpm == 120.0);
        assert(s.p.trainerSettings().stepBpm == 5.0 && s.p.trainerSettings().every == 1);
        assert(s.p.loopSettings().bars == 1 && s.p.loopSettings().rangeBars == 64 && s.p.loopSettings().rangeStartBar == 0);
        assert(s.p.countInSettings().length == CountInLength::Off);
        s.p.start();
        s.p.armLoop();
        for (int i = 0; i < 400; ++i) {
            PracticeBeat b;
            b.barIndex = i / 4 - 3; b.beatInBar = (i % 7) - 2; b.numerator = (i % 5) - 1;
            b.trackerConfidence = (i % 3 == 0) ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
            const auto a = s.p.onBeat(b);
            assert(!a.bpmOverrideValid || (std::isfinite(a.bpmOverride) && a.bpmOverride >= 20.0 && a.bpmOverride <= 400.0));
            assert(a.contentBar >= -100000);
        }
        // Tiny and huge steps.
        TrainerSettings h = trainer(20, 400, 1.0e-12, 1);
        s.p.setTrainer(h);
        assert(s.p.trainerSettings().stepBpm == 0.1);
        h.stepBpm = 1.0e12;
        s.p.setTrainer(h);
        s.p.start();
        s.bars(3);
        assert(s.p.trainerBpm() == 400.0 && s.p.trainerFinished());
    }

    return 0;
}
