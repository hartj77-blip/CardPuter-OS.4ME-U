#!/usr/bin/env python3
"""Build the IR Remote app's code database from Flipper-IRDB.

Flipper-IRDB (https://github.com/Lucaslhm/Flipper-IRDB, CC0-1.0) is a large
community collection of remote controls in the Flipper Zero ".ir" format,
filed as <Category>/<Brand>/<Model>.ir. The app reads those files as they
are; this script picks the ones it can send, and writes beside them:

  index.tsv              category, brand, model, path, buttons: one line per
                         remote, so the app can list 2,000 remotes without
                         opening them
  universal/<Name>.ir    "universal remote" lists: every distinct Power (or
                         Vol +, Mute, ...) code for a kind of device, the
                         most common first, each with "# src:" naming the
                         remote it came from
  SOURCE.txt, LICENSE    where the data came from (commit) and its licence

Usage:
  build_irdb.py --out DIR [--src FLIPPER_IRDB_CHECKOUT]
  build_irdb.py --out DIR --fetch [--cache DIR]    clone the pinned commit

The _Converted_ folder (bulk conversions from other databases, some with
their own licence terms) is left out.
"""
import argparse
import collections
import os
import re
import shutil
import subprocess
import sys

REPO = "https://github.com/Lucaslhm/Flipper-IRDB.git"
COMMIT = "d126fb1b6f1e114c52b4a8c19839ea65e3a9c24d"   # main, 2026-04-11

PROTOCOLS = {p.lower(): p for p in (
    "NEC", "NECext", "NEC42", "NEC42ext", "Samsung32", "RC5", "RC5X", "RC6",
    "SIRC", "SIRC15", "SIRC20", "Kaseikyo", "RCA", "Pioneer")}

SKIP_DIRS = {"_Converted_"}

# Universal lists: (file name, source categories, [(button, [name aliases])]).
# Aliases are compared after norm() below.
POWER = ["power", "power_toggle", "pwr", "on_off", "onoff", "standby", "power_on_off", "power_onoff"]
VOL_UP = ["vol_up", "volume_up", "volup", "vol_plus", "volume_plus", "v_up"]
VOL_DN = ["vol_dn", "vol_down", "volume_down", "volume_dn", "voldown", "vol_minus", "volume_minus", "v_dn"]
MUTE = ["mute", "mute_toggle", "mute_unmute"]
CH_UP = ["ch_next", "ch_up", "channel_up", "ch_plus", "prog_up", "p_up", "program_up", "channel_next"]
CH_DN = ["ch_prev", "ch_dn", "ch_down", "channel_down", "ch_minus", "prog_dn", "prog_down", "p_dn",
         "program_down", "channel_prev"]
UNIVERSAL = [
    ("TVs", ["TVs"], [("Power", POWER), ("Vol_up", VOL_UP), ("Vol_dn", VOL_DN), ("Mute", MUTE),
                      ("Ch_next", CH_UP), ("Ch_prev", CH_DN)]),
    ("Projectors", ["Projectors"], [("Power", POWER), ("Vol_up", VOL_UP), ("Vol_dn", VOL_DN), ("Mute", MUTE)]),
    ("Audio", ["SoundBars", "Audio_and_Video_Receivers", "Speakers"],
     [("Power", POWER), ("Vol_up", VOL_UP), ("Vol_dn", VOL_DN), ("Mute", MUTE),
      ("Play", ["play", "play_pause", "playpause", "play_and_pause"])]),
    ("ACs", ["ACs"], [("Off", ["off", "power_off", "poweroff"]), ("Power", POWER)]),
    ("Fans", ["Fans"], [("Power", POWER), ("Oscillate", ["osc", "oscillate", "oscillation", "rotate", "rotation",
                                                         "swing", "swing_toggle"]),
                        ("Speed", ["speed", "fan_speed", "speed_up", "fan_up", "wind_speed"])]),
    ("Lights", ["LED_Lighting"], [("On", ["on", "power_on", "light_on"]), ("Off", ["off", "power_off", "light_off"]),
                                  ("Power", POWER)]),
]
MAX_PER_BUTTON = 400


def norm(name):
    """Button name -> a key: "Vol+" -> vol_up, "VOL -" -> vol_dn, "Ch Up" -> ch_up."""
    s = name.strip().lower()
    s = re.sub(r"\s*\+$", "_up", s)
    s = re.sub(r"(?<=[a-z])\s*-$", "_dn", s)
    s = re.sub(r"[^a-z0-9]+", "_", s).strip("_")
    return s


