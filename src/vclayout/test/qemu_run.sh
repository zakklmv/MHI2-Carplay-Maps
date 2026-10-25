#!/usr/bin/env bash
# Boots QNX 6.5 (QEMU virt) and runs vcl_test with LD_PRELOAD=shim:core_mock, like dio_manager.
# Run inside qnx65-qemu:8.5 with the vclayout dir at /src and toolchain tests/qemu at /qemu.
set -euo pipefail

W=/tmp/qnxrun
T=/opt/qnx650/target/qnx6/armle-v7
rm -rf "$W" && mkdir -p "$W/fat"
cp build/libcpmap-vclayout.so build/test/libairplay_mock.so build/test/core_mock.so build/test/vcl_test "$W/"
cp /qemu/startup-virt /qemu/libstartup.a /qemu/devc-serdebug "$W/"
cp "$T/lib/libc.so.3" "$T/boot/sys/procnto-smp" "$T/bin/cat" "$W/"

cat > "$W/test.build" <<'EOF'
[linker="(40;1;)qcc -bootstrap -nostdlib -Wl,--no-keep-memory -Vgcc_ntoarmv7%(m!=3,%(m!=6,%(e==0, -EL%)%(e==1, -EB%)%)%)%(h!=0, -Wl,-Ttext -Wl,0x%t%)%(d!=0, -Wl,-Tdata -Wl,0x%d%) -o%o %i %[M -L%^i -Wl,-uinit_%n -lmod_%n%] /tmp/qnxrun/libstartup.a -L/opt/qnx650/target/qnx6/armle-v7/lib -L/opt/qnx650/target/qnx6/armle-v7/usr/lib -llzo -lucl -ldrvr"]
[linker="(40;2;)cp %i %o"]
[image=0x40200000]
[virtual=armle-v7,raw] .bootstrap = {
    startup-virt -v -S
    PATH=:/proc/boot LD_LIBRARY_PATH=:/proc/boot procnto-smp -vvv
}
[+script] .script = {
    procmgr_symlink ../../proc/boot/libc.so.3 /usr/lib/ldqnx.so.2
    procmgr_symlink /dev/shmem /ramdisk
    procmgr_symlink /dev/shmem /mnt/ota
    devc-serdebug -e -F -S
    waitfor /dev/ser1 4
    reopen /dev/ser1
    display_msg "@@QNXRUN-START@@"
    LD_LIBRARY_PATH=/proc/boot LD_PRELOAD=/proc/boot/libcpmap-vclayout.so:/proc/boot/core_mock.so vcl_test
    display_msg "@@SHIMLOG@@"
    cat /ramdisk/cpmap-vclayout-shim.log
    display_msg "@@QNXRUN-END@@"
}
[type=link] /usr/lib/ldqnx.so.2=/proc/boot/libc.so.3
[perms=+r,+x]
libc.so.3
procnto-smp
devc-serdebug
cat
libcpmap-vclayout.so
libairplay_mock.so
core_mock.so
vcl_test
EOF

cd "$W"
MKIFS_PATH="$W:$T/boot/sys:$T/bin:$T/lib:$T/usr/lib" mkifs test.build fat/ifs.bin

( printf '\n'; sleep 4
  printf 'virtio scan\n'; sleep 2
  printf 'fatload virtio 0:1 0x40200000 ifs.bin\n'; sleep 3
  printf 'go 0x40200000\n'; sleep 45 ) | \
  timeout 100 qemu-system-arm -M virt -m 256 -cpu cortex-a15 \
    -bios /qemu/u-boot.bin -drive file=fat:rw:"$W/fat",format=raw,media=disk \
    -nic none -display none -serial stdio > qemu.log 2>&1 || true

awk '/@@QNXRUN-START@@/{sub(/.*@@QNXRUN-START@@/,"");on=1} /@@QNXRUN-END@@/{exit} on&&length($0){print}' qemu.log
grep -oE 'Process [0-9]+ \(vcl_test\) (exited status=[0-9]+|terminated SIG[A-Z]+[^ ]*)' qemu.log | tail -1 || echo "NO EXIT LINE"
