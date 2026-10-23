#!/bin/bash
# Build the Samsung A52 (a52q) boot image: shim + kernel + ADT (--board a52q) + device firmware.
#
#   tools/a52q-boot.sh KERNEL OUT.img [EXTRA BOOT-ARGS...]
#
# The GPU firmware comes from the phone itself (fw-a52q/gpu/: a618_gmu.bin, a630_sqe.fw,
# a615_zap.*; see NOTES.md) and is packed into the payload, never into git.
set -euo pipefail
cd "$(dirname "$0")/.."
kernel=${1:?usage: a52q-boot.sh KERNEL OUT.img [boot-args...]}
out=${2:?usage: a52q-boot.sh KERNEL OUT.img [boot-args...]}
shift 2
args="debug=0xc serial=3 cpus=8 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0"
args+=" unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1"
args+=" launchd_unsecure_cache=1 wdt=-1 sweetfb_noconsole=1 sweetfb_hz=30"
args+=" sweetboard=a52q sweetusb=2 sweetcpufreq=1 $*"
fw=()
[ -d fw-a52q/gpu ] && fw=(--firmware fw-a52q/gpu)
[ -f shim/out/a52q.adt ] || python3 tools/mkadt.py shim/out/a52q.adt --board a52q
python3 tools/mkpayload.py "$kernel" shim/out/a52q.adt shim/out/payload-a52q.bin --cmdline "$args" "${fw[@]}" \
	| grep -E "kernel_end|firmware"
DTB=../dtb/a52q/boot_unpacked/dtb shim/build.sh ../shim/out/payload-a52q.bin >/dev/null
cp shim/out/shim-boot.img "$out"
echo "$out: $(stat -c %s "$out") bytes; boot-args: $args"