def parse(text):
    """Signals from a .ir file's text, as dicts; None if it isn't one."""
    sigs, cur, ok = [], None, True

    def flush():
        if cur is None or not ok or not cur.get("name"):
            return
        if cur.get("type") == "raw":
            data = cur.get("data", [])
            if len(data) % 2 == 0 and data:
                data = data[:-1]
            if not data or not 10000 <= cur.get("frequency", 38000) <= 100000:
                return
            cur["data"] = data
        elif cur.get("type") == "parsed":
            p = PROTOCOLS.get(cur.get("protocol", "").lower())
            if not p or "address" not in cur or "command" not in cur:
                return
            cur["protocol"] = p
        else:
            return
        sigs.append(cur)

    is_ir = False
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or ":" not in line:
            continue
        key, val = line.split(":", 1)
        key, val = key.strip().lower(), val.strip()
        if key == "filetype":
            is_ir = val.lower().startswith("ir ")
        elif key == "name":
            flush()
            cur, ok = {"name": val}, True
        elif cur is None:
            continue
        elif key == "type":
            cur["type"] = val.lower()
        elif key == "protocol":
            cur["protocol"] = val
        elif key in ("address", "command"):
            try:
                b = [int(x, 16) for x in val.split()]
                if not 1 <= len(b) <= 4 or any(x > 255 for x in b):
                    raise ValueError
                cur[key] = sum(x << (8 * i) for i, x in enumerate(b))
            except ValueError:
                ok = False
        elif key == "frequency":
            try:
                cur["frequency"] = int(float(val))
            except ValueError:
                ok = False
        elif key == "duty_cycle":
            cur["duty_cycle"] = val
        elif key == "data":
            try:
                vals = [int(x) for x in val.split()]
                if any(v <= 0 or v > 1000000 for v in vals):
                    raise ValueError
                cur.setdefault("data", []).extend(vals)
            except ValueError:
                ok = False
    flush()
    return sigs if (is_ir or sigs) else None


