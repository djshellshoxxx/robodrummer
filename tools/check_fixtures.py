#!/usr/bin/env python3
"""Validate an offline licensed/synthetic corpus and measure the production tracker.

Manifest schema 1: corpus_version, profile (full/smoke/synthetic), files containing
file/size_bytes/sha256 for every WAV and sidecar. No network requests are made.
"""
import argparse
import bisect
import csv
from datetime import date
import hashlib
import io
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import statistics
import subprocess
import sys
import wave

ALLOWED_LICENSES = {"CC0-1.0", "CC-BY-3.0", "CC-BY-4.0", "CC-BY-SA-3.0", "CC-BY-SA-4.0"}
MAX_CORPUS_BYTES = 400 * 1024 * 1024
MAX_ANNOTATIONS = 4096  # >600 quarter-note beats in 90 s at the 400 BPM maximum


class FixtureError(ValueError): pass


def require(condition, message):
    if not condition: raise FixtureError(message)


def number(value):
    # JSON integers are arbitrary precision. Check representability before
    # math.isfinite attempts a float conversion that could itself overflow.
    return (isinstance(value, (int, float)) and not isinstance(value, bool)
            and abs(value) <= sys.float_info.max and math.isfinite(value))


def read_json(path):
    require(path.stat().st_size <= 2 * 1024 * 1024, f"JSON too large: {path.name}")
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, f"Duplicate JSON key: {key}")
            result[key] = value
        return result
    def invalid(value): raise FixtureError(f"Nonfinite JSON number: {value}")
    try:
        result = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique, parse_constant=invalid)
        require(isinstance(result, dict), f"Expected JSON object: {path.name}")
        return result
    except FixtureError:
        raise
    except (UnicodeError, ValueError, RecursionError) as error:
        raise FixtureError(f"Invalid JSON {path.name}: {error}") from error


def safe_file(directory, name):
    require(isinstance(name, str) and name and "\\" not in name, "Invalid manifest path")
    try: name.encode("utf-8")
    except UnicodeError as error: raise FixtureError("Invalid Unicode fixture path") from error
    relative = PurePosixPath(name)
    require(not relative.is_absolute() and ".." not in relative.parts and ":" not in name, f"Unsafe path: {name}")
    candidate = directory
    for part in relative.parts:
        candidate /= part
        require(not candidate.is_symlink(), f"Symlink not permitted: {name}")
    require(candidate.is_file(), f"Missing fixture file: {name}")
    return candidate


def checksum(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""): digest.update(block)
    return digest.hexdigest()


