#pragma once
// Always-on recorder of generated drum hits + tempo/meter maps. Audio thread produces into a fixed lock-free
// SPSC ring; the message thread drains into per-take history and builds exports. See docs/specs/05-midi-capture.md.
#include "DrumEvent.h"
#include "MidiFileWriter.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace robodrummer {

inline constexpr double kMaxCaptureSeconds = 600.0;
inline constexpr std::size_t kMaxNotesPerTake = 131072;
inline constexpr std::size_t kMaxTakes = 8;
inline constexpr std::uint32_t kDefaultNoteTicks = 120;

struct CaptureBlock {            // per-audio-block facts (audio thread)
    bool hostPpqValid{false};
    double ppq{0.0};             // quarter-note position at block start (host)
    bool playing{true};
    double bpm{120.0};
    int numerator{4};
    int denominator{4};
    int numSamples{0};
    double sampleRate{44100.0};
};

struct CaptureRequest {
    int bars{8};                 // 4/8/16/32; 0 = all since start (capped at kMaxCaptureSeconds)
    bool completedBarsOnly{true};
    int takeIndex{-1};           // -1 = newest take that has notes
    DrumMap map{DrumMap::gm()};
};

enum class CaptureStatus { Ok, Empty, InvalidRequest, WriteFailed };

struct CaptureResult {
    CaptureStatus status{CaptureStatus::Empty};
    MidiWriteResult write{MidiWriteResult::Ok};
    int bars{0};
    double seconds{0.0};
    std::size_t noteCount{0};
};

struct CaptureTake {
    std::int64_t origin{0};      // first tick of the take
    std::int64_t anchor{0};      // bar-grid anchor (<= origin)
    std::int64_t latest{0};      // furthest position reached
    bool truncated{false};
    struct Note { std::int64_t tick; std::uint32_t dur; std::uint8_t instrument; std::uint8_t velocity; };
    std::vector<Note> notes;
    std::vector<ExportTempo> tempos;
    std::vector<ExportMeter> meters;
};

template <std::size_t RingCapacity = 8192>
class MidiCapture {
    static_assert(RingCapacity >= 8 && (RingCapacity & (RingCapacity - 1)) == 0, "RingCapacity must be a power of two");
public:
    static constexpr int kPpq = kMidiPpq;
    static constexpr std::size_t kRingCapacity = RingCapacity;

