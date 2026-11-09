import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

/* Decompile BBS match/search and nearby helpers in loader /02. */
public class DumpBbsMatch extends GhidraScript {
  public void run() throws Exception {
    String outPath = "F:/MrBonesPort/docs/ghidra-bbs-match.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("loader /02 base expect 0x00200000; imagebase="+currentProgram.getImageBase());
    String[][] t = {
      {"20b35c","BbsMatch_20B35C"},
      {"20b38c","BbsHelper_20B38C"},
      {"20b264","BbsPost_20B264"},
      {"20b308","BbsProbe_20B308"},
      {"20b1c0","BbsNear_20B1C0"},
      {"20b200","BbsNear_20B200"},
      {"20b400","BbsNear_20B400"},
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
      for (int i = 0; i < 56 && insn != null; i++) {
        out.println(insn.getAddress()+" "+insn);
        insn = insn.getNext();
      }
    }
    out.close();
    decomp.dispose();
    println("wrote "+outPath);
  }
}