def validate_clip(directory, path, listed, profile):
    clip = read_json(path)
    require(type(clip.get("schema")) is int and clip["schema"] == 1, f"Unsupported sidecar schema: {path.name}")
    require(isinstance(clip.get("id"), str) and re.fullmatch(r"[A-Z][0-9]{2}", clip["id"]), "Invalid clip id")
    synthetic = clip.get("synthetic", False)
    require(isinstance(synthetic, bool), "synthetic must be a boolean")
    require(synthetic == (profile == "synthetic"), "Synthetic provenance disagrees with manifest profile")
    require(isinstance(clip.get("file"), str) and clip["file"] in listed, "WAV absent from manifest")
    wav = safe_file(directory, clip["file"])
    require(wav.suffix.lower() == ".wav", "Fixture audio must be PCM WAV")
    require(path.with_suffix(".wav") == wav, "Sidecar must share its WAV basename")
    for key in ("category", "tone"):
        require(isinstance(clip.get(key), str) and bool(clip[key].strip()), f"Missing {key}")
    license_info = clip.get("license", {})
    require(isinstance(license_info, dict) and isinstance(license_info.get("spdx"), str)
            and license_info["spdx"] in ALLOWED_LICENSES, "Missing or unsupported redistribution license")
    for key in ("holder", "source"):
        require(isinstance(license_info.get(key), str) and bool(license_info[key].strip()), f"Missing license {key}")
    annotation = clip.get("annotation", {})
    require(isinstance(annotation, dict), "Missing annotation")
    for key in ("method", "annotator", "reviewed_by", "date"):
        require(isinstance(annotation.get(key), str) and bool(annotation[key].strip()), f"Missing annotation {key}")
    require(re.fullmatch(r"\d{4}-\d{2}-\d{2}", annotation["date"]), "Invalid annotation date")
    try: date.fromisoformat(annotation["date"])
    except ValueError as error: raise FixtureError("Invalid annotation date") from error
    duration = clip.get("duration_s")
    require(number(duration) and 0 < duration <= 90 and (synthetic or duration >= 30), "Invalid duration_s")
    try:
        with wave.open(str(wav), "rb") as audio:
            rate, frames = audio.getframerate(), audio.getnframes()
            require(audio.getnchannels() == 1 and audio.getsampwidth() in (2, 3) and audio.getcomptype() == "NONE", "Expected mono 16/24-bit PCM")
            require(profile != "full" or audio.getsampwidth() == 3, "Full corpus requires 24-bit PCM; 16-bit belongs to smoke/synthetic")
            require(rate in (44100, 48000) and clip.get("sample_rate") == rate, "WAV sample rate disagrees with metadata")
            require(abs(frames / rate - duration) <= 1.5 / rate, "WAV duration disagrees with metadata")
            require(len(audio.readframes(frames)) == frames * audio.getsampwidth(), "Truncated PCM data")
    except (wave.Error, EOFError) as error:
        raise FixtureError(f"Malformed PCM WAV: {wav.name}: {error}") from error
    for key in ("beats_s", "downbeats_s"):
        values = clip.get(key)
        require(isinstance(values, list) and len(values) <= MAX_ANNOTATIONS and all(number(v) and 0 <= v < duration for v in values), f"Invalid or excessive {key} annotations")
        require(all(a < b for a, b in zip(values, values[1:])), f"Unordered/duplicate {key}")
    for downbeat in clip["downbeats_s"]:
        at = bisect.bisect_left(clip["beats_s"], downbeat)
        neighbors = clip["beats_s"][max(0, at - 1):at + 1]
        require(any(abs(downbeat - beat) <= 0.001 for beat in neighbors), "Downbeat absent from beat annotations")
    meter = clip.get("meter", {})
    require(isinstance(meter, dict), "Invalid meter")
    segments = meter.get("segments")
    require(isinstance(segments, list) and 1 <= len(segments) <= 256, "Missing meter segments")
    previous = -1
    for segment in segments:
        require(isinstance(segment, dict) and type(segment.get("num")) is int and 2 <= segment["num"] <= 12 and type(segment.get("den")) is int and segment["den"] in (1, 2, 4, 8, 16), "Invalid meter segment")
        start = segment.get("start_s")
        require(number(start) and previous < start < duration, "Invalid meter segment time")
        previous = start
    require(segments[0]["start_s"] == 0 and meter.get("num") == segments[0]["num"] and meter.get("den") == segments[0]["den"], "Meter origin disagrees with segments")
    tempo = clip.get("tempo", {})
    require(isinstance(tempo, dict) and tempo.get("type") in ("constant", "ramp", "step", "none"), "Invalid tempo type")
    tempo_map = clip.get("tempo_map")
    require(isinstance(tempo_map, list) and len(tempo_map) <= 256, "Invalid tempo map")
    if tempo["type"] != "none":
        require(bool(tempo_map) and isinstance(tempo_map[0], dict) and tempo_map[0].get("start_s") == 0, "Missing tempo origin")
        previous = -1
        for index, segment in enumerate(tempo_map):
            require(isinstance(segment, dict) and number(segment.get("bpm")) and 20 <= segment["bpm"] <= 400, "Invalid tempo BPM")
            start = segment.get("start_s")
            endpoint = tempo["type"] == "ramp" and index == len(tempo_map) - 1 and start == duration
            require(number(start) and previous < start and (start < duration or endpoint), "Invalid tempo map time")
            previous = start
        if tempo["type"] == "constant":
            require(len(tempo_map) == 1 and tempo.get("bpm") == tempo_map[0]["bpm"], "Constant tempo disagrees with map")
    else:
        require(not tempo_map, "Unpitched/rubato tempo none requires an empty map")
    expected = clip.get("expected", {})
    require(isinstance(expected, dict) and type(expected.get("locks")) is bool, "Missing expected locks")
    if "max_false_lock_s" in expected:
        require(number(expected["max_false_lock_s"]) and expected["max_false_lock_s"] >= 0, "Invalid false-lock limit")
    if expected["locks"]:
        require(tempo["type"] != "none" and bool(clip["beats_s"]), "Locking clip requires tempo and beat truth")
        for key in ("lock_by_s", "tempo_tolerance_bpm"):
            require(number(expected.get(key)) and expected[key] > 0, f"Invalid expected {key}")
    clip["synthetic"] = synthetic
    return clip


