#!/bin/bash

# Utility functions for timestamped logging
log() {
  echo "[surface-pen-watcher] $1"
}

logError() {
  echo "[surface-pen-watcher] ERROR: $1" >&2
}

log "Starting Surface Pen proximity monitor..."

# Wait up to 5 seconds for udev to create the persistent symlink after power cycle
MAX_ATTEMPTS=10
ATTEMPT=0
log "Waiting for udev to create symlink /dev/input/surface-pen after power cycle..."
while [ $ATTEMPT -lt $MAX_ATTEMPTS ]; do
  if [ -e "/dev/input/surface-pen" ]; then
    log "Symlink /dev/input/surface-pen created."
    break
  fi
  ATTEMPT=$((ATTEMPT + 1))
  sleep 0.3
done

if [ ! -e "/dev/input/surface-pen" ]; then
  logError "Symlink /dev/input/surface-pen not found."
  exit 1
fi

log "Attached to symlink: /dev/input/surface-pen"
log "Listening for pen proximity-out events..."

# Open libinput stdout on File Descriptor 3
exec 3< <(exec libinput debug-events --device /dev/input/surface-pen 2>/dev/null)

while read -u 3 -r line; do
  if echo "$line" | grep -q "proximity-in"; then
    continue
  fi
  if echo "$line" | grep -q "proximity-out"; then
    log "Tool left proximity; checking for immediate pen -> eraser handoff..."
    HANDOFF_DETECTED=false
    while read -u 3 -t 0.2 -r buffered_line; do
      if echo "$buffered_line" | grep -q "proximity-in"; then
        log "Handoff detected; Aborting reset."
        HANDOFF_DETECTED=true
        break
      fi
    done
    if [ "$HANDOFF_DETECTED" = true ]; then
      continue
    fi
    log "No new proximity-in detected; pen completely removed from screen."
    if /usr/local/bin/reset-surface-touch.sh; then
      log "USB interface succesfully reset."
    else
      logError "Failed to execute reset-surface-touch.sh"
    fi
    log "Exiting watcher to re-attach after bus re-enumeration."
    exit 0
  fi
done

# Clean up file descriptor on exit
exec 3<&-

