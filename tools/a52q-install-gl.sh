#!/bin/bash
# Put the GPU userland (userland/build-gl.sh -> userland/out/redwood/lib) into the A52's iOS root on
# the phone, writing back only the blocks that change.
#
#   tools/a52q-install-gl.sh            phone in TWRP (adb), needs sudo for the HFS+ mount
#
# 1. reads the iOS root (the first 16 GiB of userdata, research/ios16-root.img's size) from the phone
#    into research/phone-root.img (a few minutes over USB);
# 2. copies it, repairs the journal-dirty volume (fsck.hfsplus: the phone never unmounts it cleanly,
#    and Linux mounts such a volume read-only), mounts it and adds /redwood/lib/*.dylib (root:wheel
#    0755) and DYLD_INSERT_LIBRARIES=/redwood/lib/libsweet-gl.dylib in backboardd's launchd job;
# 3. tools/sparsediff.py writes a sparse image of only the changed blocks, flashed through TWRP's
#    fastbootd onto userdata (the rest of userdata, the iOS data and the log ring, is not touched).
set -euo pipefail
cd "$(dirname "$0")/.."
LIB=userland/out/redwood/lib
OLD=research/phone-root.img
NEW=research/phone-root-gl.img
DIFF=research/phone-root-gl.simg
MNT=/mnt/iosroot
SIZE=$((16 * 1024 * 1024 * 1024))
[ -f "$LIB/libsweet-gl.dylib" ] || { echo "run userland/build-gl.sh first" >&2; exit 1; }
adb devices | grep -q recovery || { echo "the phone is not in TWRP (adb recovery)" >&2; exit 1; }

echo "== reading the iOS root from the phone ($((SIZE >> 30)) GiB)"
adb exec-out "dd if=/dev/block/by-name/userdata bs=4194304 count=$((SIZE / 4194304)) 2>/dev/null" > "$OLD"
[ "$(stat -c %s "$OLD")" = "$SIZE" ] || { echo "short read: $(stat -c %s "$OLD") bytes" >&2; exit 1; }
cp --reflink=auto "$OLD" "$NEW"

echo "== installing into a copy (sudo)"
sudo fsck.hfsplus -fy "$NEW" >/dev/null || true
sudo mkdir -p "$MNT"
sudo mount -t hfsplus -o loop,force "$NEW" "$MNT"
trap 'sudo umount "$MNT" 2>/dev/null || true' EXIT
sudo install -d -o 0 -g 0 -m 0755 "$MNT/redwood" "$MNT/redwood/lib"
sudo install -o 0 -g 0 -m 0755 "$LIB"/*.dylib "$MNT/redwood/lib/"
sudo python3 - "$MNT/System/Library/xpc/launchd.plist" <<'EOF'
import plistlib, sys
path = sys.argv[1]
with open(path, 'rb') as f:
    plist = plistlib.load(f)
done = 0
for job in plist['LaunchDaemons'].values():
    if job.get('Label') == 'com.apple.backboardd':
        env = job.setdefault('EnvironmentVariables', {})
        env['DYLD_INSERT_LIBRARIES'] = '/redwood/lib/libsweet-gl.dylib'
        done += 1
assert done == 1, f'backboardd jobs found: {done}'
with open(path, 'wb') as f:
    plistlib.dump(plist, f, fmt=plistlib.FMT_BINARY)
print('launchd.plist: backboardd gets DYLD_INSERT_LIBRARIES=/redwood/lib/libsweet-gl.dylib')
EOF
ls -l "$MNT/redwood/lib"
sudo umount "$MNT"
trap - EXIT

echo "== changed blocks only"
python3 tools/sparsediff.py "$OLD" "$NEW" "$DIFF"
ls -l "$DIFF"
echo "== flashing them through TWRP fastbootd"
adb reboot fastboot
for i in $(seq 1 40); do fastboot devices | grep -q . && break; sleep 2; done
fastboot flash userdata "$DIFF"
fastboot reboot recovery
echo "done: the iOS root on the phone has the GPU userland"
