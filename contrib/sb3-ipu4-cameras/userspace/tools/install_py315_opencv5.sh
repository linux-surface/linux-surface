#!/usr/bin/env bash
# Install Python 3.15 + OpenCV5 (cp315 vendor) for surface-webcam.
# Run with: sudo bash tools/install_py315_opencv5.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
V314="$ROOT/vendor"
V315="$ROOT/vendor-py315"
WHEELDIR="${TMPDIR:-/tmp}/surface-wheels315"

echo "==> Python 3.15 (system)"
dnf install -y python3.15 python3.15-libs || true
if [[ -x /usr/bin/python3.15 ]]; then
  /usr/bin/python3.15 --version
else
  echo "WARN: python3.15 missing — webcamd stays on python3 (3.14+) with fallback"
fi

echo "==> OpenCV 5 wheels"
# 3.14 vendor (abi3 opencv + cp314 numpy) — fallback interpreter
python3 -m pip install --upgrade --target="$V314" 'opencv-python==5.0.0.93' 'numpy>=2'

# 3.15 needs cp315 numpy; unpack via download (pip can't cross-install).
mkdir -p "$WHEELDIR" "$V315"
python3 -m pip download -d "$WHEELDIR" --python-version 3.15 --only-binary=:all: \
  'numpy>=2' 'opencv-python==5.0.0.93'
python3 - <<PY
import zipfile, pathlib, shutil
dest = pathlib.Path("$V315")
if dest.exists():
    shutil.rmtree(dest)
dest.mkdir(parents=True)
for whl in pathlib.Path("$WHEELDIR").glob("*.whl"):
    print("extract", whl.name)
    with zipfile.ZipFile(whl) as z:
        z.extractall(dest)
print("vendor-py315 ready")
PY
if [[ -x /usr/bin/python3.15 ]]; then
  /usr/bin/python3.15 -c "import sys; sys.path.insert(0,'$V315'); import numpy,cv2; print('3.15 ok', numpy.__version__, cv2.__version__)"
fi

echo "==> Point service at python3.15 (fallback: python3)"
UNIT=/etc/systemd/system/surface-webcam.service
if [[ -x /usr/bin/python3.15 ]]; then
  PY=/usr/bin/python3.15
  PYP="$V315:$ROOT/lib:/opt/surface-cameras/lib"
else
  PY=/usr/bin/python3
  PYP="$V314:$ROOT/lib:/opt/surface-cameras/lib"
fi
# Rewrite ExecStart + PYTHONPATH in place (keep rest of unit).
if [[ -f "$UNIT" ]]; then
  sed -i "s|^Environment=PYTHONPATH=.*|Environment=PYTHONPATH=$PYP|" "$UNIT" || true
  sed -i "s|^ExecStart=.*|ExecStart=$PY -u /opt/surface-cameras/tools/surface_webcamd.py|" "$UNIT"
  systemctl daemon-reload
fi

echo "==> raw_hold SHM binary → /opt + /dev/shm"
gcc -O2 -Wall -o "$ROOT/tools/raw_hold" "$ROOT/tools/raw_hold.c"
install -m 755 "$ROOT/tools/raw_hold" /opt/surface-cameras/tools/raw_hold
install -m 755 "$ROOT/tools/raw_hold" /dev/shm/surface-raw_hold
install -m 755 "$ROOT/tools/surface_webcamd.py" /opt/surface-cameras/tools/surface_webcamd.py
install -m 644 "$ROOT/lib/"*.py /opt/surface-cameras/lib/
# Mirror vendor trees next to /opt for root without home bind quirks.
mkdir -p /opt/surface-cameras/vendor /opt/surface-cameras/vendor-py315
cp -a "$V314"/. /opt/surface-cameras/vendor/ 2>/dev/null || true
cp -a "$V315"/. /opt/surface-cameras/vendor-py315/ 2>/dev/null || true
sysctl -w fs.pipe-max-size=16777216 || true

echo "==> restart"
systemctl reset-failed surface-webcam.service || true
systemctl restart surface-webcam.service
sleep 3
systemctl is-active surface-webcam.service
journalctl -u surface-webcam.service -n 20 --no-pager
echo "OK — expect: runtime py=3.15.x cv2=5.0.0"
echo "Note: /usr/bin/python stays 3.14 (Fedora default). Cameras use python3.15 via systemd."
