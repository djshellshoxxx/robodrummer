# 05 - MIDI Capture & Export

Status: Implemented (core, processor wiring and editor UI)

## Purpose & user value
The drummer plays something great; the user wants it in their DAW as editable MIDI. The plug-in continuously records what it
generates (always on, zero setup). The user presses "Capture last N bars" and drags a Standard MIDI File out of the editor into a
DAW track, or saves it. The file carries tempo and meter, so it lands correctly in any project.

## Behavior (precise)
**Timeline.** Positions are integer ticks at PPQ 960 (`kMidiPpq`) on the host's musical timeline of the current *take*
(host PPQ x 960, rounded). Event tick = `blockStartTick + round(sampleOffset * ticksPerSample)`, with
`ticksPerSample = 960 * bpm / 60 / sampleRate`. `sampleOffset` is the final `DrumEvent::sampleOffset` (after humanization), so
humanization offsets are preserved exactly; the recorder never quantizes.

**Takes.** A take is a contiguous run with a continuous timeline. A new take starts when: first block / after `requestClear()`,
transport stop then start, host position jumps (|actual - expected| > 480 ticks + block length: seek or loop wrap), or ring overflow
(a record was dropped). Tempo and meter changes do NOT start a take; they go into the take's tempo map / meter map. Up to 8 takes are
kept (oldest dropped). Export uses ONE take (default newest non-empty).

**Records** (audio thread -> ring, 24 bytes): Segment(origin tick, ticks-into-bar, 24-bit producer take sequence), Tempo(tick, bpm), Meter(tick, num, den),
Note(tick, instrument, velocity 1..127, duration ticks). A hit is ONE record; the note-off is derived by the writer. Default duration
120 ticks (a 32nd). The writer shortens a note to end before the next same-pitch onset.

**Storage.** Audio thread -> lock-free SPSC ring (`MidiCapture<RingCapacity = 8192>`, fixed array, no allocation). Message thread
`drain()` (editor timer ~30 Hz and before every export) moves records into per-take `std::vector` history.
*Overflow policy:* if the ring is full the NEWEST record is dropped (producer never blocks or overwrites), `droppedCount()`
increments, and the producer starts a fresh take on the next block so tempo/meter maps stay consistent.
*History cap:* `kMaxCaptureSeconds = 600` (musical time through the tempo map) and `kMaxNotesPerTake = 131072`. Oldest material is
dropped at a bar line; the tempo/meter in force is carried to the new start; `truncated` is reported.

**Bars.** Grid anchored at `origin - ticksIntoBar`; a meter change restarts the grid at its tick. Bar length = `num * 3840 / den`.
Request `bars` in {4, 8, 16, 32} or 0 = all-since-start (whole take within the cap). Default `completedBarsOnly = true`: the window
ends at the last full bar line <= latest position (falls back to including the partial bar if none is complete). Fewer bars than
requested exports what exists. Exported length is whole bars; notes crossing the end are clipped.

**SMF.** Format 1, two tracks, division 960, running status never used. Track 0: Track Name "RoboDrummer Tempo Map", Time Signature(s),
Set Tempo(s) (always one of each at tick 0, defaulting to 120 / 4/4), Text "RoboDrummer capture". Track 1: Track Name
"RoboDrummer Drums", Instrument Name "Drums", Text "Drum map: 36=Kick, 38=Snare, ...", notes on channel 10 (0x99/0x89). Same tick:
note-offs before note-ons, then ascending pitch. Velocity clamped 1..127 (never a note-on with velocity 0); note-off velocity 64.
Both tracks end with End-of-Track at/after the length. Mapping: `DrumMap::gm()` (identical to `midiNoteFor`) or any user `DrumMap`
(-1 = skip instrument). Non-finite or <= 0 tempo => writer returns `InvalidTempo` (capture ignores such inputs); finite tempo is
clamped to [4, 960] bpm. Negative tick or delta > 0x0FFFFFFF => `TickOutOfRange`.

## Parameters
None (capture is always on). UI-only value `captureBars` (0/4/8/16/32).

## Actions/MIDI mapping
None required.

## State serialization
Captured data is transient and NOT saved. Optional UI value `captureBars` (int, default 8); absence means default, no version bump.

## UI placement & labels & tooltips
Panel "Capture" (Output area):
- Choice "Length": 4 bars / 8 bars / 16 bars / 32 bars / All (max 10 min). Tooltip: "How much of the recent performance to export."
- Drag handle "Drag MIDI": press and drag onto a DAW track. Tooltip: "Drag the last N bars out as a MIDI file."
- Button "Save MIDI...": async FileChooser. Tooltip: "Save the last N bars as a .mid file."
- Status text (readable by screen readers, not color-only): "Ready: 8 bars (16 s)", "Nothing captured yet", "Capture full, oldest bars dropped".
All controls focusable; "Save MIDI..." is the keyboard alternative to dragging. Empty state is explained in text, not only dimmed.

