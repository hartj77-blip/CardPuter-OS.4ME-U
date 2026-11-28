#!/usr/bin/env python3
"""Fail if hal.h changes in a way that breaks already-built app modules
without a ZS_APP_ABI bump.

Apps call Hal by vtable slot and get its structs by value. Two rules:
- Hal's virtual calls in shell/tests/app_abi/hal-abiN.txt must stay first,
  in that order (new ones may be added after them).
- Each struct recorded there keeps exactly the same members.
0.9.5 broke both on ABI 4: Jellyfin crashed ZeroShell and Sync stopped
Tailscale.

When ZS_APP_ABI goes up, record the new layout:
    python3 shell/tests/app_abi_test.py --write
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src")


def abi():
    s = open(os.path.join(SRC, "ui", "app.h")).read()
    return int(re.search(r"^#define ZS_APP_ABI (\d+)", s, re.M).group(1))


def strip(code):
    code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)
    return re.sub(r"//[^\n]*", "", code)


def layout(path):
    s = strip(open(path).read())
    hal = re.search(r"^class Hal\b.*?^};", s, re.S | re.M).group(0)
    calls = []
    for m in re.finditer(r"virtual\s+([^;{]*?)\s*(?:=\s*0|override)?\s*(?:;|\{)", hal):
        calls.append("call " + " ".join(m.group(1).split()))
    structs = []
    for m in re.finditer(r"^struct (\w+) \{(.*?)^\};", s, re.S | re.M):
        members = [" ".join(l.split()) for l in m.group(2).split(";")]
        members = [x for x in members if x]
        structs.append("struct %s: %s" % (m.group(1), " ; ".join(members)))
    return calls, structs


def main():
    n = abi()
    snap = os.path.join(HERE, "app_abi", "hal-abi%d.txt" % n)
    calls, structs = layout(os.path.join(SRC, "hal", "hal.h"))
    if "--write" in sys.argv:
        os.makedirs(os.path.dirname(snap), exist_ok=True)
        open(snap, "w").write("\n".join(calls + structs) + "\n")
        print("wrote", snap)
        return 0
    if not os.path.exists(snap):
        print("FAIL: no %s. ZS_APP_ABI is %d: record it with --write" % (snap, n))
        return 1
    want = [l for l in open(snap).read().splitlines() if l]
    want_calls = [l for l in want if l.startswith("call ")]
    want_structs = [l for l in want if l.startswith("struct ")]
    bad = []
    for i, w in enumerate(want_calls):
        got = calls[i] if i < len(calls) else "(missing)"
        if got != w:
            bad.append("Hal call #%d: was `%s`, now `%s`" % (i, w[5:], got[5:]))
            break
    have = {l.split(":", 1)[0]: l for l in structs}
    for w in want_structs:
        name = w.split(":", 1)[0]
        if have.get(name) != w:
            bad.append("%s changed (apps get it by value)" % name)
    if bad:
        print("FAIL: hal.h breaks ABI %d apps:" % n)
        for b in bad:
            print("  " + b)
        print("Add new Hal calls at the end and leave existing structs alone,")
        print("or raise ZS_APP_ABI in shell/src/ui/app.h and run --write.")
        return 1
    print("ok: hal.h keeps app ABI %d (%d calls, %d structs)" % (n, len(want_calls), len(want_structs)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
