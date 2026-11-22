#!/usr/bin/env python3
"""Publish only some apps (docs/APPS.md, "Releasing only some apps").

  merge-index.py NEW_DIR OLD_DIR OUT_DIR APP...

NEW_DIR holds what image/apps/build.sh made from this commit: every app's
zeroos-app-<id>_<version>_<arch>.deb and zeroos-apps.index. OLD_DIR holds the
previous release's index and packages. OUT_DIR gets the release's apps:

  - a listed APP comes from NEW_DIR (its new build, a new version)
  - any other app comes from OLD_DIR, as it was (same version: devices see
    no update for it)
  - an app in neither the list nor OLD_DIR is left out: merged into main but
    not published yet

"all" as the only APP takes everything from NEW_DIR.

It refuses to carry over an app built for another app ABI than this
commit's: devices on the new ZeroOS couldn't load it, and the store would
quietly drop it. Release every app ("all") when ZS_APP_ABI changes.
"""
import os
import shutil
import sys

INDEX = "zeroos-apps.index"


def read_index(path):
    """(header lines, {id: block lines}) in file order."""
    header, blocks, order, cur = [], {}, [], None
    with open(path) as f:
        for line in f.read().splitlines():
            if line.startswith("app="):
                cur = line[4:]
                blocks[cur] = [line]
                order.append(cur)
            elif not line.strip():
                cur = None
            elif cur is not None:
                blocks[cur].append(line)
            elif not blocks:
                header.append(line)
    return header, {k: blocks[k] for k in order}


def field(block, key):
    for line in block:
        if line.startswith(key + "="):
            return line[len(key) + 1:]
    return ""


def main(argv):
    if len(argv) < 5:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    new_dir, old_dir, out_dir, wanted = argv[1], argv[2], argv[3], argv[4:]
    header, new = read_index(os.path.join(new_dir, INDEX))
    everything = wanted == ["all"]
    unknown = [a for a in wanted if a not in new and not everything]
    if unknown:
        print(f"error: no app called {', '.join(unknown)} in this commit (apps: {' '.join(new)})", file=sys.stderr)
        return 1
    old = {}
    if not everything:
        old_index = os.path.join(old_dir, INDEX)
        if not os.path.exists(old_index):
            print(f"error: no {INDEX} from the previous release in {old_dir}", file=sys.stderr)
            return 1
        _, old = read_index(old_index)

    os.makedirs(out_dir, exist_ok=True)
    out_blocks, report = [], []
    for app, block in new.items():   # this commit's order: the store's order
        src, chosen = None, None
        if everything or app in wanted:
            src, chosen = new_dir, block
            report.append(f"  {app:10} {field(block, 'version')}   new")
        elif app in old:
            src, chosen = old_dir, old[app]
            if field(chosen, "abi") != field(block, "abi"):
                print(f"error: {app} in the previous release is for app ABI {field(chosen, 'abi')}, this commit "
                      f"has {field(block, 'abi')}: release it too, or all apps", file=sys.stderr)
                return 1
            report.append(f"  {app:10} {field(chosen, 'version')}   kept")
        else:
            report.append(f"  {app:10} -          not published yet")
            continue
        deb = field(chosen, "file")
        if not deb or "/" in deb or not os.path.exists(os.path.join(src, deb)):
            print(f"error: {app}: package {deb!r} missing from {src}", file=sys.stderr)
            return 1
        shutil.copy2(os.path.join(src, deb), os.path.join(out_dir, deb))
        out_blocks.append(chosen)
    # Apps that were published before but are gone from this commit stay
    # gone: the store then offers them no more (installed copies keep working).
    for app in old:
        if app not in new:
            report.append(f"  {app:10} -          removed (no longer in this commit)")

    with open(os.path.join(out_dir, INDEX + ".tmp"), "w") as f:
        f.write("\n".join(header).rstrip("\n") + "\n\n")
        for b in out_blocks:
            f.write("\n".join(b) + "\n\n")
    os.replace(os.path.join(out_dir, INDEX + ".tmp"), os.path.join(out_dir, INDEX))
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
