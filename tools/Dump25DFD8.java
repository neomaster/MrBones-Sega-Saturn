import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class Dump25DFD8 extends GhidraScript {
  public void run() throws Exception {
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0] : "F:/MrBonesPort/docs/ghidra-25dfd8.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("Program " + currentProgram.getName()
        + " language " + currentProgram.getLanguageID()
        + " imagebase " + currentProgram.getImageBase()
        + " sha256 " + currentProgram.getExecutableSHA256());
    String[][] t = {{"25dfd8","Fn_25DFD8_cell"},{"25e05c","Near_25E05C"},{"260bc0","MaybeJump"},{"269774","JoypadBrain"}};
    for (String[] x : t) {
      Address a = toAddr(x[0]);
      out.println("\n=== "+x[1]+" @ "+a+" ===");
      Function f = getFunctionAt(a);
      if (f == null) { disassemble(a); analyzeChanges(currentProgram); f = createFunction(a, x[1]); }
      if (f == null) { out.println("NO FUN"); continue; }
      DecompileResults dr = decomp.decompileFunction(f, 90, monitor);
      if (dr.decompileCompleted()) out.println(dr.getDecompiledFunction().getC());
      else out.println("FAIL");
    }
    out.close(); decomp.dispose();
    println("wrote "+outPath);
  }
}
