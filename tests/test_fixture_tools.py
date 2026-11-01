"""Boundary and metric tests for the offline fixture infrastructure."""
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("fixture_tools", ROOT / "tools/check_fixtures.py")
fixtures = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fixtures)


def write_corpus(directory):
    wav = directory / "S01_test.wav"
    with wave.open(str(wav), "wb") as audio:
        audio.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
        audio.writeframes(b"\0\0" * 4800)
    metadata = {
        "schema": 1, "id": "S01", "file": wav.name, "sample_rate": 48000,
        "duration_s": 0.1, "category": "silence", "tone": "synthetic", "synthetic": True,
        "meter": {"num": 4, "den": 4, "segments": [{"start_s": 0, "num": 4, "den": 4}]},
        "tempo": {"type": "none"}, "tempo_map": [], "beats_s": [], "downbeats_s": [],
        "expected": {"locks": False},
        "annotation": {"method": "deterministic_synthesis", "annotator": "generator", "reviewed_by": "automated", "date": "2026-10-31"},
        "license": {"spdx": "CC0-1.0", "holder": "RoboDrummer fixture generator", "source": "synthetic"},
    }
    (directory / "S01_test.json").write_text(json.dumps(metadata), encoding="utf-8")
    refresh_manifest(directory)
    return metadata


