// RoboDrummer(TM) Practice Mode: count-in, bar/section loop and tempo trainer.
// JUCE-free, allocation-free, deterministic state machine. See docs/specs/04-practice-mode.md.
#pragma once
#include "analysis/TimingAuthorityController.h"
#include "core/SectionSequencer.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace robodrummer {

enum class CountInLength : std::uint8_t { Off, OneBeat, TwoBeats, FourBeats, OneBar };
enum class CountInSound : std::uint8_t { Click, Sticks };
enum class LoopMode : std::uint8_t { Bars, Range };
enum class TrainerUnit : std::uint8_t { Bars, LoopPasses };
enum class TrainerDirection : std::uint8_t { Up, Down, PingPong };
enum class PracticePhase : std::uint8_t { Idle, Waiting, CountIn, Playing };

struct CountInSettings {
    CountInLength length{CountInLength::Off};
    CountInSound sound{CountInSound::Click};
    bool onlyOnStart{true}; // false: also a click lead-in over the last beats before every loop wrap
};

struct LoopSettings {
    LoopMode mode{LoopMode::Bars};
    int bars{4};          // Bars mode: 1..64
    long long rangeStartBar{0}; // Range mode: content bar
    int rangeBars{4};     // Range mode: 1..64
};

struct TrainerSettings {
    bool enabled{false};
    double startBpm{80.0};
    double targetBpm{120.0};
    double stepBpm{5.0};
    int every{4};         // bars or loop passes per step, 1..64
    TrainerUnit unit{TrainerUnit::Bars};
    TrainerDirection direction{TrainerDirection::Up};
    bool gate{false};
    float gateMinConfidence{0.6f};
    bool restBar{false};
    int pingPongCycles{0}; // 0 = endless
    bool stopAtTarget{false};
};

struct CountInClick {
    bool active{false};
    bool accent{false};
    int beatNumber{0};  // 1-based within the count-in
    int totalBeats{0};
    CountInSound sound{CountInSound::Click};
};

struct PracticeBeat {
    long long barIndex{0};  // monotonic timeline bar
    int beatInBar{0};       // 0-based
    int numerator{4};
    float trackerConfidence{0.0f};
    [[nodiscard]] bool isBarBoundary() const noexcept { return beatInBar == 0; }
};

struct PracticeAction {
    bool active{false};
    PracticePhase phase{PracticePhase::Idle};
    bool muteGroove{false};
    bool restBar{false};
    CountInClick click{};
    bool bpmOverrideValid{false};
    double bpmOverride{0.0};
    bool tempoHintValid{false};
    double tempoHintBpm{0.0};
    bool tempoStepped{false};
    int trainerStep{0};
    bool gateHeld{false};
    bool trainerFinished{false};
    bool stopRequested{false};
    bool loopStarted{false};
    bool loopWrapped{false};
    long long loopBackToBar{-1};
    int loopPass{0};
    bool contentJump{false};
    long long contentBar{0};
    long long contentBarDelta{0}; // contentBar - barIndex (<= 0 after loops, rest bars)
};

struct LoopRange { long long startBar{0}; int lengthBars{1}; bool valid{false}; };

// Content-bar range covering sections [first,last] (inclusive), derived relative to the sequencer's current position.
template <std::size_t Capacity>
[[nodiscard]] inline LoopRange sectionLoopRange(const SectionSequencer<Capacity>& seq, std::size_t first, std::size_t last,
                                                long long currentContentBar) noexcept {
    LoopRange r;
    if (seq.empty()) return r;
    first = std::min(first, seq.size() - 1);
    last = std::min(std::max(last, first), seq.size() - 1);
    const std::size_t cur = seq.currentSectionIndex();
    long long start = currentContentBar - seq.barsIntoSection();
    if (first <= cur) { for (std::size_t i = first; i < cur; ++i) start -= seq.section(i).bars; }
    else { for (std::size_t i = cur; i < first; ++i) start += seq.section(i).bars; }
    long long len = 0;
    for (std::size_t i = first; i <= last; ++i) len += seq.section(i).bars;
    r.startBar = std::max(0LL, start);
    r.lengthBars = static_cast<int>(std::clamp<long long>(len, 1, 64));
    r.valid = true;
    return r;
}

