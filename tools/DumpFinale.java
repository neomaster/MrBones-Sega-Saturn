import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpFinale extends GhidraScript {
  public void run() throws Exception {
    String[][] targets = {
      {"0028d008", "CreateCFBones"}, {"0028d310", "Brain_CFBones"},
      {"0028d390", "BoltHit_CFBones"}, {"0028d468", "CreateCRing"},
      {"0028d5f4", "Brain_CRing"}, {"0028d700", "ShootRing_CRing"},
      {"0028d8d4", "Brain_CShotManager"}, {"0028dc34", "CreateCBolt"},
      {"0028dd60", "Brain_CBolt"}, {"0028dec8", "Brain_CBlueShot"},
      {"0028e098", "Brain_CShotHit"}, {"0028ccd0", "Brain_CEndLevel"},
      {"0028ce04", "EndLevel"}, {"0028cbc8", "CreateCEndLevel"}
    };
    File outFile = new File(getScriptArgs()[0]);
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    try (PrintWriter out = new PrintWriter(outFile)) {
      out.println("Program " + currentProgram.getName() + " " + currentProgram.getLanguageID());
      for (String[] target : targets) {
        Address addr = toAddr(target[0]);
        out.println("\n=== " + target[1] + " @ " + addr + " ===");
        Function fun = getFunctionAt(addr);
        if (fun == null) {
          disassemble(addr);
          analyzeChanges(currentProgram);
          fun = createFunction(addr, target[1]);
        }
        if (fun == null) { out.println("NO FUNCTION"); continue; }
        out.println("BODY " + fun.getBody());
        DecompileResults result = decomp.decompileFunction(fun, 90, monitor);
        if (result.decompileCompleted()) out.println(result.getDecompiledFunction().getC());
        else out.println("DECOMPILE FAILED " + result.getErrorMessage());
        out.println("-- listing --");
        Instruction insn = getInstructionAt(addr);
        while (insn != null && fun.getBody().contains(insn.getAddress())) {
          out.println(insn.getAddress() + " " + insn.toString());
          insn = insn.getNext();
        }
      }
    } finally { decomp.dispose(); }
  }
}
