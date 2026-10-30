import importlib.util
import json
import pathlib
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("sll_audit_collect", pathlib.Path(__file__).with_name("sll_audit_collect.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CoverageTests(unittest.TestCase):
    def fixture(self, root, mutation=""):
        (root / "expected.tsv").write_text("n0\t1\nn1\t1\n")
        (root / "expected-receivers.tsv").write_text("n0\tp0\tsll\nn1\tp1\tsll\n")
        shards = []
        for i in range(2):
            shard = root / ("shard-%02d" % i)
            (shard / "audit").mkdir(parents=True)
            net, pin = ("n0", "p0") if mutation == "duplicate" else ("n%d" % i, "p%d" % i)
            row = "%s\t%s\tsll\tsll\t1\n" % (net, pin)
            if mutation == "swapped_pin" and i == 1:
                row = "%s\twrong_pin\tsll\tsll\t1\n" % net
            if mutation == "changed_plan" and i == 1:
                row = "%s\t%s\tother\tother\t1\n" % (net, pin)
            if mutation == "missing" and i == 1:
                row = ""
            if mutation == "mismatch" and i == 1:
                row = "%s\t%s\tsll\tother\t0\n" % (net, pin)
            (shard / "audit/sll-path-audit.tsv").write_text("net\tpin\texpected\tactual\tmatch\n" + row)
            (shard / "audit/sll-source-access-audit.tsv").write_text("net\tcandidate_outputs\tused_outputs\nn%d\tO\tO\n" % i)
            shards.append(dict(shard=shard.name, exit_code=1 if mutation == "worker" and i == 1 else 0))
        (root / "audit-manifest.json").write_text(json.dumps(dict(state="completed", verified_receiver_rows=2, shards=shards)))

    def test_exact(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            self.fixture(root)
            self.assertEqual(module.collect(root, root)["receivers"], 2)

    def test_local_receiver_empty_sll_path(self):
        with tempfile.TemporaryDirectory() as name:
            root = pathlib.Path(name)
            self.fixture(root)
            (root / "expected-receivers.tsv").write_text("n0\tp0\tsll\nn1\tp1\t\n")
            path = root / "shard-01/audit/sll-path-audit.tsv"
            path.write_text("net\tpin\texpected\tactual\tmatch\nn1\tp1\t\t\t1\n")
            self.assertEqual(module.collect(root, root)["receivers"], 2)

    def test_reject_incomplete_or_inconsistent_shards(self):
        for mutation in ["missing", "duplicate", "mismatch", "worker", "swapped_pin", "changed_plan"]:
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as name:
                root = pathlib.Path(name)
                self.fixture(root, mutation)
                with self.assertRaises(ValueError):
                    module.collect(root, root)
                self.assertFalse((root / "sll-audit-merge.json").exists())


if __name__ == "__main__":
    unittest.main()
