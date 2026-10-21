// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once
#include "core/DrumEvent.h"
#include "core/MeterProfile.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace robodrummer {

// Virtual four-limb drummer (see docs/specs/08-virtual-limbs.md). Post-processes one generated bar so that it
// can be played by one person: every hit gets a limb, and hits no limb can reach in time are removed, lowest
// priority first. It only ever keeps or removes events; it never adds, moves or re-voices them.
enum class Limb : std::uint8_t { RightHand, LeftHand, RightFoot, LeftFoot, None };
enum class LimbRemoval : std::uint8_t { Rate, Travel, NoLimb, Evicted };

struct LimbSettings {
    float handMinIntervalMs{85.0f};   // single-stroke limit of one hand (16ths up to ~176 BPM)
    float footMinIntervalMs{105.0f};  // single-stroke limit of one foot (16ths up to ~143 BPM; the double-kick limit)
    float travelBaseMs{25.0f};        // hand moving between two different instruments
    float travelPerZoneMs{18.0f};     // plus this much per kit zone of distance
    float footSwitchMs{60.0f};        // a foot switching between kick and hat pedal
    float footHatMaxVelocity{0.45f};  // closed-hat hits at or below this may be played as a foot chick
    bool doublePedal{false};          // the left foot may play a second kick pedal
    bool carry{false};                // remember each limb's last hit across strictly consecutive bars
};

struct LimbContext {
    double bpm{120.0};
    double sampleRate{48000.0};
    int numerator{4};
    int denominator{4};
};

struct LimbReport {
    static constexpr std::size_t MaxEvents = 512;
    static constexpr std::size_t MaxRemoved = 128;
    struct Removed {
        DrumInstrument instrument{DrumInstrument::Kick};
        int sampleOffset{0};
        float velocity{0.0f};
        LimbRemoval reason{LimbRemoval::Rate};
    };

    std::size_t inputCount{0};
    std::size_t keptCount{0};
    std::size_t removedCount{0};                 // total; `removed` holds the first MaxRemoved
    std::array<std::size_t, 4> removedByReason{};  // indexed by LimbRemoval
    std::size_t nonDefaultLimb{0};               // kept hits played by a limb other than the instrument's usual one
    std::size_t protectedKept{0};
    std::size_t protectedDropped{0};
    bool overflow{false};                        // input larger than MaxEvents: left untouched
    std::array<Limb, MaxEvents> limb{};          // limb of each kept (output) event
    std::array<Removed, MaxRemoved> removed{};

    void clear() noexcept {
        inputCount = keptCount = removedCount = nonDefaultLimb = protectedKept = protectedDropped = 0;
        removedByReason.fill(0);
        overflow = false;
    }
};

class LimbModel {
public:
    static constexpr std::size_t MaxEvents = LimbReport::MaxEvents;

    void setSettings(const LimbSettings& s) noexcept {
        settings_ = s;
        settings_.handMinIntervalMs = sane(s.handMinIntervalMs, 85.0f, 10.0f, 1000.0f);
        settings_.footMinIntervalMs = sane(s.footMinIntervalMs, 105.0f, 10.0f, 1000.0f);
        settings_.travelBaseMs = sane(s.travelBaseMs, 25.0f, 0.0f, 500.0f);
        settings_.travelPerZoneMs = sane(s.travelPerZoneMs, 18.0f, 0.0f, 200.0f);
        settings_.footSwitchMs = sane(s.footSwitchMs, 60.0f, 0.0f, 500.0f);
        settings_.footHatMaxVelocity = sane(s.footHatMaxVelocity, 0.45f, 0.0f, 1.0f);
        carryValid_ = false;
    }
    [[nodiscard]] const LimbSettings& settings() const noexcept { return settings_; }
    void resetCarry() noexcept { carryValid_ = false; }

    // Highest sustained 16th-note tempo one limb can play under the given settings.
    [[nodiscard]] static double maxSixteenthBpm(float minIntervalMs) noexcept {
        return minIntervalMs > 0.0f ? 15000.0 / static_cast<double>(minIntervalMs) : 0.0;
    }

    [[nodiscard]] static Limb defaultLimb(DrumInstrument i) noexcept {
        switch (i) {
            case DrumInstrument::Kick: return Limb::RightFoot;
            case DrumInstrument::Snare:
            case DrumInstrument::Rimshot:
            case DrumInstrument::Sidestick:
            case DrumInstrument::Clap:
            case DrumInstrument::HighTom: return Limb::LeftHand;
            default: return Limb::RightHand;
        }
    }