class PracticeMode {
public:
    PracticeMode() noexcept { buildLadder(); }

    // ---- settings (NaN/Inf/out-of-range safe) ----
    void setCountIn(CountInSettings s) noexcept {
        if (static_cast<int>(s.length) > static_cast<int>(CountInLength::OneBar)) s.length = CountInLength::Off;
        if (static_cast<int>(s.sound) > static_cast<int>(CountInSound::Sticks)) s.sound = CountInSound::Click;
        countIn_ = s;
    }
    void setLoop(LoopSettings s) noexcept {
        if (static_cast<int>(s.mode) > static_cast<int>(LoopMode::Range)) s.mode = LoopMode::Bars;
        s.bars = std::clamp(s.bars, 1, 64);
        s.rangeBars = std::clamp(s.rangeBars, 1, 64);
        s.rangeStartBar = std::max(0LL, s.rangeStartBar);
        loop_ = s;
    }
    void setTrainer(TrainerSettings s) noexcept {
        s.startBpm = bpm(s.startBpm, 80.0);
        s.targetBpm = bpm(s.targetBpm, 120.0);
        s.stepBpm = std::isfinite(s.stepBpm) ? std::clamp(s.stepBpm, 0.1, 200.0) : 5.0;
        s.every = std::clamp(s.every, 1, 64);
        s.gateMinConfidence = std::isfinite(s.gateMinConfidence) ? std::clamp(s.gateMinConfidence, 0.0f, 1.0f) : 0.6f;
        s.pingPongCycles = std::clamp(s.pingPongCycles, 0, 1000);
        if (static_cast<int>(s.unit) > static_cast<int>(TrainerUnit::LoopPasses)) s.unit = TrainerUnit::Bars;
        if (static_cast<int>(s.direction) > static_cast<int>(TrainerDirection::PingPong)) s.direction = TrainerDirection::Up;
        const bool shapeChanged = s.startBpm != trainer_.startBpm || s.targetBpm != trainer_.targetBpm
            || s.stepBpm != trainer_.stepBpm || s.direction != trainer_.direction || s.pingPongCycles != trainer_.pingPongCycles
            || s.enabled != trainer_.enabled;
        trainer_ = s;
        if (shapeChanged) restartTrainer();
    }
    void setAuthority(LeadershipMode mode, float leadership) noexcept {
        mode_ = mode;
        leadership_ = std::isfinite(leadership) ? std::clamp(leadership, 0.0f, 1.0f) : 0.0f;
    }
    void setExternalTempoSource(bool external) noexcept { externalTempo_ = external; }

    [[nodiscard]] const CountInSettings& countInSettings() const noexcept { return countIn_; }
    [[nodiscard]] const LoopSettings& loopSettings() const noexcept { return loop_; }
    [[nodiscard]] const TrainerSettings& trainerSettings() const noexcept { return trainer_; }

    // True when the trainer may replace the BASE tempo fed to the timing authority (see spec 2.6).
    [[nodiscard]] bool trainerDrivesBase() const noexcept {
        if (externalTempo_) return false;
        if (mode_ == LeadershipMode::DrummerLeads) return true;
        return mode_ == LeadershipMode::Hybrid && leadership_ <= 0.5f;
    }

