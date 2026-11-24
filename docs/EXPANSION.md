# GPS and LoRa modules

The Cardputer Zero has two expansion ports: the 14-pin **EXT** header on the
back and the 4-pin **Grove** port. ZeroOS supports:

- **M5Stack Cap LoRa-1262** on EXT: an SX1262 LoRa radio (868-923 MHz) and
  an AT6668 GPS. It makes the Cardputer a [Meshtastic](https://meshtastic.org)
  node: text messages over radio, kilometres apart, with no phone signal or
  internet.
- **Any NMEA GPS** on the Grove port (in its default UART mode), or a USB GPS
  dongle.

Power the Cardputer off before plugging in a module; the ports aren't hot-swap.

## GPS

Open **GPS**. It turns on the expansion port's 5 V, waits for satellites
(about 30 s outdoors the first time; longer indoors) and shows:

- latitude and longitude, altitude, speed, heading, accuracy
- satellites used of those seen, and UTC time

**Enter** saves the spot to `~/Documents/waypoints.txt` (Sync copies it to your
PC). Leaving the app turns the 5 V off again unless the mesh is running or a
track is recording.

### Recording a track

**R** starts recording where you go; **R** again stops. The top right shows
the length so far and the time, and the top bar shows an arrow while a track
records.

- It keeps recording with the GPS app closed and with the screen off, so
  the Cardputer can sit in a pocket.
- Tracks are GPX files in `~/Documents/Tracks` (`track-<date>-<time>.gpx`),
  which Sync copies to your PC. Most map programs and sites open GPX.
- A point is saved when you have moved 5 m or more (at most one every two
  seconds); fixes worse than 50 m are skipped. The file is complete after
  every point, so a flat battery loses nothing.
- Recording keeps the GPS powered: stop it when you arrive **(measure the
  battery cost)**. A restart of ZeroShell ends the track.

## Mesh (Meshtastic)

Open **Mesh** and choose **Turn on mesh**. The radio starts in a few seconds,
and Mesh switches to its chat page:

- **On the Cardputer:** messages on the primary channel scroll above a text
  box; type and press **Enter** to send to everyone. The box takes typing
  straight away; while the top line says **Connecting to the radio...**,
  Enter keeps your text in the box until the radio is ready. Messages sent only to you
  are orange. **Tab** switches between chat and the info page (QR code, on/off).
  A message that arrives while you're in another app shows at the top of the
  screen with a beep.
- **From your phone:** scan the QR code; it opens the Meshtastic web client
  on the Cardputer (accept the certificate warning once). Or in the
  Meshtastic app, add a node by IP address (the address under the QR code).
  The phone and Cardputer need the same Wi-Fi, a phone hotspot, or Tailscale.
- **On a monitor:** in Dock mode, **Open on monitor** opens the web client in
  the browser.

The first time, set your **region** (for example US or EU_868) in the web
client's radio settings. The radio doesn't transmit until a region is set,
and it must match where you are and what other nodes use.

If the radio doesn't come up, the Mesh app says so instead of showing an
empty chat:

- **No radio:** meshtasticd started but couldn't talk to the SX1262 (its log
  says `SX126x init result -N` or `No SX1262 radio`). The cap isn't seated,
  has no 5 V, or a pin is wrong.
- **Radio error:** meshtasticd keeps exiting and systemd keeps restarting
  it. The app shows its last error line; **Turn on mesh** tries again.

The mesh node has no position of its own: meshtasticd can't read gpsd, and
gpsd already owns the GPS's serial port. Set a fixed position in the web
client if you want this node on the map.

Still to do: direct messages to one node, other channels, and keeping
messages after a restart (they're kept in memory, the last 200).

**Turn off mesh** stops the radio and the expansion port's 5 V. The mesh
uses its own battery: leave it off when you don't need it **(measure)**.

## How it works

| Piece | What |
| --- | --- |
| EXT pins | SPI0 on G9/G10/G11 with chip select CE1 (G7, `/dev/spidev0.1`; the LCD uses CE0), GPIO 22/23/26 for the radio's BUSY, IRQ and RESET, UART on G14/G15, I2C1 on G2/G3, 5 V out (off by default). From M5's pin map and [trail-mate](https://github.com/vicliu624/trail-mate)'s CardputerZero port. |
| Grove | UART on G14/G15 by default (G4 low), or I2C1 (G4 high). Same UART as EXT. |
| 5 V | An LED-class supply under `/sys/class/leds/` (`ext_5v_out` on M5's image). ZeroShell switches every `*5v*` one; `power-perms` lets it. |
| `config.txt`, `cmdline.txt` | `enable_uart=1`; no serial console on the UART, so it's free for GPS. |
| `gpsd` | Reads `/dev/serial0` at 115200 (and USB GPS dongles). Socket-activated: it starts when the GPS app connects and stops reading the receiver when they stop watching. Config: `/etc/default/gpsd`. |
| `meshtasticd` | Meshtastic's Linux daemon, from Meshtastic's Debian repository (beta channel), installed with the Mesh app ([APPS.md](APPS.md)); the repository's key is checked against `MESHTASTIC_KEY_FPR` in `image/build.sh`. The repository keeps only its newest build, so that is what you get; to stay on one, `sudo apt-mark hold meshtasticd`. Off at boot; the Mesh app starts it (polkit allows `pi` exactly this unit). Radio config: `/etc/meshtasticd/config.d/zeroos-cap-lora-1262.yaml` (SX1262 on `spidev0.1` at 1 MHz, RESET 26, BUSY 22, IRQ 23, DIO2 as RF switch, 3.0 V TCXO on DIO3), as in Meshtastic's own `lora-cardputer-zero.yaml`. Before every start, `/usr/lib/zeroos/mesh-cap-power` (run as root by the unit) turns on the expansion 5 V and drives P0 of the cap's IO expander (PI4IOE5V6408, I2C 0x43 on `/dev/i2c-1`) high: that is the antenna switch, and the expander forgets it whenever the 5 V goes off. Before Mesh 1.0.2 the TCXO was set to 1.8 V and the antenna switch was never set, so the radio couldn't work. It doesn't use the GPS (it can't read gpsd, and a second reader on the UART would garble gpsd's). It serves the web client on port 9443 and the app API on TCP 4403. |
| Chat in ZeroShell | A small client for that API (`hal/meshtastic.cpp`, hand-written protobuf, no library): asks for the node list, reads text messages, sends to the primary channel. |

## To confirm on hardware

- The radio settings, now the same as Meshtastic's and M5's for this cap,
  on a ZeroOS device. If the Mesh app says **No radio** or **Radio error**
  (the daemon keeps stopping; the app shows its last error line), check
  `journalctl -u meshtasticd -b`: the `zeroos-mesh:` lines say whether the
  antenna switch was set, then come meshtasticd's own errors. `ls /dev/spidev0.*` should list `spidev0.1` (needs `dtparam=spi=on`;
  M5's overlay turns off only `spidev0.0`, which the LCD uses). GPIO 22
  (BUSY) is also M5's optional third chip select: don't load their
  `spi0-spidev2-gpio22` overlay with the LoRa cap fitted.
- The RF switch: M5's page drives it from an IO expander on the Cardputer
  ADV; trail-mate uses DIO2 on the Zero, as we do.
- Whether `ext_5v_out` (and a Grove 5 V switch) appear under
  `/sys/class/leds/`.
- That the UART is `/dev/serial0` (the mini UART, since Bluetooth uses the
  main one) and the GPS answers at 115200.
- Memory and battery cost of meshtasticd **(measure)**.
