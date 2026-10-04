# RoboDrummer Engineering Specification

Version: 0.1 research baseline  
Primary implementation target: C++ with JUCE  
Initial plugin targets: VST3, CLAP  
Later targets: AU, standalone application  
Primary use case: one live guitarist jamming with an autonomous virtual drummer

## 1. Product definition

RoboDrummer is a real-time adaptive drum instrument intended to act like a jam partner rather than a loop player. It listens to one guitar input, estimates musical timing and performance state, predicts upcoming beats, and generates a stylistically appropriate drum performance.

The normal interaction should require little or no continuous user control. The target is approximately 90% autonomous behavior and 10% optional MIDI intervention.

RoboDrummer must support three timing-authority modes:

1. **Drummer Leads** — fixed/host/MIDI-clock tempo; guitar analysis can influence dynamics, accents and complexity but not the clock materially.
2. **Guitarist Leads** — the live guitar establishes and changes tempo; RoboDrummer tracks and predicts the guitarist's beat.
3. **Hybrid Leadership** — the guitarist can push/pull time while RoboDrummer applies inertia and confidence-aware stabilization.

Multi-guitarist tracking is explicitly outside this specification.

## 2. Product principles

Priority order:

1. rhythmic stability
2. reliable recovery
3. low-latency real-time safety
4. musicality
5. appropriate responsiveness
6. stylistic variety
7. higher-level intelligence
8. novelty

When uncertain, RoboDrummer should preserve the last trusted musical state rather than make a dramatic timing or structural change.

## 3. Main user workflow

A minimum free-jam workflow should be:

1. Insert RoboDrummer on a track or open the standalone app.
2. Select guitar input.
3. Select kit, drummer and style.
4. Set Leadership, for example Guitar 75%.
5. Press **Listen**.
6. Play several beats/bars.
7. RoboDrummer acquires tempo/phase confidence.
8. RoboDrummer joins on the next suitable musical boundary.
9. The player can continue without touching the computer.
10. Optional MIDI controls can request fills, change intensity, advance sections, switch half/double time, trigger crashes/breaks, or reset listening.

## 4. High-level architecture

```text
Guitar Input
    |
    v
Input Conditioning
    |
    v
Feature / Onset Analysis
    |
    +---------------------+
    |                     |
    v                     v
Tempo/Beat Tracker   Performance Analyzer
    |                     |
    v                     v
Confidence Model     Energy/Accent/Phrase State
    |                     |
    +----------+----------+
               |
               v
        Musical Clock
               |
               v
   Coordination / Jam Brain
               |
      +--------+--------+
      |        |        |
      v        v        v
   Style    Sections   Fill/Transition
      \        |        /
       \       |       /
        v      v      v
        Groove Generator
               |
               v
       Drum Event Scheduler
               |
       +-------+-------+
       |               |
       v               v
 Internal Sampler   MIDI Output
```

The architecture intentionally separates **short-timescale synchronization** from **longer-timescale musical coordination**.

## 5. Guitar input and conditioning

Supported input modes:

- mono
- stereo summed to analysis mono
- left only
- right only

Analysis path may include:

- DC removal
- high-pass filtering
- optional noise gate
- automatic or manual gain normalization
- transient enhancement
- multi-band feature extraction

The audible guitar signal does not need to be altered.

### Calibration

Optional calibration asks the guitarist to play normally for several seconds and estimates:

- RMS and peak range
- noise floor
- transient strength distribution
- average event density
- dynamic range

These values initialize thresholds but remain manually adjustable.

## 6. Real-time guitar features

Minimum feature outputs:

- onset probability
- onset timestamp
- onset strength
- short-term RMS/loudness
- spectral flux
- spectral-band energy
- transient density
- rhythmic regularity
- silence/activity probability
- accent probability

Later classification may include palm-muted versus sustained/chordal playing and solo-like versus accompaniment-like activity.

## 7. Onset detection

The detector must be robust to:

- clean single notes
- distorted power chords
- palm-muted riffs
- strumming
- sustained chords
- arpeggios
- pick noise
- amp hiss/noise

Recommended implementation is a weighted multi-feature detector using spectral flux, energy derivative, band-limited novelty and adaptive thresholds rather than a single fixed threshold.

## 8. Tempo hypothesis tracker

RoboDrummer must not keep only one tempo interpretation. It should maintain competing hypotheses, for example:

```text
120 BPM  confidence .82
 60 BPM  confidence .43
180 BPM  confidence .28
```

Each hypothesis should include:

- BPM
- beat phase
- confidence
- supporting event count
- recent consistency
- age
- predicted next beat
- relation to half/double-time alternatives

Default operating range: approximately 40-240 BPM.

## 9. Beat and phase tracking

