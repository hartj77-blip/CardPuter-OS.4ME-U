#!/usr/bin/env python3
"""Open every app on the home screen with the keyboard, in the device build.

    apps_test.py <zeroshell built with -DZS_BACKEND=device> <fakedev.so> [screenshot-dir]
        [--apps DIR]       installed app modules (default: apps/ next to the binary)
        [--expect ID,...]  apps that must be on the home screen (default: every one it loaded)
        [--path DIR]       programs the apps' own package installs (kept on the bare machine's PATH)
        [--settle SECONDS] how long an app gets before its screen is checked (default 0.9)

Runs ZeroShell twice, with the fake LCD and keyboard from device_test.py:

  1. --mock: the simulated hardware layer, everything present.
  2. --real-hal on a bare machine: the device's own hardware layer, but no
     Cardputer hardware (no camera, GPS, IR, IMU, battery gauge, radio) and
     none of the programs apps lean on (systemctl, nmcli, bluetoothctl,
     tailscale, gpsd, mpv...). Commands that would change this machine are
     stand-ins that log and fail. This is what an app sees when its
     hardware or its extras are missing.

For each app: from the home screen, open All apps, arrow to it and press Enter, check it
opened and drew something, press a few keys, then Esc back home. Then the
same for Wi-Fi and Bluetooth, which are sections of Settings and not on the
home screen: each is found with the home search. Fails if ZeroShell exits,
an app doesn't open, the screen goes black, or Esc doesn't lead home.
"""
import os
import re
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from device_test import Device  # noqa: E402

BASE_APPS = ["settings", "update", "apps"]

# What a bare Raspberry Pi OS has; everything else is "not installed".
TOOLS = """sh bash dash cat grep egrep sed awk mawk gawk head tail tr cut ls sleep date id stat test [ printf
echo env wc sort uniq basename dirname readlink realpath mkdir rm cp mv ln find xargs timeout true false
df du uname hostname touch chmod mktemp tee seq expr ps kill pgrep nproc free""".split()
# Would change this machine (or wait on a daemon): log the call and fail.
STUBS = """systemctl sudo pkexec reboot poweroff shutdown rfkill nmcli bluetoothctl wpctl journalctl
loginctl busctl dbus-send udevadm modprobe ip iw""".split()


def bare_path(d):
    bindir = os.path.join(d, "bin")
    os.makedirs(bindir)
    for t in TOOLS:
        p = shutil.which(t)
        if p:
            os.symlink(p, os.path.join(bindir, t))
    log = os.path.join(d, "commands.log")
    for s in STUBS:
        with open(os.path.join(bindir, s), "w") as f:
            f.write(f'#!/bin/sh\necho "{s} $*" >> "{log}"\nexit 1\n')
        os.chmod(os.path.join(bindir, s), 0o755)
    return bindir, log


