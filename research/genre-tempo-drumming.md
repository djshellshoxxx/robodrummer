# Genre, Tempo, Groove and Drumming Research Notes

Date: 2026-10-04

## Purpose

This note collects research relevant to how RoboDrummer should vary drumming by style, tempo, intensity and formal position. Exact BPM ranges should be treated as soft priors rather than hard genre rules.

## 1. Groove depends on structure and listener/style context

Senn et al. reconstructed 248 drum patterns from pop, rock, funk, heavy metal, rock'n'roll, hip hop, soul and R&B and collected thousands of groove ratings. Syncopation and event density were positively associated with groove, but listener familiarity and preference for the perceived style had substantially stronger relationships with ratings.

Engineering implications:

- each RoboDrummer style needs genuinely different rhythmic behavior
- changing only drum samples is insufficient
- user taste should be represented explicitly through style presets and variation controls
- Syncopation and Density should be controllable style parameters
- a style should constrain how those parameters move with intensity

Source:

- Senn, O., Kilchenmann, L., Bechtold, T., & Hoesl, F. (2018). *Groove in drum patterns as a function of both rhythmic properties and listeners' attitudes*. PLoS ONE, 13. https://consensus.app/papers/groove-in-drum-patterns-as-a-function-of-both-rhythmic-senn-kilchenmann/feaa18beafa35fffb142759012052a91/

## 2. Drum patterns communicate song form

Geary's corpus study of post-millennial pop treats drum-pattern changes through three dimensions: number of layers, rhythm and instrumentation. The study argues that drums can express formal boundaries, motion and build/release functions across sections and entire songs.

This is directly useful for RoboDrummer.

Represent a section's drum state as at least:

```text
layer count
rhythmic density
instrumentation set
accent structure
fill/transition state
```

A build does not necessarily require a new beat pattern. It may instead add layers, move from closed hat to open hat/ride, add kick activity, increase subdivision density, introduce crashes or expand fills.

A release can do the reverse.

Source:

- Geary, D. (2024). *Formal Functions of Drum Patterns in Post-Millennial Pop Songs, 2012-2021*. Music Theory Online. https://consensus.app/papers/formal-functions-of-drum-patterns-in-postmillennial-pop-geary/0781d19b362357a38da695b114f013f2/

## 3. Tempo stability itself is stylistic and historical

Condit-Schultz and Clark analyzed tempo stability across Blues, Classical, Country, EDM, Hip hop, Jazz and Rock recordings from 1920-2020. Their distinction between gradual drift and abrupt shifts is important for a jam product.

Implementation rule:

A style preset may specify a preferred timing-stability profile.

Example conceptual values:

```text
Modern EDM-inspired rock: high stability
Modern pop/quantized rock: high stability
Garage/live rock: medium stability
Blues jam: medium/loose stability
Jazz/swing: adaptive expressive timing
Experimental: user-defined
```

These are engineering defaults, not claims that every performance in a genre follows them.

Source:

- Condit-Schultz, N., & Clark, B. (2024). *Have we sold our souls to the drum machine? A historical analysis of tempo stability in Western music recordings*. Musicae Scientiae, 28, 451-477. https://consensus.app/papers/have-we-sold-our-souls-to-the-drum-machine-a-historical-condit-schultz-clark/8119a4a462c450c2a3a4d336501853a7/

## 4. Swing timing should scale with tempo

Friberg and Sundström found that jazz ride-cymbal swing ratios varied strongly with tempo, approaching straighter subdivisions at faster tempos and becoming more unequal at slower tempos.

Implementation rule:

Do not encode jazz swing as a fixed "66% swing" setting.

Use a tempo-conditioned curve:

```text
swingRatio = f(tempo, style, drummerPersonality)
```

Source:

- Friberg, A., & Sundström, A. (2002). *Swing Ratios and Ensemble Timing in Jazz Performance: Evidence for a Common Rhythmic Pattern*. Music Perception, 19, 333-349. https://consensus.app/papers/swing-ratios-and-ensemble-timing-in-jazz-performance-friberg-sundström/c633e1510a8353a28d89cdacbbf9e063/

## 5. Microtiming should be style-aware

Hofmann et al. found that jazz rhythm-section timing contains small, systematic asynchronies and that fully quantized timing was not necessarily preferred. Timing relationships were instrument-specific.

Implementation rule:

Each style/drummer preset should be able to define timing tendencies separately for kick, snare, hat and ride rather than using random jitter applied globally.

Source:

