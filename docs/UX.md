# ZeroOS interface spec (UI 2)

Status: **phase 0, agreed direction, not built yet.** This is the target that every screen in ZeroOS
and in [CardPuter-Apps.4ME-U](https://github.com/hartj77-blip/CardPuter-Apps.4ME-U) moves to. Until
the work lands, [KEYS.md](KEYS.md) describes what the device does today.

Mockups at real size (320 x 170, shown at 2x) are in [ux/](ux/).

## Why

The Cardputer has a full keyboard, but its arrows are chords (Fn + F/Z/X/C). The interface today
leans on arrows (a 2D app grid, 3 pages of 10), the keys change meaning between screens (letters
are Fn keys on home only, 3 to 9 open dock apps on home but pick items elsewhere), and every app
writes its own key hints in its own place (`R rescan`, `Tab: info`, `Enter: accept / Del: ignore`).

UI 2 borrows from phones that had real keyboards and tiny screens:

| From | We take |
| --- | --- |
| BlackBerry | Type on the home screen to search everything. A menu key that lists every action, with its shortcut letter beside it |
| Nokia / T9 phones | A soft-key bar that always says what the keys do. Numbered menus you learn by heart |
| Flipper Zero, M5's launchers | Vertical lists, one thing in focus, Back always goes back |
| Spotlight, Raycast, which-key | One search box for apps, settings and actions. A key that shows every key |

## Rules

1. **Few arrow presses.** Typing, numbers and the menu get you there; arrows are for fine moves.
2. **A key means the same thing everywhere.** The only exception is a Control screen (below), and
   its soft-key bar says so.
3. **The screen always says what the keys do.** Every screen has a soft-key bar, built from the
   same list of actions as its menu and its number badges, so it can't go stale.
4. **Apps are built from the kit,** not drawn by hand, so they look and behave alike.

## Global keys

These work on every screen, in every app, including in a text box.

| Key | Does |
| --- | --- |
| **Enter** | The main action on the item in focus (open, play, send, toggle) |
| **Esc** (or \`) | Back one step. At an app's first screen, back to home. On home, clear the search |
| **Hold Esc** (0.6 s) | Home, from anywhere |
| **Tab** | The menu: every action for the item in focus and the screen |
| **Hold Tab** (0.6 s) | Lock: screen off now (LOCK on the key). The next key wakes it to the lock screen; the one after unlocks |
| **Fn + J** (PRSC) | Screenshot to ~/Pictures/Screenshots, from anywhere, even the Terminal |
| **Fn + H** (HELP) | The key help sheet, the same as 0 |
| **Ctrl + Space** | The taskbar panel (Wi-Fi, Bluetooth, volume, brightness, battery, clock) |
| **Up / Down** (Fn + F / X) | Move focus |
| **Left / Right** (Fn + Z / C) | Switch tab, or change the value in a row that has one (volume, a timer) |
| **1 to 9** | Pick the item with that number badge (same as moving to it and pressing Enter) |
| **0** | Show every key for this screen (the key help sheet) |
| **Ctrl + a letter** | Run the menu action with that letter, without opening the menu |
| **Fn + a letter** | The Fn layer, always: arrows, volume, brightness, media keys, Home/End, Page up/down |

What changes from today:

- Numbers are **1 to 9** for items. Home moves to **hold Esc**, the panel to **Ctrl + Space**.
- **Fn is always Fn.** No screen turns plain letters into Fn keys any more; home types into search
  instead. Double-tap Fn locks the Fn layer (the keyboard driver already does this), for anyone who
  wants a run of arrow presses.
- **Tab is the menu**, not "next tab". Tabs move to Left / Right.

In a text box, 1 to 9 and 0 type digits and Tab still opens the menu, with one exception: while
word suggestions show for the word being typed, Tab types the first one ([KEYS.md](KEYS.md)). Esc in a text box goes back
(an empty box) or clears it (a box with text). Hold Esc always goes home.

## Kinds of screen

What a plain letter does depends on the kind of screen. The soft-key bar shows which kind it is.

| Kind | Plain letters | Examples |
| --- | --- | --- |
| **List** | Filter the list (type-ahead). Backspace removes a letter; with no filter, Backspace is the list's own back action (Files: up a folder) | Files, Apps, Wi-Fi networks, Settings, Media library, IR brands |
| **Text** | Type | Notes, Wi-Fi password, AI and Mesh message boxes, search |
| **Control** | Shortcuts, declared in the screen's action list and shown in the soft-key bar | Media's player, Clock, Camera, IR remote keypad, GPS |
| **Game** | Belong to the game. Only hold Esc (to its pause menu) and Ctrl + Space escape | Games while playing |

A List screen never has letter shortcuts. Its actions are in the menu (Tab, then the letter) or on
Ctrl + the letter.

## The menu (Tab)

A pop-up list, over the bottom of the screen, of every action for the item in focus, then the
screen's own actions. Each row shows its letter. In the menu:

- A plain letter runs that action. Up / Down and Enter work too, and so do 1 to 9.
- Esc or Tab closes it.
- Actions that can't run right now are greyed, not hidden, so the menu stays the same shape.
- Actions that destroy something (Delete, Forget, Remove) go last, in red, and ask to confirm.

```
  ┌──────────────────────┐
  │ Files: notes.txt     │
  │ O  Open              │
  │ R  Rename            │
  │ C  Copy              │
  │ S  Share to PC       │
  │ ─────────────────    │
  │ N  New folder        │
  │ H  Show hidden       │
  │ D  Delete            │  (red)
  └──────────────────────┘
```

Shortcut letters follow BlackBerry's habit: the first letter of the word if it's free. Some
letters mean the same thing in every app:

| Letter | Action |
| --- | --- |
| N | New |
| D | Delete / Remove / Forget |
| R | Refresh / Rescan |
| E | Edit / Rename |
| S | Share / Save |
| F | Find (only on screens that aren't Lists; a List is always searchable) |
| I | Info / Details |
| P | Settings for this app (Preferences) |

## Soft-key bar

The last line of every screen, 14 px high: up to four actions as `key label`, Enter first, Tab Menu
last. The kit builds it from the screen's action list; apps never write hint text.

```
⏎ Open   ⌫ Up   0 Keys   ⇥ Menu
```

A List screen with a filter typed shows the filter on the left instead:
`🔍 doc   ⏎ Open   ⇥ Menu`. A Control screen shows its letters: `␣ Pause  N Next  R Rec  ⇥ Menu`.

## Number badges

Rows that 1 to 9 can pick show their number all the time, in a small muted badge on the left. Like a
phone menu, the numbers belong to the **first nine rows** of the list (of what the filter leaves),
wherever it is scrolled, so "Settings, 9" is always the same row; picking one scrolls to it. Rows
past 9 have no badge. Badges are off on Text screens (the keys type digits). Screens without a
list keep the older rule: 0 shows badges on what 1 to 9 pick there.

## Confirming

Anything that destroys data or can't be undone asks once, in the same place, with the same keys:

```
  Forget "HomeNet"?
  ⏎ Forget        Esc Cancel
```

This replaces today's "press D again" and "Del twice" patterns.

## Home

Home is a search box, the pinned apps, and what you used last. It replaces the app grid and the
dock.

```
┌ 21:44 ─────────────────── ⚡78% ┐
│ › _                              │  typing goes here
│ ①Term ②Files ③Notes ④Media ⑤…   │  pinned: 1 to 9
│ RECENT                           │
│   Media · Night Drive ▶          │  opens where you left off
│   IR Remote · Living room TV     │
│   Wi-Fi · HomeNet                │
│ ⏎ Open  ↓ All apps  ⇥ Menu       │
└──────────────────────────────────┘
```

- **Pinned**: up to 7 apps on 1 to 7 (as many as fit across). Pins are made in All apps: Tab on an
  app, P, then the key to put it on (1 to 7). An app already on that key swaps places with it, or
  is unpinned if the new one wasn't pinned. U unpins. A fresh ZeroOS pins Terminal, Files, Notes,
  Media, Apps and Settings, the ones installed.
- **Recent**: the last few things you did, newest first. An app can say what to reopen (a track, a
  remote, a network, a file), so Enter goes straight back to it.
- **All apps**: Tab on Home (Home has no menu of its own), or Enter on the All apps row, opens the
  A-Z list of apps. It's a List screen, so typing filters it.
- **Search**: any letter starts a search. Results are grouped: Apps, Settings, Actions, then each
  app's own results (files, notes, remotes, networks, songs). Enter runs the top result. Left /
  Right on a value result (Volume, Brightness) change it in place.
- **Matching** (`shell/src/ui/search_match.h`, the same rule for Home search and every list's
  filter): each typed word has to be in the name, or in the words it's also known by ("bt" for
  Bluetooth, "music" for Media), in any order; capitals and punctuation don't count ("wifi" is
  Wi-Fi). The best match comes first: the whole name, then names that start with what was typed,
  then names with a word that starts with it ("fi": Files, then Wi-Fi), then names that only
  contain it, then keyword matches. Equally good results keep the Apps, Settings, Actions order.

```
› wi
  APPS      Wi-Fi
  SETTINGS  Wi-Fi › Saved networks
  ACTIONS   Turn Wi-Fi off
  WI-FI     HomeNet · connected
            Workshop-5G
```

### Search providers

An app adds results to search with a provider: given the typed text, it returns rows (title,
detail, icon, and what Enter does). Providers must answer from memory, in under 5 ms; anything
slower (a file index) is built in the background and searched from memory. Built in:

| Provider | Finds |
| --- | --- |
| Apps | App names, and words each app lists (Media: "music", "jellyfin", "voice") |
| Settings | Every row in Settings, by name and by synonyms ("dim" finds Dim screen after) |
| Actions | Wi-Fi on/off, Bluetooth on/off, volume, brightness, mute, restart, shut down, update |
| Files | File and folder names under the home folder |

Apps add their own: Notes (titles and text), Media (songs, albums), IR Remote (remotes), Wi-Fi
(networks), Remote (devices), AI (past chats).

## Taskbar panel (Ctrl + Space)

As today, a row of tiles under the status bar, numbered 1 to 7 instead of 3 to 9: Wi-Fi,
Bluetooth, Volume, Brightness, Battery, Clock, Settings. Esc or Ctrl + Space closes it.

## Settings

One app, one tree. The separate Wi-Fi, Battery, Clock and Update apps become sections of it (the
apps stay in search and All apps as shortcuts into their section).

```
Settings
 1 Wi-Fi            HomeNet
 2 Bluetooth        On
 3 Display          Brightness, dim, screen off
 4 Sound            Volume, output
 5 Battery & power  78%, Balanced
 6 Date & time
 7 Apps             Installed, sources, updates
 8 Update           0.1.0, up to date
 9 About            Model, storage, licences
```

Every row is reachable from search, so "Settings 3 2" and typing `dim` both work.

## Layout

The screen is 320 x 170.

| Part | Height | Notes |
| --- | --- | --- |
| Status bar | 20 | Time left, then the app's title; icons right |
| Content | 136 | |
| Soft-key bar | 14 | `zs_font_12`, muted text, key glyphs in an outlined keycap |

Rows in a list are 22 px (6 on screen), `zs_font_14` for the title, `zs_font_12` for the value on
the right. The focused row is lit white with black text, full width, square. A two-line row
(title and detail) is 32 px. Margins are 4 px; nothing touches the screen edge but the bars.

## Look: Monochrome (UI 3)

UI 3 is a new skin on the UI 2 keys above; nothing about navigation changes. Mockups at real
size are in [ux/ui3/mockups.html](ux/ui3/mockups.html), and
[ux/ui3/phase1-before-after.png](ux/ui3/phase1-before-after.png) shows the first step on the
simulator.

- **Greys.** Black, white and a ramp of greys (`kGrey0` to `kGrey8` in `ui/theme.h`). Screens use
  the role names (`kBg`, `kSurface`, `kText`, `kMuted`, `kBorder`), not hex values.
- **Focus** is lit white: a short white-to-light-grey gradient with black text
  (`style_focus_fill`). `kAccent` is now that white, and text on it (`kOnAccent`) is black.
- **Depth, BlackBerry-style.** Menus, dialogs, the taskbar panel and toasts are lifted sheets: a
  soft grey gradient, a 1 px border and a drop shadow (`style_raised`). The soft-key bar fades to
  black. Shadows only on the one overlay that's open, so lists stay fast.
- **Square** system chrome: no rounded corners on rows, menus, dialogs or buttons.
- **Colour is a message.** Green on/connected/installed (`kGood`), orange update/low (`kWarn`),
  blue in progress (`kInfo`), red destructive (`kBad`). Nothing else in the chrome is coloured.
- **App icons belong to their authors.** Home and the app lists draw each app's icon as the app
  ships it; the shell only adds the focus.
- **Photography** on home and the lock screen only: public-domain Ansel Adams prints from the
  National Park Service Mural Project (1941-42, National Archives series 79-AA), converted to
  greyscale ahead of time (`shell/tools/wallpaper.py`). Built in; there are no custom wallpapers.

| Step | Work | State |
| --- | --- | --- |
| 1 | Theme values and roles in `ui/theme.h`; lit focus, lifted menu/confirm/panel/toast, square chrome; UI 2 names kept as aliases | Done |
| 2 | The photograph behind home: `shell/tools/wallpaper.py` crops, shades and dithers it to neutral greys and writes `ui/wallpaper_data.cpp` (committed, like the fonts). Search and All apps stand on plain black | Done |
| 3 | The photo runs under a see-through status bar on home; pins get a white frame in focus, Recent rows the lit focus; a lock screen (`ui/lock_face.cpp`) after hold Tab: the photo, a large clock and the date. The idle timeout still wakes straight to the app | Done |
| 4 | Inter typeface; outline glyphs for system icons (not app icons) | |
| 5 | Replace hex values in apps (both repositories) with roles; a CI check; drop the aliases | |
| 6 | Mockups, emulator, release | |

## The kit

New shared components in `shell/src/ui/kit/`, for ZeroShell and for app modules. Every screen is
built from these:

| Component | Is |
| --- | --- |
| `Screen` | Title, body, soft-key bar, kind (List / Text / Control / Game). Owns the action list |
| `Action` | Letter, label, icon, enabled, destructive, `run()`. Feeds the menu, the soft-key bar, Ctrl + letter and the key help sheet |
| `ListView` | Rows with number badges, type-ahead filter, value rows, two-line rows, empty state |
| `ListDetail` | A list on the left, the focused item's details on the right (Sync, Remote, Media) |
| `Tabs` | A tab strip under the status bar; Left / Right switch |
| `ValueRow` | A row whose value Left / Right change (toggle, slider, choice) |
| `Menu` | The Tab pop-up |
| `Confirm` | The one way to ask "are you sure" |
| `Prompt` | A one-line text entry screen (replaces `kit::input_screen`) |
| `SearchProvider` | Adds results to home search |
| `Recent` | Tell home what to reopen: `recent_push(title, detail, arg)` |

The kit is part of what apps compile against. `ZS_APP_ABI` goes up by one when the first app in
CardPuter-Apps.4ME-U uses it (see How it gets built); the Apps store offers only builds that match.
(It is already 4: `ListView` changed size when it started building only the visible rows.)

## Every app, before and after

Today's keys are from the app sources. "Menu" means the action moves into the Tab menu with that
letter, and Ctrl + the letter runs it straight away.

| App | Today | UI 2 |
| --- | --- | --- |
| Home | 2D grid, dock on 3-9, Fn layer, Shift + letter jumps | Search + pinned (1-9) + recent; All apps list |
| Settings | Rows, Left / Right change values | Becomes the tree above; absorbs Wi-Fi, Battery, Clock, Update |
| Wi-Fi | `R` rescan, Del forgets | List. Menu: R Rescan, D Forget, I Details, N Hidden network |
| Battery | `P` power profile | Settings › Battery & power; the profile is a ValueRow |
| Clock | Space start/stop, `R` reset | Control: ␣ Start/stop, R Reset, L Lap. Alarms and timers as a List tab |
| Update | Enter does whatever the screen says | Settings › Update; one main action on Enter, as now |
| Apps (store) | `U` update all, `R` refresh, `S` sources, `D` remove source | List with tabs Installed / Get / Updates. Menu: U Update all, R Refresh, P Sources, D Remove |
| Files | `S`, Backspace up a folder | List; Backspace up a folder once the filter is empty. Menu: E Rename, C Copy, M Move, S Share, N New folder, H Hidden, D Delete |
| Notes | Text editor | List of notes, then Text. Menu: N New, E Rename, S Share, F Find in note, D Delete |
| Terminal | Hands the screen to tmux; Ctrl + Q back | Unchanged inside tmux. Its start screen is a List of saved commands and hosts |
| Media | Tab cycles tabs; Space, N, P, R, X, S, M, /, + - = _ 0 in places | Tabs on Left / Right. Library tabs are Lists (typing searches, so `/` goes). Player is Control: ␣ Pause, N Next, P Previous, +/- Volume, Left / Right seek |
| IR Remote | `L` learn, `A` add, `D` delete, `S` save, `O` | Remotes and brands are Lists. The keypad stays a spatial grid (Control). Menu: L Learn, N New button, S Save to My remotes, D Delete |
| Camera | Separate viewfinder program: H help, hold 4, S save, Q scan, U USB | Viewfinder is a Game screen (its own keys; H help stays). Start screen and results use the kit: menu S Save, O Open on monitor |
| GPS | Enter spot, `R` track | Control: ⏎ Mark spot, R Record track. Tracks as a List tab |
| Car | `X` forget, Del | List of modes and devices. Menu: D Forget |
| Remote | Enter SSH, `D` desktop | ListDetail. Menu: S SSH, D Desktop on monitor, F Send file, I Details |
| Sync | Enter accept, Del ignore, "Del twice" stops | ListDetail. Menu: A Accept, D Stop syncing (Confirm), I Show my device ID |
| Tether | "Del again" forgets | List. Menu: D Forget (Confirm) |
| Mesh | `Tab: info`, Tab switches Chat / Info | Tabs on Left / Right; chat is Text. Menu: I Node info, C Channel, P Radio settings |
| AI | `Tab: settings`, Up / Down history | Chat is Text; Up / Down history in an empty box. Menu: N New chat, H History, M Model, P Settings |
| Dev Tools | Code: `G` `N` `R` `T`; Git: `C` `D` `F` `L` `P` `S` `T` `U` `V`; Serial: `E`, Tab | Projects and files are Lists; every letter moves to the menu with the same letter (Git: C Commit, P Push, U Pull, L Log, D Changes…) |
| Games | `C` cores, pad uses most letters | Library is a List. Menu: C Cores, D Delete save. In a game: Game screen, hold Esc = pause menu |
| System | Left / Right switch pages | Tabs on Left / Right |
| Compass, Dock | Mostly Enter / Esc | Kit screens; nothing to move |

## How it gets built

| Phase | Work | Where |
| --- | --- | --- |
| 0 | This spec and the mockups | here |
| 1 | The kit: Screen, Action, ListView, Menu, soft-key bar, Tabs, ValueRow, Confirm, Prompt. `ZS_APP_ABI` up by one | `shell/src/ui/kit/` |
| 2 | Shell keys: hold Esc, Ctrl + Space, Tab menu, 1-9, 0 help, Ctrl + letter, Fn always Fn | `ui/shell.cpp`, `ui/numkeys.cpp`, `platform/keys.*` |
| 3 | Home: search, providers, pinned, recent, All apps | replaces `ui/launcher.cpp` |
| 4 | Settings tree; fold in Wi-Fi, Battery, Clock, Update | `apps/settings.cpp` and friends |
| 5 | Port the apps: Files, Notes, Media, Apps, IR Remote, then the rest | both repos |
| 6 | First-run tour, new KEYS.md, polish | |

Each phase keeps the device usable. The soft-key bar is opt-in: a screen gets it by setting its
actions (`shell_set_actions()` in `ui/app.h`) and is then laid out `kBodyH` high. An app not yet
ported has no bar and keeps its full height, so nothing moves until it's ported.

The kit needs no app-level (`ZS_APP_ABI`) bump on its own: it only adds to the headers, and
already-built modules never call it. The level goes up in the first change that moves an
installable app (CardPuter-Apps.4ME-U) onto the kit, since that app can't run on an older ZeroOS.

Done so far:
- Hold Esc and Ctrl + Space (0.3.8).
- The kit: actions, the soft-key bar, the Tab menu, Ctrl + a letter, the key sheet (0),
  `ListView` (numbered rows, type to filter, value rows), `shell_confirm()`. 1 to 9 pick.
- Esc goes back to the app that opened this one (a back stack); hold Esc goes home.
- Home: search (apps, settings, actions), pinned apps, Recent, All apps.
- Settings as one numbered tree; Wi-Fi, Bluetooth, Apps and Update on the kit.

Still to do: the installable apps (CardPuter-Apps.4ME-U), with the app-level bump; Tabs (Media,
Mesh, System); apps adding their own search results and Recent entries (search providers,
`recent_push`); the first-run tour.

### Testing

- `shell/tools/screenshots.sh` renders every screen headless; each phase updates it, and a port
  is reviewed as before/after pairs.
- `shell/tests/keys_test.cpp` grows a table of the global keys above, run against every app, so no
  app can take Tab, Esc, 1-9, 0 or Ctrl + Space for itself (Game screens excepted).
- Search must answer within one frame on the device; a test feeds 2,000 files and 500 songs to the
  providers and times it.
- Hold Esc and Ctrl + Space are tried on the real keyboard in phase 2, before anything builds on
  them (`docs/HARDWARE-TEST.md`).