    // ======================= audio thread (noexcept, allocation-free) =======================
    void onBlock(const CaptureBlock& b) noexcept {
        if (b.numSamples <= 0 || !(b.sampleRate > 1.0) || !std::isfinite(b.sampleRate)) return;
        const std::uint32_t gen = clearGen_.load(std::memory_order_acquire);
        if (gen != seenClearGen_) { seenClearGen_ = gen; resync_ = true; }
        if (!b.playing) { started_ = false; return; }

        double bpm = lastBpm_;
        if (std::isfinite(b.bpm) && b.bpm > 0.0) bpm = std::clamp(b.bpm, 4.0, 960.0);
        const int num = std::clamp(b.numerator, 1, 64);
        const int den = (b.denominator == 1 || b.denominator == 2 || b.denominator == 4 || b.denominator == 8 || b.denominator == 16) ? b.denominator : 4;
        const double tps = kPpq * bpm / 60.0 / b.sampleRate;

        double tickD;
        if (b.hostPpqValid && std::isfinite(b.ppq) && std::fabs(b.ppq) < 1.0e9) tickD = b.ppq * kPpq;
        else tickD = freeTick_;
        const std::int64_t tick = static_cast<std::int64_t>(std::llround(tickD));
        freeTick_ = tickD + b.numSamples * tps;
        if (tick < 0) { started_ = false; return; }  // pre-roll / count-in: not recorded

        if (started_ && b.hostPpqValid && std::fabs(tickD - expectedTick_) > 480.0 + prevBlockTicks_) resync_ = true;
        const double blockTicks = b.numSamples * tps;
        const std::int64_t endTick = tick + static_cast<std::int64_t>(std::llround(blockTicks));

        if (!started_ || resync_) {
            const std::int64_t barLen = static_cast<std::int64_t>(num) * (4 * kPpq) / den;
            const std::int64_t into = b.hostPpqValid ? tick % std::max<std::int64_t>(1, barLen) : 0;
            Rec r{}; r.kind = kSegment; r.tick = tick; r.aux = static_cast<std::uint32_t>(into); r.value = static_cast<double>(prevEnd_);  // end of the previous take
            started_ = false;
            if (push(r)) {
                Rec t{}; t.kind = kTempo; t.tick = tick; t.value = bpm;
                Rec m{}; m.kind = kMeter; m.tick = tick; m.a = static_cast<std::uint8_t>(num); m.b = static_cast<std::uint8_t>(den);
                started_ = true;
                ++segSeq_;
                resync_ = !(push(t) && push(m));  // incomplete maps: begin another take next block
            }
            lastBpm_ = bpm; lastNum_ = num; lastDen_ = den;
        } else {
            if (std::fabs(bpm - lastBpm_) > 1.0e-3) {
                Rec t{}; t.kind = kTempo; t.tick = tick; t.value = bpm;
                if (!push(t)) resync_ = true;
                lastBpm_ = bpm;
            }
            if (num != lastNum_ || den != lastDen_) {
                Rec m{}; m.kind = kMeter; m.tick = tick; m.a = static_cast<std::uint8_t>(num); m.b = static_cast<std::uint8_t>(den);
                if (!push(m)) resync_ = true;
                lastNum_ = num; lastDen_ = den;
            }
        }
        blockTick_ = tick; tps_ = tps; expectedTick_ = tickD + blockTicks; prevBlockTicks_ = blockTicks;
        prevEnd_ = endTick;
        latest_.store((static_cast<std::uint64_t>(segSeq_ & 0xFFFFFFu) << 40) | static_cast<std::uint64_t>(endTick), std::memory_order_release);
    }

    // Call after onBlock for the same block. Returns false if not recorded.
    bool noteAtSample(int sampleOffset, DrumInstrument instrument, float velocity,
                      std::uint32_t durationTicks = kDefaultNoteTicks) noexcept {
        if (!started_ || static_cast<unsigned>(instrument) >= 16) return false;
        const double off = static_cast<double>(std::max(0, sampleOffset)) * tps_;
        Rec r{}; r.kind = kNote; r.a = static_cast<std::uint8_t>(instrument);
        r.b = static_cast<std::uint8_t>(std::isfinite(velocity) ? std::clamp(static_cast<int>(std::lround(velocity * 127.0f)), 1, 127) : 64);
        r.aux = std::clamp<std::uint32_t>(durationTicks, 1, 0xFFFFFu);
        r.tick = blockTick_ + static_cast<std::int64_t>(std::llround(off));
        if (push(r)) return true;
        resync_ = true;
        return false;
    }

    // ======================= message thread =======================
    void drain() {
        const std::uint64_t latestBefore = latest_.load(std::memory_order_acquire);  // read BEFORE draining the ring
        const auto head = head_.load(std::memory_order_acquire);
        auto tail = tail_.load(std::memory_order_relaxed);
        const bool changed = tail != head;
        for (; tail != head; ++tail) apply(ring_[tail & (RingCapacity - 1)]);
        tail_.store(tail, std::memory_order_release);
        if (!takes_.empty()) {
            auto& t = takes_.back();
            // The packed value carries the take sequence it belongs to; only apply it to that take.
            if (static_cast<std::uint32_t>(latestBefore >> 40) == (seenSegments_ & 0xFFFFFFu))
                t.latest = std::max(t.latest, static_cast<std::int64_t>(latestBefore & ((1ull << 40) - 1)));
            if (changed) trim(t);
        }
        if (changed) ++version_;
    }

