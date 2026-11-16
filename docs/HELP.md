# RoboDrummer Help

RoboDrummer is an adaptive virtual drummer for a live guitarist. It listens to your guitar, finds the tempo and the beat, joins on a bar line and plays a drum part that follows your dynamics. This guide is shown inside the plug-in (HELP button) and lives in the repository as docs/HELP.md.

## Quick start
1. On the LIVE tab pick a Style, a Kit and a Drummer.
2. Choose a Timing mode. Drummer Leads: the host tempo, the Internal BPM or MIDI clock is the boss. Hybrid: shared. Guitarist Leads: your guitar sets the tempo.
3. Press LISTEN and play a few bars. RoboDrummer acquires tempo and beat, waits for a reliable beat and joins on a beat boundary.
4. Keep playing. Fills, dynamics and re-entry after silences are automatic.
5. Not sure about your settings? Press LEARN MY PLAYING on the LIVE tab (see Learn My Playing below).

## Routing the guitar
Load RoboDrummer as an instrument and send your guitar to its audio input (the sidechain or input bus, depending on the host). The input is only analysed: the audible guitar is not changed. Guitar Monitor (SETTINGS) passes a share of the input to the output; leave it at 0% when the guitar is on its own track or monitored through your interface. In the standalone application choose your audio interface in the standalone options; mute the input if you monitor through the interface to avoid doubling. See INSTALL.md for host-specific steps.

## Timing modes and following
Drummer Leads keeps the base tempo and only listens for dynamics. Hybrid blends the base tempo and the guitar according to Leadership. Guitarist Leads follows the guitar within Follow range. In both guitar-aware modes the drummer waits for a reliable beat before it joins. The confidence bars on the LIVE tab show how sure the tracker is of tempo, beat and downbeat. Follow response, Follow strength, Tempo inertia, Beat correction speed and Confidence sensitivity on SETTINGS shape how fast and how far the drummer follows.

## Tabs reference
### LIVE
Main controls, tempo, beat and section read-outs, tracker confidence and the performance buttons (LISTEN, FILL, FILL NOW, CRASH, BREAK, HALF-TIME, DOUBLE-TIME, STOP, RESUME, PREV and NEXT SECTION, INTENSITY + and -, SOLO / END). LEARN MY PLAYING and UNDO SETUP are here too.
### KIT
Pick a built-in kit or your own WAV/AIFF folder, set Output mode, Room size and Kit tuning. The mixer has Kick, Snare, Toms, Hi-Hat, Ride, Cymbals, Room and Master with level, pan, mute and solo. The Transient Shaper and the Drum Bus are on this tab (see below).
### DRUMMER
Personality, humanize, feel (push or lay back), complexity, crash tendency and the fill controls. The Riff Lock and Playability controls are here as well.
### STYLE
22 styles, manual meter and Auto meter, Jam memory and the guitar-silence behaviour.
### SONG
A six-section programmed arrangement with a style, length, intensity, half or double time, transition, personality and per-section overrides.
### PRACTICE
Count-in, loop and tempo trainer (see Practice mode below).
### MIDI
Tempo source, note commands with MIDI Learn and controller (CC) mapping with MIDI Learn.
### ANALYSIS
Onsets, predicted beat grid, tempo hypotheses, phase error, confidence history, input level and gate, state transitions, the Riff Lock grid, the limb report, the auto-meter read-out and the Capture panel.
### SETTINGS
Guitar input, gain, noise gate, Guitar Monitor, optional calibration, follow behaviour, MIDI drum output and map, and the Interface options.

## Styles, meter and odd time
The 22 styles are Rock, Blues, Funk, Punk, Metal, Shuffle, Classic Rock, Hard Rock, Alternative, Grunge, Soul, Pop, Indie, Garage Rock, Country, Reggae, Disco, Electronic Rock, Breakbeat, Half-Time, Experimental and Jazz / Swing. Each has a typical tempo range that is only a soft preference. The STYLE tab shows its pattern, swing, looseness and ghost-note tendency. Manual meter overrides the host time signature with Beats per bar and Beat unit (3/4, 6/8, 5/4, 7/8 and others). Auto meter (below) can suggest a meter from your playing when Manual meter is off.

