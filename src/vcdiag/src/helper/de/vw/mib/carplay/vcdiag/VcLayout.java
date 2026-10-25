package de.vw.mib.carplay.vcdiag;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;

/**
 * Tracks the Virtual Cockpit (790) view. The cluster never reports its view to the head unit,
 * but the steering-wheel View key reaches us (MFW keyboard 4, key 35) and the cluster cycles a
 * fixed sequence of COUNT views. A long press of the roller (key 40) anchors the count: the
 * driver sets view 0 (classic dials) and holds the roller.
 *
 * The index is persisted to every writable candidate directory, and mirrored to /ramdisk for
 * native code. Which candidate survives a reboot is not yet known; the log records it.
 */
final class VcLayout {
    static final int KEYBOARD_MFW = 4;
    static final int KEY_VIEW = 35;
    static final int KEY_ROLLER = 40;
    static final int COUNT = 5;
    /** DSI repeats a held key as states 3 (~0.8 s), 4 (~2 s), 5 (~3 s); sync on the third level. */
    static final int SYNC_STATE = 5;
    static final long SYNC_HOLD_MS = 3000L;
    /**
     * A long View press opens the cluster's view settings menu without changing the view, so the
     * view counts on release only, and only for a short press: no DSI long state (3+, ~0.8 s) and
     * released within VIEW_LONG_MS. Measured short presses in the car: 120..290 ms.
     */
    static final int VIEW_LONG_STATE = 3;
    static final long VIEW_LONG_MS = 800L;

    private static final String RAM = "/ramdisk/cpmap-vclayout";
    /** Attempt 002: the only writable location that survives a reboot. */
    private static final String[] PERSIST = {
        "/mnt/ota/cpmap-vclayout",
    };
    private static final Object LOCK = new Object();

    private static boolean loaded;
    private static int layout = -1;
    private static long rollerDownAt = -1L;
    private static boolean rollerSynced;
    private static long viewDownAt = -1L;
    private static boolean viewLong;

    private VcLayout() {
    }

    static void onKey(int keyboard, int code, int state) {
        if (keyboard != KEYBOARD_MFW || (code != KEY_VIEW && code != KEY_ROLLER)) return;
        try {
            synchronized (LOCK) {
                load();
                long now = System.currentTimeMillis();
                if (code == KEY_VIEW) {
                    if (state == 1) {
                        viewDownAt = now;
                        viewLong = false;
                    } else if (state >= VIEW_LONG_STATE) {
                        viewLong = true;
                    } else if (state == 0 && viewDownAt >= 0L) {
                        long held = now - viewDownAt;
                        viewDownAt = -1L;
                        if (viewLong || held >= VIEW_LONG_MS) {
                            VcDiag.line("layout view long-press " + held + "ms ignored (cluster menu)");
                        } else if (layout < 0) {
                            VcDiag.line("layout view-press ignored: not synced (hold roller in view 0)");
                        } else {
                            set((layout + 1) % COUNT, "view " + held + "ms");
                        }
                    }
                } else if (state == 1) {
                    rollerDownAt = now;
                    rollerSynced = false;
                } else if (state == 0) {
                    if (!rollerSynced && rollerDownAt >= 0L && now - rollerDownAt >= SYNC_HOLD_MS) {
                        set(0, "roller-hold " + (now - rollerDownAt) + "ms");
                    }
                    rollerDownAt = -1L;
                    rollerSynced = false;
                } else if (state >= SYNC_STATE && !rollerSynced) {
                    rollerSynced = true;
                    set(0, "roller-long-state " + state);
                }
            }
        } catch (Throwable t) {
            VcDiag.line("layout error " + t);
        }
    }

    private static void set(int value, String why) {
        layout = value;
        VcDiag.line("layout=" + value + " (" + why + ")");
        write(RAM, value);
        for (int i = 0; i < PERSIST.length; i++) {
            VcDiag.line("persist-write " + PERSIST[i] + " " + (write(PERSIST[i], value) ? "ok" : "fail"));
        }
    }

    private static void load() {
        if (loaded) return;
        loaded = true;
        for (int i = 0; i < PERSIST.length; i++) {
            int v = read(PERSIST[i]);
            VcDiag.line("persist-read " + PERSIST[i] + " " + (v < 0 ? "none" : String.valueOf(v)));
            if (v >= 0 && layout < 0) layout = v;
        }
        if (layout < 0) {
            // Nothing saved yet (fresh install): the patch is installed with the cockpit in view 0.
            // A roller hold in view 0 still re-anchors the count if this assumption was wrong.
            layout = 0;
            VcDiag.line("layout=0 (default, nothing persisted)");
        } else {
            VcDiag.line("layout=" + layout + " (loaded)");
        }
        write(RAM, layout);
    }

    private static boolean write(String path, int value) {
        try {
            FileOutputStream out = new FileOutputStream(path);
            try {
                out.write((value + "\n").getBytes());
            } finally {
                out.close();
            }
            return true;
        } catch (Throwable t) {
            return false;
        }
    }

    private static int read(String path) {
        try {
            File f = new File(path);
            if (!f.isFile()) return -1;
            FileInputStream in = new FileInputStream(f);
            try {
                int c = in.read();
                return c >= '0' && c < '0' + COUNT ? c - '0' : -1;
            } finally {
                in.close();
            }
        } catch (Throwable t) {
            return -1;
        }
    }
}