def validate_corpus(directory, required=False, require_full=False):
    directory = Path(directory)
    if not directory.exists():
        require(not (required or require_full), f"required corpus missing: {directory}")
        return None
    require(directory.is_dir(), "Corpus must be a directory")
    manifest_path = safe_file(directory, "MANIFEST.json")
    manifest = read_json(manifest_path)
    require(type(manifest.get("schema")) is int and manifest["schema"] == 1, "Unsupported manifest schema")
    require(isinstance(manifest.get("corpus_version"), str) and bool(manifest["corpus_version"]), "Missing corpus version")
    profile = manifest.get("profile")
    require(profile in ("synthetic", "smoke", "full"), "Invalid corpus profile")
    require(not require_full or profile == "full", "Full real corpus required; synthetic/smoke cannot qualify")
    files = manifest.get("files")
    require(isinstance(files, list) and 0 < len(files) <= 256, "Invalid manifest files")
    listed, total = {}, 0
    for entry in files:
        require(isinstance(entry, dict), "Invalid manifest entry")
        name = entry.get("file")
        path = safe_file(directory, name)
        require(name not in listed and name != "MANIFEST.json", "Duplicate/self-referential manifest file")
        require(type(entry.get("size_bytes")) is int and entry["size_bytes"] == path.stat().st_size, f"File size mismatch: {name}")
        total += entry["size_bytes"]
        require(total <= MAX_CORPUS_BYTES, "Corpus exceeds 400 MiB budget")
        require(isinstance(entry.get("sha256"), str) and re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]), "Invalid SHA-256")
        require(checksum(path) == entry["sha256"], f"SHA-256 mismatch: {name}")
        listed[name] = path
    actual = {p.relative_to(directory).as_posix() for p in directory.rglob("*") if p.suffix.lower() in (".wav", ".json") and p.name != "MANIFEST.json"}
    relevant = {name for name in listed if Path(name).suffix.lower() in (".wav", ".json")}
    require(actual == relevant, "Unlisted corpus WAV/JSON or missing manifest entry")
    clips = [validate_clip(directory, path, listed, profile) for name, path in sorted(listed.items()) if path.suffix.lower() == ".json"]
    require(bool(clips) and len({(c["id"], c["sample_rate"]) for c in clips}) == len(clips), "No clips or duplicate clip ID/rate")
    require(all(len({c["expected"]["locks"] for c in clips if c["id"] == clip["id"]}) == 1 for clip in clips), "Rate variants disagree about expected locking")
    require({c["file"] for c in clips} == {n for n in listed if Path(n).suffix.lower() == ".wav"}, "WAV without exactly one sidecar")
    if profile == "full":
        require(len({c["id"] for c in clips}) >= 20 and all(re.fullmatch(r"C(0[1-9]|1[0-9]|2[0-4])", c["id"]) for c in clips), "Full corpus requires at least 20 distinct real C01-C24 clips")
        require(any(c["id"] == "C18" and not c["expected"]["locks"] for c in clips), "Full corpus requires the C18 rubato negative control")
        require(all(c["annotation"]["annotator"] != c["annotation"]["reviewed_by"] for c in clips if c["id"] in ("C12", "C13", "C16", "C18")), "Selected real clips require two annotators")
        require((directory / "LICENSES.md").is_file(), "Full corpus LICENSES.md missing")
        require(full_rate_coverage(clips), "Full corpus requires 48 kHz primaries and 44.1 kHz variants for at least three distinct clips")
    return {"directory": directory, "profile": profile, "corpus_version": manifest["corpus_version"],
            "manifest_sha256": checksum(manifest_path), "clips": clips}


