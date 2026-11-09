import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

/* §12o: endgame / skelbar / pause / gang / thrown hit conditions. */
public class DumpEndgame extends GhidraScript {
  private static final String[][] TARGETS = {
    {"00256178", "CreateCEndLevel"},
    {"002561c4", "Message_CEndLevel"},
    {"00256280", "Brain_CEndLevel"},
    {"002563d4", "EndLevel"},
    {"002563ec", "LoadNewLevel"},
    {"0025644c", "CreateCWinBox"},
    {"00256498", "Message_CWinBox"},
    {"00256548", "Brain_CWinBox"},
    {"0025c108", "BonesObjHitChk"},
    {"0025c490", "BonesDoDamage"},
    {"0025cb98", "BonesInairHitCheck"},
    {"0025cfa0", "HitBits"},
    {"0025d0b8", "BonesHitEmergency"},
    {"0025e44c", "Hit_Brain"},
    {"002683a0", "UpdateSkelBar"},
    {"00268494", "Brain_CSkelBar"},
    {"002685dc", "PauseBrain_CSkelBar"},
    {"002686c0", "BonesLooseSkeletism"},
    {"00268714", "BonesAddSkeletism"},
    {"0026877c", "LooseSkeletism_CSkelBar"},
    {"002687c8", "AddSkeletism_CSkelBar"},
    {"002687f8", "DisablePause_CSkelBar"},
    {"0026aa08", "RGang"},
    {"0026acf0", "CreateCBluePoof"},
    {"0026adf0", "Brain_CBluePoof"},
    {"0026af48", "Brain_CRedPoof"},
    {"0026b038", "CreateCLord"},
    {"0026b410", "Brain_CLord"},
    {"0026b448", "CreateCGangMember"},
    {"0026bbac", "CheckObstacle_CGangMember"},
    {"0026c890", "Start_CGangMember"},
    {"0026d2d0", "CreateCLevelManager"},
    {"0026d448", "Brain_CLevelManager"},
    {"0026d574", "EvalSuccess_CLevelManager"},
    {"0026d58c", "CheckJumpys_CLevelManager"},
    {"0026d68c", "CheckFallHole_CLevelManager"},
    {"0026d784", "CheckConfig_CLevelManager"},
    {"0026d8a4", "CreateCThrown"},
    {"0026da50", "CheckForHit_CThrown"},
    {"0026dd34", "Brain_CThrown"},
    {"00232e78", "HealthBarAnim"},
    {"002537a0", "PauseAnim"},
  };

  public void run() throws Exception {
    String outPath = "F:/MrBonesPort/docs/ghidra-endgame.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("Program " + currentProgram.getName()
        + " language " + currentProgram.getLanguageID()
        + " imagebase " + currentProgram.getImageBase());
    out.println("MAP: Skeletism@285884 RealSkeletism@285888 Pump@28588c");
    out.println("MAP: GlobalRealSkeletism@214418 bPaused@21441c pTheSkelBar@28c708");
    out.println("MAP: HitTimeout@2859f4 bPanScrollPause@284b50");
    for (String[] t : TARGETS) {
      dumpFun(out, decomp, t[0], t[1]);
    }
    /* Dump key .data words for Skeletism / RealSkeletism / HitTimeout. */
    out.println("\n=== data words ===");
    dumpWords(out, "Skeletism", "00285884", 4);
    dumpWords(out, "RealSkeletism", "00285888", 2);
    dumpWords(out, "PumpSkeletism", "0028588c", 2);
    dumpWords(out, "HitTimeout", "002859f4", 4);
    dumpWords(out, "pTheSkelBar", "0028c708", 2);
    dumpWords(out, "endgame_data", "00284b00", 4);
    out.close();
    decomp.dispose();
    println("Wrote " + outPath);
  }

  private void dumpWords(PrintWriter out, String name, String hex, int n) throws Exception {
    Address a = toAddr(hex);
    out.printf("%s @ %s:", name, a);
    for (int i = 0; i < n; i++) {
      int w = getInt(a.add(i * 4L));
      out.printf(" %08X", w);
    }
    out.println();
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
      for (int i = 0; i < 48 && insn != null; i++) {
        out.println(insn.getAddress() + " " + insn.toString());
        insn = insn.getNext();
      }
      return;
    }
    out.println("BODY " + fun.getBody());
    DecompileResults dr = decomp.decompileFunction(fun, 180, monitor);
    if (dr.decompileCompleted())
      out.println(dr.getDecompiledFunction().getC());
    else
      out.println("DECOMPILE FAILED " + dr.getErrorMessage());
    out.println("-- listing head --");
    Instruction insn = getInstructionAt(addr);
    for (int i = 0; i < 36 && insn != null; i++) {
      out.println(insn.getAddress() + " " + insn.toString());
      insn = insn.getNext();
    }
  }
}
