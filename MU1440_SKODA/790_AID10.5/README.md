# MU1440 Škoda / 790 AID 10.5

- Firmware gate: `/ifs/lsd.jxe` = **55,840,933 bytes** (P4526)
- Cockpit views: own CarPlay layout per view (0 map only, 1 wide map + card, 2 map + centred card)
- View tracking: short View press counted on release, long press ignored, roller (OK) hold ~3 s in view 0 re-syncs
- Steering controls (upstream): roller zoom, long press map switch
- Profile core: `payload/profiles/MU1440/790/core.so` (upstream, unchanged)

## Two ways to install

**MQB Toolbox:** copy [`toolbox-overlay/`](./toolbox-overlay/) over the upstream full toolbox v1.2.2, run
*Update Toolbox*, select view 0 on the cockpit, then *Install 790* and reboot. Details in the [main README](../../README.md).

**Standalone (MMX shell):** select view 0 on the cockpit, copy this directory to the SD card, then from this
directory run `/bin/ksh ./run-install.sh`. The installer checks the exact firmware/profile again and does not
reboot by itself; reboot once after `CPMAP_INSTALL_OK`. `status-carplay-maps.sh` and `restore-carplay-maps.sh`
work as upstream (restore removes `/mnt/app/carplaymaps`, including the layout shim).

Debug logs for one boot without the toolbox menu: `touch /mnt/ota/cpmap-debug-once`, reboot, reproduce, then copy
`/ramdisk/cpmap-vcdiag.log*` and `/ramdisk/cpmap-vclayout-shim.log*` before switching off.

## Files changed against upstream v1.2.2

| File | Change |
| --- | --- |
| `payload/common/libcpmap-vclayout.so` | new: per-view CarPlay layout shim (source `src/vclayout`) |
| `payload/common/CarPlayClusterControls.jar` | upstream MU1440 jar + View tracker (source `src/vcdiag`) |
| `payload/common/dio_manager.wrapper.sh` | preloads the shim; upstream logs to `/dev/null`; one-boot debug flag |
| `payload/common/carplay-cluster-supervisor` | bridge log path → `/dev/null` (one string at `0x47c0`) |
| `install.sh`, `payload/common/install-carplay-maps.sh` | installs the shim; sets the stored view to 0 |
