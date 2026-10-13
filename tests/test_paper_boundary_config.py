import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from paper_boundary_config import validate_paper_boundary_binary

class ConfigTest(unittest.TestCase):
    cfg = dict(BoundaryClusteringStrategy='paper-hierarchical',BoundaryAwareClustering='true',PhysicalBoundaryMode='true')
    def test_legacy_does_not_query_binary(self):
        with patch('paper_boundary_config.subprocess.run') as run:
            validate_paper_boundary_binary({}, '/unused'); run.assert_not_called()
    def test_reject_old_binary_and_bad_output(self):
        for output in ('{}','not-json','null',json.dumps({'paper_hierarchical_boundaries_schema':1})):
            with patch('paper_boundary_config.subprocess.run',return_value=SimpleNamespace(returncode=0,stdout=output)):
                with self.assertRaisesRegex(ValueError,'silent fallback'):
                    validate_paper_boundary_binary(self.cfg,'/old')
    def test_accept_capable_binary(self):
        with patch('paper_boundary_config.subprocess.run',return_value=SimpleNamespace(returncode=0,stdout=json.dumps({'paper_hierarchical_boundaries_schema':2}))):
            validate_paper_boundary_binary(self.cfg,'/new')
    def test_reject_disabled_mode_or_misspelled_strategy(self):
        for cfg in (dict(self.cfg,PhysicalBoundaryMode='false'),dict(self.cfg,BoundaryAwareClustering='false'),dict(self.cfg,BoundaryClusteringStrategy='paper')):
            with self.assertRaises(ValueError):validate_paper_boundary_binary(cfg,'/unused')

if __name__ == '__main__': unittest.main()
