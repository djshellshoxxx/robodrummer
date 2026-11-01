#pragma once
// Offline fixtures only. Integer PCM RIFF/WAVE, no codec/framework dependency.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <vector>

namespace robodrummer::fixtures {

struct PcmWav {
    std::uint32_t sampleRate{0};
    std::uint16_t channels{0}, bitsPerSample{0};
    std::vector<float> mono;
};

inline PcmWav decodePcmWav(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t maxFileBytes = 64 * 1024 * 1024;
    constexpr std::size_t maxPcmBytes = 32 * 1024 * 1024;
    const auto require = [](bool valid, const char* message) {
        if (!valid) throw std::runtime_error(message);
    };
    require(bytes.size() >= 12 && bytes.size() <= maxFileBytes, "WAV file size outside fixture limits");
    const auto matches = [&](std::size_t at, const char* value) {
        return bytes[at] == value[0] && bytes[at + 1] == value[1] && bytes[at + 2] == value[2] && bytes[at + 3] == value[3];
    };
    const auto u16 = [&](std::size_t at) {
        return static_cast<std::uint16_t>(bytes[at] | (static_cast<unsigned>(bytes[at + 1]) << 8));
    };
    const auto u32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(u16(at)) | (static_cast<std::uint32_t>(u16(at + 2)) << 16);
    };
    require(matches(0, "RIFF") && matches(8, "WAVE"), "Expected little-endian RIFF/WAVE");
    require(static_cast<std::uint64_t>(u32(4)) + 8 == bytes.size(), "Truncated or inconsistent RIFF size");
    PcmWav result;
    std::size_t pcmOffset = 0, pcmBytes = 0;
    bool haveFormat = false, haveData = false;
    for (std::size_t offset = 12; offset < bytes.size();) {
        require(bytes.size() - offset >= 8, "Incomplete WAV chunk header");
        const auto length = static_cast<std::size_t>(u32(offset + 4));
        const auto payload = offset + 8;
        require(length <= bytes.size() - payload, "WAV chunk extends beyond RIFF");
        if (matches(offset, "fmt ")) {
            require(!haveFormat && length >= 16, "Missing or duplicate WAV format");
            require(u16(payload) == 1, "Only integer PCM WAV is supported");
            result.channels = u16(payload + 2);
            result.sampleRate = u32(payload + 4);
            result.bitsPerSample = u16(payload + 14);
            require(result.channels == 1 || result.channels == 2, "Expected mono/stereo WAV");
            require(result.sampleRate >= 8000 && result.sampleRate <= 192000, "Unsupported WAV sample rate");
            require(result.bitsPerSample == 8 || result.bitsPerSample == 16 || result.bitsPerSample == 24 || result.bitsPerSample == 32,
                    "Unsupported PCM bit depth");
            const auto alignment = result.channels * (result.bitsPerSample / 8);
            require(u16(payload + 12) == alignment && u32(payload + 8) == result.sampleRate * alignment, "Inconsistent WAV alignment/byte rate");
            haveFormat = true;
        } else if (matches(offset, "data")) {
            require(!haveData && length > 0 && length <= maxPcmBytes, "Missing, duplicate or oversized PCM data");
            pcmOffset = payload; pcmBytes = length; haveData = true;
        }
        const auto padded = length + (length & 1u);
        require(padded <= bytes.size() - payload, "Missing odd-chunk padding");
        offset = payload + padded;
    }
    require(haveFormat && haveData, "WAV requires format and data chunks");
    const auto sampleBytes = static_cast<std::size_t>(result.bitsPerSample / 8);
    const auto frameBytes = sampleBytes * result.channels;
    require(pcmBytes % frameBytes == 0, "Incomplete PCM frame");
    result.mono.resize(pcmBytes / frameBytes);
    for (std::size_t frame = 0; frame < result.mono.size(); ++frame) {
        double sum = 0;
        for (std::size_t channel = 0; channel < result.channels; ++channel) {
            const auto at = pcmOffset + frame * frameBytes + channel * sampleBytes;
            std::uint32_t raw = 0;
            for (std::size_t i = 0; i < sampleBytes; ++i) raw |= static_cast<std::uint32_t>(bytes[at + i]) << (8 * i);
            if (result.bitsPerSample == 8) sum += (static_cast<int>(raw) - 128) / 128.0;
            else {
                const auto sign = std::uint64_t{1} << (result.bitsPerSample - 1);
                const auto value = static_cast<std::int64_t>(raw) - ((raw & sign) ? static_cast<std::int64_t>(sign * 2) : 0);
                sum += static_cast<double>(value) / static_cast<double>(sign);
            }
        }
        result.mono[frame] = static_cast<float>(sum / result.channels);
    }
    return result;
}

inline PcmWav readPcmWav(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open fixture WAV");
    const auto size = input.tellg();
    if (size < 12 || size > 64 * 1024 * 1024) throw std::runtime_error("WAV file size outside fixture limits");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read fixture WAV");
    return decodePcmWav(bytes);
}

} // namespace robodrummer::fixtures
