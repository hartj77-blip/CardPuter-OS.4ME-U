# M5Stack's app store on ZeroOS: compatibility checklist

<!-- Written by tools/m5-apps.py from M5's registry.json and docs/m5-apps-notes.tsv. Edit those, not this file. -->

ZeroOS's Apps app lists [M5Stack's app store](https://github.com/CardputerZero/packages) alongside its own apps (marked **M5**). Those apps were made for M5's own launcher, which ZeroOS replaces, so not all of them work here. This page lists every one of them with what a look inside its package found, and what has been checked on a real Cardputer Zero running ZeroOS. How ZeroOS runs them is in [the apps guide](APPS.md#m5stacks-app-store).

Other places: [ZeroOS](../README.md) · [ZeroOS apps](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U) · [Browser emulator](https://hartj77-blip.github.io/emu-cardputer-os.4me-u/) ([repository](https://github.com/hartj77-blip/emu-cardputer-os.4me-u))

Store list of 2026-11-25, 179 apps. Checked on a device so far: **0**.

## Summary

| Verdict | Apps | What it means |
| --- | --- | --- |
| [Won't install](#wont-install) | 2 | Depends on something ZeroOS doesn't have (usually `applaunch`, M5's own launcher). apt's dry run stops it, so nothing on the device changes. |
| [Needs M5's launcher](#needs-m5s-launcher) | 1 | Installs, but the app itself (not just its install script) talks to M5's launcher, which ZeroOS replaces. Expect it not to work, or to work only in part. |
| [Installs; check closely](#installs-check-closely) | 10 | Nothing rules it out, but something in it may not fit ZeroOS: see the notes. |
| [Should work](#should-work) | 166 | Nothing in the package stands in the way: it draws on the LCD's framebuffer (or runs in the Terminal) and reads the keyboard, which ZeroOS hands over. |
| [Nothing to open](#nothing-to-open) | 0 | No launcher entry, so nothing appears on the home screen. |

The verdicts come from looking inside each package, not from running it, so treat them as a guess until the device columns are filled in. Every app also needs what M5's store says it needs (the **Heads-up** column), and apps that use the network need Wi-Fi.

## The device columns

| Column | Ticked when |
| --- | --- |
| Installs | Apps > Enter installs it without an error |
| Opens | It appears on home and draws on the screen when opened |
| Keys | Its keys do what its own help says |
| Leaves | Its own exit, or holding Esc for 3 seconds, gets back home with the screen working |

☐ not tried yet · ✅ works · 🟡 partly · ❌ doesn't · – doesn't apply

Tried one? Add a line to [m5-apps-notes.tsv](m5-apps-notes.tsv) and run `tools/m5-apps.py render`, or [open an issue](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/issues) with the app's name, the ZeroOS version and what happened.

## Won't install

Depends on something ZeroOS doesn't have (usually `applaunch`, M5's own launcher). apt's dry run stops it, so nothing on the device changes.

| App | Category | Heads-up | Notes | Installs | Opens | Keys | Leaves |
| --- | --- | --- | --- | :-: | :-: | :-: | :-: |
| [Voice Input](https://github.com/hellomsg/cardputer-zero-voice-input)<br>`m5-voice-input` 0.2.1 | AI, System Tools | runs in the background, microphone | missing: `applaunch`; Plugs into M5's launcher, so useless without it | ☐ | ☐ | ☐ | ☐ |
| [LAN Proxy](https://github.com/tongfei11/LANProxy)<br>`m5-lan-proxy` 0.1.3 | System Tools | runs in the background | missing: `applaunch` | ☐ | ☐ | ☐ | ☐ |

## Needs M5's launcher

Installs, but the app itself (not just its install script) talks to M5's launcher, which ZeroOS replaces. Expect it not to work, or to work only in part.

| App | Category | Heads-up | Notes | Installs | Opens | Keys | Leaves |
| --- | --- | --- | --- | :-: | :-: | :-: | :-: |
| [Desk Mode](https://github.com/InstantComet/cz-desk-mode)<br>`m5-cz-desk-mode` 1.0.1 | System Tools | runs in the background, monitor | Swaps M5's launcher for a desktop on HDMI. ZeroOS's Dock app does this instead | ☐ | ☐ | ☐ | ☐ |

## Installs; check closely

Nothing rules it out, but something in it may not fit ZeroOS: see the notes.

| App | Category | Heads-up | Notes | Installs | Opens | Keys | Leaves |
| --- | --- | --- | --- | :-: | :-: | :-: | :-: |
| [VibeOffice](https://github.com/tcchen2026/VibeOffice)<br>`m5-vibeoffice` 2026.11.09+git718d37 | Creative & Office, Lifestyle | monitor | Runs in its own desktop (cage + WebKitGTK) mirrored to the LCD; heavy | ☐ | ☐ | ☐ | ☐ |
| DOOM Zero<br>`m5-doom-zero-dev0` 0.0.1~dev0 | Games |  | Runs the game in its own X display and copies it to the LCD; heavy | ☐ | ☐ | ☐ | ☐ |
| [Zero Browser](https://github.com/geo-tp/Zero-Browser)<br>`m5-zero-browser` 1:0.1-2 | Media, Other |  | uses /dev/ttyN; Runs in its own desktop (labwc + QtWebKit) mirrored to the LCD; heavy | ☐ | ☐ | ☐ | ☐ |
| [Flint](https://github.com/flintdevices/flint)<br>`m5-flint` 0.0.59 | Security, Radio & Comms | needs extra hardware | opens /dev/input/eventN by number (may pick the wrong device); uses /dev/ttyN; adds you to groups; Adds boot services that take over the text console (fbcon) before login | ☐ | ☐ | ☐ | ☐ |
| [RaspyJack](https://github.com/7h30th3r0n3/RaspyJack)<br>`m5-raspyjack` 0.1.4-raspyjack1 | Security, Radio & Comms | runs in the background, needs extra hardware, camera, microphone, motion sensor | opens /dev/input/eventN by number (may pick the wrong device); Big install: web UI, Caddy, turns Apache off, and its scripts swap out M5's launcher (nothing to swap here) | ☐ | ☐ | ☐ | ☐ |
| [Battery Saver](https://github.com/InstantComet/cz-battery-saver)<br>`m5-cz-battery-saver` 1.1.1 | System Tools | runs in the background | Saves power by pausing M5's launcher; here only its other savings apply, and they overlap ZeroOS's own power saving | ☐ | ☐ | ☐ | ☐ |
| [Nook](https://github.com/delikely/Nook)<br>`m5-nook-cardputerzero` 0.1.10 | System Tools, Security | runs in the background, needs extra hardware | Wants to be started by M5's launcher; without it, runs in its standalone mode | ☐ | ☐ | ☐ | ☐ |
| Remote<br>`m5-cardputer-remote` 0.2-6 | System Tools, Development |  | Phone remote. Its go-home action restarts M5's launcher, which ZeroOS doesn't have | ☐ | ☐ | ☐ | ☐ |
| [USB Imager](https://github.com/geo-tp/USBImager-Zero)<br>`m5-zero-usbimager` 1.0.10-24.gtk | System Tools, Development |  | Runs a GTK app in its own desktop (labwc) mirrored to the LCD | ☐ | ☐ | ☐ | ☐ |
| [ZeroPower](https://github.com/baltamir1978/ZeroPower)<br>`m5-zeropower` 0.1.4-rev1 | System Tools | runs in the background | Power daemon that watches M5's launcher; overlaps ZeroOS's own power saving | ☐ | ☐ | ☐ | ☐ |

## Should work

Nothing in the package stands in the way: it draws on the LCD's framebuffer (or runs in the Terminal) and reads the keyboard, which ZeroOS hands over.

| App | Category | Heads-up | Notes | Installs | Opens | Keys | Leaves |
| --- | --- | --- | --- | :-: | :-: | :-: | :-: |
| [DaliCam](https://github.com/Coreymillia/DaliCam)<br>`m5-dalicam` 0.1.0-rev1 | AI, Creative & Office | camera |  | ☐ | ☐ | ☐ | ☐ |
| [Gemini Live](https://github.com/d4rkmen/geminilive)<br>`m5-geminilive` 0.1.0-1 | AI, Lifestyle | microphone |  | ☐ | ☐ | ☐ | ☐ |
| [KimiRC](https://github.com/mengxiyou/KimiRC)<br>`m5-kimi-remote` 1.0.1-1 | AI, Development |  |  | ☐ | ☐ | ☐ | ☐ |
| [MiMo Talk](https://github.com/RiviaRammer/CardputerZeroApps)<br>`m5-mimo-talk` 0.2.2 | AI |  |  | ☐ | ☐ | ☐ | ☐ |
| [Muser](https://github.com/YuHaibo/Muser)<br>`m5-muser` 1.0.0 | AI | microphone |  | ☐ | ☐ | ☐ | ☐ |
| [SeeSay](https://github.com/xnppnx/seesay)<br>`m5-seesay` 1.0.0-rev1 | AI, Hardware & IoT | camera | Qt and Tk are only inside bundled Python wheels | ☐ | ☐ | ☐ | ☐ |
| [TalkToMe](https://github.com/d4rkmen/talktome)<br>`m5-talktome` 0.4.0 | AI, Media |  | Wayland is only used for the clipboard | ☐ | ☐ | ☐ | ☐ |
| [XiaoZhi](https://github.com/JdaieLin/cardputer-xiaozhi)<br>`m5-cardputerzero-xiaozhi` 0.2.7-m5stack1 | AI, Radio & Comms | camera, microphone | Only its installer's message names M5's launcher | ☐ | ☐ | ☐ | ☐ |
| [AcidZero](https://github.com/kurogedelic/AcidZero)<br>`m5-acidzero` 0.2.0 | Creative & Office | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [BitMap16 DX](https://github.com/bradmcnally/bitmap16dx)<br>`m5-bitmap16dx` 0.9.1-1 | Creative & Office | motion sensor |  | ☐ | ☐ | ☐ | ☐ |
| [ChainLED](https://github.com/yutacar/chain-led-simulation)<br>`m5-chainled` 1.0.0-3 | Creative & Office, Hardware & IoT |  |  | ☐ | ☐ | ☐ | ☐ |
| [CyberCam](https://github.com/cybern0mads/CyberCam)<br>`m5-cybercam` 0.5.3 | Creative & Office, Media | runs in the background, camera, microphone |  | ☐ | ☐ | ☐ | ☐ |
| [DX Zero](https://github.com/adrianomoraes/CardputerZero-DXZero_release)<br>`m5-dx-zero` 0.3.0-m5stack1 | Creative & Office |  | Install script tries to restart M5's launcher; harmless here | ☐ | ☐ | ☐ | ☐ |
| [Email](https://github.com/H4W9/CardputerZero_Mail)<br>`m5-czmail` 0.3.4 | Creative & Office | microphone | adds you to groups; Tk code is only its desktop preview | ☐ | ☐ | ☐ | ☐ |
| [FXCam](https://github.com/imliubo/fxcam-cp0)<br>`m5-fxcam` 1.0.2-m5stack1 | Creative & Office, Media | camera |  | ☐ | ☐ | ☐ | ☐ |
| MusicTagger<br>`m5-musictagger` 0.2.1 | Creative & Office, Media |  |  | ☐ | ☐ | ☐ | ☐ |
| Zero DAW<br>`m5-zerodaw` 1.2.7-1 | Creative & Office, Media | needs extra hardware, microphone, monitor | uses /dev/ttyN | ☐ | ☐ | ☐ | ☐ |
| [ZeroChord](https://github.com/momo20261121/ZeroChord)<br>`m5-zerochord` 1.2.1 | Creative & Office, Media |  |  | ☐ | ☐ | ☐ | ☐ |
| [ZeroTracker](https://github.com/andreahaku/zerotracker)<br>`m5-zerotracker` 0.1.0-rev1 | Creative & Office, Media | microphone |  | ☐ | ☐ | ☐ | ☐ |
| [ZSynth](https://github.com/calanjoyce/zsynth)<br>`m5-zsynth` 1.0.0 | Creative & Office, Media | microphone |  | ☐ | ☐ | ☐ | ☐ |
| herdr-zero<br>`m5-herdr-zero` 0.2.0-rev1 | Development, AI |  | Ties itself to M5's launcher only when that is running | ☐ | ☐ | ☐ | ☐ |
| [Kesha Code LLM](https://github.com/EduardoMoraton/kesha-llm)<br>`m5-kesha-llm` 1.4.0 | Development, AI | needs extra hardware, camera, microphone, motion sensor |  | ☐ | ☐ | ☐ | ☐ |
| [ZeroSky](https://github.com/andreahaku/zerosky)<br>`m5-zerosky` 1.0.0 | Education | needs extra hardware, camera, motion sensor |  | ☐ | ☐ | ☐ | ☐ |
| [ZeroTag](https://github.com/sukoshi1507/zerotag)<br>`m5-zerotag` 1.0.0-rev1 | Emulators, Hardware & IoT | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [AI Wars](https://github.com/randombits0110/cardputer-zero-ai-warz)<br>`m5-ai-wars` 1.0.0-rev3 | Games |  |  | ☐ | ☐ | ☐ | ☐ |
| [AstroBox](https://github.com/kur1k0/AstroBox)<br>`m5-cardputerzero-astro-box` 1.0.1 | Games, Education | camera |  | ☐ | ☐ | ☐ | ☐ |
| [Battleship](https://github.com/yutacar/Battleship)<br>`m5-battleship` 1.0.0-2 | Games |  |  | ☐ | ☐ | ☐ | ☐ |
| Coinflip<br>`m5-coinflip` 1.0.0-rev1 | Games |  |  | ☐ | ☐ | ☐ | ☐ |
| Craps<br>`m5-craps` 1.0.1 | Games, Lifestyle | monitor | uses /dev/ttyN | ☐ | ☐ | ☐ | ☐ |
| Crock O'Gold Slots<br>`m5-crockogoldslots` 1.0.10-1 | Games, Other | needs extra hardware, monitor | uses /dev/ttyN | ☐ | ☐ | ☐ | ☐ |
| Doody Calls - Turd World War<br>`m5-doodycalls` 2.0.11 | Games, Other | monitor | uses /dev/ttyN | ☐ | ☐ | ☐ | ☐ |
| Dweebs™ Pets<br>`m5-dweebs-pets` 2.1.0-1 | Games, Other | needs extra hardware, monitor | uses /dev/ttyN | ☐ | ☐ | ☐ | ☐ |
| [Five](https://github.com/xu-c/Five)<br>`m5-five` 1.2.1-4 | Games |  | Tk code is only its desktop preview | ☐ | ☐ | ☐ | ☐ |
| [Flight Club](https://github.com/journey-ad/M5CardputerZero-FlightClub)<br>`m5-flight-club` 0.42.3 | Games | motion sensor |  | ☐ | ☐ | ☐ | ☐ |
| [Frotz Zero](https://github.com/Ahazii/frotz-zero)<br>`m5-frotz-zero` 0.8.1-1 | Games, Emulators |  |  | ☐ | ☐ | ☐ | ☐ |
| [Ghost Hunter](https://github.com/HarryYGit/zgame-ghost-hunter)<br>`m5-ghost-hunter` 2.1.0 | Games, Radio & Comms | needs extra hardware, motion sensor | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| [NS-Con](https://github.com/RiviaRammer/CardputerZeroApps)<br>`m5-ns-con` 0.2.6 | Games, Emulators |  |  | ☐ | ☐ | ☐ | ☐ |
| Pixel Aces<br>`m5-pixelaces` 0.3.0 | Games |  |  | ☐ | ☐ | ☐ | ☐ |
| REFRACT Pocket<br>`m5-refract-pocket` 6.5 | Games, Emulators | needs extra hardware, monitor | uses /dev/ttyN | ☐ | ☐ | ☐ | ☐ |
| [Retro-Zero](https://github.com/geo-tp/Retro-Zero)<br>`m5-retro-zero` 0.1.0-3 | Games, Emulators | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [Rubiks 2x2x2](https://github.com/SlendyGamer/rubiks2x2x2-autosolver-M5StackCardputerZero.git)<br>`m5-rubiks2x2x2-autosolver` 0.2.1-rev1 | Games, Education |  |  | ☐ | ☐ | ☐ | ☐ |
| Semaphore旗语<br>`m5-flagpi` 0.2-2 | Games, Education |  | opens /dev/input/eventN by number (may pick the wrong device) | ☐ | ☐ | ☐ | ☐ |
| [StickZero](https://github.com/TacoDelPaco/stickzero)<br>`m5-stickzero` 0.1.13 | Games |  |  | ☐ | ☐ | ☐ | ☐ |
| [Sudoku](https://github.com/dbrhspyjkf-code/sudokuzero)<br>`m5-sudokuzero` 1.0.0 | Games |  |  | ☐ | ☐ | ☐ | ☐ |
| Superduper<br>`m5-cardputerzero-superduper` 1.1.0-10 | Games |  |  | ☐ | ☐ | ☐ | ☐ |
| [taM5CZTyping](https://github.com/yuk1-kondo/key-dash)<br>`m5-key-dash` 0.4.2-rev1 | Games, Education |  |  | ☐ | ☐ | ☐ | ☐ |
| [TIC-80](https://github.com/andreahaku/tic80-c0)<br>`m5-tic80-c0` 1.3.1-rev1 | Games, Development |  |  | ☐ | ☐ | ☐ | ☐ |
| Wasteland<br>`m5-wasteland` 0.1.1 | Games |  |  | ☐ | ☐ | ☐ | ☐ |
| [XIANG](https://github.com/xu-c/XIANG)<br>`m5-xiang` 1.0.3-1 | Games |  | Tk code is only its desktop preview | ☐ | ☐ | ☐ | ☐ |
| [Ziiro](https://github.com/marcinsz89/ziiro)<br>`m5-ziiro` 1.1.0-1 | Games | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [仙剑1· Zero](https://github.com/n1zjqo/pal1-zero-source)<br>`m5-xianjian1-zero` 1.0 | Games |  |  | ☐ | ☐ | ☐ | ☐ |
| [CardMesh](https://github.com/maxhayim/CardMesh)<br>`m5-cardmesh` 0.3.1 | Hardware & IoT, Radio & Comms | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [Cardputer Zero Geiger Counter](https://github.com/Kasiin/Cardputer-Zero-Geiger-Counter)<br>`m5-cardputer-zero-geiger` 0.2.1-1 | Hardware & IoT | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [EDGECAM](https://github.com/Apeadv/Edgecam-For-Cardputer-Zero)<br>`m5-edgecam` 1.0.0 | Hardware & IoT | camera |  | ☐ | ☐ | ☐ | ☐ |
| [FlipCTL](https://github.com/whywilson/CardputerZeroFlipCtrl)<br>`m5-flipper-one-remote` 0.1.0 | Hardware & IoT, System Tools | needs extra hardware | Shares a lock file name with M5's launcher; harmless here | ☐ | ☐ | ☐ | ☐ |
| Hass Zero<br>`m5-hass-zero` 1.0.2 | Hardware & IoT, Lifestyle | camera | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| [IGN GPS](https://github.com/baltamir1978/ZeroGPS)<br>`m5-zerogps` 0.5.0-rev1 | Hardware & IoT, Lifestyle | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [KlipperDeckZero](https://github.com/denchi-45/KlipperDeckZero)<br>`m5-klipper-deck-zero` 0.1.0-rev1 | Hardware & IoT |  |  | ☐ | ☐ | ☐ | ☐ |
| [Launcher ESP Flasher](https://github.com/bmorcelli/LauncherESPFlasher)<br>`m5-launcherespflasher` 0.0.6-m5stack1 | Hardware & IoT, System Tools | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [LoRa Scanner](https://github.com/duanesweazy1/pocket-lorascan)<br>`m5-pocket-lorascan` 1.1.1 | Hardware & IoT, Radio & Comms | needs extra hardware | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| [OpenCartaZero](https://github.com/stefexec/OpenCartaZero)<br>`m5-opencartazero` 1.1.0 | Hardware & IoT |  | opens /dev/input/eventN by number (may pick the wrong device) | ☐ | ☐ | ☐ | ☐ |
| [RFID](https://github.com/mtoolstec/CardputerZero-RFID)<br>`m5-rfid` 1.0.4 | Hardware & IoT, Security | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [SubGHz](https://github.com/bfayers/CardputerZero-SubGHz)<br>`m5-subghz` 0.2.0-rev1 | Hardware & IoT, Radio & Comms | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [Trail Recorder](https://github.com/duanesweazy1/pocket-trail)<br>`m5-pocket-trail` 1.0.4 | Hardware & IoT, Lifestyle | needs extra hardware | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| [Ultimate Remote](https://github.com/geo-tp/Ultimate-Remote)<br>`m5-ultimate-remote` 1.3 | Hardware & IoT, Other | needs extra hardware | opens /dev/input/eventN by number (may pick the wrong device) | ☐ | ☐ | ☐ | ☐ |
| [Zero Cellular](https://github.com/hexbyte42-bot/zero-cellular/)<br>`m5-zero-cellular` 1.0.0-1 | Hardware & IoT, Radio & Comms | runs in the background, needs extra hardware, microphone |  | ☐ | ☐ | ☐ | ☐ |
| [ZeroSub](https://github.com/csaboka21/ZeroSub)<br>`m5-zerosub` 0.18.18-rev1 | Hardware & IoT, Radio & Comms | needs extra hardware, motion sensor |  | ☐ | ☐ | ☐ | ☐ |
| [ZeroTVBGone](https://github.com/baltamir1978/ZeroTVBGone)<br>`m5-zerotvbgone` 0.1.0-rev1 | Hardware & IoT |  |  | ☐ | ☐ | ☐ | ☐ |
| [czweather](https://github.com/users/zjg622/projects/1)<br>`m5-czweather` 1.2.1 | Lifestyle, Other |  | Install script tries to restart M5's launcher; harmless here | ☐ | ☐ | ☐ | ☐ |
| Sparrow<br>`m5-sparrow` 1.2.0-rev1 | Lifestyle, Education | microphone, motion sensor |  | ☐ | ☐ | ☐ | ☐ |
| [Stocks](https://github.com/ChrisOr-Dev/zero-stocks)<br>`m5-zero-stocks` 1.0.2 | Lifestyle |  |  | ☐ | ☐ | ☐ | ☐ |
| [Weather](https://github.com/ChrisOr-Dev/zero-weather)<br>`m5-zero-weather` 1.1.0 | Lifestyle | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [AuraCam](https://github.com/jrdntnnr/AuraCam)<br>`m5-auracam` 0.3.1 | Media, Hardware & IoT | needs extra hardware, camera |  | ☐ | ☐ | ☐ | ☐ |
| [CardBili](https://github.com/journey-ad/M5CardputerZero-CardBili)<br>`m5-cardbili` 0.2.4 | Media |  |  | ☐ | ☐ | ☐ | ☐ |
| [Cardplayer](https://github.com/Biersteker/cardplayer)<br>`m5-cardputer-player` 0.1.0 | Media | runs in the background, needs extra hardware | opens /dev/input/eventN by number (may pick the wrong device); Wayland code is an option; its launcher entry uses --framebuffer | ☐ | ☐ | ☐ | ☐ |
| Cardputer TV<br>`m5-cardputer-zero-iptv` 0.3.7-m5stack1 | Media |  |  | ☐ | ☐ | ☐ | ☐ |
| CardTube<br>`m5-cardtube` 0.4.5-rev1 | Media, Lifestyle | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| CZ cam<br>`m5-czcam` 1.4.0 | Media, Lifestyle | camera | Install script tries to restart M5's launcher; harmless here. ZeroOS's Camera app does the same job | ☐ | ☐ | ☐ | ☐ |
| [Frame](https://github.com/CardputerZero/frame)<br>`m5-frame` 0.3.0-1 | Media |  |  | ☐ | ☐ | ☐ | ☐ |
| [LoFiBox](https://github.com/vicliu624/LoFiBox-Zero)<br>`m5-lofibox-cardputer-zero` 0.3.9-1 | Media |  |  | ☐ | ☐ | ☐ | ☐ |
| [Mod Player](https://github.com/MrJoshMurray/mod-player)<br>`m5-mod-player` 0.2.2-rev1 | Media |  |  | ☐ | ☐ | ☐ | ☐ |
| [News](https://github.com/ChrisOr-Dev/zero-news)<br>`m5-zero-news` 1.0.2 | Media |  |  | ☐ | ☐ | ☐ | ☐ |
| [Strawberry Video Player](https://github.com/dave-taylor-7654/strawberry-video-player)<br>`m5-strawberry-video-player` 2.7.15 | Media, System Tools | monitor |  | ☐ | ☐ | ☐ | ☐ |
| Tyrell Video<br>`m5-tyrell-video` 0.1.7-1 | Media | runs in the background, monitor | uses /dev/ttyN; adds you to groups | ☐ | ☐ | ☐ | ☐ |
| [Zero Music](https://github.com/YuHaibo/ZeroMusic)<br>`m5-zero-music` 1.0.0-rev1 | Media |  |  | ☐ | ☐ | ☐ | ☐ |
| ZeroAmp<br>`m5-zeroamp` 1.8.6 | Media |  |  | ☐ | ☐ | ☐ | ☐ |
| [ZeroGallery](https://github.com/baltamir1978/ZeroGallery)<br>`m5-zerogallery` 0.3.0-rev1 | Media | motion sensor |  | ☐ | ☐ | ☐ | ☐ |
| [ZeroMusic163](https://github.com/tongfei11/ZeroMusic163)<br>`m5-zero-music163` 0.1.1 | Media, Lifestyle |  |  | ☐ | ☐ | ☐ | ☐ |
| [zread](https://github.com/JazzplayerL9/zread)<br>`m5-zread` 0.1.0-1 | Media, Other |  | opens in Terminal | ☐ | ☐ | ☐ | ☐ |
| [AirSIM Cardputer](https://github.com/kai-wu-cortex/airsim-cardputer)<br>`m5-airsim-cardputer` 0.3.13-1 | Radio & Comms | needs extra hardware, microphone |  | ☐ | ☐ | ☐ | ☐ |
| [amiibo_sim](https://github.com/RiviaRammer/CardputerZeroApps)<br>`m5-amiibo-sim` 0.2.1 | Radio & Comms, Games | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [APTool](https://github.com/FPVZaphod/APTool)<br>`m5-aptool` 1.0.0-4 | Radio & Comms, System Tools |  | adds you to groups; Install script tries to restart M5's launcher; harmless here | ☐ | ☐ | ☐ | ☐ |
| [Camillia for Meshtastic](https://github.com/oumike/camillia-mt)<br>`m5-camillia` 6.10.1 | Radio & Comms | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [CardputerSDR](https://github.com/RiviaRammer/CardputerZeroApps)<br>`m5-cardputersdr` 0.1.3 | Radio & Comms | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [CWTrainerZero](https://github.com/gregweir/CWTrainerZero)<br>`m5-cwtrainerzero` 0.3.0-1 | Radio & Comms, Education |  |  | ☐ | ☐ | ☐ | ☐ |
| [ESP32-C5 Spectrum](https://github.com/jamesjulier/esp32c5-spectrum-cardputer)<br>`m5-esp32c5-spectrum` 0.1.0 | Radio & Comms, Hardware & IoT | needs extra hardware | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| flightZero<br>`m5-flightzero` 0.2.0 | Radio & Comms, Other |  |  | ☐ | ☐ | ☐ | ☐ |
| [HamLog](https://github.com/stefexec/CardputerHamLog)<br>`m5-hamlog` 0.1.1 | Radio & Comms |  | opens /dev/input/eventN by number (may pick the wrong device) | ☐ | ☐ | ☐ | ☐ |
| [ICOMzero](https://github.com/unagisan69/ICOMzero)<br>`m5-icomzero` 0.18.0 | Radio & Comms | needs extra hardware | Tk code is only its desktop preview | ☐ | ☐ | ☐ | ☐ |
| [LORA Tunnel](https://github.com/valleytechsolutions/LORA-Tunnel)<br>`m5-lora-tunnel` 1.2.0 | Radio & Comms, Security | needs extra hardware | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| [M-Pocket](https://github.com/DavisW9/m-pocket)<br>`m5-m-pocket` 0.2.1 | Radio & Comms, Media |  |  | ☐ | ☐ | ☐ | ☐ |
| [MatrixZero](https://github.com/Z3R0C1PH3R/MatrixZero)<br>`m5-matrixzero` 1.0 | Radio & Comms, Lifestyle | camera, microphone | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| [MeshCore](https://github.com/unagisan69/meshcore-zero)<br>`m5-meshcore-zero` 0.1.32 | Radio & Comms, Hardware & IoT | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| MeshTandem<br>`m5-meshtandem` 1 | Radio & Comms | needs extra hardware | Runs its own meshtasticd: don't use it alongside ZeroOS's Mesh app | ☐ | ☐ | ☐ | ☐ |
| [MeshTerm](https://github.com/jpmartineau/MeshTerm)<br>`m5-meshterm` 1.0.0-1 | Radio & Comms | needs extra hardware | Tk code is only its desktop emulator | ☐ | ☐ | ☐ | ☐ |
| [MeshZero](https://github.com/s0liton/MeshZero)<br>`m5-meshzero` 0.3.0 | Radio & Comms | runs in the background, needs extra hardware, camera, motion sensor | adds you to groups; Runs its own meshtasticd: don't use it alongside ZeroOS's Mesh app | ☐ | ☐ | ☐ | ☐ |
| [Monster for Cyberspace](https://github.com/oumike/monster-cyberspace)<br>`m5-monster-cyberspace` 3.1.1 | Radio & Comms |  |  | ☐ | ☐ | ☐ | ☐ |
| [NFCQSL](https://github.com/RiviaRammer/CardputerZeroApps)<br>`m5-nfcqsl` 0.2.0 | Radio & Comms | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| Plai<br>`m5-plai` 3.0.1-1 | Radio & Comms | needs extra hardware, motion sensor |  | ☐ | ☐ | ☐ | ☐ |
| [Quartermaster](https://github.com/valleytechsolutions/Quartermaster)<br>`m5-quartermaster` 1.2.5 | Radio & Comms, Security | runs in the background, needs extra hardware | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| [RF Heatmap](https://github.com/igenexxx/rf-heatmap)<br>`m5-rf-heatmap` 0.3.9-1 | Radio & Comms, Hardware & IoT | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [RTL SDR](https://github.com/MrJoshMurray/rtl-sdr-receiver)<br>`m5-rtl-sdr-receiver` 0.1.5 | Radio & Comms | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [Sat Tracker](https://github.com/duanesweazy1/pocket-sattrack)<br>`m5-pocket-sattrack` 1.0.3 | Radio & Comms, Education | needs extra hardware | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| [Scheff EchoLink](https://github.com/oumike/scheff-echolink)<br>`m5-scheff-echolink` 3.1.0 | Radio & Comms | microphone |  | ☐ | ☐ | ☐ | ☐ |
| [Scheff Zello](https://github.com/oumike/scheff-zello)<br>`m5-scheff-zello` 3.1.0 | Radio & Comms | microphone |  | ☐ | ☐ | ☐ | ☐ |
| [SondeChase](https://github.com/hieuha/CardputerZero-Sondechase)<br>`m5-sondechase` 0.8.0-rev1 | Radio & Comms, Hardware & IoT | needs extra hardware, motion sensor | opens /dev/input/eventN by number (may pick the wrong device) | ☐ | ☐ | ☐ | ☐ |
| [Square SDR](https://github.com/MrJoshMurray/square-sdr)<br>`m5-square-sdr` 0.3.1 | Radio & Comms | needs extra hardware, microphone |  | ☐ | ☐ | ☐ | ☐ |
| [Telega](https://github.com/d4rkmen/telega)<br>`m5-telega` 0.3.0-1 | Radio & Comms, Lifestyle | microphone |  | ☐ | ☐ | ☐ | ☐ |
| [Universal IR](https://github.com/RealOuO/czirremote)<br>`m5-cp0-ir-remote` 1.1.2 | Radio & Comms, Hardware & IoT |  | opens /dev/input/eventN by number (may pick the wrong device) | ☐ | ☐ | ☐ | ☐ |
| [WebRadio](https://github.com/xu-c/WebRadio)<br>`m5-cardputer-zero-radio` 1.0.13-1 | Radio & Comms, Media |  | Tk code is only its desktop preview | ☐ | ☐ | ☐ | ☐ |
| [ZeroRadio](https://github.com/andreahaku/zeroradio)<br>`m5-zeroradio` 1.2.0 | Radio & Comms | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [ZeroWave](https://github.com/stefexec/ZeroWave)<br>`m5-zerowave` 1.0.0 | Radio & Comms, Hardware & IoT | needs extra hardware | opens /dev/input/eventN by number (may pick the wrong device) | ☐ | ☐ | ☐ | ☐ |
| [Bruce CardputerZero](https://github.com/BruceDevices/CardputerZero)<br>`m5-brucecardputerzero` 0.2.0-bruce1 | Security, System Tools | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [Loki](https://github.com/brainphreak/loki-recon)<br>`m5-loki` 1.0.0 | Security | runs in the background, needs extra hardware | Install script tries to restart M5's launcher; harmless here | ☐ | ☐ | ☐ | ☐ |
| [LokiCZ](https://github.com/justincredibles/Loki)<br>`m5-lokicz` 0.43 | Security, Radio & Comms | needs extra hardware | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| [Pwnagotchi](https://github.com/JdaieLin/cardputer-pwnagotchi)<br>`m5-cardputerzero-pwnagotchi` 0.1.2-m5stack1 | Security, Radio & Comms | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| Wireless Toolbox<br>`m5-wanwu-wireless` 1.1.0 | Security, Radio & Comms | runs in the background, needs extra hardware | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| [Zerogotchi](https://github.com/unagisan69/Zerogotchi)<br>`m5-zerogotchi` 0.4.26 | Security, Radio & Comms | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [AdvanceWebUI](https://github.com/bomberman30/AdvanceWebUI-for-Cardputer-ZERO)<br>`m5-advancewebui` 1.1.0 | System Tools, Creative & Office |  |  | ☐ | ☐ | ☐ | ☐ |
| [Airfiles](https://github.com/JC3GDEV/Airfiles)<br>`m5-airfiles` 0.4.0 | System Tools, Radio & Comms | runs in the background |  | ☐ | ☐ | ☐ | ☐ |
| Aria2 Remote<br>`m5-aria2-remote` 0.1.0-rev2 | System Tools |  |  | ☐ | ☐ | ☐ | ☐ |
| [Beamhound](https://github.com/bfeller/beamhound)<br>`m5-beamhound` 0.1.0 | System Tools, Hardware & IoT |  | opens /dev/input/eventN by number (may pick the wrong device) | ☐ | ☐ | ☐ | ☐ |
| [Card HY2](https://github.com/Moeyuuko/card-hy2)<br>`m5-card-hy2` 0.0.3 | System Tools | runs in the background, camera | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| [CardFlash](https://gitlab.com/ScarletPachyderm/cardflash)<br>`m5-cardflash` 0.1.4-1 | System Tools |  |  | ☐ | ☐ | ☐ | ☐ |
| CardRouter<br>`m5-cardrouter` 0.1.0 | System Tools, Radio & Comms |  |  | ☐ | ☐ | ☐ | ☐ |
| Controller<br>`m5-controller` 0.1.0-1 | System Tools, Games |  | opens /dev/input/eventN by number (may pick the wrong device) | ☐ | ☐ | ☐ | ☐ |
| CZProxy<br>`m5-czproxy` 0.3.1 | System Tools | runs in the background | Shares a lock file name with M5's launcher; harmless here | ☐ | ☐ | ☐ | ☐ |
| Desktop<br>`m5-wanwu-desktop` 1.0.1 | System Tools |  | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| [Ethernet Tester](https://github.com/LBreda/cp0-ethernet-tester)<br>`m5-ethernet-tester` 1.0.0-rev1 | System Tools, Hardware & IoT |  |  | ☐ | ☐ | ☐ | ☐ |
| [FontPreview](https://github.com/Harrison-Xu/FontPreview)<br>`m5-fontpreview` 0.4.7-1 | System Tools |  |  | ☐ | ☐ | ☐ | ☐ |
| [Ipdoit](https://github.com/Sudrien/ipdoit-for-cardputer-zero)<br>`m5-ipdoit-for-cardputer-zero` 0.2.9 | System Tools | runs in the background |  | ☐ | ☐ | ☐ | ☐ |
| [Jiggler](https://github.com/intGus/jiggler)<br>`m5-jiggler` 0.1.0-m5stack1 | System Tools, Hardware & IoT | runs in the background, needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [KanSTL](https://github.com/nullbeing/KanSTL)<br>`m5-kanstl` 0.1.0 | System Tools, Development |  |  | ☐ | ☐ | ☐ | ☐ |
| [kvxputerzero](https://github.com/kvx17/kvxputer)<br>`m5-kvxputerzero` 0.1.0-rev10 | System Tools, Security |  |  | ☐ | ☐ | ☐ | ☐ |
| [Lookout](https://github.com/bfeller/beamhound)<br>`m5-lookout` 0.1.0 | System Tools, Media | camera, microphone | opens /dev/input/eventN by number (may pick the wrong device) | ☐ | ☐ | ☐ | ☐ |
| [Mountable](https://github.com/s0liton/Mountable)<br>`m5-mountable` 0.1.0 | System Tools, Media | runs in the background |  | ☐ | ☐ | ☐ | ☐ |
| [Mouse Jiggler](https://github.com/kkazakov/cardputer-zero-mouse-emu)<br>`m5-mouse-jiggler` 0.2.7 | System Tools |  |  | ☐ | ☐ | ☐ | ☐ |
| NET Toolkit<br>`m5-nettoolkit` 0.4.70 | System Tools |  |  | ☐ | ☐ | ☐ | ☐ |
| Octet Desktop<br>`m5-octet` 5.3 | System Tools, Creative & Office | needs extra hardware, camera, monitor | uses /dev/ttyN | ☐ | ☐ | ☐ | ☐ |
| [Peers](https://github.com/InstantComet/cz-peers)<br>`m5-cz-peers` 1.2.1 | System Tools |  | opens in Terminal | ☐ | ☐ | ☐ | ☐ |
| [r86](https://github.com/tcchen2026/r86)<br>`m5-r86` 2026.11.18+gita807f0 | System Tools, Emulators | monitor |  | ☐ | ☐ | ☐ | ☐ |
| [SysMon](https://github.com/duanesweazy1/pocket-sysmon)<br>`m5-pocket-sysmon` 1.1.5 | System Tools | microphone, motion sensor | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| [Tailputer Zero](https://github.com/ashpas/tailputer-zero)<br>`m5-tailputerzero` 1.0.0-rev1 | System Tools, Radio & Comms |  |  | ☐ | ☐ | ☐ | ☐ |
| [TailZero](https://github.com/fa1247/TailZero)<br>`m5-tailzero` 0.2.0-1 | System Tools, Security |  | Tailscale, like ZeroOS's Remote app: use one or the other | ☐ | ☐ | ☐ | ☐ |
| [Tools](https://github.com/pocket-nexus/pocket-zero-tools)<br>`m5-pocket-zero-tools` 0.2.1-1 | System Tools, Hardware & IoT | camera, microphone, motion sensor |  | ☐ | ☐ | ☐ | ☐ |
| [Ultimate Flasher](https://github.com/C5Lab/Ultimate-Flasher)<br>`m5-ultimate-flasher-cardputerzero` 0.3.0 | System Tools, Hardware & IoT | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [Updoit](https://github.com/Sudrien/updoit-for-cardputer-zero)<br>`m5-updoit-for-cardputer-zero` 0.2.8 | System Tools | runs in the background | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| USB Screen<br>`m5-wanwu-usbscreen` 1.0.1 | System Tools | runs in the background, needs extra hardware | adds you to groups | ☐ | ☐ | ☐ | ☐ |
| [Wallpaper](https://github.com/RiviaRammer/CardputerZeroApps)<br>`m5-wallpaper` 0.4.1 | System Tools |  |  | ☐ | ☐ | ☐ | ☐ |
| [WiFi USB](https://github.com/adrianomoraes/CardputerZero-WiFiManager)<br>`m5-wifi-manager` 0.1.1-m5stack1 | System Tools |  |  | ☐ | ☐ | ☐ | ☐ |
| [WireZero](https://github.com/r-morato/wirezero)<br>`m5-wirezero` 0.4.1-1 | System Tools, Security | camera |  | ☐ | ☐ | ☐ | ☐ |
| [Zero SSH](https://github.com/tongfei11/ZeroSSH)<br>`m5-zero-ssh` 0.1.1 | System Tools, Development | runs in the background |  | ☐ | ☐ | ☐ | ☐ |
| [Zero Watt](https://github.com/hhzzzyy/zero-watt)<br>`m5-zero-watt` 0.1.0-rev1 | System Tools | runs in the background |  | ☐ | ☐ | ☐ | ☐ |
| [ZeroDesk](https://github.com/r-morato/zerodesk)<br>`m5-zerodesk` 0.1.9-1 | System Tools, Development |  |  | ☐ | ☐ | ☐ | ☐ |
| [ZeroSDR](https://github.com/nullbeing/zerosdr)<br>`m5-zerosdr` 0.5.0 | System Tools, Radio & Comms | needs extra hardware |  | ☐ | ☐ | ☐ | ☐ |
| [ZeroSend](https://github.com/InstantComet/zerosend)<br>`m5-zerosend` 1.0.0 | System Tools |  |  | ☐ | ☐ | ☐ | ☐ |

## Nothing to open

No launcher entry, so nothing appears on the home screen.

None.
