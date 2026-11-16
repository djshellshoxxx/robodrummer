// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#include "audio/KitBuilder.h"
#include "core/KitFileName.h"
#include "core/Mixer.h"
#include <algorithm>
#include <cmath>

namespace robodrummer {
namespace {
using Sample = DrumSamplePlayer::Sample;

struct Voicing {
    double pitch{1.0};     // tuning multiplier for tonal drums
    double decay{1.0};     // length multiplier
    double bright{0.86};   // hi-hat/cymbal high-pass coefficient
    bool lofi{false};
};

constexpr std::array<Voicing, kBuiltInKitCount> kVoicings {{
    { 1.00, 1.00, 0.86, false },
    { 1.12, 0.62, 0.90, false },
    { 0.88, 1.55, 0.82, false },
    { 1.00, 0.90, 0.80, true }
}};

float noise(std::uint32_t& rng) noexcept {
    rng = rng * 1664525u + 1013904223u;
    return static_cast<float>((rng >> 8) & 0x00FFFFFFu) / 8388608.0f - 1.0f;
}

Sample makeKick(double sr, const Voicing& v) {
    Sample s;
    const int n = static_cast<int>(sr * 0.35 * v.decay);
    s.left.resize(static_cast<std::size_t>(n));
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = i / sr;
        const double env = std::exp(-t * 13.0 / v.decay);
        const double freq = (95.0 - 55.0 * std::min(1.0, t / 0.12)) * v.pitch;
        phase += juce::MathConstants<double>::twoPi * freq / sr;
        s.left[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(phase) * env * 0.9);
    }
    return s;
}

Sample makeSnare(double sr, const Voicing& v) {
    Sample s;
    const int n = static_cast<int>(sr * 0.22 * v.decay);
    s.left.resize(static_cast<std::size_t>(n));
    std::uint32_t rng = 0x51A9E123u;
    for (int i = 0; i < n; ++i) {
        const double t = i / sr;
        const float tone = std::sin(static_cast<float>(juce::MathConstants<double>::twoPi * 185.0 * v.pitch * t));
        s.left[static_cast<std::size_t>(i)] = (noise(rng) * 0.75f + tone * 0.25f) * static_cast<float>(std::exp(-t * 22.0 / v.decay)) * 0.55f;
    }
    return s;
}

Sample makeTom(double sr, double frequency, const Voicing& v) {
    Sample s;
    const int n = static_cast<int>(sr * 0.30 * v.decay);
    s.left.resize(static_cast<std::size_t>(n));
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = i / sr;
        const double f = frequency * v.pitch * (1.0 - 0.16 * std::min(1.0, t / 0.09));
        phase += juce::MathConstants<double>::twoPi * f / sr;
        s.left[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(phase) * std::exp(-t * 10.0 / v.decay) * 0.50);
    }
    return s;
}

Sample makeNoiseHit(double sr, double seconds, double decayRate, float level, double coefficient, std::uint32_t seed) {
    Sample s;
    const int n = static_cast<int>(sr * seconds);
    s.left.resize(static_cast<std::size_t>(n));
    std::uint32_t rng = seed;
    float prev = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float x = noise(rng);
        const float hp = x - prev * static_cast<float>(coefficient);
        prev = x;
        s.left[static_cast<std::size_t>(i)] = hp * static_cast<float>(std::exp(-(i / sr) * decayRate)) * level;
    }
    return s;
}

Sample makeClick(double sr, double frequency, double seconds, float level) {
    Sample s;
    const int n = static_cast<int>(sr * seconds);
    s.left.resize(static_cast<std::size_t>(n));
    std::uint32_t rng = 0xBEEF1234u;
    for (int i = 0; i < n; ++i) {
        const double t = i / sr;
        const float body = std::sin(static_cast<float>(juce::MathConstants<double>::twoPi * frequency * t));
        s.left[static_cast<std::size_t>(i)] = (body * 0.7f + noise(rng) * 0.3f) * static_cast<float>(std::exp(-t * 70.0)) * level;
    }
    return s;
}

