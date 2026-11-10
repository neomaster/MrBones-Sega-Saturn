import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import java.io.*;

/*
 * Level-select table of 03MENU/03CODE, as UpdateLevels_CMenuSelector reads it
 * (docs/ghidra-menu.txt): for each entry of the LevelSelect screen it reads
 * one byte at table + *(entry + 8) and stores it at entry + 4.
 *
 * Prints only data words: the literals the routine loads (0023c5f8..0023c604),
 * the screen record at 0023f4a8, the entry pointer array, and for each entry
 * its words with any in-image pointer to printable text resolved. No code.
 *
 * Args: [outPath]  default docs/ghidra-menu-level-table.txt
 * Standalone: no other script in tools/ is needed.
 */
public class DumpLevelTable extends GhidraScript {
  private static final long SCREEN = 0x0023f4a8L;
  private static final long[] LITERALS = {0x0023c5f8L, 0x0023c5fcL, 0x0023c600L, 0x0023c604L};

  public void run() throws Exception {
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0] : "F:/MrBonesPort/docs/ghidra-menu-level-table.txt";
    try (PrintWriter out = new PrintWriter(new File(outPath))) {
      out.println("Program " + currentProgram.getName()
          + " language " + currentProgram.getLanguageID()
          + " imagebase " + currentProgram.getImageBase()
          + " sha256 " + currentProgram.getExecutableSHA256());
      out.println("-- UpdateLevels literals --");
      for (long l : LITERALS) word(out, l);
      out.println("-- LevelSelectScreen @ " + hex(SCREEN) + " --");
      for (int i = 0; i < 8; i++) word(out, SCREEN + 4L * i);
      int count = getInt(toAddr(SCREEN));
      long entries = getInt(toAddr(SCREEN + 8)) & 0xffffffffL;
      out.println("-- entries: count " + count + " at " + hex(entries) + " --");
      if (count < 0 || count > 64) { out.println("count out of range"); return; }
      for (int i = 0; i < count; i++) {
        long ep = getInt(toAddr(entries + 4L * i)) & 0xffffffffL;
        out.println("entry[" + i + "] @ " + hex(ep));
        if (!inImage(ep)) { out.println("  outside image"); continue; }
        for (int w = 0; w < 12; w++) {
          long a = ep + 4L * w;
          if (!inImage(a)) break;
          long v = getInt(toAddr(a)) & 0xffffffffL;
          String note = (w == 2) ? "  <- table offset" : "";
          String txt = inImage(v) ? text(v) : "";
          out.printf("  +%02x %08x%s%s%n", w * 4, v, txt.isEmpty() ? "" : "  \"" + txt + "\"", note);
        }
      }
    }
    println("Wrote " + outPath);
  }

  private boolean inImage(long a) {
    return currentProgram.getMemory().contains(toAddr(a));
  }

  private void word(PrintWriter out, long a) throws Exception {
    if (!inImage(a)) { out.println(hex(a) + " outside image"); return; }
    long v = getInt(toAddr(a)) & 0xffffffffL;
    out.printf("%s = %08x%n", hex(a), v);
  }

  /* Printable ASCII run at a, up to 24 chars; empty if the first byte is not printable. */
  private String text(long a) throws Exception {
    StringBuilder sb = new StringBuilder();
    for (int i = 0; i < 24 && inImage(a + i); i++) {
      int c = getByte(toAddr(a + i)) & 0xff;
      if (c == 0) break;
      if (c < 0x20 || c > 0x7e) return i >= 3 ? sb.toString() : "";
      sb.append((char) c);
    }
    return sb.length() >= 3 ? sb.toString() : "";
  }

  private static String hex(long a) { return String.format("%08x", a); }
}
