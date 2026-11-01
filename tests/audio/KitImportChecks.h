#pragma once
// Direct JUCE import regressions, shared by the native processor harness and a
// focused driver. No device/display needed; only uniquely owned temp fixtures.
#include "audio/KitBuilder.h"
#include <array>
#include <limits>
#include <vector>

namespace robodrummer::test {

template <typename Check>
void runKitImportChecks(Check&& check) {
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("robodrummer-import-checks", "", false);
    struct Cleanup { juce::File directory; ~Cleanup() { directory.deleteRecursively(); } } cleanup{directory};
    check(directory.createDirectory().wasOk(), "create isolated kit import fixtures");
    const auto folder = [&](const char* name) {
        auto result = directory.getChildFile(name);
        check(result.createDirectory().wasOk(), "create kit import case directory");
        return result;
    };
    const auto writeFloatWav = [&](const juce::File& file, const std::vector<float>& values, unsigned rate = 48000) {
        juce::FileOutputStream stream(file);
        check(stream.openedOk(), "open float WAV fixture");
        check(stream.setPosition(0) && stream.truncate().wasOk(), "replace float WAV fixture contents");
        const int bytes = static_cast<int>(values.size() * sizeof(float));
        stream.write("RIFF", 4); stream.writeInt(36 + bytes); stream.write("WAVEfmt ", 8);
        stream.writeInt(16); stream.writeShort(3); stream.writeShort(1); // IEEE float, mono
        stream.writeInt(static_cast<int>(rate)); stream.writeInt(static_cast<int>(rate * 4));
        stream.writeShort(4); stream.writeShort(32); stream.write("data", 4); stream.writeInt(bytes);
        for (const auto value : values) stream.writeFloat(value);
        stream.flush();
        check(stream.getStatus().wasOk(), "write actual IEEE float WAV fixture");
    };
    const auto kickFirstSample = [&](DrumSamplePlayer& player) {
        player.clear(); player.setInstrumentBus(DrumInstrument::Kick, 0);
        player.trigger({DrumInstrument::Kick, 0, 1.0f, 0});
        float output[1]{}; float* bus[]{output};
        player.renderBuses(bus, nullptr, 1, 1);
        return output[0];
    };

    // Nonfinite values are validated before sample-rate conversion. Exercise
    // main, aux, and the stateful reverb feed rather than only a dry output.
    for (const unsigned sourceRate : {44100u, 48000u}) {
        auto malformed = folder(sourceRate == 44100 ? "float-resampled" : "float-native");
        writeFloatWav(malformed.getChildFile("kick.wav"), {
            std::numeric_limits<float>::quiet_NaN(), INFINITY, -INFINITY,
            std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
            0.25f, 1.25f, 0.5f
        }, sourceRate);
        juce::String report;
        auto player = KitBuilder::loadUserFolder(malformed, 48000.0, report);
        check(report.contains("1 samples on 1 instruments"), "load actual malformed float WAV");
        juce::Reverb reverb; reverb.setSampleRate(48000.0);
        for (const int route : {0, 1}) {
            player->clear(); player->setInstrumentBus(DrumInstrument::Kick, route);
            player->trigger({DrumInstrument::Kick, 0, 1.0f, 0});
            std::array<float, 128> main{}, aux{}, roomL{}, roomR{};
            float* buses[]{main.data(), aux.data()};
            player->renderBuses(buses, nullptr, 2, 128, roomL.data(), roomR.data());
            bool finite = true;
            for (std::size_t n = 0; n < main.size(); ++n)
                finite &= std::isfinite(main[n]) && std::isfinite(aux[n])
                    && std::isfinite(roomL[n]) && std::isfinite(roomR[n])
                    && std::abs(main[n]) <= 16.0f && std::abs(aux[n]) <= 16.0f;
            check(finite, "imported float WAV remains finite on main, aux and room send");
            reverb.processStereo(roomL.data(), roomR.data(), 128);
            for (std::size_t n = 0; n < roomL.size(); ++n)
                finite &= std::isfinite(roomL[n]) && std::isfinite(roomR[n]);
            check(finite, "imported float WAV cannot poison stateful reverb");
        }
        writeFloatWav(malformed.getChildFile("kick.wav"), std::vector<float>(128, 0.75f));
        player = KitBuilder::loadUserFolder(malformed, 48000.0, report);
        check(kickFirstSample(*player) == 0.75f, "valid WAV import recovers at the original level");
        std::array<float, 128> goodL{}, goodR{}; goodL.fill(0.75f); goodR.fill(0.75f);
        reverb.processStereo(goodL.data(), goodR.data(), 128);
        check(std::all_of(goodL.begin(), goodL.end(), [](float v) { return std::isfinite(v); }),
              "stateful room recovers when valid audio resumes");
    }

    auto duplicates = folder("duplicate-slots");
    writeFloatWav(duplicates.getChildFile("kick_v1_rr1.wav"), std::vector<float>(128, 0.75f));
    writeFloatWav(duplicates.getChildFile("kick-v1-rr1.wav"), std::vector<float>(128, 0.25f));
    // A large-looking folder contains tiny files; only one slot may be decoded.
    for (int n = 0; n < 200; ++n) {
        juce::String name("kick_v1_");
        for (int bit = 0; bit < 8; ++bit) name += (n & (1 << bit)) != 0 ? "." : "_";
        writeFloatWav(duplicates.getChildFile(name + "rr1.wav"), {0.9f});
    }
    writeFloatWav(duplicates.getChildFile("kick_v5.wav"), {0.9f});
    writeFloatWav(duplicates.getChildFile("snare_rr9.wav"), {0.9f});
    juce::String report;
    auto player = KitBuilder::loadUserFolder(duplicates, 48000.0, report);
    check(report.contains("1 samples on 1 instruments"), "duplicate and unsupported slots never consume decoded storage");
    check(report.contains("201 duplicate slot") && report.contains("2 unsupported"), "report duplicate and unsupported slot counts");
    check(kickFirstSample(*player) == 0.25f, "duplicate slot selection is lexical and deterministic");
    auto repeated = KitBuilder::loadUserFolder(duplicates, 48000.0, report);
    check(kickFirstSample(*repeated) == 0.25f, "repeat import selects the same supported file");

    auto constrained = folder("budget");
    writeFloatWav(constrained.getChildFile("kick.wav"), std::vector<float>(256, 0.25f));
    writeFloatWav(constrained.getChildFile("snare.wav"), std::vector<float>(256, 0.75f));
    auto fallback = KitBuilder::buildBuiltIn(0, 48000.0);
    const auto baselineBytes = fallback->sampleStorageBytes();
    player = KitBuilder::loadUserFolder(constrained, 48000.0, report, baselineBytes + 2048);
    check(report.contains("1 samples on 1 instruments") && report.contains("1 file(s) exceed decoded-memory budget"),
          "aggregate peak budget rejects a later slot before allocating its buffers");
    check(player->sampleStorageBytes() <= baselineBytes + 2048, "retained sample bytes stay within the requested budget");
    check(kickFirstSample(*player) == 0.25f, "budget retains the deterministic earlier instrument");
    player->clear(); player->setInstrumentBus(DrumInstrument::Snare, 0);
    player->trigger({DrumInstrument::Snare, 0, 1.0f, 0});
    float snare[64]{}; float* bus[]{snare}; player->renderBuses(bus, nullptr, 1, 64);
    check(std::any_of(std::begin(snare), std::end(snare), [](float v) { return std::abs(v) > 0.01f; }),
          "over-budget and missing instruments retain playable built-in fallbacks");

    // JUCE supports PCM AIFF, so exercise the actual decoder as well as WAV.
    auto aiffFolder = folder("aiff");
    juce::AiffAudioFormat aiff;
    auto stream = std::make_unique<juce::FileOutputStream>(aiffFolder.getChildFile("kick.aiff"));
    std::unique_ptr<juce::AudioFormatWriter> writer(aiff.createWriterFor(stream.get(), 48000.0, 1, 16, {}, 0));
    check(writer != nullptr, "create supported AIFF fixture writer");
    if (writer != nullptr) {
        stream.release(); juce::AudioBuffer<float> audio(1, 64);
        for (int n = 0; n < 64; ++n) audio.setSample(0, n, 0.25f);
        check(writer->writeFromAudioSampleBuffer(audio, 0, 64), "write real PCM AIFF fixture");
        writer.reset();
        player = KitBuilder::loadUserFolder(aiffFolder, 48000.0, report);
        check(report.contains("1 samples on 1 instruments"), "load supported actual AIFF sample");
        check(std::abs(kickFirstSample(*player) - 0.25f) < 0.0001f, "AIFF ordinary levels retain PCM precision");
    }
    player = KitBuilder::loadUserFolder(directory.getChildFile("absent"), 48000.0, report);
    check(report.contains("Kit folder not found"), "missing directory reports built-in fallback");
    auto broken = folder("broken");
    check(broken.getChildFile("kick.wav").replaceWithText("not a WAV"), "create malformed sample fixture");
    player = KitBuilder::loadUserFolder(broken, 48000.0, report);
    check(report.contains("1 invalid/unreadable") && report.contains("built-in Studio"),
          "unreadable selected files report their rejection and preserve fallback");
}

} // namespace robodrummer::test
