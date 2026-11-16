#include "support/WavReader.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
void text(Bytes& out, const char* value) { out.insert(out.end(), value, value + 4); }
void u16(Bytes& out, std::uint16_t n) { out.push_back(static_cast<std::uint8_t>(n & 255)); out.push_back(static_cast<std::uint8_t>(n >> 8)); }
void u32(Bytes& out, std::uint32_t n) { u16(out, static_cast<std::uint16_t>(n & 65535)); u16(out, static_cast<std::uint16_t>(n >> 16)); }
void patch32(Bytes& out, std::size_t offset, std::uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) out[offset + i] = static_cast<std::uint8_t>(n >> (8 * i));
}
Bytes wav(int bits, const Bytes& pcm, int channels = 1) {
    Bytes out;
    text(out, "RIFF"); u32(out, 0); text(out, "WAVE");
    text(out, "JUNK"); u32(out, 1); out.push_back(7); out.push_back(0);
    text(out, "fmt "); u32(out, 16); u16(out, 1); u16(out, static_cast<std::uint16_t>(channels));
    u32(out, 48000); u32(out, static_cast<std::uint32_t>(48000 * channels * bits / 8));
    u16(out, static_cast<std::uint16_t>(channels * bits / 8)); u16(out, static_cast<std::uint16_t>(bits));
    text(out, "data"); u32(out, static_cast<std::uint32_t>(pcm.size())); out.insert(out.end(), pcm.begin(), pcm.end());
    if (pcm.size() % 2) out.push_back(0);
    patch32(out, 4, static_cast<std::uint32_t>(out.size() - 8));
    return out;
}
void rejected(const Bytes& data) {
    bool failed = false;
    try { (void)robodrummer::fixtures::decodePcmWav(data); }
    catch (const std::runtime_error&) { failed = true; }
    assert(failed);
}
}

int main() {
    using robodrummer::fixtures::decodePcmWav;
    auto audio = decodePcmWav(wav(16, {0, 128, 0, 0, 255, 127}));
    assert(audio.sampleRate == 48000 && audio.channels == 1 && audio.bitsPerSample == 16);
    assert(audio.mono.size() == 3 && audio.mono[0] == -1.0f && audio.mono[1] == 0.0f);
    assert(std::abs(audio.mono[2] - 32767.0f / 32768.0f) < 1e-7f);
    audio = decodePcmWav(wav(24, {0, 0, 128, 255, 255, 127}));
    assert(audio.mono.size() == 2 && audio.mono[0] == -1.0f && audio.mono[1] > 0.99999f);
    audio = decodePcmWav(wav(32, {0, 0, 0, 128, 255, 255, 255, 127}));
    assert(audio.mono.size() == 2 && audio.mono[0] == -1.0f && audio.mono[1] == 1.0f);
    audio = decodePcmWav(wav(8, {0, 128, 255}));
    assert(audio.mono[0] == -1.0f && audio.mono[1] == 0.0f && audio.mono[2] == 127.0f / 128.0f);
    audio = decodePcmWav(wav(16, {0, 128, 0, 0, 0, 0, 255, 127}, 2));
    assert(audio.mono.size() == 2 && audio.mono[0] == -0.5f && audio.mono[1] > 0.49f);

    rejected({});
    rejected(wav(16, {}));
    auto invalid = wav(16, {0, 0}); invalid[0] = 'X'; rejected(invalid);
    invalid = wav(16, {0, 0}); invalid.pop_back(); rejected(invalid);
    invalid = wav(16, {0, 0}); patch32(invalid, 4, 0xffffffffu); rejected(invalid);
    invalid = wav(16, {0, 0}); invalid[30] = 3; rejected(invalid); // float format, not integer PCM
    invalid = wav(16, {0, 0}); invalid[32] = 0; rejected(invalid); // zero channels
    invalid = wav(16, {0, 0}); patch32(invalid, 34, 0); rejected(invalid); // zero sample rate
    invalid = wav(16, {0, 0}); patch32(invalid, 38, 1); rejected(invalid); // wrong byte rate
    rejected(wav(16, {0})); // incomplete sample
    invalid = wav(16, {0, 0}); patch32(invalid, 26, 0xffffffffu); rejected(invalid); // oversized fmt
    invalid = wav(16, {0, 0}); text(invalid, "data"); u32(invalid, 2); u16(invalid, 0);
    patch32(invalid, 4, static_cast<std::uint32_t>(invalid.size() - 8)); rejected(invalid); // duplicate data
}
