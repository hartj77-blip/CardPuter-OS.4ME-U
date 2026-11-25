#!/usr/bin/python3 -I
# m5-apps.py: write docs/M5-APPS.md, the compatibility checklist for the apps
# in M5Stack's app store (docs/APPS.md, "M5Stack's app store").
#
#   tools/m5-apps.py scan   [--jobs N] > m5-scan.jsonl   download and look inside every package
#   tools/m5-apps.py render m5-scan.jsonl > docs/M5-APPS.md
#
# scan downloads M5's registry.json and every package in it (about 1 GB in
# all, one at a time per job, each deleted once looked at), checks its
# SHA-256, and notes what in it decides whether it can work on ZeroOS:
#
#   - Depends that apt on ZeroOS can't find (Debian trixie + Raspberry Pi's
#     archive + M5's other apps). "applaunch" is M5's own launcher, which
#     ZeroOS replaces, so an app that depends on it can't be installed.
#   - Launcher entries in /usr/share/APPLaunch/applications: what ZeroShell
#     shows on the home screen. None means there's nothing to open.
#   - Mentions of M5's launcher (APPLaunch.service, M5CardputerZero-APPLaunch)
#     in the package's files or install scripts.
#   - Mentions of X11, Wayland, GTK or Qt (needs a desktop, not the LCD), of
#     /dev/input/eventN by number, and of /dev/ttyN.
#   - systemd units, sudoers files, and install scripts that add users to
#     groups.
#
# Nothing in a package is run. render turns a scan into the checklist and
# folds in docs/m5-apps-notes.tsv, which is kept by hand: a reviewer's
# verdict where the scan alone can't tell, and results from real devices.
import argparse
import concurrent.futures as cf
import gzip
import json
import lzma
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request

REGISTRY = "https://cardputer.cc/generated/registry.json"
INDEXES = [
    ("https://deb.debian.org/debian/dists/trixie/main/binary-arm64/Packages.xz", "xz"),
    ("https://deb.debian.org/debian/dists/trixie/non-free-firmware/binary-arm64/Packages.xz", "xz"),
    ("https://archive.raspberrypi.com/debian/dists/trixie/main/binary-arm64/Packages.gz", "gz"),
]
HERE = os.path.dirname(os.path.abspath(__file__))
NOTES = os.path.join(HERE, "..", "docs", "m5-apps-notes.tsv")

PATTERNS = {
    "launcher": re.compile(rb"APPLaunch\.service|M5CardputerZero-APPLaunch"),
    "desktop": re.compile(rb"libX11\.so|libwayland-client|libgtk-3|libQt5Gui|libQt6Gui|XOpenDisplay"),
    "event_fixed": re.compile(rb"/dev/input/event[0-9]"),
    "tty": re.compile(rb"/dev/tty[0-9]"),
}


def fetch(url):
    with urllib.request.urlopen(url, timeout=120) as r:
        return r.read()


def available_packages(m5_packages):
    names = set(m5_packages)
    for url, kind in INDEXES:
        data = fetch(url)
        text = (lzma.decompress(data) if kind == "xz" else gzip.decompress(data)).decode("utf-8", "replace")
        for line in text.splitlines():
            if line.startswith("Package: "):
                names.add(line[9:].strip())
            elif line.startswith("Provides: "):
                names.update(p.split("(")[0].strip() for p in line[10:].split(","))
    return names


def missing(depends, names):
    out = []
    for part in depends.split(","):
        part = part.strip()
        if part and not any(a.split("(")[0].split(":")[0].strip() in names for a in part.split("|")):
            out.append(part)
    return out


