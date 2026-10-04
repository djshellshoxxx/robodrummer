# RoboDrummer Architecture

RoboDrummer is split into a portable musical core and host-specific adapters. The portable core deliberately has no JUCE dependency. This keeps timing, groove generation, scheduling and MIDI command behavior directly testable and allows the VST3, standalone and later CLAP layers to remain thin.

## Phase 1 modules

`MusicalClock` owns tempo, meter, beat position and bar position. It accepts sample advances and exposes value snapshots. Future guitarist-following code will correct this clock gradually rather than replacing it.

`DrumEvent` is the realtime event value type. `EventScheduler` is a fixed-capacity scheduler that keeps events ordered by sample offset without allocating during `push()`.

`Style` stores behavior probabilities. `GrooveGenerator` creates a deterministic bar from a style, musical context and seed. The generator is intentionally simple in Phase 1; future style models can add ghost notes, cymbal state, fills, phrase context and limb constraints without changing the event contract.

`MidiCommand` and `JamState` define optional performance intervention. MIDI is not the main timing source. It requests musical actions such as fills, section transitions, intensity changes and transport-like behavior.

## Realtime boundary

The future plugin audio callback may read immutable snapshots and push/read fixed-capacity events. It must not perform disk access, network access, blocking waits or unbounded allocation. Sample loading, preset serialization, analysis model setup and GUI operations belong on worker or message threads.

## Planned next layers

1. JUCE VST3 and standalone wrapper.
2. Internal drum sampler and host MIDI output.
3. Groove/fill/arrangement expansion.
4. Guitar input feature extraction.
5. Online tempo and beat-phase tracker with confidence.
6. Hybrid leadership and graceful resynchronization.
7. Phrase-level jam coordination and Jam Memory.

The fast synchronization layer and the slower musical-coordination layer must remain separate. Beat/phase corrections happen continuously; fills, section interpretation and intensity arcs operate over beats and bars.