## Fills, intensity and song mode
Fill frequency decides how often fills happen, Fill length is 1 beat to 2 bars, Fill complexity and Fill surprise set the density and the unpredictability. Intensity is the base energy; guitar dynamics, silence and the coordinator adjust it. In the SONG tab, switch on Programmed arrangement and the sections play in order. Each section can lead in with a crash, a fill, a break or nothing, and can auto-advance or wait for NEXT SECTION.

## MIDI
Default note commands arrive on MIDI channel 16 and are consumed, not forwarded:
- 36 Fill (queued), 37 Next section, 38 Previous section, 39 Crash, 40 Intensity up, 41 Intensity down
- 42 Half-time, 43 Double-time, 44 Break, 45 Stop, 46 Resume, 47 Reset listening, 48 Solo support, 49 End jam, 50 Immediate fill
- 51 Practice on/off, 52 Loop on/off, 53 Trainer on/off, 54 Trainer restart, 55 Count-in on/off

Every command can be re-mapped: on the MIDI tab press Learn next to it and play the note. Controllers map to parameters with Learn CC; the defaults are CC11 = Intensity and CC12 = Leadership. Right-click a choice, switch or slider that supports MIDI Learn and choose MIDI Learn to arm it directly. Generated drums are sent on channel 10 with the General MIDI drum map (editable in SETTINGS). Other MIDI normally passes through unchanged. Tempo source can be the host, the Internal BPM or incoming MIDI clock (24 ppqn with Start and Stop). Internal BPM only ignores host tempo, transport and PPQ; the host time signature still applies unless Manual meter overrides it. Dense or oversized MIDI can drop excess pass-through; if safety handling stops the drums, use Resume or restart host transport.

## Kits
Built-in kits are synthesised. For your own kit choose a folder on the KIT tab and name the files kick.wav, snare_v1.wav to snare_v3.wav (velocity layers), snare_v2_rr2.wav (round robin), closedhat, openhat, ride, crash, tom1, tom2 and floortom. WAV and AIFF are read. Press RELOAD after adding or renaming files. Enable the extra output buses (Kick, Snare, Toms, Hats + Cymbals) in your DAW to receive each group separately; disabled buses fall back to the main mix.

## Transient Shaper and Drum Bus
Both live on the KIT tab, are off by default and act only on the internal drums (not on generated MIDI, and not on the room return). The Transient Shaper comes first. Attack (%) makes hits punchier (positive) or softer (negative); Sustain (%) lengthens (positive) or tightens (negative) the decay; Mix (%) blends with the unshaped drums; Output (dB) trims the level. It works the same at any playing level. The text next to the meter shows the gain it is applying right now.

The Drum Bus is a compressor with a parallel mix and a gentle drive. Threshold (dB), Ratio (:1), Knee (dB), Attack (ms), Release (ms) and Makeup (dB) are the usual controls; Detector chooses Peak or RMS; Sidechain HPF (Hz) keeps the kick from pumping the hats (below 20 Hz it is off); Mix (%) blends in the uncompressed drums; Drive (%) adds soft saturation. The Gain reduction read-out shows how many dB of compression are happening.

## Riff Lock and Playability
Riff Lock (DRUMMER tab) lets the kick follow the repeating low-note rhythm of your riff. It only acts when the riff clearly repeats, learns within two to four bars and does nothing on noodling. Riff Lock (%) is the strength (0% is off), Riff Sensitivity (%) decides how soon it locks, Riff crash accents adds a crash on the strongest accent. The ANALYSIS tab draws the learned riff on a grid (taller and outlined bars are slots you hit consistently) and shows the confidence as text, for example "Riff lock: 72% (locked)". The snare is never changed.

Playability (%) removes hits that one drummer with two hands and two feet could not physically play. 0% leaves the groove untouched. ANALYSIS reports what was removed, for example "Limbs: 3 hits removed (2 rate, 1 travel)".

## Practice mode
On the PRACTICE tab switch on Practice session. Count-in plays clicks (Click or Sticks) that end exactly on a bar line; Only on start/resume limits it to the start. Loop repeats 1, 2, 4 or 8 bars, or a range of programmed sections (set the source to Section range). Tempo trainer steps the tempo from Start BPM toward Target BPM by Step (BPM) every N bars or loop passes, going up, down or ping-pong; it can wait until you are in time, mute a rest bar after each step and stop at the target. The status line shows what is happening, for example "Count-in 3/4", "Loop pass 3" or "Trainer 95 BPM". When the timing mode is guitar-led or the tempo comes from the host or MIDI clock, the trainer does not take the tempo over: it says "Trainer: hint only" and shows the tempo to aim for. Restart trainer goes back to the first step. MIDI notes 51 to 55 control the same functions from a pedal.

