# Architecture and Design Rationale

References: [architecture](../ARCHITECTURE.md), [SPEC.md](../../SPEC.md), [research](../../research/) and the [code map](Codebase-Map.md). The boundaries below describe the current source.

- **Portable algorithms and host adapters.** `src/core`, `src/analysis` and `src/dsp` contain JUCE-independent algorithms. The processor adapts host audio, MIDI and transport; the editor and preset adapters handle GUI and persistence. Portable production algorithms are exercised directly by CTest.
- **The live engine owns the rendering timeline.** [JamEngine](../../src/plugin/JamEngine.h) holds the live tempo and sample cursor. [PluginProcessor](../../src/plugin/PluginProcessor.cpp) applies host/internal/MIDI Clock tempo selection, timing authority and phase correction to it. [MusicalClock](../../src/core/MusicalClock.h) remains a separate portable tested clock component; it is not the live plug-in's rendering clock.
- **Fixed-capacity scheduling.** [EventScheduler](../../src/core/EventScheduler.h) keeps events ordered by sample offset without allocating during `push()`. Callback work and handoffs must obey the stricter [real-time contract](Developer-Guide.md#rules), including no allocation/deallocation or locks.
- **MIDI notes request intervention; MIDI Clock can supply tempo.** Note bindings request fills, sections, intensity and practice actions. The explicit MIDI Clock tempo-source option consumes incoming clock/transport messages through [MidiClockTracker](../../src/core/MidiClockTracker.h); it is distinct from note commands.
- **Two layers of synchronization.** Fast beat/phase correction runs continuously; fills, section interpretation and intensity arcs operate over beats and bars. These remain separate responsibilities.
- **Deterministic groove generation.** `GrooveGenerator` creates a bar from a style, context and seed, so output can be reproduced in tests.
- **Target split.** 90% autonomous performance with about 10% optional intervention is the stated design target (README).
