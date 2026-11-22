#!/usr/bin/env python3
"""Run the real device build of ZeroShell against a fake LCD and keyboard.

    device_test.py <zeroshell built with -DZS_BACKEND=device> <fakedev.so> [screenshot-dir]

Sends the key events M5's keyboard driver sends, and checks that ZeroShell
starts, draws, and that typed text reaches the Notes app (and its file).
The hardware layer is the simulated one (--mock); only the screen and
keyboard paths are the device's own.
"""
import os
import struct
import subprocess
import sys
import tempfile
import time

W, H = 320, 170
EV_SYN, EV_KEY = 0, 1
KEY = dict(esc=1, enter=28, space=57, shift=42, sym=0x17f, fn=0x185, right=106, left=105, up=103, down=108,
           backspace=14)
ROWS = {16: "qwertyuiop", 30: "asdfghjkl", 44: "zxcvbnm", 2: "1234567890"}
CHAR = {c: base + i for base, row in ROWS.items() for i, c in enumerate(row)}
CHAR[" "] = 57
# Sym layer: the stand-in codes the driver sends (keys.cpp).
SYM = {"!": 26, "@": 27, "#": 39, "-": 71, "_": 70, "?": 93, ".": 91, ",": 90, "/": 92, ":": 80}


class Device:
    def __init__(self, binary, shim, shots, hal="--mock", env_extra=None):
        self.shots = shots
        self.dir = tempfile.mkdtemp(prefix="zs-device-")
        self.fb = os.path.join(self.dir, "fb")
        self.kb = os.path.join(self.dir, "kb")
        with open(self.fb, "wb") as f:
            f.write(bytes(W * H * 2))
        os.mkfifo(self.kb)
        env = dict(os.environ, LD_PRELOAD=shim, ZS_FRAMEBUFFER=self.fb, ZS_KEYBOARD=self.kb, HOME=self.dir)
        env.update(env_extra or {})
        # A device build makes the installable apps (Notes among them) as modules
        # next to the binary; load them from there, as if they were installed.
        apps = os.path.join(os.path.dirname(os.path.abspath(binary)), "apps")
        if os.path.isdir(apps) and "ZS_APP_DIR" not in env:
            env["ZS_APP_DIR"] = apps
        self.log = open(os.path.join(self.dir, "log"), "w")
        self.proc = subprocess.Popen([binary, hal], env=env, stdin=subprocess.DEVNULL,
                                     stdout=self.log, stderr=subprocess.STDOUT)
        self.out = os.open(self.kb, os.O_WRONLY)   # blocks until ZeroShell opens the keyboard

    def event(self, etype, code, value):
        os.write(self.out, struct.pack("llHHi", 0, 0, etype, code, value))

    def key(self, code, value):
        self.event(EV_KEY, code, value)
        self.event(EV_SYN, 0, 0)

    def tap(self, code, hold=0.03, gap=0.03):
        self.key(code, 1)
        time.sleep(hold)
        self.key(code, 0)
        time.sleep(gap)

    def nav(self, name, times=1):
        # Arrows are on the Fn layer: Fn down, the arrow's own code, Fn up.
        fn = name in ("right", "left", "up", "down")
        for _ in range(times):
            if fn:
                self.key(KEY["fn"], 1)
            self.tap(KEY[name], gap=0.08)
            if fn:
                self.key(KEY["fn"], 0)

    def type(self, text):
        for ch in text:
            if ch in SYM:
                self.key(KEY["sym"], 1)
                self.tap(SYM[ch])
                self.key(KEY["sym"], 0)
            elif ch.lower() in CHAR:
                if ch.isupper():
                    self.key(KEY["shift"], 1)
                self.tap(CHAR[ch.lower()])
                if ch.isupper():
                    self.key(KEY["shift"], 0)
            else:
                raise ValueError(f"no key for {ch!r}")

    def burst(self, text):
        """Fast typing: every event at once, each key down before the last is up."""
        codes = [CHAR[c] for c in text]
        for i, c in enumerate(codes):
            self.event(EV_KEY, c, 1)
            if i:
                self.event(EV_KEY, codes[i - 1], 0)
            self.event(EV_SYN, 0, 0)
        self.key(codes[-1], 0)
        time.sleep(0.3)

    def frame(self):
        with open(self.fb, "rb") as f:
            return f.read()

    def shot(self, name):
        if not self.shots:
            return
        from PIL import Image
        raw = self.frame()
        img = Image.new("RGB", (W, H))
        px = []
        for i in range(0, len(raw), 2):
            v = raw[i] | raw[i + 1] << 8
            px.append(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
        img.putdata(px)
        os.makedirs(self.shots, exist_ok=True)
        img.resize((W * 2, H * 2), Image.NEAREST).save(os.path.join(self.shots, name + ".png"))

    def stop(self):
        self.proc.terminate()
        try:
            rc = self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            rc = "did not stop"
        os.close(self.out)
        self.log.close()
        return rc

    def read_log(self):
        with open(os.path.join(self.dir, "log")) as f:
            return f.read()


def main():
    binary, shim = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    shots = sys.argv[3] if len(sys.argv) > 3 else None
    failures = []

    def check(what, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + what + (f": {detail}" if detail and not ok else ""))
        if not ok:
            failures.append(what)

    d = Device(binary, shim, shots)
    try:
        time.sleep(1.0)
        check("still running after start", d.proc.poll() is None, d.read_log())
        home = d.frame()
        check("draws to the screen", any(home), "framebuffer is all black")
        d.shot("1-home")

        d.nav("right")
        time.sleep(0.3)
        moved = d.frame()
        check("arrow key moves the launcher", moved != home)

        # Notes is the 5th app: 3 more to the right, then Enter.
        d.nav("right", 3)
        d.nav("enter")
        time.sleep(0.6)
        before = d.frame()
        d.shot("2-notes")
        text = "Hello Cardputer 42, typing works_fine!"
        d.type(text)
        time.sleep(0.4)
        check("typing changes the screen", d.frame() != before)
        d.burst("fastrollover")
        d.shot("3-notes-typed")
        d.nav("esc")   # leaving Notes saves
        time.sleep(0.6)
        d.shot("4-home-again")
        note = os.path.join(d.dir, "Notes", "notes.txt")
        saved = open(note).read() if os.path.exists(note) else ""
        want = text + "fastrollover"
        check("typed text is in the note", want in saved, f"wanted {want!r} in {saved!r}")

    finally:
        rc = d.stop()
    check("stops cleanly on SIGTERM", rc == 0, f"exit {rc}")
    if failures:
        print(d.read_log())
        sys.exit(1)
    print("device_test: all passed")


if __name__ == "__main__":
    main()
