// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
// JUCE-independent kit data model, kit file-name / kit.txt parsing and the lock-free kit handoff.
// See docs/KIT_FORMAT.md. The JUCE side (decode/resample) lives in src/plugin/KitLoader.*.
#include "core/DrumEvent.h"
#include "core/DrumMidiMap.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace robodrummer {

struct DrumSample {
    std::vector<float> left;
    std::vector<float> right; // empty = mono (duplicated to both sides on playback)
};

struct DrumKitLayer {
    float minVelocity{0.0f};
    float maxVelocity{1.0f};
    std::array<DrumSample, 4> variants{}; // round-robin
    std::size_t variantCount{0};
};

struct DrumKitInstrument {
    std::array<DrumKitLayer, 4> layers{}; // velocity layers, ascending
    std::size_t layerCount{0};            // 0 = instrument not in kit (starter sample is used)
    float tuneSemitones{0.0f};
    float gain{1.0f}; // linear
    float pan{0.0f};
    int chokeGroup{0};
};

// Immutable once published to the audio thread.
struct DrumKit {
    std::array<DrumKitInstrument, DrumInstrumentCount> instruments{};
    std::string name;
    [[nodiscard]] std::size_t instrumentCount() const noexcept {
        std::size_t n = 0;
        for (const auto& i : instruments) if (i.layerCount > 0) ++n;
        return n;
    }
};

// ---- file name convention: <instrument>[_v<1-4>][_rr<1-4>].<ext> ---------------------------------
struct KitFileName {
    DrumInstrument instrument{DrumInstrument::Kick};
    int velocity{0};   // 0 = no _v tag, else 1..4
    int roundRobin{0}; // 0 = no _rr tag, else 1..4
};

[[nodiscard]] inline bool isKitAudioExtension(std::string_view ext) noexcept {
    std::string e(ext);
    for (auto& c : e) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return e == "wav" || e == "aif" || e == "aiff";
}

// `fileName` includes the extension. Returns nullopt for unsupported extension or bad stem.
[[nodiscard]] inline std::optional<KitFileName> parseKitFileName(std::string_view fileName) {
    const auto dot = fileName.rfind('.');
    if (dot == std::string_view::npos || !isKitAudioExtension(fileName.substr(dot + 1))) return std::nullopt;
    std::string stem(fileName.substr(0, dot));
    for (auto& c : stem) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    KitFileName out;
    const auto tag = [&](const char* t, int& value) {
        const std::string key = std::string("_") + t;
        const auto p = stem.rfind(key);
        if (p == std::string::npos || p + key.size() + 1 != stem.size()) return true; // absent
        const char d = stem.back();
        if (d < '1' || d > '4') return false;
        value = d - '0';
        stem.erase(p);
        return true;
    };
    // Order: trailing _rrN first, then _vN.
    if (!tag("rr", out.roundRobin)) return std::nullopt;
    if (!tag("v", out.velocity)) return std::nullopt;
    const auto inst = drumInstrumentFromName(stem);
    if (!inst) return std::nullopt;
    out.instrument = *inst;
    return out;
}

// ---- kit.txt ------------------------------------------------------------------------------------
// Lines: "<instrument> tune=<semitones -24..24> gain=<dB -60..12> pan=<-1..1> choke=<0..8>"
// '#' starts a comment. Keys are optional. Errors are appended to `errors` (line skipped).
struct KitInstrumentConfig {
    std::optional<float> tune, gainDb, pan;
    std::optional<int> choke;
};
using KitConfig = std::array<KitInstrumentConfig, DrumInstrumentCount>;

