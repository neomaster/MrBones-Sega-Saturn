import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

/* Trace tall path: InputBrain tall cell refs, 0x204128 callees, Walk/Stand msg effects. */
public class DumpTallPath extends GhidraScript {
  public void run() throws Exception {
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0] : "F:/MrBonesPort/docs/ghidra-tall-path.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("Program " + currentProgram.getName()
        + " language " + currentProgram.getLanguageID()
        + " imagebase " + currentProgram.getImageBase()
        + " sha256 " + currentProgram.getExecutableSHA256());
    out.println("03CODE base expect 0x00232000");

    /* Absolute pointer literals embedded in 03CODE. */
    long[] ptrs = { 0x28DB8CL, 0x2858BCL, 0x204128L, 0x25D534L };
    String[] names = { "TALL_28DB8C", "CELL_2858BC", "FN_204128", "SWITCH_25D534" };
    for (int p = 0; p < ptrs.length; p++) {
      out.println("\n=== refs to "+names[p]+" ===");
      byte[] be = new byte[] {
        (byte)((ptrs[p]>>24)&0xff), (byte)((ptrs[p]>>16)&0xff),
        (byte)((ptrs[p]>>8)&0xff), (byte)(ptrs[p]&0xff)
      };
      Address start = currentProgram.getMinAddress();
      Address end = currentProgram.getMaxAddress();
      Address cur = start;
      int found = 0;
      while (cur != null && cur.compareTo(end) <= 0 && found < 30) {
        Address hit = currentProgram.getMemory().findBytes(cur, end, be, null, true, monitor);
        if (hit == null) break;
        out.println("literal @"+hit+" VA");
        found++;
        try { cur = hit.add(4); } catch (Exception e) { break; }
      }
      out.println("total="+found);
    }

    String[][] funs = {
      {"25dbe0","Fn_25DBE0_tallish"},
      {"25dd04","Fn_25DD04"},
      {"25c694","Fn_25C694"},
      {"25dfd8","Fn_25DFD8"},
      {"25ee58","Fn_25EE58"},
      {"257320","CPanScroll_257320"},
      {"268a64","RWorldSprite_268A64"},
      {"25d27c","BonesYeild_25D27C"},
    };
    for (String[] x : funs) {
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
