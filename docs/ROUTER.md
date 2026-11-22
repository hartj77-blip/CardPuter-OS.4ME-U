# Router: your laptop through a Tailscale exit node

The **Router** app makes the Cardputer a pocket router. Plug a laptop into it
and everything the laptop sends goes through Tailscale to an **exit node**:
one of your own machines (your home server, say) or a Mullvad location. The
Cardputer itself is online over Wi-Fi or a tethered phone.

```
laptop --cable--> Cardputer --Wi-Fi / phone--> internet --WireGuard--> exit node --> the web
```

The laptop needs no Tailscale and no setup: it gets an address by itself, like
on any network. To websites it looks as if it is at the exit node.

| | Ethernet | USB-C |
| --- | --- | --- |
| Cable | Network cable into the Ethernet port | USB-C cable into the **right-hand** USB-C port |
| Host / Slave switch | **Host** | **Slave** |
| The Cardputer is online through | Wi-Fi, or a phone on USB-A or Bluetooth | Wi-Fi, or a phone over Bluetooth (USB-A is off on Slave) |
| Power | Battery, or a charger in the other USB-C port | The laptop powers it over the same cable (charging not checked yet) |
| Laptop sees | A wired network | A USB network adapter (NCM): Linux, macOS, Windows 11 and ChromeOS have the driver built in |

You need the **Remote** app installed and signed in to Tailscale (see
[REMOTE.md](REMOTE.md)), and at least one exit node in your tailnet.

## Using it

1. Open **Router**.
2. **Exit node**: press Enter and pick one. Your own machines come first, then
   Mullvad's locations if your tailnet has them. Machines that are offline
   are greyed out.
3. Press Enter on **Ethernet** or **USB-C**. It says **starting...** for a few
   seconds, then **on**.
4. Plug in the laptop. The row says **in use**, and the panel shows the
   laptop's name and address and the speed in both directions.

Enter on the row again turns it off. Turning on the other one turns the first
one off: one at a time.

Router mode **stays on after a restart**: if the Cardputer shuts down with it
on, it comes back on by itself. While it is on, **Settings > Tailscale** is
set to **Always on**, because the tunnel must not stop after 15 minutes;
turning the router off puts your own setting back.

### Making a machine an exit node

On a Linux machine with Tailscale:

```bash
sudo tailscale set --advertise-exit-node
```

