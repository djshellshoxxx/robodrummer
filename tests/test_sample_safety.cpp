#include "audio/DrumSamplePlayer.h"
#include <array>
#include <cassert>
#include <cmath>
#include <limits>

int main() {
    using namespace robodrummer;
    for (int install = 0; install < 3; ++install) {
        DrumSamplePlayer player;
        DrumSamplePlayer::Sample bad;
        bad.left = { std::numeric_limits<float>::quiet_NaN(), INFINITY, -INFINITY,
                     std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), 0.25f, 1.25f };
        bad.right = bad.left;
        if (install == 0) player.setSample(DrumInstrument::Kick, bad);
        else {
            DrumSamplePlayer::Sample good; good.left = { 0.25f };
            player.setVelocityLayer(DrumInstrument::Kick, 0, 0.0f, 1.0f,
                                    install == 1 ? bad : good);
            if (install == 2) {
                assert(player.addRoundRobinSample(DrumInstrument::Kick, 0, bad));
                player.trigger({DrumInstrument::Kick, 0, 1.0f, 0});
                player.clear(); // select the second round-robin variant
            }
        }
        player.setInstrumentBus(DrumInstrument::Kick, 1);
        player.trigger({DrumInstrument::Kick, 0, 1.0f, 0});
        std::array<float, 7> main{}, aux{}, room{};
        float* buses[] { main.data(), aux.data() };
        player.renderBuses(buses, nullptr, 2, 7, room.data());
        for (std::size_t i = 0; i < aux.size(); ++i) {
            assert(std::isfinite(aux[i]) && std::abs(aux[i]) <= 16.0f);
            assert(std::isfinite(room[i]) && room[i] == aux[i]);
            assert(main[i] == 0.0f);
        }
        assert(aux[0] == 0.0f && aux[1] == 0.0f && aux[2] == 0.0f);
        assert(aux[5] == 0.25f && aux[6] == 1.25f);
        // A valid hit after malformed data must recover on both main and send.
        DrumSamplePlayer::Sample valid; valid.left = { 0.75f };
        player.setSample(DrumInstrument::Kick, std::move(valid));
        player.setInstrumentBus(DrumInstrument::Kick, 0);
        player.trigger({DrumInstrument::Kick, 0, 1.0f, 0});
        main.fill(0.0f); room.fill(0.0f);
        player.renderBuses(buses, nullptr, 2, 1, room.data());
        assert(main[0] == 0.75f && room[0] == 0.75f);
    }
}