def refresh_manifest(directory):
    files = [{"file": p.name, "size_bytes": p.stat().st_size, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
             for p in sorted(directory.iterdir()) if p.name != "MANIFEST.json"]
    (directory / "MANIFEST.json").write_text(json.dumps({
        "schema": 1, "corpus_version": "synthetic-test-v1", "profile": "synthetic", "files": files,
    }), encoding="utf-8")


class FixtureBoundaryTests(unittest.TestCase):
    def test_malformed_schema_always_replaces_a_prior_success_report(self):
        for kind in ("spdx_list", "huge_integer", "over_digit_integer", "deep_json", "surrogate_path"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                corpus = directory / "corpus"
                corpus.mkdir()
                metadata = write_corpus(corpus)
                output = directory / "reports"
                command = ["python3", "-B", str(ROOT / "tools/check_fixtures.py"), "--corpus", str(corpus),
                           "--validate-only", "--output-dir", str(output)]
                before = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(before.returncode, 0, before.stderr)
                report_path = output / "fixtures-results.json"
                self.assertEqual(json.loads(report_path.read_text())["status"], "VALIDATED")
                if kind == "spdx_list": metadata["license"]["spdx"] = []
                elif kind == "huge_integer": metadata["duration_s"] = 10 ** 1000
                elif kind == "surrogate_path": metadata["file"] = "\ud800.wav"
                text = json.dumps(metadata)
                if kind == "over_digit_integer": text = '{"schema":1,"duration_s":' + "9" * 5000 + '}'
                elif kind == "deep_json": text = '{"schema":1,"nested":' + "[" * 100000 + "0" + "]" * 100000 + '}'
                (corpus / "S01_test.json").write_text(text, encoding="utf-8")
                refresh_manifest(corpus)
                failed = subprocess.run(command, capture_output=True, text=True)
                self.assertNotEqual(failed.returncode, 0)
                self.assertNotIn("Traceback", failed.stderr)
                self.assertEqual(json.loads(report_path.read_text())["status"], "ERROR")

    def test_annotation_count_limit_and_ordered_subset_boundary(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            metadata = write_corpus(directory)
            metadata["beats_s"] = [i / 50000 for i in range(4096)]
            metadata["downbeats_s"] = list(metadata["beats_s"])
            path = directory / "S01_test.json"
            path.write_text(json.dumps(metadata))
            refresh_manifest(directory)
            self.assertIsNotNone(fixtures.validate_corpus(directory))
            metadata["beats_s"].append(4096 / 50000)
            path.write_text(json.dumps(metadata))
            refresh_manifest(directory)
            with self.assertRaisesRegex(fixtures.FixtureError, "annotation|beats_s"):
                fixtures.validate_corpus(directory)

    def test_full_corpus_requires_48k_primaries_and_three_44100_variants(self):
        clips = [{"id": f"C{i:02}", "sample_rate": 48000} for i in range(1, 21)]
        self.assertFalse(fixtures.full_rate_coverage(clips))
        clips += [{"id": f"C{i:02}", "sample_rate": 44100} for i in range(1, 4)]
        self.assertTrue(fixtures.full_rate_coverage(clips))
        clips = [c for c in clips if not (c["id"] == "C01" and c["sample_rate"] == 48000)]
        self.assertFalse(fixtures.full_rate_coverage(clips))

    def test_ramp_final_breakpoint_may_equal_clip_duration(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            metadata = write_corpus(directory)
            metadata["tempo"] = {"type": "ramp"}
            metadata["tempo_map"] = [{"start_s": 0, "bpm": 100}, {"start_s": 0.1, "bpm": 130}]
            path = directory / "S01_test.json"
            path.write_text(json.dumps(metadata))
            refresh_manifest(directory)
            self.assertIsNotNone(fixtures.validate_corpus(directory))
            self.assertEqual(fixtures.target_bpm(metadata, 0.05), 115)
            metadata["tempo"]["type"] = "step"
            path.write_text(json.dumps(metadata))
            refresh_manifest(directory)
            with self.assertRaises(fixtures.FixtureError): fixtures.validate_corpus(directory)

    def test_optional_missing_corpus_skips_but_required_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "absent"
            self.assertIsNone(fixtures.validate_corpus(path))
            with self.assertRaisesRegex(fixtures.FixtureError, "required"):
                fixtures.validate_corpus(path, required=True)

    def test_valid_synthetic_corpus_cannot_qualify_as_real(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            write_corpus(directory)
            corpus = fixtures.validate_corpus(directory)
            self.assertTrue(corpus["clips"][0]["synthetic"])
            with self.assertRaisesRegex(fixtures.FixtureError, "real|full"):
                fixtures.validate_corpus(directory, require_full=True)

    def test_corruption_unlisted_files_and_traversal_are_rejected(self):
        for kind in ("hash", "missing", "unlisted", "traversal"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                write_corpus(directory)
                if kind == "hash":
                    with (directory / "S01_test.wav").open("ab") as output:
                        output.write(b"bad")
                elif kind == "missing":
                    (directory / "S01_test.wav").unlink()
                elif kind == "unlisted":
                    (directory / "other.wav").write_bytes(b"bad")
                else:
                    manifest = json.loads((directory / "MANIFEST.json").read_text())
                    manifest["files"][0]["file"] = "../outside.wav"
                    (directory / "MANIFEST.json").write_text(json.dumps(manifest))
                with self.assertRaises(fixtures.FixtureError):
                    fixtures.validate_corpus(directory)

    def test_bad_license_metadata_pcm_and_nonfinite_json_are_rejected(self):
        for kind in ("license", "duration", "rate", "beats", "pcm", "nan", "duplicate"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                metadata = write_corpus(directory)
                if kind == "license": metadata["license"]["spdx"] = "CC-BY-NC-4.0"
                elif kind == "duration": metadata["duration_s"] = 90
                elif kind == "rate": metadata["sample_rate"] = 44100
                elif kind == "beats": metadata["beats_s"] = [0.05, 0.01]
                elif kind == "pcm": (directory / "S01_test.wav").write_bytes(b"RIFF\0\0\0\0WAVE")
                text = json.dumps(metadata)
                if kind == "nan": text = text.replace('"duration_s": 0.1', '"duration_s": NaN')
                if kind == "duplicate": text = text.replace('"schema": 1', '"schema": 1, "schema": 1')
                (directory / "S01_test.json").write_text(text)
                refresh_manifest(directory)
                with self.assertRaises(fixtures.FixtureError): fixtures.validate_corpus(directory)


class FixtureMetricTests(unittest.TestCase):
    def metadata(self, locks=True):
        return {"duration_s": 10.0, "tempo": {"type": "constant", "bpm": 120.0},
                "tempo_map": [{"start_s": 0.0, "bpm": 120.0}], "beats_s": [1, 1.5, 2, 2.5],
                "expected": {"locks": locks, "lock_by_s": 6, "tempo_tolerance_bpm": 2}}

    def test_lock_requires_two_contiguous_seconds_and_rejects_octave_error(self):
        trace = [{"time_s": i * 0.1, "bpm": 120.0, "locked": i >= 30, "confidence": 1.0} for i in range(101)]
        result = fixtures.measure(self.metadata(), trace, [1.01, 1.49, 2.02, 2.5])
        self.assertAlmostEqual(result["time_to_lock_s"], 3.0)
        self.assertAlmostEqual(result["tempo_error_bpm"], 0.0)
        self.assertAlmostEqual(result["beat_f_measure"], 1.0)
        self.assertTrue(result["accepted"])
        for row in trace: row["bpm"] = 240.0
        result = fixtures.measure(self.metadata(), trace, [])
        self.assertEqual(result["octave_relation"], "double")
        self.assertEqual(result["folded_tempo_error_bpm"], 0.0)
        self.assertFalse(result["accepted"])

    def test_false_lock_duration_and_one_to_one_beat_matching(self):
        trace = [{"time_s": i * 0.1, "bpm": 120.0, "locked": i >= 20, "confidence": 1.0} for i in range(101)]
        metadata = self.metadata(False)
        metadata["tempo"] = {"type": "none"}
        metadata["tempo_map"] = []
        result = fixtures.measure(metadata, trace, [1.01, 1.02, 1.49])
        self.assertTrue(result["false_lock"])
        self.assertAlmostEqual(result["max_false_lock_s"], 8.0)
        self.assertAlmostEqual(result["beat_f_measure"], 4 / 7)
        self.assertFalse(result["accepted"])

    def test_phase_error_reports_predictions_outside_the_matching_window(self):
        trace = [{"time_s": i * 0.1, "bpm": 120.0, "locked": True, "confidence": 1.0} for i in range(101)]
        result = fixtures.measure(self.metadata(), trace, [1.2, 1.7, 2.2])
        self.assertEqual(result["beat_f_measure"], 0)
        self.assertAlmostEqual(result["median_phase_error_s"], 0.2)

    def test_full_qualification_counts_clips_and_all_block_sizes(self):
        corpus = {"clips": [{"id": f"C{i:02}", "expected": {"locks": True}} for i in range(1, 11)]}
        rows = [{"id": c["id"], "block_size": block, "accepted": True} for c in corpus["clips"] for block in (64, 512, 2048)]
        rows[0]["accepted"] = False
        self.assertTrue(fixtures.qualifies_full(corpus, rows))  # 9/10 clips
        rows[3]["accepted"] = False
        self.assertFalse(fixtures.qualifies_full(corpus, rows))  # 8/10, not 28/30 block runs

    def test_rate_variant_cannot_borrow_another_variants_block_coverage(self):
        corpus = {"clips": [{"id": "C01", "file": name, "expected": {"locks": True}} for name in ("a.wav", "b.wav")]}
        rows = [{"id": "C01", "file": "a.wav", "block_size": block, "accepted": True} for block in (64, 512, 2048)]
        rows += [{"id": "C01", "file": "b.wav", "block_size": block, "accepted": True} for block in (64, 512)]
        self.assertFalse(fixtures.qualifies_full(corpus, rows))

    def test_ramp_and_jump_targets_do_not_silently_interpolate_the_same_way(self):
        metadata = self.metadata()
        metadata["tempo_map"] = [{"start_s": 0, "bpm": 100}, {"start_s": 10, "bpm": 140}]
        metadata["tempo"] = {"type": "ramp"}
        self.assertEqual(fixtures.target_bpm(metadata, 5), 120)
        metadata["tempo"] = {"type": "step"}
        self.assertEqual(fixtures.target_bpm(metadata, 5), 100)

    def test_metadata_cannot_weaken_full_corpus_lock_and_false_lock_limits(self):
        metadata = self.metadata()
        metadata["expected"]["lock_by_s"] = 100
        trace = [{"time_s": i * 0.1, "bpm": 120.0, "locked": i >= 70, "confidence": 1.0} for i in range(101)]
        self.assertFalse(fixtures.measure(metadata, trace, [])["accepted"])
        metadata["expected"] = {"locks": False, "max_false_lock_s": 100}
        metadata["tempo"] = {"type": "none"}
        for row in trace: row["locked"] = True
        self.assertFalse(fixtures.measure(metadata, trace, [])["accepted"])


class FixtureRunnerIntegration(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("ROBODRUMMER_FIXTURE_RUNNER"), "fixture runner is built by the core CMake integration")
    def test_44100_variants_use_file_rate_and_runner_rejects_bad_pcm(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            subprocess.run(["python3", str(ROOT / "tools/generate_synthetic_fixtures.py"), str(directory / "corpus"), "--sample-rate", "44100", "--duration", "3"], check=True)
            corpus = fixtures.validate_corpus(directory / "corpus")
            for clip in (corpus["clips"][0], corpus["clips"][3], corpus["clips"][4]):
                for block in (64, 512, 2048):
                    row = fixtures.run_clip(os.environ["ROBODRUMMER_FIXTURE_RUNNER"], corpus, clip, block)
                    self.assertEqual(row["sample_rate"], 44100)
                    self.assertTrue(math.isfinite(row["final_bpm"]))
            malformed = directory / "bad.wav"
            malformed.write_bytes(b"RIFF\0\0\0\0WAVE")
            run = subprocess.run([os.environ["ROBODRUMMER_FIXTURE_RUNNER"], str(malformed), "512", "0", "4"], capture_output=True)
            self.assertNotEqual(run.returncode, 0)

    @unittest.skipUnless(os.environ.get("ROBODRUMMER_FIXTURE_RUNNER"), "fixture runner is built by the core CMake integration")
    def test_synthetic_ingestion_reports_all_three_block_sizes(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            subprocess.run(["python3", str(ROOT / "tools/generate_synthetic_fixtures.py"), str(directory / "corpus")], check=True)
            result = subprocess.run(["python3", str(ROOT / "tools/check_fixtures.py"), "--corpus", str(directory / "corpus"),
                                     "--required", "--runner", os.environ["ROBODRUMMER_FIXTURE_RUNNER"], "--output-dir", str(directory / "reports")],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads((directory / "reports/fixtures-results.json").read_text())
            self.assertFalse(report["real_guitar_qualified"])
            self.assertEqual({r["block_size"] for r in report["results"]}, {64, 512, 2048})
            self.assertEqual(len(report["results"]), 21)
            self.assertIn("SYNTHETIC", (directory / "reports/fixtures-results.md").read_text())
            # The novel one-string 100 BPM fixture exposes a half-time tracker
            # ambiguity. Preserve its truth and record the failure explicitly;
            # synthetic infrastructure success must not imply tracker quality.
            ambiguous = [r for r in report["results"] if r["id"] == "S01"]
            self.assertTrue(all(r["expected_bpm_80_percent"] == 100 for r in ambiguous))
            for row in ambiguous:
                if abs(row["final_bpm"] - 50) < 2:
                    self.assertEqual(row["octave_relation"], "half")
                    self.assertFalse(row["accepted"])
                    self.assertGreater(row["tempo_error_bpm"], 40)


if __name__ == "__main__": unittest.main()
