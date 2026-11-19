# surface-touch: gestures, bindings and an API for the Surface touchscreen and pen

`surface-touchd` runs on top of [iptsd](https://github.com/linux-surface/iptsd).
iptsd already turns the IPTS sensor data into a normal touchscreen and pen.
This daemon adds what those plain input devices don't offer:

| Feature | What you get |
|---|---|
| **Gestures** | Tap, double tap and long press with any number of fingers; swipes (left/right/up/down); edge swipes; pinch and rotate (with live begin/update/end phases) |
| **Bindings** | Gesture → key shortcut (`bind swipe:3:left = key SUPER+PAGEDOWN`) or command (`exec …`, run as your user, never root) |
| **Palm rejection while writing** | `pen_blocks_touch`: fingers are ignored while the pen is near the screen, until a short delay after it leaves |
| **Touch on/off** | Turn the touchscreen off and on at runtime (e.g. for cleaning, or in a kiosk) |
| **Rotation** | Gestures and coordinates follow the screen rotation |
| **JSON socket API** | Live touch, pen and gesture streams, device info, contact snapshots, all settings readable and writable at runtime |
| **Clients** | `surface-touchctl` command line tool and `surface_touch.py` Python library |
| **Debugging** | `--record` captures raw touch and pen input; `--replay` runs a recording through the gesture engine |

Works with iptsd's "IPTSD Virtual Touchscreen" and "IPTSD Virtual Stylus"
devices. It falls back to any direct-touch screen or pen. Tested against a
Surface Pro 5 recording (iptsd's SP5 axis ranges).

## Install

On Arch-based distros, use the package built by the `Arch (custom packages)`
GitHub Action (`surface-touch-*.pkg.tar.zst`):

```sh
sudo pacman -U surface-touch-*.pkg.tar.zst
```

Or build it yourself:

```sh
make && make test && sudo make install
```

Then start it:

```sh
sudo systemctl enable --now surface-touchd
sudo usermod -aG input "$USER"     # API access; log out and in again
surface-touchctl info
```

## Bindings

Bindings go in `/etc/surface-touchd.conf`; the example file lists all
options:

```ini
bind swipe:3:left   = key SUPER+PAGEDOWN
bind swipe:4:down   = key SUPER+D
bind tap:3          = key PLAYPAUSE
bind double_tap:2   = key CTRL+Z
bind pinch_in:4     = key SUPER+A
bind edge_swipe:right = exec notify-send hi    # needs exec_user = yourname
```

The format is `GESTURE[:FINGERS][:DIRECTION]`.

- Gestures: `tap`, `double_tap`, `long_press`, `swipe`, `edge_swipe`,
  `pinch_in`, `pinch_out`, `rotate_cw`, `rotate_ccw`.
- Leave out the finger count or direction to match any.

Check the file with `surface-touchd --check`, then restart the service.

## Command line

```sh
surface-touchctl get                          # settings + live state
surface-touchctl set pen_blocks_touch true    # palm rejection while writing
surface-touchctl set touch_enabled false      # touchscreen off
surface-touchctl set rotation 90              # gestures follow screen rotation
surface-touchctl set long_press_ms 450
surface-touchctl contacts                     # fingers on screen right now
surface-touchctl bindings
surface-touchctl monitor gesture pen          # stream events as JSON lines
```

## API

The API uses a Unix socket at `/run/surface-touchd/socket`. Only root and
`socket_group` (default `input`) can use it. Requests and responses are JSON
objects, one per line.

### Requests

| Request | Reply |
|---|---|
| `{"cmd":"ping"}` | `{"reply":"ping","ok":true,"t":…}` |
| `{"cmd":"info"}` | version, touchscreen/pen device (path, name, size in mm), event and gesture types |
| `{"cmd":"get"}` | all settings plus `pen_near`, `touch_blocked`, `contacts` |
| `{"cmd":"set","key":K,"value":V}` | the updated settings, or `ok:false` with `error` |
| `{"cmd":"contacts"}` | `contacts`: slot, id, x, y, major, minor, orientation, palm |
| `{"cmd":"bindings"}` | configured bindings |
| `{"cmd":"subscribe","events":[…]}` | start receiving `touch`, `pen`, `gesture`, `state` (or `all`) |
| `{"cmd":"unsubscribe","events":[…]}` | stop receiving them |

You can `set` these keys:

- `rotation` (0/90/180/270)
- `touch_enabled`, `pen_blocks_touch`, `bindings` (true/false)
- `tap_ms`, `double_tap_ms`, `long_press_ms`, `swipe_ms`, `pen_release_ms`
- `tap_move`, `swipe_min`, `edge_margin`, `pinch_start`, `rotate_start`,
  `palm_major`

### Events

A new connection receives no events until it subscribes. Coordinates are
0–1, in the orientation the user sees. `t` is milliseconds on the monotonic
clock.

```json
{"event":"touch","phase":"down","slot":0,"id":4,"x":0.4,"y":0.4,"major":0.017,"minor":0.013,"orientation":90,"t":1738}
{"event":"touch","phase":"palm","palm":"begin","slot":2,"id":9,…}
{"event":"gesture","gesture":"swipe","fingers":3,"direction":"left","x":0.36,"y":0.5,"dx":-0.3,"dy":0,"t":1338}
{"event":"gesture","gesture":"edge_swipe","fingers":1,"edge":"left",…}
{"event":"gesture","gesture":"pinch","fingers":2,"phase":"update","scale":1.6,…}
{"event":"gesture","gesture":"rotate","fingers":2,"phase":"end","angle":45.0,…}
{"event":"gesture","gesture":"long_press","fingers":1,…}
{"event":"action","binding":"swipe:3:left","action":"key SUPER+PAGEDOWN",…}
{"event":"pen","tool":"pen","near":true,"touching":true,"x":0.5,"y":0.5,"pressure":0.75,"tilt_x":28.6,"tilt_y":0,"button":false,"button2":false,"t":…}
{"event":"state","touch_enabled":true,"touch_blocked":false,"pen_near":true,"rotation":0,"touchscreen":true,"pen":true,"t":…}
```

The meaning of each field:

- `major` and `minor` are the contact size as a fraction of the screen
  diagonal.
- `orientation` is in degrees.
- `scale` is the current finger spread ÷ the spread at the start.
- `angle` is in degrees, clockwise positive.
- `pressure` runs 0–1.
- `tilt_x` and `tilt_y` are in degrees.

Python:

```python
from surface_touch import SurfaceTouch   # /usr/lib/surface-touch/python

with SurfaceTouch() as st:
    st.set("pen_blocks_touch", True)
    for ev in st.events("gesture"):
        print(ev["gesture"], ev["fingers"], ev.get("direction"))
```

## How touch blocking works

To turn touch off, surface-touchd takes exclusive access to the touchscreen
device (`EVIOCGRAB`), so the desktop stops receiving touch input. It only
switches while no finger is on the screen, so no touch gets stuck "down" in
the desktop.

iptsd's own `DisableOnStylus` option works one layer lower and can also cut
off a palm that is already resting on the screen. Use it together with
`pen_blocks_touch` if you write with your hand on the glass.

## Bug reports

Record the problem and attach the file. It contains only touch and pen
coordinates.

```sh
sudo surface-touchd --record > touch.rec     # reproduce the problem, then Ctrl-C
surface-touchd --replay touch.rec            # shows what the engine made of it
```

## Tests

`make test` runs three suites:

- Engine unit tests: every gesture type, palm handling, rotation, slot
  reuse, and that one gesture doesn't affect the next.
- JSON and key-name tests.
- An end-to-end replay of a Surface Pro 5 style recording through the
  daemon with bindings. It checks gestures, actions, pen pressure and tilt,
  and that every output line is valid JSON.
