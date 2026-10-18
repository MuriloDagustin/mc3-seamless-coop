"""Explain the last guest fault in a test log: each stack return address -> function (+ XDK name).

python tools/crash.py [log]   (default D:\\dev\\mc3-coop-data\\run\\logs\\mc3-3.log)
"""
import bisect
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
log = Path(sys.argv[1] if len(sys.argv) > 1 else r"D:\dev\mc3-coop-data\run\logs\mc3-3.log")
db = {int(k, 16): v for k, v in json.loads((ROOT / "out" / "xref.json").read_text()).items()}
starts = sorted(db)
syms = {}
for line in (ROOT / "config" / "game.toml").open():
    m = re.match(r"^\s*([A-Za-z_0-9@?$]+)\s*=\s*0x([0-9A-F]+)", line)
    if m:
        syms[int(m.group(2), 16)] = m.group(1)


def owner(a):
    i = bisect.bisect_right(starts, a) - 1
    return starts[i] if i >= 0 else None


lines = log.read_text(errors="replace").splitlines()
first = next((i for i, l in enumerate(lines) if "error:" in l), None)
if first is None:
    print("no error in", log)
    sys.exit()
for l in lines[first:first + 4]:
    print(l)
seen = 0
for l in lines[first:]:
    m = re.search(r"\[esp\+([0-9A-F]+)\] ([0-9A-F]{8})", l)
    if not m:
        if seen and "exception" in l:
            break
        continue
    ra = int(m.group(2), 16)
    f = owner(ra)
    if f is None or ra - f > 0x4000:
        continue
    seen += 1
    print(f"  esp+{m.group(1):>3} {ra:08X} in sub_{f:08X} {syms.get(f, '')}")
    if seen == 1:
        for va, t in db[f]["asm"]:
            if ra - 0x30 <= va <= ra + 4:
                print(f"        {va:08X} {t}")
    if seen >= 12:
        break
