#!/bin/bash
# Build the GPU userland for the iOS root into userland/out/redwood/lib (not in git):
#   libsweet-gl.dylib     ours (userland/sweet_gl_loader.c): inserted into backboardd, loads the bridge
#   libredwood-gl.dylib   HackiOS's OpenGL ES bridge behind EAGLContext (MIT, OOSEX GANG)
#   libOSMesa.8.dylib     Mesa 25.0.7 with freedreno, built by HackiOS's tools/mk-mesa.sh
#
#   userland/build-gl.sh [HACKIOS_DIR]     (default ~/hackios/hackios-xnu-kernel)
#
# HACKIOS_DIR is a HackiOS workspace (common/setup.sh) in which tools/mk-mesa.sh has been run and
# userland/obj/libredwood-gl.dylib built (see NOTES.md, stage 6). Needs theos (~/theos) with the
# iOS toolchain and iPhoneOS16.5.sdk.
set -euo pipefail
cd "$(dirname "$0")"
H=${1:-$HOME/hackios/hackios-xnu-kernel}
THEOS=${THEOS:-$HOME/theos}
BIN=$THEOS/toolchain/linux/iphone/bin
SDK=$THEOS/sdks/iPhoneOS16.5.sdk
OUT=out/redwood/lib
mkdir -p "$OUT"
"$BIN/clang" -target arm64-apple-ios16.0 -isysroot "$SDK" -O1 -Wall -dynamiclib \
	-install_name /redwood/lib/libsweet-gl.dylib -framework IOKit \
	-o "$OUT/libsweet-gl.dylib" sweet_gl_loader.c
cp "$H/userland/obj/libredwood-gl.dylib" "$OUT/"
cp "$H/xnu-deps/build/mesa/out/libOSMesa.8.dylib" "$OUT/"
# iOS maps only signed code: an ad-hoc signature on each (after install_name_tool, which voids one)
for f in "$OUT"/*.dylib; do
	"$BIN/ldid" -S "$f"
done
for f in "$OUT"/*.dylib; do
	printf '%-28s %9d bytes  %s\n' "$(basename "$f")" "$(stat -c %s "$f")" "$("$BIN/otool" -D "$f" | tail -1)"
done
