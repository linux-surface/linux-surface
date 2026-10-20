# Remote desktop (Chrome Remote Desktop / portal)

Helpers for **existing-session** remote desktop on Wayland (KDE Plasma and similar).

## Status (7.2.11)

| Area | Status |
|------|--------|
| Portal grant (no password prompt) | Ready — `crd-grant-remote-desktop` |
| Wait for user Wayland | Ready — `crd-wait-display.sh <user>` |
| Greeter env publish (universal, no hard-coded uid) | Ready — `crd-publish-greeter.sh` |
| Greeter attach + locked-screen share | **WIP** — host still needs MONITOR capture + greeter runtime wiring; do not treat as done |
| Clock skew | Host should use monotonic/network time; do not gate portal on wall-clock |

## Install

```bash
sudo install -m 755 crd-grant-remote-desktop crd-wait-display.sh crd-publish-greeter.sh /usr/local/sbin/
sudo install -m 644 crd-publish-greeter.service /etc/systemd/system/
sudo systemctl enable --now crd-publish-greeter.service
# As the desktop user:
crd-grant-remote-desktop
```

## Rules

- Never hard-code `/run/user/<login-uid>` or a username.
- Greeter and user sessions use **different** `XDG_RUNTIME_DIR`; CRD must read `/run/crd-greeter.env` when attaching to the greeter.
- Prefer ScreenCast **MONITOR** (types=1) when sharing an existing desktop; VIRTUAL is for headless extend.