inline void parseKitConfig(std::string_view text, KitConfig& cfg, std::vector<std::string>& errors) {
    std::size_t pos = 0;
    int lineNo = 0;
    const auto fail = [&](const std::string& m) { errors.push_back("kit.txt line " + std::to_string(lineNo) + ": " + m); };
    while (pos <= text.size()) {
        auto end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string line(text.substr(pos, end - pos));
        pos = end + 1;
        ++lineNo;
        if (const auto h = line.find('#'); h != std::string::npos) line.erase(h);
        std::vector<std::string> tok;
        std::string cur;
        for (const char c : line) {
            if (c == ' ' || c == '\t' || c == '\r') { if (!cur.empty()) { tok.push_back(cur); cur.clear(); } }
            else cur += c;
        }
        if (!cur.empty()) tok.push_back(cur);
        if (tok.empty()) continue;
        const auto inst = drumInstrumentFromName(tok[0]);
        if (!inst) { fail("unknown instrument '" + tok[0] + "'"); continue; }
        KitInstrumentConfig c;
        bool ok = true;
        for (std::size_t i = 1; i < tok.size() && ok; ++i) {
            const auto eq = tok[i].find('=');
            if (eq == std::string::npos) { fail("expected key=value, got '" + tok[i] + "'"); ok = false; break; }
            const std::string key = tok[i].substr(0, eq), val = tok[i].substr(eq + 1);
            char* endp = nullptr;
            const double v = std::strtod(val.c_str(), &endp);
            if (val.empty() || endp == nullptr || *endp != '\0' || !std::isfinite(v)) { fail("bad number '" + val + "'"); ok = false; break; }
            if (key == "tune") { if (v < -24 || v > 24) { fail("tune out of range -24..24"); ok = false; } else c.tune = static_cast<float>(v); }
            else if (key == "gain") { if (v < -60 || v > 12) { fail("gain out of range -60..12 dB"); ok = false; } else c.gainDb = static_cast<float>(v); }
            else if (key == "pan") { if (v < -1 || v > 1) { fail("pan out of range -1..1"); ok = false; } else c.pan = static_cast<float>(v); }
            else if (key == "choke") { if (v < 0 || v > 8 || v != std::floor(v)) { fail("choke must be integer 0..8"); ok = false; } else c.choke = static_cast<int>(v); }
            else { fail("unknown key '" + key + "'"); ok = false; }
        }
        if (ok) cfg[static_cast<std::size_t>(*inst)] = c;
    }
}

// ---- assembly -------------------------------------------------------------------------------------
// Collects decoded samples by (instrument, velocity tag, round-robin tag) and builds a DrumKit.
// Velocity tags present for an instrument are split into equal velocity ranges in ascending order;
// round-robin tags present are played in ascending order. An untagged file counts as the first
// velocity / round-robin slot; combining it with a tagged duplicate of that slot is an error.
class DrumKitBuilder {
public:
    // Returns false (sample not added) if the slot is already occupied.
    bool add(const KitFileName& n, DrumSample sample) {
        auto& slot = slots_[{static_cast<int>(n.instrument), n.velocity == 0 ? 1 : n.velocity, n.roundRobin == 0 ? 1 : n.roundRobin}];
        if (!slot.left.empty()) return false;
        slot = std::move(sample);
        return true;
    }

    [[nodiscard]] std::unique_ptr<DrumKit> build(const KitConfig& cfg, std::string name = {}) {
        auto kit = std::make_unique<DrumKit>();
        kit->name = std::move(name);
        for (std::size_t k = 0; k < DrumInstrumentCount; ++k) {
            auto& out = kit->instruments[k];
            std::vector<int> vels;
            for (const auto& [key, s] : slots_)
                if (std::get<0>(key) == static_cast<int>(k) && (vels.empty() || vels.back() != std::get<1>(key))) vels.push_back(std::get<1>(key));
            const auto n = std::min<std::size_t>(vels.size(), 4);
            for (std::size_t li = 0; li < n; ++li) {
                auto& layer = out.layers[li];
                layer.minVelocity = static_cast<float>(li) / static_cast<float>(n);
                layer.maxVelocity = static_cast<float>(li + 1) / static_cast<float>(n);
                for (const auto& [key, s] : slots_)
                    if (std::get<0>(key) == static_cast<int>(k) && std::get<1>(key) == vels[li] && layer.variantCount < 4)
                        layer.variants[layer.variantCount++] = s;
            }
            out.layerCount = n;
            const auto inst = static_cast<DrumInstrument>(k);
            out.chokeGroup = (inst == DrumInstrument::ClosedHat || inst == DrumInstrument::OpenHat) ? 1 : 0;
            const auto& c = cfg[k];
            if (c.tune) out.tuneSemitones = *c.tune;
            if (c.gainDb) out.gain = std::pow(10.0f, *c.gainDb / 20.0f);
            if (c.pan) out.pan = *c.pan;
            if (c.choke) out.chokeGroup = *c.choke;
        }
        return kit;
    }

