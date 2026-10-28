import sys
from pathlib import Path
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/diagnostics'))
import numpy as np
from fit_sa_ratio import matrix, nuisance_keys, ratio_of, robust_fit, split_driver


class RatioFitTests(unittest.TestCase):
    def test_recovers_directional_ratio_with_fanout_and_group_offsets(self):
        rng=np.random.default_rng(47);rows=[]
        for i in range(2000):
            dx,dy=rng.uniform(0,100),rng.uniform(0,220)
            group=('LUT-LUT','LUT-FF')[i%2];region=str(i%4);fo=1+i%8
            y=.01*dx+.007*dy+.09*np.log2(fo)+.4*(i%2)+.2*(i%4)+rng.normal(0,.02)
            if i%20==0:y+=2  # Routing-detour outliers should not dominate.
            rows.append(dict(dx=dx,dy=dy,fo=fo,group=group,region=region,delay=y))
        beta=robust_fit(matrix(rows,nuisance_keys(rows)),np.array([r['delay'] for r in rows]))
        self.assertAlmostEqual(ratio_of(beta),.7,delta=.015)

    def test_holdout_is_grouped_by_driver(self):
        names=['logic_'+str(i)+'/O' for i in range(1000)]
        selected=[n for n in names if split_driver(n)]
        self.assertTrue(150<len(selected)<250)
        self.assertEqual(selected,[n for n in names if split_driver(n)])

    def test_unidentified_ratio_rejected(self):
        for beta in ([0,1],[-1,1],[1,-1]):
            with self.assertRaises(ValueError):ratio_of(beta)


if __name__=='__main__':unittest.main()
