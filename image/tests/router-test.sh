#!/usr/bin/env bash
# Tests for router mode's helper (docs/ROUTER.md), with no device:
# tailscale, nmcli, nft, ip, systemctl and modprobe are stand-ins on PATH
# that log what they were asked, and a folder stands in for /sys and /proc.
# Nothing on this machine's network is changed. If nft can check rules
# here (it needs root), the kill switch's rules are checked for real too.
#   image/tests/router-test.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HELPER=$ROOT/image/stage-zeroos/files/router/zeroos-router
REAL_NFT=$(command -v nft || true)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0
ok() { echo "ok   $1"; }
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
has() {   # has <name> <file> <fixed text>
    if grep -qF -- "$3" "$2" 2>/dev/null; then ok "$1"; else bad "$1 (no '$3' in $(basename "$2"))"; sed 's/^/     /' "$2" 2>/dev/null || true; fi
}
hasnt() {
    if grep -qF -- "$3" "$2" 2>/dev/null; then bad "$1 ('$3' in $(basename "$2"))"; else ok "$1"; fi
}

mkdir -p "$T/bin" "$T/sys/proc/sys/net/ipv4/conf/eth0" "$T/sys/proc/sys/net/ipv4/conf/usb0" \
         "$T/sys/proc/sys/net/ipv4/conf/tailscale0" "$T/sys/sys/class/udc" "$T/sys/etc"
echo 0123456789abcdef0123456789abcdef > "$T/sys/etc/machine-id"
export LOG=$T/log FAKE=$T
for c in nmcli ip modprobe; do
    printf '#!/bin/sh\necho "%s $*" >> "$LOG"\n' "$c" > "$T/bin/$c"
done
# ip rule del: succeeds while a rule is "there", so the helper's loop ends.
cat > "$T/bin/ip" <<'EOF'
#!/bin/sh
echo "ip $*" >> "$LOG"
case "$*" in "rule del"*) [ -f "$FAKE/rule" ] && rm -f "$FAKE/rule" && exit 0; exit 1 ;;
             "rule add"*) touch "$FAKE/rule" ;; esac
EOF
cat > "$T/bin/nft" <<'EOF'
#!/bin/sh
echo "nft $*" >> "$LOG"
[ "$1" = -f ] && cat > "$FAKE/rules.nft"
exit 0
EOF
# systemctl: "is-active" answers from $FAKE/active-<unit>.
cat > "$T/bin/systemctl" <<'EOF'
#!/bin/sh
echo "systemctl $*" >> "$LOG"
case "$1" in
    is-active) for u; do [ -f "$FAKE/active-$u" ] && exit 0; done; exit 3 ;;
    is-system-running) cat "$FAKE/system-state" 2>/dev/null || echo running ;;
esac
EOF
# tailscale: status and prefs from files, so each test sets the scene.
cat > "$T/bin/tailscale" <<'EOF'
#!/bin/sh
echo "tailscale $*" >> "$LOG"
case "$1 $2" in
    "status --json") cat "$FAKE/ts-status.json" ;;
    "debug prefs") cat "$FAKE/ts-prefs.json" ;;
