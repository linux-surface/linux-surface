# Remote desktop (Chrome Remote Desktop / portal)

Helpers for **existing-session** remote desktop on Wayland (KDE Plasma and similar).
Target: **7.2.12** (also usable on 6.19 overlays).

## Status (7.2.12)

| Area | Status |
|------|--------|
| Portal grant (no password prompt) | Ready — `crd-grant-remote-desktop` |
| Wait for greeter/user Wayland | Ready — `crd-wait-display.sh [user]` (exits 0 on timeout) |
| Greeter env publish (no hard-coded uid) | Ready — `crd-publish-greeter.sh` (+ optional `CRD_ACL_USER`) |
| Tray icon only while connected | Ready — `crd-tray-icon.so` + `crd-viewer-flag` + `crd-icon.conf` |
| Desktop attach (MONITOR capture) | Works when host requests MONITOR — see `NOTES-MONITOR.md` |
| Greeter / lock-screen share | **WIP** — do not treat as done |

## Install

```bash
sudo install -m 755 \
  crd-grant-remote-desktop crd-wait-display.sh crd-publish-greeter.sh crd-viewer-flag \
  /usr/local/sbin/
sudo install -m 644 crd-publish-greeter.service /etc/systemd/system/
sudo systemctl enable --now crd-publish-greeter.service

# Tray: build preload, then user drop-in + flag watcher
g++ -shared -fPIC -O2 -o crd-tray-icon.so crd-tray-icon.cpp -ldl
sudo install -m 755 crd-tray-icon.so /usr/local/lib/
mkdir -p ~/.config/systemd/user/plasma-xdg-desktop-portal-kde.service.d
install -m 644 crd-icon.conf \
  ~/.config/systemd/user/plasma-xdg-desktop-portal-kde.service.d/crd-icon.conf
install -m 644 crd-viewer-flag.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now crd-viewer-flag.service
systemctl --user restart plasma-xdg-desktop-portal-kde.service

# As the desktop user:
crd-grant-remote-desktop
```

Optional greeter ACL (host user may need Wayland/bus access on the greeter runtime):

```bash
sudo CRD_ACL_USER="$USER" /usr/local/sbin/crd-publish-greeter.sh
```

## Rules

- Never hard-code `/run/user/<login-uid>` or a username.
- Greeter and user sessions use **different** `XDG_RUNTIME_DIR`; CRD must read
  `/run/crd-greeter.env` when attaching to the greeter.
- Prefer ScreenCast **MONITOR** (`types=1`) when sharing an existing desktop;
  VIRTUAL is for headless extend — see `NOTES-MONITOR.md`.
- `crd-wait-display.sh` must not block the host unit past systemd start timeout.

## Tray icon

`crd-tray-icon.so` keeps the KDE portal “Remote control” (`krfb`) SNI **Passive**
unless `crd-viewer-flag` creates `$XDG_RUNTIME_DIR/crd-viewer` while a client is
connected. Without the flag, the icon stays off the panel.