def scan_one(app, names, work):
    dl = app["download"]
    res = {"package": dl["package"]}
    d = tempfile.mkdtemp(dir=work)
    try:
        deb = os.path.join(d, "p.deb")
        if subprocess.run(["curl", "-fsSL", "--proto-redir", "=https", "--retry", "3",
                           "--max-time", "900", "-o", deb, dl["url"]]).returncode:
            res["error"] = "download failed"
            return res
        sha = subprocess.run(["sha256sum", deb], capture_output=True, text=True).stdout.split()[0]
        res["sha_ok"] = sha == dl["sha256"]
        x = os.path.join(d, "x")
        os.mkdir(x)
        subprocess.run(["dpkg-deb", "-x", deb, x], check=True, capture_output=True)
        subprocess.run(["dpkg-deb", "-e", deb, os.path.join(x, "DEBIAN")], check=True, capture_output=True)
        fields = {}
        for line in open(os.path.join(x, "DEBIAN/control"), errors="replace"):
            m = re.match(r"^([A-Za-z-]+):\s*(.*)$", line)
            if m:
                fields[m.group(1)] = m.group(2)
        res["depends"] = fields.get("Depends", "")
        res["missing"] = missing(fields.get("Depends", "") + "," + fields.get("Pre-Depends", ""), names)
        entries = []
        appdir = os.path.join(x, "usr/share/APPLaunch/applications")
        if os.path.isdir(appdir):
            for f in sorted(os.listdir(appdir)):
                if f.endswith(".desktop"):
                    t = open(os.path.join(appdir, f), errors="replace").read()
                    get = lambda k: (re.search(r"^%s=(.*)$" % k, t, re.M) or [None, ""])[1].strip()
                    entries.append({"file": f, "exec": get("Exec"),
                                    "terminal": get("Terminal").lower() == "true"})
        res["desktop"] = entries
        hits = {k: [] for k in PATTERNS}
        units = []
        for root, _, files in os.walk(x):
            for f in files:
                p = os.path.join(root, f)
                rel = os.path.relpath(p, x)
                if f.endswith((".service", ".timer")):
                    units.append(rel)
                if os.path.islink(p) or rel.startswith("usr/share/doc/") or rel.endswith((".md", ".txt")):
                    continue
                if os.path.getsize(p) > 64 << 20:
                    continue
                data = open(p, "rb").read()
                for k, pat in PATTERNS.items():
                    if len(hits[k]) < 5 and pat.search(data):
                        hits[k].append(rel)
        res["hits"] = {k: v for k, v in hits.items() if v}
        res["units"] = units
        etc = os.path.join(x, "etc")
        res["sudoers"] = os.path.isdir(os.path.join(etc, "sudoers.d"))
        scripts = ""
        for s in ("preinst", "postinst", "prerm", "postrm"):
            sp = os.path.join(x, "DEBIAN", s)
            if os.path.exists(sp):
                scripts += open(sp, errors="replace").read()
        res["script_groups"] = bool(re.search(r"usermod|adduser|gpasswd", scripts))
    except Exception as e:   # one bad package shouldn't stop the rest
        res["error"] = repr(e)
    finally:
        shutil.rmtree(d, ignore_errors=True)
    return res


def scan(args):
    apps = json.loads(fetch(REGISTRY))["apps"]
    names = available_packages(a["download"]["package"] for a in apps)
    work = tempfile.mkdtemp(prefix="m5-apps.")
    try:
        with cf.ThreadPoolExecutor(args.jobs) as ex:
            for r in ex.map(lambda a: scan_one(a, names, work), apps):
                sys.stdout.write(json.dumps(r, sort_keys=True) + "\n")
                sys.stdout.flush()
                sys.stderr.write("%s %s\n" % (r["package"], r.get("error", "")))
    finally:
        shutil.rmtree(work, ignore_errors=True)


# ---- render -----------------------------------------------------------------

VERDICTS = [
    ("no-install", "Won't install",
     "Depends on something ZeroOS doesn't have (usually `applaunch`, M5's own launcher). apt's dry run "
     "stops it, so nothing on the device changes."),
    ("launcher", "Needs M5's launcher",
     "Installs, but the app itself (not just its install script) talks to M5's launcher, which ZeroOS "
     "replaces. Expect it not to work, or to work only in part."),
    ("check", "Installs; check closely",
     "Nothing rules it out, but something in it may not fit ZeroOS: see the notes."),
    ("likely", "Should work",
     "Nothing in the package stands in the way: it draws on the LCD's framebuffer (or runs in the "
     "Terminal) and reads the keyboard, which ZeroOS hands over."),
    ("none", "Nothing to open",
     "No launcher entry, so nothing appears on the home screen."),
]

