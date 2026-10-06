#include "analysis/SilenceController.h"
#include <cassert>

int main() {
    using namespace robodrummer;

    SilenceController controller;
    SilenceSettings settings;
    settings.mode = SilenceMode::ReduceIntensity;
    settings.silenceThreshold = 0.08f;

    controller.reset();
    auto d = controller.update(0.7f, settings);
    assert(d.silentBars == 0);
    assert(d.intensityMultiplier == 1.0f);

    d = controller.update(0.02f, settings);
    assert(d.silentBars == 1);
    assert(d.intensityMultiplier < 1.0f);
    d = controller.update(0.02f, settings);
    assert(d.intensityMultiplier < 0.9f);
    assert(!d.stopDrums);

    settings.mode = SilenceMode::StopAfterBars;
    settings.stopAfterBars = 2;
    controller.reset();
    controller.update(0.01f, settings);
    d = controller.update(0.01f, settings);
    assert(d.stopDrums);
    assert(d.waitingForResume);

    d = controller.update(0.75f, settings);
    assert(d.resumeDrums);
    assert(d.markResumeWithCrash);
    assert(d.silentBars == 0);

    settings.mode = SilenceMode::FillDuringSilence;
    controller.reset();
    d = controller.update(0.01f, settings);
    assert(d.requestFill);
    d = controller.update(0.01f, settings);
    assert(!d.requestFill); // one fill request per silence episode

    settings.mode = SilenceMode::WaitForResume;
    controller.reset();
    d = controller.update(0.01f, settings);
    assert(d.stopDrums);
    assert(d.waitingForResume);
    d = controller.update(0.8f, settings);
    assert(d.resumeDrums);
}
