#!/bin/bash
# Surface Pro 11: Blank display before s2idle to prevent hang.
# This runs as a systemd service BEFORE systemd-suspend.service,
# while GNOME is still running and can process the lock request.

# Lock all sessions — GNOME will blank the display via DPMS
loginctl lock-sessions 2>/dev/null

# Wait for the display to fully power down
sleep 2
