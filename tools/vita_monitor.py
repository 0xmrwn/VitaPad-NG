#!/usr/bin/env python3
"""Read-only measuring client for the VitaPad protocol (see Common/protocol.h).

Connects to a VitaPad app (real or tools/fake_vita.py), receives its input
packets by UDP streaming (default) or TCP polling, and records timing
statistics and raw samples. Nothing is sent besides the protocol's own
discovery, hello, keepalive and request messages. The Vita's address is never
printed or written anywhere: it is only called "the Vita".

Examples:
  python3 tools/vita_monitor.py --host 192.0.2.10 --out ./runs --label stream60 --seconds 60
  python3 tools/vita_monitor.py --host 192.0.2.10 --out ./runs --label poll60 --mode poll --seconds 60
  python3 tools/vita_monitor.py --discover --out ./runs --quiet
  python3 tools/vita_monitor.py --self-test

Output files in --out: <label>-summary.json, <label>-packets.csv, <label>-touch.csv.
Exit codes: 0 recorded, 2 connection failure / bad usage, 130 Ctrl-C (files still written).
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import select
import socket
import statistics
import struct
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Dict, List, Optional, Sequence, Tuple

VERSION = "1.0"

# ---- Protocol constants, from Common/protocol.h and Client/main.cpp ----
GAMEPAD_PORT = 5000
DISCOVERY_PORT = 5000
STREAM_PORT = 5001
DISCOVERY_REQUEST = b"VITAPAD_DISCOVER"
DISCOVERY_REPLY = b"VITAPAD_HERE"
REQUEST_SIZE = 8
REQUEST_V2 = b"VPAD2\0\0\0"  # "VPAD2\0\0" in C, plus the literal's NUL: 8 bytes
STREAM_HELLO = b"VPAD_STREAM\0"  # sent with its NUL: 12 bytes
STREAM_MAGIC = b"VPS1"
STREAM_HELLO_MS = 250
KEEPALIVE_MS = 250  # Client/main.cpp
CONNECT_TIMEOUT_S = 2.0  # CONNECT_TIMEOUT_MS
SOCKET_TIMEOUT_S = 3.0  # SOCKET_TIMEOUT_MS
DISCOVERY_TIMEOUT_S = 1.5  # DISCOVERY_TIMEOUT_MS; the reference gives up after 3 s overall
FIRST_PACKET_TIMEOUT_S = 5.0

# Packed little-endian layouts (#pragma pack(push, 1))
_TOUCH = "HHBB"  # x, y, id, reserved                                  -> 6 bytes
PAD_V2 = struct.Struct("<I4BBBH" + _TOUCH * 4 + "6fIB3s")
STREAM = struct.Struct("<4sI" + PAD_V2.format[1:])
PAD_V2_SIZE = 68
STREAM_SIZE = 76
LEGACY_PAD_SIZE = 16  # PadPacket is not packed: 13 bytes padded to 16
TIMESTAMP_OFFSET = 60  # offset of PadPacketV2.timestamp

# From Client/main.h
BUTTONS: List[Tuple[int, str]] = [
    (0x000010, "UP"), (0x000040, "DOWN"), (0x000080, "LEFT"), (0x000020, "RIGHT"),
    (0x004000, "CROSS"), (0x002000, "CIRCLE"), (0x008000, "SQUARE"), (0x001000, "TRIANGLE"),
    (0x000100, "L"), (0x000200, "R"), (0x000008, "START"), (0x000001, "SELECT"),
    (0x000400, "L1"), (0x000800, "R1"), (0x000002, "L3"), (0x000004, "R3"), (0x010000, "PS"),
]


# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------

@dataclass
class Touch:
    x: int
    y: int
    id: int
    reserved: int


@dataclass
class Pad:
    buttons: int
    lx: int
    ly: int
    rx: int
    ry: int
    front_num: int
    rear_num: int
    front: List[Touch]
    rear: List[Touch]
    accel: Tuple[float, float, float]
    gyro: Tuple[float, float, float]
    timestamp: int
    battery: int


def parse_pad_packet_v2(data: bytes) -> Pad:
    """Parse a 68 byte PadPacketV2. Raises ValueError on a wrong size."""
    if len(data) != PAD_V2_SIZE:
        raise ValueError(f"PadPacketV2 must be {PAD_V2_SIZE} bytes, got {len(data)}")
    v = PAD_V2.unpack(data)
    buttons, lx, ly, rx, ry, fn, rn, _res = v[:8]
    t = v[8:24]
    touches = [Touch(*t[i * 4:i * 4 + 4]) for i in range(4)]
    accel = tuple(v[24:27])
    gyro = tuple(v[27:30])
    timestamp, battery = v[30], v[31]
    return Pad(buttons, lx, ly, rx, ry, fn, rn, touches[:2][:min(fn, 2)], touches[2:][:min(rn, 2)],
               accel, gyro, timestamp, battery)  # type: ignore[arg-type]


def parse_stream_packet(data: bytes) -> Optional[Tuple[int, Pad]]:
    """Parse a 76 byte StreamPacket into (seq, pad); None if size or magic is wrong."""
    if len(data) != STREAM_SIZE or data[:4] != STREAM_MAGIC:
        return None
    seq = struct.unpack_from("<I", data, 4)[0]
    return seq, parse_pad_packet_v2(data[8:])


def button_names(mask: int) -> List[str]:
    return [name for bit, name in BUTTONS if mask & bit]


# ---------------------------------------------------------------------------
# Statistics
# ---------------------------------------------------------------------------

def percentile(values: Sequence[float], p: float) -> float:
    """Linear-interpolated percentile (p in 0..100) of a non-empty sequence."""
    if not values:
        raise ValueError("percentile of an empty sequence")
    s = sorted(values)
    k = (len(s) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def gap_stats(arrivals_ns: Sequence[int]) -> Dict[str, object]:
    """Inter-arrival gap statistics in ms."""
    gaps = [(b - a) / 1e6 for a, b in zip(arrivals_ns, arrivals_ns[1:])]
    if not gaps:
        return {"count": 0, "mean": None, "p50": None, "p95": None, "p99": None, "max": None, "over_20ms": 0}
    return {"count": len(gaps), "mean": statistics.fmean(gaps), "p50": percentile(gaps, 50),
            "p95": percentile(gaps, 95), "p99": percentile(gaps, 99), "max": max(gaps),
            "over_20ms": sum(1 for g in gaps if g > 20.0)}


def timestamp_jitter(arrivals_ns: Sequence[int], vita_ts_us: Sequence[int]) -> List[float]:
    """(arrival - first_arrival) - (vita_ts - first_vita_ts) per packet, in ms.

    The Vita timestamp is a 32-bit microsecond counter: successive differences are
    taken modulo 2^32 (as signed values), so a wrap is handled as continuous time.
    """
    out: List[float] = []
    if not arrivals_ns:
        return out
    vita_elapsed_us = 0
    for i, (a, ts) in enumerate(zip(arrivals_ns, vita_ts_us)):
        if i:
            d = (ts - vita_ts_us[i - 1]) & 0xFFFFFFFF
            if d >= 0x80000000:
                d -= 0x100000000
            vita_elapsed_us += d
        out.append((a - arrivals_ns[0]) / 1e6 - vita_elapsed_us / 1000.0)
    return out


def _stats4(values: Sequence[float]) -> Dict[str, Optional[float]]:
    if not values:
        return {"min": None, "median": None, "max": None}
    return {"min": min(values), "median": statistics.median(values), "max": max(values)}


# ---------------------------------------------------------------------------
# Recording
# ---------------------------------------------------------------------------

@dataclass
class Sample:
    t_ns: int
    seq: Optional[int]
    pad: Pad
    raw_no_ts: bytes


@dataclass
class Recording:
    samples: List[Sample] = field(default_factory=list)
    late: int = 0  # dropped by sequence number (late or repeated)
    lost: int = 0  # sequence gaps
    ended_early: Optional[str] = None


class ConnectionFailure(Exception):
    pass


def tcp_connect(host: str, port: int) -> socket.socket:
    # Messages never include the address.
    try:
        s = socket.create_connection((host, port), timeout=CONNECT_TIMEOUT_S)
    except OSError as e:
        raise ConnectionFailure(f"could not connect to the Vita ({os.strerror(e.errno) if e.errno else 'timeout'})")
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    s.settimeout(SOCKET_TIMEOUT_S)
    return s


def recv_exact(s: socket.socket, n: int) -> bytes:
    """Read up to n bytes; returns fewer on close or timeout."""
    buf = b""
    while len(buf) < n:
        try:
            chunk = s.recv(n - len(buf))
        except (socket.timeout, OSError):
            break
        if not chunk:
            break
        buf += chunk
    return buf


def version_check(s: socket.socket) -> None:
    """The reference client's first poll: legacy-sized reply means an outdated Vita app."""
    s.sendall(REQUEST_V2)
    reply = recv_exact(s, PAD_V2_SIZE)
    if len(reply) == LEGACY_PAD_SIZE:
        raise ConnectionFailure("the Vita app is outdated (replied with a legacy packet)")
    if len(reply) != PAD_V2_SIZE:
        raise ConnectionFailure("the Vita did not answer the first request")


