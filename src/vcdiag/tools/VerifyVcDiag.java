import java.io.File;
import java.net.URL;
import java.net.URLClassLoader;

/**
 * Host check: load and verify every class of the diagnostic jar against the converted MU1440
 * classes. Run with -Xverify:all. Usage: VerifyVcDiag out.jar mu1440-lsd.jar
 */
public class VerifyVcDiag {
    public static void main(String[] a) throws Exception {
        URLClassLoader l = new URLClassLoader(new URL[] {
            new File(a[0]).toURI().toURL(), new File(a[1]).toURI().toURL()}, null);
        String[] classes = {
            "de.vw.mib.carplay.vcdiag.VcDiag",
            "de.vw.mib.carplay.vcdiag.VcLayout",
            "de.esolutions.fw.dsi.keypanel.DSIKeyPanelDispatcher",
            "de.vw.mib.carplay.clustercontrol.Mhi2ClusterControlBridge",
            "de.vw.mib.bap.mqbab2.navsd.functions.MapPresentation",
            "de.vw.mib.bap.mqbab2.navsd.functions.MapViewAndOrientation",
            "de.vw.mib.bap.mqbab2.navsd.functions.MapScale",
        };
        int fail = 0;
        java.util.zip.ZipFile out = new java.util.zip.ZipFile(a[0]);
        for (String c : classes) {
            if (out.getEntry(c.replace('.', '/') + ".class") == null) {
                System.out.println("SKIP " + c + " (not in jar)");
                continue;
            }
            try {
                Class<?> k = Class.forName(c, false, l);
                String src = k.getProtectionDomain().getCodeSource().getLocation().getPath();
                if (!src.endsWith(new File(a[0]).getName())) throw new IllegalStateException("loaded from " + src);
                k.getDeclaredMethods();
                try {
                    Class.forName(c, true, l);
                } catch (ExceptionInInitializerError e) {
                    // static init needs the live HMI; verification already passed at link time
                }
                System.out.println("OK   " + c);
            } catch (Throwable t) {
                fail++;
                System.out.println("FAIL " + c + " : " + t);
            }
        }
        if (fail != 0) System.exit(1);
    }
}
