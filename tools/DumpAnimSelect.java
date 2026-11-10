import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpAnimSelect extends GhidraScript {
  public void run() throws Exception {
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0] : "F:/MrBonesPort/docs/ghidra-anim-select.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("Program " + currentProgram.getName()
        + " language " + currentProgram.getLanguageID()
        + " imagebase " + currentProgram.getImageBase()
        + " sha256 " + currentProgram.getExecutableSHA256());
    out.println("03CODE anim / frame select / pan scroll");
    String[][] t = {
      {"25df78","Mplaynext"},
      {"25df00","Manimend"},
      {"25dea4","Mnext"},
      {"25dda8","brMCplayer"},
      {"25ddd0","brMCplayer2"},
      {"25def4","Mkey"},
      {"25dec8","Mloop"},
      {"268a64","RWorldSprite"},
      {"257320","CPanScroll_Brain"},
      {"257968","PlayPanAnim"},
      {"257728","PreloadPanAnim"},
      {"25786c","PreBufferFrames"},
      {"2575a0","PreseekPanAnim"},
      {"2577f0","PlayPreload"}
    };
    for (String[] x : t) {
      Address a = toAddr(x[0]);
      out.println("\n=== "+x[1]+" @ "+a+" ===");
      Function f = getFunctionAt(a);
      if (f == null) { disassemble(a); analyzeChanges(currentProgram); f = createFunction(a, x[1]); }
      if (f == null) { f = getFunctionContaining(a); }
      if (f == null) { out.println("NO FUN"); continue; }
      out.println("ENTRY "+f.getEntryPoint()+" BODY "+f.getBody());
      DecompileResults dr = decomp.decompileFunction(f, 120, monitor);
      if (dr.decompileCompleted()) {
        String c = dr.getDecompiledFunction().getC();
        if (c.length() > 10000) c = c.substring(0,10000)+"\n/* trunc */\n";
        out.println(c);
      } else out.println("FAIL "+dr.getErrorMessage());
    }
    out.close(); decomp.dispose();
    println("wrote "+outPath);
  }
}
