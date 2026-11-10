import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

/* §13: MENU selector/main + loader backup/dir/firstlev. */
public class DumpMenu extends GhidraScript {
  private static final String[][] MENU_TARGETS = {
    {"0023c278", "main"},
    {"0023c25c", "___main"},
    {"0023c324", "CreateCMenuSelector"},
    {"0023c370", "ctor_CMenuSelector"},
    {"0023c530", "DoLeagal_CMenuSelector"},
    {"0023c5a8", "UpdateLevels_CMenuSelector"},
    {"0023c608", "InitOptions_CMenuSelector"},
    {"0023c77c", "StartMenuScreen_CMenuSelector"},
    {"0023c900", "MoveSelectorH_CMenuSelector"},
    {"0023ca04", "MoveSelectorV_CMenuSelector"},
    {"0023ca9c", "SetSelectedEntry_CMenuSelector"},
    {"0023cbc4", "SelectMenuEntry_CMenuSelector"},
    {"0023cd7c", "BackTrakScreen_CMenuSelector"},
    {"0023cda0", "Brain_CMenuSelector"},
    {"0023c4a0", "BackupError_CMenuSelector"},
    {"00239f00", "CreateJoyPad"},
    {"002398c0", "endgame_text_start"},
  };
  private static final String[][] LOADER_TARGETS = {
    {"0020b91c", "LoadFirstLevel"},
    {"0020ba34", "BackupOpen"},
    {"0020bb0c", "BackupClose"},
    {"0020bb38", "BackupWrite"},
    {"0020bc54", "BackupRead"},
    {"0020bce0", "BackupLoadNvmShadow"},
    {"0020bd28", "BackupSaveNvmShadow"},
    {"0020b834", "CreateLoader"},
  };

  public void run() throws Exception {
    String n = currentProgram.getName().toLowerCase();
    boolean menu = n.contains("menu");
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0] : menu
        ? "F:/MrBonesPort/docs/ghidra-menu.txt"
        : "F:/MrBonesPort/docs/ghidra-loader-backup.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("Program " + currentProgram.getName()
        + " language " + currentProgram.getLanguageID()
        + " imagebase " + currentProgram.getImageBase()
        + " sha256 " + currentProgram.getExecutableSHA256());
    String[][] targets = menu ? MENU_TARGETS : LOADER_TARGETS;
    for (String[] t : targets) {
      dumpFun(out, decomp, t[0], t[1]);
      out.flush();
    }
    if (menu) {
      out.println("\n=== screen data words ===");
      tryDumpWords(out, "StartScreen", "0023e0f4", 8);
      tryDumpWords(out, "LevelSelectScreen", "0023f4a8", 8);
      tryDumpWords(out, "LevelSelectEntry", "0023f4b8", 8);
      tryDumpWords(out, "OptionsScreen", "0023f650", 8);
    } else {
      out.println("\n=== NvmShadow / Dir tables ===");
      tryDumpWords(out, "NvmShadow", "00228980", 10);
      tryDumpWords(out, "DirTable", "0022ddc0", 3);
      tryDumpWords(out, "BackupStatus", "0021daec", 1);
      out.println("DirNameTable@22da00 first 64 bytes:");
      try {
        Address a = toAddr("0022da00");
        for (int i = 0; i < 64; i++) {
          out.printf("%02X", getByte(a.add(i)) & 0xff);
          if ((i & 15) == 15) out.println();
          else out.print(' ');
        }
      } catch (Exception ex) {
        out.println("UNMAPPED " + ex.getMessage());
      }
    }
    out.close();
    decomp.dispose();
    println("Wrote " + outPath);
  }

  private void tryDumpWords(PrintWriter out, String name, String hex, int n) {
    try {
      Address a = toAddr(hex);
      out.printf("%s @ %s:", name, a);
      for (int i = 0; i < n; i++) {
        int w = getInt(a.add(i * 4L));
        out.printf(" %08X", w);
      }
      out.println();
    } catch (Exception ex) {
      out.println(name + " @ " + hex + ": UNMAPPED");
    }
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
      out.println("NO FUNCTION");
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
    for (int i = 0; i < 28 && insn != null; i++) {
      out.println(insn.getAddress() + " " + insn.toString());
      insn = insn.getNext();
    }
  }
}