    // ---- session control ----
    void start() noexcept {
        resetRuntime();
        active_ = true;
        pendingCountIn_ = true;
        phase_ = PracticePhase::Playing;
    }
    void resume() noexcept {
        if (!active_) { start(); return; }
        pendingCountIn_ = true;
        phase_ = PracticePhase::Playing;
        barOpen_ = false;
        resting_ = false;
    }
    void stop() noexcept { active_ = false; phase_ = PracticePhase::Idle; pendingCountIn_ = false; }
    void reset() noexcept { resetRuntime(); active_ = false; phase_ = PracticePhase::Idle; }
    void armLoop() noexcept { loopArmed_ = true; }
    void disarmLoop() noexcept { loopArmed_ = false; loopActive_ = false; loopPass_ = 0; }
    void toggleLoop() noexcept { (loopArmed_ || loopActive_) ? disarmLoop() : armLoop(); }
    void restartTrainer() noexcept { buildLadder(); barsSinceStep_ = passesSinceStep_ = 0; clearGate(); stepCount_ = 0; }

    // ---- queries ----
    [[nodiscard]] bool isActive() const noexcept { return active_; }
    [[nodiscard]] PracticePhase phase() const noexcept { return active_ ? phase_ : PracticePhase::Idle; }
    [[nodiscard]] bool loopArmed() const noexcept { return loopArmed_; }
    [[nodiscard]] bool loopActive() const noexcept { return loopActive_; }
    [[nodiscard]] int loopPass() const noexcept { return loopPass_; }
    [[nodiscard]] double trainerBpm() const noexcept { return ladder_.bpm(); }
    [[nodiscard]] int trainerStepCount() const noexcept { return stepCount_; }
    [[nodiscard]] bool trainerFinished() const noexcept { return ladder_.finished; }

    // ---- per-beat driver ----
    [[nodiscard]] PracticeAction onBeat(PracticeBeat b) noexcept {
        PracticeAction a;
        b.barIndex = std::max(0LL, b.barIndex);
        b.numerator = std::clamp(b.numerator, 1, 32);
        b.beatInBar = std::clamp(b.beatInBar, 0, b.numerator - 1);
        const float conf = (std::isfinite(b.trackerConfidence)) ? std::clamp(b.trackerConfidence, 0.0f, 1.0f) : 0.0f;

        if (!active_) { a.contentBar = b.barIndex; return a; }
        a.active = true;

        // Host seek / loop: keep content continuity, re-arm boundary detection.
        if (haveLast_ && (b.barIndex < lastBar_ || b.barIndex > lastBar_ + 1)) {
            offset_ = b.barIndex - contentBar_;
            lastBoundaryBar_ = -1;
            barOpen_ = false;
        }
        haveLast_ = true;
        lastBar_ = b.barIndex;

        const bool boundary = b.isBarBoundary() && b.barIndex != lastBoundaryBar_;
        if (boundary) lastBoundaryBar_ = b.barIndex;

        if (pendingCountIn_) planCountIn(b);
        a.phase = phase_;

        if (phase_ == PracticePhase::Waiting) {
            a.muteGroove = true;
            if (--waitBeats_ <= 0) phase_ = PracticePhase::CountIn; // clicks begin NEXT beat
            // the beat that ends the wait is itself silent; the following beat is the first click
        } else if (phase_ == PracticePhase::CountIn) {
            a.muteGroove = true;
            const int n = clickTotal_ - clicksLeft_ + 1;
            a.click = {true, n == 1 || b.beatInBar == 0, n, clickTotal_, countIn_.sound};
            if (--clicksLeft_ <= 0) phase_ = PracticePhase::Playing;
        } else {
            playBeat(b, conf, boundary, a);
        }

        contentBar_ = b.barIndex - offset_;
        a.contentBar = contentBar_;
        a.contentBarDelta = contentBar_ - b.barIndex;
        a.loopPass = loopPass_;
        a.trainerStep = stepCount_;
        if (trainer_.enabled) {
            const double cur = ladder_.bpm();
            if (trainerDrivesBase()) { a.bpmOverrideValid = true; a.bpmOverride = cur; }
            else { a.tempoHintValid = true; a.tempoHintBpm = cur; }
        }
        return a;
    }

private:
    struct Ladder {
        double begin{80.0}, end{120.0}, step{5.0};
        int n{0}, k{0}, dir{1}, cycles{0}, maxCycles{0};
        bool pingPong{false}, finished{false};
        [[nodiscard]] double bpm() const noexcept {
            if (k >= n) return end;
            const double sign = end >= begin ? 1.0 : -1.0;
            return begin + sign * k * step;
        }
        void stepOnce() noexcept {
            if (finished) return;
            if (!pingPong) { if (k < n) ++k; if (k >= n) finished = true; return; }
            if (dir > 0) {
                if (k < n) ++k; else { dir = -1; if (k > 0) --k; }
            } else {
                if (k > 0) --k;
                if (k <= 0) {
                    ++cycles;
                    if (maxCycles > 0 && cycles >= maxCycles) finished = true;
                    dir = 1;
                }
            }
        }
    };