def full_rate_coverage(clips):
    ids = {clip["id"] for clip in clips}
    primary = {clip["id"] for clip in clips if clip["sample_rate"] == 48000}
    variants = {clip["id"] for clip in clips if clip["sample_rate"] == 44100}
    return ids == primary and len(variants) >= 3


def target_bpm(clip, seconds):
    if clip["tempo"]["type"] == "none": return None
    points = clip["tempo_map"]
    index = max(0, bisect.bisect_right([p["start_s"] for p in points], seconds) - 1)
    current = points[index]
    if clip["tempo"]["type"] == "ramp" and index + 1 < len(points):
        following = points[index + 1]
        fraction = (seconds - current["start_s"]) / (following["start_s"] - current["start_s"])
        return current["bpm"] + fraction * (following["bpm"] - current["bpm"])
    return current["bpm"]


def lock_time(trace, clip, start, end):
    since = None
    for row in trace:
        t = row["time_s"]
        if not start <= t < end: continue
        good = row["locked"] and abs(row["bpm"] - target_bpm(clip, t)) <= min(2.0, clip["expected"]["tempo_tolerance_bpm"])
        if not good: since = None
        elif since is None: since = t
        elif t - since >= 2.0 - 1e-9: return since - start
    return None


def measure(clip, trace, predicted_beats):
    require(bool(trace), "Empty tracker trace")
    require(all(number(row[k]) for row in trace for k in ("time_s", "bpm", "confidence")), "Nonfinite tracker output")
    require(all(r["bpm"] > 0 and 0 <= r["confidence"] <= 1 for r in trace), "Out-of-range tracker output")
    require(all(a["time_s"] < b["time_s"] for a, b in zip(trace, trace[1:])), "Unordered tracker trace")
    require(all(number(beat) and 0 <= beat <= clip["duration_s"] for beat in predicted_beats), "Invalid predicted beat time")
    matched, offsets, i, j = 0, [], 0, 0
    truth, predicted = clip["beats_s"], sorted(predicted_beats)
    while i < len(truth) and j < len(predicted):
        delta = predicted[j] - truth[i]
        if abs(delta) <= 0.070 + 1e-9:
            matched += 1; offsets.append(abs(delta)); i += 1; j += 1
        elif delta < 0: j += 1
        else: i += 1
    f_measure = 2 * matched / (len(truth) + len(predicted)) if truth or predicted else 0.0
    longest, since = 0.0, None
    for row in trace:
        if row["locked"]:
            if since is None: since = row["time_s"]
            longest = max(longest, row["time_s"] - since)
        else: since = None
    # Phase error includes unmatched predictions too; restricting it to the
    # 70 ms matching window would conceal the largest timing errors.
    phase_errors = []
    if truth:
        for beat in predicted:
            at = bisect.bisect_left(truth, beat)
            neighbors = truth[max(0, at - 1):at + 1]
            phase_errors.append(min(abs(beat - value) for value in neighbors))
    result = {"final_bpm": trace[-1]["bpm"], "expected_bpm_80_percent": target_bpm(clip, clip["duration_s"] * 0.8), "beat_f_measure": f_measure,
              "median_phase_error_s": statistics.median(phase_errors) if phase_errors else None,
              "false_lock": not clip["expected"]["locks"] and any(r["locked"] for r in trace),
              "max_false_lock_s": longest if not clip["expected"]["locks"] else 0.0,
              "tempo_error_bpm": None, "folded_tempo_error_bpm": None, "octave_relation": "none",
              "time_to_lock_s": None, "segments": []}
    if clip["expected"]["locks"]:
        points = clip["tempo_map"]
        starts = [p["start_s"] for p in points] if clip["tempo"]["type"] == "step" else [0.0]
        for start, end in zip(starts, starts[1:] + [clip["duration_s"]]):
            rows = [r for r in trace if start + 0.8 * (end - start) <= r["time_s"] <= end]
            require(bool(rows), "Missing end-of-segment measurements")
            error = statistics.median(abs(r["bpm"] - target_bpm(clip, r["time_s"])) for r in rows)
            result["segments"].append({"start_s": start, "tempo_error_bpm": error, "time_to_lock_s": lock_time(trace, clip, start, end)})
        result["tempo_error_bpm"] = max(s["tempo_error_bpm"] for s in result["segments"])
        result["time_to_lock_s"] = result["segments"][0]["time_to_lock_s"]
        row = min(trace, key=lambda r: abs(r["time_s"] - clip["duration_s"] * 0.8))
        target = target_bpm(clip, row["time_s"])
        result["folded_tempo_error_bpm"] = min(abs(row["bpm"] * factor - target) for factor in (0.5, 1, 2))
        if abs(row["bpm"] / target - 2) < 0.08: result["octave_relation"] = "double"
        elif abs(row["bpm"] / target - 0.5) < 0.02: result["octave_relation"] = "half"
        result["accepted"] = all(s["tempo_error_bpm"] < 2.0 and s["time_to_lock_s"] is not None and s["time_to_lock_s"] <= min(6.0, clip["expected"]["lock_by_s"]) for s in result["segments"])
    else:
        result["accepted"] = longest <= min(5.0, clip["expected"].get("max_false_lock_s", 5.0))
    return result