    void requestClear() noexcept {  // message thread
        tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release);
        takes_.clear();  // seenSegments_ keeps counting so stale latest values never match a later take
        clearGen_.fetch_add(1, std::memory_order_release);
        ++version_;
    }

    [[nodiscard]] const std::vector<CaptureTake>& takes() const noexcept { return takes_; }
    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }
    [[nodiscard]] std::uint32_t droppedCount() const noexcept { return dropped_.load(std::memory_order_relaxed); }

    // Builds export data without writing bytes.
    [[nodiscard]] CaptureResult buildExport(const CaptureRequest& req, MidiExportData& out) const {
        out = MidiExportData{};
        CaptureResult res;
        if (req.bars < 0 || req.bars > 4096) { res.status = CaptureStatus::InvalidRequest; return res; }
        const CaptureTake* take = nullptr;
        if (req.takeIndex >= 0) {
            if (static_cast<std::size_t>(req.takeIndex) >= takes_.size()) { res.status = CaptureStatus::InvalidRequest; return res; }
            take = &takes_[static_cast<std::size_t>(req.takeIndex)];
        } else {
            for (auto it = takes_.rbegin(); it != takes_.rend(); ++it)
                if (!it->notes.empty()) { take = &*it; break; }
        }
        if (!take || take->notes.empty()) return res;  // Empty

        const auto edges = barEdges(*take, take->latest);  // starts of bars; last edge > latest
        std::size_t cur = 0;  // bar containing latest
        while (cur + 2 < edges.size() && edges[cur + 1] <= take->latest) ++cur;
        if (take->latest < edges[0]) cur = 0;
        const std::size_t endIdx = (req.completedBarsOnly && cur >= 1) ? cur : std::min(cur + 1, edges.size() - 1);
        const std::size_t n = req.bars == 0 ? endIdx : std::min<std::size_t>(endIdx, static_cast<std::size_t>(req.bars));
        const std::size_t startIdx = endIdx - n;
        const std::int64_t ws = edges[startIdx], we = edges[endIdx];
        if (we <= ws) return res;

        std::array<bool, 16> used{};
        for (const auto& nt : take->notes) {
            if (nt.tick < ws || nt.tick >= we || nt.instrument >= 16) continue;
            const int mn = req.map.noteFor(static_cast<DrumInstrument>(nt.instrument));
            if (mn < 0) continue;
            ExportNote e;
            e.tick = nt.tick - ws;
            e.duration = static_cast<std::uint32_t>(std::min<std::int64_t>(nt.dur, we - nt.tick));
            e.note = static_cast<std::uint8_t>(mn);
            e.velocity = nt.velocity;
            e.instrument = static_cast<std::int8_t>(nt.instrument);
            out.notes.push_back(e);
            used[nt.instrument] = true;
        }
        if (out.notes.empty()) return res;

        auto stateAt = [&](const auto& v, auto make) {
            using V = std::decay_t<decltype(v.front())>;
            V cur0{}; bool have = false;
            std::vector<V> r;
            for (const auto& x : v) {
                if (x.tick <= ws) { cur0 = x; have = true; continue; }
                break;
            }
            if (!have && !v.empty()) cur0 = v.front();
            if (have || !v.empty()) r.push_back(make(cur0, 0));
            for (const auto& x : v) if (x.tick > ws && x.tick < we) r.push_back(make(x, x.tick - ws));
            return r;
        };
        out.tempos = stateAt(take->tempos, [](ExportTempo t, std::int64_t tk) { t.tick = tk; return t; });
        out.meters = stateAt(take->meters, [](ExportMeter m, std::int64_t tk) { m.tick = tk; return m; });
        dedupe(out.tempos, [](auto& a, auto& b) { return a.bpm == b.bpm; });
        dedupe(out.meters, [](auto& a, auto& b) { return a.numerator == b.numerator && a.denominator == b.denominator; });
        out.lengthTicks = we - ws;

        std::string txt = "Drum map:";
        bool first = true;
        for (int i = 0; i < 16; ++i) {
            if (!used[static_cast<std::size_t>(i)]) continue;
            txt += first ? " " : ", "; first = false;
            txt += std::to_string(req.map.noteFor(static_cast<DrumInstrument>(i))) + "=" + drumInstrumentName(static_cast<DrumInstrument>(i));
        }
        out.drumMapText = txt;

        res.status = CaptureStatus::Ok;
        res.bars = static_cast<int>(n);
        res.noteCount = out.notes.size();
        res.seconds = secondsBetween(out.tempos, 0, out.lengthTicks);
        return res;
    }

    [[nodiscard]] CaptureResult exportSmf(const CaptureRequest& req, std::vector<std::uint8_t>& bytes) const {
        MidiExportData d;
        auto res = buildExport(req, d);
        bytes.clear();
        if (res.status != CaptureStatus::Ok) return res;
        res.write = writeStandardMidiFile(d, bytes);
        if (res.write != MidiWriteResult::Ok) res.status = CaptureStatus::WriteFailed;
        return res;
    }

