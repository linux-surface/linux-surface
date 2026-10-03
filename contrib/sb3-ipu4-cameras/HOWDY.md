# Howdy + Surface IR + empty-password face login

English UI. IR feeds the `Surface-IR-Howdy` v4l2loopback (resolved by card name;
`/dev/video*` numbers may change across boots — never hard-code them).

## Empty-password face login

Submit an **empty** password field to run Howdy. A typed password skips the camera
entirely (stock PAM only). There is no greeter button and no greeter↔camera
ordering dependency.

| Action | What runs |
|--------|-----------|
| Empty password / Enter | `face-pam-auth` → resident Howdy scan → `pam_face_cred` |
| Typed password / Enter | Distro `password-auth` only — no Howdy, no IR wait |
| Connected remote viewer | Face scan skipped (`face-viewer up <name>` or a mark under `/run/face-login/viewers/`) |
| Listening remote host | Face scan still runs (listening alone is not a viewer) |

After a face match, X11-only Plasma helpers (`ksmserver`, tray proxies, kaccess)
get `DISPLAY=:0` via user-unit drop-ins so they do not crash before Xwayland is
imported into the session. Never put `DISPLAY` in a global `environment.d`.

Boot must **never** order the greeter `After=` webcam/IR/Howdy.

Install / refresh:

```bash
sudo bash tools/install-face-login-safe.sh
```

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

## Enable / enroll

```bash
sudo configure-howdy-ir.sh
surface-howdy-add-profile
sudo howdy --user "$USER" test
```

## Switch smoke test

```bash
sudo test-ir-switch.sh
sudo test-howdy-switch.sh
```

## Kernel IR timing

Windows ConfigMipiClk layout on CSI-2 1: `+0x34=1155`, `+0x3c=1269`