def run(binary, shim, shots, hal, expect, apps_dir, settle, env_extra=None):
    failures = []
    label = hal.lstrip("-")

    def check(what, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + f"[{label}] {what}" + (f": {detail}" if detail and not ok else ""))
        if not ok:
            failures.append(what)
        return ok

    env = dict(env_extra or {})
    if apps_dir:
        env["ZS_APP_DIR"] = apps_dir
    d = Device(binary, shim, None, hal=hal, env_extra=env)
    d.shots = shots and os.path.join(shots, label)
    seen = 0   # how much of the log has been read

    def new_log():
        nonlocal seen
        text = d.read_log()
        out, seen = text[seen:], len(text)
        return out

    def opened(text):
        return re.findall(r"^zeroshell: open (\S+)$", text, re.M)

    try:
        time.sleep(1.2)
        start = new_log()
        if not check("starts", d.proc.poll() is None, start):
            return failures
        loaded = re.findall(r"^zeroshell: loaded app (\S+) from ", start, re.M)
        broken = re.findall(r"^zeroshell: app module .*$", start, re.M)
        check("every app module loads", not broken, "\n".join(broken))
        want = expect or BASE_APPS + loaded
        home = d.frame()
        check("home screen draws", any(home), "framebuffer is all black")

        # Home's All apps (Up from the pinned apps is its last row) lists every
        # app A-Z; the n-th is n arrows down from the first.
        visited = []
        for i in range(len(want) + 2):   # +2: a few extra, to see it wrap round
            d.nav("up")
            d.nav("enter")
            time.sleep(0.5)
            d.nav("down", i)
            d.nav("enter")
            time.sleep(settle)
            ids = [x for x in opened(new_log()) if x != "home"]
            if not ids:
                check(f"app {i + 1} on the home screen opens", False, "Enter did nothing")
                break
            app = ids[-1]
            if app in visited:
                break   # wrapped round to the first app again
            visited.append(app)
            alive = d.proc.poll() is None
            if not check(f"{app}: opens", alive, d.read_log()[-2000:]):
                break
            check(f"{app}: draws", any(d.frame()), "screen is black")
            d.shot(f"{len(visited):02d}-{app}")
            # A few keys: move about, type a letter, open what's selected.
            for k in ("down", "down", "up"):
                d.nav(k)
            d.type("a")
            d.nav("enter")
            time.sleep(0.6)
            if not check(f"{app}: takes keys", d.proc.poll() is None, d.read_log()[-2000:]):
                break
            d.shot(f"{len(visited):02d}-{app}-keys")
            # Back out: Esc until the home screen (an app may go back a page first).
            home_again = False
            for _ in range(6):
                d.nav("esc")
                time.sleep(0.35)
                if "home" in opened(new_log()):
                    home_again = True
                    break
            if not check(f"{app}: Esc leads home", home_again):
                break
            time.sleep(0.4)
        missing = [a for a in want if a not in visited]
        check(f"every app opened ({len(visited)})", not missing, "not reached: " + " ".join(missing))

        # Wi-Fi and Bluetooth are sections of Settings, not apps: type the
        # name on the home screen and Enter opens Settings at that section.
        def esc_home():
            for _ in range(6):
                d.nav("esc")
                time.sleep(0.35)
                if "home" in opened(new_log()):
                    return True
            return False
        esc_home()   # the loop above ends with the first app open again
        time.sleep(0.4)
        for section in ("wifi", "bluetooth"):
            if d.proc.poll() is not None:
                break
            new_log()
            d.type(section)
            time.sleep(0.4)
            d.nav("enter")
            time.sleep(settle)
            ids = opened(new_log())
            if not check(f"settings > {section}: found by search, opens in Settings",
                         ids == ["settings"] and d.proc.poll() is None, f"opened {ids}\n" + d.read_log()[-2000:]):
                break
            check(f"settings > {section}: draws", any(d.frame()), "screen is black")
            d.shot(f"settings-{section}")
            # Off the switch, onto the first network or device (or the next
            # value row), and Enter: join it, connect it, or step the value.
            d.nav("down")
            d.nav("enter")
            time.sleep(0.6)
            if not check(f"settings > {section}: takes keys", d.proc.poll() is None, d.read_log()[-2000:]):
                break
            d.shot(f"settings-{section}-keys")
            if not check(f"settings > {section}: Esc leads home", esc_home()):
                break
            time.sleep(0.4)
    finally:
        rc = d.stop()
    check("stops cleanly on SIGTERM", rc == 0, f"exit {rc}")
    if failures:
        print(d.read_log()[-6000:])
    return failures


def main():
    args = sys.argv[1:]
    expect, apps_dir, extra_path, settle = None, None, None, 0.9
    if "--path" in args:
        i = args.index("--path")
        extra_path = os.path.abspath(args[i + 1])
        del args[i:i + 2]
    if "--settle" in args:
        i = args.index("--settle")
        settle = float(args[i + 1])
        del args[i:i + 2]
    if "--expect" in args:
        i = args.index("--expect")
        expect = [x for x in args[i + 1].split(",") if x]
        del args[i:i + 2]
    if "--apps" in args:
        i = args.index("--apps")
        apps_dir = os.path.abspath(args[i + 1])
        del args[i:i + 2]
    binary, shim = os.path.abspath(args[0]), os.path.abspath(args[1])
    shots = args[2] if len(args) > 2 else None

    env = {"PATH": extra_path + os.pathsep + os.environ["PATH"]} if extra_path else {}
    failures = run(binary, shim, shots, "--mock", expect, apps_dir, settle, env)
    with tempfile.TemporaryDirectory(prefix="zs-bare-") as bare:
        bindir, cmdlog = bare_path(bare)
        path = bindir + (os.pathsep + extra_path if extra_path else "")
        failures += run(binary, shim, shots, "--real-hal", expect, apps_dir, settle, {"PATH": path})
        if os.path.exists(cmdlog):
            calls = sorted(set(open(cmdlog).read().split("\n")) - {""})
            print(f"[real-hal] commands apps tried (stand-ins, all failed): {len(calls)}")
            for c in calls[:40]:
                print("     " + c)
    if failures:
        print(f"apps_test: {len(failures)} failed")
        sys.exit(1)
    print("apps_test: all passed")


if __name__ == "__main__":
    main()
