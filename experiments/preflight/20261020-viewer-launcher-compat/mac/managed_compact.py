import runpy,sys
from pathlib import Path
p=Path('/Users/jinyanglyu/Documents/ChatGPT/增量编译器和布尔处理器/AMFplacer3.0/experiments/preflight/20261024-viewer-session-lifecycle/managed_compact.py')
sys.path.insert(0,str(p.parent))
runpy.run_path(str(p),run_name='__main__')
