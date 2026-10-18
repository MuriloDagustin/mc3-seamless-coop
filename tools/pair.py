"""Two connected test copies (host 45703, partner 45704) from a fresh start to the career's free roam (cruise), saves
xsave\\i3 and i4 past the first race. Stops this tool's earlier copies first (the stop file; only its own processes).
python tools/pair.py            # launches in the background and returns when both are in cruise
"""
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ctl import Copy  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
RUN = Path(r"D:\dev\mc3-coop-data\xrun")
PORTS = (45703, 45704)


def both(button, wait, ms=300):
    for p in PORTS:
        try:
            Copy(p).command(f"pad {button} {ms}")
        except Exception:
            pass
    time.sleep(wait)


def state(p):
    try:
        return Copy(p).command("state")
    except Exception:
        return ""


def main():
    (RUN / "stop-tests").write_text("stop")
    time.sleep(5)
    log = open(ROOT / "out" / "xrun2.log", "w")
    subprocess.Popen([sys.executable, str(ROOT / "tools" / "xrun.py"), "--copies", "2", "--seconds", "7200",
                      "--env1", "MC3COOP_COOP=host", "--env1", "MC3COOP_PORT=45420", "--env1", "MC3COOP_NICK=Murilo",
                      "--env2", "MC3COOP_COOP=join:127.0.0.1", "--env2", "MC3COOP_PORT=45420",
                      "--env2", "MC3COOP_NICK=Amigo"], stdout=log, stderr=subprocess.STDOUT)
    time.sleep(32)
    for _ in range(7):
        both("start", 2.5)
    time.sleep(8)
    both("a", 3)
    both("a", 40)
    for _ in range(8):
        if all("cruise=1" in state(p) for p in PORTS):
            break
        both("a", 3, 250)
    for p in PORTS:
        print(p, re.search(r"race_state=\d+ cruise=\d+", state(p) or "race_state=? cruise=?").group())


if __name__ == "__main__":
    main()
