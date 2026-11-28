# Sync

Sync keeps **Notes**, **Documents** and **Recordings** the same on the
Cardputer Zero and your other computers and phone. Write a note on the train,
and it's on your PC when you get home; drop a PDF in Documents on the PC, and
it's on the Cardputer. It uses [Syncthing](https://syncthing.net): free,
open source, no account or cloud. Your devices copy files directly to each
other, encrypted, over Wi-Fi at home or over the internet (Tailscale works
too, but isn't needed).

## First time

1. Install Syncthing on your PC ([syncthing.net/downloads](https://syncthing.net/downloads/)),
   or on an Android phone (Syncthing-Fork) or iPhone (Möbius Sync).
2. On the Cardputer, open **Sync** and choose **Show my device ID**.
3. In Syncthing on the PC, choose **Add Remote Device** and enter that ID (on
   the same Wi-Fi, the PC usually lists the Cardputer under "nearby devices",
   so you can just click it). Phones can scan the QR code.
4. The Cardputer shows "*PC* asks". Press **Enter** to accept. It shares
   Notes, Documents and Recordings with the PC.
5. On the PC, accept the three folders when Syncthing offers them and pick
   where they go.

Or go the other way: **Add device by ID** on the Cardputer and type the PC's
ID (Syncthing on the PC: Actions > Show ID), then accept the Cardputer on the PC.

## Using it

In **Sync**:

| | |
| --- | --- |
| Top left | Up to date, Syncing *n*%, Waiting for devices, or Problem |
| *device* asks / *device*: *folder* | Enter accepts, Del ignores. A folder someone shares with you goes into `~/Sync/<name>`. |
| A folder | Enter looks for changes now (it also notices changes by itself) |
| A device | Enter shows whether it is online and the address it is connected at. **Tab > P** sets an address for it (below); **Tab > D** (or Del) stops syncing with it, after asking |
| Sync now | Runs until everything is up to date, then stops |
| Show my device ID | QR code and ID, to add this Cardputer on another device |
| Runs: ... | When Sync runs (below) |

The status bar shows the sync arrows while Syncthing is running.

Notes now saves to `~/Notes/notes.txt` (older images used `~/notes.txt`; it
moves over the first time). Voice notes are already in `~/Recordings`. Put
anything else you want everywhere in `~/Documents`.

A file deleted or overwritten by another device is kept for 14 days in the
folder's `.stversions`, in case you need it back.

## When it runs

Settings → **Sync** (also in the Sync app):

- **While charging** (default): runs when the Cardputer is on USB power and
  has a network, so it catches up every time you plug it in without costing
  battery.
- **Always on**: runs all the time; changes arrive within seconds, but
  Syncthing keeps the Wi-Fi and CPU a little busier **(measure)**.
- **When asked**: only with **Sync now** or while the Sync app is open.

In every mode it also runs while the Sync app is open, and **Sync now**
works anywhere: it runs until all your online devices are up to date, then
stops (or gives up after 3 minutes if none of them is online, or 10 minutes
in all). It stops a minute after it's no longer wanted.

## Where is the other computer?

You don't normally set an address. Devices find each other by ID: on the same
Wi-Fi by Syncthing's local discovery, elsewhere through its discovery and
relay servers. Enter on a device in **Sync** shows where it is right now
(for example `online at 192.168.1.50:22000`).

If a device stays "off" (a router that keeps devices apart, another VLAN or
subnet, a VPN), tell the Cardputer where it is: select the device, **Tab > P**
(Set address), and type its IP or name, such as `192.168.1.50` or
`mypc.local`, with `:port` if the PC doesn't use Syncthing's usual 22000.
The Cardputer tries that address first and still falls back to discovery.
Leave it blank to go back to finding it by itself. The PC needs a steady
address for this: give it a reservation in your router, or use its name.

## Back up to a USB drive

Sync can also copy Notes, Documents and Recordings to a USB stick or drive,
and copy them back. This needs no Syncthing, no network and no account.

1. Set the board's side switch to **Host** and plug the drive into the USB-C
   port (through an OTG adapter or a USB-C stick). It mounts by itself under
   `/media/<label>`. FAT, exFAT, NTFS and ext4 drives work.
2. In **Sync**, choose **Back up to USB** (or **Tab > B**) and confirm. The
   progress shows on the left and a toast says how many files were copied.

The backup is a plain folder, `CardputerBackup/Notes`, `/Documents` and
`/Recordings` on the drive, so any computer can open it. A second backup only
copies what changed. It never deletes anything from the drive, so files you
deleted on the Cardputer stay in the backup until you remove them yourself.
Syncthing's `.stversions` folders and temporary files aren't copied.

**Restore from USB** (**Tab > E**) brings the files back: anything missing on
the Cardputer is copied, a file the backup has a newer copy of is replaced,
and a file that is newer on the Cardputer is left alone. A replaced file isn't
lost: the old copy goes to `~/.local/share/zeroos/restore-trash/<date>`. If
Syncthing is running, restored files then sync to your other devices like any
other change.

Good to know: FAT drives can't hold names with `: ? * " < > |`, so a file
like that is counted as "couldn't be copied" and the rest carry on; the drive
needs room for what's new (the app says "Not enough space" if not); pulling
the drive out mid-copy stops it ("The drive was removed"), and running the
backup again carries on. With several drives plugged in, the first one with
room (backup) or with a backup on it (restore) is used.

## How it works

| Piece | What |
| --- | --- |
| `syncthing` package | From Debian. `syncthing@<user>.service` (the UI user, uid 1000: `pi` unless renamed in Raspberry Pi Imager) is never started at boot; ZeroShell starts and stops it (polkit allows that user exactly this unit). |
| `syncthing@.service.d/zeroos.conf` | Small for 512 MB: Go heap limit 48 MB, two cores, low CPU and disk priority, no default folder, no self-upgrade. earlyoom closes it before anything you're using. After start it runs `zeroos-sync setup`. |
| `zeroos-sync` | Python helper that talks to Syncthing's local API (key from Syncthing's own config). `setup` turns off usage and crash reports, syncs one folder at a time, and creates the three folders (`zeroos-notes`, `zeroos-documents`, `zeroos-recordings`) with file watching and 14-day trash-can versioning. Also `status`, `add-device`, `remove-device`, `ignore-device`, `accept-folder`, `ignore-folder`, `rescan`, `address`, and `usb`, `backup MOUNT` and `restore MOUNT` for USB drives (they copy in the background and write progress to `~/.cache/zeroos/usb-job.state`; tested by `tests/usb-backup-test.sh`). Works over ssh too. |
| ZeroShell | Decides when Syncthing runs (`sync.mode`), runs "Sync now", and draws the Sync app from `zeroos-sync status` (every few seconds, only while the app is open). |
| `zeroos-usb-mount` | Part of the base image (`90-zeroos-usb.udev`): udev mounts a plugged-in USB drive under `/media/<label>` with `systemd-mount`, owned by the UI user and `noexec,nosuid,nodev`. Unplugging unmounts it. |

The Syncthing web page stays on the Cardputer only (`127.0.0.1:8384`); in
Dock mode you can open it in the browser for anything the Sync app doesn't
cover (ignore patterns, receive-only folders, more folders).

## Limits and things to measure

- **Memory**: Syncthing should take about 30-50 MB while running **(measure)**.
  It isn't running most of the time on battery.
- **Battery**: "Always on" keeps connections to your devices open; measure
  its cost before making it the default.
- **Big folders**: the first sync of a big Documents folder takes a while on
  the Cardputer's CPU and SD card. Large media is better left out of it.
- **Conflicts**: editing the same note on two devices before they sync keeps
  both copies (`notes.sync-conflict-....txt`); Files shows both.
