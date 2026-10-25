#!/usr/bin/env bash
# Builds and host-verifies the MU1440/790 CarPlayClusterControls.jar (VC view tracker).
# Inputs (env vars, all required):
#   OMONOB_JAR  omonob v1.2.2 full toolbox: Custom/carplay_maps_vc/payload/profiles/MU1440/790/CarPlayClusterControls.jar
#   STOCK_JAR   MU1440 lsd.jxe (P4526) converted to a jar with luka-dev/jxe2jar
#   JDK8        JDK 8 home (javac -target 1.2 -> class major 46, required by the J9 HMI)
#   ASM         OW2 ASM 9.7 jar
# RELEASE=1 builds the shipped jar; without it the jar also logs BAP NavSD 54/44/45 (research only).
set -euo pipefail
cd "$(dirname "$0")"

: "${OMONOB_JAR:?set OMONOB_JAR}" "${STOCK_JAR:?set STOCK_JAR}" "${JDK8:?set JDK8}" "${ASM:?set ASM}"
OUT=build/CarPlayClusterControls.jar
# RELEASE=1: no patched stock BAP classes (only the key hook for the VC view tracker)
RELEASE_ARG=""; [ "${RELEASE:-0}" = 1 ] && RELEASE_ARG="--release"

rm -rf build && mkdir -p build/helper build/tools
# VcDiag uses only String/StringBuffer/File/FileOutputStream/System, all present in J9 CDC.
"$JDK8/bin/javac" -nowarn -source 1.2 -target 1.2 -d build/helper \
    src/helper/de/vw/mib/carplay/vcdiag/*.java
"$JDK8/bin/javac" -nowarn -cp "$ASM" -d build/tools tools/PatchVcDiag.java tools/VerifyVcDiag.java

SEP=":"; P() { echo "$1"; }
if command -v cygpath >/dev/null 2>&1; then SEP=";"; P() { cygpath -w "$1"; }; fi
"$JDK8/bin/java" -cp "build/tools${SEP}$(P "$ASM")" PatchVcDiag "$(P "$OMONOB_JAR")" "$(P "$STOCK_JAR")" build/helper "$OUT" $RELEASE_ARG
"$JDK8/bin/java" -Xverify:all -cp build/tools VerifyVcDiag "$OUT" "$(P "$STOCK_JAR")"
sha256sum "$OUT"
