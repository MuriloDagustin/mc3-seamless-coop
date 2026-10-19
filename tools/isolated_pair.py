"""Two muted disposable test copies on an invisible desktop; closes only its own Job Object.
Does not touch another controller's stop file, saves or executables. No Windows keyboard input.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import socket
import time

from ctl import Copy
from isolated_job import IsolatedJob
from firewall_preflight import approved_udp_program

ROOT = Path(__file__).resolve().parents[1]
DATA = Path(r'D:\dev\mc3-coop-data')
GAME = DATA / 'testgame' / 'Midnight Club 3 DUB Edition Remix'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name', default='pair-' + str(time.time_ns()))
    parser.add_argument('--seconds', type=int, default=900)
    parser.add_argument('--finish-gate', choices=('0', '1'))
    parser.add_argument('--boot', action='store_true', help='navigate own copies with internal virtual pad to free roam')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,80}', args.name):
        parser.error('run name must be a single directory name')
    run = ROOT / 'out' / 'isolated-pairs' / args.name
    run.mkdir(parents=True, exist_ok=False)  # never reuse another controller's checkout/saves
    assets = ROOT / 'out' / 'live-isolated' / 'assets'
    if not (assets / 'default.xbe').is_file():
        shutil.copytree(DATA / 'assets', assets, dirs_exist_ok=True)
    sources = sorted((p for p in GAME.glob('*.exe') if p.stat().st_size >= 20 << 20), key=lambda p: p.stat().st_mtime)
    if not sources:
        raise RuntimeError('Prepare the testgame runner first with tools/test_launcher.py')
    # Reuse one executable path: changing it on each run causes repeated Windows Firewall prompts.
    stable = ROOT / 'out' / 'live-isolated'
    exe = stable / 'MC3Runner.exe'
    source = sources[-1]
    if not exe.exists() or (exe.stat().st_size,exe.stat().st_mtime_ns)!=(source.stat().st_size,source.stat().st_mtime_ns):
        shutil.copy2(source,exe)
    # Never offer elevation or create rules. No existing broad UDP allow rule => no game starts.
    if not approved_udp_program(exe):
        raise SystemExit('Teste não iniciado: sem regra UDP já autorizada para o runner. Firewall não foi alterado; nenhuma permissão foi solicitada.')
    for dll in (ROOT / 'out' / 'mod').glob('*.dll'):
        shutil.copy2(dll, stable / dll.name)
    for i in (3, 4):
        shutil.copytree(DATA / 'xsave' / f'i{i}', run / 'saves' / f'i{i}')
    for port in (45803, 45804, 45520):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(('127.0.0.1', port))
    stop = run / ('stop-' + str(os.getpid()))
    def pause(seconds):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if stop.exists():raise InterruptedError('Parada solicitada ao controlador próprio')
            time.sleep(min(.25,max(0,end-time.monotonic())))
    copies = [Copy(port, timeout=3) for port in (45803, 45804)]
    with IsolatedJob() as job:
        processes = []
        for i, port in ((3, 45803), (4, 45804)):
            env = dict(os.environ, MC3COOP_GAME=str(GAME), MC3COOP_MUTE='1', MC3COOP_INSTANCE=str(i),
                       MC3COOP_CONTROL=str(port), MC3COOP_COOP='host' if i == 3 else 'join:127.0.0.1',
                       MC3COOP_PORT='45520', MC3COOP_NICK='TesteHost' if i == 3 else 'TesteParceiro',
                       XRECOMP_NO_DIALOGS='1', XRECOMP_LOG_DIR=str(run / 'logs' / f'runner-{i}'),
                       XRECOMP_GPU_BACKEND='d3d12', XRECOMP_PRESENT_MODE='immediate',
                       XRECOMP_FULLSCREEN='0', XRECOMP_BORDERLESS='0',
                       XRECOMP_NATIVE_CONTROLLERS='0', XRECOMP_KEYBOARD='1', XRECOMP_AUDIO_SRC_GAIN='0',
                       SDL_AUDIODRIVER='dummy', SDL_AUDIO_DRIVER='dummy',
                       XRECOMP_FPS_LIMIT='30', XRECOMP_FRAME_LIMIT='30', XRECOMP_MAX_FPS='30')
            env.pop('MC3COOP_FINISH_GATE', None)
            if args.finish_gate is not None:
                env['MC3COOP_FINISH_GATE'] = args.finish_gate
            processes.append(job.start(exe, ['--xbe', assets / 'default.xbe', '--game-dir', assets,
                                            '--save-name', run / 'saves' / f'i{i}'], env))
        metadata = dict(controller_pid=os.getpid(), processes=[dict(pid=p.pid, port=c.port) for p, c in zip(processes, copies)],
                        stop=str(stop), desktop=job.name, run=str(run))
        (run / 'owned-processes.json').write_text(json.dumps(metadata, indent=2))
        print(json.dumps(metadata), flush=True)
        began = time.monotonic()
        if args.boot:
            pause(25)
            for n in range(7):
                for c in copies:
                    if stop.exists():raise InterruptedError('Parada solicitada ao controlador próprio')
                    c.command('pad start 250')
                print('boot start', n, flush=True)
                pause(2.5)
            for n in range(35):
                ready = []
                for c in copies:
                    if stop.exists():raise InterruptedError('Parada solicitada ao controlador próprio')
                    state = c.command('state')
                    done = 'cruise=1' in state and 'race_state=5 ' in state and 'car=1 ' in state
                    ready.append(done)
                    if not done:
                        c.command('pad a 250')
                if all(ready):
                    print('PASS: both copies ready in free roam', flush=True)
                    break
                print('boot career', n, flush=True)
                pause(4)
            else:
                raise TimeoutError('Disposable copies did not reach free roam')
        while time.monotonic() - began < args.seconds and not stop.exists():
            for p in processes:
                code = job.exit_code(p)
                if code != 259:
                    raise RuntimeError(f'Own test process {p.pid} exited {code:#x}')
            time.sleep(0.5)
    print('Closed only this Job Object, pids', [p.pid for p in processes], flush=True)


if __name__ == '__main__':
    main()
