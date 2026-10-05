#include "analysis/JamBrain.h"
#include <cassert>

int main() {
    using namespace robodrummer;

    JamBrain brain;
    JamBrainSettings settings;
    settings.phraseBars = 4;
    settings.minBarsBeforeFill = 3;
    brain.reset();

    JamBarObservation o;
    o.beatConfidence = 0.9f;
    o.downbeatConfidence = 0.8f;
    o.activity = 0.65f;
    o.intensity = 0.45f;

    auto d = brain.update(o, settings);
    assert(d.phraseState == PhraseState::Stable);
    assert(!d.requestFill);

    o.intensity = 0.58f;
    brain.update(o, settings);
    o.intensity = 0.72f;
    brain.update(o, settings);
    o.intensity = 0.84f;
    d = brain.update(o, settings);
    assert(d.phraseState == PhraseState::Build);
    assert(d.phraseBoundary);
    assert(d.requestFill);
    assert(d.fillStrength > 0.5f);

    // A sustained energy drop should be classified as a release, not a new build.
    o.intensity = 0.55f;
    brain.update(o, settings);
    o.intensity = 0.35f;
    d = brain.update(o, settings);
    assert(d.phraseState == PhraseState::Release);

    // Sustained near-silence should produce a break state and suppress fills.
    o.activity = 0.02f;
    o.intensity = 0.05f;
    brain.update(o, settings);
    brain.update(o, settings);
    d = brain.update(o, settings);
    assert(d.phraseState == PhraseState::Break);
    assert(d.breakLikely);
    assert(!d.requestFill);

    // Weak tracking evidence must reduce autonomous decisions.
    brain.reset();
    o = {};
    o.intensity = 0.9f;
    o.activity = 0.9f;
    o.beatConfidence = 0.15f;
    o.downbeatConfidence = 0.10f;
    for (int i = 0; i < 4; ++i)
        d = brain.update(o, settings);
    assert(!d.requestFill);
    assert(!d.phraseBoundary);
}
