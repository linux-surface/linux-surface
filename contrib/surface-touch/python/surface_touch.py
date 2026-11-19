# SPDX-License-Identifier: GPL-2.0-or-later
"""Python client for the surface-touchd API (JSON lines over a Unix socket).

    from surface_touch import SurfaceTouch

    with SurfaceTouch() as st:
        print(st.info())
        st.set("pen_blocks_touch", True)
        for ev in st.events("gesture", "pen"):
            if ev["event"] == "gesture" and ev["gesture"] == "swipe":
                print(ev["fingers"], "finger swipe", ev["direction"])

No dependencies beyond the standard library.
"""

import json
import os
import socket

DEFAULT_SOCKET = "/run/surface-touchd/socket"
EVENT_TYPES = ("touch", "pen", "gesture", "state")


class SurfaceTouchError(Exception):
    """The daemon rejected a request."""


class SurfaceTouch:
    def __init__(self, path=None, timeout=5.0):
        self.path = path or os.environ.get("SURFACE_TOUCHD_SOCKET", DEFAULT_SOCKET)
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect(self.path)
        self._buf = b""
        self._pending = []  # events that arrived while waiting for a reply

    def close(self):
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # -- low level ---------------------------------------------------------

    def _readline(self):
        while b"\n" not in self._buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("surface-touchd closed the connection")
            self._buf += chunk
        line, self._buf = self._buf.split(b"\n", 1)
        return json.loads(line)

    def request(self, cmd, **fields):
        """Send a command and return its reply (raises SurfaceTouchError)."""
        msg = dict(cmd=cmd, **fields)
        self.sock.sendall(json.dumps(msg).encode() + b"\n")
        while True:
            obj = self._readline()
            if "reply" in obj:
                if not obj.get("ok"):
                    raise SurfaceTouchError(obj.get("error", "request failed"))
                return obj
            self._pending.append(obj)

    # -- commands ----------------------------------------------------------

    def ping(self):
        return self.request("ping")

    def info(self):
        """Devices, version, event and gesture types."""
        return self.request("info")

    def get(self):
        """All settings plus live state (pen_near, contacts, touch_blocked)."""
        return self.request("get")

    def set(self, key, value):
        """Change a setting: rotation, touch_enabled, pen_blocks_touch,
        bindings, tap_ms, double_tap_ms, long_press_ms, swipe_ms,
        pen_release_ms, tap_move, swipe_min, edge_margin, pinch_start,
        rotate_start, palm_major."""
        return self.request("set", key=key, value=value)

    def contacts(self):
        """Fingers currently on the screen."""
        return self.request("contacts")["contacts"]

    def bindings(self):
        return self.request("bindings")["bindings"]

    def subscribe(self, *types):
        bad = [t for t in types if t not in EVENT_TYPES + ("all",)]
        if bad:
            raise ValueError(f"unknown event types: {bad}")
        return self.request("subscribe", events=list(types))

    def unsubscribe(self, *types):
        return self.request("unsubscribe", events=list(types))

    def events(self, *types, timeout=None):
        """Subscribe to the given event types (default: all) and yield events
        forever (or until no event arrives for `timeout` seconds)."""
        self.subscribe(*(types or ("all",)))
        self.sock.settimeout(timeout)
        while True:
            if self._pending:
                yield self._pending.pop(0)
                continue
            try:
                obj = self._readline()
            except socket.timeout:
                return
            if "reply" not in obj:
                yield obj


def _main():
    import sys

    with SurfaceTouch() as st:
        if len(sys.argv) > 1 and sys.argv[1] == "info":
            print(json.dumps(st.info(), indent=2))
            return
        types = sys.argv[1:] or ["gesture", "state"]
        for ev in st.events(*types):
            print(json.dumps(ev))


if __name__ == "__main__":
    _main()
