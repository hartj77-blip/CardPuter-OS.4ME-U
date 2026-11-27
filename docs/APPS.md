# Apps

ZeroOS itself is small: it comes with **Apps**, **Settings** (Wi-Fi and Bluetooth are sections of
it) and **Update**, which are what a device needs to get online, update itself and install apps. Everything else is an app you
install from the **Apps** app, downloaded from the apps repository,
[CardPuter-Apps.4ME-U](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U): Battery, Camera, Car,
Clock, Compass, Dock, Files, GPS, IR Remote, Media, Mesh, Notes, Remote, Sync, System, Terminal,
Tether and AI, the [Dev Tools](DEV.md) pack (Code, REPL, Git and Serial) and [Games](GAMES.md).

Terminal, Files and Dock were built in up to ZeroOS 0.1.x. The release SD card image still comes
with them installed. A device updated from 0.1.x gets them back the first time you open **Apps**
and press **Tab > U** (which that update needs anyway, for the new app ABI). Their system parts stay
in ZeroOS: the desktop on the monitor (`zeroos-dock`), which Files, Camera, Mesh and Remote also
use, and the console service behind Terminal. Remote's SSH and mosh need the Terminal app.

Apps installed under 0.1.x were built for the old app ABI and can't run on a newer ZeroOS. The
first install or **U** after the update takes those old builds off (your files and settings stay)
and **U** installs the new builds. It then clears extras that only the old builds needed. Before ZeroOS 0.3.1 the old builds were left on and every
install failed with "Could not install"; if you see that, update ZeroOS first, or run
`sudo apt-get -f install -y` over SSH once.

Each app brings its own extras with it, so the base image doesn't carry them: Remote brings
Tailscale, Sync brings Syncthing, Camera brings libcamera and M5Stack's camera program, Mesh brings
meshtasticd, GPS brings gpsd. Removing an app takes its extras away again, unless another app still
needs them.

## Using it

Open **Apps** (pinned on the home screen, or type "apps" there). It looks for new apps when its
list is more than 6 hours old. Type to find an app.

| Key | |
| --- | --- |
| Enter | an app's page: **Install**, **Update**, **Open**, **Remove** (Remove asks first) |
| Source row (top) | Left / Right shows only one source's apps (**All**, then each source, M5Stack's store as **M5Stack**); Tab > S does the same |
| Tab > U | update every app that has a newer version |
| Tab > R | look for new apps now |
| Tab > P | sources: where apps come from, in order (the higher one wins). Tab > K / Tab > J moves the highlighted one up / down; Tab > D removes one (it asks first); **Add a GitHub repo** adds one, after your password |

Ctrl and the letter does the same without opening the menu (Ctrl + U updates every app).