    // Rewrites `events` in place with the playable subset (original order and sequence numbers preserved) and returns
    // the new count. amount 0 (or NaN) is a bit-exact passthrough.
    std::size_t process(DrumEvent* events, std::size_t count, const LimbContext& ctx, float amount,
                        LimbReport* report = nullptr) noexcept {
        if (report != nullptr) { report->clear(); report->inputCount = count; }
        const bool valid = events != nullptr && std::isfinite(ctx.bpm) && ctx.bpm > 0.0 && std::isfinite(ctx.sampleRate) &&
                           ctx.sampleRate > 0.0 && ctx.numerator > 0 && ctx.denominator > 0;
        if (!valid || count == 0 || !(amount > 0.0f)) {
            if (report != nullptr) report->keptCount = count;
            // Passthrough reports every kept event on its default limb so the UI has something consistent to show.
            if (report != nullptr && events != nullptr)
                for (std::size_t i = 0; i < count && i < MaxEvents; ++i) report->limb[i] = defaultLimb(events[i].instrument);
            return count;
        }
        if (count > MaxEvents) {
            if (report != nullptr) { report->keptCount = count; report->overflow = true; }
            return count;
        }
        amount = std::min(amount, 1.0f);

        const double spb = ctx.sampleRate * 60.0 / ctx.bpm * (4.0 / static_cast<double>(ctx.denominator));
        const double barSamples = spb * static_cast<double>(ctx.numerator);
        const double window = std::clamp(spb * 0.125, 0.005 * ctx.sampleRate, 0.040 * ctx.sampleRate);
        const auto meter = MeterProfile::forMeter(ctx.numerator, ctx.denominator);

        // Priorities and protection.
        for (std::size_t i = 0; i < count; ++i) {
            const auto& e = events[i];
            const double off = static_cast<double>(e.sampleOffset);
            const double beat = off / spb;
            const double nearest = std::floor(beat + 0.5);
            const double beatDist = std::fabs(off - nearest * spb);
            const double halfDist = std::fabs(off - std::floor(beat * 2.0 + 0.5) * 0.5 * spb);
            const bool onBeat = beatDist <= window;
            const bool onEighth = halfDist <= window;
            const bool downbeat = std::fabs(off) <= window;
            const float vel = std::isfinite(e.velocity) ? std::clamp(e.velocity, 0.0f, 1.0f) : 0.0f;
            bool prot = false;
            float base = 30.0f;
            switch (e.instrument) {
                case DrumInstrument::Snare:
                    base = vel < 0.4f ? 20.0f : 70.0f; break;
                case DrumInstrument::Rimshot:
                case DrumInstrument::Sidestick:
                case DrumInstrument::Clap: base = 65.0f; break;
                case DrumInstrument::Kick: base = 60.0f; break;
                case DrumInstrument::Crash: base = 55.0f; break;
                case DrumInstrument::HighTom:
                case DrumInstrument::MidTom:
                case DrumInstrument::FloorTom:
                case DrumInstrument::China: base = 50.0f; break;
                case DrumInstrument::Ride: base = 45.0f; break;
                case DrumInstrument::OpenHat: base = 40.0f; break;
                case DrumInstrument::ClosedHat: base = 35.0f; break;
                case DrumInstrument::Splash: base = 40.0f; break;
                default: base = 30.0f; break;
            }
            if (e.instrument == DrumInstrument::Kick || e.instrument == DrumInstrument::Crash) prot = downbeat;
            if (isSnareFamily(e.instrument) && vel >= 0.5f && onBeat) {
                const auto k = static_cast<long long>(nearest);
                prot = k >= 0 && k < ctx.numerator && meter.backbeat[static_cast<std::size_t>(k)];
            }
            prio_[i] = base + (onBeat ? 10.0f : (onEighth ? 4.0f : 0.0f)) + 0.5f * vel + (prot ? 1000.0f : 0.0f);
            protected_[i] = prot;
            removed_[i] = false;
            limb_[i] = Limb::None;
            order_[i] = static_cast<std::uint16_t>(i);
        }
        std::sort(order_.begin(), order_.begin() + static_cast<std::ptrdiff_t>(count), [&](std::uint16_t a, std::uint16_t b) {
            if (events[a].sampleOffset != events[b].sampleOffset) return events[a].sampleOffset < events[b].sampleOffset;
            if (prio_[a] != prio_[b]) return prio_[a] > prio_[b];
            return a < b;
        });

        for (auto& st : state_) st.n = 0;
        if (settings_.carry && carryValid_) {
            for (std::size_t l = 0; l < 4; ++l) {
                if (!carry_[l].valid) continue;
                state_[l].hist[0] = { -carry_[l].ago, carry_[l].inst, -1 };
                state_[l].n = 1;
            }
        }

        const double msToSamples = ctx.sampleRate / 1000.0;
        const double simWindow = 6.0 * msToSamples;
        LimbReport::Removed* removedOut = report != nullptr ? report->removed.data() : nullptr;
        auto noteRemoved = [&](std::size_t i, LimbRemoval reason) {
            removed_[i] = true;
            if (report == nullptr) return;
            ++report->removedByReason[static_cast<std::size_t>(reason)];
            if (report->removedCount < LimbReport::MaxRemoved)
                removedOut[report->removedCount] = { events[i].instrument, events[i].sampleOffset, events[i].velocity, reason };
            ++report->removedCount;
        };

        for (std::size_t k = 0; k < count; ++k) {
            const std::size_t i = order_[k];
            const auto& e = events[i];
            const double t = static_cast<double>(e.sampleOffset);
            Cand cands[3];
            const int nc = candidates(e.instrument, e.velocity, cands);

            int pick = -1;
            float pickCost = 0.0f;
            for (int c = 0; c < nc; ++c) {
                if (takesOverStream(cands, c, e.instrument)) continue;
                const auto& st = state_[static_cast<std::size_t>(cands[c].limb)];
                if (!feasible(st, cands[c].limb, e.instrument, t, amount, msToSamples)) continue;
                const float cost = cands[c].penalty + 0.02f * static_cast<float>(travelMsFor(st, cands[c].limb, e.instrument) * static_cast<double>(amount));
                if (pick < 0 || cost < pickCost) { pick = c; pickCost = cost; }
            }
            if (pick >= 0) { assign(cands[pick].limb, t, e.instrument, static_cast<int>(i)); limb_[i] = cands[pick].limb; continue; }

            // No free limb: evict the lowest-priority blocker if it is weaker than the newcomer and its limb then fits.
            int victimCand = -1;
            float victimPrio = 0.0f;
            for (int c = 0; c < nc; ++c) {
                if (takesOverStream(cands, c, e.instrument)) continue;
                auto& st = state_[static_cast<std::size_t>(cands[c].limb)];
                if (st.n == 0) continue;
                const auto top = st.hist[st.n - 1];
                if (top.idx < 0 || protected_[static_cast<std::size_t>(top.idx)] ||
                    !(prio_[static_cast<std::size_t>(top.idx)] < prio_[i]))
                    continue;
                LimbState without = st;
                --without.n;
                if (!feasible(without, cands[c].limb, e.instrument, t, amount, msToSamples)) continue;
                const float p = prio_[static_cast<std::size_t>(top.idx)];
                if (victimCand < 0 || p < victimPrio) { victimCand = c; victimPrio = p; }
            }
            if (victimCand >= 0) {
                auto& st = state_[static_cast<std::size_t>(cands[victimCand].limb)];
                const auto victim = st.hist[st.n - 1];
                --st.n;
                noteRemoved(static_cast<std::size_t>(victim.idx), LimbRemoval::Evicted);
                limb_[static_cast<std::size_t>(victim.idx)] = Limb::None;
                assign(cands[victimCand].limb, t, e.instrument, static_cast<int>(i));
                limb_[i] = cands[victimCand].limb;
                continue;
            }

            // Newcomer loses. Classify using its preferred limb.
            LimbRemoval reason = LimbRemoval::NoLimb;
            if (nc > 0) {
                const auto& st = state_[static_cast<std::size_t>(cands[0].limb)];
                if (st.n > 0) {
                    const auto top = st.hist[st.n - 1];
                    if (t - top.t > simWindow) {
                        const bool foot = isFoot(cands[0].limb);
                        const double strokeMs = foot ? settings_.footMinIntervalMs : settings_.handMinIntervalMs;
                        reason = travelMsFor(st, cands[0].limb, e.instrument) > strokeMs ? LimbRemoval::Travel : LimbRemoval::Rate;
                    }
                }
            }
            noteRemoved(i, reason);
            if (report != nullptr && protected_[i]) ++report->protectedDropped;
        }

        // Compact kept events in original order.
        std::size_t out = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (removed_[i]) continue;
            if (report != nullptr) {
                report->limb[out] = limb_[i];
                if (limb_[i] != defaultLimb(events[i].instrument)) ++report->nonDefaultLimb;
                if (protected_[i]) ++report->protectedKept;
            }
            events[out++] = events[i];
        }
        if (report != nullptr) report->keptCount = out;

