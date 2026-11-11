import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;
import java.io.*;

public class DumpDuckPlayer extends GhidraScript {
  public void run() throws Exception {
    String[][] targets = {
      {"06010f50", "playLoop"}, {"06011380", "DKAVIFilePlay"},
      {"060114c8", "duckplay"}, {"0601238c", "getVCRControl"},
      {"060123e4", "waitWhilePaused"}, {"060126f8", "duckSetVBlank"}
    };
    DecompInterface decompiler = new DecompInterface();
    decompiler.openProgram(currentProgram);
    try (PrintWriter out = new PrintWriter(new File(getScriptArgs()[0]))) {
      out.println("Program " + currentProgram.getName() + " " + currentProgram.getLanguageID()
          + " imageBase " + currentProgram.getImageBase() + " memoryStart "
          + currentProgram.getMemory().getMinAddress() + " sha256 " + currentProgram.getExecutableSHA256());
      for (String[] target : targets) {
        Address address = toAddr(target[0]);
        Function function = getFunctionAt(address);
        if (function == null) {
          disassemble(address);
          analyzeChanges(currentProgram);
          function = createFunction(address, target[1]);
        }
        out.println("\n=== " + target[1] + " @ " + address + " ===");
        if (function == null) { out.println("NO FUNCTION"); continue; }
        out.println("BODY " + function.getBody());
        for (Reference reference : getReferencesTo(address))
          out.println("REF " + reference.getFromAddress() + " " + reference.getReferenceType());
        DecompileResults result = decompiler.decompileFunction(function, 90, monitor);
        out.println(result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
        Instruction instruction = getInstructionAt(address);
        while (instruction != null && function.getBody().contains(instruction.getAddress())) {
          out.println(instruction.getAddress() + " " + instruction);
          instruction = instruction.getNext();
        }
      }
    } finally { decompiler.dispose(); }
  }
}
