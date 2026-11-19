#!/usr/bin/env bash
# End-to-end test: replay a Surface Pro 5 style recording through the daemon
# with a bindings config and check the gestures, actions and pen events.
set -euo pipefail

BIN="${1:?usage: test-replay.sh path/to/surface-touchd}"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$("$BIN" -c "$DIR/fixtures/bindings.conf" --replay "$DIR/fixtures/sp5-gestures.rec")"
fails=0

expect() {
    local what="$1" pattern="$2" n="${3:-1}" got
    got="$(grep -cE -- "$pattern" <<<"$OUT" || true)"
    if [ "$got" != "$n" ]; then
        echo "FAIL: $what: expected $n line(s) matching $pattern, got $got"
        fails=$((fails + 1))
    fi
}

expect "3-finger swipe left" '"gesture":"swipe","fingers":3,.*"direction":"left"'
expect "4-finger swipe up" '"gesture":"swipe","fingers":4,.*"direction":"up"'
expect "2-finger tap" '"gesture":"tap","fingers":2'
expect "edge swipe from the left" '"gesture":"edge_swipe".*"edge":"left"'
expect "pinch begin" '"gesture":"pinch".*"phase":"begin"'
expect "pinch end" '"gesture":"pinch".*"phase":"end","scale":4'
expect "long press" '"gesture":"long_press","fingers":1'
expect "no stray taps" '"gesture":"(tap|double_tap)"' 1
expect "6 bindings ran" '"event":"action"' 6
expect "pen pressure 0.75" '"event":"pen".*"touching":true.*"pressure":0.75'
expect "pen tilt in degrees" '"event":"pen".*"tilt_x":28.6' 5
expect "pen leaves" '"event":"state".*"pen_near":false'
expect "touch down events" '"event":"touch","phase":"down"' 13
expect "touch up events" '"event":"touch","phase":"up"' 13

# every line must be valid JSON
if command -v python3 >/dev/null; then
    python3 -I -c 'import json,sys
for n, l in enumerate(sys.stdin, 1):
    json.loads(l)' <<<"$OUT" || { echo "FAIL: invalid JSON"; fails=$((fails + 1)); }
fi

# config checking
"$BIN" --check -c "$DIR/fixtures/bindings.conf" >/dev/null || { echo "FAIL: --check on a good config"; fails=$((fails + 1)); }
BAD="$(mktemp)"
printf 'bind swipe:3:sideways = key A\nbind tap:2 = key NOPE\nbogus = 1\n' > "$BAD"
if "$BIN" --check -c "$BAD" 2>/dev/null; then
    echo "FAIL: --check accepted a bad config"; fails=$((fails + 1))
fi
rm -f "$BAD"

if [ "$fails" = 0 ]; then echo "all replay tests passed"; else echo "$fails replay test(s) failed"; exit 1; fi