PERM_NOTES = [
    ("background_service", "runs in the background"),
    ("additional_hardware", "needs extra hardware"),
    ("camera", "camera"),
    ("microphone", "microphone"),
    ("imu", "motion sensor"),
    ("external_display", "monitor"),
]

TICK = {"": "☐", "yes": "✅", "no": "❌", "part": "🟡", "n/a": "–"}


def read_notes():
    notes = {}
    if not os.path.exists(NOTES):
        return notes
    for line in open(NOTES, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        cols = (line.rstrip("\n").split("\t") + [""] * 9)[:9]
        pkg, verdict, installs, opens, keys, leaves, zeroos, date, note = (c.strip() for c in cols)
        notes[pkg] = {"verdict": verdict, "installs": installs, "opens": opens, "keys": keys,
                      "leaves": leaves, "zeroos": zeroos, "date": date, "note": note}
    return notes


def static_verdict(r):
    if r.get("error") or not r.get("sha_ok", False):
        return "check"
    if r.get("missing"):
        return "no-install"
    if not r.get("desktop"):
        return "none"
    if r.get("hits", {}).get("desktop") or r.get("hits", {}).get("launcher"):
        return "check"
    return "likely"


def scan_notes(r, reviewed=False):
    out = []
    if r.get("error"):
        out.append("scan failed: " + r["error"])
    elif not r.get("sha_ok", False):
        out.append("download didn't match M5's SHA-256")
    if r.get("missing"):
        out.append("missing: " + ", ".join("`%s`" % m for m in r["missing"]))
    hits = r.get("hits", {})
    if hits.get("launcher") and not reviewed:
        out.append("mentions M5's launcher")
    if hits.get("desktop") and not reviewed:
        out.append("mentions X11/Wayland/GTK/Qt")
    if hits.get("event_fixed"):
        out.append("opens /dev/input/eventN by number (may pick the wrong device)")
    if hits.get("tty"):
        out.append("uses /dev/ttyN")
    if any(d.get("terminal") for d in r.get("desktop", [])):
        out.append("opens in Terminal")
    if len(r.get("desktop", [])) > 1:
        out.append("%d entries on home" % len(r["desktop"]))
    if r.get("sudoers"):
        out.append("adds sudo rules")
    if r.get("script_groups"):
        out.append("adds you to groups")
    return out


def cell(s):
    return str(s).replace("|", "\\|").replace("\n", " ")


def render(args):
    apps = {a["download"]["package"]: a for a in json.loads(fetch(REGISTRY))["apps"]}
    scans = {}
    for line in open(args.scan, encoding="utf-8"):
        r = json.loads(line)
        scans[r["package"]] = r
    notes = read_notes()
    rows = {v[0]: [] for v in VERDICTS}
    tested = 0
    for pkg, app in apps.items():
        r = scans.get(pkg, {"package": pkg, "error": "not scanned"})
        n = notes.get(pkg, {})
        verdict = n.get("verdict") or static_verdict(r)
        if verdict not in rows:
            verdict = static_verdict(r)
        perms = app.get("permissions") or {}
        heads = [label for key, label in PERM_NOTES if perms.get(key)]
        info = scan_notes(r, reviewed=bool(n.get("verdict") or n.get("note")))
        if n.get("note"):
            info.append(n["note"])
        if any(n.get(k) for k in ("installs", "opens", "keys", "leaves")):
            tested += 1
        rows[verdict].append((app, n, heads, info))

    o = []
    w = o.append
    w("# M5Stack's app store on ZeroOS: compatibility checklist\n")
    w("<!-- Written by tools/m5-apps.py from M5's registry.json and docs/m5-apps-notes.tsv. "
      "Edit those, not this file. -->\n")
    w("ZeroOS's Apps app lists [M5Stack's app store](https://github.com/CardputerZero/packages) "
      "alongside its own apps (marked **M5**). Those apps were made for M5's own launcher, which "
      "ZeroOS replaces, so not all of them work here. This page lists every one of them with what a "
      "look inside its package found, and what has been checked on a real Cardputer Zero running "
      "ZeroOS. How ZeroOS runs them is in [the apps guide](APPS.md#m5stacks-app-store).\n")
    w("Other places: [ZeroOS](../README.md) · "
      "[ZeroOS apps](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U) · "
      "[Browser emulator](https://hartj77-blip.github.io/emu-cardputer-os.4me-u/) "
      "([repository](https://github.com/hartj77-blip/emu-cardputer-os.4me-u))\n")
    w("Store list of %s, %d apps. Checked on a device so far: **%d**.\n"
      % (args.date or "the scan's date", len(apps), tested))
    w("## Summary\n")
    w("| Verdict | Apps | What it means |\n| --- | --- | --- |")
    for key, title, meaning in VERDICTS:
        anchor = re.sub(r"[^a-z0-9 -]", "", title.lower()).replace(" ", "-")
        w("| [%s](#%s) | %d | %s |" % (title, anchor, len(rows[key]), meaning))
    w("")
    w("The verdicts come from looking inside each package, not from running it, so treat them as a "
      "guess until the device columns are filled in. Every app also needs what M5's store says it "
      "needs (the **Heads-up** column), and apps that use the network need Wi-Fi.\n")
    w("## The device columns\n")
    w("| Column | Ticked when |\n| --- | --- |")
    w("| Installs | Apps > Enter installs it without an error |")
    w("| Opens | It appears on home and draws on the screen when opened |")
    w("| Keys | Its keys do what its own help says |")
    w("| Leaves | Its own exit, or holding Esc for 3 seconds, gets back home with the screen working |")
    w("")
    w("☐ not tried yet · ✅ works · 🟡 partly · ❌ doesn't · – doesn't apply\n")
    w("Tried one? Add a line to [m5-apps-notes.tsv](m5-apps-notes.tsv) and run "
      "`tools/m5-apps.py render`, or [open an issue](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/issues) "
      "with the app's name, the ZeroOS version and what happened.\n")
    for key, title, meaning in VERDICTS:
        items = sorted(rows[key], key=lambda t: ((t[0].get("categories") or ["~"])[0], t[0]["title"].lower()))
        w("## %s\n" % title)
        w(meaning + "\n")
        if not items:
            w("None.\n")
            continue
        w("| App | Category | Heads-up | Notes | Installs | Opens | Keys | Leaves |")
        w("| --- | --- | --- | --- | :-: | :-: | :-: | :-: |")
        for app, n, heads, info in items:
            pkg = app["download"]["package"]
            src = app.get("source_repo") or ""
            name = "[%s](%s)" % (cell(app["title"]), src) if src.startswith("https://") else cell(app["title"])
            name += "<br>`m5-%s` %s" % (pkg, cell(app.get("version", "")))
            if n.get("zeroos"):
                info = info + ["tried on ZeroOS %s%s" % (n["zeroos"], (" (%s)" % n["date"]) if n.get("date") else "")]
            w("| %s | %s | %s | %s | %s | %s | %s | %s |" % (
                name, cell(", ".join(app.get("categories") or [])), cell(", ".join(heads)),
                cell("; ".join(info)), TICK.get(n.get("installs", ""), "☐"), TICK.get(n.get("opens", ""), "☐"),
                TICK.get(n.get("keys", ""), "☐"), TICK.get(n.get("leaves", ""), "☐")))
        w("")
    sys.stdout.write("\n".join(o))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("scan")
    s.add_argument("--jobs", type=int, default=4)
    r = sub.add_parser("render")
    r.add_argument("scan")
    r.add_argument("--date", help="the date of the scan, for the page")
    args = p.parse_args()
    (scan if args.cmd == "scan" else render)(args)


if __name__ == "__main__":
    main()
