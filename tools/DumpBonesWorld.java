import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

/* Map Bones world X/Y: RWorldSprite readers, WalkMove cells, CollProbe positions, writers. */
public class DumpBonesWorld extends GhidraScript {
  static String be32(byte[] mem, long off) {
    if (off < 0 || off + 3 >= mem.length) return "OOB";
    long v = ((mem[(int)off] & 0xffL) << 24) | ((mem[(int)off + 1] & 0xffL) << 16)
           | ((mem[(int)off + 2] & 0xffL) << 8) | (mem[(int)off + 3] & 0xffL);
    return String.format("0x%08X", v);
  }

  public void run() throws Exception {
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0] : "F:/MrBonesPort/docs/ghidra-bones-world.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("Program " + currentProgram.getName()
        + " language " + currentProgram.getLanguageID()
        + " imagebase " + currentProgram.getImageBase()
        + " sha256 " + currentProgram.getExecutableSHA256());

    /* Dump literal pools as BE32 from file-backed memory via getByte. */
    long[][] pools = {
      {0x25c98cL, 0x25c9b8L, /* CollProbe */},
      {0x25c7a0L, 0x25c7d4L, /* WalkAnim */},
      {0x25db88L, 0x25db9cL, /* WalkMove */},
      {0x26049cL, 0x2604e0L, /* BonesWalk head */},
      {0x25c98cL - 4, 0x25c98cL, /* Y neighbor guess */},
    };
    String[] poolNames = {"CollProbe", "WalkAnim", "WalkMove", "BonesWalk", "CollProbe-4"};
    for (int p = 0; p < pools.length; p++) {
      out.println("\n=== pool " + poolNames[p] + " ===");
      for (long va = pools[p][0]; va < pools[p][1]; va += 4) {
        Address a = toAddr(va);
        int b0 = getByte(a) & 0xff, b1 = getByte(a.add(1)) & 0xff;
        int b2 = getByte(a.add(2)) & 0xff, b3 = getByte(a.add(3)) & 0xff;
        long v = ((long)b0 << 24) | ((long)b1 << 16) | ((long)b2 << 8) | b3;
        out.printf("0x%08X = 0x%08X%n", va, v);
      }
    }

    String[][] funs = {
      {"25db1c", "WalkMove_25DB1C"},
      {"25c694", "WalkAnim_25C694"},
      {"25c900", "CollProbe_25C900"},
      {"25c7d4", "Fn_25C7D4"},
      {"25b380", "NearVel_25B380"},
      {"25e600", "NearVel_25E600"},
      {"26a900", "NearVel_26A900"},
      {"268a64", "RWorldSprite_268A64"},
      {"25d534", "BrainSwitch_25D534"},
      {"203154", "LoaderMaybe_203154"},
    };
    for (String[] x : funs) {
      Address a = toAddr(x[0]);
      out.println("\n=== " + x[1] + " @ " + a + " ===");
      Function f = getFunctionAt(a);
      if (f == null) {
        disassemble(a);
        analyzeChanges(currentProgram);
        f = createFunction(a, x[1]);
      }
      if (f == null) {
        out.println("NO FUN");
        continue;
      }
      out.println("BODY " + f.getBody());
      DecompileResults dr = decomp.decompileFunction(f, 120, monitor);
      if (dr.decompileCompleted()) out.println(dr.getDecompiledFunction().getC());
      else out.println("FAIL " + dr.getErrorMessage());
    }

    /* Scan for code that stores to cells 0x2858E6 / 0x285904 / 0x285928 via literal. */
    out.println("\n=== literal sites summary ===");
    long[] cells = {0x2858E6L, 0x2858EAL, 0x285904L, 0x285928L, 0x2858A0L, 0x28589CL};
    String[] names = {"cur_ush", "tgt_ush", "velA", "velB", "objA", "brain_slot"};
    for (int i = 0; i < cells.length; i++) {
      out.println(names[i] + " 0x" + Long.toHexString(cells[i]) + ":");
      byte[] be = new byte[] {
        (byte)((cells[i] >> 24) & 0xff), (byte)((cells[i] >> 16) & 0xff),
        (byte)((cells[i] >> 8) & 0xff), (byte)(cells[i] & 0xff)
      };
      Address cur = currentProgram.getMinAddress();
      Address end = currentProgram.getMaxAddress();
      int n = 0;
      while (cur != null && n < 12) {
        Address hit = currentProgram.getMemory().findBytes(cur, end, be, null, true, monitor);
        if (hit == null) break;
        out.println("  @" + hit);
        n++;
        try { cur = hit.add(4); } catch (Exception e) { break; }
      }
    }

    out.close();
    decomp.dispose();
    println("wrote " + outPath);
  }
}
