#!/bin/bash
# Runs the scripted test build (make PACX_TEST=1 NO_DOLXZ=1) in Dolphin and collects the
# screenshots it takes.
#
#   tools/dolphin_test.sh <dolphin user folder> [seconds]
#
# The user folder needs Config/Dolphin.ini with WiiSDCard, WiiSDCardAllowWrites and
# WiiSDCardEnableFolderSync set, and Config/GFX.ini with
#   [Hacks]
#   XFBToTextureEnable = False
# so the program can read its own frames back. The screenshots end up in
# <user folder>/Load/WiiSDSync/shots as PNG files.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
D=${1:?usage: dolphin_test.sh <dolphin user folder> [seconds]}
T=${2:-180}
SD=$D/Load/WiiSDSync
mkdir -p "$SD/apps"
rsync -a --delete "$ROOT/apps/pacx/" "$SD/apps/pacx/"
cp "$ROOT/pacx-test.elf" "$SD/apps/pacx/boot.elf"
rm -rf "$SD/shots"
/Applications/Dolphin.app/Contents/MacOS/Dolphin -u "$D" -b -e "$SD/apps/pacx/boot.elf" > "$D/dolphin.log" 2>&1 &
P=$!
# the test build powers the console off when it is done, which ends Dolphin
for i in $(seq 1 "$T"); do
  kill -0 $P 2>/dev/null || break
  sleep 1
done
kill $P 2>/dev/null || true
wait $P 2>/dev/null || true
for f in "$SD"/shots/*.bmp; do
  [ -f "$f" ] && sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null
done
ls "$SD/shots"
