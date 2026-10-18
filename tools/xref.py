"""Index of the recompiled code: functions, calls and constants, from the asm comments in generated/game.

python tools/xref.py build                 # writes out/xref.json (a few seconds)
python tools/xref.py callers 0x4187A0      # functions that call an address
python tools/xref.py callees 0x123450      # what a function calls
python tools/xref.py refs 0x445C64         # functions using a constant (a string, a global)
python tools/xref.py str "Race Over"       # string in default.xbe -> its VA -> refs
python tools/xref.py asm 0x123450 [n]      # the function's asm lines
"""
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "generated" / "game"
DB = ROOT / "out" / "xref.json"
XBE = ROOT / "assets" / "default.xbe"

FUNC = re.compile(r"^X86_FUNC\(__imp__sub_([0-9A-F]{8})\)")
ASM = re.compile(r"^\t// ([0-9A-F]{8}) (.*)$")
CALL = re.compile(r"\bcall 0x([0-9A-F]+)\b")
CONST = re.compile(r"0x([0-9A-F]{5,8})\b")


def build():
    funcs = {}
    for path in sorted(GEN.glob("x86_recomp.*.cpp")):
        cur = None
        for line in path.open(encoding="utf-8", errors="replace"):
            m = FUNC.match(line)
            if m:
                cur = {"asm": [], "calls": set(), "consts": set()}
                funcs[int(m.group(1), 16)] = cur
                continue
            if cur is None:
                continue
            m = ASM.match(line)
            if not m:
                continue
            va, text = int(m.group(1), 16), m.group(2)
            cur["asm"].append((va, text))
            c = CALL.search(text)
            if c:
                cur["calls"].add(int(c.group(1), 16))
            else:
                for k in CONST.findall(text):
                    cur["consts"].add(int(k, 16))
    out = {f"{a:08X}": {"asm": v["asm"], "calls": sorted(v["calls"]), "consts": sorted(v["consts"])} for a, v in funcs.items()}
    DB.write_text(json.dumps(out))
    print(f"{len(out)} functions -> {DB}")


def load():
    return {int(k, 16): v for k, v in json.loads(DB.read_text()).items()}


def xbe_strings(text):
    d = XBE.read_bytes()
    base = struct.unpack_from("<I", d, 0x104)[0]
    nsec, sec = struct.unpack_from("<II", d, 0x11C)
    hits = []
    needle = text.encode()
    start = 0
    while (i := d.find(needle, start)) >= 0:
        start = i + 1
        for s in range(nsec):
            _, va, vsize, raw, rsize = struct.unpack_from("<IIIII", d, sec - base + s * 56)
            if raw <= i < raw + rsize:
                hits.append(va + i - raw)
    return hits


def main():
    cmd = sys.argv[1]
    if cmd == "build":
        return build()
    db = load()
    arg = sys.argv[2] if len(sys.argv) > 2 else ""
    if cmd == "callers":
        a = int(arg, 16)
        for f, v in sorted(db.items()):
            if a in v["calls"]:
                print(f"{f:08X}")
    elif cmd == "callees":
        for c in db[int(arg, 16)]["calls"]:
            print(f"{c:08X}")
    elif cmd == "refs":
        a = int(arg, 16)
        for f, v in sorted(db.items()):
            if a in v["consts"]:
                print(f"{f:08X}")
    elif cmd == "str":
        for va in xbe_strings(arg):
            users = [f"{f:08X}" for f, v in sorted(db.items()) if va in v["consts"]]
            print(f"{va:08X}: {' '.join(users) or '(no direct refs)'}")
    elif cmd == "asm":
        n = int(sys.argv[3]) if len(sys.argv) > 3 else 10000
        for va, text in db[int(arg, 16)]["asm"][:n]:
            print(f"{va:08X} {text}")


if __name__ == "__main__":
    main()
