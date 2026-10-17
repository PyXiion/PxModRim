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
HOME="$home" XDG_CONFIG_HOME="$home/.config" \
  PX_LOG_LEVEL=info timeout "$seconds" "$exe" >"$log" 2>&1
status=$?
cat "$log"

# timeout exits 124 when the app was still running, which is the success case.
if [ "$status" -ne 124 ]; then
  echo "smoke: app exited early with status $status" >&2
  exit 1
fi
if grep -Eiq 'is not installed|cannot load library|failed to load' "$log"; then
  echo "smoke: deployment is missing a Qt component" >&2
  exit 1
fi
if ! grep -q 'Services initialized' "$log"; then
  echo "smoke: startup did not finish" >&2
  exit 1
fi
