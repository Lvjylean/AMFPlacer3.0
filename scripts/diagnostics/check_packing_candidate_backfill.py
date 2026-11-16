#!/usr/bin/env python3
"""Compile the production refill block and replay observed search windows.

Only query scheduling is isolated. The query stub records its arguments;
this does not simulate packing legality, scoring, or a complete placement.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

SOURCE = "src/lib/HiFPlacer/placement/packing/ParallelCLBPacker_PackingCLBSite.cc"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--source-ref")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    source = (subprocess.check_output(
        ["git", "show", f"{args.source_ref}:{SOURCE}"], cwd=args.repo, text=True
    ) if args.source_ref else (args.repo / SOURCE).read_text())
    start = source.index("    if (neighborPUs.size() < numNeighbor", source.index("void ParallelCLBPacker::PackingCLBSite::updateStep"))
    stop = source.index("    findNewClustersWithNeighborPUs();", start)
    block = source[start:stop]
    harness = r'''
#include <algorithm>
#include <iostream>
#include <set>
#include <vector>
namespace DesignInfo { enum Type { CellType_LUT6, CellType_FDCE }; }
struct Position { float X() const { return 0; } float Y() const { return 0; } };
struct Query { int type; float low, high; };
struct Site {
    std::set<int> neighborPUs;
    unsigned numNeighbor = 10;
    float curD, maxD, deltaD = .25f, y2xRatio = .4f;
    Position position, *CLBSite = &position;
    std::vector<Site *> PUId2PackingCLBSite;
    std::vector<Query> queries;
    void findNeiborPUsFromBinGrid(DesignInfo::Type type, float, float,
                                float low, float high, int,
                                const std::vector<Site *> &, float,
                                std::set<int> *) {
        queries.push_back({type, low, high});
    }
    void refill(bool initial) {
''' + block + r'''
    }
};
int main() {
    float radius, maximum; int initial, neighbors;
    while (std::cin >> radius >> maximum >> initial >> neighbors) {
        Site site; site.curD = radius; site.maxD = maximum;
        for (int n = 0; n < neighbors; ++n) site.neighborPUs.insert(n);
        site.refill(initial);
        std::cout << site.curD << " " << site.queries.size();
        for (const auto &q : site.queries)
            std::cout << " " << q.type << " " << q.low << " " << q.high;
        std::cout << "\n";
    }
}
'''
    cpp = args.out / "production_refill.cc"
    exe = args.out / "production_refill"
    cpp.write_text(harness)
    command = ["/usr/bin/c++", "-std=c++17", "-O0", "-Wall", str(cpp), "-o", str(exe)]
    subprocess.run(command, check=True)
    fixture = json.loads(args.fixture.read_text())
    events = fixture["events"]
    cases = [dict(name=f"{r['site']}/iter{r['iteration']}/PU{r['pu_id']}",
                  radius=r["radius"], maximum=6, initial=False,
                  neighbors=r["neighbors"], distance=r["distance"], real=True)
             for r in events]
    cases += [
        dict(name="initial-radius", radius=.5, maximum=6, initial=True, neighbors=0),
        dict(name="outer-annulus-still-covered", radius=1, maximum=6, initial=False, neighbors=1, distance=1.2),
        dict(name="maximum-radius-backfill", radius=6, maximum=6, initial=False, neighbors=1, distance=.3),
        dict(name="radius-clamped", radius=5.9, maximum=6, initial=False, neighbors=1, distance=6),
        dict(name="sufficient-neighbors-no-query", radius=1, maximum=6, initial=False, neighbors=10),
        dict(name="sufficient-neighbors-at-cap", radius=6, maximum=6, initial=False, neighbors=10),
    ]
    payload = "".join(f"{c['radius']} {c['maximum']} {int(c['initial'])} {c['neighbors']}\n" for c in cases)
    lines = subprocess.check_output([str(exe)], input=payload, text=True).splitlines()
    assert len(lines) == len(cases)
    results = []
    for c, line in zip(cases, lines):
        values = line.split()
        radius, count = float(values[0]), int(values[1])
        queries = [(int(values[i]), float(values[i+1]), float(values[i+2])) for i in range(2, len(values), 3)]
        should_search = c["neighbors"] < 10
        expected_upper = c["radius"] if c["initial"] else min(c["radius"] + .25, c["maximum"])
        if should_search:
            passed = count == 2 and queries == [(0, 0, expected_upper), (1, 0, expected_upper)] and abs(radius - expected_upper) < 1e-5
        else:
            passed = count == 0 and abs(radius - c["radius"]) < 1e-5
        covered = any(low < c["distance"] <= high for _, low, high in queries) if "distance" in c else None
        if should_search and covered is not None:
            passed = passed and covered
        results.append(dict(**c, passed=passed, queried_radius=radius, queries=queries, covered=covered))
    eligible = [r for r in results if r.get("real") and r["neighbors"] < 10]
    report = dict(scope="production refill block, query-window replay; no physical or full-placement validation",
                  source_ref=args.source_ref, source_sha256=hashlib.sha256(source.encode()).hexdigest(),
                  block_sha256=hashlib.sha256(block.encode()).hexdigest(), compile=command,
                  total=len(results), passed=sum(r["passed"] for r in results),
                  real_events=len(events), eligible_real_events=len(eligible),
                  recovered_eligible_events=sum(r["covered"] for r in eligible),
                  real_events_unchanged_by_threshold=sum(r["neighbors"] >= 10 for r in results if r.get("real")),
                  results=results)
    (args.out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k not in ("results", "compile")}, indent=2))
    return 0 if report["passed"] == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
