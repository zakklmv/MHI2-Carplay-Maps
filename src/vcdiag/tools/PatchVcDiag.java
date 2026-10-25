import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.File;
import java.io.InputStream;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.Handle;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;
import org.objectweb.asm.Type;

/**
 * Builds the diagnostic CarPlayClusterControls.jar for MU1440/790:
 *   omonob's MU1440 jar + VcDiag hooks in its DSIKeyPanelDispatcher (key/encoder events)
 *   + stock NavSD MapPresentation(54) / MapViewAndOrientation(44) / MapScale(45) with a
 *     VcDiag.bap() call at the start of setGetProperty/getProperty, downgraded to major 46.
 * Usage: PatchVcDiag omonob.jar mu1440-lsd.jar helperClassesDir out.jar
 */
public class PatchVcDiag {
    static final String H = "de/vw/mib/carplay/vcdiag/VcDiag";
    static final String DISPATCHER = "de/esolutions/fw/dsi/keypanel/DSIKeyPanelDispatcher";
    static final String NAVSD = "de/vw/mib/bap/mqbab2/navsd/functions/";
    static final String PROP_DESC = "(Lde/vw/mib/bap/datatypes/BAPEntity;Lde/vw/mib/bap/functions/PropertyListener;)V";

    public static void main(String[] a) throws Exception {
        boolean release = a.length == 5 && "--release".equals(a[4]);
        if (a.length != 4 && !release)
            throw new IllegalArgumentException("omonob.jar stock.jar helperDir out.jar [--release]");
        Map<String, byte[]> out = new LinkedHashMap<String, byte[]>();

        ZipFile omonob = new ZipFile(a[0]);
        for (ZipEntry e : java.util.Collections.list(omonob.entries())) {
            if (!e.isDirectory()) out.put(e.getName(), read(omonob.getInputStream(e)));
        }
        String dName = DISPATCHER + ".class";
        out.put(dName, patchDispatcher(require(out, dName)));

        ZipFile stock = new ZipFile(a[1]);
        /* --release: no patched stock BAP classes, only the key hook needed for the VC view tracker */
        String[][] navsd = release ? new String[0][]
            : new String[][] {{"MapPresentation", "54"}, {"MapViewAndOrientation", "44"}, {"MapScale", "45"}};
        for (String[] n : navsd) {
            String name = NAVSD + n[0] + ".class";
            ZipEntry e = stock.getEntry(name);
            if (e == null) throw new IllegalStateException("stock class missing: " + name);
            out.put(name, patchNavSd(read(stock.getInputStream(e)), Integer.parseInt(n[1])));
        }

        File helperDir = new File(a[2]);
        addHelpers(helperDir, helperDir, out);

        ZipOutputStream z = new ZipOutputStream(new FileOutputStream(a[3]));
        for (Map.Entry<String, byte[]> e : out.entrySet()) {
            z.putNextEntry(new ZipEntry(e.getKey()));
            z.write(e.getValue());
            z.closeEntry();
        }
        z.close();
        System.out.println("wrote " + a[3] + " entries=" + out.size());
    }

    static byte[] patchDispatcher(byte[] in) {
        final int[] hits = new int[2];
        ClassReader r = new ClassReader(in);
        ClassWriter w = new ClassWriter(r, ClassWriter.COMPUTE_MAXS);
        r.accept(new ClassVisitor(Opcodes.ASM9, w) {
            public MethodVisitor visitMethod(int acc, String name, String desc, String sig, String[] ex) {
                MethodVisitor mv = super.visitMethod(acc, name, desc, sig, ex);
                final String target = "updateKey2".equals(name) ? "key" : "updateEncoder2".equals(name) ? "encoder" : null;
                if (target == null || !"(IIIII)V".equals(desc) || (acc & Opcodes.ACC_STATIC) != 0) return mv;
                hits["key".equals(target) ? 0 : 1]++;
                return new MethodVisitor(Opcodes.ASM9, mv) {
                    public void visitCode() {
                        super.visitCode();
                        for (int i = 1; i <= 5; i++) super.visitVarInsn(Opcodes.ILOAD, i);
                        super.visitMethodInsn(Opcodes.INVOKESTATIC, H, target, "(IIIII)V", false);
                    }
                };
            }
        }, 0);
        if (hits[0] != 1 || hits[1] != 1) throw new IllegalStateException("dispatcher hooks " + hits[0] + "/" + hits[1]);
        System.out.println("patched " + DISPATCHER + " (keeps major " + major(in) + ")");
        return w.toByteArray();
    }

