import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class DumpCollFine extends GhidraScript {
  public void run() throws Exception {
    String outPath = "F:/MrBonesPort/docs/ghidra-coll-fine.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("imagebase="+currentProgram.getImageBase()+" lang="+currentProgram.getLanguageID());

    String[][] targets;
    String name = currentProgram.getName();
    if (name != null && name.toLowerCase().contains("loader")) {
      targets = new String[][] {
        {"213fa0", "BitExtract_213FA0"},
        {"213fd0", "BitExtract_alt"}
      };
    } else {
      targets = new String[][] {
        {"256a68", "CollFn_256A68"},
        {"25c900", "CollProbe_25C900"},
        {"25c694", "WalkAnim_25C694"},
        {"25c7d4", "Fn_25C7D4"}
      };
      out.println("\n=== pool CollFn @ 00256AD0 ===");
      Address p = toAddr("256ad0");
      for (int i = 0; i < 8; i++) {
        out.println(p.add(i*2) + " = " + getShort(p.add(i*2)));
      }
      out.println("ptr ad8=" + getInt(toAddr("256ad8")));
      out.println("ptr adc=" + getInt(toAddr("256adc")));
      out.println("\n=== refs / nearby inits mentioning 285A6C ===");
      /* dump words at known offset cells if present in image (unlikely BSS) */
    }

    for (String[] x : targets) {
      Address a = toAddr(x[0]);
      out.println("\n=== " + x[1] + " @ " + a + " ===");
      Function f = getFunctionAt(a);
      if (f == null) {
        disassemble(a);
        analyzeChanges(currentProgram);
        f = createFunction(a, x[1]);
      }
      if (f == null) { out.println("NO FUN"); continue; }
      out.println("BODY " + f.getBody());
      DecompileResults dr = decomp.decompileFunction(f, 90, monitor);
      if (dr.decompileCompleted()) out.println(dr.getDecompiledFunction().getC());
      else out.println("FAIL " + dr.getErrorMessage());
      Instruction insn = getInstructionAt(a);
      for (int i = 0; i < 60 && insn != null; i++) {
        out.println(insn.getAddress() + " " + insn);
        insn = insn.getNext();
      }
    }
    out.close();
    decomp.dispose();
    println("wrote " + outPath);
  }
}
