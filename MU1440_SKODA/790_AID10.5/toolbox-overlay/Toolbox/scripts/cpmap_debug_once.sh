#!/bin/ksh
# CarPlay Maps VC - enable the size-capped VC-layout debug logs for the NEXT start only.
# dio_manager.wrapper.sh consumes the flag at start, so the following boot is clean again.

if [ -d /mnt/ota ] && [ -s /ifs/lsd.jxe ]; then
    MMX=""
elif [ -d /net/mmx/mnt/ota ]; then
    MMX="/net/mmx"
else
    echo "FAIL: /mnt/ota not found"
    exit 1
fi

if : > "$MMX/mnt/ota/cpmap-debug-once" 2>/dev/null; then
    echo "VC debug logs: ON for the next start only."
    echo "Reboot the unit, reproduce, then run"
    echo "'Collect VC diagnostics to SD' BEFORE switching off."
else
    echo "FAIL: cannot write $MMX/mnt/ota/cpmap-debug-once"
    exit 1
fi
exit 0
