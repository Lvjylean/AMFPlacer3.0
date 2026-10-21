#!/usr/bin/env python3
"""Check launcher dispatch/default compatibility without starting a placer."""
import ast
import json
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
from paper_boundary_config import boundary_clustering_strategy, validate_paper_boundary_binary

class StrategyConfigTests(unittest.TestCase):
    def setUp(self):
        self.cfg={'BoundaryAwareClustering':'true','PhysicalBoundaryMode':'true'}
        self.cap={'paper_hierarchical_boundaries_schema':2,'paper_boundary_stage_mode_schema':1,
                  'paper_region_clipping_toggle_schema':1,'boundary_clustering_strategy_schema':1,
                  'boundary_clustering_strategy_default':'slr-hierarchical'}
    def reply(self,cap):
        return patch('paper_boundary_config.subprocess.run',
                     return_value=SimpleNamespace(returncode=0,stdout=json.dumps(cap)))
    def test_omitted_canonical_and_alias(self):
        for name in (None,'slr-hierarchical','paper-hierarchical'):
            c=dict(self.cfg)
            if name is not None:c['BoundaryClusteringStrategy']=name
            self.assertEqual(boundary_clustering_strategy(c),'slr-hierarchical')
            with self.reply(self.cap) as launch:
                validate_paper_boundary_binary(c,'unused')
                launch.assert_called_once()
    def test_legacy_is_explicit(self):
        c=dict(self.cfg,BoundaryClusteringStrategy='region-gain')
        self.assertEqual(boundary_clustering_strategy(c),'region-gain')
        with self.reply(self.cap) as launch:
            validate_paper_boundary_binary(c,'unused')
            launch.assert_not_called()
    def test_disabled_is_inert(self):
        for name in (None,'slr-hierarchical','paper-hierarchical','cr-slr-hierarchical','region-gain'):
            c={'BoundaryAwareClustering':'false','BoundaryStageMode':'merged'}
            if name is not None:c['BoundaryClusteringStrategy']=name
            with self.reply(self.cap) as launch:
                validate_paper_boundary_binary(c,'unused')
                launch.assert_not_called()
    def test_cr_strategy_requires_its_own_binary_capability(self):
        c=dict(self.cfg,BoundaryClusteringStrategy='cr-slr-hierarchical')
        self.assertEqual(boundary_clustering_strategy(c),'cr-slr-hierarchical')
        with self.reply(self.cap),self.assertRaisesRegex(ValueError,'cr-slr-hierarchical'):
            validate_paper_boundary_binary(c,'old')
        for mode in ('independent-max2','merged'):
            with self.reply(dict(self.cap,cr_slr_hierarchical_schema=1)):
                validate_paper_boundary_binary(dict(c,BoundaryStageMode=mode),'new')
    def test_old_binary_cannot_silently_use_old_default(self):
        cap=dict(self.cap);cap.pop('boundary_clustering_strategy_schema');cap.pop('boundary_clustering_strategy_default')
        with self.reply(cap):
            for c in (self.cfg,dict(self.cfg,BoundaryClusteringStrategy='slr-hierarchical')):
                with self.assertRaisesRegex(ValueError,'name/default'):
                    validate_paper_boundary_binary(c,'old')
            validate_paper_boundary_binary(dict(self.cfg,BoundaryClusteringStrategy='paper-hierarchical'),'old')
    def test_unknown_strategy_and_physical_gate(self):
        with self.assertRaisesRegex(ValueError,'Unknown BoundaryClusteringStrategy'):
            validate_paper_boundary_binary(dict(self.cfg,BoundaryClusteringStrategy='typo'),'unused')
        with self.assertRaisesRegex(ValueError,'PhysicalBoundaryMode'):
            validate_paper_boundary_binary({'BoundaryAwareClustering':'true'},'unused')
    def test_stage_and_clipping_capabilities(self):
        for name,key in [('BoundaryStageMode','paper_boundary_stage_mode_schema'),('BoundaryRegionClipping','paper_region_clipping_toggle_schema')]:
            cap=dict(self.cap);cap.pop(key)
            c=dict(self.cfg,**{name:'merged' if name=='BoundaryStageMode' else 'false'})
            with self.reply(cap),self.assertRaises(ValueError):
                validate_paper_boundary_binary(c,'old')
    def test_all_dfx_launchers_validate_without_explicit_stage_key(self):
        root=Path(__file__).resolve().parent
        for name in ['run_native_module.py','run_dfx.py','run_dfx_full.py','run_parallel_incremental.py']:
            tree=ast.parse((root/name).read_text())
            fn=next(n for n in ast.walk(tree) if isinstance(n,(ast.FunctionDef,ast.AsyncFunctionDef)) and
                    any(isinstance(x,ast.ImportFrom) and x.module=='paper_boundary_config' for x in n.body))
            nodes=[n for n in fn.body if isinstance(n,ast.ImportFrom) and n.module=='paper_boundary_config' or
                   isinstance(n,ast.Expr) and isinstance(n.value,ast.Call) and isinstance(n.value.func,ast.Name) and n.value.func.id=='validate_paper_boundary_binary']
            self.assertEqual(len(nodes),2,name)
            with patch('paper_boundary_config.validate_paper_boundary_binary') as guard:
                exec(compile(ast.Module(body=nodes,type_ignores=[]),name,'exec'),
                     {'cfg':dict(self.cfg),'binary':'sentinel','args':SimpleNamespace(binary='sentinel')})
                guard.assert_called_once_with(self.cfg,'sentinel')

if __name__=='__main__':unittest.main()

