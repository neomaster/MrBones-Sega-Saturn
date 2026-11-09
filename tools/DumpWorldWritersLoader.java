import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

/* Decompile world-X writers: 206AB0 (loader), WalkStep, NearVel pools, 2858D4 sites. */
public class DumpWorldWritersLoader extends GhidraScript {
  public void run() throws Exception {
    String outPath = "F:/MrBonesPort/docs/ghidra-world-xform-loader.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("program=" + currentProgram.getName() + " base=" + currentProgram.getImageBase()
        + " lang=" + currentProgram.getLanguageID());

    long baseOff = currentProgram.getImageBase().getOffset();
    boolean isLoader = (baseOff == 0x200000L) || currentProgram.getName().contains("02")
        || currentProgram.getName().toUpperCase().contains("LOADER");

    String[][] funs;
    if (isLoader) {
      funs = new String[][] {
        {"206ab0", "WorldXform_206AB0"},
        {"2080d0", "Fn_2080D0"},
        {"204f9c", "Fn_204F9C"},
        {"204e3c", "Fn_204E3C"},
        {"213edc", "Fn_213EDC"},
      };
    } else {
      funs = new String[][] {
        {"25ee58", "WalkStep_25EE58"},
        {"25c7d4", "Fn_25C7D4"},
        {"25b380", "NearVel_25B380"},
        {"25e620", "Near_25E620"},
        {"256a68", "CollFn_256A68"},
        {"25c694", "WalkAnim_25C694"},
        {"268a64", "RWorldSprite_268A64"},
        {"26a614", "InputBrain_26A614"},
        {"25c8c0", "Near_25C8C0_2858D4"},
        {"25cd00", "Near_25CD00_2858D4"},
        {"25d1e0", "Near_25D1E0_2858D4"},
        {"25d420", "Near_25D420_2858D4"},
        {"265b00", "Near_265B00_2858D4"},
        {"2674a0", "Near_2674A0_2858D4"},
        {"26d4c0", "Near_26D4C0_2858D4"},
      };
      out.println("\n=== functions near literal 0x2858D4 ===");
      dumpLiteralSites(out, decomp, 0x2858D4L, 12);
      out.println("\n=== functions near literal 0x2858D8 ===");
      dumpLiteralSites(out, decomp, 0x2858D8L, 8);
      out.println("\n=== functions near literal 0x2858BC (sample) ===");
      dumpLiteralSites(out, decomp, 0x2858BCL, 8);
    }

    for (String[] x : funs) {
      Address a = toAddr(x[0]);
      out.println("\n=== " + x[1] + " @ " + a + " ===");
      Function f = getFunctionAt(a);
      if (f == null) {
        f = getFunctionContaining(a);
      }
      if (f == null) {
        disassemble(a);
        analyzeChanges(currentProgram);
        f = createFunction(a, x[1]);
      }
      if (f == null) {
        out.println("NO FUN insn=" + getInstructionAt(a));
        continue;
      }
      out.println("ENTRY " + f.getEntryPoint() + " BODY " + f.getBody());
      DecompileResults dr = decomp.decompileFunction(f, 150, monitor);
      if (dr.decompileCompleted()) {
        String c = dr.getDecompiledFunction().getC();
        if (c.length() > 6000) c = c.substring(0, 6000) + "\n/* truncated */\n";
        out.println(c);
      } else {
        out.println("FAIL " + dr.getErrorMessage());
      }
    }
    out.close();
    decomp.dispose();
    println("wrote " + outPath);
  }

  void dumpLiteralSites(PrintWriter out, DecompInterface decomp, long cell, int max)
      throws Exception {
    byte[] be = new byte[] {
      (byte)((cell >> 24) & 0xff), (byte)((cell >> 16) & 0xff),
      (byte)((cell >> 8) & 0xff), (byte)(cell & 0xff)
    };
    Address cur = currentProgram.getMinAddress();
    Address end = currentProgram.getMaxAddress();
    int n = 0;
    java.util.HashSet seen = new java.util.HashSet();
    while (cur != null && n < max) {
      Address hit = currentProgram.getMemory().findBytes(cur, end, be, null, true, monitor);
      if (hit == null) break;
      Function f = getFunctionContaining(hit);
      String key = (f == null) ? hit.toString() : f.getEntryPoint().toString();
      out.println("literal @" + hit + " fun=" + (f == null ? "none" : f.getName() + " " + f.getEntryPoint()));
      if (f != null && !seen.contains(key)) {
        seen.add(key);
        DecompileResults dr = decomp.decompileFunction(f, 90, monitor);
        if (dr.decompileCompleted()) {
          String c = dr.getDecompiledFunction().getC();
          if (c.length() > 2800) c = c.substring(0, 2800) + "\n/* truncated */\n";
          out.println(c);
        }
      }
      n++;
      try { cur = hit.add(4); } catch (Exception e) { break; }
    }
  }
}
