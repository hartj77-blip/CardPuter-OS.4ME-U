# Keys

Everything on ZeroOS works from the keyboard. Arrows move, Enter opens, and Esc (top left) goes back.
The \` key works as Esc too.

The keys are being redesigned to work the same on every screen ([UX.md](UX.md) is the plan).
Screens move over one at a time: everything that is part of ZeroOS itself is done, and the
installable apps follow.

## Everywhere

| Key | What it does |
| --- | --- |
| Esc | Back. Hold it for a moment to go to the home screen from anywhere, even from a text box (HOME on the key) |
| Tab, held | Lock: the screen goes off at once (LOCK on the key). The next key only wakes it |
| Fn + J (PRSC) | Screenshot: saved as a PNG in ~/Pictures/Screenshots |
| Fn + H (HELP) | Show every key for this screen, the same as 0 |
| Ctrl + Space | Open or close the taskbar panel, from anywhere, even while typing |
| 1 to 9 | Pick the 1st to 9th item on screen: the same as moving to it and pressing Enter |
| 0 | Show every key for this screen. On screens not moved over yet, show which number is which |

Away from a text box the number keys are buttons; in a text box they type numbers. An app can use a
number key for something of its own first. For example, 0 goes back to the start of a video in
Media.

Like Shift, Fn and Sym, Ctrl doesn't need holding: tap Ctrl and the status bar shows **Ctrl** until
you press the next key. Tap it again to cancel. Holding it down works too.

## Screens moved to the new design

Home, Settings, Wi-Fi, Bluetooth, Apps and Update so far. A line along the bottom of the screen says what the keys do, and:

| Key | What it does |
| --- | --- |
| Tab | A menu of everything you can do here, each with a letter: press the letter to do it |
| Ctrl + a letter | Do that menu item straight away, without opening the menu |
| Letters | In a list, type to find: only the rows that match stay. Backspace or Esc undoes it |

Every row shows its number key: 1 to 9 are the first nine rows of a list, like a phone menu.
Esc in an app opened from another one (Settings > Wi-Fi) goes back to that one. Something that can't be undone (forgetting, removing) asks first:
Enter does it, Esc doesn't.

## Home screen

Home is a search box, seven pinned apps and the apps you opened last.

| Key | What it does |
| --- | --- |
| Letters | Search apps, settings and actions ("wifi", "bright", "mute"). Enter opens the first |
| 1 to 7 | Open a pinned app. While searching, 1 to 9 pick a result |
| Arrows (Fn + F/Z/X/C) | Move between the pinned apps and the Recent list |
| Esc | Clear the search |
| Tab | All apps |

**All apps** (Tab, or the last row) lists every app A to Z: type to find one. To pin an app,
highlight it and press Tab, P, then the key you want it on (1 to 7). The app already on that key
swaps places with it, or is unpinned. Tab, U unpins. Some search results change
in place: on Volume or Brightness, Left and Right turn it down or up.

## The taskbar panel

Press Ctrl + Space to open a row of tiles under the status bar. Each tile has a number key:

| Key | Tile | Enter or its number |
| --- | --- | --- |
| 1 | Wi-Fi | Turns Wi-Fi on or off |
| 2 | Bluetooth | Turns Bluetooth on or off |
| 3 | Volume | Up/Down change it. Enter mutes |
| 4 | Brightness | Up/Down change it |
| 5 | Battery | Opens Battery |
| 6 | Clock | Opens Clock |
| 7 | More | Opens Settings |

Left/Right move between the tiles, and Esc or Ctrl + Space closes the panel.

## Fn keys

Hold Fn and press a key, anywhere. In the taskbar panel you can leave out the Fn. This is M5's Fn
layer for the Cardputer Zero.

| Key | With Fn |
| --- | --- |
| F / Z / X / C | Up / Left / Down / Right |
| A | Sound on or off |
| S / D | Volume down / up |
| U / I | Brightness down / up |
| Q | Play or pause music |
| W / E | Previous / next track |
| K / N | Home / End: the first or last row of a list |
| L / M | Page up / Page down: five items at a time in a list |
| J | PRSC: screenshot, saved as a PNG in ~/Pictures/Screenshots. Works in the Terminal and M5Stack apps too (a beep says it's saved) |
| H | HELP: every key for this screen, the same as 0 |
| B | INS: Insert, for the Terminal and apps that use it |
| Backspace | Delete |
| 1 to 0, O, P | F1 to F12 (not used yet) |

These are the red labels on the keys. A USB keyboard's PrintScreen key takes a screenshot too.
