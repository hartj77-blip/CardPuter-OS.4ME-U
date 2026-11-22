# Games

**Games** plays your games for classic consoles on the built-in screen. It runs the emulators as
**libretro cores**: the same emulator cores RetroArch uses, loaded by an app built for the
Cardputer's 320 x 170 screen and keyboard. Install it from the **Apps** app.

Only play games you own. ZeroOS comes with no games and no console BIOS files.

## Why not RetroArch itself?

RetroArch draws with OpenGL, and the built-in screen is a small SPI panel with no GPU behind it.
RetroArch also expects a gamepad and a big screen for its menus. So Games is a small frontend of
its own that runs the same libretro cores, scales each picture to fit the panel, and uses the
keyboard. A core you download for Games is a normal libretro core, and the save files are the
usual `.srm` battery saves.

On an HDMI monitor ([Dock](DOCK.md)) you can also run RetroArch itself
(`sudo apt install retroarch`), with the cores Games installed.

## Consoles

| Console | Folder in `~/Games` | Files | Core |
| --- | --- | --- | --- |
| NES / Famicom | `nes` | `.nes .fds .unf` | Nestopia (comes with Games), FCEUmm |
| Game Boy | `gb` | `.gb` | Gambatte (comes with Games), mGBA |
| Game Boy Color | `gbc` | `.gbc` | Gambatte, mGBA |
| Game Boy Advance | `gba` | `.gba` | mGBA (comes with Games) |
| Super Nintendo | `snes` | `.sfc .smc` | Snes9x 2010 |
| Mega Drive / Genesis | `md` | `.md .gen .smd .bin` | Genesis Plus GX, PicoDrive |
| Master System / Game Gear | `sms` | `.sms .gg .sg` | Genesis Plus GX, PicoDrive |
| PC Engine / TurboGrafx-16 | `pce` | `.pce` | Beetle PCE Fast |
| Atari 2600 | `a26` | `.a26` | Stella 2014 |
| PlayStation | `psx` | `.cue .chd .pbp .m3u` | PCSX ReARMed |
| WonderSwan | `ws` | `.ws .wsc` | Beetle WonderSwan |

Unzip games first: cores here read the plain file, not a `.zip`. PlayStation games need the `.cue`
and its `.bin` files side by side; for a game on several discs, an `.m3u` that lists them is
shown instead. PCSX ReARMed has its own BIOS built in. A real one in `~/Games/system/` is used if
it's there, and some games need it.

## Getting games on

Games opens with a folder for each console in `~/Games`. Copy your files in with:

- **Files**, from a USB stick
- **Sync** (Syncthing), from a folder on your PC
- **Remote**'s Taildrop, then move them from `~/Downloads`
- `scp game.gba pi@cardputer.local:Games/gba/` from a PC

## Cores

NES, Game Boy, Game Boy Color and Game Boy Advance work as soon as Games is installed: their
cores come from Debian with it. For the other consoles, press **C** for the core list and
**Enter** on one to download it from libretro's nightly builds (buildbot.libretro.com, about
1 to 3 MB each). Downloaded cores go in `~/Games/cores`; **Del** removes one. Removing the Games
app keeps your games, saves and downloaded cores.

You can also copy any `<name>_libretro.so` built for arm64 into `~/Games/cores` yourself. Games
uses a core it knows for a console (see the table above). A copy in `~/Games/cores` is used
before the system one.

## Playing

| Key | |
| --- | --- |
| W A S D (or the arrow keys) | D-pad |
| K / J | A / B |
| I / U | X / Y |
| Q / E | L / R |
| Enter / Space | Start / Select |
| Esc | Menu: Resume, Save state, Load state, Reset, Quit game |

On a full-size keyboard, X and Z are A and B too. Hold several keys at once: Games reads the
keyboard directly, so you can run and jump.

**Gamepads** work as well: USB, or Bluetooth. Pair a Bluetooth one once in the Terminal: turn on
Bluetooth in **Settings**, put the gamepad in pairing mode, then run `bluetoothctl`, and in it
`scan on`, then `pair`, `trust` and `connect` with the gamepad's address. Buttons follow the usual
layout, the d-pad or left stick moves, and Home/Guide opens the menu. A gamepad that connects
mid-game is picked up within a couple of seconds.

**Saves.** Games with a battery save (most RPGs) are saved as they are played and when you quit,
in `~/Games/saves/<game>.srm`. **Save state** stores the exact moment in
`~/Games/states/<game>.state` (one per game); **Load state** goes back to it. Turning the screen
off, or putting the Cardputer face down, pauses the game.

**Sound** plays through the speaker or headphones at the system volume.

## How well it runs

The Cardputer Zero has a quad-core 1 GHz Arm Cortex-A53. NES, Game Boy, Game Boy Color, Master
System, Game Gear, Atari 2600, PC Engine, WonderSwan and Mega Drive run at full speed. Game Boy
Advance and Super Nintendo (Snes9x 2010) run at full speed in most games. bsnes-mercury is too
slow here; use Snes9x 2010. PlayStation (PCSX ReARMed) runs many games at full speed. These are
estimates for this chip and haven't been measured on the device yet. Use the **Performance**
power profile in **Battery** for the demanding ones.

The picture is scaled to fit the screen, keeping its shape. A Game Boy picture (160 x 144) is
shown at its real size, because that looks sharpest. Bigger pictures are scaled down, so very
small text in some games can be hard to read.
