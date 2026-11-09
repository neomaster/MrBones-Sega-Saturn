import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpProbe extends GhidraScript {
  public void run() throws Exception {
    String outPath = "F:/MrBonesPort/docs/ghidra-message-probe.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("imagebase "+currentProgram.getImageBase()+" lang "+currentProgram.getLanguageID());
    String[][] t = {
      {"20b308","MsgProbe_20B308"},
      {"20b264","Fn_20B264"},
      {"2050b0","ObjYeild"}
    };
    for (String[] x: t) {
      Address a = toAddr(x[0]);
      out.println("\n=== "+x[1]+" @ "+a+" ===");
      Function f = getFunctionAt(a);
      if (f==null){ disassemble(a); analyzeChanges(currentProgram); f=createFunction(a,x[1]); }
      if (f==null){ out.println("NO FUN"); continue; }
      out.println("BODY "+f.getBody());
      DecompileResults dr = decomp.decompileFunction(f,60,monitor);
      if (dr.decompileCompleted()) out.println(dr.getDecompiledFunction().getC());
      else out.println("FAIL "+dr.getErrorMessage());
      Instruction insn=getInstructionAt(a);
      for(int i=0;i<40 && insn!=null;i++){ out.println(insn.getAddress()+" "+insn); insn=insn.getNext(); }
    }
    out.close(); decomp.dispose();
    println("wrote "+outPath);
  }
}
