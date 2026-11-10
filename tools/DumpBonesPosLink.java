import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

/* Link Bones position object to RWorldSprite: decompile fn near 0x25D400 pool and sprite init. */
public class DumpBonesPosLink extends GhidraScript {
  public void run() throws Exception {
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0] : "F:/MrBonesPort/docs/ghidra-bones-pos-link.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("Program " + currentProgram.getName()
        + " language " + currentProgram.getLanguageID()
        + " imagebase " + currentProgram.getImageBase()
        + " sha256 " + currentProgram.getExecutableSHA256());

    /* Force-create and decompile candidates that own the 0x25D3xx / 0x25Cxxx pools. */
    String[][] funs = {
      {"25d27c", "BonesYeild_25D27C"},
      {"25d000", "Cand_25D000"},
      {"25cf00", "Cand_25CF00"},
      {"25ce00", "Cand_25CE00"},
      {"25cd00", "Cand_25CD00"},
      {"25cc00", "Cand_25CC00"},
      {"25cb00", "Cand_25CB00"},
      {"25ca00", "Cand_25CA00"},
      {"25c900", "CollProbe_25C900"},
      {"25b1c8", "NearVelEntry_25B1C8"},
      {"25b380", "NearVelAdd_25B380"},
      {"259d28", "VecAdd_259D28"},
      {"2686c0", "Fn_2686C0"},
      {"257320", "CPanScroll_257320"},
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
        if (c.length() > 5000) c = c.substring(0, 5000) + "\n/* truncated */\n";
        out.println(c);
      } else out.println("FAIL " + dr.getErrorMessage());
    }

    /* Dump pool 0x25D3F0..0x25D450 as BE32 */
    out.println("\n=== pool 0x25D3F0..0x25D450 ===");
    for (long va = 0x25d3f0L; va < 0x25d450L; va += 4) {
      Address a = toAddr(va);
      long v = ((getByte(a) & 0xffL) << 24) | ((getByte(a.add(1)) & 0xffL) << 16)
             | ((getByte(a.add(2)) & 0xffL) << 8) | (getByte(a.add(3)) & 0xffL);
      out.printf("0x%08X = 0x%08X%n", va, v);
    }

    /* Who stores pointer-to-0x2858BC? Scan for mov.l imm patterns via literal sites
       and decompile containing function if we can find one by creating from nearby. */
    out.println("\n=== create+decomp around 2858BC literal sites (code before pool) ===");
    long[] sites = {0x25b53cL, 0x25b8d8L, 0x25c7ccL, 0x25d400L, 0x25d5e4L, 0x2604d0L, 0x26a6bcL};
    for (long site : sites) {
      /* walk back up to 0x200 bytes for rts/nop or known entry */
      Address hit = toAddr(site);
      Function f = null;
      for (long back = 4; back < 0x200 && f == null; back += 2) {
        Address cand = toAddr(site - back);
        f = getFunctionContaining(cand);
        if (f != null) break;
        /* try create at common entries */
      }
      out.println("site @" + hit + " containing=" + (f == null ? "none" : f.getName() + " " + f.getEntryPoint()));
      if (f != null) {
        DecompileResults dr = decomp.decompileFunction(f, 90, monitor);
        if (dr.decompileCompleted()) {
          String c = dr.getDecompiledFunction().getC();
          if (c.length() > 2000) c = c.substring(0, 2000) + "\n/* truncated */\n";
          out.println(c);
        }
      }
    }

    out.close();
    decomp.dispose();
    println("wrote " + outPath);
  }
}
