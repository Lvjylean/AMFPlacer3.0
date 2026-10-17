"""Reject a misleading positive-slack report whose clock coverage was lost."""
import re
from pathlib import Path
from run_dfx import sha

FIELDS = ('no_clock', 'unconstrained_internal_endpoints')

def counts(text):
    result = {}
    for field in FIELDS:
        values = {int(x) for x in re.findall(r'checking '+field+r' \((\d+)\)', text)}
        if len(values) != 1:
            raise ValueError('Missing/inconsistent timing coverage count: '+field)
        result[field] = values.pop()
    return result

def verify(report, contract):
    reference = Path(contract['path'])
    if sha(reference) != contract['sha256']:
        raise ValueError('Timing coverage baseline changed')
    baseline = counts(reference.read_text())
    actual = counts(Path(report).read_text())
    if any(actual[k] > baseline[k] for k in FIELDS):
        raise ValueError('Timing coverage regression: '+str(actual)+' baseline='+str(baseline))
    return dict(passed=True, actual=actual, baseline=baseline, reference=str(reference),
                reference_sha256=contract['sha256'])
