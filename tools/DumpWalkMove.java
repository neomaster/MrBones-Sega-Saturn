import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpWalkMove extends GhidraScript {
  public void run() throws Exception {
    String outPath = "F:/MrBonesPort/docs/ghidra-walk-move.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    String[][] t = {
      {"25db1c","WalkMove_25DB1C"},
      {"25c694","WalkAnim_25C694"},
      {"25ee58","WalkStep_25EE58"},
      {"25d27c","BonesYeild_25D27C"},
      {"2570e4","PanHelper_2570E4"},
    };
    for (String[] x : t) {
      Address a = toAddr(x[0]);
      out.println("\n=== "+x[1]+" @ "+a+" ===");
      Function f = getFunctionAt(a);
      if (f == null) { disassemble(a); analyzeChanges(currentProgram); f = createFunction(a, x[1]); }
      if (f == null) { out.println("NO FUN"); continue; }
      out.println("BODY "+f.getBody());
      DecompileResults dr = decomp.decompileFunction(f, 120, monitor);
      if (dr.decompileCompleted()) out.println(dr.getDecompiledFunction().getC());
      else out.println("FAIL");
    }
    out.close(); decomp.dispose();
    println("wrote "+outPath);
  }
}
