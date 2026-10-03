import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/diagnostics'))
from verify_minimap2_full_logic import bind_aliases
from verify_minimap2_logic_rewrites import Pool

class HierarchicalAliases(unittest.TestCase):
    def test_cross_module_alias_reaches_driver_and_parent(self):
        p=Pool();rendered={n:p.add('\t'+n+'\ts') for n in ['ram/reset','dma/reset_q','parent/reset']}
        bind_aliases(p,rendered,[('ram/reset','parent/reset'),('dma/reset_q','parent/reset')])
        self.assertEqual(p.root(rendered['ram/reset']),p.root(rendered['dma/reset_q']))

    def test_distinct_constants_cannot_be_shorted(self):
        with self.assertRaises(ValueError):bind_aliases(Pool(),{'gnd':0,'vcc':1},[('gnd','vcc')])

if __name__=='__main__':unittest.main()
