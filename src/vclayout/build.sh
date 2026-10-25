#!/usr/bin/env bash
# Run inside qnx65-armv7-toolchain:8.5 with this directory mounted at /src.
set -euo pipefail
CC=arm-unknown-nto-qnx6.5.0eabi-gcc
CFLAGS="-O2 -Wall -Wextra -Wno-unused-parameter -fPIC"
mkdir -p build build/test

$CC $CFLAGS -shared src/vclayout_shim.c -o build/libcpmap-vclayout.so -lc

# QEMU test fixtures
$CC $CFLAGS -shared test/airplay_mock.c -o build/test/libairplay_mock.so -lc
$CC $CFLAGS -shared test/core_mock.c -o build/test/core_mock.so -Lbuild/test -lairplay_mock -lc
$CC -O2 -Wall test/vcl_test.c -o build/test/vcl_test -Lbuild/test -lairplay_mock

# SDP readelf is 32-bit and cannot stat files on Windows bind mounts.
cp build/libcpmap-vclayout.so /tmp/shim.so
arm-unknown-nto-qnx6.5.0eabi-readelf -d /tmp/shim.so | grep NEEDED
arm-unknown-nto-qnx6.5.0eabi-nm -D --defined-only /tmp/shim.so | grep -E " T (AirPlay|_init)"
file build/libcpmap-vclayout.so
