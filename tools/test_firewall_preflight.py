"""Testes mockados: não consulta o firewall e não executa PowerShell, launcher ou jogo."""
import contextlib
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import firewall_preflight as preflight


class FirewallPreflightTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).resolve().parent.parent / "out" / "firewall-preflight-tests"
        root.mkdir(parents=True, exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(dir=root)
        assert Path(cls.temp.name).resolve().is_relative_to(root.resolve())
        cls.exe = Path(cls.temp.name) / "pasta com acentos á e ' apóstrofo" / "MC3Runner.exe"
        cls.exe.parent.mkdir()
        cls.exe.write_bytes(b"fixture, never executed")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def row(self, **changes):
        row = dict(Program=str(self.exe), Enabled="True", Direction="Inbound", Action="Allow",
                   Profile="Any", Protocol="UDP", LocalPort=["Any"], RemotePort=["Any"],
                   LocalAddress=["Any"], RemoteAddress=["Any"])
        row.update(changes)
        return row

    def check(self, rows):
        with patch.object(preflight, "_query", return_value=rows) as query:
            result = preflight.check_udp_program(self.exe)
            query.assert_called_once_with(preflight._canonical_program(self.exe))
            return result

    def test_valid_udp_and_any_flags(self):
        for protocol in ("UDP", "17", "Any", "256"):
            with self.subTest(protocol=protocol):
                self.assertTrue(self.check([self.row(Protocol=protocol)]).approved)
        self.assertTrue(self.check([self.row(Enabled=True, Direction=1, Action=2, Profile=0)]).approved)

    def test_reject_restricted_or_disabled_rules(self):
        invalid = dict(Enabled="False", Direction="Outbound", Action="Block", Profile="Private",
                       Protocol="TCP", LocalPort=["45410"], RemotePort=["45410"],
                       LocalAddress=["LocalSubnet"], RemoteAddress=["192.168.1.0/24"])
        for field, value in invalid.items():
            with self.subTest(field=field):
                self.assertFalse(self.check([self.row(**{field: value})]).approved)
        for field in ("LocalPort", "RemotePort", "LocalAddress", "RemoteAddress"):
            self.assertFalse(self.check([self.row(**{field: ["Any", "extra"]})]).approved)
            self.assertFalse(self.check([self.row(**{field: None})]).approved)

    def test_exact_resolved_program_with_spaces(self):
        alias = self.exe.parent / ".." / self.exe.parent.name / self.exe.name
        self.assertTrue(self.check([self.row(Program=str(alias))]).approved)
        other = self.exe.with_name("OutroRunner.exe")
        other.write_bytes(b"fixture")
        self.assertFalse(self.check([self.row(Program=str(other))]).approved)
        self.assertFalse(self.check([self.row(Program="Any")]).approved)
        self.assertFalse(self.check([self.row(Program="MC3Runner.exe")]).approved)

    def test_empty_malformed_or_partial_rules_fail_closed(self):
        for rows in ([], [None], [{}], [self.row(Profile="Domain, Private, Public")]):
            self.assertFalse(self.check(rows).approved)
        self.assertTrue(self.check([self.row(Profile="Private"), self.row()]).approved)

    def test_explicit_block_or_malformed_response_wins_in_any_order(self):
        for rows in ([self.row(), self.row(Action="Block")],
                     [self.row(Action="Block"), self.row()], [self.row(), None]):
            self.assertFalse(self.check(rows).approved)

    def test_errors_fail_without_dialog(self):
        for error in (OSError("acesso negado"), ValueError("JSON inválido"),
                      subprocess.TimeoutExpired("powershell.exe", 30)):
            with self.subTest(error=type(error).__name__), patch.object(preflight, "_query", side_effect=error):
                output = io.StringIO()
                with contextlib.redirect_stderr(output):
                    self.assertFalse(preflight.approved_udp_program(self.exe))
                self.assertIn("Teste não iniciado", output.getvalue())

    def test_missing_or_relative_executable_never_queries(self):
        for exe in (self.exe.with_name("missing.exe"), "relative.exe", self.exe.parent):
            with patch.object(preflight, "_query") as query:
                self.assertFalse(preflight.check_udp_program(exe).approved)
                query.assert_not_called()

    def test_approval_is_silent(self):
        with patch.object(preflight, "_query", return_value=[self.row()]):
            output = io.StringIO()
            with contextlib.redirect_stderr(output):
                self.assertTrue(preflight.approved_udp_program(self.exe))
            self.assertEqual(output.getvalue(), "")

    def test_query_uses_hidden_noninteractive_powershell_with_env_path(self):
        # A chamada de processo também é interceptada; nenhum cmdlet é executado.
        result = subprocess.CompletedProcess([], 0, b"[]", b"")
        with patch.object(preflight.subprocess, "run", return_value=result) as run:
            self.assertEqual(preflight._query(str(self.exe)), [])
        args, kwargs = run.call_args
        self.assertIn("-NonInteractive", args[0])
        self.assertIn("-EncodedCommand", args[0])
        self.assertEqual(kwargs["env"]["MC3COOP_FIREWALL_PREFLIGHT_EXE"], str(self.exe))
        self.assertEqual(kwargs["creationflags"], subprocess.CREATE_NO_WINDOW)
        self.assertFalse(kwargs["check"])


if __name__ == "__main__":
    unittest.main()
