"""Build the regression against frozen production libraries without editing their snapshot."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('--build', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
source = Path(__file__).resolve().parents[2] / 'src/tests/check_and2b1l_timing.cc'
commands = subprocess.check_output(['ninja', '-t', 'commands', 'checkFFControlPolarity'], cwd=a.build, text=True).splitlines()
compile_command = next(c for c in commands if ' -c ' in c and 'check_ff_control_polarity.cc' in c)
argv = shlex.split(compile_command)
argv[argv.index('-c') + 1] = str(source)
old_object = argv[argv.index('-o') + 1]
obj = a.output.resolve() / 'check.o'
argv[argv.index('-o') + 1] = str(obj)
if '-MF' in argv:
    argv[argv.index('-MF') + 1] = str(a.output.resolve() / 'check.d')
link = next(c for c in commands if ' -o checkFFControlPolarity ' in c)
exe = a.output.resolve() / 'checkAND2B1LTiming'
link = link.replace(old_object, shlex.quote(str(obj))).replace(' -o checkFFControlPolarity ', ' -o ' + shlex.quote(str(exe)) + ' ')
start = time.monotonic()
subprocess.run(argv, cwd=a.build, check=True)
subprocess.run(['bash', '-c', link], cwd=a.build, check=True)
(a.output / 'manifest.json').write_text(json.dumps({
    'production_build': str(a.build.resolve()),
    'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'compile': argv, 'link': link, 'wall_seconds': time.monotonic() - start,
    'binary_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
}, indent=2) + '\n')