        // Remember where each limb ended for the next bar.
        if (settings_.carry) {
            for (std::size_t l = 0; l < 4; ++l) {
                const auto& st = state_[l];
                carry_[l].valid = st.n > 0;
                if (st.n > 0) {
                    carry_[l].ago = std::max(0.0, barSamples - st.hist[st.n - 1].t);
                    carry_[l].inst = st.hist[st.n - 1].inst;
                }
            }
            carryValid_ = true;
        }
        return out;
    }

    // Independent re-check of an output bar against the stroke/travel rules and the instrument-to-limb table.
    // `limbs[i]` is the limb of events[i] (as in LimbReport::limb). Returns the number of violations.
    [[nodiscard]] std::size_t countViolations(const DrumEvent* events, std::size_t count, const Limb* limbs,
                                              const LimbContext& ctx, float amount) const noexcept {
        if (events == nullptr || limbs == nullptr || !(amount > 0.0f) || !(ctx.sampleRate > 0.0)) return 0;
        amount = std::min(amount, 1.0f);
        const double msToSamples = ctx.sampleRate / 1000.0;
        std::size_t violations = 0;
        for (std::size_t i = 0; i < count; ++i) {
            Cand cands[3];
            const int nc = candidates(events[i].instrument, events[i].velocity, cands);
            bool legal = false;
            for (int c = 0; c < nc; ++c) legal = legal || cands[c].limb == limbs[i];
            if (!legal) ++violations;
            // Closest preceding hit on the same limb.
            int prev = -1;
            for (std::size_t j = 0; j < count; ++j) {
                if (j == i || limbs[j] != limbs[i]) continue;
                const bool before = events[j].sampleOffset < events[i].sampleOffset ||
                                    (events[j].sampleOffset == events[i].sampleOffset && j < i);
                if (!before) continue;
                if (prev < 0 || events[j].sampleOffset > events[static_cast<std::size_t>(prev)].sampleOffset ||
                    (events[j].sampleOffset == events[static_cast<std::size_t>(prev)].sampleOffset && j > static_cast<std::size_t>(prev)))
                    prev = static_cast<int>(j);
            }
            if (prev < 0) continue;
            const double gap = static_cast<double>(events[i].sampleOffset - events[static_cast<std::size_t>(prev)].sampleOffset);
            const double need = needSamples(limbs[i], events[static_cast<std::size_t>(prev)].instrument, events[i].instrument, amount, msToSamples);
            if (gap < need) ++violations;
        }
        return violations;
    }

