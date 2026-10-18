"""Drive a fresh test copy from boot to the city, with pictures along the way (captures/city-*.png).

Keyboard on the runner (xrevive defaults seen in tests): Enter skips movies and splash pages, Space is A,
arrows move in menus. A first run with an empty save buys the tC and leaves the garage.
python tools/to_city.py [port] [--saved]
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ctl import Copy  # noqa: E402


def step(c, key, wait, ms=150):
    c.command(f"key {key} {ms}")
    time.sleep(wait)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 45703
    c = Copy(port)
    end = time.time() + 60
    while time.time() < end:  # the mod answers once the runner has loaded it
        try:
            c.command("ping")
            break
        except Exception:
            time.sleep(1)
    time.sleep(8)
    for _ in range(8):  # movies, legal page, attract
        step(c, "enter", 2.5)
    print(c.shot("city-1"))
    if "--saved" in sys.argv:  # a profile with its car: Start Career goes straight to the first race; B skips its intro
        for key, wait in [("enter", 3), ("space", 4), ("space", 35), ("back", 4)]:  # Career, Start Career
            step(c, key, wait, 200)
        print(c.shot("city-2"))
        return
    # a new profile: the nickname page, the intro, Buy Vehicles (tC), purchase, exit garage
    for key, wait in [("enter", 3), ("enter", 4), ("enter", 4), ("enter", 4), ("space", 3), ("space", 4),
                      ("space", 6), ("up", 1), ("space", 25)]:
        step(c, key, wait, 200)
    print(c.shot("city-2"))


if __name__ == "__main__":
    main()
