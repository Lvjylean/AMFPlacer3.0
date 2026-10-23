#!/usr/bin/env python3
"""Dispatch regression tests; no Vivado or placement process is used."""
import json
import os
from pathlib import Path
import socket
import tempfile
import unittest
from unittest.mock import patch

import launch_placement as launch


class DispatchTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.primary = {
            "device": "U250", "ClockPeriod": "10", "jobs": "12",
            "visualization": "false", "ProFFClustering": "true",
            "ProPrimitiveTiming": "false", "ProLegacyIOBoundary": "false",
            "BoundaryAwareClustering": "true", "noBlockAware": "false",
            "BoundaryClusteringStrategy": "cr-slr-hierarchical",
            "BoundaryStageMode": "independent-max2", "BoundaryRegionClipping": "true",
            "BoundaryWeakAttraction": "true", "BoundaryAnchorStopOnRegionEntry": "false",
            "ClockRegionColumnStretch": "false", "IncrementalCompilation": "false",
        }
        self.base = dict(self.primary)
        for key in ["vivado extracted device information file",
                    "vivado extracted design information file", "physical boundary model file"]:
            p = self.root/(str(len(self.base))+".input")
            p.write_text(key)
            self.base[key] = str(p)
        self.base.update(DumpCLBPacking="DumpCLBPacking",
                         BipartiteMatchingBackend="legacy_components")
        (self.root/"base.json").write_text(json.dumps(self.base))
        (self.root/"provenance.json").write_text("{}")
        for name in ["AMFPlacer", "partitionHyperGraph", "vivado", "dcp"]:
            (self.root/name).write_text("fixture")
        (self.root/"scripts").mkdir()
        (self.root/"scripts/amf3.py").write_text(
            "from pathlib import Path\nPath('stub-ran').write_text('ok')\n")
        target = dict(root=str(self.root), hostnames=[socket.gethostname().split(".")[0]],
                      profile="83", binary="AMFPlacer",
                      binary_sha256=launch.digest(self.root/"AMFPlacer"),
                      helper_sha256=launch.digest(self.root/"partitionHyperGraph"),
                      required_capabilities={"device_placement_policy_schema": 1},
                      vivado=str(self.root/"vivado"), environment={},
                      flow_files={}, minimum_memory_gib=60, minimum_disk_gib=20)
        case = dict(base="base.json", base_sha256=launch.digest(self.root/"base.json"),
                    provenance="provenance.json", provenance_sha256=launch.digest(self.root/"provenance.json"),
                    vivado_clock_period_ns=10, support_files=[], inputs={
                        key: dict(path=value, sha256=launch.digest(value))
                        for key, value in self.base.items() if key.endswith("information file")
                        or key == "physical boundary model file"})
        case["inputs"]["post_opt.dcp"] = dict(path=str(self.root/"dcp"),
                                            sha256=launch.digest(self.root/"dcp"))
        self.catalog = dict(targets={"072": target}, cases={"getrf-prepared": {"072": case}})
        self.request = dict(schema=1, target="072", id="test-run", action="check",
                            case="getrf-prepared", primary_text=json.dumps(self.primary),
                            config_origin="fixture")
        self.resource = patch.object(launch, "resources", return_value={
            "memory_available_gib": 100, "disk_free_gib": 100})
        self.capabilities = patch.object(launch.subprocess, "check_output",
                                        return_value='{"device_placement_policy_schema":1}')
        self.resource_mock = self.resource.start()
        self.capabilities.start()

    def tearDown(self):
        self.resource.stop()
        self.capabilities.stop()
        self.tmp.cleanup()

    def changed(self, **values):
        primary = dict(self.primary, **values)
        return dict(self.request, primary_text=json.dumps(primary))

    def test_visualization_overrides_all_host_choices(self):
        for host in ("auto", "072", "099"):
            self.assertEqual(launch.select_host({"visualization": "true"}, host, "072"), "099")

    def test_normal_host_choice(self):
        self.assertEqual(launch.select_host(self.primary, "auto", None), "072")
        self.assertEqual(launch.select_host(self.primary, "auto", "099"), "099")
        self.assertEqual(launch.select_host(self.primary, "099", "072"), "099")

    def test_dry_run_no_mutation_or_dispatch(self):
        before = sorted(str(p) for p in self.root.rglob("*"))
        with patch.object(launch.subprocess, "Popen") as popen:
            report = launch.receive(self.request, self.catalog)
            self.assertFalse(report["experiment_started"])
            popen.assert_not_called()
        self.assertEqual(before, sorted(str(p) for p in self.root.rglob("*")))

    def test_remote_cannot_bypass_visual_host(self):
        with self.assertRaisesRegex(ValueError, "099"):
            launch.receive(self.changed(visualization="true"), self.catalog)

    def test_incremental_not_sent_to_full_runner(self):
        with self.assertRaisesRegex(ValueError, "DFX"):
            launch.receive(self.changed(IncrementalCompilation="true"), self.catalog)

    def test_clock_mismatch_stops_full_run(self):
        with self.assertRaisesRegex(ValueError, "period"):
            launch.receive(self.changed(ClockPeriod="12"), self.catalog)

    def test_stale_input_rejected(self):
        (self.root/"dcp").write_text("changed")
        with self.assertRaisesRegex(ValueError, "hash changed"):
            launch.receive(self.request, self.catalog)

    def test_missing_helper_rejected(self):
        (self.root/"partitionHyperGraph").unlink()
        with self.assertRaises(OSError):
            launch.receive(self.request, self.catalog)

    def test_clock_capacity_requires_real_build_record(self):
        build = self.root/"build"
        build.mkdir()
        binary = build/"AMFPlacer"
        binary.write_text("fixture")
        with self.assertRaises(OSError):
            launch.verify_build_metadata(binary)
        record = {"state": "completed", "capabilities": {
            "schema": "amf-capabilities-v1", "clock_resource_capacity_schema": 1},
            "binaries": {"AMFPlacer": launch.digest(binary)}}
        (self.root/"manifest.json").write_text(json.dumps(record))
        self.assertEqual(launch.verify_build_metadata(binary)["path"],
                         str((self.root/"manifest.json").resolve()))
        binary.write_text("changed")
        with self.assertRaises(ValueError):
            launch.verify_build_metadata(binary)

    def test_resource_shortage_never_submits(self):
        self.resource_mock.return_value = {"memory_available_gib": 59, "disk_free_gib": 100}
        with patch.object(launch.subprocess, "Popen") as popen:
            with self.assertRaisesRegex(ValueError, "threshold"):
                launch.receive(dict(self.request, action="start"), self.catalog)
            popen.assert_not_called()

    def test_thread_environment_follows_jobs(self):
        with patch.dict(os.environ, {"AMF_UNREQUESTED": "1", "OMP_NUM_THREADS": "8"}):
            env = launch.environment({}, "12")
        self.assertNotIn("AMF_UNREQUESTED", env)
        for key in ("OMP_NUM_THREADS", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS"):
            self.assertEqual(env[key], "12")

    def test_freeze_and_idempotence(self):
        request = dict(self.request, action="start")
        with patch.object(launch.subprocess, "Popen") as popen:
            popen.return_value.pid = 12345
            first = launch.receive(request, self.catalog)
            second = launch.receive(request, self.catalog)
            self.assertEqual(popen.call_count, 1)
        job = Path(first["request_directory"])
        self.assertFalse(second["resubmitted"])
        self.assertEqual((job/"placer.json").read_text(), request["primary_text"])
        self.assertEqual(json.loads((job/"amf_config.json").read_text())["jobs"], "12")
        with self.assertRaisesRegex(ValueError, "another configuration"):
            launch.receive(dict(self.changed(jobs="8"), action="start"), self.catalog)

    def test_worker_runs_only_stub_and_requires_review(self):
        request = dict(self.request, action="start")
        with patch.object(launch.subprocess, "Popen") as popen:
            popen.return_value.pid = 12345
            response = launch.receive(request, self.catalog)
        job = Path(response["request_directory"])
        self.assertEqual(launch.worker(job), 0)
        self.assertTrue((self.root/"stub-ran").is_file())
        status = json.loads((job/"status.json").read_text())
        self.assertEqual(status["state"], "completed_pending_review")
        argv = json.loads((job/"command.json").read_text())["argv"]
        self.assertEqual(argv[argv.index("--config")+1], str(job/"amf_config.json"))
        self.assertEqual(launch.worker(job), 1)
        self.assertEqual(json.loads((job/"status.json").read_text()), status)


if __name__ == "__main__":
    unittest.main()
