# Howdy + Surface IR (Commit 2) + RGB profiles

English UI. IR feeds the `Surface-IR-Howdy` v4l2loopback (resolved by card name;
`/dev/video*` numbers may change across boots — never hard-code them).

## RGB profiles (SP7-style)

Six RGB loopbacks + IR. Default app camera is **Surface-Front-Standard**.
Only one physical IPU sensor streams at a time; opening any profile preempts.

| Device label | Capture | Output YUYV | Aspect | Target FPS | Notes |
|---|---:|---:|---|---:|---|
| `Surface-Front-Standard` | 1296×972 | 1296×972 | 4:3 | 30 | **default**; no FHD stretch |
| `Surface-Front-HQ` | 2592×1944 | 2592×1944 | 4:3 | 15 | full sensor |
| `Surface-Front-Fast` | 1296×972 | 1296×728 | 16:9 | 60 | center-crop; measured FPS may be lower |
| `Surface-Back-Standard` | 1632×1224 | 1632×1224 | 4:3 | 30 | continuous AE + AF |
| `Surface-Back-HQ` | 3264×2448 | 3264×2448 | 4:3 | 15 | continuous AE + AF |
| `Surface-Back-Fast` | 800×600 | 800×450 | 16:9 | 60 | crop; fallback ~30 if ISYS limited |
| `Surface-IR-Howdy` | 640×480→rot90 | 480×640 | 3:4 | ~30 | Howdy only |

**AE:** one-shot at profile start + continuous (~1 s) for Front/Back; slow AE for IR.
**AF:** Back only (DW9719) — coarse sweep after STREAMON, then micro-hunt every ~2.5 s.
Front / IR: fixed focus.

## Behaviour

| Client | Webcamd action | IR LED |
|--------|----------------|--------|
| App / browser opens a Front/Back profile | STREAMON that RGB profile | off |
| App / browser opens Surface-IR-Howdy | STREAMON IR (preempts RGB) | **steady** |
| Howdy / Add Face Profile / `pam_howdy` | STREAMON IR (preempts RGB) | **steady** |
| Idle | no sensor STREAMON | off |

Only one IPU sensor streams at a time. Opening IR always preempts Front/Back.

**Mutex:** Howdy and preview/browser cannot share IR. When Howdy holds
`Surface-IR-Howdy`, webcamd SIGTERMs other IR readers. Status shows
`ir_owner=howdy|preview` and `ir_led=howdy-steady|steady|off`.

## Enable / enroll

```bash
# SURFACE_WEBCAM_IR=1; Howdy config is a real INI with [core]/[video]
sudo configure-howdy-ir.sh

# Add a face profile (English menu entry):
surface-howdy-add-profile
# or: Applications → Howdy — Add Face Profile

# Dry-run:
sudo surface-howdy-test.sh
# or: sudo howdy --user "$USER" test
```

### PAM

- Lock: `/etc/pam.d/kde` — `pam_howdy.so` (sufficient) before password
- Greeter: `/etc/pam.d/plasmalogin` — same stack
- `surface-ir-wait-auth.sh` opens the IR loopback briefly (≤8 s) so webcamd
  STREAMONs before Howdy; always exits 0 so password still works
- Session hooks must **not** poke ov7251 I2C or pkill loopbacks
- `howdy-gtk --start-auth-ui` is a no-op under PAM (avoids Wayland lock freezes)
- Boot: `surface-howdy-enable.service` sets `disabled=false` after webcamd

### No face model (first boot)

If the user has no model under `/etc/howdy/models/`, Howdy exits in under a
second (`exit 10`) and PAM falls through to password. Login is not blocked.

## Switch smoke test

```bash
sudo test-ir-switch.sh       # preferred name
sudo test-howdy-switch.sh    # same script
```

## Kernel IR timing

Windows ConfigMipiClk layout on CSI-2 1: `+0x34=1155`, `+0x3c=1269`