def code_key(s):
    """Identity of a code, for removing duplicates across remotes."""
    if s["type"] == "parsed":
        return ("p", s["protocol"], s["address"], s["command"])
    # Raw captures of the same button differ slightly: round to 10%.
    return ("r", round(s.get("frequency", 38000), -3),
            tuple(int(round(v / max(50, v // 10))) for v in s["data"]))


def signal_text(s, name, src):
    out = ["#", "# src: " + src, "name: " + name]
    if s["type"] == "parsed":
        def b(v):
            return " ".join("%02X" % ((v >> (8 * i)) & 0xFF) for i in range(4))
        out += ["type: parsed", "protocol: " + s["protocol"], "address: " + b(s["address"]),
                "command: " + b(s["command"])]
    else:
        out += ["type: raw", "frequency: %d" % s.get("frequency", 38000),
                "duty_cycle: " + s.get("duty_cycle", "0.330000"), "data: " + " ".join(map(str, s["data"]))]
    return "\n".join(out)


def pretty_model(brand, stem, series):
    m = stem
    if m.lower().startswith(brand.lower()):
        m = m[len(brand):].lstrip("_- ") or stem
    m = m.replace("_", " ").strip()
    return " / ".join(series + [m]) if series else m


def fetch(cache):
    if not os.path.isdir(os.path.join(cache, ".git")):
        os.makedirs(cache, exist_ok=True)
        subprocess.run(["git", "init", "-q", cache], check=True)
        subprocess.run(["git", "-C", cache, "remote", "add", "origin", REPO], check=True)
    head = subprocess.run(["git", "-C", cache, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    if head != COMMIT:
        subprocess.run(["git", "-C", cache, "fetch", "-q", "--depth", "1", "origin", COMMIT], check=True)
        subprocess.run(["git", "-C", cache, "checkout", "-q", "--force", COMMIT], check=True)
    return cache


def build(src, out, commit=None):
    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(out)
    index = []
    by_cat = collections.defaultdict(list)   # category -> [(relpath, signals)]
    brand_of = {}
    skipped = 0
    for cat in sorted(os.listdir(src)):
        cdir = os.path.join(src, cat)
        if cat.startswith(".") or cat in SKIP_DIRS or not os.path.isdir(cdir):
            continue
        for root, dirs, files in os.walk(cdir):
            dirs.sort()
            for f in sorted(files):
                if not f.lower().endswith(".ir"):
                    continue
                path = os.path.join(root, f)
                rel = os.path.relpath(path, src)
                if "\t" in rel or "\n" in rel:
                    continue
                with open(path, encoding="utf-8", errors="replace") as fh:
                    sigs = parse(fh.read())
                if not sigs:
                    skipped += 1
                    continue
                parts = rel.split(os.sep)
                stem = os.path.splitext(parts[-1])[0]
                if len(parts) >= 3:
                    brand, series = parts[1], parts[2:-1]
                else:
                    brand, series = stem.split("_")[0], []
                brand = brand.replace("_", " ").strip()
                dest = os.path.join(out, rel)
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                shutil.copyfile(path, dest)
                rel = rel.replace(os.sep, "/")
                index.append((cat, brand, pretty_model(brand, stem, series), rel, len(sigs)))
                brand_of[rel] = brand
                by_cat[cat].append((rel, sigs))

    index.sort(key=lambda r: (r[0].lower(), r[1].lower(), r[2].lower()))
    with open(os.path.join(out, "index.tsv"), "w", encoding="utf-8") as fh:
        fh.write("# category\tbrand\tmodel\tpath\tbuttons\n")
        for row in index:
            fh.write("\t".join(map(str, row)) + "\n")

    os.makedirs(os.path.join(out, "universal"))
    summary = []
    for name, cats, buttons in UNIVERSAL:
        blocks = []
        total = 0
        for button, aliases in buttons:
            aliases = set(aliases)
            # code key -> [signal, count, {brand: [files, first file]}]
            seen = collections.OrderedDict()
            for cat in cats:
                for rel, sigs in by_cat.get(cat, []):
                    brand = brand_of[rel]
                    in_file = set()
                    for s in sigs:
                        if norm(s["name"]) not in aliases:
                            continue
                        k = code_key(s)
                        if k in in_file:
                            continue
                        in_file.add(k)
                        e = seen.setdefault(k, [s, 0, {}])
                        e[1] += 1
                        e[2].setdefault(brand.lower(), [0, rel])[0] += 1
            ranked = sorted(seen.values(), key=lambda e: -e[1])[:MAX_PER_BUTTON]
            # Credit each code to the brand with the most remotes using it,
            # so stopping on it in the app names the likely maker.
            blocks += [signal_text(s, button, max(b.values(), key=lambda v: v[0])[1]) for s, _, b in ranked]
            total += len(ranked)
        if total:
            with open(os.path.join(out, "universal", name + ".ir"), "w", encoding="utf-8") as fh:
                fh.write("Filetype: IR library file\nVersion: 1\n# ZeroOS universal remote: %s\n" % name)
                fh.write("\n".join(blocks) + "\n")
            summary.append("%s %d" % (name, total))

    lic = os.path.join(src, "LICENSE")
    if os.path.exists(lic):
        shutil.copyfile(lic, os.path.join(out, "LICENSE"))
    with open(os.path.join(out, "SOURCE.txt"), "w", encoding="utf-8") as fh:
        fh.write("IR codes from Flipper-IRDB, %s\ncommit: %s\n" % (REPO, commit or "local copy"))
        fh.write("Licence: CC0-1.0 (see LICENSE and the project's README for its scope).\n")
        fh.write("Built by tools/irdb/build_irdb.py: %d remotes, universal lists: %s\n"
                 % (len(index), ", ".join(summary)))
    print("irdb: %d remotes (%d skipped), universal: %s -> %s" % (len(index), skipped, ", ".join(summary), out))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--src", help="a Flipper-IRDB checkout (or any folder laid out the same way)")
    ap.add_argument("--fetch", action="store_true", help="clone the pinned Flipper-IRDB commit first")
    ap.add_argument("--cache", default=os.path.join("build", "irdb-src"), help="where --fetch clones to")
    a = ap.parse_args()
    if a.fetch:
        src, commit = fetch(a.cache), COMMIT
    elif a.src:
        src, commit = a.src, None
    else:
        ap.error("give --src or --fetch")
    if not os.path.isdir(src):
        sys.exit("error: %s is not a folder" % src)
    build(src, a.out, commit)


if __name__ == "__main__":
    main()
