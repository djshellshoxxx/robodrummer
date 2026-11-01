# Codebase Map: where to find what

| I want to... | Look in |
|---|---|
| Musical clock, groove generator, scheduler, styles, MIDI commands, jam state | `src/core/` |
| DSP | `src/dsp/` |
| Analysis (tempo, downbeat tracking, auto setup, MIDI capture) | `src/analysis/` |
| Audio I/O and realtime boundary | `src/audio/` |
| JUCE plugin and editor (VST3/CLAP/Standalone) | `src/plugin/` |
| Core tests | `tests/` (55 entries including the `harness/` folder) |
| Build | `CMakeLists.txt`, `docs/BUILDING.md`, `docs/JUCE_BUILD.md` |
| Installer | `installer/RoboDrummer.iss` |
| CI and packaging | `.github/workflows/`, `scripts/ci/` |
| Specs | `SPEC.md`, `docs/specs/` |
| Research | `research/` |
| Feature audits | `docs/feature-audit/` |
