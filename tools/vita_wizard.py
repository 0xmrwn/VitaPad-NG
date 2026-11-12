#!/usr/bin/env python3
"""Step-by-step hands-on check of the VitaPad NG app on a real Vita.

You hold the Vita; this wizard tells you one thing to do at a time, watches what the Vita
sends, and says at once whether it worked. Nothing is timed against you: each step waits
until it sees what it asked for (or you skip it).

Usage:
    python3 tools/vita_wizard.py --host-file FILE --out DIR [--vu PATH]

--host-file  a file holding the Vita's address (never printed or written anywhere else)
--vu         a vita-use wrapper script; when given, the wizard takes screenshots itself
--out        where results.json and the screenshots go

The VitaPad NG app must be in the foreground on the Vita.
"""
import argparse
import json
import os
import select
import socket
import subprocess
import sys
import time
from typing import Callable, Dict, List, Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vita_monitor as vm  # noqa: E402  (protocol constants and packet parsing)

L1, R1, L3, R3 = 0x400, 0x800, 0x002, 0x004
ZONE_MASK = L1 | R1 | L3 | R3
CROSS, RIGHT, VITA_L = 0x4000, 0x20, 0x100

BOLD, DIM, GREEN, RED, YELLOW, RESET = "\033[1m", "\033[2m", "\033[32m", "\033[31m", "\033[33m", "\033[0m"


def names(mask: int) -> str:
    return "+".join(vm.button_names(mask)) or "nothing"


class Link:
    """A streaming session with the Vita, kept alive between steps."""

    def __init__(self, host: str):
        self.host = host
        self.tcp: Optional[socket.socket] = None
        self.udp: Optional[socket.socket] = None
        self.last_hello = 0.0
        self.last_keepalive = 0.0
        self.pending = False
        self.got = 0
        self.pad: Optional[vm.Pad] = None
        self.front_points_seen = 0  # packets that carried front touch points (must stay 0 in zones mode)
        self.packets = 0
        self.reconnects = 0

    def connect(self) -> None:
        self.tcp = vm.tcp_connect(self.host, vm.GAMEPAD_PORT)
        vm.version_check(self.tcp)
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.connect((self.host, vm.STREAM_PORT))
        self.udp.setblocking(False)
        self.last_keepalive = time.perf_counter()

    def close(self) -> None:
        for s in (self.udp, self.tcp):
            if s is not None:
                try:
                    s.close()
                except OSError:
                    pass

    def reconnect(self) -> None:
        """The Vita drops a client after 3 s of silence (a Wi-Fi stall is enough): connect again, quietly."""
        self.close()
        self.pending = False
        deadline = time.perf_counter() + 30
        while True:
            try:
                self.connect()
                self.reconnects += 1
                return
            except (vm.ConnectionFailure, OSError):
                if time.perf_counter() > deadline:
                    raise vm.ConnectionFailure("the Vita stopped answering for 30 seconds")
                time.sleep(0.5)

    def pump(self, seconds: float, until: Optional[Callable[[vm.Pad], bool]] = None) -> bool:
        """Keep the session alive for up to `seconds`; stop early and return True when `until(pad)` holds.
        A dropped connection is re-established and the wait continues."""
        end = time.perf_counter() + seconds
        while True:
            try:
                return self._pump(end, until)
            except (OSError, vm.ConnectionFailure) as e:
                if isinstance(e, vm.ConnectionFailure) and "30 seconds" in str(e):
                    raise
                self.reconnect()

    def _pump(self, end: float, until: Optional[Callable[[vm.Pad], bool]]) -> bool:
        assert self.tcp is not None and self.udp is not None
        while True:
            now = time.perf_counter()
            if now >= end:
                return False
            if now - self.last_hello >= vm.STREAM_HELLO_MS / 1000:
                try:
                    self.udp.send(vm.STREAM_HELLO)
                except OSError:
                    pass
                self.last_hello = now
            if not self.pending and now - self.last_keepalive >= vm.KEEPALIVE_MS / 1000:
                self.tcp.sendall(vm.REQUEST_V2)
                self.pending, self.got, self.last_keepalive = True, 0, now
            if self.pending and now - self.last_keepalive > 2.5:
                raise vm.ConnectionFailure("keepalive not answered")
            ready, _, _ = select.select([self.udp] + ([self.tcp] if self.pending else []), [], [], 0.02)
            if self.tcp in ready:
                chunk = self.tcp.recv(vm.PAD_V2_SIZE - self.got)
                if not chunk:
                    raise vm.ConnectionFailure("the Vita closed the connection")
                self.got += len(chunk)
                if self.got == vm.PAD_V2_SIZE:
                    self.pending = False
            if self.udp in ready:
                while True:
                    try:
                        data = self.udp.recv(2048)
                    except (BlockingIOError, ConnectionRefusedError):
                        break
                    parsed = vm.parse_stream_packet(data)
                    if parsed is None:
                        continue
                    self.pad = parsed[1]
                    self.packets += 1
                    if self.pad.front_num:
                        self.front_points_seen += 1
                    if until is not None and until(self.pad):
                        return True


