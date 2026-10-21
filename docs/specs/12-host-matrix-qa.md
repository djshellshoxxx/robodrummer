# 12. Host matrix QA

## Purpose & user value

Automated suites and pluginval prove the plug-in is well behaved in a synthetic host. They do not prove it behaves in the DAWs users run. This spec defines the manual checklist per host, the validator commands, and the results table that `docs/QA.md` links to. Every cell defaults to **not run**; nothing is claimed until a person ran it and recorded the date, version and build.

## Behavior (precise)

### Hosts and formats

| Host | Platform | Format to test | Notes |
|---|---|---|---|
| Reaper | Windows, macOS, Linux | VST3, CLAP | Primary smoke host; also test the standalone |
| Ableton Live | Windows, macOS | VST3 (Win/mac), AU (mac) | Sidechain input routing via "Audio From" |
| FL Studio | Windows, macOS | VST3, CLAP | Instrument as a generator, guitar via mixer insert sidechain |
| Cubase | Windows, macOS | VST3 | Instrument track plus side-chain input activation |
| Bitwig Studio | Windows, macOS, Linux | VST3, CLAP | Multiple output bus handling |
| Logic Pro | macOS | AU | Software instrument with side chain |
| GarageBand | macOS | AU | Limited hosting: check which items apply (no sidechain, no automation of some params); record "n/a" with reason |
| Studio One | Windows, macOS | VST3, CLAP | |
| Standalone | all | app | Device selection, guitar monitor 100 % default, mute-input note |

### Checklist (run once per host/format/OS)

Prep: install the exact build under test (`18-release-process.md`), clear the host's plug-in cache, note host version, OS version, audio device, sample rate, buffer size.

| # | Item | Pass criteria |
|---|---|---|
| 1 | Scan and load | Plug-in appears as an instrument; loads without error; no scan crash |
| 2 | Editor opens | All 8 tabs render; no blank panel; fonts legible |
| 3 | Resize | Drag editor to min (900x640) and larger; layout follows; no crash; HiDPI / 125 % / 150 % scaling readable |
| 4 | Close and reopen editor | State shown matches the processor; no leak/crash after 10 open/close cycles |
| 5 | Save and recall project | Change style, kit, leadership, intensity, a Song section, a MIDI Learn mapping; save; close; reopen; all restored |
| 6 | Preset round trip | Save and load a user preset; factory Jam presets appear as host programs |
| 7 | Automate | Record automation of at least `intensity`, `jamStyle`, `leadershipMode`; playback moves the GUI and the sound; GUI changes are recorded by the host |
| 8 | Parameter list | Parameters named and grouped sensibly in the host's list; automation lanes of a choice parameter step correctly |
| 9 | Transport | Play/stop/continue: drums start on play (Drummer Leads), silence on stop; host tempo followed; tempo change mid-play followed within one bar |
| 10 | Loop and seek | Loop region and jump to a bar: bar position re-syncs without hung notes; no double triggers |
| 11 | Bounce / offline render | Output identical enough between two renders (same seed) and no NaN; no stuck notes; correct length including 2 s tail |
| 12 | Guitar input | Sidechain/input bus receives the guitar; tracker locks in Guitarist Leads; Guitar Monitor at 0 % does not double the guitar |
| 13 | Multi-output | Enable the four aux buses; each carries its group; hosts that do not support them still work |
| 14 | MIDI in | Command notes on channel 16 act; MIDI Learn learns a note and a CC; other MIDI passes through |
| 15 | MIDI out | Generated drums on channel 10 reach another track/instrument; Output Mode "MIDI only" silences internal audio |
| 16 | Buffer and rate | 64, 256, 1024 samples; 44.1, 48, 96 kHz; no dropouts at <= 50 % host CPU; CPU figure recorded |
| 17 | Plug-in bypass / delete / reinsert | No crash; no hung notes on bypass; reinsert restores state |
| 18 | Multiple instances | Two instances: independent state, no shared-global interference |
| 19 | Unload while playing | Remove the plug-in during playback: no crash |
| 20 | Stability soak | 30 min playing with guitar input: no crash, no growth in memory beyond 20 MB |

### Results table (copy into `docs/QA.md`; "not run" until executed)

Cell values: `PASS`, `FAIL #issue`, `n/a (reason)`, `not run`. Record host version and date in the notes column.

