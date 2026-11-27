#!/usr/bin/env python3
"""Services that run only while needed, and a status bar that starts no programs.

    demand_test.py <zeroshell built with -DZS_BACKEND=device> <fakedev.so>

Runs the device build with its own hardware layer (--real-hal) against a
made-up /sys and /proc (ZS_SYSROOT) and stand-ins for systemctl, wpctl,
pw-cli, nmcli and bluetoothctl that log what they were asked. Checks
(docs/PERFORMANCE.md, "Started when needed"):

  - with nothing changing, no program is started to fill the status bar;
    a network change is still seen within a few seconds
  - bluetoothd is stopped while Bluetooth is off and started with it
  - PipeWire is stopped when nothing uses it, never under an open stream,
    and a volume key pressed while it is stopped starts it and sets the
    volume, without asking it first
"""
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from device_test import Device  # noqa: E402

GRACE = 3   # seconds unneeded before a service is stopped (60 on the device)
TOOLS = """sh bash dash cat grep egrep sed awk mawk gawk head tail tr cut ls sleep date id stat test [ printf
echo env wc sort uniq basename dirname readlink realpath mkdir rm cp mv ln find xargs timeout true false
df du uname touch chmod mktemp tee seq expr ps kill pgrep nproc free""".split()
# Logged, and failing: none of them may be needed for what is tested here.
STUBS = "nmcli bluetoothctl rfkill hostname ip busctl sudo pkexec journalctl loginctl".split()

# A process is "running" while its made-up /proc/<pid>/comm holds its name.
SYSTEMCTL = r'''#!/bin/sh
echo "systemctl $*" >> "$FAKE/commands.log"
case "$*" in
    "--user is-active pipewire.socket") echo active ;;
    "--user start pipewire.service") echo pipewire > "$FAKE/proc/900/comm" ;;
    "--user stop "*"pipewire.service") echo gone > "$FAKE/proc/900/comm" ;;
    "start bluetooth.service") echo bluetoothd > "$FAKE/proc/433/comm" ;;
    "stop bluetooth.service") echo gone > "$FAKE/proc/433/comm" ;;
    *) exit 1 ;;
esac
'''
WPCTL = r'''#!/bin/sh
echo "wpctl $*" >> "$FAKE/commands.log"
grep -qx pipewire "$FAKE/proc/900/comm" || exit 1   # no PipeWire: nothing answers
case "$1" in
    get-volume) echo "Volume: $(cat "$FAKE/volume")$(grep -qx 1 "$FAKE/muted" && echo ' [MUTED]')" ;;
    set-volume) printf '0.%02d\n' "${3%\%}" > "$FAKE/volume" ;;
    set-mute) echo "$3" > "$FAKE/muted" ;;
esac
'''
PWCLI = r'''#!/bin/sh
echo "pw-cli $*" >> "$FAKE/commands.log"
grep -qx pipewire "$FAKE/proc/900/comm" || exit 1
printf '\tid 52, type PipeWire:Interface:Node/3\n \t\tmedia.class = "Audio/Sink"\n'
[ -e "$FAKE/stream" ] && printf '\tid 71, type PipeWire:Interface:Node/3\n \t\tmedia.class = "Stream/Output/Audio"\n'
exit 0
'''


def put(root, path, text):
    full = root + path
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "w") as f:
        f.write(text + "\n")


