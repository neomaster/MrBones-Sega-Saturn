import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpStandSwitch extends GhidraScript {
  public void run() throws Exception {
    String outPath = "F:/MrBonesPort/docs/ghidra-stand-switch.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("03CODE base expect 0x00232000; imagebase="+currentProgram.getImageBase());
    String[][] t = {
      {"25d534","BrainSwitch_25D534"},
      {"25c900","Fn_25C900"},
      {"25c9b8","Fn_25C9B8"},
      {"25dd58","Fn_25DD58"},
      {"26073c","Fn_26073C"},
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
      if (f == null) { out.println("NO FUN"); continue; }
      out.println("BODY "+f.getBody());
      DecompileResults dr = decomp.decompileFunction(f, 90, monitor);
      if (dr.decompileCompleted()) out.println(dr.getDecompiledFunction().getC());
      else out.println("FAIL "+dr.getErrorMessage());
    }
    out.close();
    decomp.dispose();
    println("wrote "+outPath);
  }
}
