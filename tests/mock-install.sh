#!/bin/bash
# Offline check of the 003 installer change: the vclayout shim is installed for MU1440/790 only,
# the wrapper prepends it to LD_PRELOAD, and restore removes it. Runs the real installer in its
# CPMAP_TEST_ROOT mode against a synthetic MMX tree. Usage: mock-install.sh <toolbox dir>
set -u
TB=$(cd "$1" && pwd)
PAYLOAD="$TB/Custom/carplay_maps_vc/payload"
WORK=$(mktemp -d)
PASS=0; FAIL=0
check() { if eval "$2"; then echo "  PASS  $1"; PASS=$((PASS+1)); else echo "  FAIL  $1"; FAIL=$((FAIL+1)); fi; }

cat > "$WORK/navcheck" <<'EOF'
#!/bin/sh
case "$1" in --self-test|--payload) exit 0 ;; --has-patch) exit 1 ;; --same) cmp -s "$2" "$3"; exit $? ;; esac
exit 1
EOF
chmod 755 "$WORK/navcheck"

cat > "$WORK/lsd.sh" <<'EOF'
#!/bin/ksh
BASE_DIR=/mnt/app/eso/hmi
FIXBOOTCLASSPATH_EXT=
BOOTCLASSPATH=-Xbootclasspath/a:/ifs/lsd.jxe:/ifs/jre/lib/charconv.zip:$FIXBOOTCLASSPATH_EXT:$BASE_DIR/lsd/jars/GEM.jar
$J9 $VMOPTIONS $BOOTCLASSPATH -jxe $HMI_JXE &
EOF

mkroot() {   # $1 root, $2 lsd.jxe size
    rm -rf "$1"
    mkdir -p "$1/ifs" "$1/ramdisk" "$1/mnt/app/eso/bin/apps" "$1/mnt/app/eso/hmi/lsd/jars"
    truncate -s "$2" "$1/ifs/lsd.jxe"
    head -c 600000 /dev/urandom > "$1/mnt/app/eso/bin/apps/dio_manager"
    chmod 755 "$1/mnt/app/eso/bin/apps/dio_manager"
    cp "$WORK/lsd.sh" "$1/mnt/app/eso/hmi/lsd/lsd.sh"
}
run() { CPMAP_TEST_ROOT="$1" CPMAP_TEST_NAV_CHECK="$WORK/navcheck" ksh "$PAYLOAD/common/install-carplay-maps.sh" "$PAYLOAD" "$2" "$3" 2>&1; }

R="$WORK/root"
echo "[1] MU1440/790"
mkroot "$R" 55840933
OUT=$(run "$R" MU1440 790); echo "$OUT" | sed 's/^/      /'
check "install OK" 'echo "$OUT" | grep -q "^CPMAP_INSTALL_OK$"'
check "shim installed" 'cmp -s "$R/mnt/app/carplaymaps/libcpmap-vclayout.so" "$PAYLOAD/common/libcpmap-vclayout.so"'
check "installer reports CPMAP_VCLAYOUT=INSTALLED" 'echo "$OUT" | grep -q "^CPMAP_VCLAYOUT=INSTALLED$"'
check "MU1440 control jar installed" 'cmp -s "$R/mnt/app/eso/hmi/lsd/jars/CarPlayClusterControls.jar" "$PAYLOAD/profiles/MU1440/790/CarPlayClusterControls.jar"'
check "dio_manager is the new wrapper" 'cmp -s "$R/mnt/app/eso/bin/apps/dio_manager" "$PAYLOAD/common/dio_manager.wrapper.sh"'
check "core.so is stock MU1440/790" 'cmp -s "$R/mnt/app/carplaymaps/core.so" "$PAYLOAD/profiles/MU1440/790/core.so"'

echo "[2] wrapper LD_PRELOAD order"
W="$R/mnt/app/carplaymaps/dio_manager.wrapper.sh"
sed -e "s#^ROOT=/mnt/app/carplaymaps#ROOT=$R/mnt/app/carplaymaps#" \
    -e 's#^exec "\$REAL" "\$@"#echo "PRELOAD=$LD_PRELOAD"; exit 0#' \
    -e 's#^LOG=.*#LOG=/dev/null#' -e 's#^PIDFILE=.*#PIDFILE=/dev/null#' \
    -e 's#"\$SUPERVISOR" >/dev/null 2>&1#:#' -e 's#"\$PAIR_HELPER" --server >/dev/null 2>&1#:#' "$W" > "$WORK/wrapper-test.sh"
P=$(sh "$WORK/wrapper-test.sh" | sed -n 's/^PRELOAD=//p')
echo "      LD_PRELOAD=$P"
check "shim first, then core.so, then security bridge" '[ "$P" = "$R/mnt/app/carplaymaps/libcpmap-vclayout.so:$R/mnt/app/carplaymaps/core.so:$R/mnt/app/carplaymaps/libcarplay-security-bridge.so" ]'

