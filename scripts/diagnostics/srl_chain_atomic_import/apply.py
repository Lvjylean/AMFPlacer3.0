"""Apply the validated optional SRL-chain import adapter to a prepared backend."""
from pathlib import Path
import argparse
from instrument_import import install as instrument
from srl_chain_import import install
if __name__ == '__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--backend',type=Path,required=True)
 a=p.parse_args();instrument(a.backend);print(install(a.backend))