- Hofmann, A., Wesolowski, B. C., & Goebl, W. (2017). *The Tight-interlocked Rhythm Section: Production and Perception of Synchronisation in Jazz Trio Performance*. Journal of New Music Research, 46, 329-341. https://consensus.app/papers/the-tightinterlocked-rhythm-section-production-and-hofmann-wesolowski/24591565e80c5ab58c96c4fdce0f08b4/

## 6. Practical style representation

Every RoboDrummer style should define behavioral ranges instead of one canonical groove.

Suggested schema:

```yaml
style: hard_rock
meter_preferences:
  - 4/4
tempo_prior:
  center: 120
  soft_min: 75
  soft_max: 180
pulse:
  backbeat_strength: 0.95
  kick_density: [0.35, 0.70]
  hat_subdivision: eighth
  syncopation: [0.05, 0.25]
  ghost_notes: [0.00, 0.15]
intensity_mapping:
  kick_density: positive
  open_hat_probability: positive
  crash_probability: positive
  ghost_notes: slight_negative
fills:
  base_frequency: medium
  complexity: medium
humanization:
  timing_profile: tight_live
```

Numbers above are examples of a data model, not research-derived universal values.

## 7. Tempo priors

RoboDrummer should avoid pretending genres have exact BPM boundaries. Instead styles can carry broad soft priors used only when evidence is ambiguous.

Potential implementation:

```text
P(tempo | live evidence, style)
    proportional to
P(live evidence | tempo) * P(tempo | style)
```

The live guitar evidence must dominate once confidence is high.

This allows a style to help resolve 60/120/240 BPM ambiguity without preventing the guitarist from playing outside a typical range.

## 8. Style dimensions worth modeling

### Rock / Hard Rock

Useful parameters:

- strong 2/4 backbeat
- relatively stable pulse
- eighth-note or quarter/eighth cymbal framework
- intensity expressed through kick density, open hats/ride, crashes and larger fills
- section starts often reinforced by crashes

### Blues

Useful parameters:

- straight or shuffle/swing subdivision options
- more tolerance for push/pull and tempo drift in live-jam presets
- restrained fills unless intensity rises
- strong support for repeating phrase cycles

### Funk

Useful parameters:

- higher syncopation
- active kick placement
- ghost-note vocabulary
- tight subdivision grid
- density and syncopation are distinct controls

### Punk

Useful parameters:

- high energy
- relatively straightforward harmonic pulse support
- frequent driving eighths
- simple but forceful fills
- optional double-time interpretation

### Metal

Useful parameters:

- strong subdivision precision
- double-kick vocabulary where applicable
- half/double-time section changes
- high density without destroying beat salience
- tighter tracking defaults than loose blues/garage profiles

### Reggae

Useful parameters:

- distinct kick/snare placement conventions
- sparse groove can be more important than density
- hi-hat subdivision/feel and one-drop/related patterns should be represented as style families rather than generic rock substitutions

### Jazz/Swing

Useful parameters:

- ride-led pulse
- tempo-conditioned swing
- comping vocabulary
- lower reliance on pop/rock backbeat assumptions
- structured microtiming

Jazz should likely be implemented after the core tracker because the required timing behavior is more demanding.

## 9. Song-form behavior model

A section should not just pick a different MIDI groove. It should modify dimensions such as:

```text
INTRO
  low layers
  restrained cymbals
  low fill probability

VERSE
  stable groove
  moderate density
  controlled variation

BUILD
  increasing layers/density
  opening cymbals
  stronger kick activity

CHORUS
  wider instrumentation
  stronger accents
  higher crash probability
  potentially higher density

BREAK
  deliberately remove layers

SOLO_SUPPORT
  preserve strong pulse
  adapt density to leave space

OUTRO
  increasing transition probability
  ending vocabulary enabled
```

The exact mapping must be style-specific.

## 10. Jam-oriented difference from fixed song form

In free-jam mode, the engine should infer tendencies rather than labels with certainty.

Example:

```text
energy increasing for 3 bars
+ repeated phrase ending approaching
+ strong downbeat accent
=> BUILD probability rises
=> fill probability rises
=> crash-on-next-downbeat probability rises
```

No single cue should force a transition.

## 11. User taste as a first-class control

Because style preference strongly influences perceived groove, RoboDrummer should expose:

- Style
- Variation
- Busyness
- Syncopation
- Tightness
- Fill level
- Creativity
- Aggression

Factory presets should cover recognizable behaviors while allowing users to create hybrids.

## 12. Research TODO

Future literature work should add focused notes for:

- reggae rhythm-section analysis
- metal double-kick and subdivision practices
- blues/shuffle timing
- funk ghost-note and syncopation structure
- country drum conventions
- punk tempo/density conventions
- formal studies of drum fills and phrase boundaries

These can refine style files without changing the core architecture.
