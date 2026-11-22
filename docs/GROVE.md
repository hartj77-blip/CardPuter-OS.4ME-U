# Grove

The **Grove** app is for things plugged into the Cardputer Zero's 4-pin Grove port. For now it
lights a NeoPixel LED strip (WS2812 or SK6812), such as M5Stack's Grove RGB LED strip. Other Grove
devices will be added to the app's first page as they're supported.

Power the Cardputer off before you plug anything into the Grove port. The port isn't hot-swap.

## LED strip

Install **Grove** from the Apps app, open it and choose **LED strip (NeoPixel)**.

| Key | What it does |
| --- | --- |
| Up / Down | Choose a row |
| Left / Right (or Enter) | Change it |
| Space | Lights on or off |
| Esc | Back to the device list |

- **Effect**: Solid, Breathe, Rainbow, Chase or Sparkle.
- **Colour**: used by every effect except Rainbow.
- **Brightness**: starts at 10%. NeoPixels are bright, and the Grove port's 5 V can't power a
  long strip at full white. The service also limits the whole strip to about 500 mA by dimming it.
- **Speed**: how fast the animations run.
- **LEDs**: how many LEDs the strip has. M5Stack's Grove strips have 10, 15 or 20.

The row of dots at the top previews the effect. Changing any setting turns the lights on, so you
see what you picked. The strip **stays lit after you leave the app**. Open the app again and
press Space to turn it off. Your settings are kept for next time.

### From a script or the Terminal

The service listens on `/run/zeroos-grove-ledd.sock`. Users in the `gpio` group, which includes
`pi`, can use it. Send it one line at a time, and each line gets a one-line reply:

```bash
echo "set effect=rainbow brightness=20 count=15" | socat - UNIX-CONNECT:/run/zeroos-grove-ledd.sock
```

```bash
echo "set effect=pixels pixels=ff0000,00ff00,0000ff" | socat - UNIX-CONNECT:/run/zeroos-grove-ledd.sock
```

```bash
echo off | socat - UNIX-CONNECT:/run/zeroos-grove-ledd.sock
```

| Command | What it does |
| --- | --- |
| `set key=value ...` | Change any of: `effect` (off, solid, breathe, rainbow, chase, sparkle, pixels), `color` (hex, like `ff8000`), `brightness` (1 to 100), `speed` (1 to 10), `count` (1 to 144), `pixels` (hex colours separated by commas, for `effect=pixels`) |
| `off` | Turn the strip off |
| `status` | Show the settings, then `hardware=on`, `off` or `error`, with `error=<why>` |

`socat` isn't installed by default. Install it with `sudo apt install socat`, or use Python's
`socket` module.

### Using GPS and the LED strip

The LED strip and a Grove GPS use the same two pins, so only one can be plugged in at a time.
While the strip is lit, the Grove pins aren't a UART. Two seconds after the strip is turned off,
the service gives the pins back to the UART, so the GPS app works again.

The GPS and Mesh apps turn the expansion port's 5 V off when you leave them. If you open one of
them while the strip is lit, open Grove afterwards and press Space twice to turn the strip back on.

## How it works

The Grove port's signal pins are GPIO 14 and 15 (in its default UART mode; see
[EXPANSION.md](EXPANSION.md)). NeoPixels need exact timing: a 0 bit and a 1 bit differ by about
300 ns. The usual Raspberry Pi NeoPixel library only works on a PWM, PCM or SPI pin, and none of
those reach the Grove port. Timing the pin from a program doesn't work either, because Linux
interrupts it and the colours glitch.

GPIO 14 and 15 are also data lines 6 and 7 of the SoC's **SMI** (secondary memory interface), a
parallel bus that sends bytes at a fixed rate, fed by DMA. So:

| Piece | What |
| --- | --- |
| `zeroos-grove-ledd` | A small root service (`shell/ledd/`). It maps the SMI, DMA, clock and GPIO registers through `/dev/mem`, and gets uncached memory for DMA from the VideoCore through `/dev/vcio`. Effects are drawn by the same code as the app's preview (`shell/src/grove/leds.h`). |
| Timing | SMI clock 125 MHz (PLLD / 4), 39 clocks = 312.5 ns per byte, 4 bytes per LED bit (1.25 µs). A 0 is high for 312 ns, a 1 for 625 ns. 300 µs low before and after each frame latches every WS2812 revision. Colours are sent green, red, blue. |
| Pins | Both GPIO 14 and 15 carry the signal, so the strip works whichever Grove wire its data is on. The pins' old function (the UART) is saved and restored when the strip turns off. `--pins 14` or `--pins 15` drives only one of them. |
| DMA | Channel 10, as rpi_ws281x uses (`--dma N` to change it). The service refuses a channel that is already busy. |
| Start and stop | `zeroos-grove-ledd.socket` starts the service when someone connects. With the strip off, it releases the pins after 2 s, and exits after a minute with no one connected. |
| 5 V | The service switches the expansion port's 5 V on (`/sys/class/leds/*5v*`) whenever the strip is lit. |
| Without hardware | `zeroos-grove-ledd --dry-run --socket /tmp/s` prints frames instead of lighting a strip. `shell/tests/ledd-test.sh` uses it in CI. |

## To confirm on hardware

None of this has been tried on a real Cardputer Zero yet.

- **The strip lights at all.** If it doesn't, check `journalctl -u zeroos-grove-ledd` and the
  `error=` part of `status`. A timeout means SMI or DMA didn't move data. A mailbox error means
  the VideoCore didn't hand out memory (`gpu_mem` is 32 MB on ZeroOS).
- **Which Grove wire is data**, and whether the Grove port's UART/I2C switch (G4) passes GPIO 14
  and 15 straight through in both directions. If only one pin reaches the port, the other can be
  dropped with `--pins`.
- **3.3 V data into a 5 V strip.** Most WS2812B and SK6812 strips accept it. If the first LED
  flickers, lower the strip's supply or add a level shifter.
- **DMA channel 10** isn't used by this kernel. If the SD card or screen misbehaves while the
  strip runs, try another channel.
- **The 5 V budget** of the Grove port. The 500 mA limit is a guess (`--max-ma` changes it).
