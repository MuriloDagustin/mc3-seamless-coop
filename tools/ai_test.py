"""AI opponents in the shared race: each opponent's position on the host vs on the partner, a few times.
Run while both copies race the same career race (tools/race_start_test.py).
python tools/ai_test.py [rounds]
"""
import math
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ctl import Copy  # noqa: E402
from race_finish_test import r32  # noqa: E402


def opponents(c):
    mgr = r32(c, 0x534074)
    n, nc, array = r32(c, mgr), r32(c, mgr + 4), r32(c, mgr + 8)
    out = []
    for i in range(min(nc, 8)):
        vehicle = r32(c, array + i * 0x43C + 0x20)
        inst = r32(c, vehicle + 0x1C)
        out.append(tuple(float(v) for v in c.command(f"peekf {inst + 0x34:08X} 3").split()))
    return out


def main():
    rounds = int(sys.argv[1]) if len(sys.argv) > 1 else 4
    h, p = Copy(45703, timeout=20), Copy(45704, timeout=20)
    for k in range(rounds):
        a, b = opponents(h), opponents(p)
        d = [math.dist(x, y) for x, y in zip(a, b)]
        moved = math.dist(a[0], (0, 0, 0)) if a else 0
        print(f"round {k}: {len(a)} opponents, distance host/partner per car: {[round(x, 1) for x in d]}")
        print("   partner", p.command("state").split("ai=")[1] if "ai=" in p.command("state") else "?")
        time.sleep(3)


if __name__ == "__main__":
    main()