def main():
    binary, shim = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    import tempfile
    fake = tempfile.mkdtemp(prefix="zs-demand-")
    bindir = os.path.join(fake, "bin")
    os.makedirs(bindir)
    for t in TOOLS:
        p = shutil.which(t)
        if p:
            os.symlink(p, os.path.join(bindir, t))
    log = os.path.join(fake, "commands.log")
    open(log, "w").close()
    for s in STUBS:
        with open(os.path.join(bindir, s), "w") as f:
            f.write(f'#!/bin/sh\necho "{s} $*" >> "{log}"\nexit 1\n')
    for name, text in (("systemctl", SYSTEMCTL), ("wpctl", WPCTL), ("pw-cli", PWCLI)):
        with open(os.path.join(bindir, name), "w") as f:
            f.write(text)
    for n in os.listdir(bindir):
        if not os.path.islink(os.path.join(bindir, n)):
            os.chmod(os.path.join(bindir, n), 0o755)

    # The device as it is a while after boot: Wi-Fi on and connected,
    # Bluetooth switched off with bluetoothd still there from boot, PipeWire
    # running with a program's stream open, nothing playing.
    bt_soft = "/sys/class/rfkill/rfkill0/soft"
    put(fake, "/sys/class/rfkill/rfkill0/type", "bluetooth")
    put(fake, bt_soft, "1")
    put(fake, "/sys/class/rfkill/rfkill0/hard", "0")
    put(fake, "/sys/class/rfkill/rfkill1/type", "wlan")
    put(fake, "/sys/class/rfkill/rfkill1/soft", "0")
    put(fake, "/sys/class/rfkill/rfkill1/hard", "0")
    put(fake, "/sys/class/net/wlan0/operstate", "up")
    put(fake, "/sys/class/net/wlan0/carrier_changes", "2")
    put(fake, "/sys/class/bluetooth/hci0/type", "primary")
    put(fake, "/proc/asound/card0/pcm0p/sub0/status", "closed")
    put(fake, "/proc/asound/card0/pcm0c/sub0/status", "closed")
    put(fake, "/proc/433/comm", "bluetoothd")
    put(fake, "/proc/900/comm", "pipewire")
    put(fake, "/volume", "0.45")
    put(fake, "/muted", "0")
    put(fake, "/stream", "")

    failures = []

    def check(what, ok, detail=""):
        print(("ok   " if ok else "FAIL ") + what + (f": {detail}" if detail and not ok else ""))
        if not ok:
            failures.append(what)

    def calls(prefix=""):
        with open(log) as f:
            return [line.rstrip("\n") for line in f if line.startswith(prefix)]

    def wait_for(prefix, seconds):
        end = time.time() + seconds
        while time.time() < end:
            if calls(prefix):
                return True
            time.sleep(0.2)
        return False

    def forget():
        open(log, "w").close()

    env = {"PATH": bindir, "FAKE": fake, "ZS_SYSROOT": fake, "ZS_DEMAND": "1", "ZS_DEMAND_GRACE": str(GRACE)}
    d = Device(binary, shim, None, hal="--real-hal", env_extra=env)
    settings = os.path.join(d.dir, ".config", "zeroos", "settings.conf")
    stop_audio = "systemctl --user stop pipewire-pulse.service wireplumber.service pipewire.service"
    try:
        # --- Bluetooth is off: bluetoothd goes. PipeWire has a stream: it stays.
        check("bluetoothd is stopped while Bluetooth is off",
              wait_for("systemctl stop bluetooth.service", GRACE + 8), "\n".join(calls()))
        check("still running", d.proc.poll() is None, d.read_log())
        wait_for("pw-cli ls Node", GRACE + 6)
        time.sleep(2 * GRACE + 4)
        check("PipeWire was looked at closely before stopping it", bool(calls("pw-cli ls Node")))
        check("PipeWire is not stopped under an open stream", not calls(stop_audio), "\n".join(calls("systemctl")))
        check("the status came from nmcli at the start", bool(calls("nmcli radio wifi")))
        check("addresses and radio switches are read, not asked for",
              not calls("hostname") and not calls("rfkill") and not calls("ip "), "\n".join(calls()))
        check("Bluetooth off: bluetoothctl is not run", not calls("bluetoothctl"), "\n".join(calls("bluetoothctl")))

        # --- Nothing changes: no program is started for the status bar.
        # (One look every 30 s remains, for the signal strength. It was one every 2 s.)
        forget()
        time.sleep(8)
        check("nothing changed: the status is not asked for again", len(calls("nmcli radio wifi")) <= 1,
              "\n".join(calls("nmcli")))
        check("the mute icon doesn't ask PipeWire every second", len(calls("wpctl get-volume")) <= 3,
              "\n".join(calls("wpctl")))
        forget()
        # A network change is seen at once.
        put(fake, "/sys/class/net/wlan0/carrier_changes", "4")
        check("Wi-Fi dropped and rejoined: the status is read again", wait_for("nmcli radio wifi", 5))
        check("without starting a scan", all("--rescan no" in c for c in calls("nmcli -t -f ACTIVE")), "\n".join(calls("nmcli")))

        # --- The stream closes: PipeWire goes, and its volume is remembered.
        os.unlink(fake + "/stream")
        check("PipeWire is stopped when nothing uses it", wait_for(stop_audio, 2 * GRACE + 8), "\n".join(calls()))
        end = time.time() + 10
        saved = ""
        while time.time() < end and "audio.volume=45" not in saved:
            time.sleep(0.3)
            saved = open(settings).read() if os.path.exists(settings) else ""
        check("its volume is kept in settings.conf", "audio.volume=45" in saved, saved)

        # --- Volume down while it is stopped: started, then set to 35%.
        forget()
        time.sleep(1.0)
        d.tap(114)   # KEY_VOLUMEDOWN, as Fn+S sends it
        check("a volume key starts PipeWire", wait_for("systemctl --user start pipewire.service", 5), "\n".join(calls()))
        check("and sets the new volume", wait_for("wpctl set-volume @DEFAULT_AUDIO_SINK@ 35%", 5), "\n".join(calls()))
        seen = calls()
        start = seen.index("systemctl --user start pipewire.service") if "systemctl --user start pipewire.service" in seen else 0
        check("PipeWire was not asked for the volume while stopped",
              not any(c.startswith("wpctl") for c in seen[:start]), "\n".join(seen))
        check("the speaker has the new volume", open(fake + "/volume").read().strip() == "0.35")
        # Left alone, it goes again.
        check("PipeWire is stopped again after the volume change", wait_for(stop_audio, 2 * GRACE + 8), "\n".join(calls()))

        # --- Bluetooth on: bluetoothd and PipeWire come with it, and stay.
        forget()
        put(fake, bt_soft, "0")
        check("Bluetooth on: bluetoothd is started", wait_for("systemctl start bluetooth.service", 6), "\n".join(calls()))
        check("Bluetooth on: PipeWire is started (a headset needs it there)",
              wait_for("systemctl --user start pipewire.service", 6), "\n".join(calls()))
        check("Bluetooth on: connected devices are asked for", wait_for("bluetoothctl devices Connected", 8))
        time.sleep(2 * GRACE + 4)
        check("neither is stopped while Bluetooth is on",
              not calls("systemctl stop bluetooth.service") and not calls(stop_audio), "\n".join(calls("systemctl")))

        check("still running at the end", d.proc.poll() is None, d.read_log())
    finally:
        rc = d.stop()
    check("stops cleanly", rc == 0, f"exit {rc}\n{d.read_log()}")
    shutil.rmtree(fake, ignore_errors=True)
    if failures:
        print(f"\n{len(failures)} failed")
        return 1
    print("\ndemand_test: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