## Learn My Playing
Press LEARN MY PLAYING on the LIVE tab and play naturally for about ten seconds. A card shows the progress, what to do next in words, and your input level. When it is done a review card lists suggested changes (input gain, gate, follow settings, timing mode, tempo, style, intensity) with the old and new value and the reasons. Tick the ones you want and press Apply; press Cancel (or Escape) to change nothing. After applying, UNDO AUTO SETUP restores the previous values. If it could not hear enough playing it says so and offers Try again. Suggestions never change anything until you press Apply.

## Auto meter
Auto meter (STYLE tab) suggests a time signature from your playing. The read-out reads like "Auto meter: 3/4 (82%)". It is used only when Manual meter is off and the host gives no time signature, and only when its confidence reaches the Auto-meter confidence setting.

## Capture and export
RoboDrummer always records the drums it plays. In the Capture panel on the ANALYSIS tab choose a length (4, 8, 16, 32 bars or All) and either drag the DRAG MIDI handle onto a DAW track or press SAVE .MID... to choose a file. The file carries the tempo and time signature. The status text says what is ready, for example "Ready: 8 bars (16 s)". CLEAR forgets the capture after a second press to confirm. Captures are not saved with your project.

## Presets and sessions
The header loads and saves Jam, Drummer, Style, Kit, Song and Analysis presets. Each type stores only its own slice, so a Kit preset never changes the groove. User presets are kept per user in the Circuit Drift Labs application data folder. The host saves and restores settings with the project; practice runtime switches load off and transient captures/Auto Setup Undo are not restored.

## Controls and gestures
- Drag a slider or knob to change it. Double-click a slider to reset it to its default.
- Right-click any slider, choice or switch for Reset to default, Copy value and Paste value (pasted values are checked and never silently altered). Controls that can be mapped also offer MIDI Learn and Clear MIDI mapping.
- Keyboard: Tab and Shift+Tab move between controls and a focus ring shows where you are. Arrow keys change sliders, Page Up and Page Down take larger steps, Home and End jump to the ends, Backspace or Delete resets, Space toggles a switch, Shift+F10 opens the right-click menu, Ctrl+Tab and Ctrl+Shift+Tab switch tabs, Escape closes Help or cancels Learn My Playing. Some hosts do not pass keys to plug-in windows; everything also works with the mouse.
- Tooltips explain each control. Under SETTINGS, Interface you can switch tooltips off, change their delay, reduce motion (status refreshes 5 times a second and the onset/grid view freezes until UPDATE) and turn on high-contrast outlines. These options are stored per user, not in your project. When no explicit Reduce motion choice is saved, macOS/Windows use the system preference; unavailable Linux preference defaults off.
- The quick tour runs on first launch, can be skipped at any time and can be restarted from HELP or SETTINGS.

## Troubleshooting
- No sound: check the Output mode (Internal, MIDI or both), the Master mute on the KIT tab and that the drummer is not STOPPED on the LIVE tab.
- It does not join: in guitar-aware modes it waits for a reliable beat. Watch the confidence bars, play a clear rhythm for a few bars, or use LEARN MY PLAYING.
- Unstable tempo: check the input level on the ANALYSIS tab, use the noise gate or Calibrate, and raise Tempo inertia.
- Doubled guitar: set Guitar Monitor to 0% when the guitar is already monitored.
- Host gives no tempo: RoboDrummer uses the Internal BPM.
- CPU: the built-in kits are light; the Drum Bus and Transient Shaper add a small fixed cost only while switched on.
- Reporting a problem: open HELP and press COPY DIAGNOSTICS, then paste the text into your report. Nothing is sent anywhere.

## Privacy and licensing
RoboDrummer makes no network connections. It sends no telemetry or analytics and has no licence or update checks. The only links are the ones you click, which open in your browser. Pre-release builds may be unsigned. Circuit Drift Labs: https://djshellshoxxx.github.io/circuitdriftlabs/

## Glossary
- Onset: the start of a played note.
- Lock: the tracker is confident about tempo and beat.
- Hypothesis: a candidate tempo the tracker is considering.
- Phase: where the beat falls compared with where the drummer expects it.
- Lookahead: how far ahead the drummer plans to stay in time.
- Leadership: how much the guitarist rather than the drummer decides the tempo.