void applyLoFi(Sample& s) {
    float held = 0.0f;
    for (std::size_t i = 0; i < s.left.size(); ++i) {
        if (i % 2 == 0) held = std::round(s.left[i] * 64.0f) / 64.0f;
        s.left[i] = held;
    }
}

Sample synthSample(DrumInstrument instrument, double sr, const Voicing& v) {
    Sample s;
    switch (instrument) {
        case DrumInstrument::Kick: s = makeKick(sr, v); break;
        case DrumInstrument::Snare: s = makeSnare(sr, v); break;
        case DrumInstrument::ClosedHat: s = makeNoiseHit(sr, 0.08 * v.decay, 55.0 / v.decay, 0.24f, v.bright, 0xC105ED42u); break;
        case DrumInstrument::OpenHat: s = makeNoiseHit(sr, 0.24 * v.decay, 14.0 / v.decay, 0.24f, v.bright, 0xC105ED43u); break;
        case DrumInstrument::Ride: s = makeNoiseHit(sr, 0.35 * v.decay, 14.0 / v.decay, 0.20f, v.bright, 0x71DE0001u); break;
        case DrumInstrument::Crash: s = makeNoiseHit(sr, 1.2 * v.decay, 3.8 / v.decay, 0.17f, 0.93, 0xCA45A112u); break;
        case DrumInstrument::China: s = makeNoiseHit(sr, 0.9 * v.decay, 5.0 / v.decay, 0.17f, 0.88, 0xC41A0001u); break;
        case DrumInstrument::Splash: s = makeNoiseHit(sr, 0.45 * v.decay, 9.0 / v.decay, 0.16f, 0.92, 0x5B1A5401u); break;
        case DrumInstrument::HighTom: s = makeTom(sr, 190.0, v); break;
        case DrumInstrument::MidTom: s = makeTom(sr, 145.0, v); break;
        case DrumInstrument::FloorTom: s = makeTom(sr, 105.0, v); break;
        case DrumInstrument::Rimshot: s = makeClick(sr, 1700.0 * v.pitch, 0.06, 0.55f); break;
        case DrumInstrument::Sidestick: s = makeClick(sr, 1200.0 * v.pitch, 0.05, 0.45f); break;
        case DrumInstrument::Cowbell: s = makeClick(sr, 800.0 * v.pitch, 0.18, 0.40f); break;
        case DrumInstrument::Tambourine: s = makeNoiseHit(sr, 0.12 * v.decay, 30.0 / v.decay, 0.22f, 0.8, 0x7A3B0001u); break;
        case DrumInstrument::Clap: s = makeNoiseHit(sr, 0.14 * v.decay, 28.0 / v.decay, 0.30f, 0.55, 0xC1A90001u); break;
    }
    if (v.lofi) applyLoFi(s);
    return s;
}

void configure(DrumSamplePlayer& player, double sampleRate) {
    player.setSampleRate(sampleRate);
    // Open and closed hats share a choke group; each mixer channel feeds its aux output bus.
    player.setChokeGroup(DrumInstrument::ClosedHat, 1);
    player.setChokeGroup(DrumInstrument::OpenHat, 1);
    for (int i = 0; i <= static_cast<int>(DrumInstrument::Clap); ++i) {
        const auto instrument = static_cast<DrumInstrument>(i);
        player.setInstrumentBus(instrument, auxBusFor(mixerChannelFor(instrument)));
    }
}

void installSynth(DrumSamplePlayer& player, DrumInstrument instrument, double sr, const Voicing& v) {
    player.setSample(instrument, synthSample(instrument, sr, v));
}

