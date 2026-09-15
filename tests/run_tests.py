#!/usr/bin/env python3
"""
Boot TermuOS under QEMU, type commands on the emulated PS/2 keyboard and
assert on what comes back over the serial port.

Usage:  python3 tests/run_tests.py [--iso termuos.iso] [--img test.img]

Exits 0 if every check passed, 1 otherwise. Normally run via `make test`,
which builds the ISO and a fresh disk image with the fixtures installed.
"""
import argparse
import os
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BANNER = b"type 'help'"
PANIC = "KERNEL PANIC"

# Characters the QEMU monitor names differently from the character itself.
SHIFTED = {'!': '1', '@': '2', '#': '3', '$': '4', '%': '5', '^': '6',
           '&': '7', '*': '8', '(': '9', ')': '0', '_': 'minus',
           '+': 'equal', '{': 'bracket_left', '}': 'bracket_right',
           '|': 'backslash', ':': 'semicolon', '"': 'apostrophe',
           '<': 'comma', '>': 'dot', '?': 'slash', '~': 'grave_accent'}
PLAIN = {' ': 'spc', '-': 'minus', '=': 'equal', '[': 'bracket_left',
         ']': 'bracket_right', '\\': 'backslash', ';': 'semicolon',
         "'": 'apostrophe', ',': 'comma', '.': 'dot', '/': 'slash',
         '`': 'grave_accent', '\n': 'ret'}


def keyname(ch):
    if ch.isupper():
        return 'shift-' + ch.lower()
    if ch in SHIFTED:
        return 'shift-' + SHIFTED[ch]
    if ch in PLAIN:
        return PLAIN[ch]
    if ch.isalnum():
        return ch
    raise ValueError('no key mapping for %r' % ch)


# Each case: (command, [substrings that must appear in its output]).
# "KERNEL PANIC" must never appear, in any case's output.
CASES = [
    ("echo hello world",
     ["hello world", "exited with code 0"]),

    ("uname",
     ["TermuOS", "exited with code 0"]),

    # Issue #41's acceptance criterion.
    ("run /bin/cat.tsys /etc/motd",
     ["Welcome to TermuOS", "MOTD read via sys_read", "exited with code 0"]),

    # 1008 bytes: more than one capped read.
    ("run /bin/cat.tsys /etc/big.txt",
     ["line 01 ", "line 12 ", "line 24 ", "exited with code 0"]),

    # 4600 bytes: crosses the 4096-byte TFS block boundary as well.
    ("run /bin/cat.tsys /etc/block.txt",
     ["block-boundary row 001", "block-boundary row 050",
      "block-boundary row 100", "exited with code 0"]),

    # A file with no contents must be an immediate EOF, not a hang.
    ("run /bin/cat.tsys /etc/empty.txt",
     ["exited with code 0"]),

    # Several files in one go.
    ("run /bin/cat.tsys /etc/motd /etc/empty.txt /etc/big.txt",
     ["Welcome to TermuOS", "line 24 ", "exited with code 0"]),

    # A missing file must report an error and a non-zero status.
    ("run /bin/cat.tsys /etc/does-not-exist",
     ["cat: ", "exited with code 1"]),

    # read() routing and the descriptor reservation (issue #41).
    ("run /bin/readtest.tsys",
     ["readtest done", "exited with code 0"]),

    # write() routing and file writes (issue #42).
    ("run /bin/writetest.tsys",
     ["writetest done", "exited with code 0"]),

    # open()/close() (issue #43). fdleak runs first on purpose: it exits with
    # descriptors still open, so if exiting leaks them, opentest's "first
    # descriptor is 3" check below fails.
    ("run /bin/fdleak.tsys",
     ["fdleak: opened without closing", "exited with code 0"]),

    ("run /bin/opentest.tsys",
     ["opentest done", "exited with code 0"]),

    # User-buffer helpers (issue #40).
    ("run /bin/uatest.tsys",
     ["uatest done", "exited with code 0"]),
]


def boot(iso, img, port, logpath):
    if os.path.exists(logpath):
        os.remove(logpath)
    qemu = subprocess.Popen([
        "qemu-system-x86_64", "-cdrom", iso,
        "-cpu", "qemu64,+syscall",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-drive", "file=%s,format=raw,if=ide" % img,
        "-serial", "file:" + logpath,
        "-display", "none",
        "-monitor", "tcp:127.0.0.1:%d,server,nowait" % port,
    ], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)

    deadline = time.time() + 90
    while time.time() < deadline:
        if qemu.poll() is not None:
            raise SystemExit("qemu exited early (rc=%s)" % qemu.returncode)
        if os.path.exists(logpath):
            with open(logpath, 'rb') as f:
                if BANNER in f.read():
                    time.sleep(1.0)
                    return qemu
        time.sleep(0.5)
    qemu.kill()
    raise SystemExit("timed out waiting for the shell prompt")


def read_log(logpath):
    with open(logpath, 'rb') as f:
        return f.read().decode('utf-8', 'replace')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=os.path.join(ROOT, "termuos.iso"))
    ap.add_argument("--img", default=os.path.join(ROOT, "test.img"))
    ap.add_argument("--port", type=int, default=55731)
    ap.add_argument("--settle", type=float, default=3.0,
                    help="seconds to wait for each command to finish")
    args = ap.parse_args()

    for path in (args.iso, args.img):
        if not os.path.exists(path):
            raise SystemExit("missing %s - run `make test` instead" % path)

    logpath = os.path.join(ROOT, "tests-serial.log")
    qemu = boot(args.iso, args.img, args.port, logpath)
    mon = socket.create_connection(("127.0.0.1", args.port), timeout=10)
    time.sleep(0.5)
    mon.recv(65536)

    failures = []
    try:
        for command, expected in CASES:
            before = len(read_log(logpath))
            for ch in command + "\n":
                mon.sendall(("sendkey %s\n" % keyname(ch)).encode())
                time.sleep(0.035)
            time.sleep(args.settle)
            output = read_log(logpath)[before:]

            missing = [e for e in expected if e not in output]
            panicked = PANIC in output
            if missing or panicked:
                failures.append((command, missing, panicked, output))
                print("FAIL  %s" % command)
                if panicked:
                    print("      kernel panicked")
                for m in missing:
                    print("      expected in output: %r" % m)
            else:
                print("ok    %s" % command)
    finally:
        try:
            mon.sendall(b"quit\n")
            time.sleep(0.5)
        except OSError:
            pass
        qemu.kill()
        qemu.wait()

    # Per-check lines printed by the test programs themselves.
    whole = read_log(logpath)
    inner = [l for l in whole.splitlines() if l.startswith("FAIL  ")]
    for line in inner:
        print("FAIL  (in-guest) %s" % line[6:])

    print("\nserial log: %s" % logpath)
    if failures or inner:
        print("%d failing command(s), %d failing in-guest check(s)"
              % (len(failures), len(inner)))
        return 1
    print("all %d commands passed" % len(CASES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
