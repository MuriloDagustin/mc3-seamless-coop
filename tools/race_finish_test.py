"""Two connected copies in the same career race: the host's car is taken through every checkpoint to the finish
(place = memory teleport), then both copies are read: the host won, and the partner's copy counts the race as won
too (its local racer +0x5D set by the mod before the race ends; ReportRaceResult(0)).
python tools/race_finish_test.py [host_port] [partner_port]
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ctl import Copy  # noqa: E402

ENTRY, ENTRY_SIZE = 0x98, 0xB8


def r32(c, a):
    return int(c.command(f"peek {a:08X} 1").split()[0], 16)


def fl(c, a, n):
    return [float(x) for x in c.command(f"peekf {a:08X} {n}").split()]


def race(c):
    return r32(c, r32(c, 0x533E24) + 4)


def state(c):
    return r32(c, r32(c, 0x557EEC) + 4)


def racer(c, i):
    r = race(c) + ENTRY + i * ENTRY_SIZE
    b = r32(c, r + 4)
    return {"finished": b & 0xFF, "won": (r32(c, r + 0x5C) >> 8) & 0xFF, "next_cp": r32(c, r + 0x34) & 0xFFFF,
            "laps_left": r32(c, r + 0x18), "pos": r32(c, r + 0x14)}


def main():
    hp = int(sys.argv[1]) if len(sys.argv) > 1 else 45703
    pp = int(sys.argv[2]) if len(sys.argv) > 2 else 45704
    h, p = Copy(hp, timeout=10), Copy(pp, timeout=10)
    rc = race(h)
    count, cps = r32(h, rc + 0x24), r32(h, rc + 0x28)
    print(f"race {rc:08X} checkpoints {count} at {cps:08X}, state {state(h)}")
    while state(h) < 5:
        time.sleep(0.5)
    for step in range(400):
        me = racer(h, 0)
        if me["finished"] or state(h) >= 7:
            break
        cp = cps + me["next_cp"] * 0x120
        x, y, z = fl(h, cp + 0xFC, 3)
        h.command(f"place {x:.2f} {y + 0.5:.2f} {z:.2f}")
        time.sleep(0.25)
    time.sleep(1)
    print("host  racer0", racer(h, 0), "state", state(h))
    print("partner racer0", racer(p, 0), "state", state(p))
    print("host  ", h.command("state").split("race_state")[1])
    print("partner", p.command("state").split("race_state")[1])
    print(h.shot("finish-host"), p.shot("finish-partner"))


if __name__ == "__main__":
    main()
