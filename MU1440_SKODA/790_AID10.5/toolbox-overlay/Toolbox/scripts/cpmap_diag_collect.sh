#!/bin/ksh
# CarPlay Maps VC - copy VC-layout diagnostics from the MMX ramdisk to the toolbox SD:
#   Dump/cpmap-vcdiag/<N>/
# /ramdisk is cleared on reboot: run this BEFORE switching the unit off.

. /eso/hmi/engdefs/scripts/mqb/util_mountsd.sh

if [ -d /net/mmx/ramdisk ]; then
    RD=/net/mmx/ramdisk
    MMX=/net/mmx
else
    RD=/ramdisk
    MMX=""
fi

BASE="$VOLUME/Dump/cpmap-vcdiag"
mkdir -p "$BASE" || { echo "FAIL: cannot create $BASE"; exit 1; }
n=1
while [ -d "$BASE/$n" ]; do
    n=$((n + 1))
done
DST="$BASE/$n"
mkdir -p "$DST" || { echo "FAIL: cannot create $DST"; exit 1; }

found=0
for f in cpmap-vcdiag.log cpmap-vcdiag.log.1 cpmap-vclayout cpmap-vclayout-shim.log cpmap-vclayout-shim.log.1 carplaymaps-launcher.log carplaymaps-video.log carplay-maps-session.log; do
    if [ -f "$RD/$f" ]; then
        cp "$RD/$f" "$DST/" && echo "Copied $f" && found=1
    fi
done
[ "$found" = 1 ] || echo "WARNING: no logs found in $RD"

for p in /var/app/cpmap-vclayout /mnt/efs-persist/cpmap-vclayout /mnt/ota/cpmap-vclayout; do
    if [ -f "$MMX$p" ]; then
        echo "$p `cat "$MMX$p"`" >> "$DST/vclayout-persist.txt"
    else
        echo "$p missing" >> "$DST/vclayout-persist.txt"
    fi
done

grep -n "bootclasspath" "$MMX/mnt/app/eso/hmi/lsd/lsd.sh" > "$DST/lsd-bootclasspath.txt" 2>/dev/null
ls -l "$MMX/mnt/app/eso/hmi/lsd/jars" > "$DST/jars.txt" 2>/dev/null
cat "$MMX/mnt/app/carplaymaps/PROFILE" "$MMX/mnt/app/carplaymaps/VERSION" > "$DST/profile.txt" 2>/dev/null
sync

echo
echo "Saved to SD: Dump/cpmap-vcdiag/$n"
exit 0