echo "[3] repeat install keeps the shim"
OUT=$(run "$R" MU1440 790)
check "repeat install OK" 'echo "$OUT" | grep -q "^CPMAP_INSTALL_OK$"'
check "shim still installed" '[ -s "$R/mnt/app/carplaymaps/libcpmap-vclayout.so" ]'

echo "[4] MU1102/791 does not get the shim"
mkroot "$R" 55915411
OUT=$(run "$R" MU1102 791)
check "install OK" 'echo "$OUT" | grep -q "^CPMAP_INSTALL_OK$"'
check "no shim" '[ ! -e "$R/mnt/app/carplaymaps/libcpmap-vclayout.so" ]'
P=$(sed -e "s#^ROOT=/mnt/app/carplaymaps#ROOT=$R/mnt/app/carplaymaps#" -e 's#^exec "\$REAL" "\$@"#echo "PRELOAD=$LD_PRELOAD"; exit 0#' -e 's#^LOG=.*#LOG=/dev/null#' -e 's#^PIDFILE=.*#PIDFILE=/dev/null#' -e 's#"\$SUPERVISOR" >/dev/null 2>&1#:#' -e 's#"\$PAIR_HELPER" --server >/dev/null 2>&1#:#' "$R/mnt/app/carplaymaps/dio_manager.wrapper.sh" | sh | sed -n 's/^PRELOAD=//p')
check "LD_PRELOAD without shim" '[ "$P" = "$R/mnt/app/carplaymaps/core.so:$R/mnt/app/carplaymaps/libcarplay-security-bridge.so" ]'

echo "[5] 007: logs off by default, one-shot debug flag"
mkroot "$R" 55840933
OUT=$(run "$R" MU1440 790)
check "install OK" 'echo "$OUT" | grep -q "^CPMAP_INSTALL_OK$"'
check "patched supervisor installed (bridge log -> /dev/null)" '[ "$(sha256sum "$R/mnt/app/carplaymaps/carplay-cluster-supervisor" | cut -c1-16)" = "8e6ac3a85f04c3fa" ]'
W="$R/mnt/app/carplaymaps/dio_manager.wrapper.sh"
sed -e "s#^ROOT=/mnt/app/carplaymaps#ROOT=$R/mnt/app/carplaymaps#" \
    -e 's#^exec "\$REAL" "\$@"#echo "ALT=$CARPLAY_ALTSCREEN_LOG SEC=$CARPLAY_SECURITY_BRIDGE_LOG"; exit 0#' \
    -e 's#^LOG=.*#LOG=/dev/null#' -e 's#^PIDFILE=.*#PIDFILE=/dev/null#' \
    -e 's#"\$SUPERVISOR" >/dev/null 2>&1#:#' -e 's#"\$PAIR_HELPER" --server >/dev/null 2>&1#:#' "$W" > "$WORK/w5.sh"
mkdir -p /mnt/ota /ramdisk
rm -f /mnt/ota/cpmap-debug-once /ramdisk/cpmap-debug-active
E=$(sh "$WORK/w5.sh")
echo "      no flag: $E"
check "core.so and security-bridge logs -> /dev/null" '[ "$E" = "ALT=/dev/null SEC=/dev/null" ]'
check "no debug marker without flag" '[ ! -e /ramdisk/cpmap-debug-active ]'
: > /mnt/ota/cpmap-debug-once
E=$(sh "$WORK/w5.sh")
echo "      with flag: $E"
check "flag consumed" '[ ! -e /mnt/ota/cpmap-debug-once ]'
check "debug marker created" '[ -e /ramdisk/cpmap-debug-active ]'
check "omonob logs still /dev/null in debug boot" '[ "$E" = "ALT=/dev/null SEC=/dev/null" ]'
E=$(sh "$WORK/w5.sh")
check "dio_manager restart in same boot keeps marker" '[ -e /ramdisk/cpmap-debug-active ]'
rm -f /ramdisk/cpmap-debug-active

echo "[6] 009: install anchors the VC view at 0"
mkroot "$R" 55840933
mkdir -p "$R/mnt/ota"; echo 3 > "$R/mnt/ota/cpmap-vclayout"; echo 3 > "$R/ramdisk/cpmap-vclayout"
OUT=$(run "$R" MU1440 790)
check "install OK" 'echo "$OUT" | grep -q "^CPMAP_INSTALL_OK$"'
check "installer reports CPMAP_VC_VIEW=0" 'echo "$OUT" | grep -q "^CPMAP_VC_VIEW=0$"'
check "persisted view reset to 0" '[ "$(cat "$R/mnt/ota/cpmap-vclayout")" = 0 ]'
check "stale RAM view removed" '[ ! -e "$R/ramdisk/cpmap-vclayout" ]'
mkroot "$R" 55915411
mkdir -p "$R/mnt/ota"
OUT=$(run "$R" MU1102 791)
check "MU1102/791 leaves /mnt/ota alone" '[ ! -e "$R/mnt/ota/cpmap-vclayout" ]'

rm -rf "$WORK"
echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