## Threading/real-time
Audio thread calls only `onBlock` and `noteAtSample` (noexcept, allocation-free, lock-free, NaN-safe). Message thread: `drain`,
`buildExport`, `exportSmf`, `requestClear`, stats. SPSC contract: one producer thread, one consumer thread. Take history is
message-thread-only. `requestClear` and the latest-position value use atomics. Segment records carry the producer take sequence explicitly, so clearing undrained records cannot desynchronize the consumer from the packed latest-position sequence. Velocity is clamped in floating point before rounding to MIDI 1..127; non-finite velocity uses 64.

## Test plan
`test_midi_file_writer`: round trip through an in-test SMF parser (VLQ, no running status, EOT, deltas, off-before-on, tempo and
time-signature meta), tempo map, odd meters, empty capture, huge ticks, NaN/Inf/zero tempo rejected, velocity clamp, same-pitch
overlap, custom map skip, exact non-grid (humanized) timing.
`test_midi_capture`: SPSC two-thread stress, ring wrap-around, overflow + new take, take segmentation (stop/start, seek, loop),
tempo-change map, meter change restarts grid, last-N-bars (completed vs partial), all-since-start, 10-minute cap, humanization
offsets preserved, NaN inputs, empty capture, end-to-end capture -> SMF -> parse.

## Acceptance criteria
All tests pass with -UNDEBUG; audio-thread functions use only fixed storage (1M-event soak test); exported ticks equal recorded ticks
exactly; file opens in a DAW with correct tempo and meter.

## CDL compliance notes
Visible labels with units, textual status, keyboard alternative to drag, no color-only state, tooltips. Failures are shown as text
("Could not create MIDI file"). No network, no personal data, temp files removed per the rules below.

## Integration hook points
Processor member: `robodrummer::MidiCapture<> capture_;`
1. `processBlock`, once per block before the event loop (audio thread), after the clock/tempo/meter for the block are known:
   `capture_.onBlock(CaptureBlock{hostPpqValid, ppq, playing, bpm, numerator, denominator, numSamples, sampleRate});`
   Without a host PPQ pass `hostPpqValid=false`; the recorder free-runs while `playing` (standalone: `playing=true`).
2. In the event loop, for each event whose `midiNoteFor(...) >= 0`, regardless of output mode (internal-audio-only records too):
   `capture_.noteAtSample(events[i].sampleOffset, events[i].instrument, events[i].velocity);`
3. Editor timer (message thread): `processor.capture().drain();` then `summary()` for the status text.
4. Buttons: `capture_.exportSmf(request, bytes)`, then write bytes to the drag temp file or the chosen file.
5. Optional `capture_.requestClear()` on reset/style load. CMake: add `midi_capture` and `midi_file_writer` to the test list
   (this branch lists tests explicitly).

### Drag-and-drop from editor (spec only)
- Editor inherits `juce::DragAndDropContainer` (or use `DragAndDropContainer::findParentDragContainerFor(comp)`).
- `CaptureDragHandle : juce::Component`, in `mouseDrag`, when `e.getDistanceFromDragStart() > 6` and no drag is active:
  1. `capture.drain(); exportSmf(...)` on the message thread (reuse the cached file if `capture.version()` and request are unchanged).
  2. Write `File::getSpecialLocation(tempDirectory)/RoboDrummer/Capture/<instanceId>/RoboDrummer_<style>_<bpm>bpm_<N>bars.mid`
     (`getNonexistentChildFile`, name sanitized to `[A-Za-z0-9_-]`, `createDirectory()`, `replaceWithData`). Never on the audio thread.
  3. `container->performExternalDragDropOfFiles({file.getFullPathName()}, /*canMoveFiles*/ false, this, [this, file]{ scheduleCleanup(file); });`
  4. If the capture is empty, the write fails, or the host/OS refuses the external drag, show the status text; "Save MIDI..." remains the fallback.
- Cleanup: never delete inside the drop callback (the target may copy asynchronously). Delete the file with a one-shot `juce::Timer`
  5 minutes after the callback; on editor construction sweep `<temp>/RoboDrummer/Capture/*` removing files older than 24 h and empty
  instance dirs (best effort, ignore errors); on processor destruction delete this instance's files older than 5 minutes. Keep at most
  20 files per instance (oldest first). Drag files never go outside the temp directory.
