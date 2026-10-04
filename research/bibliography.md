# RoboDrummer Research Bibliography

Date compiled: 2026-10-04

This bibliography is the starting research set for RoboDrummer. It is intentionally biased toward real-time rhythm tracking, ensemble timing, improvisation, groove and drum-pattern structure.

## Improvisation, entrainment and jam interaction

1. Clayton, M., Jakubowski, K., Eerola, T., Keller, P. E., Camurri, A., Volpe, G., & Alborno, P. (2020). **Interpersonal Entrainment in Music Performance.** *Music Perception.*
   - Relevance: separates sensorimotor synchronization from longer-timescale coordination; useful for RoboDrummer's two-layer timing/jam architecture.
   - https://consensus.app/papers/interpersonal-entrainment-in-music-performance-clayton-jakubowski/f401cf31ce6f59a18bc74cfbcaaf075f/

2. Walton, A., Washburn, A., Langland-Hassan, P., Chemero, A., Kloos, H., & Richardson, M. (2017). **Creating time: social collaboration in music improvisation.** *Topics in Cognitive Science*, 10, 95-119.
   - Relevance: multiscale coordination and turn-taking in unscripted musical interaction.
   - https://consensus.app/papers/creating-time-social-collaboration-in-music-walton-washburn/4ab8f5b7b21c5de889eb19ebfd00136a/

3. Goupil, L., Wolf, T., Saint-Germier, P., Aucouturier, J., & Canonne, C. (2021). **Emergent Shared Intentions Support Coordination During Collective Musical Improvisations.** *Cognitive Science*, 45(1).
   - Relevance: shared intentions and communicative strategies can emerge during free improvisation; supports probabilistic transition/intent modeling.
   - https://consensus.app/papers/emergent-shared-intentions-support-coordination-during-goupil-wolf/d255e1453c7b5de6a4a7a6f8224525e6/

4. Setzler, M., & Goldstone, R. L. (2020). **Coordination and Consonance Between Interacting, Improvising Musicians.** *Open Mind: Discoveries in Cognitive Science*, 4, 88-101.
   - Relevance: mutually adaptive improvisers achieve stronger temporal coordination than non-interacting overdub conditions.
   - https://consensus.app/papers/details/b590a233766853cdbd818ea50a9cfb91/

## Tempo stability and expressive timing

5. Condit-Schultz, N., & Clark, B. (2024). **Have we sold our souls to the drum machine? A historical analysis of tempo stability in Western music recordings.** *Musicae Scientiae*, 28, 451-477.
   - Relevance: distinguishes gradual tempo drift and abrupt tempo shifts across a large multi-genre recording corpus.
   - https://consensus.app/papers/have-we-sold-our-souls-to-the-drum-machine-a-historical-condit-schultz-clark/8119a4a462c450c2a3a4d336501853a7/

6. Hofmann, A., Wesolowski, B. C., & Goebl, W. (2017). **The Tight-interlocked Rhythm Section: Production and Perception of Synchronisation in Jazz Trio Performance.** *Journal of New Music Research*, 46, 329-341.
   - Relevance: drummer timing influences tempo; small natural asynchronies can be musically preferred over full quantization.
   - https://consensus.app/papers/the-tightinterlocked-rhythm-section-production-and-hofmann-wesolowski/24591565e80c5ab58c96c4fdce0f08b4/

7. Friberg, A., & Sundström, A. (2002). **Swing Ratios and Ensemble Timing in Jazz Performance: Evidence for a Common Rhythmic Pattern.** *Music Perception*, 19, 333-349.
   - Relevance: swing ratio changes substantially with tempo; supports tempo-conditioned swing rather than a fixed swing percentage.
   - https://consensus.app/papers/swing-ratios-and-ensemble-timing-in-jazz-performance-friberg-sundström/c633e1510a8353a28d89cdacbbf9e063/

## Groove, drum patterns and song form

8. Senn, O., Kilchenmann, L., Bechtold, T., & Hoesl, F. (2018). **Groove in drum patterns as a function of both rhythmic properties and listeners' attitudes.** *PLoS ONE*, 13.
   - Relevance: study of 248 reconstructed popular-music drum patterns; syncopation, event density, familiarity and style preference contribute to groove judgments.
   - https://consensus.app/papers/groove-in-drum-patterns-as-a-function-of-both-rhythmic-senn-kilchenmann/feaa18beafa35fffb142759012052a91/

