import ghidra.app.script.GhidraScript;
import java.io.*;

/*
 * Options and Start screens of 03MENU/03CODE, data words only.
 *
 * InitOptions_CMenuSelector (0023c608) and cases 4/5 of SelectMenuEntry
 * (0023cbc4) read and write two option words through their literal pools;
 * Brain_CMenuSelector (0023cda0) is the menu loop. This prints those pools,
 * the OptionsScreen and StartScreen records and every entry they point to,
 * and tags any word that falls inside _NvmShadow (00228950, 0x28 bytes).
 * No code is printed.
 *
 * Args: [outPath]  default docs/ghidra-menu-options.txt
 * Standalone: no other script in tools/ is needed.
 */
public class DumpMenuOptions extends GhidraScript {
  private static final long NVM = 0x00228950L, NVM_SIZE = 0x28L;
  private static final long LOADER = 0x00200000L;
  private static final long[][] POOLS = {
      {0x0023c592L, 0x0023c5a8L}, /* DoLeagal */
      {0x0023c668L, 0x0023c688L}, /* InitOptions */
      {0x0023c8c4L, 0x0023c900L}, /* StartMenuScreen */
      {0x0023cd24L, 0x0023cd7cL}, /* SelectMenuEntry */
      {0x0023ce62L, 0x0023ce9cL}, /* Brain */
  };
  private static final String[] POOL_NAMES = {
      "DoLeagal", "InitOptions", "StartMenuScreen", "SelectMenuEntry", "Brain"};
  private static final long[] SCREENS = {0x0023e0f4L, 0x0023f650L};
  private static final String[] SCREEN_NAMES = {"StartScreen", "OptionsScreen"};

  public void run() throws Exception {
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0] : "F:/MrBonesPort/docs/ghidra-menu-options.txt";
    try (PrintWriter out = new PrintWriter(new File(outPath))) {
      out.println("Program " + currentProgram.getName()
          + " language " + currentProgram.getLanguageID()
          + " imagebase " + currentProgram.getImageBase()
          + " sha256 " + currentProgram.getExecutableSHA256());
      for (int p = 0; p < POOLS.length; p++) {
        out.println("-- " + POOL_NAMES[p] + " literal pool --");
        long a = POOLS[p][0];
        /* 16-bit literals come first, up to the first 4-aligned address. */
        for (; (a & 3) != 0 && a < POOLS[p][1]; a += 2) half(out, a);
        if (p == 1) { half(out, a); half(out, a + 2); a += 4; } /* InitOptions: c668, c66a */
        for (; a < POOLS[p][1]; a += 4) word(out, a);
      }
      for (int s = 0; s < SCREENS.length; s++) screen(out, SCREEN_NAMES[s], SCREENS[s]);
    }
    println("Wrote " + outPath);
  }

  private void screen(PrintWriter out, String name, long at) throws Exception {
    out.println("-- " + name + " @ " + hex(at) + " --");
    for (int i = 0; i < 8 && inImage(at + 4L * i); i++) word(out, at + 4L * i);
    int count = getInt(toAddr(at));
    int columns = getInt(toAddr(at + 4));
    long entries = getInt(toAddr(at + 8)) & 0xffffffffL;
    out.println("-- " + name + " entries: count " + count + " columns " + columns
        + " at " + hex(entries) + " --");
    if (count < 0 || count > 64 || !inImage(entries)) { out.println("out of range"); return; }
    for (int i = 0; i < count; i++) {
      long ep = getInt(toAddr(entries + 4L * i)) & 0xffffffffL;
      out.println(name + " entry[" + i + "] @ " + hex(ep));
      if (!inImage(ep)) { out.println("  outside image"); continue; }
      for (int w = 0; w < 12 && inImage(ep + 4L * w); w++) {
        long v = getInt(toAddr(ep + 4L * w)) & 0xffffffffL;
        out.printf("  +%02x %08x%s%n", w * 4, v, note(v));
      }
    }
  }

  private String note(long v) throws Exception {
    if (v >= NVM && v < NVM + NVM_SIZE) return String.format("  _NvmShadow+0x%02x", v - NVM);
    if (inImage(v)) {
      String t = text(v);
      return t.isEmpty() ? "  menu image" : "  \"" + t + "\"";
    }
    if (v >= LOADER && v < currentProgram.getImageBase().getOffset()) return "  loader/RAM below image";
    return "";
  }

  private boolean inImage(long a) {
    return currentProgram.getMemory().contains(toAddr(a));
  }

  private void word(PrintWriter out, long a) throws Exception {
    if (!inImage(a)) { out.println(hex(a) + " outside image"); return; }
    long v = getInt(toAddr(a)) & 0xffffffffL;
    out.printf("%s = %08x%s%n", hex(a), v, note(v));
  }

  private void half(PrintWriter out, long a) throws Exception {
    if (!inImage(a)) { out.println(hex(a) + " outside image"); return; }
    out.printf("%s = %04x (16-bit)%n", hex(a), getShort(toAddr(a)) & 0xffff);
  }

  /* Printable ASCII run at a, up to 24 chars; empty unless at least 3 chars. */
  private String text(long a) throws Exception {
    StringBuilder sb = new StringBuilder();
    for (int i = 0; i < 24 && inImage(a + i); i++) {
      int c = getByte(toAddr(a + i)) & 0xff;
      if (c == 0) break;
      if (c < 0x20 || c > 0x7e) return "";
      sb.append((char) c);
    }
    return sb.length() >= 3 ? sb.toString() : "";
  }

  private static String hex(long a) { return String.format("%08x", a); }
}
