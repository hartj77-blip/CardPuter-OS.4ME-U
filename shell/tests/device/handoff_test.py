#!/usr/bin/env python3
"""An M5Stack app has the screen, the screen dims, the app closes: home is drawn again.

    handoff_test.py <zeroshell built with -DZS_BACKEND=device> <fakedev.so> [screenshot-dir]

Opening an app made for M5Stack's launcher hands it the LCD (ui/m5_apps.cpp);
ZeroShell draws nothing until the app exits. This runs ZeroShell with the fake
LCD and keyboard from device_test.py, the device's own hardware layer
(--real-hal: the simulated one starts no programs) and a stand-in M5 app that
paints the LCD black and waits to be told to exit.

It checks what went wrong on a Cardputer: the screen dimmed while the app was
open, and after the app closed the screen stayed black until a restart, with
the keys still working. And the other way round: after the screen has dimmed
on the home screen, ZeroShell must not draw over an app that has the screen.
Last, the app closing while the screen is off.

And an app that opens the LCD's DRM card by the number it has on M5's system
(/dev/dri/card1; AdvanceWebUI crashed on ZeroOS, where that is HDMI's): the app
gets the LCD's card under that name (hal/lcd_drm.h, platform/m5_drm_shim.c).
Two plain files stand in for the cards.
"""
import os
import re
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from apps_test import bare_path  # noqa: E402
from device_test import KEY, Device, H, W  # noqa: E402

DIM_AFTER = 2    # seconds without a key until the screen dims (Settings > Display)
OFF_AFTER = 6    # ...and until it goes off
# Home's row of pinned apps: the same each time home is drawn (the status bar
# and the big clock can tick, and the app joins the recent ones below).
PINNED = slice(W * 2 * 46, W * 2 * 80)

# The stand-in app: black over the whole LCD, as an app does when it starts
# and when it leaves; exits when the test makes the "stop" file.
APP = """import os, sys, time
here = os.path.dirname(os.path.abspath(__file__))
def black():
    with open(os.environ["LV_LINUX_FBDEV_DEVICE"], "r+b") as fb:
        fb.write(bytes(%d))
black()
# On M5's system this app opens "/dev/dri/card1" for the LCD: having that name
# in it is what makes ZeroShell swap the cards for it. Which one does each
# name give? (The test's two files stand in for /dev/dri.)
for name in ("card1", "card0"):
    with open(os.path.join(here, name)) as card, open(os.path.join(here, name + ".was"), "w") as out:
        out.write(card.read())
open(os.path.join(here, "started"), "w").close()
while not os.path.exists(os.path.join(here, "stop")):
    time.sleep(0.05)
black()
""" % (W * H * 2)


