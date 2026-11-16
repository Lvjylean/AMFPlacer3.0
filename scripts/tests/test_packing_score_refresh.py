#!/usr/bin/env python3
"""Execute frozen scoring method bodies with isolated dependencies.

This reproduces accumulator state only. It does not run the full packer,
construct a physical netlist, or predict CLB savings.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import subprocess


def body(text, name):
    start = text.index("void ParallelCLBPacker::PackingCLBSite::PackingCLBCluster::" + name)
    begin = text.index("{", start)
    depth = 0
    for end in range(begin, len(text)):
        depth += (text[end] == "{") - (text[end] == "}")
        if depth == 0:
            return text[begin:end + 1]
    raise ValueError("Unbalanced method")


PREAMBLE = r'''
#include <set>
#include <vector>
#include <string>
#include <iostream>
namespace PlacementInfo {
enum Kind { PlacementUnitType_Macro, Single };
struct PlacementNet {
    std::vector<int> units{0,1};
    const std::vector<int>& getUnits() const { return units; }
};
struct PlacementUnit {
    float slack;
    std::set<PlacementNet*> nets;
    auto getNetsSetPtr() { return &nets; }
    Kind getType() const { return Single; }
    int getWeight() const { return 1; }
};
}
struct Packing_Netcompare {
    bool operator()(PlacementInfo::PlacementNet* a, PlacementInfo::PlacementNet* b) const { return a<b; }
};
struct Parent { bool isTimingPathLenFactorDisabled() const { return false; } };
struct Probe {
    Parent parent;
    Parent* parentPackingCLB=&parent;
    std::vector<PlacementInfo::PlacementUnit*> PUs;
    float HPWLChange=0,totalNegSlack=0,totalConnectivityScore=0,scoreInSite=0,HPWLWeight=.01;
    int totalNetNum=0,totalLen=0,totalCellNum=0;
    std::set<int> singleLUTs{1,2},pairedLUTs;
    float getHPWLChangeForPU(PlacementInfo::PlacementUnit*) { return 0; }
    int getPlacementUnitMaxPathLen(PlacementInfo::PlacementUnit*) { return 10; }
    float getPlacementUnitMaxPathNegativeSlack(PlacementInfo::PlacementUnit* p) { return p->slack; }
    int getInternalPinsNum(PlacementInfo::PlacementNet*) { return 1; }
'''
MAIN = r'''
};
int main() {
    for (std::string name : {"positive", "negative", "zero", "incremental_then_refresh", "remove_then_refresh"}) {
        PlacementInfo::PlacementUnit a{2,{}},b{3,{}};
        if(name=="negative") {a.slack=-2;b.slack=-3;}
        if(name=="zero") {a.slack=0;b.slack=0;}
        Probe p;p.PUs={&a,&b};
        for(int i=0;i<3;++i) {
            if(i==0 && name=="incremental_then_refresh") p.incrementalUpdateScoreInSite(&a);
            else p.updateScoreInSite();
            float expected=0;for(auto pu:p.PUs) expected+=pu->slack;
            std::cout<<name<<"\t"<<i<<"\t"<<p.totalNegSlack<<"\t"<<expected<<"\t"<<p.scoreInSite<<"\n";
            if(i==0 && name=="remove_then_refresh") p.PUs={&a};
        }
    }
}
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--expect-drift", action="store_true")
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    source = args.source.read_text()
    cpp = args.output / "score_refresh.cc"
    cpp.write_text(PREAMBLE + "void updateScoreInSite()\n" + body(source,"updateScoreInSite()") +
                  "\nvoid incrementalUpdateScoreInSite(PlacementInfo::PlacementUnit* curPU)\n" +
                  body(source,"incrementalUpdateScoreInSite(") + MAIN)
    binary = args.output / "score_refresh"
    command = ["c++","-std=c++17","-O0",str(cpp),"-o",str(binary)]
    result = subprocess.run(command,capture_output=True,text=True)
    (args.output/"build.log").write_text(result.stdout+result.stderr)
    result.check_returncode()
    result = subprocess.run([str(binary)],capture_output=True,text=True,check=True)
    (args.output/"results.tsv").write_text("scenario\tcall\taccumulator\texpected\tscore\n"+result.stdout)
    rows=[]
    for line in result.stdout.splitlines():
        scenario,call,actual,expected,score=line.split("\t")
        rows.append(dict(scenario=scenario,call=int(call),actual=float(actual),expected=float(expected),score=float(score),matches=abs(float(actual)-float(expected))<1e-6))
    count=sum(not r["matches"] for r in rows)
    expected_count=8 if args.expect_drift else 0
    report=dict(source=str(args.source.resolve()),source_sha256=hashlib.sha256(args.source.read_bytes()).hexdigest(),method="Exact production scoring method bodies; dependency stubs isolate accumulator state",scenarios=5,evaluations=len(rows),mismatch_count=count,expected_mismatch_count=expected_count,passed=count==expected_count,rows=rows,limitations=["Not the complete packer or physical validation", "Full GETRF placement compares CLB occupancy separately"])
    (args.output/"result.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({k:report[k] for k in ["scenarios","evaluations","mismatch_count","expected_mismatch_count","passed"]}))
    if not report["passed"]: raise SystemExit(1)

if __name__ == "__main__":
    main()