def run_clip(runner, corpus, clip, block_size):
    args = [str(Path(runner).resolve()), str((corpus["directory"] / clip["file"]).resolve()), str(block_size)]
    for segment in clip["meter"]["segments"]: args += [str(segment["start_s"]), str(segment["num"])]
    run = subprocess.run(args, text=True, capture_output=True, timeout=120, check=False)
    require(run.returncode == 0, f"Tracker runner failed: {run.stderr.strip()}")
    trace, beats = [], []
    try:
        for row in csv.DictReader(io.StringIO(run.stdout)):
            trace.append({"time_s": float(row["time_s"]), "bpm": float(row["bpm"]), "confidence": float(row["confidence"]), "locked": row["locked"] == "1"})
            if row["predicted_beat_s"]: beats.append(float(row["predicted_beat_s"]))
    except (ValueError, KeyError) as error: raise FixtureError("Malformed tracker trace") from error
    return {"id": clip["id"], "file": clip["file"], "category": clip["category"], "block_size": block_size, "sample_rate": clip["sample_rate"],
            "synthetic": clip["synthetic"], **measure(clip, trace, beats)}


def qualifies_full(corpus, results):
    # Qualification is per clip, not per block-size result: one good rate of
    # processing cannot hide failures for that same clip at another block size.
    locking = {c["id"]: c for c in corpus["clips"] if c["expected"]["locks"] and c["id"] not in ("C18", "C24")}
    def passed(clip):
        for variant in (c for c in corpus["clips"] if c["id"] == clip["id"]):
            rows = [r for r in results if r["id"] == clip["id"] and r.get("file") == variant.get("file")]
            if {r["block_size"] for r in rows} != {64, 512, 2048} or not all(r["accepted"] for r in rows):
                return False
        return True
    return bool(locking) and sum(passed(c) for c in locking.values()) / len(locking) >= 0.90 and all(passed(c) for c in corpus["clips"] if not c["expected"]["locks"])


