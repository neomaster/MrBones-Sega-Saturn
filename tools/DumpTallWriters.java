import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpTallWriters extends GhidraScript {
  public void run() throws Exception {
    String outPath = "F:/MrBonesPort/docs/ghidra-tall-writers.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("03CODE imagebase="+currentProgram.getImageBase());
    String[][] t = {
      {"26d51c","Fn_26D51C"},
      {"26d7f8","Fn_26D7F8"},
      {"25e0f4","Fn_25E0F4"},
      {"2686c0","Fn_2686C0"},
      {"265b2c","Fn_265B2C"},
      {"257320","CPanScroll_257320"},
      {"268a64","RWorldSprite_268A64"},
      {"25c900","CollProbe_25C900"},
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
      if (f == null) { out.println("NO FUN insn="+getInstructionAt(a)); continue; }
      out.println("BODY "+f.getBody());
      DecompileResults dr = decomp.decompileFunction(f, 120, monitor);
      if (dr.decompileCompleted()) out.println(dr.getDecompiledFunction().getC());
      else out.println("FAIL "+dr.getErrorMessage());
      Instruction insn = getInstructionAt(a);
      for (int i = 0; i < 24 && insn != null; i++) {
        out.println(insn.getAddress()+" "+insn);
        insn = insn.getNext();
      }
    }
    out.close(); decomp.dispose();
    println("wrote "+outPath);
  }
}
