// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#include "audio/KitBuilder.h"
#include "core/KitFileName.h"
#include "core/Mixer.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

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

void configure(DrumSamplePlayer& player) {
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

struct LoadedFile {
    int layer{0};
    int roundRobin{0};
    juce::String name;
    Sample sample;
};
} // namespace

std::unique_ptr<DrumSamplePlayer> KitBuilder::buildBuiltIn(int kitIndex, double sampleRate) {
    const auto& voicing = kVoicings[static_cast<std::size_t>(std::clamp(kitIndex, 0, kBuiltInKitCount - 1))];
    auto player = std::make_unique<DrumSamplePlayer>();
    for (int i = 0; i <= static_cast<int>(DrumInstrument::Clap); ++i)
        installSynth(*player, static_cast<DrumInstrument>(i), sampleRate, voicing);
    configure(*player);
    return player;
}

std::unique_ptr<DrumSamplePlayer> KitBuilder::loadUserFolder(const juce::File& folder, double sampleRate, juce::String& report) {
    auto player = std::make_unique<DrumSamplePlayer>();
    if (!folder.isDirectory()) {
        report = "Kit folder not found - using the built-in Studio kit.";
        return buildBuiltIn(0, sampleRate);
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::map<int, std::vector<LoadedFile>> byInstrument;
    int fileCount = 0;
    int skipped = 0;
    auto files = folder.findChildFiles(juce::File::findFiles, false);
    files.sort();
    for (const auto& file : files) {
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext != ".wav" && ext != ".aif" && ext != ".aiff" && ext != ".flac") continue;
        const auto info = parseKitFileName(file.getFileNameWithoutExtension().toStdString());
        if (!info) { ++skipped; continue; }
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr || reader->lengthInSamples <= 0) { ++skipped; continue; }

        const auto length = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, static_cast<juce::int64>(reader->sampleRate * 20.0)));
        juce::AudioBuffer<float> buffer(static_cast<int>(std::min<unsigned>(reader->numChannels, 2)), length);
        reader->read(&buffer, 0, length, 0, true, buffer.getNumChannels() > 1);

        LoadedFile loaded;
        loaded.layer = info->layer;
        loaded.roundRobin = info->roundRobin;
        loaded.name = file.getFileName();
        loaded.sample.left = resample(buffer.getReadPointer(0), length, reader->sampleRate, sampleRate);
        if (buffer.getNumChannels() > 1)
            loaded.sample.right = resample(buffer.getReadPointer(1), length, reader->sampleRate, sampleRate);
        byInstrument[static_cast<int>(info->instrument)].push_back(std::move(loaded));
        ++fileCount;
    }

    int loadedInstruments = 0;
    juce::StringArray missing;
    for (int i = 0; i <= static_cast<int>(DrumInstrument::Clap); ++i) {
        const auto instrument = static_cast<DrumInstrument>(i);
        auto found = byInstrument.find(i);
        if (found == byInstrument.end()) {
            installSynth(*player, instrument, sampleRate, kVoicings[0]);
            // Only the core pieces are worth listing as missing.
            if (i <= static_cast<int>(DrumInstrument::FloorTom)) {
                static const char* names[] = { "kick", "snare", "closed hat", "open hat", "ride", "crash", "high tom", "mid tom", "floor tom" };
                missing.add(names[i]);
            }
            continue;
        }
        auto& entries = found->second;
        std::stable_sort(entries.begin(), entries.end(), [](const LoadedFile& a, const LoadedFile& b) {
            return std::tie(a.layer, a.roundRobin, a.name) < std::tie(b.layer, b.roundRobin, b.name);
        });
        std::vector<int> layers;
        for (const auto& e : entries)
            if (std::find(layers.begin(), layers.end(), e.layer) == layers.end()) layers.push_back(e.layer);
        const auto layerCount = std::min<std::size_t>(layers.size(), DrumSamplePlayer::MaxVelocityLayers);
        for (std::size_t li = 0; li < layerCount; ++li) {
            const float lo = static_cast<float>(li) / static_cast<float>(layerCount);
            const float hi = static_cast<float>(li + 1) / static_cast<float>(layerCount);
            bool first = true;
            for (auto& e : entries) {
                if (e.layer != layers[li]) continue;
                if (first) { player->setVelocityLayer(instrument, li, lo, hi, std::move(e.sample)); first = false; }
                else (void) player->addRoundRobinSample(instrument, li, std::move(e.sample));
            }
        }
        ++loadedInstruments;
    }
    configure(*player);

    report = juce::String(fileCount) + " samples on " + juce::String(loadedInstruments) + " instruments loaded from " + folder.getFileName();
    if (skipped > 0) report << "; " << skipped << " unrecognised file(s) ignored";
    if (!missing.isEmpty()) report << ". Built-in sounds fill in: " << missing.joinIntoString(", ");
    if (fileCount == 0) report = "No recognisable WAV/AIFF drum samples found - using the built-in Studio kit. Name files like kick.wav, snare_v1.wav, closedhat_rr2.wav.";
    return player;
}

} // namespace robodrummer
