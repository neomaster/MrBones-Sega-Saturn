import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.util.*;

/*
 * Ending brain (CEndLevel::Brain) of one stage, as docs/fmv-routes.md reads it.
 *
 * Writes, in this order:
 *   1. a provenance line: program, language, image base, SHA-256 of the binary;
 *   2. the first instructions at the brain address, so the prologue can be checked
 *      against the .MAP (the map address may be a few bytes off);
 *   3. "=== <stage> <addr> ===" and the decompiled C, the same body layout as the
 *      existing dumps in docs/ghidra-fmv-brains/;
 *   4. the literal pool the function reads: every data address referenced from the
 *      body, with its 32-bit value, tagged "loader" when the value falls between
 *      0x00200000 and the image base (the loader /02 lives there: _LoadLevel 0020b4ac,
 *      _PlayDuckMovie 0020b61c, _PlayNewDuckMovie 0020b714).
 *
 * Args: <stage> <brainAddr> [outPath]
 *   stage      DirNameTable leaf, e.g. 04RUNWAY
 *   brainAddr  revalidated prologue, hex, e.g. 00256280
 *   outPath    default F:/MrBonesPort/docs/ghidra-fmv-brains/<stage>.txt
 *
 * Standalone: no other script in tools/ is needed.
 */
public class DumpFmvBrain extends GhidraScript {
  private static final long LOADER_BASE = 0x00200000L;

  public void run() throws Exception {
    String[] args = getScriptArgs();
    if (args.length < 2) {
      printerr("usage: DumpFmvBrain <stage> <brainAddr> [outPath]");
      return;
    }
    String stage = args[0];
    Address brain = toAddr(args[1]);
    String outPath = args.length > 2 ? args[2]
        : "F:/MrBonesPort/docs/ghidra-fmv-brains/" + stage + ".txt";

    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    try (PrintWriter out = new PrintWriter(new File(outPath))) {
      out.println("Program " + currentProgram.getName()
          + " language " + currentProgram.getLanguageID()
          + " imagebase " + currentProgram.getImageBase()
          + " sha256 " + currentProgram.getExecutableSHA256());

      Function fun = getFunctionAt(brain);
      if (fun == null) {
        disassemble(brain);
        analyzeChanges(currentProgram);
        fun = createFunction(brain, "fmv_brain_" + stage);
      } else if (fun.getName().startsWith("FUN_")) {
        fun.setName("fmv_brain_" + stage, SourceType.USER_DEFINED);
      }

      out.println("-- prologue --");
      Instruction insn = getInstructionAt(brain);
      for (int i = 0; i < 6 && insn != null; i++) {
        out.println(insn.getAddress() + " " + insn.toString());
        insn = insn.getNext();
      }
      out.println();

      out.println("=== " + stage + " " + args[1] + " ===");
      if (fun == null) {
        out.println("no function");
        return;
      }
      out.println();
      DecompileResults res = decomp.decompileFunction(fun, 180, monitor);
      out.println(res.decompileCompleted() ? res.getDecompiledFunction().getC()
                                           : res.getErrorMessage());
      out.println();
      dumpLiteralPool(out, fun);
    } finally {
      decomp.dispose();
    }
    println("Wrote " + outPath);
  }

  /* Every data address the body reads, once, in address order, with its value. */
  private void dumpLiteralPool(PrintWriter out, Function fun) throws Exception {
    TreeMap<Address, Integer> pool = new TreeMap<>();
    InstructionIterator it = currentProgram.getListing().getInstructions(fun.getBody(), true);
    while (it.hasNext()) {
      Instruction insn = it.next();
      for (Reference ref : insn.getReferencesFrom()) {
        Address to = ref.getToAddress();
        if (!ref.getReferenceType().isData()) continue;
        if (!currentProgram.getMemory().contains(to)) continue;
        if ((to.getOffset() & 3L) != 0L) continue;
        if (fun.getBody().contains(to)) continue;
        pool.put(to, getInt(to));
      }
    }
    long imageBase = currentProgram.getImageBase().getOffset();
    out.println("-- literal pool (" + pool.size() + ") --");
    for (Map.Entry<Address, Integer> e : pool.entrySet()) {
      long value = e.getValue() & 0xffffffffL;
      String tag = "";
      if (value >= LOADER_BASE && value < imageBase) tag = " loader";
      else if (currentProgram.getMemory().contains(toAddr(value))) tag = " stage";
      out.printf("%s %08x%s%n", e.getKey(), value, tag);
    }
  }
}
