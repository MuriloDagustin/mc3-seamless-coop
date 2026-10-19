"""No game, socket, desktop or firewall query: refusal must happen before any process is created."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

import isolated_pair
import test_launcher


class LaunchGuards(unittest.TestCase):
    def test_package_refuses_before_job_or_dialog_when_not_preapproved(self):
        fake = Path(__file__).resolve().parent / 'fixture' / 'MC3Coop.exe'
        with patch.object(sys, 'argv', ['test_launcher.py']), \
                patch.object(test_launcher, 'setup', return_value=(fake, fake.with_name('source.exe'))), \
                patch.object(test_launcher.socket, 'socket', return_value=MagicMock()) as socket, \
                patch.object(test_launcher, 'approved_udp_program', return_value=False), \
                patch.object(test_launcher, 'IsolatedJob') as job:
            with self.assertRaises(SystemExit) as stopped:
                test_launcher.main()
            self.assertIn('não iniciado', str(stopped.exception))
            job.assert_not_called()
            socket.assert_not_called()

    def test_pair_reuses_runner_path_and_refuses_before_job_or_socket(self):
        out = Path(__file__).resolve().parents[1] / 'out' / 'launch-guard-tests'
        out.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=out) as folder:
            root = Path(folder)
            self.assertTrue(root.resolve().is_relative_to(out.resolve()))
            assets = root / 'out' / 'live-isolated' / 'assets'
            assets.mkdir(parents=True)
            (assets / 'default.xbe').write_text('fixture, never executed')
            game = root / 'fake-game'
            game.mkdir()
            with (game / 'new-runner.exe').open('wb') as f:
                f.truncate(20 << 20)  # not a PE; no possibility of executing a real game
            with patch.object(isolated_pair, 'ROOT', root), patch.object(isolated_pair, 'GAME', game), \
                    patch.object(isolated_pair, 'approved_udp_program', return_value=False) as check, \
                    patch.object(isolated_pair, 'IsolatedJob') as job, \
                    patch.object(isolated_pair.socket, 'socket') as socket:
                for name in ('one', 'two'):
                    with patch.object(sys, 'argv', ['isolated_pair.py', '--name', name]), \
                            self.assertRaises(SystemExit) as stopped:
                        isolated_pair.main()
                    self.assertIn('não iniciado', str(stopped.exception))
                expected = root / 'out' / 'live-isolated' / 'MC3Runner.exe'
                self.assertEqual([call.args[0] for call in check.call_args_list], [expected, expected])
                job.assert_not_called()
                socket.assert_not_called()


if __name__ == '__main__':
    unittest.main()
