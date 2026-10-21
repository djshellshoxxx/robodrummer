# Architecture and Design Rationale

Sources: `docs/ARCHITECTURE.md`, `SPEC.md`, `research/`. *(inferred)* marks conclusions from the code.

- **Portable core, thin host adapters.** Timing, groove generation, scheduling and MIDI command behaviour live in `src/core` and `src/dsp` with no JUCE dependency, so they are tested directly by ctest.
- **`MusicalClock` owns tempo and position.** Guitarist-following code is meant to correct the clock gradually, not replace it.
- **Fixed-capacity event scheduler.** `EventScheduler` keeps events ordered by sample offset and does not allocate during `push()`. This suits the realtime callback.
- **MIDI is intervention, not timing.** MIDI requests musical actions (fills, sections, intensity). It is not the main timing source.
- **Two layers of synchronisation.** Fast beat/phase correction runs continuously; fills, section interpretation and intensity arcs operate over beats and bars. These must stay separate (`docs/ARCHITECTURE.md`).
- **Deterministic groove generation.** `GrooveGenerator` creates a bar from a style, context and seed, so output can be reproduced in tests.
- **Target split.** 90% autonomous performance with about 10% optional intervention is the stated design target (README).
