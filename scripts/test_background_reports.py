import json,tempfile,unittest
from pathlib import Path
from unittest.mock import patch,Mock
from run_auxiliary_reports import dispatch_after_completion
from run_dfx import save

class BackgroundReports(unittest.TestCase):
    def test_requires_completed_parent_and_does_not_wait_or_edit_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);parent=root/'manifest.json'
            save(parent,dict(state='running'))
            with self.assertRaises(ValueError):dispatch_after_completion(root/'joint',root/'reports',parent)
            self.assertFalse((root/'reports').exists())
            save(parent,dict(state='completed',ended_epoch=1,wall_seconds=25))
            before=parent.read_bytes();child=Mock(pid=123)
            with patch('run_auxiliary_reports.subprocess.Popen',return_value=child) as spawn:
                result=dispatch_after_completion(root/'joint',root/'reports',parent)
            self.assertEqual(result['state'],'dispatched')
            self.assertEqual(result['main_wall_seconds'],25)
            self.assertEqual(parent.read_bytes(),before)
            self.assertTrue(spawn.call_args.kwargs['start_new_session'])
            child.wait.assert_not_called();child.communicate.assert_not_called()

    def test_report_dispatch_failure_preserves_accepted_result(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);parent=root/'manifest.json'
            save(parent,dict(state='completed',ended_epoch=1,wall_seconds=25));before=parent.read_bytes()
            with patch('run_auxiliary_reports.subprocess.Popen',side_effect=OSError('deliberate launch failure')):
                result=dispatch_after_completion(root/'joint',root/'reports',parent)
            self.assertEqual(result['state'],'dispatch_failed')
            self.assertEqual(parent.read_bytes(),before)
            self.assertIn('deliberate launch failure',json.loads((root/'reports/dispatch.json').read_text())['error'])

if __name__=='__main__':unittest.main()
