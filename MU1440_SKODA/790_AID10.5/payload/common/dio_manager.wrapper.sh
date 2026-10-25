#!/bin/sh

ROOT=/mnt/app/carplaymaps
REAL="$ROOT/stock_first/dio_manager.real"
CORE="$ROOT/core.so"
SEC="$ROOT/libcarplay-security-bridge.so"
PAIR_ROOT="$ROOT/security_bridge"
PAIR_HELPER="$PAIR_ROOT/carplay-pairing-helper"
PAIR_CORE="$PAIR_ROOT/libCarPlayPairingCore.so"
SUPERVISOR="$ROOT/carplay-cluster-supervisor"
BRIDGE="$ROOT/altscreen-most-bridge"
LOG=/ramdisk/carplaymaps-launcher.log
PIDFILE=/ramdisk/carplay-dio-manager.pid

log_line() {
    echo "$1" >> "$LOG" 2>/dev/null || true
}

if test ! -x "$REAL" || test ! -r "$CORE"; then
    log_line "fatal=runtime-missing"
    exit 127
fi

export IPL_CONFIG_DIR=/etc/eso/production
export IPL_CONFIG_DIR_DIO_MANAGER=/etc/eso/production
export PATH=/proc/boot:/sbin:/bin:/usr/bin:/usr/sbin:/armle/usr/bin:/armle/usr/sbin:/armle/sbin:/armle/bin
export LD_LIBRARY_PATH=/mnt/app/root/lib-target:/eso/lib:/mnt/app/usr/lib:/mnt/app/armle/lib:/mnt/app/armle/lib/dll:/mnt/app/armle/usr/lib
export CARPLAY_ALTSCREEN_ENABLE=1
# /ramdisk is 20 MB and holds the 16 MB video ring; unbounded diagnostic logs filled it and froze
# the cluster stream after ~20 min (omonob/MHI2-Carplay-Maps#48). core.so and security-bridge
# logs stay off. The toolbox flag /mnt/ota/cpmap-debug-once enables only the small, size-capped
# VC-layout logs for exactly one boot: it is consumed here and replaced by a /ramdisk marker.
export CARPLAY_ALTSCREEN_LOG=/dev/null
DEBUG_ONCE=/mnt/ota/cpmap-debug-once
DEBUG_ACTIVE=/ramdisk/cpmap-debug-active
if test -e "$DEBUG_ONCE"; then
    rm -f "$DEBUG_ONCE" 2>/dev/null
    : > "$DEBUG_ACTIVE" 2>/dev/null
    log_line "debug-logs=on-this-boot"
fi
export CARPLAY_ALTSCREEN_CAPTURE_DIR=/ramdisk
unset CARPLAY_CLUSTER_INTRO
unset CARPLAY_CLUSTER_INTRO_FPS

echo $$ > "$PIDFILE" 2>/dev/null || true

if test -x "$SUPERVISOR" && test -x "$BRIDGE"; then
    (
        unset LD_PRELOAD
        "$SUPERVISOR" >/dev/null 2>&1
    ) &
    log_line "cluster-supervisor=start-requested"
fi

PRELOAD="$CORE"
if test -r "$SEC"; then
    PRELOAD="$CORE:$SEC"
    export CARPLAY_SECURITY_BRIDGE_LOG=/dev/null
    if test -x "$PAIR_HELPER" && test -r "$PAIR_CORE"; then
        (
            unset LD_PRELOAD
            export LD_LIBRARY_PATH="$PAIR_ROOT:$LD_LIBRARY_PATH"
            export CARPLAY_PAIRING_STORE="$PAIR_ROOT"
            "$PAIR_HELPER" --server >/dev/null 2>&1
        ) &
        log_line "pairing-helper=start-requested"
    fi
fi

# Per-VC-view cluster geometry (MU1440/790). Must precede core.so: core.so reaches libairplay
# with dlsym(RTLD_NEXT), so the chain is libairplay -> vclayout -> core.so -> libairplay.
VCLAYOUT="$ROOT/libcpmap-vclayout.so"
if test -r "$VCLAYOUT"; then
    PRELOAD="$VCLAYOUT:$PRELOAD"
    log_line "vclayout=preload"
fi

export LD_PRELOAD="$PRELOAD"
log_line "launch=carplay-maps-video-only"
exec "$REAL" "$@"

log_line "fatal=dio-manager-exec-failed"
exit 126
