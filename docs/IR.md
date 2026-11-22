# IR Remote: a universal remote

The Cardputer Zero has an IR LED and an IR receiver. The **IR Remote** app turns them into a
universal remote for TVs, projectors, sound bars, air conditioners, fans and LED lights, using the
codes of about 2,000 real remotes from [Flipper-IRDB](https://github.com/Lucaslhm/Flipper-IRDB).

## Using it

The app opens on three lists.

**My remotes** are yours to keep and change (they live in `~/IR`).

- **New remote**: name it, then press **A** for each button you want: give the button a name,
  point your old remote at the Cardputer and press that button. NEC and Samsung codes are
  recognised and stored as codes; anything else is kept as a raw recording, which replays the same.
- On a remote: **Enter** sends the button, **L** learns it again, **A** adds a button, **D** (twice)
  deletes one.

**Universal: send every code** is for when you don't know which remote a device uses.

1. Pick the kind of device (TVs, Projectors, Audio, ACs, Fans, Lights) and the button
   (Power, Vol +, Vol -, Mute, Ch +, Ch -...).
2. Point the Cardputer at the device and press **Enter**. Every distinct code for that button
   goes out, one every 150 ms, the most common first (for TV power: LG, Samsung, Sony, Panasonic...).
3. Press any key as soon as the device reacts. The app stops on the code it just sent and shows
   whose it is. **Left** and **Right** try the codes either side of it (you might have pressed a
   moment late), **Enter** sends it again.
4. **O** opens that brand's remotes, where you can find the model with all its buttons and press
   **S** to save it to My remotes.

**Browse or search** lists every remote by kind, brand and model. Type to search: on the first
page the search covers every brand and model (`sam`, `bravia`, `roku`); inside a kind it filters
brands, inside a brand it filters models. **Backspace** edits the search, **Esc** clears it.
On a remote, **S** saves a copy to My remotes.

## Flipper Zero files

Remotes are stored in the Flipper Zero's `.ir` format, so files go both ways: copy a `.ir` file
from a Flipper (or from Flipper-IRDB) into `~/IR` and it appears under My remotes; copy one of
yours to a Flipper's `infrared/` folder and it works there. Sync (docs/SYNC.md) doesn't carry
`~/IR` yet; Files and Taildrop do.

Every protocol in Flipper-IRDB can be sent: NEC, NECext, NEC42, NEC42ext, Samsung32, RC5,
RC5X, RC6, SIRC (12, 15 and 20 bit), Kaseikyo (Panasonic, Denon...), RCA, Pioneer, and raw
recordings (most air conditioners).

## How it works

| Part | Where |
| --- | --- |
| `.ir` reader and writer; the encoders that turn a protocol, address and command into IR timings; recognising learned codes | `shell/src/ir/ir.cpp` |
| The database and `~/IR` | `shell/src/ir/db.cpp` |
| The app | `shell/src/apps/ir_remote.cpp` |
| Sending and receiving: `ir-ctl` on the rc-core devices (`gpio-ir-tx`, `gpio-ir-recv`) | `LinuxHal::ir_send`, `ir_learn` in `shell/src/hal/linux_hal.cpp` |
| Building the database | `tools/irdb/build_irdb.py` |
| Tests (encoders checked against known frames, file round trips) | `shell/tests/ir_test.cpp`, run by CI |

The encoders are our own, written from the protocols' published timings; they pack address and
command bits the way the Flipper does, so codes it recorded replay the same. Every one of the
36,000 codes in the database encodes, and the longest (an air conditioner) is 959 edges, under
the kernel's 1,024 limit.

To send, ZeroShell writes the timings as `pulse`/`space` lines and runs
`ir-ctl -d <tx device> -c <carrier> -s <file>`. It finds which `/dev/lircN` sends and which
receives from `ir-ctl --features`. Learning runs `ir-ctl -r --one-shot` for up to 5 seconds.

## The database

It comes with the IR Remote app (docs/APPS.md), whose package installs it in
`/usr/share/zeroos/ir` (about 12 MB on disk, under 1 MB in the package), so the base image
doesn't carry it. `tools/irdb/build_irdb.py --fetch` clones a pinned Flipper-IRDB
commit and writes:

- the `.ir` files as published, `<Category>/<Brand>/<Model>.ir`;
- `index.tsv`, one line per remote, so the app lists 2,000 remotes without opening them;
- `universal/<Kind>.ir`: for each kind of device, every distinct code for each common button,
  most-used first, each credited to the brand that uses it most;
- `SOURCE.txt` (commit) and Flipper-IRDB's `LICENSE`.

`image/apps/build.sh` runs it (into `build/irdb`) when it isn't there yet; set `ZEROOS_IRDB` to
use another folder. To move to a newer Flipper-IRDB, change `COMMIT` in the
script. The app reads `ZEROOS_IR_DB` instead of `/usr/share/zeroos/ir` if it is set, which is how
the simulator gets codes:

```sh
tools/irdb/build_irdb.py --src tools/irdb/sample --out build/irdb-sample   # 14 hand-written remotes
ZEROOS_IR_DB=$PWD/build/irdb-sample ./build/sim/zeroshell
```

(or `--fetch --out build/irdb` for the full set).

### Licence

Flipper-IRDB is CC0-1.0 (public domain dedication). Its README notes that commits before
[2319685](https://github.com/Lucaslhm/Flipper-IRDB/commit/2319685f2cbf0cd3f809609622cade14d24fb819)
are not covered by that dedication; IR codes are facts about devices, and we ship the files as
published with their `LICENSE` and the commit they came from. Its `_Converted_` folder (bulk
conversions from other databases, some with their own terms) is left out. We don't use
[probonopd/irdb](https://github.com/probonopd/irdb): its licence asks every product that ships it
to register with the project and send units on request.

## Still to do

- Try it on the hardware: whether `gpio-ir-tx` sends with the stock overlay (docs/HARDWARE.md),
  and the range of the LED.
- Hold-to-repeat for volume keys (today each press sends one frame).
- Recognise more protocols when learning (RC5, RC6, SIRC are stored raw for now, which works but
  can't be read).
- Search across My remotes too; rename a remote.