class Wizard:
    def __init__(self, link: Link, out: str, vu: Optional[str], timeout: float = 40, answers: Optional[str] = None):
        self.link = link
        self.out = out
        self.vu = vu
        self.results: List[Dict[str, object]] = []
        self.step_no = 0
        self.total = 0
        self.timeout = timeout
        self.last_answer = ""
        self.answers = answers
        self.answers_read = 0

    # ---- talking to the person ----
    def header(self, title: str) -> None:
        self.step_no += 1
        print(f"\n{BOLD}Step {self.step_no} of {self.total}: {title}{RESET}")

    def say(self, text: str) -> None:
        print(f"  {text}")

    def ok(self, text: str) -> None:
        print(f"  {GREEN}OK{RESET}  {text}")

    def bad(self, text: str) -> None:
        print(f"  {RED}NOT OK{RESET}  {text}")

    def ask(self, question: str) -> str:
        """Ask on the keyboard while keeping the Vita session alive."""
        sys.stdout.write(f"  {YELLOW}{question}{RESET} ")
        sys.stdout.flush()
        while True:
            self.link.pump(0.2)
            if self.answers:
                # Answers typed in another terminal arrive as lines appended to a file
                try:
                    with open(self.answers) as f:
                        lines = f.read().split("\n")[:-1]
                except OSError:
                    lines = []
                if len(lines) > self.answers_read:
                    self.answers_read += 1
                    return lines[self.answers_read - 1].strip()
            elif select.select([sys.stdin], [], [], 0)[0]:
                return sys.stdin.readline().strip()

    def yes(self, question: str) -> Optional[bool]:
        """y or n; anything else is kept as a comment and counts as neither."""
        a = self.ask(question + " [y/n, or type a comment]")
        self.last_answer = a
        return True if a.lower() in ("y", "yes") else False if a.lower() in ("n", "no") else None

    def record(self, name: str, passed: Optional[bool], **detail: object) -> None:
        entry: Dict[str, object] = {"step": name, "passed": passed, **detail}
        if self.last_answer:
            entry["answer"] = self.last_answer
            self.last_answer = ""
        self.results.append(entry)

    # ---- helpers ----
    def wait_release(self) -> None:
        """Wait until no zone or button is held, so one step never leaks into the next."""
        self.link.pump(20, lambda p: p.buttons == 0)

    def wait_bits(self, want: int, hold_s: float = 0.3) -> Dict[str, object]:
        """Wait until all `want` bits are held for hold_s. Reports every other bit seen meanwhile."""
        since = [0.0]
        other = [0]

        def cond(p: vm.Pad) -> bool:
            if p.buttons & ~want:
                other[0] |= p.buttons & ~want
            if p.buttons & want == want:
                if not since[0]:
                    since[0] = time.perf_counter()
                return time.perf_counter() - since[0] >= hold_s
            since[0] = 0.0
            return False

        got = self.link.pump(self.timeout, cond)
        return {"got": got, "other": other[0]}

    def screenshot(self, name: str) -> Optional[str]:
        """Take a screenshot through vita-use while keeping the session alive. Returns the path or None."""
        if not self.vu:
            return None
        path = os.path.join(self.out, name)
        if os.path.exists(path):
            os.remove(path)
        proc = subprocess.Popen([self.vu, "capture", "--output", path, "--allow-network"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        while proc.poll() is None:
            self.link.pump(0.1)
        return path if proc.returncode == 0 and os.path.exists(path) else None

    # ---- steps ----
    def zone(self, where: str, bit: int) -> None:
        self.header(f"touch the {where} of the screen")
        self.wait_release()
        self.say(f"Put one thumb on the {BOLD}{where}{RESET} of the screen and hold it there.")
        self.say(f"{DIM}Waiting for the Vita to send {names(bit)}... (it gives up after {self.timeout:.0f} s){RESET}")
        r = self.wait_bits(bit)
        wrong = int(r["other"]) & ZONE_MASK
        if r["got"] and not wrong:
            self.ok(f"the PC receives {names(bit)}. You can lift your thumb.")
        elif r["got"]:
            self.bad(f"{names(bit)} arrived, but so did {names(wrong)}.")
        else:
            self.bad(f"{names(bit)} never arrived. Seen instead: {names(int(r['other']))}.")
        self.record(f"zone {where}", bool(r["got"] and not wrong), expected=names(bit), also_seen=names(int(r["other"])))

    def slide(self) -> None:
        self.header("slide across the screen without lifting")
        self.wait_release()
        self.say(f"Touch the {BOLD}top left{RESET}, keep your thumb down, slide slowly to the "
                 f"{BOLD}bottom right{RESET}, then lift.")
        self.say(f"{DIM}Waiting for the touch to start...{RESET}")
        seen = [0]

        def started(p: vm.Pad) -> bool:
            return bool(p.buttons & ZONE_MASK)

        if not self.link.pump(self.timeout, started):
            self.bad("no touch arrived.")
            self.record("slide keeps first zone", False, seen="nothing")
            return
        self.say(f"{DIM}Touch seen. Slide now, then lift.{RESET}")

        def lifted(p: vm.Pad) -> bool:
            seen[0] |= p.buttons & ZONE_MASK
            return not (p.buttons & ZONE_MASK)

        self.link.pump(30, lifted)
        passed = seen[0] == L1
        if passed:
            self.ok("the press stayed L1 for the whole slide.")
        else:
            self.bad(f"the slide produced {names(seen[0])}; only L1 was expected.")
        self.record("slide keeps first zone", passed, seen=names(seen[0]))

    def centre_tap(self, label: str, shot: str, question: str) -> None:
        self.header(label)
        self.wait_release()
        before = self.link.packets
        zone_bits = [0]

        def watch(p: vm.Pad) -> bool:
            zone_bits[0] |= p.buttons & ZONE_MASK
            return False

        self.say(f"Tap the {BOLD}centre{RESET} of the screen once (a quick tap, between the two sticks).")
        self.ask("Press Enter here after you tapped.")
        self.link.pump(1.0, watch)
        path = self.screenshot(shot)
        answer = self.yes(question)
        if zone_bits[0]:
            self.bad(f"the tap also pressed {names(zone_bits[0])} on the PC.")
        else:
            self.ok("the tap sent no button to the PC.")
        self.record(label, answer, screenshot=path, zone_bits=names(zone_bits[0]), packets=self.link.packets - before)

    def lit(self, label: str, instruction: str, want: int, shot: str, question: str) -> None:
        self.header(label)
        self.wait_release()
        self.say(instruction)
        self.say(f"{DIM}Waiting for {names(want)}... keep holding until I say so.{RESET}")
        r = self.wait_bits(want, hold_s=0.5)
        if not r["got"]:
            self.bad(f"{names(want)} never arrived together. Seen: {names(int(r['other']))}.")
            self.record(label, False, seen=names(int(r["other"])))
            return
        self.say("Got it. Keep holding, taking a screenshot...")
        path = self.screenshot(shot)
        self.ok("screenshot taken. You can let go." if path else "the PC received it. You can let go.")
        answer = self.yes(question)
        self.record(label, answer, screenshot=path)

    def run(self) -> None:
        self.total = 11
        print(f"{BOLD}VitaPad NG hands-on check{RESET}")
        print("Hold the Vita as you normally play. Each step tells you one thing to do and waits for it.")
        print("To skip a step, just wait: it gives up by itself after 40 seconds.")
        self.ask("Press Enter to start.")
        self.link.pump(1.0)
        if self.link.pad is None:
            print(f"{RED}The Vita is not sending anything. Is VitaPad NG open on it?{RESET}")
            return

        self.zone("top left", L1)
        self.zone("top right", R1)
        self.zone("bottom left", L3)
        self.zone("bottom right", R3)
        self.slide()
        self.centre_tap("tap the centre for the stats", "stats.png",
                        "Did four lines of stats appear near the bottom of the Vita screen?")
        self.centre_tap("tap the centre again for the full readout", "diag.png",
                        "Did the screen change to a full page of numbers?")
        self.centre_tap("tap the centre a third time to close it", "closed.png",
                        "Did the numbers disappear?")
        self.lit("hold some buttons",
                 f"Hold {BOLD}Cross{RESET}, the {BOLD}L shoulder{RESET} and {BOLD}D-pad right{RESET} together.",
                 CROSS | VITA_L | RIGHT, "lit-buttons.png",
                 "On the Vita, did Cross, the left trigger and the right D-pad arm light up clearly?")
        self.lit("hold two touch zones",
                 f"Put one thumb on the {BOLD}top left{RESET} and the other on the {BOLD}bottom right{RESET} and hold.",
                 L1 | R3, "lit-zones.png",
                 "Did the L1 band (top left of the controller) and the right stick with a ring light up?")
        self.header("sticks")
        self.wait_release()
        self.say(f"Push the {BOLD}left stick{RESET} fully to one side and hold it.")
        moved = self.link.pump(self.timeout, lambda p: abs(p.lx - 128) > 90 or abs(p.ly - 128) > 90)
        path = self.screenshot("lit-stick.png") if moved else None
        answer = self.yes("Is the moving stick cap bright enough to see on the Vita?") if moved else False
        self.record("stick cap visible", answer, screenshot=path, moved=moved)

        note = self.ask("Anything that looked wrong or ugly? Type it (or just press Enter).")
        self.record("free comment", None, comment=note)
        self.record("front touch points forwarded", self.link.front_points_seen == 0,
                    packets_with_points=self.link.front_points_seen, packets=self.link.packets,
                    reconnects=self.link.reconnects)

    def finish(self) -> None:
        os.makedirs(self.out, exist_ok=True)
        with open(os.path.join(self.out, "results.json"), "w") as f:
            json.dump({"results": self.results}, f, indent=1)
        print(f"\n{BOLD}Summary{RESET}")
        for r in self.results:
            p = r["passed"]
            mark = f"{GREEN}OK{RESET}" if p is True else f"{RED}NOT OK{RESET}" if p is False else f"{DIM}--{RESET}"
            print(f"  {mark:>6}  {r['step']}")
        print("\nDone. The results are saved; you can go back to the chat.")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host-file", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--vu")
    ap.add_argument("--answers", help="read answers from lines appended to this file instead of the keyboard")
    ap.add_argument("--timeout", type=float, default=40, help="seconds a step waits before giving up")
    args = ap.parse_args()
    with open(args.host_file) as f:
        host = f.read().strip()
    os.makedirs(args.out, exist_ok=True)
    link = Link(host)
    try:
        link.connect()
    except vm.ConnectionFailure as e:
        print(f"{RED}Could not reach the Vita: {e}{RESET}")
        return 2
    wiz = Wizard(link, args.out, args.vu, args.timeout, args.answers)
    try:
        wiz.run()
    except vm.ConnectionFailure as e:
        print(f"\n{RED}Lost the Vita: {e}{RESET}")
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        wiz.finish()
        link.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