BPM without phase is insufficient. The system must know predicted beat positions.

The active musical clock should contain at least:

```text
current tempo
beat phase
bar phase
estimated tempo velocity
beat confidence
downbeat confidence
meter confidence
```

The tracker should resemble a phase-locked adaptive clock: incoming evidence nudges the prediction, but isolated events cannot force large jumps.

## 10. Predictive scheduling

The drummer must predict future beats instead of reacting after guitar events occur.

Concept:

```text
recent onsets -> tempo/phase model -> next-beat prediction -> schedule drums
                                      ^
                                      |
                               new evidence corrects
```

The scheduler must expose a configurable lookahead appropriate to host buffer size and tracking confidence.

## 11. Tempo movement and drift

The timing model must distinguish:

- jitter/microtiming
- gradual tempo drift
- intentional acceleration/deceleration
- abrupt tempo shift
- half-time/double-time reinterpretation

State should include tempo velocity so a sequence such as 118, 119, 121, 123 BPM is interpreted as acceleration rather than unrelated measurement noise.

### Follow response presets

- Very Stable
- Stable
- Balanced
- Responsive
- Very Responsive

These map to smoothing, phase-correction limits, acceleration sensitivity and outlier rejection.

## 12. Leadership modes

### 12.1 Drummer Leads

Tempo authority comes from:

- manual BPM
- DAW host transport
- MIDI clock
- tap tempo

Guitar analysis may still drive dynamics, density and accent behavior.

### 12.2 Guitarist Leads

Guitar analysis drives the musical clock subject to confidence thresholds.

### 12.3 Hybrid

Primary control:

**Leadership 0-100%**

- 0% = drummer leads completely
- 50% = shared authority
- 100% = guitarist leads completely

Additional controls:

- Follow Strength
- Tempo Inertia
- Follow Range
- Beat Correction Speed
- Confidence Sensitivity

Example:

```text
Leadership       70%
Base tempo       120 BPM
Follow range     +/-15 BPM
Tempo inertia    Medium
Accent follow    70%
Dynamic follow   60%
```

## 13. Confidence model

All major inferences should carry confidence:

- tempo
- beat
- downbeat
- meter
- activity
- transition/phrase boundary

Default behavior guideline:

- 80-100%: normal active following
- 60-79%: conservative following
- 40-59%: limited correction
- 20-39%: preserve clock
- below 20%: freeze adaptation while continuing analysis

Confidence should have hysteresis to prevent rapid switching.

## 14. Recovery behavior

If tracking confidence falls:

1. preserve last trusted tempo and phase
2. reduce correction magnitude
3. continue evaluating multiple hypotheses
4. wait for coherent evidence
5. blend toward the new interpretation

Severe resynchronization may be disguised musically with a fill, crash, break or restart on the next confident downbeat.

## 15. Silence and re-entry

Configurable guitarist-silence behavior:

- Keep Playing
- Reduce Intensity
- Hold Groove
- Fill During Silence
- Stop After N Bars
- Wait for Resume

On resume, tracking should reacquire without immediately destroying the current groove.

## 16. Meter and downbeat

Initial beta should strongly prioritize 4/4.

Later supported meters:

- 3/4
- 6/8
- 5/4
- 7/8

Manual meter must always be available even if automatic detection exists.

Downbeat inference may use:

- accent distribution
- chord/energy changes
- phrase repetition
- prior bar model
- section history

## 17. Performance analyzer

The slower performance layer estimates:

- intensity
- density
- accent map
- silence/activity
- rhythmic regularity
- likely phrase boundary
- build/release tendency

It should not infer a section change from one transient.

## 18. Intensity model

Intensity should combine more than loudness:

- RMS/loudness
- onset density
- transient strength
- spectral density
- rhythmic activity

Output: normalized 0-100% intensity.

Style-dependent mappings can change:

- kick density
- cymbal openness
- crash probability
- velocity
- fill complexity
- ghost-note level

## 19. Drum-generation philosophy

RoboDrummer is not primarily a MIDI-loop launcher.

A style should be a behavioral model combining:

- anchor rules
- probability grids
- phrase position
- instrument roles
- density ranges
- syncopation behavior
- fill rules
- transition rules
- humanization

Static MIDI patterns may be reference material or optional user content but should not define the core engine.

## 20. Style model

A style definition should include:

- typical meter(s)
- normal tempo region as a soft preference, not a hard limit
- kick behavior
- snare/backbeat behavior
- cymbal/hat subdivision
- syncopation range
- event-density range
- ghost-note behavior
- crash/ride tendencies
- fill frequency and vocabulary
- section-change behavior
- intensity response curves
- permissible timing feel

Initial style set:

