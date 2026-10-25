package de.vw.mib.carplay.vcdiag;

import java.io.File;
import java.io.FileOutputStream;

/**
 * Passive diagnostics for Virtual Cockpit layout research. Called from hooks inserted at the
 * start of stock/omonob methods; never throws and never changes their behaviour.
 * Source level 1.2 for the MU1440 J9/CDC runtime: no StringBuilder, generics or class literals.
 */
public final class VcDiag {
    private static final String LOG = "/ramdisk/cpmap-vcdiag.log";
    /** Created by dio_manager.wrapper.sh for one boot when the toolbox debug flag was set. */
    private static final String DEBUG_MARKER = "/ramdisk/cpmap-debug-active";
    /** /ramdisk is 20 MB and holds the 16 MB video ring: keep log + .1 under 256 KB. */
    private static final long MAX_BYTES = 128L * 1024L;
    private static final Object LOCK = new Object();
    private static boolean started;

    private VcDiag() {
    }

    public static void key(int keyboard, int code, int state, int p4, int p5) {
        ints("key", keyboard, code, state, p4, p5);
        VcLayout.onKey(keyboard, code, state);
    }

    public static void encoder(int keyboard, int encoder, int steps, int p4, int p5) {
        ints("enc", keyboard, encoder, steps, p4, p5);
    }

    public static void bap(int fctId, String method, Object entity) {
        StringBuffer b = new StringBuffer(200);
        b.append("bap fct=").append(fctId).append(' ').append(method).append(' ');
        try {
            b.append(String.valueOf(entity));
        } catch (Throwable t) {
            b.append("toString-failed");
        }
        line(b.toString());
    }

    private static void ints(String kind, int a, int b, int c, int d, int e) {
        StringBuffer s = new StringBuffer(64);
        s.append(kind).append(' ').append(a).append(' ').append(b).append(' ')
            .append(c).append(' ').append(d).append(' ').append(e);
        line(s.toString());
    }

    static void line(String text) {
        try {
            if (!new File(DEBUG_MARKER).exists()) return;
            synchronized (LOCK) {
                File f = new File(LOG);
                if (f.length() > MAX_BYTES) {
                    File old = new File(LOG + ".1");
                    old.delete();
                    f.renameTo(old);
                }
                FileOutputStream out = new FileOutputStream(LOG, true);
                try {
                    StringBuffer s = new StringBuffer(text.length() + 32);
                    if (!started) {
                        started = true;
                        s.append(System.currentTimeMillis()).append(" vcdiag-start v4\n");
                    }
                    s.append(System.currentTimeMillis()).append(' ').append(text).append('\n');
                    out.write(s.toString().getBytes());
                } finally {
                    out.close();
                }
            }
        } catch (Throwable t) {
            // diagnostics must never disturb the HMI
        }
    }
}
