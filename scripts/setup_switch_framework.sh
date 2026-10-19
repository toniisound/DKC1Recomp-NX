#!/bin/sh
# Adds the Nintendo Switch (libnx) layer to the pinned snesrecomp submodule.
# The upstream pin (elliotttate/snesrecomp) has no Switch support; these
# files come from souldbminerr/snesrecomp ("switch port", 896586a).
# Safe to run more than once.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SR="$ROOT/snesrecomp"
OV="$ROOT/switch/snesrecomp-overlay"

[ -f "$SR/runner/src/common_rtl.c" ] || {
  echo "snesrecomp is empty: run 'git submodule update --init --recursive' first" >&2
  exit 1
}

cp "$OV/runner/switch.mk" "$SR/runner/switch.mk"
mkdir -p "$SR/runner/src/switch"
cp "$OV/runner/src/switch/switch_impl.c" "$OV/runner/src/switch/switch_impl.h" \
   "$SR/runner/src/switch/"

if grep -q '"horizon"' "$SR/runner/src/host_report.c"; then
  echo "host_report.c already patched"
else
  patch -p1 -d "$SR" < "$OV/host_report-switch.patch"
fi
echo "Switch framework layer ready."
