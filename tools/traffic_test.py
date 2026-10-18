"""Traffic sync check on two connected copies in free roam: for every slot, the host's position vs the partner's.
python tools/traffic_test.py [seconds]
"""
import math
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ctl import Copy  # noqa: E402
from race_finish_test import r32  # noqa: E402


def pool(c):
    tm = r32(c, r32(c, 0x50152C) + 8)
    n = r32(c, tm + 0x12C) & 0xFFFF
    slots = r32(c, tm + 0x130)
    out = {}
    for i in range(n):
        s = slots + i * 0x21C
        handle = r32(c, s + 0x2C) & 0xFFFF
        if handle == 0xFFFF:
            continue
        x, y, z = [float(v) for v in c.command(f"peekf {s + 0x58:08X} 3").split()]
        out[i] = (x, y, z, r32(c, s + 0xFC) != 0)
    return n, out


def main():
    seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    h, p = Copy(45703, timeout=20), Copy(45704, timeout=20)
    time.sleep(seconds)
    print("partner:", p.command("state").split("traffic=")[1])
    hn, hs = pool(h)
    pn, ps = pool(p)
    host_active = {i: v for i, v in hs.items() if v[3]}
    near = far = missing = 0
    worst = 0.0
    for i, (x, y, z, _) in host_active.items():
        if i not in ps or ps[i][1] < -400:
            missing += 1
            continue
        d = math.dist((x, y, z), ps[i][:3])
        worst = max(worst, d)
        near += d < 3.0
        far += d >= 3.0
    hidden = sum(1 for i, v in ps.items() if v[1] < -400)
    print(f"pool {hn}/{pn}; host active {len(host_active)}: matched within 3 m {near}, off {far} (worst {worst:.1f} m), "
          f"not spawned on partner {missing}; partner hidden {hidden}")


if __name__ == "__main__":
    main()
