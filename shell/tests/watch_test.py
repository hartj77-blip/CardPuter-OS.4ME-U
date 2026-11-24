#!/usr/bin/env python3
"""The systemd watchdog ping and the app-folder watch, in the headless build.

    watch_test.py <zeroshell built with -DZS_BACKEND=headless>

1. Watchdog: with NOTIFY_SOCKET and WATCHDOG_USEC set, as systemd sets them,
   ZeroShell sends "WATCHDOG=1" to the socket when it starts
   (src/platform/watchdog.cpp).
2. Home screen: an M5 launcher entry written while home is open, into a folder
   that did not exist when ZeroShell started, makes ZeroShell read the app
   list again within a few seconds (src/ui/app_watch.cpp).
3. Another app open: the same change waits until home is opened again.

The headless build checks the folder once a second instead of in poll()
(platform_watch_fd), so the timings here are looser than on the device.
"""
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time

BIN = os.path.abspath(sys.argv[1])
ENTRY = "[Desktop Entry]\nType=Application\nName=Watch Test\nExec=/bin/true\n"
failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    failures += 0 if ok else 1


def run(script, env, tmp):
    return subprocess.run([BIN, "--script", script], env=env, cwd=tmp, capture_output=True, text=True, timeout=60)


with tempfile.TemporaryDirectory() as tmp:
    env = dict(os.environ, HOME=tmp)
    env.pop("NOTIFY_SOCKET", None)

    # 1. The watchdog's first ping.
    path = os.path.join(tmp, "notify")
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    sock.bind(path)
    sock.settimeout(10)
    run("home,wait:100", dict(env, NOTIFY_SOCKET=path, WATCHDOG_USEC="10000000"), tmp)
    try:
        msg = sock.recv(256).decode()
    except socket.timeout:
        msg = ""
    check(msg == "WATCHDOG=1", "sends WATCHDOG=1 to systemd's socket at start (got %r)" % msg)

    # 2 and 3. A launcher entry appears in a folder made after start.
    for n, (script, want) in enumerate([
        ("home,live:6000", "apps changed on disk, reading them again"),
        ("open:settings,live:5000,home,wait:100", "apps changed on disk while away"),
    ]):
        apps = os.path.join(tmp, "m5-%d" % n, "applications")
        notes = os.path.join(tmp, "notes-%d" % n)
        os.makedirs(notes)
        e = dict(env, ZS_M5_APP_DIR=apps, ZS_M5_STATE_DIR=notes, ZS_APP_DIR=os.path.join(tmp, "none"))

        def later():
            time.sleep(1.5)
            os.makedirs(apps)
            with open(os.path.join(apps, "watch_test.desktop"), "w") as f:
                f.write(ENTRY)

        t = threading.Thread(target=later)
        t.start()
        out = run(script, e, tmp).stderr
        t.join()
        if n == 0:
            check(want in out, "home reads the app list again after a new launcher entry")
        else:
            at_settings = out.find("open settings")
            at_home = out.rfind("open home")
            seen = out.find(want)
            check(seen > at_home > at_settings >= 0 and "reading them again\n" not in out[:at_home],
                  "in another app, the list is read again only on the way home")

print("watch_test: " + ("all passed" if failures == 0 else "%d failed" % failures))
sys.exit(1 if failures else 0)
