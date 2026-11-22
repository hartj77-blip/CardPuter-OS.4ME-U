# Net Tools

Net Tools is one installable pack ([APPS.md](APPS.md)) that adds six
keyboard-driven network apps to the home screen, each a front-end for a standard
Linux tool. Like the RF pack, the apps are thin: the analysis is done by the
same programs you'd run on any Linux box, so the output is the real thing.

It works over whatever connection the device has — the built-in Wi-Fi, a
tethered phone, or the **Ethernet port**. Discover and Capture let you pick the
interface.

Install it from **Apps** (pick **Net Tools**, **Install**), then open any of the
apps below. Removing the pack removes the apps and the tools it brought.

| App | Tool | What it does |
| --- | --- | --- |
| **Discover** | `arp-scan` | Every device on your network: IP, vendor, MAC |
| **Ports** | `nmap` | The open ports and services on one host |
| **Trace** | `mtr` | The path to a host, with loss and latency per hop |
| **DNS** | `dig` | Look up a name (A, AAAA, MX, TXT, NS) |
| **Capture** | `tcpdump` | Watch live traffic and save a `.pcap` for Wireshark |
| **Netinfo** | `ip`, `ss` | This device's interfaces, routes, DNS and open ports |

## Discover

Sweeps the local subnet and lists every device that answers, newest address
order, with the hardware vendor read from its MAC.

| Key | Does |
| --- | --- |
| Up/Down | Move through the devices |
| Enter | Scan that device's ports (opens **Ports**) |
| R | Scan again |
| F | Interface: auto, `eth0` (Ethernet), `wlan0` (Wi-Fi) |

## Ports

Runs an nmap connect scan of the common ports on one host and lists the service
nmap names on each. Type an address, or arrive here with **Enter** from
Discover. **R** scans again; **Esc** goes back to type another host.

## Trace

Shows the route to a host — one line per hop with packet loss and round-trip
time, so you can see where a connection slows down or stops. Type a host and
**Enter**.

## DNS

Looks up a name with dig. **Left**/**Right** change the record type (A, AAAA,
MX, TXT, NS); **R** looks it up again.

## Capture

Records the traffic on the wire. **Space** starts and stops; while it runs you
see a live one-line-per-packet summary and the packets are written to a `.pcap`
in `~/Recordings` at the same time. Open the file in Wireshark on a PC (Sync
copies `~/Recordings` off the device). **F** picks the interface before you
start; **R** clears the view.

## Netinfo

Two pages (**Left**/**Right**):

- **Links** — every interface (Ethernet, Wi-Fi, USB) with its state and address,
  the default gateway and the DNS servers in use.
- **Listening** — the TCP and UDP ports this device is accepting connections on.

**R** refreshes.

## Permissions

Finding devices (arp-scan) and capturing packets (tcpdump) need root. The pack
installs a small `sudoers` rule (`/etc/sudoers.d/zeroos-net`) that lets the
home-screen user run just those two tools without a password, which is how the
apps run them. Everything else — nmap's connect scan, mtr, dig, ip, ss — runs as
you, with no extra privileges. Removing the pack removes the rule.

## A note on use

These are the ordinary tools a network admin uses to see what's on a network and
how it's reaching the internet. Scan and capture only on networks you own or are
allowed to test — port scanning and packet capture on networks you don't control
may be against the rules or the law where you are.

## On the simulator

In the PC simulator and the headless build there is no real network, so the apps
replay a small canned network (a router, a Pi, a few phones and laptops) so you
can see how each screen looks. On the device they run the real tools.
