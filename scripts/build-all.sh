#!/bin/sh
# Builds Pac the Man X for every system: the Wii, Wii U, Switch, 3DS and Mac.
#
#   scripts/build-all.sh "<.../Pac the Man X.app/Contents/Resources>"
#
# needs devkitPro (devkitPPC, devkitARM, devkitA64, libogc, wut, libctru, libnx and the
# SDL2 portlibs of the Switch and Wii U), Python 3 with Pillow, ffmpeg, and the Xcode
# command line tools for the Mac. Without the Resources folder, the data converted by an
# earlier run is used.
set -e
cd "$(dirname "$0")/.."
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"

if [ -n "$1" ]; then
    python3 -I tools/convert_assets.py "$1"
    python3 -I tools/convert_assets.py --console "$1"
    python3 -I tools/convert_assets.py --mac "$1"
fi

make dist                  # Wii:     apps/pacx/boot.dol
make -C ports/wiiu         # Wii U:   pacx-wiiu.wuhb (and .rpx)
make -C ports/switch       # Switch:  pacx-switch.nro
make -C ports/ctr          # 3DS:     pacx-3ds.3dsx
make -C mac                # Mac:     build-mac/Pac the Man X.app