9. Geary, D. (2024). **Formal Functions of Drum Patterns in Post-Millennial Pop Songs, 2012-2021.** *Music Theory Online.*
   - Relevance: drum patterns express formal boundaries and motion through changes in layers, rhythm and instrumentation.
   - https://consensus.app/papers/formal-functions-of-drum-patterns-in-postmillennial-pop-geary/0781d19b362357a38da695b114f013f2/

10. Bechtold, T., Jerjen, R., Hoesl, F., Kilchenmann, L., & Senn, O. (2026). **The Lucerne Groove Library: An audio stimulus corpus of 444 short Western popular music drum and bass patterns with behavioural, structural, and audio measurements.** *Behavior Research Methods*, 58.
    - Relevance: recent corpus with style assignments, tempo, event density and groove-related measurements; potentially useful for later style-model validation.
    - https://consensus.app/papers/details/49ef6f36b8b35038a7c76acbf6e5e7ee/

## Online beat/downbeat/tempo tracking

11. Heydari, M., Cwitkowitz, F., & Duan, Z. (2021). **BeatNet: CRNN and Particle Filtering for Online Joint Beat, Downbeat and Meter Tracking.** ISMIR / arXiv 2108.03576.
    - Relevance: causal real-time beat/downbeat/meter estimation using CRNN activations and particle filtering.
    - https://consensus.app/papers/beatnet-crnn-and-particle-filtering-for-online-joint-beat-heydari-cwitkowitz/4b84b3e32b54598596c24f4d591264ac/
    - Code: https://github.com/mjhydri/BeatNet

12. Meier, P., Chiu, C.-Y., & Müller, M. (2024). **A Real-Time Beat Tracking System with Zero Latency and Enhanced Controllability.** *Transactions of the International Society for Music Information Retrieval*, 7, 213-227.
    - Relevance: realtime local-pulse tracking and dynamic pulse characteristics.
    - https://consensus.app/papers/details/39c1330559fa543ab0c35158cfd7b5d9/

13. Oliveira, J., Gouyon, F., Martins, L., & Reis, L. P. (2010). **IBT: A Real-time Tempo and Beat Tracking System.**
    - Relevance: causal processing with competing agents/hypotheses; C++ implementation lineage in Marsyas.
    - https://consensus.app/papers/details/2349f5c8108c5074932bd1e6c267ec0b/

14. Allen, P. E., & Dannenberg, R. (1990). **Tracking Musical Beats in Real Time.**
    - Relevance: early explicit argument for multiple simultaneous beat interpretations and predictive realtime following.
    - https://consensus.app/papers/details/6fb938dc29195f71b29e1a65ae45d22c/

15. Chang, C.-C., & Su, L. (2023/2024). **BEAST: Online Joint Beat and Downbeat Tracking Based on Streaming Transformer.** *ICASSP 2024.*
    - Relevance: streaming transformer architecture targeting low-latency online beat/downbeat tracking.
    - https://consensus.app/papers/details/d66f0da563755c8cbb35de515645d936/

16. Heydari, M., & Duan, Z. et al. (2024). **BeatNet+: Real-Time Rhythm Analysis for Diverse Music Audio.** *Transactions of the International Society for Music Information Retrieval*, 7, 274-287.
    - Relevance: extends online rhythm analysis toward signals with weaker percussion and more diverse musical content, directly relevant to isolated guitar challenges.
    - https://consensus.app/papers/details/2ff60739ae6e53e79d1c8b44843097c6/

## Open-source technical references

17. **BeatNet** — https://github.com/mjhydri/BeatNet
    - streaming/realtime/online beat, downbeat, tempo and meter tracking
    - repository declares CC BY 4.0

18. **aubio** — https://github.com/aubio/aubio
    - onset, beat, tempo, pitch and audio feature extraction
    - GPLv3

19. **madmom** — https://github.com/CPJKU/madmom
    - MIR library with beat/downbeat/onset algorithms and online processors
    - source generally BSD; model/data licensing differs and must be reviewed separately

## Research direction

The most relevant unresolved questions for RoboDrummer are:

- how well existing causal beat trackers perform on isolated guitar rather than full mixes
- which guitar features best predict intentional phrase boundaries in a jam
- how quickly the drummer should adapt versus stabilize tempo at different confidence levels
- how style-specific drum-generation rules should change with energy during free improvisation
- which forms of timing variation sound intentionally human versus simply inaccurate

These questions should drive the first experimental harness and recording dataset.
