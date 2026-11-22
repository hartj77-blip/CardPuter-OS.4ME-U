# Apps

ZeroOS itself is small: it comes with **Apps**, **Settings**, **Update** and **Wi-Fi**, which are
what a device needs to get online, update itself and install apps. Everything else is an app you
install from the **Apps** app, downloaded from GitHub: Battery, Camera, Car, Clock, Compass, Dock,
Files, GPS, IR Remote, Media, Mesh, Notes, Remote, Sync, System, Terminal and Tether.

Terminal, Files and Dock were built in up to ZeroOS 0.1.x. The release SD card image still comes
with them installed. A device updated from 0.1.x gets them back the first time you open **Apps**
and press **U** (which that update needs anyway, for the new app ABI). Their system parts stay
in ZeroOS: the desktop on the monitor (`zeroos-dock`), which Files, Camera, Mesh and Remote also
use, and the console service behind Terminal. Remote's SSH and mosh need the Terminal app.

Each app brings its own extras with it, so the base image doesn't carry them: Remote brings
Tailscale, Sync brings Syncthing, Camera brings libcamera and M5Stack's camera program, Mesh brings
meshtasticd, GPS brings gpsd. Removing an app takes its extras away again, unless another app still
needs them.

## Using it

Open **Apps** (it's in the dock). It looks for new apps when its list is more than 6 hours old.

| Key | |
| --- | --- |
| Enter | an app's page: **Install**, **Update**, **Open**, **Remove** |
| U | update every app that has a newer version |
| R | look for new apps now |
| S | sources: where apps come from. **D** removes one; **Add a GitHub repo** adds one |

Installing needs a connection (Wi-Fi, Ethernet or a tethered phone). A new app appears on the home
screen as soon as it's installed. After an update to an app you already opened, the screen
restarts once (about a second) to run the new version.

From a terminal:

```sh
zeroos-apps list                 # what's on offer and what's installed
sudo zeroos-apps install media
sudo zeroos-apps remove media
sudo zeroos-apps upgrade         # every app with a newer version
sudo zeroos-apps add-source someone/cardputer-apps
sudo zeroos-apps install-file zeroos-app-media_0.5.0_arm64.deb   # a package you copied over
```

An SD card image can have apps on it from the start: the **Build image** workflow takes a list
(for example `notes media gps`), as does `ZEROOS_APPS="notes media gps" image/build.sh`.

## Where apps come from

A **source** is a GitHub repository whose newest release holds a file called `zeroos-apps.index`
and the packages it lists. ZeroOS starts with one: this repository. Its releases publish the
apps built from the same commit as the OS. Any other repository that
publishes the same files works too: add it in **Apps > S > Add a GitHub repo**, or add a line to
`/etc/zeroos/apps.sources` (an `owner/repo`, or any `https://` address that serves the index).
When two sources offer an app with the same name, the one higher in the list wins.

**Trust.** An app runs with the same access as the home screen, and installing one runs its
package's scripts as root. The SHA-256 in the index only proves the download is complete; the
index comes from the same place. Only add sources you trust.

## How it works

```
Apps app (ZeroShell)  --systemctl start-->  zeroos-apps@install:media.service  (root)
        ^                                         |  1. read the catalog (from every source's index)
        |  /run/zeroos/apps.status                |  2. download zeroos-app-media_<v>_arm64.deb, check SHA-256
        +-----------------------------------------+  3. apt-get install it, with what it depends on
                                                     4. ZeroShell loads /usr/lib/zeroos/apps/media.so
```

- **An app is a module**: a shared library, `/usr/lib/zeroos/apps/<id>.so`, that ZeroShell loads
  at start and after each install. It exports `zs_app_module()` (see `shell/src/ui/app.h`) and uses
  LVGL and the hardware layer from the zeroshell binary, which exports them. ZeroShell loads a
  module only if it was built for the same **app ABI** (`ZS_APP_ABI` in `app.h`;
  `zeroshell --app-abi`). The store only offers matching builds, and the `zeroos` package
  `Provides: zeroos-app-abi-<n>`, which every app package depends on.
- **An app is a Debian package**, `zeroos-app-<id>`: the module, the app's own files (services,
  scripts, settings) and `Depends:` on the Debian packages it needs. Apps that need a Debian
  repository of their own (Remote: Tailscale; Mesh: Meshtastic) carry its list file and key;
  `zeroos-apps` puts those in place before asking apt for the rest.
- **zeroos-apps** (`image/stage-zeroos/files/apps/zeroos-apps`) does the work as root, started
  through `zeroos-apps@<job>.service` (polkit lets the UI user start those). It writes the merged
  catalog to `/var/lib/zeroos/apps/catalog` and progress to `/run/zeroos/apps.status`.

## Making an app

In this repository:

1. Write the app in `shell/src/apps/<id>.cpp` (an `App`, as in [ARCHITECTURE.md](ARCHITECTURE.md),
   with a `make_<id>_app()` factory).
2. List it in `shell/apps.cmake`: `zs_app(<id> "Name" <icon> <colour> make_<id>_app SOURCES src/apps/<id>.cpp)`.
   Leave out `BASE` and it becomes an installable app on the device; the simulator and screenshots
   still build it in.
3. Describe its package in `image/apps/<id>.pkg`: `NAME`, `SUMMARY`, `DEPENDS` (Debian packages),
   units to `ENABLE`, and a `payload()` that copies its files. The other `.pkg` files are examples.

CI builds every module for arm64, loads each one in a headless ZeroShell
(`shell/tools/modules-test.sh`), builds the packages (`image/apps/build.sh`) and tests the installer
(`image/tests/apps-test.sh`). A release publishes the packages and `zeroos-apps.index`.

In another repository: add this repository as a submodule, include
`shell/cmake/ZeroShellApps.cmake`, call `zs_app()` for each app and `zs_apps_build()`, then pack
the modules with `image/apps/make-app-deb.sh`, which prints the app's block for
`zeroos-apps.index`. Publish the packages and the index in a GitHub release.

Apps must be rebuilt when ZeroOS bumps `ZS_APP_ABI` (a change to the headers apps use). Until
they are, ZeroShell skips them and the store doesn't offer them.

## What's not done yet

- Nothing here has run on a Cardputer yet: the installer is tested against stand-ins for apt and
  GitHub (`image/tests/apps-test.sh`), and the modules in a headless ZeroShell on a PC.
- A ZeroOS update doesn't update apps by itself; open **Apps** and press **U**.
- Removing Terminal also removes one-key SSH from Remote (Remote says so); removing Dock leaves
  Dock mode reachable through Settings' "On HDMI" option.
- The store shows an app's real icon only once it's installed (until then, a grid).
- An app's page doesn't show screenshots.
