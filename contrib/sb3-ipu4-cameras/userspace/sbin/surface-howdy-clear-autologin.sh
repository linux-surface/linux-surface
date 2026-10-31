#!/usr/bin/env bash
# Remove a one-shot Face/Howdy autologin so the next boot still asks.
set -u
python3 - <<'PY'
from pathlib import Path
for p in (Path("/etc/plasmalogin.conf.d/10-surface-face.conf"), Path("/etc/plasmalogin.conf")):
    if not p.exists():
        continue
    text = p.read_text()
    if p.name == "plasmalogin.conf" and "surface-howdy-oneshot" not in text and "surface-face-oneshot" not in text:
        continue
    if p.name.endswith(".conf") and p.parent.name == "plasmalogin.conf.d":
        p.write_text(
            "[Autologin]\nUser=\nSession=\nRelogin=false\n\n"
            "[Users]\nRememberLastUser=true\nRememberLastSession=true\n"
        )
        continue
    lines = [ln for ln in text.splitlines()
             if not ln.startswith("User=")
             and not ln.startswith("Session=")
             and "surface-howdy-oneshot" not in ln
             and "surface-face-oneshot" not in ln]
    p.write_text("\n".join(lines) + "\n")
PY
exit 0