def write_report(report, output):
    output.mkdir(parents=True, exist_ok=True)
    (output / "fixtures-results.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    title = "SYNTHETIC infrastructure validation — not real-guitar qualification" if report.get("profile") == "synthetic" else "Real-guitar fixture measurements"
    lines = [f"# {title}", "", f"Status: {report['status']}", ""]
    if report.get("error"): lines += [f"Error: {report['error']}", ""]
    if report.get("manifest_sha256"): lines += [f"Manifest SHA-256: `{report['manifest_sha256']}`", ""]
    lines += ["| Clip | Block | Expected BPM | Final BPM | Error BPM | Lock s | False lock | Octave | Beat F | Phase ms |", "|---|---:|---:|---:|---:|---:|---|---|---:|---:|"]
    for r in report.get("results", []):
        fmt = lambda x: "—" if x is None else f"{x:.3f}"
        phase = r["median_phase_error_s"]
        lines.append(f"| {r['id']} ({r['sample_rate']} Hz) | {r['block_size']} | {fmt(r['expected_bpm_80_percent'])} | {fmt(r['final_bpm'])} | {fmt(r['tempo_error_bpm'])} | {fmt(r['time_to_lock_s'])} | {r['false_lock']} | {r['octave_relation']} | {fmt(r['beat_f_measure'])} | {fmt(None if phase is None else phase * 1000)} |")
    markdown = "\n".join(lines) + "\n"
    (output / "fixtures-results.md").write_text(markdown, encoding="utf-8")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as summary: summary.write(markdown)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    default = os.environ.get("ROBODRUMMER_FIXTURES") or str(Path(__file__).resolve().parents[1] / "fixtures/real-guitar-smoke")
    parser.add_argument("--corpus", type=Path, default=default)
    parser.add_argument("--required", action="store_true", default=os.environ.get("ROBODRUMMER_FIXTURES_REQUIRED") == "1")
    parser.add_argument("--require-full", action="store_true")
    parser.add_argument("--runner", type=Path)
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument("--output-dir", type=Path, default=Path("work/fixture-results"))
    args = parser.parse_args()
    try:
        corpus = validate_corpus(args.corpus, args.required, args.require_full)
        if corpus is None:
            report = {"schema": 1, "status": "SKIPPED (corpus not present)", "real_guitar_qualified": False, "results": []}
            write_report(report, args.output_dir); print(report["status"]); return 0
        report = {"schema": 1, "status": "VALIDATED", "real_guitar_qualified": False,
                  **{k: corpus[k] for k in ("profile", "corpus_version", "manifest_sha256")}, "results": []}
        if not args.validate_only:
            require(args.runner is not None and args.runner.is_file(), "Production fixture --runner required for measurements")
            for clip in corpus["clips"]:
                for block in (64, 512, 2048): report["results"].append(run_clip(args.runner, corpus, clip, block))
            full_pass = qualifies_full(corpus, report["results"])
            report["real_guitar_qualified"] = corpus["profile"] == "full" and full_pass
            if corpus["profile"] == "full": passed = full_pass
            else:
                # Until a complete licensed corpus exists, smoke checks protect
                # ingestion and steady-tempo sanity. Dynamic errors stay visible.
                steady_ids = {c["id"] for c in corpus["clips"] if c["tempo"]["type"] == "constant" and c["expected"]["locks"]}
                passed = all(r["tempo_error_bpm"] < 5.0 for r in report["results"] if r["id"] in steady_ids)
                passed = passed and all(not r["false_lock"] for r in report["results"] if not next(c for c in corpus["clips"] if c["id"] == r["id"])["expected"]["locks"])
            report["tracking_sanity_pass"] = passed
            # Newly synthesized signals are characterization inputs, not a
            # substitute for the licensed corpus's qualification denominator.
            report["status"] = ("PASS (synthetic infrastructure; tracking results reported)" if corpus["profile"] == "synthetic"
                                else "PASS" if passed else "FAIL")
        write_report(report, args.output_dir)
        print(f"{report['status']}: {corpus['profile']} {len(corpus['clips'])} clips; real_guitar_qualified={report['real_guitar_qualified']}")
        return 1 if report["status"] == "FAIL" else 0
    except (FixtureError, OSError, subprocess.TimeoutExpired) as error:
        print(f"Fixture validation failed: {error}", file=sys.stderr)
        try:
            write_report({"schema": 1, "status": "ERROR", "error": str(error), "real_guitar_qualified": False, "results": []}, args.output_dir)
        except OSError as report_error:
            print(f"Could not save failure report: {report_error}", file=sys.stderr)
        return 1


if __name__ == "__main__": sys.exit(main())
