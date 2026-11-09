import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.HashSet;

/* vel 0x285904/928 writers + NearVel integrate path */
public class DumpVelChain extends GhidraScript {
  public void run() throws Exception {
    String outPath = "F:/MrBonesPort/docs/ghidra-vel-chain.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("program=" + currentProgram.getName() + " base=" + currentProgram.getImageBase());

    out.println("\n=== literal 0x285904 sites + containing fn ===");
    dumpCell(out, decomp, 0x285904L, 12);
    out.println("\n=== literal 0x285928 sites + containing fn ===");
    dumpCell(out, decomp, 0x285928L, 12);
    out.println("\n=== literal 0x285908 (NearVel vel vec?) ===");
    dumpCell(out, decomp, 0x285908L, 8);
    out.println("\n=== literal 0x2858FC (NearVel pos mid?) ===");
    dumpCell(out, decomp, 0x2858FCL, 8);

    String[][] funs = {
      {"25b1c8", "NearVel_25B1C8"},
      {"25e500", "Near_25E500"},
      {"25e620", "Near_25E620"},
      {"25dbe0", "Fn_25DBE0"},
      {"262f80", "Near_262F80_vel"},
      {"263000", "Near_263000_vel"},
      {"263780", "Near_263780_vel"},
      {"26a900", "Near_26A900"},
      {"259d28", "VecAdd_259D28"},
      {"25b938", "Fn_25B938"},
    };
    for (String[] x : funs) {
      Address a = toAddr(x[0]);
      out.println("\n=== " + x[1] + " @ " + a + " ===");
      Function f = getFunctionAt(a);
      if (f == null) f = getFunctionContaining(a);
      if (f == null) {
        disassemble(a);
        analyzeChanges(currentProgram);
        f = createFunction(a, x[1]);
      }
      if (f == null) { out.println("NO FUN"); continue; }
      out.println("ENTRY " + f.getEntryPoint() + " BODY " + f.getBody());
      DecompileResults dr = decomp.decompileFunction(f, 150, monitor);
      if (dr.decompileCompleted()) {
        String c = dr.getDecompiledFunction().getC();
        if (c.length() > 5500) c = c.substring(0, 5500) + "\n/* truncated */\n";
        out.println(c);
      } else out.println("FAIL " + dr.getErrorMessage());
    }

    /* pools at vel literal neighborhoods */
    long[] pools = {0x25b3d0L, 0x25e620L, 0x263070L, 0x2637f0L, 0x26a9a0L};
    for (long p : pools) {
      out.println(String.format("%n=== pool @0x%08X ===", p));
      for (long va = p; va < p + 0x40; va += 4) {
        Address a = toAddr(va);
        long v = ((getByte(a) & 0xffL) << 24) | ((getByte(a.add(1)) & 0xffL) << 16)
               | ((getByte(a.add(2)) & 0xffL) << 8) | (getByte(a.add(3)) & 0xffL);
        out.printf("0x%08X = 0x%08X%n", va, v);
      }
    }
    out.close();
    decomp.dispose();
    println("wrote " + outPath);
  }

  void dumpCell(PrintWriter out, DecompInterface decomp, long cell, int max) throws Exception {
    byte[] be = new byte[] {
      (byte)((cell >> 24) & 0xff), (byte)((cell >> 16) & 0xff),
      (byte)((cell >> 8) & 0xff), (byte)(cell & 0xff)
    };
    Address cur = currentProgram.getMinAddress();
    Address end = currentProgram.getMaxAddress();
    HashSet seen = new HashSet();
    int n = 0;
    while (cur != null && n < max) {
      Address hit = currentProgram.getMemory().findBytes(cur, end, be, null, true, monitor);
      if (hit == null) break;
      /* walk back to find function */
      Function f = null;
      for (long back = 4; back < 0x300 && f == null; back += 2) {
        try { f = getFunctionContaining(toAddr(hit.getOffset() - back)); } catch (Exception e) { break; }
      }
      if (f == null) {
        /* try create from nearby rts-aligned candidates: scan back for 000B 0009 then next insn */
        for (long back = 8; back < 0x200; back += 2) {
          Address c = toAddr(hit.getOffset() - back);
          if (getFunctionAt(c) != null) { f = getFunctionAt(c); break; }
        }
      }
      out.println("literal @" + hit + " fun=" + (f == null ? "none" : f.getName() + " " + f.getEntryPoint()));
      if (f != null && !seen.contains(f.getEntryPoint().toString())) {
        seen.add(f.getEntryPoint().toString());
        DecompileResults dr = decomp.decompileFunction(f, 100, monitor);
        if (dr.decompileCompleted()) {
          String c = dr.getDecompiledFunction().getC();
          if (c.length() > 3200) c = c.substring(0, 3200) + "\n/* truncated */\n";
          out.println(c);
        }
      }
      n++;
      try { cur = hit.add(4); } catch (Exception e) { break; }
    }
  }
}
