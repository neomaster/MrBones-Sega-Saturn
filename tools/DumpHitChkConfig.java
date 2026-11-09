import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpHitChkConfig extends GhidraScript {
  public void run() throws Exception {
    String outPath = "F:/MrBonesPort/docs/ghidra-hitchk-config.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("03CODE HitChk config/hit writers");
    String[][] t = {
      {"25d0b8","BonesHitEmergency"},
      {"25d75c","BonesSetConfig"},
      {"25d5f8","BonesFirstConfig"},
      {"25d688","BonesChangeConfig"},
      {"25d6f4","BonesTriggerConfig"},
      {"25d610","BonesTriggerConfigState"},
      {"25d92c","BonesFindConfigInfo"},
      {"25c108","BonesObjHitChk"},
      {"25cb98","BonesInairHitCheck"},
      {"25e95c","Fn_near_e95c"},
      {"264340","Fn_near_4340"},
      {"26ec70","Fn_near_ec70"},
      {"2602a8","Fn_near_02a8"}
    };
    for (String[] x : t) {
      Address a = toAddr(x[0]);
      out.println("\n=== "+x[1]+" @ "+a+" ===");
      Function f = getFunctionAt(a);
      if (f == null) {
        /* try containing */
        f = getFunctionContaining(a);
        if (f == null) {
          disassemble(a);
          analyzeChanges(currentProgram);
          f = createFunction(a, x[1]);
        }
      }
      if (f == null) { out.println("NO FUN"); continue; }
      out.println("ENTRY "+f.getEntryPoint()+" BODY "+f.getBody());
      DecompileResults dr = decomp.decompileFunction(f, 150, monitor);
      if (dr.decompileCompleted()) {
        String c = dr.getDecompiledFunction().getC();
        if (c.length() > 16000) c = c.substring(0,16000)+"\n/* trunc */\n";
        out.println(c);
      } else out.println("FAIL "+dr.getErrorMessage());
      /* dump pool words after body end if small */
    }
    out.close(); decomp.dispose();
    println("wrote "+outPath);
  }
}