// Linear-interpolating sample-rate conversion; used once at load time, never on the audio thread.
std::vector<float> resample(const float* data, int numSamples, double sourceRate, double targetRate) {
    if (numSamples <= 0) return {};
    if (std::abs(sourceRate - targetRate) < 0.5)
        return std::vector<float>(data, data + numSamples);
    const double ratio = sourceRate / targetRate;
    const int outCount = std::max(1, static_cast<int>(std::floor(static_cast<double>(numSamples) / ratio)));
    std::vector<float> out(static_cast<std::size_t>(outCount));
    for (int i = 0; i < outCount; ++i) {
        const double pos = i * ratio;
        const int i0 = std::min(numSamples - 1, static_cast<int>(pos));
        const int i1 = std::min(numSamples - 1, i0 + 1);
        const float frac = static_cast<float>(pos - i0);
        out[static_cast<std::size_t>(i)] = data[i0] + (data[i1] - data[i0]) * frac;
    }
    return out;
}

} // namespace

std::unique_ptr<DrumSamplePlayer> KitBuilder::buildBuiltIn(int kitIndex, double sampleRate) {
    const auto& voicing = kVoicings[static_cast<std::size_t>(std::clamp(kitIndex, 0, kBuiltInKitCount - 1))];
    auto player = std::make_unique<DrumSamplePlayer>();
    for (int i = 0; i <= static_cast<int>(DrumInstrument::Clap); ++i)
        installSynth(*player, static_cast<DrumInstrument>(i), sampleRate, voicing);
    configure(*player, sampleRate);
    return player;
}

std::unique_ptr<DrumSamplePlayer> KitBuilder::loadUserFolder(const juce::File& folder, double sampleRate, juce::String& report) {
    return loadUserFolder(folder, sampleRate, report, kMaxKitDecodedBytes);
}