Then approve it in the Tailscale admin console (Machines > the machine >
Edit route settings > Use as exit node). On Windows and macOS it is in the
Tailscale menu: Exit node > Run as exit node. See
[Tailscale's exit node guide](https://tailscale.com/kb/1103/exit-nodes).

## Safety: the kill switch

The laptop's traffic can only leave through the tunnel. If Tailscale drops,
the exit node goes offline or the Wi-Fi changes, the laptop simply has no
internet until the tunnel is back; nothing goes out through the Wi-Fi
directly, and the Wi-Fi network can't reach the laptop either.

- **DNS** goes through Tailscale's resolver (`100.100.100.100`), which uses the
  exit node, with `1.1.1.1` as a fallback, also through the exit node. The
  Wi-Fi's own DNS server is never asked.
- **IPv6** is off on the laptop's link, so nothing goes around the tunnel over
  IPv6.
- The laptop is told to use packets of at most 1280 bytes (the tunnel's
  size), and TCP connections are clamped to fit, so pages don't hang.

## Speed

Expect about **15-30 Mbit/s**. The limit is the Cardputer's Wi-Fi, which is
2.4 GHz only. The Ethernet port is 100 Mbit/s, and WireGuard encryption is not
the bottleneck on its four cores. Video calls and streaming in HD work; big
downloads are slower than on the laptop's own Wi-Fi.

With the screen off the router keeps working. Wi-Fi, Ethernet and the tunnel
together use the battery much faster than the Cardputer on its own, so keep it
on a charger for long sessions (on USB-C, the laptop is the charger).

## When something is wrong

| The app says | What to do |
| --- | --- |
| Router needs Remote (Tailscale) | Install **Remote** from Apps and sign in. |
| Sign in to Tailscale in Remote first | Open **Remote** and scan the sign-in code. |
| Pick an exit node first | **Exit node** > Enter, then pick one. If the list is empty, make a machine an exit node (above). |
| *name* is offline: no internet until it's back | The exit node is off or unreachable. Pick another, or wait. |
| No USB device port: set the switch on the left to Slave | USB-C mode needs the switch on **Slave**. Turn it off and on again after moving the switch. |
| Car's USB stick mode is using the port | Turn off **USB stick mode** in Car; the port does one thing at a time. |
| Plug the laptop into ... | Router is on but no cable link yet. On USB-C, use the **right-hand** port and a data cable (not a charge-only one). |
| Laptop plugged in, getting an address... | The laptop is set to a fixed address: set it back to automatic (DHCP). |

On the laptop, `curl https://ifconfig.me` (or any "what is my IP" page) should
show the exit node's address.

Over SSH, `systemctl status zeroos-router@ethernet` (or `@usb`) and
`journalctl -u zeroos-router@ethernet` show what happened, and
`cat /run/zeroos/router.state` shows what the app sees.

## How it works

| Piece | What it does |
| --- | --- |
| `zeroos-router@ethernet.service`, `zeroos-router@usb.service` | Run `/usr/lib/zeroos/router start <mode>` as root, and `stop` when turned off. polkit (`91-zeroos-router.rules`) lets the Router app start and stop them. |
| Tailscale | The helper starts `tailscaled` and checks that you are signed in and an exit node is picked. Picking one in the app runs `tailscale set --exit-node=<ip>` as `pi` (Tailscale's operator). |
| Kill switch | An nftables table, `inet zeroos-router`, loaded **before** the network comes up: traffic from the laptop may only go out of `tailscale0`, nothing may come in except from `tailscale0`; outgoing traffic is masqueraded to the Cardputer's Tailscale address; TCP MSS is clamped to the tunnel's MTU. |
| The laptop's network | A NetworkManager connection, `zeroos-router`, in **shared** mode on `eth0` (10.42.0.1/24) or `usb0` (10.43.0.1/24): NetworkManager runs dnsmasq for DHCP and DNS. `dnsmasq-shared.d/zeroos-router.conf` points its DNS at Tailscale and hands out MTU 1280. |
| Routing | `ip rule to <subnet> lookup main` (priority 5200), so answers to the laptop stay on the cable instead of following Tailscale's exit-node routes into the tunnel. |
| USB-C | A configfs USB gadget, `zeroos-net`, with one NCM network function, on the `dwc2` device port (as Car's USB stick mode). Its MAC addresses come from the machine ID, so the laptop sees the same adapter every time. |
| After a restart | The helper remembers the mode in `/var/lib/zeroos/router.mode`; `zeroos-router-boot.service` starts it again at boot. Turning it off by hand forgets it; a shutdown doesn't. |

The helper's tests run on a PC with stand-ins for every command:
`image/tests/router-test.sh`.

## Not checked on a device yet

Nothing here has run on a Cardputer. In particular:

- that the laptop gets an address and browses through the exit node over
  Ethernet, and over USB-C on Linux, macOS and Windows 11;
- that with the exit node turned off, the laptop gets nothing (the kill
  switch), and that DNS lookups don't go to the Wi-Fi's DNS server;
- that NetworkManager's shared mode and Tailscale's own firewall rules work
  together as described, and the real speed;
- whether the right-hand USB-C port charges the Cardputer from a laptop;
- that router mode comes back after a restart.

## Limits

- One laptop (or one switch with several devices behind it on Ethernet; each
  gets an address). No Wi-Fi hotspot: the Cardputer's one Wi-Fi radio is busy
  being its uplink.
- Older Windows (10 and before) has no NCM driver, so USB-C mode needs
  Windows 11 there; Ethernet works everywhere.
- The laptop can't reach devices on the Wi-Fi network the Cardputer is on.
  It can reach the machines on your tailnet, by name too (MagicDNS), and
  they see it as the Cardputer.