- Rock
- Hard Rock
- Classic Rock
- Metal
- Punk
- Alternative
- Grunge
- Blues
- Funk
- Soul
- Pop
- Indie
- Garage Rock
- Country
- Reggae
- Disco
- Electronic Rock
- Breakbeat
- Half-Time
- Experimental
- Jazz/Swing after the timing model is proven

## 21. Groove generation

Generate at least one upcoming bar of structural material, with short-horizon events scheduled closer to playback.

Bar states may include:

- Normal
- Build
- Release
- Fill
- Transition
- Break
- Intro
- Outro
- Crash Entry
- Half-Time
- Double-Time

## 22. Humanization and microtiming

Humanization dimensions:

- timing
- velocity
- articulation/sample choice
- limb-specific feel

Humanization must be constrained by genre and tempo. It must not simply add uniform random error.

Optional future virtual-limb model:

- left hand
- right hand
- left foot
- right foot

This can prevent physically implausible performances.

## 23. Fills and transitions

Fill probability may depend on:

- bars since last fill
- approaching programmed section boundary
- probable live phrase boundary
- intensity trend
- style
- MIDI request

Fill controls:

- frequency
- complexity
- length
- intensity
- surprise

Supported lengths:

- 1 beat
- 2 beats
- 1 bar
- 2 bars
- long transition

A MIDI-triggered **Fill** normally queues the fill to the next appropriate musical location; **Immediate Fill** can be a separate command.

## 24. Jam coordination layer

This layer handles longer-timescale interaction.

States may include:

- Establishing Groove
- Stable Jam
- Building
- Releasing
- Awaiting Cue
- Transition Likely
- Break
- Solo Support
- Ending Likely

Inputs should be trends over multiple beats/bars, not single events.

The system should treat structural decisions probabilistically unless a programmed arrangement or explicit MIDI cue overrides them.

## 25. Song and jam modes

### Free Jam

No predefined length. RoboDrummer continues indefinitely with constrained variation.

### Semi-Structured Jam

User defines named sections but manually requests transitions. RoboDrummer executes them at musical boundaries.

### Programmed Song

Timeline contains sections with:

- name
- bar count
- style/variation
- intensity
- drummer personality
- fill frequency
- leadership
- follow strength
- half/double-time state
- transition type

Different sections may use different styles.

## 26. MIDI intervention

MIDI must be optional.

Suggested default triggers:

1. Fill
2. Next Section
3. Previous Section
4. Crash
5. Intensity Up
6. Intensity Down
7. Half-Time
8. Double-Time
9. Break
10. Stop
11. Resume
12. Reset Listening

Expression-pedal defaults may control Intensity or Leadership.

All meaningful live parameters should support MIDI Learn.

## 27. MIDI output

Modes:

- Internal Drums
- MIDI Only
- Internal + MIDI

Support General MIDI and custom maps so generated performance can trigger external drum instruments.

## 28. Internal sampler

Minimum support:

- WAV
- AIFF
- velocity layers
- round robin
- choke groups
- pitch/tuning
- gain/pan
- envelopes
- per-instrument routing

Kit instruments should include kick, snare, closed/open hat, ride, crashes and toms, with optional percussion.

## 29. Mixer

Minimum channels:

- Kick
- Snare
- Toms
- Hi-Hat
- Ride
- Cymbals
- Room
- Master

Basic gain/pan/mute/solo. Multi-output routing is desirable for DAW use.

## 30. GUI

Primary tabs:

- LIVE
- KIT
- DRUMMER
- STYLE
- SONG
- MIDI
- ANALYSIS
- SETTINGS

### LIVE

Large controls/status:

- Mode
- Style
- Kit
- Leadership
- Intensity
- Complexity
- Fill Level
- Follow Range
- BPM
- Beat/Bar
- Current section
- Tempo/Beat confidence

### ANALYSIS

Development and advanced-user view:

- onset markers
- tempo candidates
- active tempo
- predicted beat grid
- phase error
- confidence history
- input level/noise threshold
- state transitions

## 31. Presets

Preset types:

- Jam Preset
- Drummer Preset
- Style Preset
- Kit Preset
- Song Preset
- Analysis Preset

DAW state restoration must serialize all settings and reference user samples safely.

## 32. Session/Jam memory

Initial Jam Memory should use transparent statistics, not a heavyweight learned model.

Possible observations:

- typical tempo and drift
- average phrase duration
- recurring intensity changes
- pause lengths
- preferred fill density

It may adjust probabilities during a session without permanently changing global presets.

## 33. Threading and real-time safety

Recommended components:

- Audio Thread
- Analysis Worker
- Musical Intelligence Worker
- GUI Thread
- Disk/Sample Worker

Audio thread must not:

