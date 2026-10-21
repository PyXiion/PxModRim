#!/usr/bin/env bash
# Starts a deployed build headless and fails on missing Qt pieces: QML modules
# or libraries that deployment dropped.
# The bundle ships only the platform plugin of its OS, so Linux needs a display
# (xvfb-run -a scripts/smoke-package.sh ...).
#
# usage: smoke-package.sh <executable> [seconds]
set -uo pipefail

exe="${1:?usage: smoke-package.sh <executable> [seconds]}"
seconds="${2:-10}"

home="$(mktemp -d)"
log="$home/out.txt"
# No `timeout`: it does not exist on macOS runners.
HOME="$home" XDG_CONFIG_HOME="$home/.config" PX_LOG_LEVEL=info \
  "$exe" >"$log" 2>&1 &
pid=$!
sleep "$seconds"
if ! kill "$pid" 2>/dev/null; then
  status=0
  wait "$pid" || status=$?
  cat "$log"
  echo "smoke: app exited early with status $status" >&2
  exit 1
fi
wait "$pid" 2>/dev/null || true
cat "$log"

if grep -Eiq 'is not installed|cannot load library|failed to load' "$log"; then
  echo "smoke: deployment is missing a Qt component" >&2
  exit 1
fi
if ! grep -q 'Services initialized' "$log"; then
  echo "smoke: startup did not finish" >&2
  exit 1
fi