def main():
    binary, shim = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    shots = sys.argv[3] if len(sys.argv) > 3 else None
    failures = []

    def check(what, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + what + (f": {detail}" if detail and not ok else ""))
        if not ok:
            failures.append(what)
        return ok

    with tempfile.TemporaryDirectory(prefix="zs-handoff-") as tmp:
        home = os.path.join(tmp, "home")
        entries = os.path.join(tmp, "applications")
        notes = os.path.join(tmp, "m5")
        os.makedirs(os.path.join(home, ".config", "zeroos"))
        os.makedirs(entries)
        os.makedirs(notes)
        with open(os.path.join(home, ".config", "zeroos", "settings.conf"), "w") as f:
            f.write(f"display.dim_after={DIM_AFTER}\ndisplay.off_after={OFF_AFTER}\n")
        app = os.path.join(tmp, "app.py")
        with open(app, "w") as f:
            f.write(APP)
        with open(os.path.join(entries, "standin.desktop"), "w") as f:
            f.write(f"[Desktop Entry]\nType=Application\nName=Standin\nExec={sys.executable} {app}\n")
        started, stop = os.path.join(tmp, "started"), os.path.join(tmp, "stop")
        # On ZeroOS card1 is HDMI's and card0 the LCD's; on M5's system, the
        # other way round.
        card1, card0 = os.path.join(tmp, "card1"), os.path.join(tmp, "card0")
        for path, text in ((card1, "hdmi"), (card0, "lcd")):
            with open(path, "w") as f:
                f.write(text)
        bindir, _ = bare_path(tmp)
        d = Device(binary, shim, shots, hal="--real-hal",
                   env_extra={"HOME": home, "PATH": bindir, "ZS_M5_APP_DIR": entries, "ZS_M5_STATE_DIR": notes,
                              "ZS_M5_DRM_SWAP": f"{card1}:{card0}"})
        seen = 0   # how much of the log has been read

        def new_log():
            nonlocal seen
            text = d.read_log()
            out, seen = text[seen:], len(text)
            return out

        def open_app():
            """From home: find the app by name, open it, wait until it has painted."""
            for f in (started, stop):
                if os.path.exists(f):
                    os.remove(f)
            d.type("standin")
            time.sleep(0.3)
            d.nav("enter")
            for _ in range(60):
                if os.path.exists(started):
                    break
                time.sleep(0.05)
            time.sleep(0.2)
            return os.path.exists(started) and "M5Stack app started" in new_log()

        def close_app(key=True):
            """Esc (the key that wakes the screen), and the app exits; wait for home."""
            if key:
                d.tap(KEY["esc"])
            open(stop, "w").close()
            for _ in range(80):   # ZeroShell looks every 0.5 s, from 1.5 s after the start
                if re.search(r"^zeroshell: open home$", new_log(), re.M):
                    time.sleep(0.6)
                    return True
                time.sleep(0.05)
            return False

        def read(path):
            try:
                with open(path) as f:
                    return f.read()
            except OSError as e:
                return f"<{e.strerror}>"

        try:
            scenario(d, check, new_log, open_app, close_app)
            # Every time it ran (three), the app asked for card1 and card0.
            check("an app that opens /dev/dri/card1 gets the LCD's card", read(card1 + ".was") == "lcd",
                  f"it read {read(card1 + '.was')!r}")
            check("...and the LCD's own number gives it the other card", read(card0 + ".was") == "hdmi",
                  f"it read {read(card0 + '.was')!r}")
            check("ZeroShell says so in its log", "DRM cards swapped for it" in d.read_log())
            check("the swap library is in ~/.cache/zeroos",
                  os.path.getsize(os.path.join(home, ".cache", "zeroos", "m5-drm-shim.so")) > 1000
                  if os.path.exists(os.path.join(home, ".cache", "zeroos", "m5-drm-shim.so")) else False)
        finally:
            rc = d.stop()
        check("stops cleanly on SIGTERM", rc == 0, f"exit {rc}")
        if failures:
            print(d.read_log()[-6000:])
            sys.exit(1)
    print("handoff_test: all passed")


def scenario(d, check, new_log, open_app, close_app):
    time.sleep(1.0)
    if not check("starts", d.proc.poll() is None, d.read_log()):
        return
    new_log()
    home_row = d.frame()[PINNED]
    check("home screen draws", any(home_row), "framebuffer is all black")

    def home_again(what):
        check(what, d.frame()[PINNED] == home_row,
              "the screen is black" if not any(d.frame()) else "not the home screen")

    # 1. The reported one: open the app, leave it until the screen has dimmed,
    # close it.
    if not check("the app opens", open_app(), d.read_log()[-2000:]):
        return
    check("the app has the screen", not any(d.frame()))
    time.sleep(DIM_AFTER + 1.5)
    check("ZeroShell draws nothing over the app when the screen dims", not any(d.frame()))
    d.shot("handoff-1-app")
    check("the app closing leads home", close_app(), d.read_log()[-2000:])
    d.shot("handoff-2-home-after-dim")
    home_again("home is drawn again after the app closed")

    # 2. The screen dims on the home screen and a key wakes it, twice; then
    # the same again. ZeroShell must still keep off the app's screen.
    for _ in range(2):
        time.sleep(DIM_AFTER + 1.0)
        d.tap(KEY["esc"])
        time.sleep(0.4)
    home_again("home is drawn after dimming and waking")
    if not check("the app opens again", open_app(), d.read_log()[-2000:]):
        return
    time.sleep(DIM_AFTER + 1.5)
    check("ZeroShell still draws nothing over the app when the screen dims", not any(d.frame()))
    d.shot("handoff-3-app-again")
    check("the app closing leads home again", close_app(), d.read_log()[-2000:])
    d.shot("handoff-4-home-again")
    home_again("home is drawn again the second time")

    # 3. The app closes by itself while the screen is off; a key then wakes it.
    # (No backlight here, so ZeroShell draws "off" as a black screen.)
    time.sleep(0.5)
    if not check("the app opens a third time", open_app(), d.read_log()[-2000:]):
        return
    time.sleep(OFF_AFTER + 1.5)
    check("the app closing with the screen off leads home", close_app(key=False), d.read_log()[-2000:])
    check("the screen stays off", not any(d.frame()))
    d.tap(KEY["esc"])
    time.sleep(0.6)
    d.shot("handoff-5-home-after-off")
    home_again("home is drawn when a key wakes the screen")


if __name__ == "__main__":
    main()
