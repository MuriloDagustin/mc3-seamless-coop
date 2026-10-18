"""Talk to a test copy's control channel (src/game/control.cpp).

python tools/ctl.py 45703 ping
python tools/ctl.py 45703 shot [name]     # the next frame as captures/<name or time>.png (only final.bmp kept)
python tools/ctl.py 45703 wait "play=1" 120   # repeat "state" until the text shows, up to n seconds
"""
import socket
import sys
import time
from pathlib import Path

RUN = Path(r"D:\dev\mc3-coop-data\run")
XRUN = Path(r"D:\dev\mc3-coop-data\xrun")
CAPTURES = Path(__file__).resolve().parent.parent / "captures"


class Copy:
    def __init__(self, port, timeout=3.0):
        self.port = port
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(timeout)

    def command(self, text, check=True):
        self.sock.sendto(text.encode(), ("127.0.0.1", self.port))
        reply = self.sock.recv(65536).decode(errors="replace")
        if check and reply.startswith("error"):
            raise RuntimeError(f"{self.port} {text}: {reply}")
        return reply

    def state(self):
        out = {}
        for part in self.command("state").replace('"', "").split():
            if "=" in part:
                k, v = part.split("=", 1)
                out[k] = v
        return out

    def wait_for(self, text, seconds):
        end = time.time() + seconds
        while time.time() < end:
            try:
                s = self.command("state")
                if text in s:
                    return s
            except (socket.timeout, ConnectionResetError):
                pass
            time.sleep(0.5)
        raise TimeoutError(f"{self.port}: no '{text}' in {seconds}s")

    def shot(self, name=None):
        """The game window (PrintWindow in the mod) as captures/<name or time>-<port>.png; returns its path."""
        from PIL import Image
        CAPTURES.mkdir(exist_ok=True)
        bmp = XRUN / f"shot-{self.port}.bmp"
        self.sock.settimeout(12)
        self.command(f"shot {bmp}")
        self.sock.settimeout(3)
        out = CAPTURES / f"{name or time.strftime('%H%M%S')}-{self.port}.png"
        Image.open(bmp).convert("RGB").save(out)
        bmp.unlink()
        return out


def main():
    port, cmd = int(sys.argv[1]), sys.argv[2]
    c = Copy(port)
    if cmd == "shot":
        print(c.shot(sys.argv[3] if len(sys.argv) > 3 else None))
    elif cmd == "wait":
        print(c.wait_for(sys.argv[3], float(sys.argv[4]) if len(sys.argv) > 4 else 60))
    else:
        print(c.command(" ".join(sys.argv[2:]), check=False))


if __name__ == "__main__":
    main()
