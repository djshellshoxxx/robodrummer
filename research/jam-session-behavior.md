# Jam Session Behavior Research Notes

Date: 2026-10-04

## Purpose

RoboDrummer is aimed at unscripted or semi-structured jam sessions, so its timing model should reflect how human musicians actually coordinate rather than assuming a permanently fixed metronomic grid.

## 1. Entrainment versus coordination

Clayton et al. distinguish short-timescale sensorimotor synchronization from longer-timescale coordination in musical performance. Their review describes synchronization as largely automatic at rhythmic timescales while coordination includes longer musical structures and more deliberate/shared behavior.

Engineering implication:

- beat/phase following should operate continuously at a short timescale
- phrase, section, build, break and transition logic should operate more slowly
- higher-level decisions should consume trends accumulated over several beats/bars rather than reacting directly to every onset

Source:

- Clayton, M., Jakubowski, K., Eerola, T., Keller, P. E., Camurri, A., Volpe, G., & Alborno, P. (2020). *Interpersonal Entrainment in Music Performance*. Music Perception. https://consensus.app/papers/interpersonal-entrainment-in-music-performance-clayton-jakubowski/f401cf31ce6f59a18bc74cfbcaaf075f/

## 2. Improvised coordination is emergent and context-sensitive

Walton et al. studied improvising professional pianists and found that coordination patterns changed depending on the rhythmic/harmonic structure of the backing context. Improvisation behaved as a multiscale interaction rather than a fixed leader/follower script. Their results also support turn-taking as part of improvised musical interaction.

Engineering implication:

RoboDrummer should not assume one permanent interaction rule. The jam brain should track contextual states such as:

- establishing groove
- stable groove
- build
- release
- waiting/listening
- transition likely
- break
- solo-support state

The same guitar event can mean different things depending on recent context.

Source:

- Walton, A., Washburn, A., Langland-Hassan, P., Chemero, A., Kloos, H., & Richardson, M. (2017). *Creating time: social collaboration in music improvisation*. Topics in Cognitive Science, 10, 95-119. https://consensus.app/papers/creating-time-social-collaboration-in-music-walton-washburn/4ab8f5b7b21c5de889eb19ebfd00136a/

## 3. Shared intent can emerge during improvisation

Goupil et al. reported that shared intentions can emerge spontaneously during collective musical improvisation and can improve coordination. Musicians use communicative strategies to make intentions perceptible to collaborators.

Engineering implication:

RoboDrummer cannot directly know intent, but it can maintain probabilities for musically meaningful hypotheses rather than binary decisions. Example cues:

- sustained chord near phrase boundary
- sudden drop in density
- repeated accent pattern
- rising energy over several bars
- silence near a predicted downbeat
- repeated phrase length

A cue should increase or decrease probabilities for actions such as fill, build, break or section transition rather than forcing them immediately.

Source:

- Goupil, L., Wolf, T., Saint-Germier, P., Aucouturier, J., & Canonne, C. (2021). *Emergent Shared Intentions Support Coordination During Collective Musical Improvisations*. Cognitive Science, 45(1). https://consensus.app/papers/emergent-shared-intentions-support-coordination-during-goupil-wolf/d255e1453c7b5de6a4a7a6f8224525e6/

## 4. Tempo should be allowed to move naturally

Condit-Schultz and Clark analyzed measure-level tempo estimates across more than 45,000 recordings in several Western genres and explicitly distinguish gradual tempo drift from abrupt tempo shifts. They found historical changes in tempo stability associated with production technology such as click tracks and quantization.

Engineering implication:

RoboDrummer should represent at least three timing phenomena separately:

1. microtiming/jitter
2. gradual tempo drift
3. abrupt tempo shift

The guitarist-following engine therefore needs tempo velocity/acceleration, not just instantaneous BPM.

For a jam-oriented product, a perfectly rigid tempo should be an option rather than the only behavior.

Source:

