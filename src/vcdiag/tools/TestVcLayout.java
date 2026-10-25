import de.vw.mib.carplay.vcdiag.VcDiag;

/** Host scenario for VcLayout. Arg "boot1" or "boot2" (second run simulates a reboot). */
public class TestVcLayout {
    static void key(int code, int state) {
        VcDiag.key(4, code, state, 0, 1);
    }

    static void press(int code, long holdMs) throws Exception {
        key(code, 1);
        Thread.sleep(holdMs);
        key(code, 0);
    }

    public static void main(String[] a) throws Exception {
        if ("boot1".equals(a[0])) {
            press(35, 100);        // fresh install: default 0 -> 1
            press(40, 200);        // short roller press -> no sync
            press(40, 1600);       // 1.6 s hold -> no sync any more
            press(35, 100);        // 1 -> 2
            press(40, 3100);       // 3.1 s hold -> re-anchor layout=0
            press(35, 100);        // 1
            press(35, 100);        // 2
            press(35, 100);        // 3
            key(35, 1);            // long View: DSI long state -> cluster menu, no change
            key(35, 3);
            key(35, 4);
            key(35, 0);
            press(35, 900);        // long View by time only -> no change
            key(35, 0);            // stray release without press -> no change
            VcDiag.key(13, 78, 1, 0, 1); // other keyboard -> ignored
        } else {
            press(35, 100);        // 3 -> 4 (restored from persistence)
            press(35, 100);        // 4 -> 0 wrap
            key(40, 1);            // DSI long states: 3 and 4 do not sync, 5 does (once)
            key(40, 3);
            key(40, 4);
            press(35, 100);        // 0 -> 1 (no sync happened yet)
            key(40, 1);
            key(40, 3);
            key(40, 4);
            key(40, 5);
            key(40, 5);
            key(40, 0);
        }
    }
}