std::unique_ptr<DrumSamplePlayer> KitBuilder::loadUserFolder(const juce::File& folder, double sampleRate,
                                                         juce::String& report, std::size_t maxDecodedBytes) {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 768000.0) sampleRate = 44100.0;
    auto player = buildBuiltIn(0, sampleRate);
    if (!folder.isDirectory()) {
        report = "Kit folder not found - using the built-in Studio kit.";
        return player;
    }

    // Keep only the lexical-first file for each supported instrument/layer/RR
    // slot. Enumeration order cannot change the selected kit, and duplicates
    // never allocate decoded buffers or crowd out another instrument's budget.
    std::array<juce::File, kKitSlotCount> selected;
    std::size_t duplicates = 0, unsupported = 0, invalid = 0, overBudget = 0;
    for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFiles)) {
        const auto file = entry.getFile();
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext != ".wav" && ext != ".aif" && ext != ".aiff" && ext != ".flac") continue;
        const auto info = parseKitFileName(file.getFileNameWithoutExtension().toStdString());
        const auto slot = info ? kitSlotIndex(*info) : std::optional<std::size_t>{};
        if (!slot) { ++unsupported; continue; }
        auto& chosen = selected[*slot];
        if (chosen != juce::File{}) ++duplicates;
        if (chosen == juce::File{} || file.getFileName().compare(chosen.getFileName()) < 0) chosen = file;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::array<Sample, kKitSlotCount> samples;
    std::array<bool, kKitSlotCount> loaded{};
    KitImportBudget budget(std::min(maxDecodedBytes, kMaxKitDecodedBytes), player->sampleStorageBytes());
    int fileCount = 0;
    for (std::size_t slot = 0; slot < selected.size(); ++slot) {
        if (selected[slot] == juce::File{}) continue;
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(selected[slot]));
        if (reader == nullptr) { ++invalid; continue; }
        const unsigned channels = std::min(reader->numChannels, 2u);
        const auto plan = planKitRead(reader->lengthInSamples, reader->sampleRate, sampleRate, channels);
        if (!plan) { ++invalid; continue; }
        if (!budget.fits(plan->sourceBytes, plan->targetBytes)) { ++overBudget; continue; }
        juce::AudioBuffer<float> buffer(static_cast<int>(channels), plan->sourceFrames);
        if (!reader->read(&buffer, 0, plan->sourceFrames, 0, true, channels > 1)) { ++invalid; continue; }
        // Validate before interpolation and before any stateful room processing.
        for (unsigned channel = 0; channel < channels; ++channel)
            for (int frame = 0; frame < plan->sourceFrames; ++frame)
                buffer.setSample(static_cast<int>(channel), frame,
                    DrumSamplePlayer::sanitiseSampleValue(buffer.getSample(static_cast<int>(channel), frame)));
        auto& sample = samples[slot];
        sample.left = resample(buffer.getReadPointer(0), plan->sourceFrames, reader->sampleRate, sampleRate);
        if (channels > 1)
            sample.right = resample(buffer.getReadPointer(1), plan->sourceFrames, reader->sampleRate, sampleRate);
        budget.retain(plan->targetBytes);
        loaded[slot] = true;
        ++fileCount;
    }

    int loadedInstruments = 0;
    juce::StringArray missing;
    constexpr auto slotsPerInstrument = DrumSamplePlayer::MaxVelocityLayers * DrumSamplePlayer::MaxRoundRobinVariants;
    for (std::size_t instrument = 0; instrument < DrumSamplePlayer::InstrumentCount; ++instrument) {
        std::array<bool, DrumSamplePlayer::MaxVelocityLayers> loadedLayers{};
        std::size_t layerCount = 0;
        for (std::size_t layer = 0; layer < loadedLayers.size(); ++layer) {
            for (std::size_t rr = 0; rr < DrumSamplePlayer::MaxRoundRobinVariants; ++rr)
                loadedLayers[layer] = loadedLayers[layer] || loaded[instrument * slotsPerInstrument
                    + layer * DrumSamplePlayer::MaxRoundRobinVariants + rr];
            if (loadedLayers[layer]) ++layerCount;
        }
        if (layerCount == 0) {
            if (instrument <= static_cast<std::size_t>(DrumInstrument::FloorTom)) {
                static const char* names[] = { "kick", "snare", "closed hat", "open hat", "ride", "crash", "high tom", "mid tom", "floor tom" };
                missing.add(names[instrument]);
            }
            continue;
        }
        const auto drum = static_cast<DrumInstrument>(instrument);
        std::size_t outputLayer = 0;
        for (std::size_t layer = 0; layer < loadedLayers.size(); ++layer) {
            if (!loadedLayers[layer]) continue;
            const float lo = static_cast<float>(outputLayer) / static_cast<float>(layerCount);
            const float hi = static_cast<float>(outputLayer + 1) / static_cast<float>(layerCount);
            bool first = true;
            for (std::size_t rr = 0; rr < DrumSamplePlayer::MaxRoundRobinVariants; ++rr) {
                const auto slot = instrument * slotsPerInstrument + layer * DrumSamplePlayer::MaxRoundRobinVariants + rr;
                if (!loaded[slot]) continue;
                if (first) { player->setVelocityLayer(drum, outputLayer, lo, hi, std::move(samples[slot])); first = false; }
                else (void) player->addRoundRobinSample(drum, outputLayer, std::move(samples[slot]));
            }
            ++outputLayer;
        }
        ++loadedInstruments;
    }

    report = juce::String(fileCount) + " samples on " + juce::String(loadedInstruments) + " instruments loaded from " + folder.getFileName();
    if (fileCount == 0) report = "No recognisable WAV/AIFF drum samples loaded - using the built-in Studio kit.";
    if (duplicates > 0) report << "; " << juce::String(static_cast<juce::int64>(duplicates)) << " duplicate slot file(s) skipped";
    if (unsupported > 0) report << "; " << juce::String(static_cast<juce::int64>(unsupported)) << " unsupported name/slot file(s) skipped";
    if (invalid > 0) report << "; " << juce::String(static_cast<juce::int64>(invalid)) << " invalid/unreadable file(s) skipped";
    if (overBudget > 0) report << "; " << juce::String(static_cast<juce::int64>(overBudget)) << " file(s) exceed decoded-memory budget";
    if (!missing.isEmpty()) report << ". Built-in sounds fill in: " << missing.joinIntoString(", ");
    return player;
}

} // namespace robodrummer