esac
EOF
chmod +x "$T"/bin/*
export PATH=$T/bin:$PATH
export ZEROOS_ROUTER_STATE=$T/run/router.state ZEROOS_ROUTER_MODE_FILE=$T/lib/router.mode \
       ZEROOS_ROUTER_DNSMASQ=$T/dnsmasq/zeroos-router.conf ZEROOS_ROUTER_SYS=$T/sys
mkdir -p "$T/dnsmasq"
R() { : > "$LOG"; python3 "$HELPER" "$@"; }

status() {   # status <BackendState> [exit-node-peer]
    cat > "$T/ts-status.json" <<EOF
{"BackendState": "$1", "Peer": {
  "a": {"HostName": "homelab", "DNSName": "homelab.tail1.ts.net.", "TailscaleIPs": ["100.101.4.2", "fd7a::2"],
        "Online": true, "ExitNodeOption": true, "ExitNode": ${2:-false}},
  "b": {"HostName": "phone", "DNSName": "phone.tail1.ts.net.", "TailscaleIPs": ["100.64.0.5"], "Online": true},
  "c": {"HostName": "nl-ams-wg-001", "DNSName": "nl-ams-wg-001.mullvad.ts.net.", "TailscaleIPs": ["100.120.0.7"],
        "Online": true, "ExitNodeOption": true, "Location": {"Country": "Netherlands", "City": "Amsterdam"}},
  "d": {"HostName": "office-mac", "DNSName": "office-mac.tail1.ts.net.", "TailscaleIPs": ["100.64.3.30"],
        "Online": false, "ExitNodeOption": true}}}
EOF
}
prefs() { echo "{\"ExitNodeID\": \"$1\"}" > "$T/ts-prefs.json"; }

# --- nodes: what the app lists ---------------------------------------------
status Running true
R nodes > "$T/nodes"
[ "$(head -1 "$T/nodes")" = "$(printf 'state\tRunning')" ] && ok "nodes: Tailscale's state first" || bad "nodes: state line"
[ "$(grep -c '^node' "$T/nodes")" = 3 ] && ok "nodes: only machines that offer to be an exit node" || bad "nodes: count"
[ "$(sed -n 2p "$T/nodes")" = "$(printf 'node\t100.101.4.2\thomelab\t1\t1\t')" ] \
    && ok "nodes: the one in use first, IPv4 address" || { bad "nodes: first"; cat "$T/nodes"; }
[ "$(sed -n 3p "$T/nodes" | cut -f3)" = office-mac ] && ok "nodes: own machines before Mullvad" || { bad "nodes: order"; cat "$T/nodes"; }
has "nodes: Mullvad's place" "$T/nodes" "Amsterdam, Netherlands"
mv "$T/bin/tailscale" "$T/tailscale.off"
R nodes > "$T/nodes"
[ "$(cat "$T/nodes")" = "$(printf 'state\tMissing')" ] && ok "nodes: no Tailscale says Missing" || bad "nodes: missing"
mv "$T/tailscale.off" "$T/bin/tailscale"

# --- start without an exit node: an error, nothing left behind --------------
status Running false; prefs ""
R start ethernet
has "no exit node: error state" "$ZEROOS_ROUTER_STATE" "state=error"
has "no exit node: says what to do" "$ZEROOS_ROUTER_STATE" "error=Pick an exit node first"
hasnt "no exit node: no network set up" "$LOG" "connection add"
[ ! -e "$T/dnsmasq/zeroos-router.conf" ] && ok "no exit node: no DNS settings left" || bad "no exit node: dnsmasq conf left"

status NeedsLogin false
R start ethernet
has "signed out: asks to sign in" "$ZEROOS_ROUTER_STATE" "error=Sign in to Tailscale in Remote first"

status Stopped false; prefs n123
R start ethernet
has "Tailscale down: brings it up" "$LOG" "tailscale up"
has "starts tailscaled" "$LOG" "systemctl start tailscaled.service"

# --- start over Ethernet ---------------------------------------------------
status Running true; prefs n123
R start ethernet
has "ethernet: on" "$ZEROOS_ROUTER_STATE" "state=on"
has "ethernet: lan is eth0" "$ZEROOS_ROUTER_STATE" "lan=eth0"
has "ethernet: remembered for the next boot" "$ZEROOS_ROUTER_MODE_FILE" "ethernet"
has "ethernet: shared connection" "$LOG" "nmcli connection add type ethernet con-name zeroos-router ifname eth0 ipv4.method shared ipv4.addresses 10.42.0.1/24 ipv6.method disabled"
has "ethernet: subnet stays local" "$LOG" "ip rule add to 10.42.0.0/24 lookup main priority 5200"
has "kill switch: only into the tunnel" "$T/rules.nft" 'iifname "eth0" oifname != "tailscale0" counter drop'
has "kill switch: nothing in from Wi-Fi" "$T/rules.nft" 'oifname "eth0" iifname != "tailscale0" counter drop'
has "MSS clamp for tailscale0's MTU" "$T/rules.nft" "tcp option maxseg size set rt mtu"
has "masquerade into the tunnel" "$T/rules.nft" 'oifname "tailscale0" ip saddr 10.42.0.0/24 masquerade'
first_nft=$(grep -n '^nft -f' "$LOG" | head -1 | cut -d: -f1)
first_nm=$(grep -n 'connection add' "$LOG" | head -1 | cut -d: -f1)
[ "$first_nft" -lt "$first_nm" ] && ok "kill switch before the network comes up" || bad "kill switch order"
has "DNS through Tailscale" "$T/dnsmasq/zeroos-router.conf" "server=100.100.100.100"
has "DNS: never the Wi-Fi's" "$T/dnsmasq/zeroos-router.conf" "no-resolv"
has "DHCP gives MTU 1280" "$T/dnsmasq/zeroos-router.conf" "dhcp-option=option:mtu,1280"
[ "$(cat "$T/sys/proc/sys/net/ipv4/ip_forward")" = 1 ] && ok "forwarding on" || bad "ip_forward"
[ "$(cat "$T/sys/proc/sys/net/ipv4/conf/tailscale0/rp_filter")" = 2 ] && ok "loose rp_filter on tailscale0" || bad "rp_filter"
if [ -n "$REAL_NFT" ] && [ "$(id -u)" = 0 ] && "$REAL_NFT" -c -f "$T/rules.nft" 2>"$T/nft.err"; then
    ok "nft accepts the rules"
elif [ -n "$REAL_NFT" ] && [ "$(id -u)" = 0 ]; then
    bad "nft rejects the rules"; cat "$T/nft.err"
else
    echo "skip nft syntax check (needs nft and root)"
fi

# --- one mode at a time ----------------------------------------------------
touch "$T/active-zeroos-router@ethernet.service"
R start usb
has "usb while ethernet is on: refused" "$ZEROOS_ROUTER_STATE" "error=Router is already on over Ethernet"
has "usb refused: ethernet stays remembered" "$ZEROOS_ROUTER_MODE_FILE" "ethernet"
rm "$T/active-zeroos-router@ethernet.service"

# --- stop: by hand it stays off; at shutdown it comes back ------------------
R stop ethernet
[ ! -e "$ZEROOS_ROUTER_STATE" ] && ok "stop: state gone" || bad "stop: state left"
[ ! -e "$ZEROOS_ROUTER_MODE_FILE" ] && ok "stop by hand: stays off after a restart" || bad "stop: mode file left"
[ ! -e "$T/dnsmasq/zeroos-router.conf" ] && ok "stop: DNS settings gone" || bad "stop: dnsmasq conf left"
has "stop: connection deleted" "$LOG" "nmcli connection delete zeroos-router"
has "stop: kill switch removed" "$LOG" "nft delete table inet zeroos-router"
[ ! -e "$T/rule" ] && ok "stop: routing rule removed" || bad "stop: ip rule left"
R start ethernet
echo stopping > "$T/system-state"
R stop ethernet
has "shutdown: remembered for the next boot" "$ZEROOS_ROUTER_MODE_FILE" "ethernet"
rm "$T/system-state"
R boot
has "boot: turns it on again" "$LOG" "systemctl start --no-block zeroos-router@ethernet.service"
R stop ethernet

# --- USB-C: the gadget -----------------------------------------------------
R start usb
has "usb, switch on Host: says to set Slave" "$ZEROOS_ROUTER_STATE" "set the switch on the left to Slave"
mkdir -p "$T/sys/sys/class/udc/3f980000.usb"
mkdir -p "$T/sys/sys/kernel/config/usb_gadget/zeroos-stick"
echo 3f980000.usb > "$T/sys/sys/kernel/config/usb_gadget/zeroos-stick/UDC"
R start usb
has "usb while Car's stick mode is on: refused" "$ZEROOS_ROUTER_STATE" "USB stick mode is using the port"
rm -rf "$T/sys/sys/kernel/config/usb_gadget/zeroos-stick"
R start usb
G=$T/sys/sys/kernel/config/usb_gadget/zeroos-net
has "usb: on" "$ZEROOS_ROUTER_STATE" "state=on"
has "usb: lan is usb0" "$ZEROOS_ROUTER_STATE" "lan=usb0"
has "usb: its own subnet" "$LOG" "ipv4.addresses 10.43.0.1/24"
has "usb: gadget bound to the port" "$G/UDC" "3f980000.usb"
[ -L "$G/configs/c.1/ncm.usb0" ] && ok "usb: NCM function in the configuration" || bad "usb: no ncm link"
mac1=$(cat "$G/functions/ncm.usb0/host_addr")
[[ $mac1 =~ ^[0-9a-f][26ae](:[0-9a-f]{2}){5}$ ]] && ok "usb: locally administered MAC ($mac1)" || bad "usb: MAC $mac1"
has "kill switch on usb0" "$T/rules.nft" 'iifname "usb0" oifname != "tailscale0" counter drop'
R stop usb
[ ! -e "$G" ] || [ -z "$(cat "$G/UDC" 2>/dev/null)" ] && ok "usb stop: port released" || bad "usb stop: still bound"
R start usb
[ "$(cat "$G/functions/ncm.usb0/host_addr")" = "$mac1" ] && ok "usb: same MAC every time" || bad "usb: MAC changed"
R stop usb

R bogus > /dev/null 2>&1 && bad "bad command accepted" || ok "bad command refused"

[ "$fails" = 0 ] && echo "all router tests passed" || { echo "$fails failed"; exit 1; }
