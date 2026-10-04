# Phase 2 Scope: JUCE Shell

This phase converts RoboDrummer from a tested headless core into a playable plugin/standalone shell.

Included:

- JUCE VST3 target
- JUCE Standalone target
- Host transport adapter
- MIDI intervention mapping
- Internal sample player
- Jam engine bridge from core groove events to audio/MIDI
- Minimal live GUI
- Parameter/state persistence
- Windows/Linux CI build coverage

Excluded until the next phase:

- Live guitar onset detection
- Adaptive tempo/beat tracking
- Guitarist-leads mode
- Phrase/section inference
- Jam Memory

The architectural rule for this phase is that the existing core remains JUCE-independent and testable with plain C++.
