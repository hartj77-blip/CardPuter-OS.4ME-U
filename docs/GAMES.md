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

Games plays on a TV itself (below). In Dock mode ([Dock](DOCK.md)) you can also run RetroArch
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

**How safe is a downloaded core?** A core is a program that Games runs with your user's rights,
so the download is kept narrow: https only, from the fixed address `buildbot.libretro.com`,
following no redirects; unpacked aside and moved into place only when it is a whole Linux
library; into `~/Games/cores`, which only you can write to. What it can't do is check the core
against a known fingerprint: libretro's nightly builds change every day and come with no
published checksums or signatures. So you trust libretro's build server (and its https
certificate) for each core you download, as RetroArch users do. Cores from Debian (the ones
Games installs, or `sudo apt install libretro-...`) are checked by apt's signatures and are
the safer choice where one exists.

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

**Gamepads** work as well: USB, or Bluetooth. Pair a Bluetooth one once: put it in pairing mode,
open **Settings > Bluetooth** (or type "bluetooth" on the home screen), press **Ctrl + S** to scan and
**Enter** on the gamepad. It reconnects by itself after that. Buttons follow the usual
layout, the d-pad or left stick moves, and Home/Guide opens the menu. A gamepad that connects
mid-game is picked up within a couple of seconds.

**Xbox Wireless Controller** (Xbox One S / Series, Bluetooth): turn it on, hold the small pair
button on top until the Xbox logo flashes fast, then **Ctrl + S** and **Enter** in **Settings > Bluetooth**. The logo
goes solid when it's paired. Pairing only works for 2 minutes after a scan (anything nearby could
pair while it's open). If the logo keeps blinking after pairing, update the controller (Xbox
Accessories app on Windows, or an Xbox), then **X** (forget) and pair again. ZeroOS sets BlueZ up
for these controllers (`Privacy = device` and steady LE connection values in
`/etc/bluetooth/main.conf`, set by `/usr/lib/zeroos/bluetooth-conf`): without them they drop
after a couple of minutes and forget the pairing.

**Two players (or more).** Each gamepad is a player of its own: the first to connect is player
1, the next player 2, up to 4. The keyboard is always player 1; **Swap players 1 and 2** in the
menu makes a lone gamepad player 2, so the keyboard and a gamepad can play together. Players 3
and 4 need a core with a multitap. In the menu, a gamepad's d-pad moves, A chooses and B goes back.

**On a TV.** With a TV or monitor on the HDMI port, the game plays on it at 720p, scaled up by a
whole number where that fills most of the screen; the menu shows on both screens. Sound stays on
the Cardputer. Plugging in or out mid-game moves the picture over. **Play on TV: Off** in the menu
keeps it on the built-in screen. Leave Dock mode first: it has the HDMI port while it runs. Games
1.2.0 and later; details in the app's own guide (CardPuter-Apps.4ME-U, `apps/games/README.md`).

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
power profile (**Settings > Battery & power**) for the demanding ones.

The picture is scaled to fit the screen, keeping its shape. A Game Boy picture (160 x 144) is
shown at its real size, because that looks sharpest. Bigger pictures are scaled down, so very
small text in some games can be hard to read.
