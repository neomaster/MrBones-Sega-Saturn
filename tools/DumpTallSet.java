import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpTallSet extends GhidraScript {
  public void run() throws Exception {
    String outPath = "F:/MrBonesPort/docs/ghidra-tall-set.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    String[][] t = {
      {"26d68c","TallBrain_26D68C"},
      {"26d784","TallSet_26D784"},
      {"26d7a0","TallSet_26D7A0"},
      {"26d7b0","TallSet_26D7B0"},
      {"26d790","TallSet_26D790"},
    };
    for (String[] x : t) {
      Address a = toAddr(x[0]);
      out.println("\n=== "+x[1]+" @ "+a+" ===");
      Function f = getFunctionAt(a);
      if (f == null) {
        disassemble(a);
        analyzeChanges(currentProgram);
        f = createFunction(a, x[1]);
      }
      if (f == null) {
        out.println("NO FUN");
        Instruction insn = getInstructionAt(a);
        for (int i = 0; i < 32 && insn != null; i++) {
          out.println(insn.getAddress()+" "+insn);
          insn = insn.getNext();
        }
        continue;
      }
      out.println("BODY "+f.getBody());
      DecompileResults dr = decomp.decompileFunction(f, 120, monitor);
      if (dr.decompileCompleted()) out.println(dr.getDecompiledFunction().getC());
      else out.println("FAIL "+dr.getErrorMessage());
      Instruction insn = getInstructionAt(f.getEntryPoint());
      for (int i = 0; i < 40 && insn != null; i++) {
        out.println(insn.getAddress()+" "+insn);
        insn = insn.getNext();
      }
    }
    out.close(); decomp.dispose();
    println("wrote "+outPath);
  }
}