| Host / format / OS | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Reaper VST3 Windows | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | |
| Reaper CLAP Windows | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | |
| Reaper VST3 Linux | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | |
| Ableton Live VST3 Windows | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | |
| Ableton Live AU macOS | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | |
| FL Studio VST3 Windows | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | |
| FL Studio CLAP Windows | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | |
| Cubase VST3 Windows | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | |
| Bitwig VST3 Windows | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | |
| Bitwig CLAP Linux | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | |
| Logic Pro AU macOS | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | |
| GarageBand AU macOS | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | |
| Studio One VST3 Windows | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | |
| Standalone Windows / macOS / Linux | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | not run | |

### Validator commands

pluginval (VST3, strictness 10, the level already used in CI and recorded in QA.md):

```bash
# Linux (needs a display; use xvfb on servers)
xvfb-run -a ./pluginval --strictness-level 10 --validate-in-process --timeout-ms 600000 --output-dir pluginval-report path/to/RoboDrummer.vst3
# Windows (PowerShell)
./pluginval.exe --strictness-level 10 --validate-in-process --timeout-ms 600000 --output-dir pluginval-report "C:\Program Files\Common Files\VST3\RoboDrummer.vst3"
# macOS
/Applications/pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --validate-in-process --timeout-ms 600000 --output-dir pluginval-report ~/Library/Audio/Plug-Ins/VST3/RoboDrummer.vst3
```

auval (AU, macOS only). The plug-in is an instrument; manufacturer code `CDrf`, plug-in code `RDrm` (`CMakeLists.txt`):

```bash
killall -9 AudioComponentRegistrar 2>/dev/null; auval -a | grep -i robodrummer
auval -v aumf RDrm CDrf
```

(`CMakeLists.txt` sets `AU_MAIN_TYPE kAudioUnitType_MusicEffect`, so the type is `aumf` (MIDI-controlled effect, which is how a plug-in with a guitar audio input and MIDI is classed). Confirm with `auval -a`; if the AU type is later changed to a music device the type becomes `aumu`. Logic lists `aumf` plug-ins under "Audio FX"/MIDI-controlled effects, not Instruments; record where each host exposes it. Record the full output tail "AU VALIDATION SUCCEEDED" in the QA record. An `auval` failure is a release blocker for the AU format only.)

CLAP: no mandatory validator; run `clap-validator validate path/to/RoboDrummer.clap` (free-audio/clap-validator) and record the result, non-blocking.

### Failure handling

* Every FAIL gets a GitHub issue with host/version/OS, steps, and a project file if possible.
* A host-specific workaround is allowed only if it cannot affect other hosts; otherwise fix at the root.
* Any crash, hung note, or state loss in a primary host (Reaper, Ableton, FL Studio, Cubase, Bitwig on Windows) blocks the beta-to-release step (`18-release-process.md`).

## Parameters

None.

## Actions/MIDI mapping

None.

## State serialization

None. Results are recorded in `docs/QA.md` under "Manual / host checks" (replace the single "not run" row with this table at the first run).

## UI placement & labels & tooltips

None.

## Threading/real-time

Item 16 and 20 record CPU: use the host's meter and, where available, the harness `testCpuBudget` line for comparison. The worst-case block load measured here feeds the revisit criteria in `10-analysis-worker-thread.md`.

## Test plan

Manual, per the checklist. A second person repeats items 5, 7, 9 and 11 on the primary hosts before release.

## Acceptance criteria

* Beta: pluginval strictness 10 PASS on Windows and Linux (CI) and at least Reaper (one OS) and one more host with items 1 to 11 PASS.
* Release: all five primary hosts on Windows pass items 1 to 15 and 17 to 19; one macOS host passes with VST3 and AU, `auval` passes; Linux Reaper and Bitwig pass 1 to 11; GarageBand/Logic recorded (PASS or documented n/a).

## CDL compliance notes

Supports CDL 3.3 (formats, buses, transport) and 4.1 (host state). Results satisfy the baseline's host verification expectation; any "n/a" must state why.

## Integration hook points

`docs/QA.md` "Manual / host checks" table; `.github/workflows/release-build.yml` already uploads `pluginval-*` artifacts; add an `auval` step to the macOS job (see `13-code-signing-notarization.md`, after signing).
