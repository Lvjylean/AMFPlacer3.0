import fcntl
import gzip
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

from preview_cache_state import atomic_json, signature, preview_status, CACHE_NAME


class PreviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.run = Path(self.temp.name)
        self.directory = self.run / "placement/visualization"
        self.directory.mkdir(parents=True)
        self.sources = []
        for i in range(3):
            source = self.directory / f"DumpAllCoordTrace-{i}.gz"
            with gzip.open(source, "wt") as handle:
                handle.write(f"{i+1} {i+2} test_cell\n")
            self.sources.append(source)
        self.root = self.directory / CACHE_NAME
        self.root.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def state(self):
        return preview_status(self.directory, self.sources)

    def record(self, **fields):
        atomic_json(self.root / "build-status.json",
                    dict(source_signature=signature(self.sources), **fields))

    def test_idle_does_not_claim_running(self):
        self.assertEqual(self.state()["state"], "idle")

    def test_running_requires_held_lock_and_counts_finished_frames(self):
        self.record(state="running", phase="rendering", completed=1, pid=123)
        (self.root / (self.sources[0].name + ".webp")).write_bytes(b"fixture")
        with (self.root / "build.lock").open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            state = self.state()
            self.assertEqual((state["state"], state["completed"], state["percent"]),
                             ("running", 1, 33.3))
        self.assertEqual(self.state()["state"], "interrupted")

    def test_failure_exposed_without_live_process(self):
        self.record(state="failed", message="fixture error")
        self.assertEqual(self.state()["message"], "fixture error")

    def test_ready_requires_current_sources_and_all_images(self):
        for source in self.sources:
            (self.root / (source.name + ".webp")).write_bytes(b"fixture")
        atomic_json(self.root / "manifest.json",
                    {"source_signature": signature(self.sources)})
        self.assertTrue(self.state()["ready"])
        (self.root / (self.sources[0].name + ".webp")).unlink()
        self.assertEqual(self.state()["state"], "stale")
        self.sources[1].write_bytes(b"changed")
        self.assertFalse(self.state()["ready"])

    def fixture_config(self):
        with zipfile.ZipFile(self.run / "net.zip", "w") as archive:
            archive.writestr("allCellPinNet", "curCell=> test_cell type=> LUT1\n")
        with zipfile.ZipFile(self.run / "sites.zip", "w") as archive:
            archive.writestr("exportSiteLocation",
                            "site=> SLICE_X0Y0 centerx=> 0 centery=> 0 clockRegionName=> X0Y0\n"
                            "site=> SLICE_X9Y9 centerx=> 9 centery=> 9 clockRegionName=> X0Y0\n")
        (self.run / "boundaries.json").write_text(json.dumps(
            {"coordinate_bounds": [0, 10, 0, 10], "boundaries": []}))
        (self.run / "config.json").write_text(json.dumps({
            "vivado extracted design information file": str(self.run / "net.zip"),
            "vivado extracted device information file": str(self.run / "sites.zip"),
            "physical boundary model file": str(self.run / "physical.tsv"),
            "clockRegionXNum": "1", "clockRegionYNum": "1",
            "device":"U250", "physical device part":"xcu250-test",
        }))

    def builder(self):
        return subprocess.run(
            [sys.executable, str(Path(__file__).with_name("prepare_preview.py")),
             "--run", str(self.run), "--workers", "2"],
            capture_output=True, text=True, timeout=30)

    def test_nested_trace_build_reuse_and_duplicate_lock(self):
        self.fixture_config()
        with (self.root / "build.lock").open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            duplicate = self.builder()
            self.assertEqual(duplicate.returncode, 2, duplicate.stderr)
        build = self.builder()
        self.assertEqual(build.returncode, 0, build.stderr)
        self.assertEqual(self.state()["completed"], 3)
        self.assertTrue(self.state()["ready"])
        before = (self.root / "manifest.json").stat().st_mtime_ns
        again = self.builder()
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertEqual((self.root / "manifest.json").stat().st_mtime_ns, before)
        from PIL import Image
        for source in self.sources:
            with Image.open(self.root / (source.name + ".webp")) as image:
                self.assertEqual(image.size, (900, 1100))

    def test_render_error_writes_failed_state(self):
        self.fixture_config()
        self.sources[0].write_bytes(b"invalid gzip")
        failed = self.builder()
        self.assertNotEqual(failed.returncode, 0)
        self.assertEqual(self.state()["state"], "failed")
        self.assertIn("BadGzipFile", self.state()["message"])


if __name__ == "__main__":
    unittest.main()
