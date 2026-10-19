"""Verificação de firewall somente leitura para cópias de teste, sem janelas ou elevação.

approved_udp_program(exe) retorna bool e escreve a razão da recusa em stderr.
check_udp_program(exe) devolve o resultado e a razão sem imprimir nada.
Não cria regras, não altera perfis e nunca inicia o executável informado.
"""
from __future__ import annotations

import base64
from dataclasses import dataclass
import json
import os
from pathlib import Path
import subprocess
import sys
from typing import Any


@dataclass(frozen=True)
class PreflightResult:
    approved: bool
    reason: str


# O caminho é passado por variável de ambiente, sem interpolação no código PowerShell.
# Todos os cmdlets usados são Get-*, exclusivamente no ActiveStore.
_QUERY = r"""
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
try {
    $program = $env:MC3COOP_FIREWALL_PREFLIGHT_EXE
    $rows = @(
        foreach ($app in @(Get-NetFirewallApplicationFilter -PolicyStore ActiveStore)) {
            if ([Environment]::ExpandEnvironmentVariables([string]$app.Program) -ine $program) { continue }
            foreach ($rule in @(Get-NetFirewallRule -AssociatedNetFirewallApplicationFilter $app -PolicyStore ActiveStore)) {
                $port = @(Get-NetFirewallPortFilter -AssociatedNetFirewallRule $rule)
                $address = @(Get-NetFirewallAddressFilter -AssociatedNetFirewallRule $rule)
                if ($port.Count -ne 1 -or $address.Count -ne 1) { throw 'Filtros de firewall incompletos ou ambíguos.' }
                [pscustomobject]@{
                    Program = [string]$app.Program
                    Enabled = [string]$rule.Enabled
                    Direction = [string]$rule.Direction
                    Action = [string]$rule.Action
                    Profile = [string]$rule.Profile
                    Protocol = [string]$port[0].Protocol
                    LocalPort = @($port[0].LocalPort | ForEach-Object { [string]$_ })
                    RemotePort = @($port[0].RemotePort | ForEach-Object { [string]$_ })
                    LocalAddress = @($address[0].LocalAddress | ForEach-Object { [string]$_ })
                    RemoteAddress = @($address[0].RemoteAddress | ForEach-Object { [string]$_ })
                }
            }
        }
    )
    ConvertTo-Json -Compress -Depth 5 -InputObject $rows
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
"""


def _canonical_program(exe: os.PathLike[str] | str) -> str:
    path = Path(os.path.expandvars(os.fspath(exe)))
    if not path.is_absolute():
        raise ValueError("o caminho do executável precisa ser absoluto")
    path = path.resolve(strict=True)
    if not path.is_file() or path.suffix.casefold() != ".exe":
        raise ValueError("o caminho não aponta para um arquivo .exe")
    return os.path.normcase(str(path))


def _token(value: Any) -> str:
    return str(value).strip().casefold()


def _any_filter(value: Any) -> bool:
    if isinstance(value, list):
        return len(value) == 1 and _token(value[0]) == "any"
    return isinstance(value, str) and _token(value) == "any"


def _evaluate(program: str, rows: list[Any]) -> PreflightResult:
    if any(not isinstance(row, dict) for row in rows):
        return PreflightResult(False, "a consulta retornou uma regra inválida")
    matching = []
    for row in rows:
        try:
            matches = _canonical_program(row.get("Program", "")) == program
        except (OSError, ValueError, TypeError):
            matches = False
        if not matches:
            continue
        matching.append(row)
    # Explicit blocks take precedence over allow rules; fail closed regardless of enumeration order.
    for row in matching:
        if (_token(row.get("Enabled")) in {"true", "1"}
                and _token(row.get("Direction")) in {"inbound", "1"}
                and _token(row.get("Action")) in {"block", "4"}
                and _token(row.get("Protocol")) in {"udp", "17", "any", "256"}):
            return PreflightResult(False, "existe um bloqueio de entrada para este executável")
    for row in matching:
        if (_token(row.get("Enabled")) not in {"true", "1"}
                or _token(row.get("Direction")) not in {"inbound", "1"}
                or _token(row.get("Action")) not in {"allow", "2"}
                or _token(row.get("Profile")) not in {"any", "0"}
                or _token(row.get("Protocol")) not in {"udp", "17", "any", "256"}):
            continue
        if all(_any_filter(row.get(key)) for key in
               ("LocalPort", "RemotePort", "LocalAddress", "RemoteAddress")):
            return PreflightResult(True, "regra de entrada UDP para este executável encontrada no ActiveStore")
    return PreflightResult(False, "não há regra ActiveStore para este executável com entrada habilitada, "
                           "ação Allow, perfil Any, protocolo UDP/Any e portas/endereços Any")


def _query(program: str) -> list[Any]:
    if os.name != "nt":
        raise OSError("esta verificação requer o Firewall do Windows")
    powershell = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "WindowsPowerShell" / "v1.0" / "powershell.exe"
    encoded = base64.b64encode(_QUERY.encode("utf-16-le")).decode("ascii")
    env = dict(os.environ, MC3COOP_FIREWALL_PREFLIGHT_EXE=program)
    result = subprocess.run(
        [str(powershell), "-NoLogo", "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded],
        env=env, capture_output=True, timeout=30, check=False,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    if result.returncode:
        raise OSError(f"consulta do ActiveStore falhou (código {result.returncode})")
    rows = json.loads(result.stdout.decode("utf-8-sig"))
    if not isinstance(rows, list):
        raise ValueError("a consulta não retornou uma lista de regras")
    return rows


def check_udp_program(exe: os.PathLike[str] | str) -> PreflightResult:
    """Não imprime nem modifica nada. Qualquer falha impede a aprovação."""
    try:
        program = _canonical_program(exe)
        return _evaluate(program, _query(program))
    except (OSError, ValueError, TypeError, subprocess.SubprocessError) as error:
        return PreflightResult(False, f"não foi possível conferir o firewall: {error}")


def approved_udp_program(exe: os.PathLike[str] | str) -> bool:
    """Retorna False e a razão em stderr; nunca mostra diálogo nem pede elevação."""
    result = check_udp_program(exe)
    if not result.approved:
        print(f"Teste não iniciado: {result.reason}.", file=sys.stderr)
    return result.approved


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Uso: python tools/firewall_preflight.py CAMINHO_ABSOLUTO_EXE", file=sys.stderr)
        raise SystemExit(2)
    raise SystemExit(0 if approved_udp_program(sys.argv[1]) else 1)
