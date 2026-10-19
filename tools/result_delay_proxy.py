"""Local UDP fixture: delay only client -> host race results, preserving the other co-op traffic.

Example (start before the client):
    python tools/result_delay_proxy.py --host-port 45520 --listen-port 45521 --delay 2 --seconds 600
Host uses MC3COOP_PORT=45520. Client joins 127.0.0.1 with MC3COOP_PORT=45521.
Both copies must use protocol 4. This script starts no game, launcher or other process.
Logs contain only timing, packet kinds, race serials and terminal booleans; never raw payloads.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from datetime import datetime, timezone
import heapq
import math
import selectors
import socket
import struct
import threading
import time
from typing import Callable


MAGIC = 0x4F43334D
PROTOCOL = 4
HEADER = struct.Struct("<IHBBIII")  # 20 bytes, packed as in mod/src/net/session.cpp
HELLO, WON, FINISHED = 1, 11, 13
LOOPBACK = "127.0.0.1"


@dataclass(order=True)
class Delayed:
    due: float
    order: int
    received: float = field(compare=False)
    data: bytes = field(compare=False)
    kind: str = field(compare=False)
    serial: int = field(compare=False)
    won: int = field(compare=False)
    dnf: int = field(compare=False)


class ResultDelayProxy:
    def __init__(self, host_port: int, listen_port: int, delay: float,
                 emit: Callable[[str], None] | None = None):
        self.host = (LOOPBACK, host_port)
        self.client: tuple[str, int] | None = None
        self.delay = delay
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((LOOPBACK, listen_port))
        self.sock.setblocking(False)
        self.address = self.sock.getsockname()
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.sock, selectors.EVENT_READ)
        self.queue: list[Delayed] = []
        self.order = 0
        self.started = time.monotonic()
        self.stopped = threading.Event()
        self.emit = emit or (lambda message: print(message, flush=True))
        self.stats = dict(to_host=0, to_client=0, queued=0, released=0, invalid=0, unknown=0, send_errors=0)

    def log(self, event: str, **values):
        stamp = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
        elapsed = time.monotonic() - self.started
        fields = " ".join(f"{key}={value}" for key, value in values.items())
        self.emit(f"{stamp} t={elapsed:.3f} event={event}" + (f" {fields}" if fields else ""))

    def send(self, data: bytes, destination: tuple[str, int], counter: str):
        try:
            sent = self.sock.sendto(data, destination)
            if sent != len(data):
                self.stats["send_errors"] += 1
            else:
                self.stats[counter] += 1
        except OSError:
            self.stats["send_errors"] += 1

    def receive(self, data: bytes, endpoint: tuple[str, int], now: float):
        if not HEADER.size <= len(data) <= 1500:
            self.stats["invalid"] += 1
            return
        magic, protocol, kind, flags, _session, _seq, _ms = HEADER.unpack_from(data)
        if magic != MAGIC or protocol != PROTOCOL:
            self.stats["invalid"] += 1
            return
        if endpoint == self.host:
            if self.client is None:
                self.stats["unknown"] += 1
            else:
                self.send(data, self.client, "to_client")
            return
        if self.client is None:
            # Learn exactly one test client through its real handshake, not a pose/result or an arbitrary sender.
            if endpoint[0] != LOOPBACK or kind != HELLO or len(data) != HEADER.size + 40 or flags != 0x0F:
                self.stats["unknown"] += 1
                return
            self.client = endpoint
            self.log("client_learned", port=endpoint[1])
        if endpoint != self.client:
            self.stats["unknown"] += 1
            return
        if kind not in (WON, FINISHED):
            self.send(data, self.host, "to_host")
            return
        if kind == WON and len(data) == HEADER.size + 8:
            serial, _prize = struct.unpack_from("<Ii", data, HEADER.size)
            label, won, dnf = "won", 1, 0
        elif kind == FINISHED and len(data) == HEADER.size + 20:
            serial, won, dnf = struct.unpack_from("<IBB", data, HEADER.size)
            if won > 1 or dnf > 1:
                self.stats["invalid"] += 1
                return
            label = "finished"
        else:
            self.stats["invalid"] += 1
            return
        self.order += 1
        item = Delayed(now + self.delay, self.order, now, data, label, serial, won, dnf)
        heapq.heappush(self.queue, item)
        self.stats["queued"] += 1
        self.log("result_queued", id=item.order, kind=label, serial=serial, won=won, dnf=dnf,
                 due_t=f"{item.due-self.started:.3f}", pending=len(self.queue))

    def release(self, now: float):
        while self.queue and self.queue[0].due <= now:
            item = heapq.heappop(self.queue)
            self.send(item.data, self.host, "to_host")
            self.stats["released"] += 1
            self.log("result_released", id=item.order, kind=item.kind, serial=item.serial,
                     won=item.won, dnf=item.dnf, waited_ms=f"{(now-item.received)*1000:.1f}",
                     pending=len(self.queue))

    def run(self, seconds: float):
        deadline = self.started + seconds
        next_summary = self.started + 5
        self.log("started", listen_port=self.address[1], host_port=self.host[1],
                 delay_seconds=self.delay, protocol=PROTOCOL)
        try:
            while not self.stopped.is_set():
                now = time.monotonic()
                if now >= deadline:
                    break
                self.release(now)
                if now >= next_summary:
                    self.log("summary", **self.stats, pending=len(self.queue))
                    next_summary = now + 5
                wake = min(deadline, now + 0.1, self.queue[0].due if self.queue else deadline)
                for _key, _events in self.selector.select(max(0, wake - time.monotonic())):
                    for _ in range(256):
                        try:
                            data, endpoint = self.sock.recvfrom(65535)
                        except BlockingIOError:
                            break
                        except ConnectionResetError:
                            # A vanished test endpoint must not close or affect another process.
                            self.stats["send_errors"] += 1
                            break
                        self.receive(data, endpoint, time.monotonic())
        except KeyboardInterrupt:
            self.log("interrupted")
        finally:
            self.log("closed", **self.stats, pending_discarded=len(self.queue))
            # Do not flush delayed results early on shutdown: that would invalidate the timing fixture.
            self.selector.close()
            self.sock.close()


def self_test():
    """Real loopback UDP only, with fake protocol datagrams and no game processes/files."""
    events: list[str] = []
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as host, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
        host.bind((LOOPBACK, 0))
        client.bind((LOOPBACK, 0))
        host.settimeout(1)
        client.settimeout(1)
        proxy = ResultDelayProxy(host.getsockname()[1], 0, 0.15, events.append)
        thread = threading.Thread(target=proxy.run, args=(4,), daemon=True)
        thread.start()
        def packet(kind, payload=b"", flags=0x01, protocol=PROTOCOL):
            return HEADER.pack(MAGIC, protocol, kind, flags, 123, 456, 789) + payload
        try:
            hello = packet(HELLO, bytes(40), 0x0F)
            client.sendto(hello, proxy.address)
            assert host.recvfrom(1500) == (hello, proxy.address)
            welcome = packet(2, flags=0x10)
            host.sendto(welcome, proxy.address)
            assert client.recvfrom(1500) == (welcome, proxy.address)
            won = packet(WON, struct.pack("<Ii", 42, 800))
            finished = packet(FINISHED, struct.pack("<IBBBBffi", 42, 1, 0, 0, 0, 10.0, 1.0, 0))
            began = time.monotonic()
            client.sendto(won, proxy.address)
            client.sendto(finished, proxy.address)
            pose = packet(3, b"synthetic-pose")
            client.sendto(pose, proxy.address)
            assert host.recvfrom(1500)[0] == pose  # results do not block handshake/poses
            assert time.monotonic() - began < 0.15
            a = host.recvfrom(1500)[0]
            assert time.monotonic() - began >= 0.14
            b = host.recvfrom(1500)[0]
            assert (a, b) == (won, finished)  # unchanged bytes and original order
            # Host -> client results are immediate; only the requested direction is delayed.
            host.sendto(won, proxy.address)
            assert client.recvfrom(1500)[0] == won
            wrong_protocol = packet(3, protocol=PROTOCOL + 1)
            client.sendto(wrong_protocol, proxy.address)
            client.sendto(packet(FINISHED, b"too-short"), proxy.address)
            host.settimeout(0.25)
            try:
                host.recvfrom(1500)
                raise AssertionError("invalid packets escaped the fixture")
            except socket.timeout:
                pass
            assert proxy.stats["queued"] == proxy.stats["released"] == 2
            assert proxy.stats["invalid"] == 2
            assert any("event=result_queued" in e and "won=1" in e for e in events)
            assert any("event=result_released" in e for e in events)
        finally:
            proxy.stopped.set()
            thread.join(2)
            assert not thread.is_alive()
    print("PASS: loopback handshake/poses immediate; both results delayed unchanged; reverse direction immediate; invalid protocol/length rejected.", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host-port", type=int, default=45520)
    parser.add_argument("--listen-port", type=int, default=45521)
    parser.add_argument("--delay", type=float, default=2.0)
    parser.add_argument("--seconds", type=float, default=600.0)
    parser.add_argument("--self-test", action="store_true", help="fake datagrams on loopback; no game")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not (1 <= args.host_port <= 65535 and 1 <= args.listen_port <= 65535 and args.host_port != args.listen_port):
        parser.error("host/listen must be distinct valid UDP ports")
    if not math.isfinite(args.delay) or not 0 <= args.delay <= 60:
        parser.error("delay must be between 0 and 60 seconds")
    if not math.isfinite(args.seconds) or args.seconds <= 0:
        parser.error("seconds must be positive and finite")
    ResultDelayProxy(args.host_port, args.listen_port, args.delay).run(args.seconds)


if __name__ == "__main__":
    main()