    static byte[] patchNavSd(byte[] in, final int fctId) {
        final int[] set = new int[1];
        ClassReader r = new ClassReader(in);
        ClassWriter w = new ClassWriter(r, ClassWriter.COMPUTE_MAXS);
        final String cls = r.getClassName();
        r.accept(new ClassVisitor(Opcodes.ASM9, w) {
            public void visit(int v, int acc, String name, String sig, String sup, String[] itf) {
                super.visit(Opcodes.V1_2, acc, name, sig, sup, itf);
            }

            public MethodVisitor visitMethod(int acc, String name, String desc, String sig, String[] ex) {
                MethodVisitor mv = super.visitMethod(acc, name, desc, sig, ex);
                mv = new Pre5Guard(mv, cls + "." + name);
                final String kind = "setGetProperty".equals(name) ? "set" : "getProperty".equals(name) ? "get" : null;
                if (kind == null || !PROP_DESC.equals(desc) || (acc & Opcodes.ACC_STATIC) != 0) return mv;
                if ("set".equals(kind)) set[0]++;
                return new MethodVisitor(Opcodes.ASM9, mv) {
                    public void visitCode() {
                        super.visitCode();
                        super.visitIntInsn(Opcodes.BIPUSH, fctId);
                        super.visitLdcInsn(kind);
                        super.visitVarInsn(Opcodes.ALOAD, 1);
                        super.visitMethodInsn(Opcodes.INVOKESTATIC, H, "bap", "(ILjava/lang/String;Ljava/lang/Object;)V", false);
                    }
                };
            }
        }, 0);
        if (set[0] != 1) throw new IllegalStateException(cls + " setGetProperty hooks=" + set[0]);
        byte[] b = w.toByteArray();
        System.out.println("patched " + cls + " fct=" + fctId + " major " + major(in) + " -> " + major(b));
        return b;
    }

    /** Rejects bytecode that a major-46 class cannot legally contain. */
    static final class Pre5Guard extends MethodVisitor {
        final String where;
        Pre5Guard(MethodVisitor mv, String where) {
            super(Opcodes.ASM9, mv);
            this.where = where;
        }
        public void visitLdcInsn(Object v) {
            if (v instanceof Type || v instanceof Handle || !(v instanceof Number || v instanceof String))
                throw new IllegalStateException("post-1.4 ldc in " + where + ": " + v);
            super.visitLdcInsn(v);
        }
        public void visitInvokeDynamicInsn(String n, String d, Handle h, Object... args) {
            throw new IllegalStateException("invokedynamic in " + where);
        }
    }

    static void addHelpers(File root, File dir, Map<String, byte[]> out) throws Exception {
        File[] files = dir.listFiles();
        if (files == null) return;
        for (File f : files) {
            if (f.isDirectory()) {
                addHelpers(root, f, out);
            } else if (f.getName().endsWith(".class")) {
                String name = root.toURI().relativize(f.toURI()).getPath();
                byte[] b = read(new FileInputStream(f));
                if (major(b) > 46) throw new IllegalStateException("helper " + name + " major " + major(b));
                out.put(name, b);
                System.out.println("helper " + name + " major " + major(b));
            }
        }
    }

    static byte[] require(Map<String, byte[]> m, String name) {
        byte[] b = m.get(name);
        if (b == null) throw new IllegalStateException("missing " + name);
        return b;
    }

    static int major(byte[] b) {
        return ((b[6] & 0xff) << 8) | (b[7] & 0xff);
    }

    static byte[] read(InputStream in) throws Exception {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        int n;
        while ((n = in.read(buf)) > 0) o.write(buf, 0, n);
        in.close();
        return o.toByteArray();
    }
}