- Condit-Schultz, N., & Clark, B. (2024). *Have we sold our souls to the drum machine? A historical analysis of tempo stability in Western music recordings*. Musicae Scientiae, 28, 451-477. https://consensus.app/papers/have-we-sold-our-souls-to-the-drum-machine-a-historical-condit-schultz-clark/8119a4a462c450c2a3a4d336501853a7/

## 5. Human timing is not the same as random timing error

Hofmann, Wesolowski and Goebl examined jazz trio synchronization. They found measurable performer-specific timing differences and small asynchronies, with listeners preferring tightly interlocked performances that retained natural timing variability over fully quantized timing.

Engineering implication:

Humanization should be structured and instrument/style dependent.

Do not implement humanization as one uniform random timing range across all drum hits.

Potential style model fields:

```text
kick timing tendency
snare timing tendency
hat/ride timing tendency
swing/subdivision model
velocity variance
allowed inter-instrument offsets
```

Source:

- Hofmann, A., Wesolowski, B. C., & Goebl, W. (2017). *The Tight-interlocked Rhythm Section: Production and Perception of Synchronisation in Jazz Trio Performance*. Journal of New Music Research, 46, 329-341. https://consensus.app/papers/the-tightinterlocked-rhythm-section-production-and-hofmann-wesolowski/24591565e80c5ab58c96c4fdce0f08b4/

## 6. Swing changes with tempo

Friberg and Sundström measured jazz swing timing and found that the long-short eighth-note relationship changes with tempo rather than remaining a fixed 2:1 ratio. Slow performances showed much larger ratios while faster playing approached more even subdivisions.

Engineering implication:

Any swing style must make swing amount a function of tempo. A fixed percentage cannot represent expert jazz timing across the usable range.

Source:

- Friberg, A., & Sundström, A. (2002). *Swing Ratios and Ensemble Timing in Jazz Performance: Evidence for a Common Rhythmic Pattern*. Music Perception, 19, 333-349. https://consensus.app/papers/swing-ratios-and-ensemble-timing-in-jazz-performance-friberg-sundström/c633e1510a8353a28d89cdacbbf9e063/

## 7. Proposed RoboDrummer jam-state model

A first practical state machine should be probabilistic rather than rigid:

```text
LISTENING
   |
   v
ESTABLISHING_GROOVE
   |
   v
STABLE_JAM <------+
   |               |
   +--> BUILD -----+
   |               |
   +--> RELEASE ---+
   |               |
   +--> BREAK -----+
   |               |
   +--> TRANSITION-+
   |
   +--> SOLO_SUPPORT
   |
   +--> ENDING_LIKELY
```

Each state should adjust probabilities rather than dictate an exact MIDI pattern.

## 8. Suggested observations over multiple bars

Maintain rolling measurements for:

- tempo mean
- tempo slope
- phase error
- onset density
- accent distribution
- RMS/intensity
- silence duration
- phrase repetition
- approximate phrase length
- bars since last fill
- bars since style variation

Recommended windows:

- short: 1-2 beats
- medium: 1-2 bars
- long: 4-16 bars

These windows give RoboDrummer separate evidence for immediate timing corrections versus structural behavior.

## 9. Jam-session behavior to avoid

The engine should avoid:

- changing tempo due to one syncopated note
- deciding that silence always means stop
- filling at every phrase boundary
- forcing a section change because intensity increases once
- quantizing expressive push/pull out of the performance
- chasing every onset so aggressively that the drummer becomes unstable

## 10. Research gap relevant to RoboDrummer

Much existing work studies ensemble synchronization, improvisation, jazz timing or beat tracking separately. RoboDrummer's engineering problem sits at their intersection: causal audio tracking plus procedural drum generation plus longer-timescale improvised interaction.

That gap supports an architecture where the system is explicitly split into:

- perception
- short-term synchronization
- confidence/recovery
- coordination
- stylistic drum generation
