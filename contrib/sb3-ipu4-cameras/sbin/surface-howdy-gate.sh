#!/bin/bash
# IR STREAMON (Howdy/ov7251) kills ISYS on Surface Book 3. RGB goes through webcamd.
# Howdy stays disabled — this is required for a live IPU, not a precaution.
CFG=/etc/howdy/config.ini
[ -f "$CFG" ] || exit 0
sed -i "s/^disabled = .*/disabled = true/" "$CFG" 2>/dev/null || true
echo "howdy PAM: disabled (IR STREAMON kills IPU4 ISYS)"
exit 0
