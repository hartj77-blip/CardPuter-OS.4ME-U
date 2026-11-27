#!/bin/bash
# The polkit rule (files/90-zeroos.rules) run in node with a stand-in polkit:
# the UI user may run the Apps jobs that use the sources already listed, but
# not add a source (that decides what runs as root), and nobody else gets
# anything.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
command -v node >/dev/null || { echo "polkit-test: needs node" >&2; exit 1; }
node - "$ROOT/image/stage-zeroos/files/90-zeroos.rules" <<'JS'
const fs = require("fs");
const src = fs.readFileSync(process.argv[2], "utf8");
const rules = [];
const polkit = {
    Result: { YES: "yes", NO: "no", AUTH_ADMIN: "auth_admin" },
    addRule: (f) => rules.push(f),
    spawn: () => "pi\n",
};
new Function("polkit", src)(polkit);
function decide(user, id, unit) {
    const action = { id, lookup: (k) => (k === "unit" ? unit : undefined) };
    for (const r of rules) { const v = r(action, { user }); if (v !== undefined) return v; }
    return "default";
}
// unit names as ZeroShell makes them (store.cpp unit_escape).
const esc = (s) => [...s].map((c, i) => c === "/" ? "-" :
    (/[A-Za-z0-9:_]/.test(c) || (c === "." && i > 0)) ? c : "\\x" + c.charCodeAt(0).toString(16).padStart(2, "0")).join("");
const unit = (job) => "zeroos-apps@" + esc(job) + ".service";
const M = "org.freedesktop.systemd1.manage-units";
let fails = 0;
function expect(what, got, want) {
    if (got === want) console.log("ok   " + what);
    else { console.log("FAIL " + what + ": " + got + ", wanted " + want); fails++; }
}
for (const job of ["refresh", "upgrade", "install:media", "remove:media",
                   "remove-source:someone/apps", "source-up:m5stack", "source-down:someone/apps"])
    expect("pi may run " + job, decide("pi", M, unit(job)), "yes");
for (const job of ["add-source:someone/apps", "add-source:https://example.com/x", "add-source:m5stack",
                   "refreshx", "install:", "add\\x2dsource:x", "evil", ""])
    expect("pi may not run '" + job + "' unasked", decide("pi", M, unit(job)), "default");
expect("raw add-source unit", decide("pi", M, "zeroos-apps@add\\x2dsource:a-b.service"), "default");
expect("another user can't install", decide("mallory", M, unit("install:media")), "default");
expect("pi may start the Terminal", decide("pi", M, "zeroos-terminal.service"), "yes");
expect("pi may let phones reach Mesh", decide("pi", M, "zeroos-mesh-lan.service"), "yes");
expect("pi may not start sshd", decide("pi", M, "ssh.service"), "default");
expect("pi may reboot", decide("pi", "org.freedesktop.login1.reboot", ""), "yes");
// Settings > Date & time.
for (const a of ["set-time", "set-timezone", "set-ntp"])
    expect("pi may " + a, decide("pi", "org.freedesktop.timedate1." + a, ""), "yes");
expect("pi may not put the hardware clock on local time", decide("pi", "org.freedesktop.timedate1.set-local-rtc", ""), "default");
expect("another user can't set the time", decide("mallory", "org.freedesktop.timedate1.set-time", ""), "default");
process.exit(fails ? 1 : 0);
JS
