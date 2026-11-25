#!/bin/bash
set -euo pipefail
T=/opt/google/chrome-remote-desktop/pipewire.conf.template
cp -a "$T" "$T.bak-surface-$(date +%s)"
if ! grep -q 'api.v4l2' "$T"; then
  sed -i '/support\.\*.*= support\/libspa-support/a\    api.v4l2.*      = v4l2/libspa-v4l2' "$T"
fi
grep -n 'spa-libs\|v4l2\|support' "$T" | head -20
