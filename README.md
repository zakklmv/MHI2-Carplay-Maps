# CarPlay Maps VC for Škoda MU1440 / 790

[![License](https://img.shields.io/badge/license-GPL--3.0-blue?style=flat-square)](LICENSE)
[![Upstream](https://img.shields.io/badge/upstream-omonob%2FMHI2--Carplay--Maps-555?style=flat-square)](https://github.com/omonob/MHI2-Carplay-Maps)



A fork of [omonob/MHI2-Carplay-Maps](https://github.com/omonob/MHI2-Carplay-Maps) v1.2.2 for one exact setup:
**Škoda MHI2 (MIB2.5 High) MU1440, firmware `MHI2_ER_SKG13_P4526` (`/ifs/lsd.jxe` = 55,840,933 bytes) with the
790 AID 10.5" Virtual Cockpit.** The CarPlay map with a turn card is shown in the cockpit and follows the cockpit view.

---


### What this fork adds

| | Upstream v1.2.2 | This fork |
| --- | --- | --- |
| Turn card | map only | map + turn card, centred |
| Cockpit views | one layout for all views | own CarPlay layout for each Virtual Cockpit view |
| UI scale | stock | smaller (display announced as 300 × 112 mm) |


Layout per cockpit view (the View button cycles five views, always in the same order):

| View | Cockpit | CarPlay |
| --- | --- | --- |
| 0 | classic dials, map between them | map only, no turn card |
| 1 | large map | wide map, turn card at the side |
| 2 | map + side panels | map, turn card centred on top |
| 3 | range | (no map in the cockpit) |
| 4 | sport dial | (no map in the cockpit) |

### How the view is tracked

The 790 cockpit never reports its view to the head unit, so the head unit counts the steering-wheel **View** button:

- a short press is counted when the button is **released**;
- a **long press** (the cockpit opens its view-settings menu, the view does not change) is ignored;
- the count is stored in `/mnt/ota/cpmap-vclayout` and survives reboots;
- installing sets the count to view 0, so **install with the cockpit in view 0** (classic dials);
- if the count ever drifts: select view 0 on the cockpit and **hold the roller (OK) for about 3 s**.

### Installation (MQB Toolbox)

1. Download full toolbox
   [`from releases`](https://github.com/zakklmv/MHI2-Carplay-Maps/releases)
   and extract it to the SD-card root.
2. In MQB Toolbox run **Update Toolbox** once (new menu entries).
3. Select **view 0** (classic dials) on the cockpit.
4. *Customization → CarPlay Maps VC* → **Install 790**. Do not press View until the reboot.
5. After `CPMAP_INSTALL_OK` reboot the unit once.



### Known limitations

- Google Maps sometimes does not bring the turn card back when switching from view 0 to view 1/2 (Apple Maps does).
  A UI reset on every view change (`stopUI` → `showUI`) did not help, so it is left to the app.
- Roller map zoom and different layouts works  over cable; through wireless CarPlay adapters it does not (adapter limitation).
- Tested only on one car: MU1440 P4526 + 790, iPhone, Apple Maps and Google Maps.


### License and disclaimer

GNU GPL v3, see [LICENSE](LICENSE). Upstream attribution and license terms are kept. MQB Toolbox, Škoda,
Volkswagen, Apple CarPlay and other names and trademarks belong to their owners.

Writing infotainment files can make the unit fail to boot. Check the firmware and cluster, keep stable power,
have a recovery route, never install or debug while driving. This independent project is not affiliated with
Škoda Auto, Volkswagen AG or Apple Inc. and comes without warranty.