- allocate dynamically in normal processing
- block on mutexes
- touch disk
- perform network I/O
- run uncontrolled heavy inference

Communication should use ring buffers, atomics, preallocated queues and immutable/snapshotted state.

## 34. Suggested modules

```text
AudioEngine
InputAnalyzer
OnsetDetector
TempoTracker
BeatTracker
ConfidenceEngine
MusicalClock
PerformanceAnalyzer
JamCoordinator
StyleEngine
GrooveGenerator
FillGenerator
ArrangementEngine
DrumSampler
MidiEngine
PresetManager
SessionMemory
Diagnostics
UI
```

## 35. Processing pseudocode

```cpp
processBlock(audio, midi)
{
    captureGuitarInputToLockFreeAnalysisBuffer(audio);
    auto state = realtimeAnalysisSnapshot.load();

    musicalClock.updateFrom(state);
    midiEngine.consumeCommands(midi);
    scheduler.collectEventsForBlock(musicalClock);

    drumSampler.render(scheduler.events(), audio);
    midiEngine.writeGeneratedDrumMidi();

    publishRealtimeGuiState();
}
```

Analysis worker:

```cpp
while (running)
{
    samples = analysisBuffer.read();
    features = featureExtractor.process(samples);
    onsets = onsetDetector.process(features);

    tempoTracker.update(onsets);
    beatTracker.update(tempoTracker.candidates(), onsets);
    performanceAnalyzer.update(features, onsets);
    confidenceEngine.update();

    publishImmutableSnapshot();
}
```

## 36. Testing strategy

Required test layers:

- unit tests
- integration tests
- deterministic analysis fixtures
- real-time safety tests
- CPU/latency benchmarks
- host compatibility tests
- musical behavior tests

Synthetic fixtures should include:

- steady metronomic input
- tempo ramps
- missing events
- extra/syncopated events
- silence
- half/double-time ambiguity
- sudden tempo change
- noisy guitar-like transients

Real guitar fixtures should include clean strumming, distortion, palm muting, blues, funk, slow sustained chords, arpeggios, riffs and deliberately irregular time.

Every field-discovered tracking bug should become a regression fixture.

## 37. Beta acceptance scenario

The first meaningful live beta is successful when this works repeatedly:

1. user selects Rock + Guitar Leads
2. guitarist plays four bars
3. system acquires tempo/phase
4. drummer enters at a sensible boundary
5. guitarist gradually accelerates
6. drummer follows without obvious jumps
7. guitarist becomes more intense
8. drummer appropriately increases energy
9. guitarist requests a fill by MIDI
10. drummer executes it musically
11. guitar briefly stops
12. configured break/silence behavior occurs
13. guitar resumes
14. drummer continues without manual restart

## 38. Development phases

### Phase 1 — conventional virtual drummer

- JUCE project
- sampler
- kits
- fixed tempo
- host sync
- procedural style engine
- fills
- humanization
- MIDI control/output

### Phase 2 — guitar feature interaction

- input calibration
- onset detection
- dynamics
- accents
- activity/silence
- first tempo estimator
- confidence UI

### Phase 3 — true guitarist leadership

- competing tempo hypotheses
- beat phase
- predictive scheduling
- tempo velocity
- confidence gating
- hybrid leadership
- recovery/resync

### Phase 4 — jam intelligence

- phrase-boundary probability
- longer-timescale interaction states
- adaptive fills/transitions
- Jam Memory
- richer meter support

### Phase 5 — optional learned intelligence

Only after deterministic/statistical layers are validated:

- compact learned beat/downbeat models
- section/playing-state classification
- learned style adaptation

## 39. Non-goals for initial releases

- multi-guitarist tracking
- full song recognition
- lyric analysis
- automatic chord transcription as a dependency
- arbitrary polyphonic transcription
- fully autonomous human-level musical intent inference

## 40. Research-derived architectural requirements

Initial literature review produced several requirements that are now part of the specification:

1. **Synchronization and coordination are different timescales.** Beat/phase tracking must remain separate from higher-level jam behavior.
2. **Tempo should not be artificially rigid by default.** Human popular-music performances can contain gradual drift and expressive variability.
3. **Natural microtiming should be style-aware, not simply randomized.**
4. **Genre/style preference strongly affects perceived groove.** Styles therefore need distinct rhythmic behavior instead of one generic groove with different samples.
5. **Drum-pattern changes communicate formal motion.** Section and build/release logic should modify layers, rhythm and instrumentation.
6. **Online analysis must be causal.** Offline beat trackers are not suitable as the live timing authority.
7. **Multiple hypotheses and explicit confidence are required.** A single fragile beat interpretation is not acceptable for live jamming.

See the `research/` directory for sources and detailed notes.
