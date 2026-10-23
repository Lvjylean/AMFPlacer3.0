import tempfile
from pathlib import Path
import unittest
from dfx_static_membership_names import classify, run


class MembershipTests(unittest.TestCase):
    def test_direct_missing_and_dynamic(self):
        self.assertEqual(classify(['s/a', 's/b', 'rp/a'], {'s/a'}, set(), ['rp']),
                         (2, ['s/b'], []))

    def test_hierarchy_and_prefix_boundaries(self):
        self.assertEqual(classify(['s/a/x', 's/a/y', 's/a2/z'], {'s/a'}, {'s/a'}, []),
                         (3, ['s/a2/z'], []))

    def test_primitive_prefix_does_not_own_descendant(self):
        self.assertEqual(classify(['s/a', 's/a/x'], {'s/a'}, set(), []),
                         (2, ['s/a/x'], []))

    def test_dynamic_in_static_hierarchy_is_error(self):
        self.assertEqual(classify(['top/rp/x', 'top/static/y'], {'top'}, {'top'}, ['top/rp']),
                         (1, [], ['top/rp/x']))

    def test_literal_brackets_and_rp_prefix(self):
        self.assertEqual(classify(['top/a[0]/x', 'top/a[1]/x', 'top/rp2/x'],
                                 {'top/a[0]'}, {'top/a[0]'}, ['top/rp']),
                         (3, ['top/a[1]/x', 'top/rp2/x'], []))

    def test_empty_files_and_duplicate_rejection(self):
        with tempfile.TemporaryDirectory() as tmp:
            prefix = Path(tmp) / 'names'
            for tag in ('primitives', 'direct', 'hierarchy', 'rps'):
                Path(str(prefix) + '_' + tag + '.txt').write_text('\n')
            self.assertEqual(run(prefix)['missing'], 0)
            Path(str(prefix) + '_primitives.txt').write_text('s/a\ns/a\n')
            with self.assertRaises(ValueError):
                run(prefix)


if __name__ == '__main__':
    unittest.main()
