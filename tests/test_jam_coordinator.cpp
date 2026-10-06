#include "analysis/JamCoordinator.h"
#include <cassert>

int main() {
    using namespace robodrummer;

    JamCoordinator coordinator;
    JamCoordinatorSettings settings;
    settings.mode = JamMode::FreeJam;
    coordinator.reset();

    JamCoordinatorObservation o;
    o.activity = 0.7f;
    o.intensity = 0.5f;
    o.trackingConfidence = 0.9f;
    o.phraseState = PhraseState::Stable;

    auto d = coordinator.update(o, settings);
    assert(d.state == JamCoordinationState::EstablishingGroove);

    d = coordinator.update(o, settings);
    assert(d.state == JamCoordinationState::EstablishingGroove);

    d = coordinator.update(o, settings);
    assert(d.state == JamCoordinationState::StableJam);

    // A build should remain a build until a trusted musical boundary arrives.
    o.phraseState = PhraseState::Build;
    d = coordinator.update(o, settings);
    assert(d.state == JamCoordinationState::Building);
    assert(!d.executeTransition);

    o.phraseBoundary = true;
    d = coordinator.update(o, settings);
    assert(d.state == JamCoordinationState::TransitionLikely);
    assert(d.transitionProbability >= 0.65f);
    assert(!d.executeTransition); // Free Jam changes behavior but does not invent a named section.

    // Semi-structured mode waits for the player to request the transition.
    coordinator.reset();
    settings.mode = JamMode::SemiStructured;
    o.phraseBoundary = true;
    o.phraseState = PhraseState::Build;
    d = coordinator.update(o, settings);
    assert(d.state == JamCoordinationState::EstablishingGroove);
    coordinator.update(o, settings);
    d = coordinator.update(o, settings);
    assert(d.state == JamCoordinationState::AwaitingCue);
    assert(!d.executeTransition);

    o.nextSectionCue = true;
    d = coordinator.update(o, settings);
    assert(d.state == JamCoordinationState::TransitionLikely);
    assert(d.executeTransition);

    // Programmed song boundaries are authoritative.
    coordinator.reset();
    settings.mode = JamMode::ProgrammedSong;
    o = {};
    o.activity = 0.8f;
    o.intensity = 0.6f;
    o.trackingConfidence = 0.9f;
    o.programmedBoundaryDue = true;
    d = coordinator.update(o, settings);
    assert(d.state == JamCoordinationState::TransitionLikely);
    assert(d.executeTransition);
    assert(d.transitionProbability == 1.0f);

    // Explicit solo/end cues override probabilistic inference.
    o.programmedBoundaryDue = false;
    o.soloCue = true;
    d = coordinator.update(o, settings);
    assert(d.state == JamCoordinationState::SoloSupport);
    assert(d.intensityBias < 0.0f);
    assert(d.suppressBusyFills);

    o.soloCue = false;
    o.endCue = true;
    d = coordinator.update(o, settings);
    assert(d.state == JamCoordinationState::EndingLikely);
    assert(d.endingProbability == 1.0f);

    // Sustained low activity is a break independent of jam mode.
    coordinator.reset();
    settings.mode = JamMode::FreeJam;
    o = {};
    o.trackingConfidence = 0.8f;
    o.activity = 0.02f;
    for (int i = 0; i < 2; ++i)
        d = coordinator.update(o, settings);
    assert(d.state == JamCoordinationState::Break);

    // Low-confidence evidence must not cause autonomous structural decisions.
    coordinator.reset();
    o = {};
    o.activity = 0.8f;
    o.intensity = 0.9f;
    o.trackingConfidence = 0.15f;
    o.phraseState = PhraseState::Build;
    o.phraseBoundary = true;
    for (int i = 0; i < 4; ++i)
        d = coordinator.update(o, settings);
    assert(d.state == JamCoordinationState::StableJam);
    assert(!d.executeTransition);
}
