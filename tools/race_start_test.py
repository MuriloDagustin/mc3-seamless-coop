"""The host starts a career race in free roam (its car taken into the start marker of its first open race in this
city, the hookman filter lifted for the test); the partner's copy must start the same race by itself.
python tools/race_start_test.py
"""
import re
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ctl import Copy  # noqa: E402
from race_finish_test import r32  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host-port", type=int, default=45703)
    parser.add_argument("--partner-port", type=int, default=45704)
    parser.add_argument("--no-shots", action="store_true")
    args = parser.parse_args()
    h, p = Copy(args.host_port, timeout=15), Copy(args.partner_port, timeout=15)
    data = r32(h, r32(h, 0x557F0C) + 0x0C)
    city = r32(h, r32(h, 0x53F2E0)) & 0xFF
    begin, end = r32(h, data + 0xA5C), r32(h, data + 0xA60)
    index = None
    for n, r in enumerate(range(begin, end, 0x4C)):
        if r32(h, r + 8) & 0xFF == city:
            w = r32(h, r + 8)
            h.command(f"poke {r + 8:08X} {(w & 0xFFFF00FF) | 0xFF00:08X}")  # no roaming hookman: always a marker
            index = n
            break
    if index is None:
        raise SystemExit(f"no open race in city {city} on the host")
    print(h.command(f"gorace {index}").splitlines()[0])
    f = lambda s: re.search(r"race_state=\d+ cruise=\d+ phase=\d+", s).group()
    for i in range(25):
        time.sleep(1.5)
        hs, ps = f(h.command("state")), f(p.command("state"))
        print(f"t={i * 1.5:4.1f}s host {hs} | partner {ps}")
        if "race_state=12" in hs and "race_state=12" in ps:
            break
    time.sleep(20)
    if not args.no_shots:
        print(h.shot("start-host"), p.shot("start-partner"))


if __name__ == "__main__":
    main()
