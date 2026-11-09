import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpJump extends GhidraScript {
  public void run() throws Exception {
    PrintWriter out = new PrintWriter(new File("F:/MrBonesPort/docs/ghidra-jump.txt"));
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    out.println("program=" + currentProgram.getName());

    String[][] funs = {
      {"260bc0", "MaybeJump_260BC0"},
      {"260b00", "NearJump_260B00"},
      {"260a80", "NearJump_260A80"},
      {"25e48c", "VelThrust_25E48C"},
      {"25ff98", "Stand_Brain"},
      {"260244", "StandPool_near"},
      {"25e0f4", "Fn_25E0F4"},
      {"25c9b8", "Fn_25C9B8"},
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
      if (f == null) { out.println("NO FUN insn=" + getInstructionAt(a)); continue; }
      out.println("ENTRY " + f.getEntryPoint() + " BODY " + f.getBody());
      DecompileResults dr = decomp.decompileFunction(f, 150, monitor);
      if (dr.decompileCompleted()) {
        String c = dr.getDecompiledFunction().getC();
        if (c.length() > 6500) c = c.substring(0, 6500) + "\n/* truncated */\n";
        out.println(c);
      } else out.println("FAIL");
    }

    /* Stand extended pool 0x260240..0x2602e0 */
    out.println("\n=== pool Stand 0x260240..0x2602e0 ===");
    for (long va = 0x260240L; va < 0x2602e0L; va += 4) {
      Address a = toAddr(va);
      long v = ((getByte(a)&0xffL)<<24)|((getByte(a.add(1))&0xffL)<<16)|((getByte(a.add(2))&0xffL)<<8)|(getByte(a.add(3))&0xffL);
      out.printf("0x%08X = 0x%08X%n", va, v);
    }
    /* Jump neighborhood pool */
    out.println("\n=== pool near 0x260bc0 ===");
    for (long va = 0x260b80L; va < 0x260c80L; va += 4) {
      Address a = toAddr(va);
      long v = ((getByte(a)&0xffL)<<24)|((getByte(a.add(1))&0xffL)<<16)|((getByte(a.add(2))&0xffL)<<8)|(getByte(a.add(3))&0xffL);
      out.printf("0x%08X = 0x%08X%n", va, v);
    }
    /* BonesWalk param_3 / message pool 0x2606b0 */
    out.println("\n=== pool BonesWalk 0x2606b0 ===");
    for (long va = 0x2606b0L; va < 0x260720L; va += 4) {
      Address a = toAddr(va);
      long v = ((getByte(a)&0xffL)<<24)|((getByte(a.add(1))&0xffL)<<16)|((getByte(a.add(2))&0xffL)<<8)|(getByte(a.add(3))&0xffL);
      out.printf("0x%08X = 0x%08X%n", va, v);
    }
    out.close(); decomp.dispose();
    println("wrote jump");
  }
}