Installing needs a connection (Wi-Fi, Ethernet or a tethered phone). A new app appears on the home
screen as soon as it's installed, whether it was installed from Apps or from a terminal (see
[How the home screen finds new apps](#how-the-home-screen-finds-new-apps)). After an update to an
app you already opened from Apps, the screen restarts once (about a second) to run the new
version. After the same update from a terminal, home says "An app was updated: restart to use the
new version" instead, because ZeroShell won't restart itself under an app you may be using.

From a terminal:

```sh
zeroos-apps list                 # what's on offer and what's installed
sudo zeroos-apps install media
sudo zeroos-apps remove media
sudo zeroos-apps upgrade         # every app with a newer version
sudo zeroos-apps add-source someone/cardputer-apps
sudo zeroos-apps source-up someone/cardputer-apps     # move it up the list (source-down: down)
sudo zeroos-apps install-file zeroos-app-media_0.5.0_arm64.deb   # a package you copied over
```

An SD card image can have apps on it from the start: the **Build image** workflow takes a list
(for example `notes media gps`), as does `ZEROOS_APPS="notes media gps" image/build.sh`.

## Where apps come from

A **source** is a GitHub repository whose newest release holds a file called `zeroos-apps.index`
and the packages it lists, or M5Stack's own app store (below). ZeroOS starts with three, in this
order:

| Source | What it is |
| --- | --- |
| [`hartj77-blip/CardPuter-Apps.4ME-U`](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U) | The apps' own repository. Each app is developed and released there, with its own version. |
| `hartj77-blip/CardPuter-OS.4ME-U` | This repository. Its releases still carry the apps built from the same commit as the OS, as a fallback while the move is under way. |
| `m5stack` | [M5Stack's app store](#m5stacks-app-store): community apps made for M5's own launcher. |

When two sources offer an app with the same name, the one higher in the list wins, so apps come
from the apps repository whenever it has a build for this ZeroOS. To change the order, highlight a
source in **Apps > Tab > P** and press **Tab > K** (up) or **Tab > J** (down); the list of apps is
fetched again in the new order. A device updated from ZeroOS
0.3.4 or older gets the apps repository added to its list by the update. The first **U** after
that updates every installed app once, to the apps repository's builds.

Any other repository that publishes the same files works too: add it in
**Apps > Tab > P > Add a GitHub repo**, or add a line to `/etc/zeroos/apps.sources` (an `owner/repo`,
or any `https://` address that serves the index).

Adding a source asks for your password (the one you log in with). A source decides what runs as
root on the device, so it is the one thing in Apps that needs it. Installing, updating, removing
and reordering don't: they only use sources that are already on the list. Behind the scenes,
polkit lets the home screen run those jobs without a password, but not `add-source`
(`image/stage-zeroos/files/90-zeroos.rules`). Apps starts that job with `sudo`, which reads your
password from a pipe, never from the command line.

**Trust.** An app runs with the same access as the home screen, and installing one runs its
package's scripts as root. The SHA-256 in the index only proves the download is complete; the
index comes from the same place. Once ZeroOS has a release key, this project's two sources must
sign their index ([UPDATE.md, "Signatures"](UPDATE.md#signatures)). Only add sources you trust.

Other checks on every install:

- **No going back.** Apps never installs an older version than the one already there, so a stale
  or replayed list can't put back a version with a known hole. `sudo zeroos-apps install-file`
  still can, if you mean it.
- **An app's own package source is checked.** An app may add an apt repository for what it needs
  (Mesh adds Meshtastic's, Remote adds Tailscale's). That repository must name the app's own key
  (`signed-by=/usr/share/keyrings/zeroos-app-<id>.gpg` or `.asc`), and nothing in it may switch
  apt's checks off (`trusted=yes`, `allow-insecure` and the like). Otherwise the app isn't
  installed.

## M5Stack's app store

M5Stack runs an app store for the Cardputer Zero: about 190 apps by the community, listed in
[CardputerZero/packages](https://github.com/CardputerZero/packages) and reviewed there before they
are published. They are made for M5's own launcher (APPLaunch), which ZeroOS replaces, but they
are ordinary Debian packages, so ZeroOS installs them too and opens them its own way.

- **In Apps** they are in the same list as ZeroOS's apps, marked **M5**, with ids `m5-<package>`.
  An app's page says it's from M5Stack's app store, and what to know first ("Runs in the
  background", "Needs extra hardware") as M5's store lists it.
- **Opening one** hands it the screen, the way Camera works: ZeroShell stops drawing and the app
  draws on the LCD and reads the keyboard itself. **Hold Esc for 3 seconds** to leave an app that
  has no way out of its own (as on M5's launcher); ZeroShell then stops it and everything it
  started, and goes home. Apps marked to run in a terminal open in the Terminal instead.
- **Install, update and remove** work as for any app, Update all included. `zeroos-apps` reads
  M5's `registry.json` (`https://cardputer.cc/generated/registry.json`; a sources line with the old
  `cardputerzero.github.io` address is read as the new one, since that one now redirects to plain http), checks each
  download against its SHA-256 and installs it with apt, which brings the Debian packages it
  depends on.

To take M5Stack's store out of the list: **Apps > Tab > P**, highlight it, **Tab > D**. To put it
back, **Add a GitHub repo** and type `m5stack`.

What's different from ZeroOS's own apps:

- **They are not checked by this project.** M5 reviews them; ZeroOS only checks that each
  download is the one M5's list names. Like any app, installing one runs its package's scripts
  as root, and some change system settings (a battery saver that adds a service, apps that add
  you to hardware groups).
- **They can't replace what's already on the device.** Only apps M5's list marks as reviewed
  (`"review": {"status": "approved"}`) are offered; one without a review status is left out. An
  M5 package can't take the name of a package already installed that didn't come from M5's store
  (`sudo`, `openssh-server`...), or replace, conflict with or break one. Before installing, apt
  does a dry run, and if it would remove anything but another M5 app, nothing is installed. (Every
  relation in M5's list today is between M5 apps: an app's old name.)
- **Some may not work.** They were tested on M5's system with M5's launcher running. An app that
  talks to that launcher, expects its services, or draws somewhere other than the LCD's
  framebuffer won't work here. Most apps (Python with pygame or Pillow, LVGL, SDL) only need the
  screen and keyboard, which ZeroOS hands over.
  The [compatibility checklist](M5-APPS.md) goes through all of them: two can't install (they
  need M5's launcher), and a few more need a closer look.
- **No real icons yet**: on the home screen each shows its first letter.
- **Background apps keep running** after you leave them, as on M5's system (an app with a
  service, such as a battery saver). One that runs as you shows in Settings > Running programs,
  where Enter stops it until it's started again; a system service doesn't, so remove the app to
  stop that.

How it fits together:

```
zeroos-apps refresh      registry.json --(image/stage-zeroos/files/apps/m5-registry)--> catalog blocks
                         app=m5-<pkg>, kind=m5, package=, url=, sha256=, size=, needs=, warn=
zeroos-apps install      download url, check SHA-256, apt-get install, note the package and its
                         launcher entries in /var/lib/zeroos/apps/m5/m5-<pkg>
ZeroShell                one app per launcher entry (/usr/share/APPLaunch/applications/*.desktop),
                         ui/m5_apps.cpp; Hal::external_open runs its Exec line with sh in
                         /usr/share/APPLaunch, as uid 1000, with LV_LINUX_FBDEV_DEVICE and
                         APPLAUNCH_LINUX_FBDEV_DEVICE set to the LCD's framebuffer
udev (m5-fb0-link)       /dev/fb0 -> the LCD when nothing else is fb0: many apps open /dev/fb0
                         directly, and here the LCD is usually fb1 (the firmware's boot
                         framebuffer took fb0, then went away). A real fb0 (HDMI) is left alone
```

An M5 package copied over by hand installs with `sudo zeroos-apps install-file <file>.deb`. M5's
camera program's own launcher entry is hidden: the Camera app already opens it.

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

### How the home screen finds new apps

The home screen's app list is read from three folders: app modules in `/usr/lib/zeroos/apps`,
M5Stack launcher entries in `/usr/share/APPLaunch/applications`, and `zeroos-apps`' notes on M5
apps in `/var/lib/zeroos/apps/m5`. The Apps app reads them again after each of its own jobs. An
install from a terminal (`sudo zeroos-apps install`, `apt`, `dpkg -i`) doesn't go through Apps, so
ZeroShell also watches those folders itself (`shell/src/ui/app_watch.cpp`):

1. At start it asks the kernel, through inotify, to report files created, deleted, renamed into
   or out of, or finished being written in each folder. A folder that doesn't exist yet (the M5
   one is created by the first M5 app, and dpkg removes it again with the last) is watched through
   its nearest parent that does, for its creation only.
2. On the device the inotify file sits in the main loop's `poll()` next to the keyboards, so while
   nothing is installed it costs no CPU at all: no timer, no polling.
3. One apt run changes files many times, so ZeroShell waits until nothing has changed for 1.5
   seconds.
4. If home is open, it reads the app list again and redraws home on the page it was on (a search
   keeps its text; All apps keeps its filter and the app in focus). If another app is open, it
   only notes the change and reads the list on the way back home, because an open app may still
   be using entries from the old list.
5. If a module that was already loaded has been replaced (an upgrade), its old code stays in use
   until ZeroShell restarts, as with any shared library. Home shows that once as a message.

The journal shows each reload: `journalctl -u zeroshell | grep "apps changed"`.

## Making an app

Apps are made in the apps repository,
[CardPuter-Apps.4ME-U](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U): one folder per app,
with its code, its package description and its docs. Its README shows how to add or change one.
It builds against this repository (pinned there as `base/`), using three things from here:

| From this repository | What it is |
| --- | --- |
| `shell/src/` headers, `ZS_APP_ABI` in `shell/src/ui/app.h` | What an app is compiled against, and the app level |
| `shell/cmake/ZeroShellApps.cmake` | `zs_app()` and `zs_apps_build()`, which turn an app into a module |
| `image/apps/make-app-deb.sh` | Packs a module into `zeroos-app-<id>.deb` and prints its block for the index |

A screen says what its keys do with `shell_set_actions()` (`ui/app.h`): the shell then draws the
soft-key bar, the Tab menu and Ctrl + letter shortcuts from that one list. See [UX.md](UX.md).

New apps go there, not here. The apps still in `shell/src/apps/` and `image/apps/` are the
fallback copies; they will be removed once a device has installed from the apps repository.
Don't change an app in both places: change it in the apps repository.

### The app level

Raise `ZS_APP_ABI` in the same pull request as any change that breaks already-built modules: a
header an app includes (`ui/`, `hal/`, `apps/common.h`), LVGL or `lv_conf.h`. ZeroShell skips
modules built for another level and the store doesn't offer them.

The level is 4 since `ListView` (`shell/src/ui/list_view.h`) started building only the rows that
fit on the screen. Its private members changed, so its size changed, and an app that keeps a
`ListView` inside its own class (Net Tools and RF Tools, through `apps/kit/suite.h` in the apps
repository) must be rebuilt against the new header. Its public functions are the same, so the
apps' code doesn't change, only their build.

The apps repository watches this one. It builds every app for the level of the newest ZeroOS
release, and for the level on `main` when that is different, so the apps for the next ZeroOS are
published before it is released. Before releasing a ZeroOS that raises the level, check that the
apps repository's [newest release](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U/releases/latest)
lists builds for it (its notes say which levels it was built for). If it doesn't yet, run its
Release workflow by hand.

### Text boxes and word suggestions

Every LVGL text area (`lv_textarea`) in an app gets ZeroOS's word suggestions with no code in the
app: ZeroShell watches whichever text box has the focus. Two things to do in an app:

- A box for a secret must use password mode (`lv_textarea_set_password_mode(ta, true)`). Nothing
  is suggested there and nothing typed there is learned.
- A box for something that is no prose and shouldn't be remembered (a code, a serial number, a
  device ID) can opt out with `lv_obj_add_flag(ta, LV_OBJ_FLAG_USER_4)`. In the OS sources that
  flag is named `kNoSuggestFlag` (`shell/src/ui/suggest_bar.h`); it is a plain LVGL flag, so it
  needs no newer app level.

While suggestions show, Tab goes to them before the app's `on_key()`. An app that uses Tab in a
text screen still gets it whenever no suggestion is showing.

## What's not done yet

- Nothing here has run on a Cardputer yet: the installer is tested against stand-ins for apt and
  GitHub (`image/tests/apps-test.sh`), and the modules in a headless ZeroShell on a PC.
- A ZeroOS update doesn't update apps by itself; open **Apps** and press **Tab > U**.
- Removing Terminal also removes one-key SSH from Remote (Remote says so); removing Dock leaves
  Dock mode reachable through Settings' "On HDMI" option.
- The store shows an app's real icon only once it's installed (until then, a grid). Apps from
  M5Stack's store show a letter, not their icon.
- Apps from M5Stack's store haven't been tried on a Cardputer running ZeroOS yet. The
  [compatibility checklist](M5-APPS.md) is where results go.
- An app's page doesn't show screenshots.
