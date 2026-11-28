# Updating

ZeroOS updates itself over the network. No SD card re-flash, and it does not
matter how the Cardputer is online: Wi-Fi, the built-in Ethernet port, a
tethered phone or Tailscale. A PC on the same network can also push a build
straight to it.

## On the device

Open **Update** (or **Settings > Software update**). It looks for a newer
version at once. If there is one, **Enter** installs it: download, install,
and the home screen restarts on the new version. A minute or two in all.

- It won't start under 20% battery unless the Cardputer is plugged in.
- Your files, settings, Wi-Fi networks and sign-ins are not touched.
- Nothing checks in the background (that would cost battery). It looks when
  you open the app.

From a terminal or over SSH, the same thing:

```sh
sudo zeroos-update check
sudo zeroos-update install
sudo zeroos-update rollback      # back to the version before the last update
zeroos-update status
```

## It says "Up to date" but a newer release exists

This happens on a Cardputer set up from the project's old home
(`hartj77-blip/cardputer-zero`, versions 0.1.x). It still asks the old address,
where 0.1.1 is the last version. Point it at this repository once:

1. Open the **Terminal** app on the Cardputer, or from a PC on the same
   network run `ssh pi@cardputer.local`.
2. Run this one line (copy it exactly):

   ```sh
   sudo sed -i 's#cardputer-zero#CardPuter-OS.4ME-U#' /etc/zeroos/update.conf
   ```

3. Open **Update** again. It now finds the newest version; press **Enter**
   to install it.

To see which address a device asks, run `cat /etc/zeroos/update.conf`. If you
would rather not type anything, flash the newest image instead (README,
Install); that erases the card.

## It says the package system "needs repair first"

Up to ZeroOS 0.3.2, Update and Apps stopped with this message when an earlier
install had been left half done (a power cut, or an app's extra that would
not set up). From 0.3.3 they run the repair themselves, and Update also waits
for Apps to finish what it is installing instead of failing.

A device on 0.3.2 or older has to be repaired once by hand before it can take
the newer version. From a PC on the same network:

```sh
ssh -t pi@cardputer.local "sudo apt-get -f install -y; sudo dpkg --configure -a"
```

Then open **Update** again. Flashing the newest image (README, Install) also
works, and erases the card.

## From a PC, over Ethernet or Wi-Fi

For trying a build that is not a release yet. The PC and the Cardputer need
to reach each other: both on the same router or switch (Ethernet or Wi-Fi),
or both on Tailscale.

```sh
image/package.sh build/device/zeroshell      # makes build/update/zeroos_<version>_arm64.deb
tools/push-update.sh cardputer.local         # sends the newest package and installs it
```

`push-update.sh` takes any `[user@]host` and, as a second argument, any
package: one from a release, or the `zeroos-package` artifact that CI builds
for every pull request. It copies the package over SSH and runs
`zeroos-update install-file` there, so the same checks and the same rollback
apply.

A Cardputer flashed before updates existed has no `zeroos-update` yet.
`push-update.sh` notices, installs the package directly that once, and from
then on the device can update itself.

**(measure)** An Ethernet cable straight from PC to Cardputer, with no router,
should work through `cardputer.local` (both ends give themselves link-local
addresses). Not tried on the real unit yet.

## Publishing an update

On GitHub, open **Actions**, choose
[Release](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/actions/workflows/release.yml),
press **Run workflow**, type the version (such as `0.4.0`) and press the green button. It
releases the newest commit on `main` and tags it `v0.4.0`.

Pushing a tag does the same, for a commit that is on `main` (a tag on any other branch is
refused, since only `main` has been through a reviewed pull request):

```sh
git tag v0.4.0
git push origin v0.4.0
```

The **Release** workflow builds ZeroShell for arm64, makes the package and
publishes a GitHub release with two files: `zeroos_0.2.0_arm64.deb` and
`zeroos-update.manifest`. Devices look at the newest release, so they see it
the next time someone opens **Update**. The tag is the version.

The SD card image follows: once the release exists, the same workflow builds
the full image (about 10 minutes on the arm64 runner) and attaches it to the release as
`zeroos-cardputerzero-arm64.img.xz` with a `.sha256`, so the README's download
link always gives new users the newest version. To attach an image to a
release by hand (or replace one), run **Build image** from the Actions tab with
that release's tag in *release_tag*.

It first runs all of CI on the tagged commit (simulator, screenshots, browser
build, device build, update tests) and publishes nothing if any of it fails.
A tag with a `-`, such as `v0.3.0-rc1`, is published as a pre-release:
devices only look at the latest full release, so it reaches nobody until you
mark it as the latest release on GitHub (or install it by hand with
`tools/push-update.sh`).

Builds between releases get versions such as `0.2.0+3.gabc1234` (three commits
after v0.2.0), which count as newer than 0.2.0 and older than 0.2.1
(`image/version.sh`).

## How it works

| Part | Job |
| --- | --- |
| `image/stage-zeroos/payload.sh` | The list of every file ZeroOS owns: ZeroShell, its services, scripts and settings. |
| `image/package.sh` | Packs that list into one Debian package, `zeroos`, and writes the manifest (version, file name, SHA-256, size). |
| Image build | Installs the very same package, so a flashed card and an updated one hold the same files. A copy stays on the card as the first version to roll back to. |
| `/usr/bin/zeroos-update` | On the device, as root: reads the manifest, compares versions with `dpkg`, downloads, checks the SHA-256, installs. |
| `zeroos-update.service`, `zeroos-update-check.service` | How ZeroShell (which runs as `pi`) starts the two jobs; a polkit rule allows it. Progress goes to `/run/zeroos/update.status`. |
| `/etc/zeroos/update.conf` | Where to look. `UPDATE_URL` is the GitHub releases of this repository by default. Your edits survive updates. |

