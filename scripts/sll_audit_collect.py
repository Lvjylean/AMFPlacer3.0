#!/usr/bin/env python3
"""Merge successful read-only audit shards with exact net/receiver coverage."""
import json
import pathlib
import sys


def collect(directory, output):
    directory, output = pathlib.Path(directory), pathlib.Path(output)
    manifest = json.loads((directory / "audit-manifest.json").read_text())
    if manifest.get("state") != "completed":
        raise ValueError("SLL audit workers did not all complete")
    expected = {}
    with (directory / "expected.tsv").open() as stream:
        for line in stream:
            name, count = line.rstrip("\n").split("\t")
            if name in expected:
                raise ValueError("Repeated expected SLL net: " + name)
            expected[name] = int(count)
    planned = {}
    with (directory / "expected-receivers.tsv").open() as stream:
        for line in stream:
            net, pin, path = line.rstrip("\n").split("\t")
            key = (net, pin)
            if net not in expected or key in planned:
                raise ValueError("Repeated or unknown planned SLL receiver")
            planned[key] = path
    roots = sorted(directory.glob("shard-*"))
    statuses = {row["shard"]: row for row in manifest["shards"]}
    if len(statuses) != len(manifest["shards"]) or set(statuses) != {root.name for root in roots}:
        raise ValueError("SLL audit shard coverage mismatch")
    receivers, sources, counts = set(), set(), {}
    tables = [
        ("sll-path-audit.tsv", ["net", "pin", "expected", "actual", "match"]),
        ("sll-source-access-audit.tsv", ["net", "candidate_outputs", "used_outputs"]),
    ]
    temporary = []
    try:
        for filename, header in tables:
            target = output / (filename + ".tmp")
            temporary.append(target)
            with target.open("w") as dest:
                dest.write("\t".join(header) + "\n")
                for root in roots:
                    if statuses[root.name]["exit_code"] != 0:
                        raise ValueError("Failed SLL audit shard: " + root.name)
                    with (root / "audit" / filename).open() as stream:
                        if next(stream).rstrip("\n").split("\t") != header:
                            raise ValueError("SLL audit table header mismatch")
                        for line in stream:
                            row = line.rstrip("\n").split("\t")
                            if len(row) != len(header) or row[0] not in expected:
                                raise ValueError("Unknown or malformed SLL audit row")
                            if filename == "sll-path-audit.tsv":
                                key = tuple(row[:2])
                                if key in receivers:
                                    raise ValueError("Repeated receiver audit: " + repr(key))
                                if key not in planned or row[2] != planned[key]:
                                    raise ValueError("Unplanned receiver or expected SLL path changed")
                                if row[-1] != "1" or row[2] != row[3]:
                                    raise ValueError("Receiver SLL path mismatch")
                                receivers.add(key)
                                counts[row[0]] = counts.get(row[0], 0) + 1
                            else:
                                if row[0] in sources or not row[2]:
                                    raise ValueError("Repeated or empty source audit")
                                sources.add(row[0])
                            dest.write("\t".join(row) + "\n")
        if sources != set(expected) or {k: counts.get(k, 0) for k in expected} != expected:
            raise ValueError("Missing net or receiver audit coverage")
        if receivers != set(planned):
            raise ValueError("Missing planned receiver audit")
        if len(receivers) != manifest.get("verified_receiver_rows"):
            raise ValueError("Worker/collector receiver count mismatch")
        for target in temporary:
            target.replace(output / target.name[:-4])
        result = dict(state="completed", nets=len(sources), receivers=len(receivers),
                      receiver_coverage_exact=True, source_coverage_exact=True)
        (output / "sll-audit-merge.json").write_text(json.dumps(result, indent=2) + "\n")
        return result
    except BaseException:
        for target in temporary:
            target.unlink(missing_ok=True)
        raise


if __name__ == "__main__":
    collect(*sys.argv[1:])
