# Howdy + Surface IR (Commit 2)

English UI. IR feeds the `Surface-IR-Howdy` v4l2loopback (resolved by card name;
node numbers may change across boots).

## Behaviour

| Client | Webcamd action | IR LED |
|--------|----------------|--------|
| App / browser opens Front or Back | STREAMON that RGB cam | off |
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
(`intel-ipu4p-isys.ko`). Loopback size for IR is native **480×640** after rot90
(no FHD upscale).
