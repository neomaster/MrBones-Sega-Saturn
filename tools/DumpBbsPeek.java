import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

/* Decompile BBS peek/unlink and loader Fn_204128 (Stand 0x10700 callee). */
public class DumpBbsPeek extends GhidraScript {
  public void run() throws Exception {
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0] : "F:/MrBonesPort/docs/ghidra-bbs-peek.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("Program " + currentProgram.getName()
        + " language " + currentProgram.getLanguageID()
        + " imagebase " + currentProgram.getImageBase()
        + " sha256 " + currentProgram.getExecutableSHA256());
    out.println("loader /02 base expect 0x00200000");
    String[][] t = {
      {"20b2e8","BbsPeek_20B2E8"},
      {"20b2a4","BbsUnlink_20B2A4"},
      {"20b35c","BbsMatch_20B35C"},
      {"20b308","BbsConsume_20B308"},
      {"20b264","BbsPost_20B264"},
      {"204128","Fn_204128"},
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
      DecompileResults dr = decomp.decompileFunction(f, 90, monitor);
      if (dr.decompileCompleted()) out.println(dr.getDecompiledFunction().getC());
      else out.println("FAIL "+dr.getErrorMessage());
      Instruction insn = getInstructionAt(a);
      for (int i = 0; i < 48 && insn != null; i++) {
        out.println(insn.getAddress()+" "+insn);
        insn = insn.getNext();
      }
    }
    out.close();
    decomp.dispose();
    println("wrote "+outPath);
  }
}
