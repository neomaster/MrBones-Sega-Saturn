import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.MemoryAccessException;
import java.io.*;
import java.util.*;

public class DumpHitChkWriters extends GhidraScript {
  static final long[] HIT = {
    0x285A5C,0x285A60,0x285A64,0x285A68,0x285A6C,0x285A70,
    0x285A74,0x285A78,0x285A7C,0x285A80,0x285A84,0x285A88
  };
  static final String[] HITN = {
    "TopMidX","TopMidY","BotMidX","BotMidY","TopRghtX","TopRghtY",
    "TopLeftX","TopLeftY","BotRghtX","BotRghtY","BotLeftX","BotLeftY"
  };

  public void run() throws Exception {
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0] : "F:/MrBonesPort/docs/ghidra-hitchk-writers.txt";
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("Program " + currentProgram.getName()
        + " language " + currentProgram.getLanguageID()
        + " imagebase " + currentProgram.getImageBase()
        + " sha256 " + currentProgram.getExecutableSHA256());
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);

    /* Literal BE32 refs to each HitChk cell → owning function */
    Set<Address> funcs = new LinkedHashSet<Address>();
    for (int i = 0; i < HIT.length; i++) {
      Address target = toAddr(Long.toHexString(HIT[i]));
      out.println("\n=== refs to "+HITN[i]+" @"+target+" ===");
      AddressSetView set = currentProgram.getMemory().getLoadedAndInitializedAddressSet();
      byte[] pat = new byte[]{
        (byte)((HIT[i]>>24)&0xff),(byte)((HIT[i]>>16)&0xff),
        (byte)((HIT[i]>>8)&0xff),(byte)(HIT[i]&0xff)
      };
      Address start = set.getMinAddress();
      int n = 0;
      while (start != null && n < 40) {
        Address found = currentProgram.getMemory().findBytes(start, set.getMaxAddress(), pat, null, true, monitor);
        if (found == null) break;
        Function f = getFunctionContaining(found);
        out.println("  lit@"+found+(f!=null ? " in "+f.getName()+" @"+f.getEntryPoint() : " NOFUN"));
        if (f != null) funcs.add(f.getEntryPoint());
        start = found.add(2);
        n++;
      }
    }

    /* Also scan for base 0x2858BC / 0x285A00 literals (struct-relative stores) */
    long[] bases = {0x2858BC, 0x285A00, 0x285A5C, 0x285850};
    for (long b : bases) {
      out.println("\n=== literal base "+Long.toHexString(b)+" ===");
      byte[] pat = new byte[]{
        (byte)((b>>24)&0xff),(byte)((b>>16)&0xff),(byte)((b>>8)&0xff),(byte)(b&0xff)
      };
      AddressSetView set = currentProgram.getMemory().getLoadedAndInitializedAddressSet();
      Address start = set.getMinAddress();
      int n = 0;
      while (start != null && n < 30) {
        Address found = currentProgram.getMemory().findBytes(start, set.getMaxAddress(), pat, null, true, monitor);
        if (found == null) break;
        Function f = getFunctionContaining(found);
        out.println("  lit@"+found+(f!=null ? " in "+f.getName()+" @"+f.getEntryPoint() : ""));
        if (f != null) funcs.add(f.getEntryPoint());
        start = found.add(2); n++;
      }
    }

    String[][] force = {
      {"25c108","BonesObjHitChk"},
      {"25cb98","BonesInairHitCheck"},
      {"25c694","BonesPushOutOfWalls"},
      {"25c900","BonesWallAboveCheck"},
      {"268a64","RWorldSprite"},
      {"257320","CPanScroll_Brain"},
      {"257968","PlayPanAnim"},
      {"257728","PreloadPanAnim"},
      {"25786c","PreBufferFrames"}
    };
    for (String[] x : force) {
      try { funcs.add(toAddr(x[0])); } catch (Exception e) {}
    }

    for (Address ep : funcs) {
      Function f = getFunctionAt(ep);
      if (f == null) {
        disassemble(ep);
        analyzeChanges(currentProgram);
        f = createFunction(ep, "FUN_"+ep);
      }
      if (f == null) continue;
      out.println("\n=== "+f.getName()+" @ "+ep+" ===");
      out.println("BODY "+f.getBody());
      DecompileResults dr = decomp.decompileFunction(f, 120, monitor);
      if (dr.decompileCompleted()) {
        String c = dr.getDecompiledFunction().getC();
        if (c.length() > 12000) c = c.substring(0, 12000) + "\n/* truncated */\n";
        out.println(c);
      } else out.println("FAIL "+dr.getErrorMessage());
    }
    out.close();
    decomp.dispose();
    println("wrote "+outPath+" funcs="+funcs.size());
  }
}
