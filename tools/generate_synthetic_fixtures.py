#!/usr/bin/env python3
"""Generate deterministic original guitar-like PCM for infrastructure tests only.

These are synthesized plucks/strums, never licensed recordings or evidence of
real-guitar tracking quality. Output sidecars and manifest mark that provenance.
"""
import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import random
import sys
import wave


CASES = ("steady", "ramp", "jump", "strum", "palm_muted", "noise", "silence")


def generate(directory, sample_rate=48000, duration=30.0):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    if any(directory.iterdir()): raise ValueError("Synthetic output directory must be empty")
    if sample_rate not in (44100, 48000) or not 1 <= duration <= 90: raise ValueError("Invalid synthetic rate/duration")
    for index, category in enumerate(CASES, 1):
        pcm = array("d", [0.0]) * round(duration * sample_rate)
        beats = []
        t = 0.25
        while t < duration:
            bpm = 100 + 30 * min(max((t - 5) / 20, 0), 1) if category == "ramp" else 140 if category == "jump" and t >= 12 else 120 if category == "palm_muted" else 100
            beats.append(t)
            t += 60 / bpm
        if category in ("noise", "silence"): beats = []
        for beat, t in enumerate(beats):
            strings = (82.41, 110, 146.83, 196, 246.94, 329.63) if category == "strum" else (196.0,)
            for string, frequency in enumerate(strings):
                begin = round((t + string * 0.004) * sample_rate)
                decay = 0.016 if category == "palm_muted" else 0.050
                amplitude = (0.55 if beat % 4 == 0 else 0.35) / len(strings)
                for sample in range(min(round(0.20 * sample_rate), len(pcm) - begin)):
                    dt = sample / sample_rate
                    phase = math.tau * frequency * dt
                    body = (math.sin(phase) + 0.18 * math.sin(phase * 2)) * math.exp(-dt / decay)
                    transient = 0.30 * math.sin(math.tau * 1800 * dt) * math.exp(-dt / 0.004)
                    value = amplitude * (body + transient)
                    if category == "palm_muted": value = math.tanh(value * 2) * 0.6
                    pcm[begin + sample] += value
        if category == "noise":
            rng = random.Random(1729)
            for i in range(len(pcm)): pcm[i] = 0.007 * (2 * rng.random() - 1) + 0.003 * math.sin(math.tau * 60 * i / sample_rate)
        name = f"S{index:02d}_{category}"
        encoded = array("h", (round(max(-1, min(1, value)) * 32767) for value in pcm))
        if sys.byteorder != "little": encoded.byteswap()
        with wave.open(str(directory / (name + ".wav")), "wb") as output:
            output.setparams((1, 2, sample_rate, 0, "NONE", "not compressed"))
            output.writeframes(encoded.tobytes())
        tempo = {"type": "none"} if not beats else {"type": "ramp"} if category == "ramp" else {"type": "step"} if category == "jump" else {"type": "constant", "bpm": 120 if category == "palm_muted" else 100}
        tempo_map = [] if not beats else [{"start_s": 0, "bpm": tempo.get("bpm", 100)}]
        if category == "ramp": tempo_map += [{"start_s": 5, "bpm": 100}, {"start_s": 25, "bpm": 130}]
        elif category == "jump": tempo_map += [{"start_s": 12, "bpm": 140}]
        tempo_map = [p for p in tempo_map if p["start_s"] < duration]
        metadata = {
            "schema": 1, "id": f"S{index:02d}", "file": name + ".wav", "sample_rate": sample_rate,
            "duration_s": len(pcm) / sample_rate, "category": category, "tone": "synthetic_plucked_strings",
            "synthetic": True, "meter": {"num": 4, "den": 4, "segments": [{"start_s": 0, "num": 4, "den": 4}]},
            "tempo": tempo, "tempo_map": tempo_map, "beats_s": beats, "downbeats_s": beats[::4],
            "expected": {"locks": bool(beats), "lock_by_s": 6.0, "tempo_tolerance_bpm": 2.0},
            "annotation": {"method": "deterministic_synthesis", "annotator": "generator", "reviewed_by": "automated", "date": "2026-10-31"},
            "license": {"spdx": "CC0-1.0", "holder": "RoboDrummer synthetic fixture contributors", "source": "original deterministic synthesis"},
            "notes": "SYNTHETIC: exercises ingestion/metrics only; does not qualify real guitar.",
        }
        (directory / (name + ".json")).write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    files = [{"file": path.name, "size_bytes": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
             for path in sorted(directory.iterdir())]
    (directory / "MANIFEST.json").write_text(json.dumps({"schema": 1, "corpus_version": "synthetic-guitar-v1",
                                                           "profile": "synthetic", "files": files}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--sample-rate", type=int, default=48000)
    parser.add_argument("--duration", type=float, default=30.0)
    args = parser.parse_args()
    generate(args.output, args.sample_rate, args.duration)
