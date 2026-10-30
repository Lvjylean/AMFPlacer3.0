"""Run only a configured preview. Parent-channel loss also closes the service."""
import json
import os
from pathlib import Path
import runpy
import sys
ROOT=Path(__file__).resolve().parent
spec=json.loads((ROOT/'remote-config.json').read_text())[sys.argv[1]]
owner=json.loads(sys.stdin.readline())
token=owner.get('token','')
if len(token)!=64 or any(c not in '0123456789abcdef' for c in token):raise ValueError('Invalid owner token')
os.environ['AMF_VIEWER_OWNER_TOKEN']=token
os.environ['AMF_VIEWER_WATCH_STDIN']='1'
sys.argv=[str(ROOT/'placement_viewer.py'),*spec['args']]
runpy.run_path(sys.argv[0],run_name='__main__')
