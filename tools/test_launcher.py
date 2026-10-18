"""Extract the actual private ZIP and test its launcher, muted on an invisible desktop.
Only our Job Object owns/cleans this test tree. Exit2 means the duplicate guard blocked gameplay, NOT a pass.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import time
import zipfile

from ctl import Copy
from isolated_job import IsolatedJob

ROOT = Path(__file__).resolve().parents[1]
DATA = Path(r'D:\dev\mc3-coop-data')
GAME = DATA / 'testgame' / 'Midnight Club 3 DUB Edition Remix'
REAL = Path(r'D:\Jogos\xbox-xrevive\Midnight Club 3 DUB Edition Remix')


def setup(archive):
    GAME.mkdir(parents=True, exist_ok=True)
    runners = sorted((p for p in REAL.glob('*.exe') if p.stat().st_size >= 20 << 20),
                     key=lambda p: p.stat().st_mtime)
    if not runners:
        raise RuntimeError('No runner source found (read-only installation)')
    source = runners[-1]
    target = GAME / source.name
    if not target.exists() or (target.stat().st_size, target.stat().st_mtime_ns) != (source.stat().st_size, source.stat().st_mtime_ns):
        shutil.copy2(source, target)
    if not (GAME / 'extracted' / 'default.xbe').is_file():
        shutil.copytree(DATA / 'assets', GAME / 'extracted', dirs_exist_ok=True)
    saves = GAME / 'SaveData' / 'Midnight Club 3_ DUB edition'
    if not saves.exists():
        shutil.copytree(DATA / 'xsave' / 'i3', saves)
    # Unique folder, never recursively deletes a previous test or running executable.
    pkg = ROOT / 'out' / 'launcher-tests' / str(time.time_ns())
    pkg.mkdir(parents=True)
    with zipfile.ZipFile(archive) as z:
        for entry in z.infolist():
            if not (pkg / entry.filename).resolve().is_relative_to(pkg.resolve()):
                raise RuntimeError('ZIP path outside test folder')
        z.extractall(pkg)
    exe = pkg / 'MC3Coop' / 'MC3Coop.exe'
    # No UAC or firewall change in the movie session. Unit tests cover the real offer code.
    (exe.parent / 'MC3Coop.ini').write_text(
        f'[launcher]\ngame={GAME}\nfirewallAsked=1\nmissingOnlineExplained=1\n', encoding='utf-16')
    return exe, source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('seconds', nargs='?', type=int, default=90)
    parser.add_argument('--zip', type=Path, default=ROOT / 'out' / 'MC3Coop-test.zip')
    parser.add_argument('--logs-only', action='store_true', help='test the packaged --logs executable without opening a game')
    args = parser.parse_args()
    port = 45809
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(('127.0.0.1', port))
    exe, source = setup(args.zip)
    env = dict(os.environ, MC3COOP_GAME=str(GAME), MC3COOP_CONTROL=str(port), MC3COOP_MUTE='1',
               MC3COOP_INSTANCE='9', XRECOMP_NO_DIALOGS='1', XRECOMP_FULLSCREEN='0', XRECOMP_BORDERLESS='0',
               XRECOMP_PRESENT_MODE='immediate', XRECOMP_AUDIO_SRC_GAIN='0', SDL_AUDIODRIVER='dummy',
               SDL_AUDIO_DRIVER='dummy', XRECOMP_FPS_LIMIT='30', XRECOMP_FRAME_LIMIT='30', XRECOMP_MAX_FPS='30')
    env.pop('MC3COOP_COOP', None)
    evidence = {'archive': str(args.zip), 'runner_source': source.name, 'package': str(exe.parent)}
    with IsolatedJob() as job:
        if args.logs_only:
            (exe.parent / 'logs' / 'runner').mkdir(parents=True, exist_ok=True)
            (exe.parent / 'logs' / 'smoke.log').write_text('mod diagnostic smoke')
            (exe.parent / 'logs' / 'runner' / 'smoke.log').write_text('runner diagnostic smoke')
            process = job.start(exe, ['--logs'], env)
            for _ in range(120):
                messages = job.messages(process)
                paths = [Path(m.split('\n')[-1].strip()) for m in messages if 'Logs reunidos.' in m]
                if paths:
                    break
                time.sleep(0.25)
            else:
                raise TimeoutError('No --logs success dialog: ' + repr(messages))
            archive = paths[0]
            if not archive.name.startswith('MC3Coop-logs-') or archive.suffix != '.zip':
                raise RuntimeError('Unexpected diagnostic path')
            with zipfile.ZipFile(archive) as z:
                names = z.namelist()
                assert 'logs/smoke.log' in names and 'logs/runner/smoke.log' in names
                assert not any(n.lower().endswith('.toml') for n in names)
                assert z.testzip() is None
            destination = exe.parent / 'collected-smoke-logs.zip'
            shutil.copy2(archive, destination)
            archive.unlink()  # only the exact archive this test just created
            evidence.update(result='PASS_PACKAGED_LOGS', entries=names, collected=str(destination))
            (exe.parent / 'logs-smoke-evidence.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
            print(json.dumps(evidence), flush=True)
            return
        process = job.start(exe, env=env)
        evidence.update(launcher_pid=process.pid, desktop=job.name)
        print(json.dumps(evidence), flush=True)
        copy = Copy(port, timeout=1)
        end = time.monotonic() + max(20, args.seconds)
        while time.monotonic() < end:
            messages = job.messages(process)
            if any('já está aberto' in m or 'possível runner aberto' in m for m in messages):
                evidence.update(result='BLOCKED_EXISTING_PROCESS', messages=messages)
                break
            if messages:
                raise RuntimeError('Unexpected launcher dialog: ' + repr(messages))
            try:
                if copy.command('ping') == 'pong':
                    break
            except (socket.timeout, ConnectionResetError):
                pass
            if job.exit_code(process) not in (0, 259):
                raise RuntimeError(f'Launcher exited {job.exit_code(process)}')
            time.sleep(0.5)
        else:
            raise TimeoutError('Packaged mod did not open its control channel')
        if evidence.get('result') != 'BLOCKED_EXISTING_PROCESS':
            time.sleep(4)
            evidence['state'] = copy.command('state')
            copy.command('panel toggle')  # same panel as F10, NO Windows keyboard messages
            time.sleep(2)
            shot = exe.parent / 'panel.bmp'
            copy.sock.settimeout(12)
            copy.command(f'shot {shot}')
            evidence.update(result='PASS_MOD_AND_PANEL', capture=str(shot))
        (exe.parent / 'test-evidence.json').write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf-8')
        print(json.dumps(evidence, ensure_ascii=False), flush=True)
    raise SystemExit(2 if evidence['result'] == 'BLOCKED_EXISTING_PROCESS' else 0)


if __name__ == '__main__':
    main()
