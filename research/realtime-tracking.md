# Real-Time Beat, Downbeat and Tempo Tracking Research Notes

Date: 2026-10-04

## 1. Why causal tracking matters

RoboDrummer cannot use an algorithm that requires access to the future of a recorded song. The live timing authority must be causal or use only a very small bounded lookahead.

The system must estimate:

- tempo
- beat phase
- next predicted beat
- downbeat/bar phase
- meter where possible
- confidence

and update these continuously from a live guitar stream.

## 2. BeatNet

Heydari, Cwitkowitz and Duan proposed BeatNet, an online system for joint beat, downbeat and meter tracking using causal convolutional/recurrent processing plus sequential Monte Carlo particle filtering. The work is relevant because it explicitly targets real-time rhythmic inference and maintains probabilistic state rather than a single brittle interpretation.

Engineering takeaways:

- use multiple hypotheses
- treat beat and downbeat as related but distinct estimates
- particle/state-space methods are a viable approach to uncertainty
- meter can be estimated online rather than necessarily fixed
- computational gating matters in real time

Paper:

- Heydari, M., Cwitkowitz, F., & Duan, Z. (2021). *BeatNet: CRNN and Particle Filtering for Online Joint Beat, Downbeat and Meter Tracking*. ISMIR / arXiv 2108.03576. https://consensus.app/papers/beatnet-crnn-and-particle-filtering-for-online-joint-beat-heydari-cwitkowitz/4b84b3e32b54598596c24f4d591264ac/

Project:

- https://github.com/mjhydri/BeatNet

The repository describes streaming, realtime, online and offline modes and includes training code and pretrained models. The repository license is CC BY 4.0. RoboDrummer should treat it as reference/prototyping material unless the eventual licensing plan is reviewed carefully.

## 3. Multiple hypotheses are not optional

Classic real-time beat-tracking work such as Allen and Dannenberg emphasized that a system retaining only one interpretation can fail catastrophically after a mistake. Their proposed approach considered multiple possible beat interpretations and selected the most credible one.

This aligns directly with RoboDrummer's need to survive:

- half/double-time ambiguity
- syncopation
- missing guitar attacks
- extra attacks
- tempo movement
- temporary silence

Implementation concept:

```text
hypothesis 1: 118 BPM / phase A / confidence .82
hypothesis 2:  59 BPM / phase A / confidence .44
hypothesis 3: 177 BPM / phase B / confidence .21
```

The active clock is based on the best stable interpretation, but alternates remain alive long enough to permit recovery.

## 4. Causal prediction rather than reaction

A drummer cannot sound tight if every hit is triggered only after detecting the corresponding guitar attack.

Required cycle:

```text
observe recent events
        |
        v
estimate period + phase
        |
        v
predict future beat time
        |
        v
schedule drum event before deadline
        |
        v
new guitar evidence corrects future prediction
```

This distinction is fundamental.

## 5. Proposed tracker state

```cpp
struct RhythmState {
    double tempoBpm;
    double tempoVelocity;
    double beatPhase;
    double predictedNextBeatSeconds;
    int beatInBar;
    int meterNumerator;

    float tempoConfidence;
    float beatConfidence;
    float downbeatConfidence;
    float meterConfidence;
};
```

## 6. Confidence and hysteresis

Confidence should affect how much new evidence can move the clock.

Example:

```text
high confidence:
    normal adaptive corrections

medium confidence:
    smaller corrections

low confidence:
    maintain prediction and observe

very low confidence:
    freeze timing adaptation, retain last trusted clock
```

Use hysteresis so the system does not repeatedly enter/leave tracking states on adjacent frames.

## 7. Guitar-specific challenge

Many beat trackers are trained and tested on complete mixes containing drums. RoboDrummer is unusual because its timing source may be isolated guitar.

This creates a risk:

- sustained notes provide weak onset information
- syncopated riffs may imply subdivisions rather than tactus beats
- distorted guitar can create many spectral changes
- arpeggios can produce dense onset streams
- palm-muted riffs can be highly percussive

Therefore a generic full-mix model must not be assumed to perform optimally on guitar.

Recommended development sequence:

1. build a transparent DSP onset/tempo baseline
2. collect representative isolated-guitar fixtures
3. benchmark open models against those fixtures
4. measure acquisition time, continuity and phase error
5. only then decide whether a learned model belongs in the release engine

## 8. Acquisition versus tracking

Treat initial acquisition differently from ongoing tracking.

### Acquisition

Goal: determine plausible tempo/phase from little history.

Permitted behavior:

- remain silent/listening
- show confidence building
- optionally use tap-tempo seed

### Tracking

Goal: preserve continuity.

Once locked, the prior state should strongly constrain new estimates.

This prevents the engine from re-solving tempo from scratch every bar.

## 9. Tempo velocity

State should include rate of tempo change.

Example:

```text
118 -> 119 -> 121 -> 123 BPM
```

should increase confidence in an acceleration trend.

A simple early model could use a Kalman-style state or filtered derivative:

```text
state = [tempo, tempoVelocity, phase]
```

## 10. Phase correction limits

Do not hard-jump phase for small errors.

Instead distribute correction over future beats.

Example:

```text
phase error = +18 ms
apply +4 ms correction for several beats
```

Exact limits need listening tests.

## 11. Hard-resync strategy

If phase is clearly wrong by a musically significant amount and a high-confidence downbeat is found, resynchronization can be masked as a musical event:

- short fill
- break
- crash
- restart on beat 1

The jam brain should choose an action compatible with current style and energy.

## 12. Evaluation metrics

RoboDrummer testing should track more than BPM error.

Useful metrics:

- acquisition time
- beat F-measure or equivalent event-match score
- continuity
- mean predicted-beat timing error
- 95th-percentile timing error
- tempo error
- number of half/double-time failures
- recovery time after deliberate disruption
- false resynchronizations
- CPU cost
- algorithmic latency

For the product, perceived continuity may matter more than a slightly better average offline benchmark.

## 13. Test fixtures

Synthetic:

- 60/90/120/160/200 BPM pulse trains
- tempo ramps
- missing beats
- inserted offbeats
- syncopated patterns
- half-time switch
- double-time switch
- silence and re-entry

Recorded guitar:

- clean quarter/eighth strumming
- distorted power chords
- palm-muted eighths/sixteenths
- funk syncopation
- blues shuffle
- arpeggios
- long sustained chords
- irregular beginner timing
- intentional ritardando/accelerando

## 14. Candidate implementation path

### Baseline A: transparent DSP

- adaptive onset novelty
- inter-onset interval histogram
- autocorrelation/comb candidates
- competing phase agents
- confidence scoring

Advantages:

- easy to debug
- no model licensing/deployment complexity
- can be optimized directly in C++

### Baseline B: learned activation + probabilistic tracker

- compact causal network produces beat/downbeat likelihoods
- particle/beam/state-space tracker converts activations into predictions

Advantages:

- potentially more robust across complex input

Disadvantages:

- training data and model licensing
- CPU complexity
- isolated-guitar domain mismatch

Recommended: build A first, benchmark B experimentally.

## 15. Research conclusion

The timing engine should be designed around prediction, multiple hypotheses, explicit confidence and recovery from the beginning. These are architectural requirements, not later polish.