**If the new version is broken.** After installing, `zeroos-update` restarts
ZeroShell and watches it for 10 seconds. If it does not stay up, the previous
package is put back, ZeroShell restarts on it and the app says **Update
undone**. The last two packages are kept in `/var/lib/zeroos/update/`
(`current.deb`, `previous.deb`), which is also what `rollback` uses.

**Your own update server.** Any web server that holds the two files from
`build/update/` will do, for a device with no route to GitHub:

```sh
cd build/update && python3 -m http.server 8000
# on the Cardputer, in /etc/zeroos/update.conf:
#   UPDATE_URL=http://192.168.1.20:8000
```

## What an update does not change

An update replaces ZeroOS's own files. It does not touch:

- **Debian packages** (the kernel, drivers, Chromium, Tailscale...). Those
  update the Debian way: `sudo apt update && sudo apt full-upgrade`. The
  kernel is pinned by the image; a new kernel means a new image.
- **Boot settings** (`config.txt`, `cmdline.txt`) and the initramfs.
- A release that needs a new Debian package says so in its notes; until
  then the feature that needs it reports that the tool is missing.

For those, flash a new image: the newest is always at
[releases/latest](https://github.com/hartj77-blip/CardPuter-OS.4ME-U/releases/latest)
(see the README's Install section), or run the **Build image** workflow.

## Trust

The download must match the SHA-256 in the manifest, which catches a
damaged or cut-off download. The manifest comes from the same place as the
package, so the checksum says nothing about *who* made it: whoever controls
the update address, or the network between it and the Cardputer, can serve
their own package, and its install scripts run as root. So:

- The default address is GitHub over HTTPS: you trust whoever can publish
  releases on the repository.
- Plain `http://` is only accepted for your own network: `localhost`,
  private addresses (`10.*`, `172.16-31.*`, `192.168.*`), Tailscale
  (`100.64-127.*`, `*.ts.net`) and `*.local`, `*.lan`, `*.home.arpa` names.
  Anyone on that network could still swap the package. Anything else over
  `http://` is refused unless `/etc/zeroos/update.conf` also says
  `ALLOW_HTTP=1`.
- The manifest must also be signed with ZeroOS's release key (below), and the manifest holds
  the package's SHA-256, so neither the address nor the network matters, only the key.

## Signatures

ZeroOS checks an Ed25519 signature on the update manifest and on each app source's
`zeroos-apps.index`. Every package's SHA-256 is in one of those two lists, so a valid signature
covers the packages too.

- **On the device**, `/usr/lib/zeroos/verify` checks them. `/usr/share/zeroos/keys/signers`
  says which key signs what: `update` for ZeroOS updates, and each of this project's two app
  sources. The keys are the `.pem` files next to it. Your own keys can go in `/etc/zeroos/keys`,
  in the same layout (for example, to sign your own update server's manifest).
- **The release key** (`zeroos-release.pem`) has shipped since ZeroOS 0.8.1, so a manifest or
  index from this project without a valid signature is refused. A `signers` line whose `.pem`
  file isn't there does nothing. Update says
  "The update isn't signed with ZeroOS's key". Apps leaves that source's apps out of its list
  and keeps the others. A source with no key (M5Stack's store, or one you added) is taken as
  before.
- **Signing happens in the release workflows.** The private key is the secret
  `ZEROOS_SIGNING_KEY` of the repository's `release` environment, and `tools/sign-release.sh`
  writes `zeroos-update.manifest.sig` and `zeroos-apps.index.sig`. If ZeroOS ships a key and the
  secret is missing or doesn't match, the release stops rather than publish lists that devices
  would refuse.

Turning it on, once:

1. On your own computer, from this repository: `tools/signing-key.sh ~/zeroos-release-key.pem`.
   It writes the private key there (keep it safe, out of any repository) and the public key to
   `image/stage-zeroos/files/keys/zeroos-release.pem`.
2. Add the private key as the `ZEROOS_SIGNING_KEY` secret of a `release` environment in **both**
   repositories (Settings > Environments, or the `gh secret set` lines in the script). Add
   yourself as a required reviewer there too: then nothing reaches devices until you approve it,
   even if someone gets push access.
3. Publish an Apps release (Actions > Release > Run workflow in the Apps repository), so its
   index is signed before any device starts checking.
4. Commit the public key to `main` and release ZeroOS. Devices check from that version on.

The key is a GitHub secret, so this guards against a changed release file, a stolen token that
can upload files, and a push that skips review (with a required reviewer). Anyone who can read
the environment's secrets can still sign. For more, keep the key offline and sign
`zeroos-update.manifest` by hand with `tools/sign-release.sh` before publishing.

If an update stops part-way (power cut mid-install), the next update first
finishes it with `dpkg --configure -a`. If the new package won't install,
the old one goes back on, and the Update app says which version you're on;
if even that fails it tells you to run `sudo dpkg --configure -a`.

## Not yet tested on hardware

- The whole path on a real Cardputer: download speed, how long ZeroShell is
  away during the restart **(measure)**.
- An update while Dock mode, the Terminal or the camera has the screen.
- The direct Ethernet cable, as above.

`image/tests/update-test.sh` covers the logic on a PC with stand-ins for
`dpkg -i` and `systemctl`: finding an update, refusing a bad checksum or an
odd manifest, installing, rolling back by hand, and rolling back a version
that keeps crashing. CI runs it on every pull request.