    static double bpm(double v, double fallback) noexcept {
        return std::clamp(std::isfinite(v) ? v : fallback, 20.0, 400.0);
    }

    void resetRuntime() noexcept {
        phase_ = PracticePhase::Idle; pendingCountIn_ = false; waitBeats_ = clicksLeft_ = clickTotal_ = 0;
        offset_ = 0; contentBar_ = 0; lastBar_ = 0; lastBoundaryBar_ = -1; haveLast_ = false; barOpen_ = false;
        loopArmed_ = loopActive_ = false; loopStart_ = 0; loopLen_ = 1; loopPass_ = 0; resting_ = false;
        restartTrainer();
    }

    void buildLadder() noexcept {
        Ladder l;
        l.step = trainer_.stepBpm;
        l.pingPong = trainer_.direction == TrainerDirection::PingPong;
        l.maxCycles = trainer_.pingPongCycles;
        const double lo = std::min(trainer_.startBpm, trainer_.targetBpm), hi = std::max(trainer_.startBpm, trainer_.targetBpm);
        switch (trainer_.direction) {
            case TrainerDirection::Up: l.begin = lo; l.end = hi; break;
            case TrainerDirection::Down: l.begin = hi; l.end = lo; break;
            case TrainerDirection::PingPong: l.begin = trainer_.startBpm; l.end = trainer_.targetBpm; break;
        }
        const double span = std::abs(l.end - l.begin);
        l.n = span < 1.0e-9 ? 0 : std::max(1, static_cast<int>(std::ceil(span / l.step - 1.0e-9)));
        l.finished = (l.n == 0);
        ladder_ = l;
    }

    void clearGate() noexcept { gateSum_ = 0.0; gateBeats_ = 0; }

    void planCountIn(const PracticeBeat& b) noexcept {
        pendingCountIn_ = false;
        int len = 0;
        switch (countIn_.length) {
            case CountInLength::OneBeat: len = 1; break;
            case CountInLength::TwoBeats: len = 2; break;
            case CountInLength::FourBeats: len = 4; break;
            case CountInLength::OneBar: len = b.numerator; break;
            default: break;
        }
        if (len <= 0) { phase_ = PracticePhase::Playing; return; }
        int total = (b.beatInBar == 0) ? b.numerator : b.numerator - b.beatInBar;
        while (total < len) total += b.numerator;
        waitBeats_ = total - len;
        clickTotal_ = clicksLeft_ = len;
        phase_ = waitBeats_ > 0 ? PracticePhase::Waiting : PracticePhase::CountIn;
    }