private:
    struct Cand { Limb limb{Limb::None}; float penalty{0.0f}; };
    struct Hit { double t{0.0}; DrumInstrument inst{DrumInstrument::Kick}; int idx{-1}; };
    struct LimbState { std::array<Hit, 4> hist{}; std::size_t n{0}; };
    struct Carry { bool valid{false}; double ago{0.0}; DrumInstrument inst{DrumInstrument::Kick}; };

    static float sane(float v, float fallback, float lo, float hi) noexcept {
        return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
    }
    static bool isSnareFamily(DrumInstrument i) noexcept {
        return i == DrumInstrument::Snare || i == DrumInstrument::Rimshot || i == DrumInstrument::Sidestick || i == DrumInstrument::Clap;
    }
    static bool isFoot(Limb l) noexcept { return l == Limb::RightFoot || l == Limb::LeftFoot; }

    // Position on the kit, left to right as seen from the throne.
    static double zone(DrumInstrument i) noexcept {
        switch (i) {
            case DrumInstrument::Snare: case DrumInstrument::Rimshot: case DrumInstrument::Sidestick: case DrumInstrument::Clap: return -1.0;
            case DrumInstrument::ClosedHat: case DrumInstrument::OpenHat: return -3.0;
            case DrumInstrument::Tambourine: case DrumInstrument::Cowbell: case DrumInstrument::Splash: return -2.5;
            case DrumInstrument::HighTom: return -0.3;
            case DrumInstrument::MidTom: return 0.5;
            case DrumInstrument::FloorTom: return 1.4;
            case DrumInstrument::Ride: return 2.0;
            case DrumInstrument::Crash: return 2.4;
            case DrumInstrument::China: return 2.8;
            default: return 0.0;
        }
    }

    // Milliseconds a limb needs to travel from the previous instrument to this one (0 for the same instrument).
    double travelMs(Limb l, DrumInstrument from, DrumInstrument to) const noexcept {
        if (from == to) return 0.0;
        if (isFoot(l)) return static_cast<double>(settings_.footSwitchMs);
        const double dist = std::fabs(zone(from) - zone(to));
        if (dist < 0.25) return 0.0;
        return static_cast<double>(settings_.travelBaseMs) + static_cast<double>(settings_.travelPerZoneMs) * dist;
    }
    double travelMsFor(const LimbState& st, Limb l, DrumInstrument to) const noexcept {
        return st.n == 0 ? 0.0 : travelMs(l, st.hist[st.n - 1].inst, to);
    }
    double needSamples(Limb l, DrumInstrument from, DrumInstrument to, float amount, double msToSamples) const noexcept {
        const double stroke = static_cast<double>(isFoot(l) ? settings_.footMinIntervalMs : settings_.handMinIntervalMs);
        const double ms = std::max(stroke, travelMs(l, from, to)) * static_cast<double>(amount);
        return std::max(1.0, std::floor(ms * msToSamples));
    }
    bool feasible(const LimbState& st, Limb l, DrumInstrument inst, double t, float amount, double msToSamples) const noexcept {
        if (st.n == 0) return true;
        const auto& last = st.hist[st.n - 1];
        const double need = needSamples(l, last.inst, inst, amount, msToSamples);
        return t - last.t >= need;
    }
    // Hats and cymbals are timekeeping streams: the second hand may join in when the first is busy elsewhere, but
    // may not alternate with it on the same instrument to beat the single-stroke limit.
    bool takesOverStream(const Cand* cands, int c, DrumInstrument inst) const noexcept {
        if (c == 0 || isSnareFamily(inst) || isFoot(cands[c].limb)) return false;
        switch (inst) {
            case DrumInstrument::Kick: case DrumInstrument::HighTom: case DrumInstrument::MidTom: case DrumInstrument::FloorTom:
                return false;
            default: break;
        }
        const auto& primary = state_[static_cast<std::size_t>(cands[0].limb)];
        return primary.n > 0 && primary.hist[primary.n - 1].inst == inst;
    }
    void assign(Limb l, double t, DrumInstrument inst, int idx) noexcept {
        auto& st = state_[static_cast<std::size_t>(l)];
        if (st.n == st.hist.size()) {
            for (std::size_t i = 1; i < st.hist.size(); ++i) st.hist[i - 1] = st.hist[i];
            --st.n;
        }
        st.hist[st.n++] = { t, inst, idx };
    }

    int candidates(DrumInstrument inst, float velocity, Cand* out) const noexcept {
        int n = 0;
        auto add = [&](Limb l, float p) { out[n++] = { l, p }; };
        switch (inst) {
            case DrumInstrument::Kick:
                add(Limb::RightFoot, 0.0f);
                if (settings_.doublePedal) add(Limb::LeftFoot, 2.0f);
                break;
            case DrumInstrument::Snare: case DrumInstrument::Rimshot: case DrumInstrument::Sidestick: case DrumInstrument::Clap:
                add(Limb::LeftHand, 0.0f); add(Limb::RightHand, 4.0f); break;
            case DrumInstrument::HighTom:
                add(Limb::LeftHand, 0.0f); add(Limb::RightHand, 0.0f); break;
            case DrumInstrument::MidTom:
                add(Limb::RightHand, 0.0f); add(Limb::LeftHand, 1.0f); break;
            case DrumInstrument::FloorTom:
                add(Limb::RightHand, 0.0f); add(Limb::LeftHand, 2.0f); break;
            case DrumInstrument::ClosedHat:
                add(Limb::RightHand, 0.0f); add(Limb::LeftHand, 4.0f);
                if (std::isfinite(velocity) && velocity <= settings_.footHatMaxVelocity) add(Limb::LeftFoot, 2.0f);
                break;
            case DrumInstrument::OpenHat: case DrumInstrument::Ride:
                add(Limb::RightHand, 0.0f); add(Limb::LeftHand, 4.0f); break;
            default:
                add(Limb::RightHand, 0.0f); add(Limb::LeftHand, 3.0f); break;
        }
        return n;
    }

    LimbSettings settings_{};
    std::array<float, MaxEvents> prio_{};
    std::array<bool, MaxEvents> protected_{};
    std::array<bool, MaxEvents> removed_{};
    std::array<Limb, MaxEvents> limb_{};
    std::array<std::uint16_t, MaxEvents> order_{};
    std::array<LimbState, 4> state_{};
    std::array<Carry, 4> carry_{};
    bool carryValid_{false};
};

} // namespace robodrummer
