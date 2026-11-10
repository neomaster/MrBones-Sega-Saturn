import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpStride38 extends GhidraScript {
  public void run() throws Exception {
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0] : "F:/MrBonesPort/docs/ghidra-stride38.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("Program " + currentProgram.getName()
        + " language " + currentProgram.getLanguageID()
        + " imagebase " + currentProgram.getImageBase()
        + " sha256 " + currentProgram.getExecutableSHA256());
    String[][] t = {
      {"2675ba","Near_mov38_a"},
      {"26eb0e","Near_mov38_b"},
      {"267500","Fn_near_7500"},
      {"26eb00","Fn_near_eb00"},
      {"25df78","Mplaynext"},
      {"25dea4","Mnext"},
      {"25df00","Manimend"}
    };
    for (String[] x : t) {
      Address a = toAddr(x[0]);
      Function f = getFunctionContaining(a);
      if (f == null) {
        disassemble(a); analyzeChanges(currentProgram);
        f = getFunctionContaining(a);
        if (f == null) f = createFunction(a, x[1]);
      }
      out.println("\n=== "+x[1]+" probe@"+a+" fun="+(f==null?"null":f.getName()+"@"+f.getEntryPoint())+" ===");
      if (f == null) continue;
      out.println("BODY "+f.getBody());
      DecompileResults dr = decomp.decompileFunction(f, 120, monitor);
      if (dr.decompileCompleted()) {
        String c = dr.getDecompiledFunction().getC();
        if (c.length()>12000) c=c.substring(0,12000)+"\n/*trunc*/\n";
        out.println(c);
      }
    }
    out.close(); decomp.dispose();
    println("wrote "+outPath);
  }
}
