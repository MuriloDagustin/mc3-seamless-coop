"""AI opponents in the shared race: each opponent's position on the host vs on the partner, a few times.
Run while both copies race the same career race (tools/race_start_test.py).
python tools/ai_test.py [rounds]
"""
import math
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ctl import Copy  # noqa: E402
from race_finish_test import r32, race, racer, state, fl  # noqa: E402


def opponents(c):
    mgr = r32(c, 0x534074)
    n, nc, array = r32(c, mgr), r32(c, mgr + 4), r32(c, mgr + 8)
    out = []
    for i in range(min(nc, 8)):
        vehicle = r32(c, array + i * 0x43C + 0x20)
        inst = r32(c, vehicle + 0x1C)
        out.append(tuple(float(v) for v in c.command(f"peekf {inst + 0x34:08X} 3").split()))
    return out


def finish(copy):
    """Test-only teleport through the native checkpoints; never calls a guest function."""
    current = race(copy)
    if r32(copy, current) != 0x46B2D8 or state(copy) not in (5, 6):
        raise RuntimeError("finish test requires a running common circuit")
    count, checkpoints = r32(copy, current + 0x24), r32(copy, current + 0x28)
    if not 1 <= count <= 128:
        raise RuntimeError("invalid checkpoint count")
    for _ in range(400):
        local = racer(copy, 0)
        if local["finished"] or state(copy) >= 7:
            return
        if local["next_cp"] >= count:
            raise RuntimeError("invalid next checkpoint")
        x, y, z = fl(copy, checkpoints + local["next_cp"] * 0x120 + 0xFC, 3)
        if not all(math.isfinite(v) for v in (x, y, z)):
            raise RuntimeError("invalid checkpoint position")
        copy.command(f"place {x:.2f} {y + 0.5:.2f} {z:.2f}")
        time.sleep(0.3)
    raise TimeoutError("native finish did not occur")


def ranking(copy):
    progress = r32(copy, race(copy) + 0x658)
    count = r32(copy, r32(copy, 0x534074) + 4)
    if not 1 <= count <= 8:
        raise RuntimeError("invalid opponent count")
    return [r32(copy, progress + i * 0x60 + 0x14) for i in range(count)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rounds", type=int, nargs="?", default=4)
    parser.add_argument("--host-port", type=int, default=45703)
    parser.add_argument("--partner-port", type=int, default=45704)
    parser.add_argument("--max-error", type=float, default=5.0)
    parser.add_argument("--finish", action="store_true", help="teleport both test players to native finishes and compare final AI ranks")
    args = parser.parse_args()
    h, p = Copy(args.host_port, timeout=20), Copy(args.partner_port, timeout=20)
    failed = False
    for k in range(args.rounds):
        a, b = opponents(h), opponents(p)
        d = [math.dist(x, y) for x, y in zip(a, b)]
        status = p.command("state")
        active = "ai=experimental" in status and f"applied={len(a)} " in status
        failed |= not a or len(a) != len(b) or not active or any(x > args.max_error for x in d)
        print(f"round {k}: opponents {len(a)}/{len(b)}, distance host/partner per car: {[round(x, 2) for x in d]}")
        print("   partner", status.split("ai=", 1)[1] if "ai=" in status else "?")
        time.sleep(3)
    if failed:
        raise SystemExit("FAIL: opponent count, puppet activation or position tolerance")
    if args.finish:
        finish(h)
        time.sleep(2)  # exceeds the normal stale timeout: the terminal host snapshot must remain authoritative
        if "ai=experimental" not in p.command("state"):
            raise SystemExit("FAIL: puppets released after host finish")
        finish(p)
        time.sleep(2)
        for _ in range(3):
            a, b = ranking(h), ranking(p)
            print("final opponent ranks:", a, b, flush=True)
            if a != b:
                raise SystemExit("FAIL: final AI ranks differ")
            time.sleep(1)
        print("PASS: native finishes and identical final AI ranks", flush=True)


if __name__ == "__main__":
    main()