def _no_ts(raw: bytes) -> bytes:
    return raw[:TIMESTAMP_OFFSET] + raw[TIMESTAMP_OFFSET + 4:]


def format_line(count: int, max_gap_ms: float, lost: int, pad: Pad) -> str:
    def touches(name: str, pts: List[Touch]) -> str:
        return f"{name}:" + (" -" if not pts else "".join(f" ({p.x:4d},{p.y:4d})" for p in pts))
    return (f"[{count:4d} pkt/s, max gap {max_gap_ms:5.1f} ms, lost {lost:3d}] bat {pad.battery:3d}% | "
            f"L({pad.lx:3d},{pad.ly:3d}) R({pad.rx:3d},{pad.ry:3d}) | {touches('front', pad.front)} "
            f"{touches('rear', pad.rear)} |" + "".join(" " + n for n in button_names(pad.buttons)))


class Reporter:
    """Prints one line per second, like the reference --monitor."""

    def __init__(self, quiet: bool) -> None:
        self.quiet = quiet
        self.start_ns = 0
        self.bucket = 0
        self.count = 0
        self.lost = 0
        self.max_gap_ns = 0
        self.last_ns = 0
        self.last_pad: Optional[Pad] = None

    def packet(self, t_ns: int, pad: Pad, lost: int) -> None:
        if not self.start_ns:
            self.start_ns = self.last_ns = t_ns
        b = int((t_ns - self.start_ns) // 1_000_000_000)
        if b > self.bucket:
            self.flush()
            self.bucket = b
        self.max_gap_ns = max(self.max_gap_ns, t_ns - self.last_ns)
        self.last_ns = t_ns
        self.count += 1
        self.lost += lost
        self.last_pad = pad

    def flush(self) -> None:
        if not self.quiet and self.last_pad is not None and self.count:
            print(format_line(self.count, self.max_gap_ns / 1e6, self.lost, self.last_pad), flush=True)
        self.count = self.lost = self.max_gap_ns = 0


def record_stream(host: str, port: int, stream_port: int, seconds: float, rec: Recording, rep: Reporter) -> None:
    tcp = tcp_connect(host, port)
    try:
        version_check(tcp)
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            udp.connect((host, stream_port))
            udp.setblocking(False)
            _stream_loop(tcp, udp, seconds, rec, rep)
        finally:
            udp.close()
    finally:
        tcp.close()


def _stream_loop(tcp: socket.socket, udp: socket.socket, seconds: float, rec: Recording, rep: Reporter) -> None:
    clock = time.perf_counter
    start = clock()
    last_hello = -1e9
    last_keepalive = start
    pending = False
    got = 0
    first_ns = 0
    last_seq = 0
    streaming = False
    while True:
        now = clock()
        if streaming and (time.perf_counter_ns() - first_ns) / 1e9 >= seconds:
            return
        if not streaming and now - start > FIRST_PACKET_TIMEOUT_S:
            raise ConnectionFailure("no stream packets from the Vita within 5 seconds")
        if now - last_hello >= STREAM_HELLO_MS / 1000:
            udp.send(STREAM_HELLO)
            last_hello = now
        if not pending and now - last_keepalive >= KEEPALIVE_MS / 1000:
            tcp.sendall(REQUEST_V2)
            pending, got, last_keepalive = True, 0, now
        if pending and now - last_keepalive > SOCKET_TIMEOUT_S:
            rec.ended_early = "keepalive timeout"
            return
        rlist = [udp] + ([tcp] if pending else [])
        ready, _, _ = select.select(rlist, [], [], 0.05)
        if tcp in ready:
            chunk = tcp.recv(PAD_V2_SIZE - got)
            if not chunk:
                rec.ended_early = "connection to the Vita closed"
                return
            got += len(chunk)
            if got == PAD_V2_SIZE:
                pending = False
        if udp in ready:
            while True:
                try:
                    data = udp.recv(2048)
                except (BlockingIOError, ConnectionRefusedError):
                    break
                t_ns = time.perf_counter_ns()
                parsed = parse_stream_packet(data)
                if parsed is None:
                    continue
                seq, pad = parsed
                lost = 0
                if streaming:
                    delta = ((seq - last_seq + 0x80000000) & 0xFFFFFFFF) - 0x80000000  # (int32_t)(seq - last_seq)
                    if delta <= 0:
                        rec.late += 1
                        continue
                    lost = delta - 1
                    rec.lost += lost
                else:
                    streaming, first_ns = True, t_ns
                last_seq = seq
                rec.samples.append(Sample(t_ns, seq, pad, _no_ts(data[8:])))
                rep.packet(t_ns, pad, lost)
                if (t_ns - first_ns) / 1e9 >= seconds:
                    return


def record_poll(host: str, port: int, seconds: float, rec: Recording, rep: Reporter) -> None:
    tcp = tcp_connect(host, port)
    try:
        version_check(tcp)
        tcp.settimeout(FIRST_PACKET_TIMEOUT_S)
        first_ns = 0
        while True:
            tcp.sendall(REQUEST_V2)
            data = recv_exact(tcp, PAD_V2_SIZE)
            t_ns = time.perf_counter_ns()
            if len(data) != PAD_V2_SIZE:
                if not rec.samples:
                    raise ConnectionFailure("no poll replies from the Vita within 5 seconds")
                rec.ended_early = "connection to the Vita lost"
                return
            if not first_ns:
                first_ns = t_ns
                tcp.settimeout(SOCKET_TIMEOUT_S)
            pad = parse_pad_packet_v2(data)
            rec.samples.append(Sample(t_ns, None, pad, _no_ts(data)))
            rep.packet(t_ns, pad, 0)
            if (t_ns - first_ns) / 1e9 >= seconds:
                return
    finally:
        tcp.close()


# ---------------------------------------------------------------------------
# Discovery (as Client/main.cpp discoverVita: broadcast to every interface, first answer wins)
# ---------------------------------------------------------------------------

def broadcast_addresses() -> List[str]:
    addrs = ["255.255.255.255"]
    try:
        out = subprocess.run(["ifconfig"], capture_output=True, text=True, timeout=5).stdout
        for m in re.finditer(r"broadcast (\d+\.\d+\.\d+\.\d+)", out):
            if m.group(1) not in addrs:
                addrs.append(m.group(1))
    except (OSError, subprocess.SubprocessError):
        pass
    return addrs


def discover(port: int = DISCOVERY_PORT) -> Optional[str]:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        for a in broadcast_addresses():
            try:
                s.sendto(DISCOVERY_REQUEST, (a, port))
            except OSError:
                pass
        start = time.time()
        while time.time() - start < 3:
            ready, _, _ = select.select([s], [], [], DISCOVERY_TIMEOUT_S)
            if not ready:
                return None
            data, addr = s.recvfrom(64)
            if data.startswith(DISCOVERY_REPLY):
                return addr[0]
        return None
    finally:
        s.close()


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def build_summary(rec: Recording, mode: str, start_utc: str) -> Dict[str, object]:
    sm = rec.samples
    n = len(sm)
    arr = [s.t_ns for s in sm]
    span = (arr[-1] - arr[0]) / 1e9 if n > 1 else 0.0
    # Per-second counts over complete seconds (a partial last second would skew the minimum)
    counts: Dict[int, int] = {}
    for t in arr:
        counts[int((t - arr[0]) // 1_000_000_000)] = counts.get(int((t - arr[0]) // 1_000_000_000), 0) + 1
    full = [counts.get(i, 0) for i in range(int(span))] if int(span) >= 1 else list(counts.values())
    jitter = timestamp_jitter(arr, [s.pad.timestamp for s in sm])
    sticks: Dict[str, object] = {}
    for name in ("lx", "ly", "rx", "ry"):
        vals = [getattr(s.pad, name) for s in sm]
        sticks[name] = ({"min": min(vals), "max": max(vals), "mean": statistics.fmean(vals),
                         "samples_differing_from_first_by_more_than_2": sum(1 for v in vals if abs(v - vals[0]) > 2)}
                        if vals else None)
    seen = sorted({nm for s in sm for nm in button_names(s.pad.buttons)})
    dup = sum(1 for a, b in zip(sm, sm[1:]) if a.raw_no_ts == b.raw_no_ts)
    stream = mode == "stream"
    return {
        "tool": "vita_monitor", "tool_version": VERSION, "start_time_utc": start_utc,
        "mode": mode, "seconds": span, "packets": n,
        "packets_per_second": ((n - 1) / span) if span > 0 else None,
        "packets_per_second_per_second": _stats4(full),
        "inter_arrival_gap_ms": gap_stats(arr),
        "gaps_over_20ms": gap_stats(arr)["over_20ms"],
        "lost_packets": rec.lost if stream else None,
        "late_packets": rec.late if stream else None,
        "duplicate_or_unchanged_packets": dup,
        "delivery_jitter_ms": ({"p50": percentile(jitter, 50), "p95": percentile(jitter, 95),
                                "p99": percentile(jitter, 99), "max_minus_min": max(jitter) - min(jitter)}
                               if jitter else None),
        "stick_rest_stats": sticks,
        "buttons_seen": seen,
        "front_touch_samples": sum(1 for s in sm if s.pad.front_num),
        "rear_touch_samples": sum(1 for s in sm if s.pad.rear_num),
        "battery": {"first": sm[0].pad.battery, "last": sm[-1].pad.battery} if sm else None,
        "ended_early": rec.ended_early,
        "notes": ("delivery_jitter_ms is (arrival - first arrival) - (Vita timestamp - first Vita timestamp), "
                  "relative to the first packet: it shows variation in delivery delay, not absolute latency. "
                  "packets_per_second is (packets - 1) / seconds. Per-second counts cover complete seconds only. "
                  "late_packets counts packets dropped by sequence number (late or repeated)."),
    }


def write_outputs(out_dir: str, label: str, rec: Recording, mode: str, start_utc: str) -> Dict[str, object]:
    os.makedirs(out_dir, exist_ok=True)
    summary = build_summary(rec, mode, start_utc)
    t0 = rec.samples[0].t_ns if rec.samples else 0
    with open(os.path.join(out_dir, f"{label}-summary.json"), "w") as f:
        json.dump(summary, f, indent=2)
        f.write("\n")
    with open(os.path.join(out_dir, f"{label}-packets.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["t_ms", "seq", "vita_ts_us", "buttons_hex", "lx", "ly", "rx", "ry", "front_num", "rear_num", "battery"])
        for s in rec.samples:
            p = s.pad
            w.writerow([f"{(s.t_ns - t0) / 1e6:.3f}", "" if s.seq is None else s.seq, p.timestamp, f"0x{p.buttons:05x}",
                        p.lx, p.ly, p.rx, p.ry, p.front_num, p.rear_num, p.battery])
    with open(os.path.join(out_dir, f"{label}-touch.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["t_ms", "panel", "index", "x", "y", "id"])
        for s in rec.samples:
            for panel, pts in (("front", s.pad.front), ("rear", s.pad.rear)):
                for i, t in enumerate(pts):
                    w.writerow([f"{(s.t_ns - t0) / 1e6:.3f}", panel, i, t.x, t.y, t.id])
    return summary


def print_summary(s: Dict[str, object]) -> None:
    g = s["inter_arrival_gap_ms"]
    assert isinstance(g, dict)
    f = lambda v: "n/a" if v is None else f"{v:.2f}"  # noqa: E731
    print(f"Recorded {s['packets']} packets in {f(s['seconds'])} s ({s['mode']}), {f(s['packets_per_second'])} pkt/s")
    print(f"gap ms: mean {f(g['mean'])} p50 {f(g['p50'])} p95 {f(g['p95'])} p99 {f(g['p99'])} max {f(g['max'])}; "
          f"over 20 ms: {s['gaps_over_20ms']}")
    print(f"lost {s['lost_packets']} late {s['late_packets']} unchanged {s['duplicate_or_unchanged_packets']}")
    j = s["delivery_jitter_ms"]
    if isinstance(j, dict):
        print(f"delivery jitter ms: p50 {f(j['p50'])} p95 {f(j['p95'])} p99 {f(j['p99'])} range {f(j['max_minus_min'])}")


# ---------------------------------------------------------------------------
# Self-test
# ---------------------------------------------------------------------------

def self_test() -> int:
    failures: List[str] = []

    def check(name: str, cond: bool) -> None:
        if not cond:
            failures.append(name)

    check("PadPacketV2 size", PAD_V2.size == 68)
    check("StreamPacket size", STREAM.size == 76)
    # Built field by field (not through PAD_V2) from the header layout
    pad = (struct.pack("<I", 0x10005) + bytes([10, 20, 30, 40, 1, 2]) + struct.pack("<H", 0)
           + struct.pack("<HHBB", 300, 200, 7, 0) + b"\0" * 6
           + struct.pack("<HHBB", 100, 50, 9, 0) + struct.pack("<HHBB", 101, 51, 10, 0)
           + struct.pack("<3f", 0.0, -1.0, 0.5) + struct.pack("<3f", 1.0, 2.0, 3.0)
           + struct.pack("<I", 123456789) + bytes([77, 0, 0, 0]))
    check("built size", len(pad) == 68)
    p = parse_pad_packet_v2(pad)
    check("buttons", p.buttons == 0x10005 and sorted(button_names(p.buttons)) == ["PS", "R3", "SELECT"])
    check("sticks", (p.lx, p.ly, p.rx, p.ry) == (10, 20, 30, 40))
    check("touch counts", (p.front_num, p.rear_num) == (1, 2))
    check("front", len(p.front) == 1 and (p.front[0].x, p.front[0].y, p.front[0].id) == (300, 200, 7))
    check("rear", [(t.x, t.y, t.id) for t in p.rear] == [(100, 50, 9), (101, 51, 10)])
    check("accel/gyro", p.accel == (0.0, -1.0, 0.5) and p.gyro == (1.0, 2.0, 3.0))
    check("timestamp/battery", p.timestamp == 123456789 and p.battery == 77)
    sp = parse_stream_packet(b"VPS1" + struct.pack("<I", 4242) + pad)
    check("stream parse", sp is not None and sp[0] == 4242 and sp[1].battery == 77)
    check("stream bad magic", parse_stream_packet(b"XXXX" + struct.pack("<I", 1) + pad) is None)
    check("stream bad size", parse_stream_packet(b"VPS1" + pad) is None)
    try:
        parse_pad_packet_v2(pad[:-1])
        check("short pad rejected", False)
    except ValueError:
        pass
    data = [1, 2, 3, 4, 5]
    check("percentile p0/p50/p100", (percentile(data, 0), percentile(data, 50), percentile(data, 100)) == (1, 3, 5))
    check("percentile p25", percentile(data, 25) == 2 and abs(percentile([0, 10], 90) - 9) < 1e-9)
    # Vita clock crossing 2^32 us while arrivals are exactly 10 ms apart: jitter must be ~0
    ts = [(0xFFFFFFFF - 15_000 + 10_000 * i) & 0xFFFFFFFF for i in range(5)]
    check("sequence crosses wrap", ts[1] < ts[0] or ts[2] < ts[1])
    j = timestamp_jitter([i * 10_000_000 for i in range(5)], ts)
    check("wraparound jitter", all(abs(x) < 1e-9 for x in j))
    j2 = timestamp_jitter([0, 10_000_000, 25_000_000], [0, 10_000, 20_000])
    check("jitter value", abs(j2[2] - 5.0) < 1e-9)
    gs = gap_stats([0, 10_000_000, 40_000_000])
    check("gap stats", gs["over_20ms"] == 1 and abs(gs["max"] - 30.0) < 1e-9)
    if failures:
        print("self-test FAILED: " + ", ".join(failures))
        return 1
    print("self-test passed")
    return 0


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description="Read-only VitaPad measuring client.")
    ap.add_argument("--host", help="the Vita's address (never printed or written)")
    ap.add_argument("--discover", action="store_true", help="find the Vita by broadcast; prints 'found' or 'not found'")
    ap.add_argument("--mode", choices=("stream", "poll"), default="stream")
    ap.add_argument("--seconds", type=float, default=30.0, help="recording time from the first packet")
    ap.add_argument("--out", help="output directory")
    ap.add_argument("--label", default="run", help="file name prefix [A-Za-z0-9_-]")
    ap.add_argument("--quiet", action="store_true", help="no per-second lines")
    ap.add_argument("--port", type=int, default=GAMEPAD_PORT, help="TCP port override (testing)")
    ap.add_argument("--stream-port", type=int, default=STREAM_PORT, help="UDP port override (testing)")
    ap.add_argument("--self-test", action="store_true", help="run offline checks and exit")
    args = ap.parse_args(argv)

    if args.self_test:
        return self_test()
    if not args.out:
        ap.error("--out is required")
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.label):
        ap.error("--label must match [A-Za-z0-9_-]+")
    if args.seconds <= 0:
        ap.error("--seconds must be positive")
    if not args.host and not args.discover:
        ap.error("--host is required unless --discover is given")

    host = args.host
    if args.discover:
        host = discover(args.port)
        print("found" if host else "not found", flush=True)
        if not host:
            return 2

    start_utc = datetime.now(timezone.utc).isoformat(timespec="seconds")
    rec = Recording()
    rep = Reporter(args.quiet)
    code = 0
    try:
        if args.mode == "stream":
            record_stream(host, args.port, args.stream_port, args.seconds, rec, rep)
        else:
            record_poll(host, args.port, args.seconds, rec, rep)
        rep.flush()
    except KeyboardInterrupt:
        rep.flush()
        code = 130
    except ConnectionFailure as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    except OSError as e:
        print(f"error: connection to the Vita failed ({os.strerror(e.errno) if e.errno else 'I/O error'})",
              file=sys.stderr)
        return 2
    if not rec.samples:
        print("error: no packets recorded", file=sys.stderr)
        return 2 if code == 0 else code
    summary = write_outputs(args.out, args.label, rec, args.mode, start_utc)
    print_summary(summary)
    return code


if __name__ == "__main__":
    sys.exit(main())
