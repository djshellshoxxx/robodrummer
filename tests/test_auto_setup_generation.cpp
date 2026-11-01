#include "analysis/AutoSetup.h"
#include <cassert>
#include <cmath>
#include <thread>
#include <vector>

using namespace robodrummer;
namespace {
std::vector<float> guitar() {
    std::vector<float> samples(48000 * 13);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double pos = std::fmod(static_cast<double>(i), 14400.0);
        samples[i] = static_cast<float>(0.4 * std::exp(-pos / 9000.0) * std::sin(6.2831853 * 196.0 * pos / 48000.0));
    }
    return samples;
}
void analyze(AutoSetup& setup, const std::vector<float>& samples, bool finalize = true) {
    setup.start();
    for (std::size_t pos = 0; pos + 512 <= samples.size() && setup.state() == AutoSetupState::Listening; pos += 512)
        setup.process(samples.data() + pos, 512);
    assert(setup.state() == AutoSetupState::Analyzing);
    if (finalize) setup.finalize();
}
}
int main() {
    const auto samples = guitar();
    AutoSetup setup;
    setup.prepare(48000.0);
    setup.start();
    const auto queued = setup.generation();
    setup.invalidate();
    assert(setup.generation() != queued && setup.state() == AutoSetupState::Cancelled);
    setup.process(samples.data(), 512);
    setup.finalize();
    assert(setup.state() == AutoSetupState::Cancelled);

    setup.start();
    setup.process(samples.data(), 512);
    setup.invalidate();
    setup.process(samples.data(), 512);
    assert(setup.state() == AutoSetupState::Cancelled);

    analyze(setup, samples, false);
    setup.invalidate();
    setup.finalize();
    assert(setup.state() == AutoSetupState::Cancelled);

    analyze(setup, samples);
    assert(setup.state() == AutoSetupState::Done);
    auto old = setup.result().proposal;
    assert(old.generation == setup.generation() && setup.accepts(old));
    auto selected = old;
    selected.bpm.apply = false;
    assert(setup.accepts(selected));
    setup.invalidate();
    assert(!setup.accepts(old) && !setup.accepts(selected));
    analyze(setup, samples);
    assert(setup.accepts(setup.result().proposal) && !setup.accepts(old));
    assert(!setup.accepts(AutoSetupProposal{}));

    // A host-state invalidation cannot be overwritten by the audio producer's
    // next Listening -> Analyzing transition, including a queued start.
    for (int iteration = 0; iteration < 100; ++iteration) {
        setup.start();
        std::thread audio([&] { setup.process(samples.data(), static_cast<int>(samples.size())); });
        setup.invalidate();
        audio.join();
        setup.finalize();
        assert(setup.state() == AutoSetupState::Cancelled);
    }
}
