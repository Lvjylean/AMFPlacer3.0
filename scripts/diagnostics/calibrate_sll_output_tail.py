"""Calibrate only the long SLL-exit-to-receiver delay, without an intercept."""
import argparse
import collections
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import time

def read(path):
    with path.open() as f:
        return list(csv.DictReader(f, delimiter="\t"))

def stats(values):
    return dict(n=len(values), mae=statistics.mean(map(abs, values)),
                bias=statistics.mean(values), rmse=math.sqrt(statistics.mean(x*x for x in values))) if values else {"n": 0}

def fit(data):
    counts = collections.Counter(r["source_pin"] for r in data)
    denominator = sum(r["z"]**2/counts[r["source_pin"]] for r in data)
    if denominator <= 0:
        raise ValueError("No training samples above the fixed threshold")
    return max(0.0, sum(r["z"]*r["output_error"]/counts[r["source_pin"]] for r in data)/denominator)

p = argparse.ArgumentParser()
p.add_argument("--decomposition", type=Path, required=True)
p.add_argument("--previous-predictions", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
a = p.parse_args()
start = time.time()
a.output.mkdir(parents=True, exist_ok=False)
reports = a.output / "reports"
reports.mkdir()
inputs = a.output / "inputs"
inputs.mkdir()
source = a.decomposition / "reports/decomposed-connections.tsv"
data = read(source)
previous = read(a.previous_predictions / "reports/connection-predictions.tsv")
critical = {mode: {(r["source_pin"], r["sink_pin"]) for r in previous if r["mode"]==mode
                  and r["sample"]=="top100" and r["source_slr"]!=r["sink_slr"]} for mode in ["fixed", "free"]}
for r in data:
    r["fold"] = int(hashlib.sha256(("delay-fit-fold-v1|"+r["source_pin"]).encode()).hexdigest()[:8],16)%5
    r["z"] = max(0., float(r["receiver_pred_ns"])-1.5)
    r["output_error"] = float(r["receiver_actual_ns"])-float(r["receiver_pred_ns"])
good = [r for r in data if r["severe_any_leg"]=="0" and r["sll_count"]=="1"]
train = [r for r in good if r["mode"]=="fixed" and r["sample"]=="stratified"]
slope = fit(train)
folds = {k: fit([r for r in train if r["fold"]!=k]) for k in range(5)}
evaluations = []
for r in data:
    fold_training = [t for t in train if t["fold"]!=r["fold"]]
    assert r["source_pin"] not in {t["source_pin"] for t in fold_training}
    correction = folds[r["fold"]]*r["z"]
    old = float(r["prediction_actual_crossing_ns"])
    observed = float(r["actual_ns"])
    assert r["z"]>0 or correction==0
    evaluations.append(dict(id=r["id"], mode=r["mode"], source_pin=r["source_pin"],sink_pin=r["sink_pin"],
        sample=r["sample"],fold=r["fold"],stratum=r["stratum"], severe_any_leg=r["severe_any_leg"],sll_count=r["sll_count"],
        original_critical=int((r["source_pin"],r["sink_pin"]) in critical[r["mode"]]),
        output_old_ns=float(r["receiver_pred_ns"]),output_actual_ns=float(r["receiver_actual_ns"]),
        applied_oof_correction_ns=correction,old_net_ns=old,new_net_ns=old+correction,actual_net_ns=observed,
        old_error_ns=observed-old,new_error_ns=observed-old-correction,
        old_output_error_ns=r["output_error"],new_output_error_ns=r["output_error"]-correction))
summary = {"formula": "f_out_new = f_out_old + slope * max(0, f_out_old - threshold_ns)",
    "threshold_ns":1.5,"slope":slope,"intercept_ns":0,"slope_units":"dimensionless",
    "coefficient_fit_target":"routed exit-to-receiver delay minus original exit-to-receiver prediction, NOT whole-net residual",
    "fit_objective":"nonnegative weighted least squares, equal aggregate weight per sampled source net",
    "training_n":len(train),"training_nets":len({r['source_pin'] for r in train}),
    "training_long_n":sum(r['z']>0 for r in train),"training_long_nets":len({r['source_pin'] for r in train if r['z']>0}),
    "fold_slopes":folds,"groups":{},
    "scope":"terminal SLL exit -> actual receiver only; original local polynomial, first source leg, transit between crossings, and SLL intrinsic delays unchanged",
    "validation":"5 folds grouped by source net; fixed stratified training only; free mode uses actual routed crossing geometry; threshold fixed in advance",
    "filter":"exclude any local leg with walked/direct > 1.5 AND excess physical COLUMN+ROW > 30; fit single-boundary paths only",
    "limitations":["One GETRF placement; no independent benchmark validation", "No full placer/routing QoR run", "Free critical top100 all fail geometric filter; no retained critical validation there"]}
for mode in ['fixed','free']:
    for group in ['stratified','critical_union','original_critical']:
        subset=[r for r in evaluations if r['mode']==mode and r['severe_any_leg']=='0' and r['sll_count']=='1'
                and ((group=='stratified' and r['sample']=='stratified') or
                     (group=='critical_union' and r['sample']=='critical') or
                     (group=='original_critical' and r['original_critical']))]
        for distance in ['all','unchanged','long']:
            selected=[r for r in subset if distance=='all' or ((r['output_old_ns']<=1.5)==(distance=='unchanged'))]
            summary['groups'][mode+'/'+group+'/'+distance] = dict(
                old_net=stats([r['old_error_ns'] for r in selected]),new_net=stats([r['new_error_ns'] for r in selected]),
                old_output=stats([r['old_output_error_ns'] for r in selected]),new_output=stats([r['new_output_error_ns'] for r in selected]))
with (reports/'oof-connections.tsv').open('w') as f:
    w=csv.DictWriter(f,fieldnames=list(evaluations[0]),delimiter='\t');w.writeheader();w.writerows(evaluations)
(reports/'calibration.json').write_text(json.dumps(summary,indent=2)+'\n')
(inputs/'calibrate_sll_output_tail.py').write_bytes(Path(__file__).read_bytes())
manifest={'inputs':{},'threshold_preselected_ns':1.5,'coefficient_count':1,'created_epoch':time.time()}
for path in [source,a.previous_predictions/'reports/connection-predictions.tsv',Path(__file__)]:
    manifest['inputs'][str(path.resolve())]=hashlib.sha256(path.read_bytes()).hexdigest()
(a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(a.output/'status.json').write_text(json.dumps(dict(status='completed',elapsed_seconds=time.time()-start))+'\n')
print(json.dumps({k:v for k,v in summary.items() if k!='groups'},indent=2))
for name in ['fixed/stratified/all','free/stratified/all','fixed/original_critical/all']:
    print(name,summary['groups'][name])
