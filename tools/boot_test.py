"""One hidden, muted copy for a while: buttons now and then, draws counted, a picture at the end, the crash
explained if it falls. Only the copy it starts is ever closed (testcopies.py, by its own handle).

python tools/boot_test.py [seconds] [--press a,start] [--every 5]
"""
import argparse
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ctl import Copy  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
RUN = Path(r"D:\dev\mc3-coop-data\run")
PORT = 45703


def draws(copy):
    """Draws in the next frame (a raw capture's frame.txt), or -1."""
    folder = RUN / "captures" / "i3"
    before = set(folder.glob("*")) if folder.exists() else set()
    copy.command("shot")
    end = time.time() + 10
    while time.time() < end:
        new = [d for d in folder.glob("*") if d not in before and (d / "final.bmp").exists()]
        if new:
            time.sleep(0.5)
            n = sum(1 for line in (new[0] / "frame.txt").open() if " draw" in line)
            for f in new[0].glob("*"):
                f.unlink()
            new[0].rmdir()
            return n
        time.sleep(0.2)
    return -1


def main():
    p = argparse.ArgumentParser()
    p.add_argument("seconds", type=int, nargs="?", default=90)
    p.add_argument("--press", default="a,start")
    p.add_argument("--every", type=float, default=5)
    p.add_argument("--name", default="boot")
    args = p.parse_args()
    launcher = subprocess.Popen([sys.executable, str(ROOT / "tools" / "testcopies.py"), "--copies", "1",
                                 "--seconds", str(args.seconds + 30)], stdout=subprocess.PIPE, text=True)
    copy = Copy(PORT, timeout=2)
    start = time.time()
    buttons = args.press.split(",")
    i = 0
    alive = True
    while time.time() - start < args.seconds:
        time.sleep(args.every)
        if launcher.poll() is not None:
            break
        try:
            copy.command(f"pad {buttons[i % len(buttons)]} 200")
            i += 1
            print(f"{time.time() - start:5.0f}s {copy.command('ping')} draws={draws(copy)}", flush=True)
        except (socket.timeout, ConnectionResetError) as e:
            print(f"{time.time() - start:5.0f}s no answer ({e.__class__.__name__})", flush=True)
    try:
        print(copy.shot(args.name))
        print(copy.command("state"))
    except Exception as e:
        alive = False
        print("no picture:", e)
    (RUN / "stop-tests").write_text("stop")
    out, _ = launcher.communicate(timeout=60)
    print(out.strip())
    if "exit" in out or not alive:
        subprocess.run([sys.executable, str(ROOT / "tools" / "crash.py")])


if __name__ == "__main__":
    main()