    void playBeat(const PracticeBeat& b, float conf, bool boundary, PracticeAction& a) noexcept {
        if (resting_) { a.restBar = true; a.muteGroove = true; }
        if (boundary) {
            bool restEnded = false;
            if (resting_) {
                resting_ = false; restEnded = true;
                ++offset_; a.contentJump = true;
                a.restBar = false; a.muteGroove = false;
            } else if (barOpen_) {
                ++barsSinceStep_;
            }
            long long content = b.barIndex - offset_;

            if (loopArmed_ && !loopActive_) {
                loopActive_ = true; loopArmed_ = false; loopPass_ = 0; a.loopStarted = true;
                if (loop_.mode == LoopMode::Bars) { loopStart_ = content; loopLen_ = loop_.bars; }
                else { loopStart_ = loop_.rangeStartBar; loopLen_ = loop_.rangeBars; }
            }
            bool wrapped = false;
            if (loopActive_ && content >= loopStart_ + loopLen_) {
                offset_ += content - loopStart_;
                content = loopStart_;
                wrapped = true; ++loopPass_; ++passesSinceStep_;
                a.loopWrapped = true; a.loopBackToBar = loopStart_; a.contentJump = true;
            }

            if (!restEnded && barOpen_ && trainer_.enabled && !ladder_.finished) {
                const bool usePasses = trainer_.unit == TrainerUnit::LoopPasses && loopActive_;
                const bool due = usePasses ? (wrapped && passesSinceStep_ >= trainer_.every)
                                           : (barsSinceStep_ >= trainer_.every);
                if (due) stepTrainer(a);
            }
            barOpen_ = true;
        }
        if (!resting_) { gateSum_ += conf; ++gateBeats_; }

        // Optional click lead-in over the last beats of the final loop bar.
        if (!countIn_.onlyOnStart && loopActive_ && countIn_.length != CountInLength::Off && !resting_) {
            const long long content = b.barIndex - offset_;
            if (content == loopStart_ + loopLen_ - 1) {
                int len = countIn_.length == CountInLength::OneBeat ? 1 : countIn_.length == CountInLength::TwoBeats ? 2
                        : countIn_.length == CountInLength::FourBeats ? 4 : b.numerator;
                len = std::min(len, b.numerator);
                const int first = b.numerator - len;
                if (b.beatInBar >= first) {
                    const int n = b.beatInBar - first + 1;
                    a.click = {true, n == 1, n, len, countIn_.sound};
                }
            }
        }
    }

    void stepTrainer(PracticeAction& a) noexcept {
        barsSinceStep_ = 0; passesSinceStep_ = 0;
        const double before = ladder_.bpm();
        Ladder next = ladder_;
        next.stepOnce();
        const double after = next.bpm();
        const double mean = gateBeats_ > 0 ? gateSum_ / gateBeats_ : 0.0;
        clearGate();
        if (trainer_.gate && after > before && mean < static_cast<double>(trainer_.gateMinConfidence)) {
            a.gateHeld = true;
            return;
        }
        ladder_ = next;
        if (after != before) {
            ++stepCount_; a.tempoStepped = true;
            if (trainer_.restBar && !ladder_.finished) resting_ = true, a.restBar = true, a.muteGroove = true;
        }
        if (ladder_.finished) {
            a.trainerFinished = true;
            if (trainer_.stopAtTarget) a.stopRequested = true;
        }
    }

    CountInSettings countIn_{};
    LoopSettings loop_{};
    TrainerSettings trainer_{};
    LeadershipMode mode_{LeadershipMode::DrummerLeads};
    float leadership_{0.0f};
    bool externalTempo_{false};

    bool active_{false};
    PracticePhase phase_{PracticePhase::Idle};
    bool pendingCountIn_{false};
    int waitBeats_{0}, clicksLeft_{0}, clickTotal_{0};
    long long offset_{0}, contentBar_{0}, lastBar_{0}, lastBoundaryBar_{-1};
    bool haveLast_{false}, barOpen_{false};
    bool loopArmed_{false}, loopActive_{false};
    long long loopStart_{0};
    int loopLen_{1}, loopPass_{0};
    bool resting_{false};
    Ladder ladder_{};
    int barsSinceStep_{0}, passesSinceStep_{0}, stepCount_{0};
    double gateSum_{0.0};
    int gateBeats_{0};
};

} // namespace robodrummer