    [[nodiscard]] bool empty() const noexcept { return slots_.empty(); }

private:
    std::map<std::tuple<int, int, int>, DrumSample> slots_;
};

// ---- lock-free kit handoff -------------------------------------------------------------------------
// Single producer (message/loader thread) -> single consumer (audio thread), single slot.
//
//  Message thread:  handoff.publish(std::move(kit));   // allocates/frees freely
//                   handoff.collectGarbage();          // call from a timer / after publish
//  Audio thread:    if (auto* k = handoff.acquire()) player.useKit(k);   // wait-free, no alloc/free
//
// Ownership: the handoff owns every kit. The audio thread only ever holds a raw pointer to the
// kit returned by acquire()/current(), valid until the NEXT acquire() that returns non-null.
// Superseded kits are parked in a retire slot and deleted by publish()/collectGarbage()/
// destructor on the message thread, never on the audio thread. A kit published but not yet
// acquired is deleted by a newer publish(). Publishing an empty DrumKit means "use starter kit".
// The audio thread must stop using the old kit pointer (DrumSamplePlayer::useKit does so,
// silencing voices that read from it) when acquire() returns a new one.
class KitHandoff {
public:
    KitHandoff() = default;
    KitHandoff(const KitHandoff&) = delete;
    KitHandoff& operator=(const KitHandoff&) = delete;
    ~KitHandoff() {
        delete pending_.exchange(nullptr);
        delete active_.exchange(nullptr);
        collectGarbage();
    }

    // Message thread only.
    void publish(std::unique_ptr<DrumKit> kit) {
        collectGarbage();
        if (!kit) kit = std::make_unique<DrumKit>();
        delete pending_.exchange(kit.release(), std::memory_order_acq_rel);
    }

    // Message thread only. Frees kits the audio thread has retired.
    void collectGarbage() noexcept {
        for (auto& r : retired_) delete r.exchange(nullptr, std::memory_order_acq_rel);
    }

    // Audio thread only. Returns the new kit if one was swapped in by this call, else nullptr.
    [[nodiscard]] DrumKit* acquire() noexcept {
        if (pending_.load(std::memory_order_acquire) == nullptr) return nullptr;
        // Make sure there is room to retire the current kit before taking the new one.
        DrumKit* old = active_.load(std::memory_order_relaxed);
        std::atomic<DrumKit*>* free = nullptr;
        if (old != nullptr) {
            for (auto& r : retired_) {
                if (r.load(std::memory_order_acquire) == nullptr) { free = &r; break; }
            }
            if (free == nullptr) return nullptr; // message thread has not collected yet; try later
        }
        DrumKit* fresh = pending_.exchange(nullptr, std::memory_order_acq_rel);
        if (fresh == nullptr) return nullptr;
        active_.store(fresh, std::memory_order_release);
        if (old != nullptr) free->store(old, std::memory_order_release);
        return fresh;
    }

    // Audio thread: the kit currently in use (may be nullptr before the first acquire).
    [[nodiscard]] DrumKit* current() const noexcept { return active_.load(std::memory_order_acquire); }

private:
    std::atomic<DrumKit*> pending_{nullptr};
    std::atomic<DrumKit*> active_{nullptr};
    std::array<std::atomic<DrumKit*>, 4> retired_{};
};

} // namespace robodrummer
