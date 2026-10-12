# RoboDrummer user kit format

A user kit is a folder of WAV or AIFF files, optionally with a `kit.txt`.

## File names

`<instrument>[_v<1-4>][_rr<1-4>].wav|aif|aiff` (case-insensitive)

Instruments: `kick, snare, closedhat, openhat, ride, crash, hightom, midtom, floortom,
rimshot, sidestick, china, splash, cowbell, tambourine, clap`.

- `_v1.._v4` velocity layers. The layer numbers present are split into equal velocity ranges,
  ascending (two layers: 0-0.5 and 0.5-1). No `_v` tag = a single layer.
- `_rr1.._rr4` round-robin variants, played in ascending order.
- An untagged file counts as `_v1` / `_rr1`; a tagged duplicate of the same slot is rejected.
- Instruments missing from the folder keep the built-in starter sounds.

## kit.txt (optional)

One instrument per line, `#` starts a comment, all keys optional:

    snare tune=-1.5 gain=-3 pan=0.1 choke=0

| key   | range            | default                         |
|-------|------------------|---------------------------------|
| tune  | -24..24 semitones | 0                              |
| gain  | -60..12 dB       | 0                               |
| pan   | -1..1            | 0                               |
| choke | 0..8 (0 = none)  | 1 for closedhat/openhat, else 0 |

Invalid lines are skipped and reported.

## Limits and conversion

- Formats: WAV, AIFF; mono or stereo. Other extensions/files are ignored.
- At most 20 seconds per sample; at most 64 MB per kit (decoded, at plugin rate, stereo float).
- Files are resampled to the plugin sample rate; mono is duplicated to stereo.
- Corrupt, truncated, unsupported, too long, misnamed or over-budget files are skipped and listed
  in the error list (`KitLoader::errors()`); the rest of the kit still loads. If nothing is usable
  the load fails and the current/starter kit stays.
- Host state stores only the kit folder path; if it is missing at restore, the starter kit is used.

## Threading and handoff

`KitLoader::loadAsync(folder, sampleRate, callback)` decodes on a background thread and calls back
on the message thread with a `KitLoadResult`. Never load on the audio thread.

    // message thread
    loader.loadAsync(folder, sr, [&](robodrummer::KitLoadResult r) {
        if (r.kit) handoff.publish(std::move(r.kit));   // else keep current kit, show r.errors
    });
    handoff.collectGarbage();                            // from a timer

    // audio thread, start of processBlock
    if (auto* k = handoff.acquire()) samplePlayer.useKit(k);

`KitHandoff` (src/audio/DrumKit.h) is a single-slot lock-free swap: publish() atomically exchanges
the pending pointer (freeing a superseded unacquired kit on the message thread); acquire() is
wait-free and retires the previous kit to a slot that the message thread deletes. The handoff owns
all kits; the audio thread's raw pointer is valid until the next acquire() returning non-null.
`useKit` stops voices still reading the old kit. Publishing an empty `DrumKit` (or `nullptr`)
reverts to the starter kit.
