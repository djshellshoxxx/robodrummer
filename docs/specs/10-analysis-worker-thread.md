# 10. ADR: analysis on the audio thread or on a worker (SPEC 33)

Status: **Decided, keep analysis on the audio thread.** Revisit only when a criterion below is met.

## Context

SPEC 33 recommends five components (audio, analysis worker, musical-intelligence worker, GUI, disk/sample worker) and SPEC 35 sketches an analysis worker that reads a ring buffer and publishes immutable snapshots. The current build (0.3.x) runs the whole guitar path inside `RoboDrummerAudioProcessor::processBlock`: `InputConditioner` -> `AdaptiveOnsetDetector` / `RhythmAnalyzer` (tempo, PLL, movement) -> `PerformanceAnalyzer` -> optional `CalibrationAnalyzer`, in chunks of `AnalysisChunk` (4096 samples, `src/plugin/PluginProcessor.h`). Sample loading is already asynchronous and the GUI reads atomics and a lock-free onset FIFO (`popOnsetMarks`), so only the analysis worker is a deviation. The audit lists it as "Deviation" and backlog item P3.

Facts that bear on the decision (from `docs/QA.md` and the harness):

* Cost: about 0.7 % of real time at 48 kHz / 512 samples with aux buses, room reverb and guitar following (development container, one shared core). The harness fails the build above 25 % (`testCpuBudget`).
* Allocation audit: 0 allocations over 1500 blocks with MIDI clock, aux buses, reverb and calibration.
* pluginval 1.0.4 at strictness 10 passes on Linux x64 (parameter thread safety, background-thread state, 64 to 1024 sample blocks at 44.1/48/96 kHz).
* Latency reported to the host: 0 samples. Scheduler lookahead follows the host buffer (`src/core/Lookahead.h`).
* Determinism: the core tests and the 5-minute soak assume analysis output is a pure function of (input samples, parameters, block boundaries). The tracking fixtures feed the same `RhythmAnalyzer` the plug-in uses.

## Decision

Keep analysis on the audio thread. Do not add an analysis worker for the deterministic DSP layers.

Reasoning:

1. **Latency.** The drummer must react to an onset detected in the same block. A worker adds at least one block of hand-off plus scheduling jitter (waking a thread through the OS is not real-time). In Guitarist Leads with tight lookahead this directly worsens phase error, which serves the top product priority (SPEC 2: rhythmic stability first).
2. **Determinism and testability.** Inline analysis is reproducible across runs. A worker makes results depend on thread scheduling, so fixtures, the soak test and field bug reports stop being replayable unless a second synchronous test path is maintained.
3. **Real-time safety surface.** A worker needs a ring buffer, a snapshot hand-off, thread lifecycle in `prepareToPlay`/`releaseResources`, and correct behaviour in offline render, where the host calls `processBlock` faster than real time. Each is a new failure mode that pluginval and hosts exercise.
4. **Cost does not justify it.** At 0.7 % the analysis is not a CPU risk. SPEC 33 forbids "uncontrolled heavy inference" on the audio thread; the current work is bounded and measured, so the rule's intent is met.
5. **Offline rendering, freeze and bounce** are correct only with inline analysis (or a sync fallback).

Proposed SPEC 33 clarification for the next revision: heavy or unbounded work (learned inference, model loads, file I/O) must leave the audio thread; bounded, measured DSP may stay on it.

## Consequences

* `docs/QA.md` keeps "Analysis runs on the audio thread" under Known limitations. `docs/COMPLIANCE.md` needs no change.
* New features must stay inside a budget: **analysis total <= 5 % of real time** on the reference container (the harness hard limit stays 25 %), zero allocations, per-block work bounded independently of input content.
* Heavy additions (Phase 5 models, see `14-learned-models-phase5.md`) must be designed with the worker below from the start.
* The GUI thread stays decoupled through atomics and the onset FIFO; nothing changes there.

## Revisit when (any one)

1. Measured analysis cost exceeds **5 %** of real time at 48 kHz / 512 samples on the reference machine, or any single block exceeds **25 %** of its real-time budget (worst case, from the CPU runs in `12-host-matrix-qa.md`).
2. A host reports xruns attributable to analysis (reproducible on a >= 128 sample buffer at <= 50 % total CPU).
3. A feature with unbounded or block-size dependent cost is accepted (learned model, long autocorrelation, spectral analysis above 2048 points per frame).
4. Real-guitar tuning (`11-real-guitar-fixtures.md`) shows that an analysis window longer than 4096 samples is required for accuracy.

Reverse because a measurement above is met, not because SPEC 33 suggests a worker.

## Worker design if reversed (ready to implement)

Scope: move only the heavy stages; keep `InputConditioner` and the onset detector inline so the tightest-latency events stay in-block.

* **Hand-off in:** single-producer single-consumer lock-free ring of conditioned mono `float` samples (capacity 2^18, about 5.4 s at 48 kHz, allocated in `prepareToPlay`). The audio thread writes each chunk with a monotonically increasing absolute sample index.
* **Worker:** one `std::thread` created in `prepareToPlay`, joined in `releaseResources` and the destructor. Wake via an atomic flag the audio thread sets (never take a lock on the audio side) with a 2 ms timed wait on the worker side as fallback. The worker uses the absolute sample index as its clock so results are stamped in host time, not wall time.
* **Hand-off out:** triple-buffered POD `AnalysisSnapshot` (tempo, phase anchor in absolute samples, confidence, hypotheses, movement class, performance features) with an acquire/release sequence counter. The audio thread takes the newest complete snapshot at block start and **extrapolates** the beat grid from the phase anchor to the current sample, so staleness costs accuracy, not alignment.
* **Staleness rule:** if the newest snapshot is older than 200 ms, mark confidence degraded and hold the last trusted state (SPEC 2); never block.
* **Determinism mode:** `AnalysisPipeline::process(chunk)` is the single shared function. A `analysisSync` mode runs it inline from `processBlock`; tests, the harness soak and offline render (`isNonRealtime()`) always use it.
* **Priority:** default or slightly above; no real-time priority escalation from a plug-in.
* **Required before enabling:** sync vs async equivalence on all tracking fixtures (tempo error < 0.5 BPM, phase error < 10 ms); audio-thread allocation audit still 0; ThreadSanitizer run of the harness; pluginval strictness 10; a prepare/release/resize churn test (1000 cycles).

## Test plan

Decision only. The guard that keeps it valid is the existing `testCpuBudget` plus a worst-case-block report (below).

## Acceptance criteria

* Every release candidate records the analysis CPU figure in `docs/QA.md`; above 5 % the release checklist (`18-release-process.md`) blocks until this ADR is reviewed.
* The harness additionally prints the worst single-block load, not only the average (small follow-up change).

## CDL compliance notes

CDL 3.2 (real-time safety) is satisfied by the current design and evidenced by the allocation audit. A worker, if added, must keep that audit at zero on the audio thread.

## Integration hook points

None now. If reversed: `prepareToPlay` (start worker, allocate ring and snapshots), `processBlock` (write chunk, read snapshot, extrapolate), `releaseResources` and destructor (stop and join), `setNonRealtime` (switch to sync mode).
