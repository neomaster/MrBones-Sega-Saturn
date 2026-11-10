import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryAccessException;
import java.io.*;

/* Dump Stand_Brain / BonesWalk / InputBrain C + key PC-relative words. */
public class DumpRunwayBrains extends GhidraScript {
  private static final String[][] TARGETS = {
    {"0025ff98", "Stand_Brain"},
    {"00260370", "BonesWalk"},
    {"0026a614", "InputBrain"},
    {"0025d27c", "BonesYeild"},
    {"00259d28", "CMover_Move"},
  };

  public void run() throws Exception {
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0] : System.getProperty("mrbones.out",
        "F:/MrBonesPort/docs/ghidra-runway-brains.txt");
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("Program " + currentProgram.getName()
        + " language " + currentProgram.getLanguageID()
        + " imagebase " + currentProgram.getImageBase()
        + " sha256 " + currentProgram.getExecutableSHA256());
    out.println("SHA note: analyze against 03CODE.bin base 0x00200000");
    for (String[] t : TARGETS) {
      Address addr = toAddr(t[0]);
      out.println("\n=== " + t[1] + " @ " + addr + " ===");
      Function fun = getFunctionAt(addr);
      if (fun == null) {
        disassemble(addr);
        analyzeChanges(currentProgram);
        fun = createFunction(addr, t[1]);
      }
      if (fun == null) {
        out.println("NO FUNCTION; insn=" + getInstructionAt(addr));
        continue;
      }
      out.println("BODY " + fun.getBody());
      DecompileResults dr = decomp.decompileFunction(fun, 90, monitor);
      if (dr.decompileCompleted())
        out.println(dr.getDecompiledFunction().getC());
      else
        out.println("DECOMPILE FAILED " + dr.getErrorMessage());
      /* Dump 64 bytes of listing after entry for immediate/literal context. */
      out.println("-- listing head --");
      Instruction insn = getInstructionAt(addr);
      for (int i = 0; i < 48 && insn != null; i++) {
        out.println(insn.getAddress() + " " + insn.toString());
        insn = insn.getNext();
      }
    }
    /* Known InputBrain short slots near 0x0026a6aa (Ghidra names). */
    out.println("\n=== raw words near InputBrain pool 0x26a6a0 ===");
    dumpWords(out, toAddr("0026a6a0"), 24);
    out.println("\n=== raw words near Stand pool gap 0x25ffe0 ===");
    dumpWords(out, toAddr("0025ffe0"), 16);
    out.println("\n=== raw words near BonesWalk pool 0x260490 ===");
    dumpWords(out, toAddr("00260490"), 24);
    out.close();
    decomp.dispose();
    println("Wrote " + outPath);
  }

  private void dumpWords(PrintWriter out, Address start, int count)
      throws MemoryAccessException {
    Address a = start;
    for (int i = 0; i < count; i++) {
      int w = getInt(a);
      out.printf("%s = %08X (%d)%n", a, w, w);
      a = a.add(4);
    }
  }
}
