#!/usr/bin/env python3
"""Run the real device build of ZeroShell against a fake LCD and keyboard.

    device_test.py <zeroshell built with -DZS_BACKEND=device> <fakedev.so> [screenshot-dir]

Sends the key events M5's keyboard driver sends, and checks that ZeroShell
starts, draws, and that typed text reaches the Notes app (and its file).
The hardware layer is the simulated one (--mock); only the screen and
keyboard paths are the device's own.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile
import time

W, H = 320, 170
EV_SYN, EV_KEY = 0, 1
KEY = dict(esc=1, enter=28, space=57, shift=42, ctrl=29, sym=0x17f, fn=0x185, right=106, left=105, up=103, down=108,
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

        # Home's search: type a name, Enter opens the first result.
        d.type("notes")
        time.sleep(0.3)
        d.shot("1b-search")
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
        # Ctrl + Space opens the taskbar panel, even from a text box, and
        # types nothing into it (docs/UX.md).
        typed_frame = d.frame()
        d.key(KEY["ctrl"], 1)
        d.tap(KEY["space"])
        d.key(KEY["ctrl"], 0)
        time.sleep(0.4)
        check("ctrl+space opens the taskbar panel", d.frame() != typed_frame)
        d.shot("4-panel")
        # Esc held down goes home (and leaving Notes saves). A tap would only
        # close the panel.
        log_before = len(d.read_log())
        d.tap(KEY["esc"], hold=1.0)
        time.sleep(0.6)
        check("held esc goes home", "zeroshell: open home" in d.read_log()[log_before:], d.read_log()[log_before:])
        d.shot("5-home-again")
        # Tapping Ctrl (M5's driver sends it as a plain key) waits for the
        # next key: the status bar says Ctrl, and Space then opens the panel.
        # Compare rows below the status bar only (the clock can tick).
        body = slice(W * 2 * 20, W * 2 * 80)
        home_body, home_bar = d.frame()[body], d.frame()[:W * 2 * 20]
        d.tap(KEY["ctrl"])
        time.sleep(0.4)
        d.shot("6-ctrl-tapped")
        check("tapped ctrl shows in the status bar", d.frame()[:W * 2 * 20] != home_bar)
        d.tap(KEY["space"])
        time.sleep(0.4)
        d.shot("7-panel-from-tapped-ctrl")
        check("tap ctrl, then space opens the panel", d.frame()[body] != home_body)
        d.tap(KEY["esc"])
        time.sleep(0.4)
        check("esc closes the panel", d.frame()[body] == home_body)

        # Lists number their rows like a phone menu: Settings' 7th row (off
        # the screen) is Software update. Esc there comes back to Settings,
        # and Esc in Settings goes home.
        def opened_since(mark):
            return re.findall(r"^zeroshell: open (\S+)$", d.read_log()[mark:], re.M)
        mark = len(d.read_log())
        d.type("settings")
        time.sleep(0.3)
        d.nav("enter")
        time.sleep(0.6)
        d.shot("8-settings")
        d.type("7")
        time.sleep(0.6)
        d.shot("9-update-from-settings")
        d.nav("esc")
        time.sleep(0.6)
        d.nav("esc")
        time.sleep(0.6)
        got = opened_since(mark)
        check("search opens Settings, 7 opens its 7th row, Esc goes back, then home",
              got == ["settings", "update", "settings", "home"], str(got))
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
