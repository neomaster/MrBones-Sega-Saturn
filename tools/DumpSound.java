import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.program.model.symbol.*;
import java.io.*;

/* Dump loader sound.o / sequence helpers and RUNWAY call-site probes. */
public class DumpSound extends GhidraScript {
  private static final String[][] LOADER_TARGETS = {
    {"00204128", "SoundEvent"},
    {"002041d0", "SoundStartSequence"},
    {"00200468", "SND_Init"},
    {"00200b38", "SND_StartPcm"},
    {"002006b0", "SND_StartSeq"},
    {"002004cc", "SoundStartSequence_mapoff"}, /* if map-relative wrong */
  };
  private static final String[][] CODE_TARGETS = {
    {"0025ff98", "Stand_Brain"},
    {"00260370", "BonesWalk"},
    {"0025d27c", "BonesYeild"},
    {"0025c694", "BonesPushOutOfWalls"},
    {"0026d68c", "TallBrain"},
    {"0026d784", "TallSet"},
  };

  public void run() throws Exception {
    String n = currentProgram.getName().toLowerCase();
    boolean loader = n.contains("02") || n.contains("loader");
    String outPath = loader
        ? "F:/MrBonesPort/docs/ghidra-sound-loader.txt"
        : "F:/MrBonesPort/docs/ghidra-sound-code.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("Program " + currentProgram.getName()
        + " language " + currentProgram.getLanguageID()
        + " imagebase " + currentProgram.getImageBase());
    String[][] targets = loader ? LOADER_TARGETS : CODE_TARGETS;
    for (String[] t : targets) {
      dumpFun(out, decomp, t[0], t[1]);
    }
    /* Scan for PC literals pointing at SoundEvent / SoundStartSequence. */
    out.println("\n=== refs to 0x00204128 / 0x002041d0 (word scan) ===");
    scanWord(out, 0x00204128);
    scanWord(out, 0x002041d0);
    out.println("\n=== refs to SoundFunctionList-ish / common sound IDs ===");
    /* Common small sound event indices often appear as mov #imm */
    out.close();
    decomp.dispose();
    println("Wrote " + outPath);
  }

  private void dumpFun(PrintWriter out, DecompInterface decomp, String hex, String name)
      throws Exception {
    Address addr = toAddr(hex);
    out.println("\n=== " + name + " @ " + addr + " ===");
    Function fun = getFunctionAt(addr);
    if (fun == null) {
      disassemble(addr);
      analyzeChanges(currentProgram);
      fun = createFunction(addr, name);
    }
    if (fun == null) {
      out.println("NO FUNCTION; insn=" + getInstructionAt(addr));
      Instruction insn = getInstructionAt(addr);
      for (int i = 0; i < 32 && insn != null; i++) {
        out.println(insn.getAddress() + " " + insn.toString());
        insn = insn.getNext();
      }
      return;
    }
    out.println("BODY " + fun.getBody());
    DecompileResults dr = decomp.decompileFunction(fun, 120, monitor);
    if (dr.decompileCompleted())
      out.println(dr.getDecompiledFunction().getC());
    else
      out.println("DECOMPILE FAILED " + dr.getErrorMessage());
    out.println("-- listing head --");
    Instruction insn = getInstructionAt(addr);
    for (int i = 0; i < 40 && insn != null; i++) {
      out.println(insn.getAddress() + " " + insn.toString());
      insn = insn.getNext();
    }
  }

  private void scanWord(PrintWriter out, long value) throws MemoryAccessException {
    byte[] be = new byte[] {
      (byte)(value >>> 24), (byte)(value >>> 16),
      (byte)(value >>> 8), (byte)value
    };
    Address start = currentProgram.getMinAddress();
    Address end = currentProgram.getMaxAddress();
    Address found = currentProgram.getMemory().findBytes(start, end, be, null, true, monitor);
    int n = 0;
    while (found != null && n < 40) {
      out.printf("  lit@%s -> %08X%n", found, value);
      n++;
      Address next = found.add(4);
      if (next.compareTo(end) > 0) break;
      found = currentProgram.getMemory().findBytes(next, end, be, null, true, monitor);
    }
    out.println("  count=" + n);
  }
}