private:
    enum : std::uint8_t { kSegment = 1, kTempo, kMeter, kNote };
    struct Rec { std::uint8_t kind{0}, a{0}, b{0}, c{0}; std::uint32_t aux{0}; std::int64_t tick{0}; double value{0.0}; };
    static_assert(sizeof(Rec) == 24);

    bool push(const Rec& r) noexcept {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= RingCapacity) { dropped_.fetch_add(1, std::memory_order_relaxed); return false; }
        ring_[head & (RingCapacity - 1)] = r;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool apply(const Rec& r) {  // returns true when a new take began
        if (r.kind == kSegment) {
            ++seenSegments_;
            if (!takes_.empty() && r.value >= 0.0) takes_.back().latest = std::max(takes_.back().latest, static_cast<std::int64_t>(r.value));
            CaptureTake t;
            t.origin = t.latest = r.tick;
            t.anchor = r.tick - static_cast<std::int64_t>(r.aux);
            if (takes_.size() >= kMaxTakes) takes_.erase(takes_.begin());
            takes_.push_back(std::move(t));
            return true;
        }
        if (takes_.empty()) return false;  // record from before a clear: ignore
        auto& t = takes_.back();
        switch (r.kind) {
            case kTempo:
                if (!t.tempos.empty() && t.tempos.back().tick == r.tick) t.tempos.back().bpm = r.value;
                else t.tempos.push_back({r.tick, r.value});
                break;
            case kMeter:
                if (!t.meters.empty() && t.meters.back().tick == r.tick) t.meters.back() = {r.tick, r.a, r.b};
                else t.meters.push_back({r.tick, r.a, r.b});
                break;
            case kNote:
                t.notes.push_back({r.tick, r.aux, r.a, r.b});
                t.latest = std::max(t.latest, r.tick);
                break;
        }
        return false;
    }

    // Bar start ticks from the anchor; meter changes restart the grid; final edge is the first one > until.
    static std::vector<std::int64_t> barEdges(const CaptureTake& t, std::int64_t until) {
        std::vector<std::int64_t> v;
        std::int64_t pos = t.anchor;
        int num = t.meters.empty() ? 4 : t.meters.front().numerator;
        int den = t.meters.empty() ? 4 : t.meters.front().denominator;
        std::size_t mi = t.meters.empty() ? 0 : 1;  // first meter governs from the anchor
        for (int guard = 0; guard < 2000000; ++guard) {
            v.push_back(pos);
            if (pos > until) break;
            std::int64_t next = pos + std::max<std::int64_t>(1, static_cast<std::int64_t>(num) * (4 * kPpq) / std::max(1, den));
            while (mi < t.meters.size() && t.meters[mi].tick <= pos) { num = t.meters[mi].numerator; den = t.meters[mi].denominator; ++mi; next = pos + std::max<std::int64_t>(1, static_cast<std::int64_t>(num) * (4 * kPpq) / std::max(1, den)); }
            if (mi < t.meters.size() && t.meters[mi].tick < next) next = t.meters[mi].tick;
            pos = next;
        }
        if (v.size() < 2) v.push_back(v.back() + 1);
        return v;
    }

    static double secondsBetween(const std::vector<ExportTempo>& tempos, std::int64_t a, std::int64_t b) {
        double s = 0.0;
        for (std::size_t i = 0; i < tempos.size(); ++i) {
            const std::int64_t lo = std::max(a, tempos[i].tick);
            const std::int64_t hi = std::min(b, i + 1 < tempos.size() ? tempos[i + 1].tick : b);
            if (hi > lo) s += static_cast<double>(hi - lo) * 60.0 / (tempos[i].bpm * kPpq);
        }
        if (tempos.empty()) s = static_cast<double>(b - a) * 60.0 / (120.0 * kPpq);
        return s;
    }

    template <class V, class Eq> static void dedupe(std::vector<V>& v, Eq eq) {
        std::vector<V> r;
        for (const auto& x : v) if (r.empty() || !eq(r.back(), x)) r.push_back(x);
        v.swap(r);
    }

    void trim(CaptureTake& t) {
        std::int64_t cutoff = t.origin;
        if (t.notes.size() > kMaxNotesPerTake) {
            std::vector<std::int64_t> ticks;
            ticks.reserve(t.notes.size());
            for (const auto& n : t.notes) ticks.push_back(n.tick);
            const auto k = t.notes.size() - kMaxNotesPerTake;
            std::nth_element(ticks.begin(), ticks.begin() + static_cast<std::ptrdiff_t>(k), ticks.end());
            cutoff = std::max(cutoff, ticks[k]);
        }
        double remain = kMaxCaptureSeconds;
        std::int64_t end = t.latest;
        for (std::size_t i = t.tempos.size(); i-- > 0;) {
            const std::int64_t s = std::max(t.tempos[i].tick, t.origin);
            if (s >= end) continue;
            const double secs = static_cast<double>(end - s) * 60.0 / (t.tempos[i].bpm * kPpq);
            if (secs >= remain) { cutoff = std::max(cutoff, end - static_cast<std::int64_t>(remain * t.tempos[i].bpm * kPpq / 60.0)); break; }
            remain -= secs; end = s;
        }
        if (cutoff <= t.origin) return;
        for (const auto e : barEdges(t, t.latest)) if (e >= cutoff) { cutoff = e; break; }
        if (cutoff > t.latest) return;
        cutAt(t, cutoff);
    }

    static void cutAt(CaptureTake& t, std::int64_t cutoff) {
        t.notes.erase(std::remove_if(t.notes.begin(), t.notes.end(), [&](auto& n) { return n.tick < cutoff; }), t.notes.end());
        auto carry = [&](auto& v) {
            if (v.empty()) return;
            std::size_t keepFrom = 0;
            while (keepFrom < v.size() && v[keepFrom].tick <= cutoff) ++keepFrom;
            auto state = v[keepFrom == 0 ? 0 : keepFrom - 1];
            state.tick = cutoff;
            v.erase(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(keepFrom));
            v.insert(v.begin(), state);
        };
        carry(t.tempos); carry(t.meters);
        t.origin = t.anchor = cutoff;
        t.truncated = true;
    }

    // ring
    std::array<Rec, RingCapacity> ring_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    std::atomic<std::uint32_t> dropped_{0};
    std::atomic<std::uint64_t> latest_{0};  // (take sequence << 40) | end tick of the last block
    std::atomic<std::uint32_t> clearGen_{0};
    // audio-thread state
    bool started_{false}, resync_{false};
    std::uint32_t seenClearGen_{0};
    double freeTick_{0.0}, expectedTick_{0.0}, prevBlockTicks_{0.0}, tps_{0.0}, lastBpm_{120.0};
    std::int64_t blockTick_{0};
    std::uint32_t segSeq_{0};
    std::int64_t prevEnd_{-1};
    int lastNum_{4}, lastDen_{4};
    // message-thread state
    std::vector<CaptureTake> takes_;
    std::uint64_t version_{0};
    std::uint32_t seenSegments_{0};
};

}  // namespace robodrummer
