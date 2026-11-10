import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpVelWriters2 extends GhidraScript {
  public void run() throws Exception {
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0] : "F:/MrBonesPort/docs/ghidra-vel-writers2.txt";
    PrintWriter out = new PrintWriter(new File(outPath));
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    out.println("Program " + currentProgram.getName()
        + " language " + currentProgram.getLanguageID()
        + " imagebase " + currentProgram.getImageBase()
        + " sha256 " + currentProgram.getExecutableSHA256());
    String[][] funs = {
      {"262fd0", "VelFn_262FD0"},
      {"2636f8", "VelFn_2636F8"},
      {"25e48c", "VelThrust_25E48C"},
      {"25e088", "KillVelAccel_25E088"},
      {"259d28", "CMover_Move_259D28"},
      {"25dbe0", "Fn_25DBE0"},
    };
    for (String[] x : funs) {
      Address a = toAddr(x[0]);
      out.println("\n=== " + x[1] + " @ " + a + " ===");
      Function f = getFunctionAt(a);
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
        if (c.length() > 7000) c = c.substring(0, 7000) + "\n/* truncated */\n";
        out.println(c);
      } else out.println("FAIL");
    }
    /* KillVel pool */
    out.println("\n=== pool 0x25e0a0 ===");
    for (long va = 0x25e0a0L; va < 0x25e0c0L; va += 4) {
      Address a = toAddr(va);
      long v = ((getByte(a)&0xffL)<<24)|((getByte(a.add(1))&0xffL)<<16)|((getByte(a.add(2))&0xffL)<<8)|(getByte(a.add(3))&0xffL);
      out.printf("0x%08X = 0x%08X%n", va, v);
    }
    out.close(); decomp.dispose();
    println("wrote " + outPath);
  }
}
