import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import java.io.*;
import java.util.*;

/*
 * Loader (/02, base 0x00200000) code that reads or writes the two option words
 * 0x0021d9dc and 0x0021d9e0 that the MENU Options screen toggles, plus the
 * sound-driver byte 0x25a00483 that SelectMenuEntry case 5 writes.
 *
 * Method: find every 32-bit literal in the image equal to a target value, then
 * every mov.l @(disp,PC),Rn whose pool address is one of those literals, then
 * decompile the function that holds the instruction. Per function the output
 * lists the literal words it loads (address, pool address, value) and its
 * decompiled C. No other instruction text is printed.
 *
 * Args: [outPath]  default docs/ghidra-loader-options.txt
 *
 * Standalone: no other script in tools/ is needed.
 */
public class DumpLoaderOptions extends GhidraScript {
  private static final long[] TARGETS = {0x0021d9dcL, 0x0021d9e0L, 0x25a00483L};

  private String hex(long v) { return String.format("%08x", v & 0xffffffffL); }

  public void run() throws Exception {
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0]
        : "F:/MrBonesPort/docs/ghidra-loader-options.txt";
    Memory mem = currentProgram.getMemory();
    long lo = mem.getMinAddress().getOffset();
    long hi = mem.getMaxAddress().getOffset();

    DecompInterface d = new DecompInterface();
    d.openProgram(currentProgram);
    try (PrintWriter out = new PrintWriter(new File(outPath))) {
      out.println("Program " + currentProgram.getName()
          + " language " + currentProgram.getLanguageID()
          + " imagebase " + currentProgram.getImageBase()
          + " sha256 " + currentProgram.getExecutableSHA256());

      /* 1. literal pool words equal to a target */
      Map<Long, Long> pools = new TreeMap<>(); /* pool address -> value */
      out.println("-- literal words equal to a target --");
      for (long a = lo & ~3L; a + 3 <= hi; a += 4) {
        long w = mem.getInt(toAddr(a)) & 0xffffffffL;
        for (long t : TARGETS) {
          if (w == t) {
            pools.put(a, w);
            out.println(hex(a) + " = " + hex(w));
          }
        }
      }

      /* 2. mov.l @(disp,PC),Rn that load one of those pool words */
      Map<Long, Long> hits = new TreeMap<>(); /* insn address -> pool address */
      out.println("-- mov.l @(disp,PC),Rn loads of those words --");
      for (long ia = lo & ~1L; ia + 1 <= hi; ia += 2) {
        int h = mem.getShort(toAddr(ia)) & 0xffff;
        if ((h & 0xf000) != 0xd000) continue;
        long pool = ((ia + 4) & ~3L) + (h & 0xff) * 4L;
        if (pools.containsKey(pool)) {
          hits.put(ia, pool);
          out.println(hex(ia) + " r" + ((h >> 8) & 0xf) + " <- pool "
              + hex(pool) + " (" + hex(pools.get(pool)) + ")");
        }
      }
      out.flush();

      /* 3. owning function of each load, decompiled once */
      Set<Long> done = new TreeSet<>();
      for (long ia : hits.keySet()) {
        Address at = toAddr(ia);
        Function f = getFunctionContaining(at);
        if (f == null) {
          long start = ia;
          boolean found = false;
          for (long p = ia; p > ia - 0x600 && p >= lo; p -= 2) {
            if ((mem.getShort(toAddr(p)) & 0xffff) == 0x4f22) { /* sts.l pr,@-r15 */
              start = p;
              while (start - 2 >= lo) {
                int prev = mem.getShort(toAddr(start - 2)) & 0xffff;
                if ((prev & 0xff0f) == 0x2f06) start -= 2; /* mov.l Rm,@-r15 */
                else break;
              }
              found = true;
              break;
            }
          }
          if (found) {
            disassemble(toAddr(start));
            f = createFunction(toAddr(start), "opt_" + hex(start));
          }
        }
        out.println();
        if (f == null) {
          out.println("=== load at " + hex(ia) + ": no function found ===");
          continue;
        }
        long entry = f.getEntryPoint().getOffset();
        if (!done.add(entry)) continue;
        out.println("=== function " + hex(entry) + " (contains load at "
            + hex(ia) + ") ===");

        AddressSet body = new AddressSet(f.getBody());
        out.println("-- literal words loaded in this function --");
        for (long p = body.getMinAddress().getOffset();
             p <= body.getMaxAddress().getOffset(); p += 2) {
          if (!body.contains(toAddr(p))) continue;
          int h = mem.getShort(toAddr(p)) & 0xffff;
          if ((h & 0xf000) != 0xd000) continue;
          long pool = ((p + 4) & ~3L) + (h & 0xff) * 4L;
          if (pool < lo || pool + 3 > hi) continue;
          out.println(hex(p) + " r" + ((h >> 8) & 0xf) + " <- "
              + hex(pool) + " = " + hex(mem.getInt(toAddr(pool)) & 0xffffffffL));
        }
        out.println("-- decompiled --");
        DecompileResults r = d.decompileFunction(f, 180, monitor);
        out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC()
                                           : r.getErrorMessage());
        out.flush();
      }
    } finally {
      d.dispose();
    }
    println("Wrote " + outPath);
  }
}
