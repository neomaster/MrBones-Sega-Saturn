import ghidra.app.decompiler.DecompInterface;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.io.FileWriter;

public class DumpObjScheduler extends GhidraScript {
  private void dump(DecompInterface di, FileWriter w, long addr, String label) throws Exception {
    Address a = toAddr(addr);
    Function f = getFunctionAt(a);
    if (f == null) f = createFunction(a, label);
    w.write("=== " + label + " @ " + String.format("%08x", addr) + " ===\n");
    if (f == null) { w.write("NO FUNCTION\n\n"); return; }
    var res = di.decompileFunction(f, 60, monitor);
    w.write(res == null || res.getDecompiledFunction() == null ? "DECOMPILE FAIL\n\n"
      : res.getDecompiledFunction().getC() + "\n\n");
    w.write("--- asm ---\n");
    var listing = currentProgram.getListing();
    var ins = listing.getInstructions(f.getBody(), true);
    while (ins.hasNext()) {
      var i = ins.next();
      w.write(i.getAddress() + " " + i.toString() + "\n");
    }
    w.write("\n");
  }
  public void run() throws Exception {
    DecompInterface di = new DecompInterface();
    di.openProgram(currentProgram);
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0] : "F:/MrBonesPort/docs/ghidra-objyeild.txt";
    FileWriter w = new FileWriter(outPath);
    w.write("Program " + currentProgram.getName()
        + " language " + currentProgram.getLanguageID()
        + " imagebase " + currentProgram.getImageBase()
        + " sha256 " + currentProgram.getExecutableSHA256() + "\n");
    dump(di, w, 0x002050b0L, "ObjYeild");
    dump(di, w, 0x00205168L, "ObjNewBrain");
    dump(di, w, 0x002051a0L, "CreateCObject");
    dump(di, w, 0x002051e4L, "CObject_Delay");
    dump(di, w, 0x0020529cL, "CObject_Brain");
    dump(di, w, 0x00205214L, "CObject_LastCall");
    dump(di, w, 0x00205238L, "CObject_FirstCall");
    dump(di, w, 0x0020bf08L, "SMPCgetPad_alt");
    dump(di, w, 0x0020b834L, "CreateLoader_alt");
    w.close();
    di.dispose();
    println("wrote " + outPath);
  }
}
