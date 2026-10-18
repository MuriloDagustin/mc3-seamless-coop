"""A test copy with a saved profile, from boot to the first career race, with the virtual pad (tools/xrun.py copies
report one; the runner then takes no keys). Pictures in captures/race-*.png.
python tools/to_race.py [port]
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ctl import Copy  # noqa: E402


def press(c, button, wait):
    c.command(f"pad {button} 300")
    time.sleep(wait)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 45703
    c = Copy(port)
    end = time.time() + 60
    while time.time() < end:
        try:
            c.command("ping")
            break
        except Exception:
            time.sleep(1)
    time.sleep(10)
    for _ in range(7):  # movies, legal page, attract: A and Start skip them
        press(c, "start", 2.5)
    print(c.shot("race-menu"))
    time.sleep(8)  # the main menu takes no input while its camera flies in
    # Career, then Start Career; the main menu sometimes misses the first press, and a third one while the race
    # loads does nothing
    press(c, "a", 3)
    press(c, "a", 3)
    press(c, "a", 38)
    press(c, "b", 4)    # skip the race intro
    print(c.shot("race-start"))


if __name__ == "__main__":
    main()
