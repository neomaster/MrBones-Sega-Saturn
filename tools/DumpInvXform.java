import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;
public class DumpInvXform extends GhidraScript {
  public void run() throws Exception {
    PrintWriter out = new PrintWriter(new File("F:/MrBonesPort/docs/ghidra-inv-xform.txt"));
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    String[][] funs = {{"2078c0","InvXform_2078C0"},{"206b10","WorldXformPool"},{"206ab0","WorldXform_206AB0"}};
    for (String[] x: funs) {
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
    // pool at end of 206AB0
    out.println("\n=== pool 0x206b10 ===");
    for (long va = 0x206b10L; va < 0x206b40L; va += 4) {
      Address a = toAddr(va);
      long v = ((getByte(a)&0xffL)<<24)|((getByte(a.add(1))&0xffL)<<16)|((getByte(a.add(2))&0xffL)<<8)|(getByte(a.add(3))&0xffL);
      out.printf("0x%08X = 0x%08X%n", va, v);
    }
    out.close(); decomp.dispose(); println("wrote inv-xform");
  }
}
