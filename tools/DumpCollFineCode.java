import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpCollFineCode extends GhidraScript {
  public void run() throws Exception {
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0] : "F:/MrBonesPort/docs/ghidra-coll-fine-code.txt";
    DecompInterface decomp = new DecompInterface();
    decomp.openProgram(currentProgram);
    PrintWriter out = new PrintWriter(new File(outPath));
    out.println("Program " + currentProgram.getName()
        + " language " + currentProgram.getLanguageID()
        + " imagebase " + currentProgram.getImageBase()
        + " sha256 " + currentProgram.getExecutableSHA256());
    out.println("MAP: _PANCOLLGetHixel@256a68 _BonesPushOutOfWalls@25c694 _BonesWallAboveCheck@25c900");
    out.println("HitChk BSS/data @285A5C..285A88 (runtime; image zeros)");
    String[][] targets = {
      {"256a68","PANCOLLGetHixel_256A68"},
      {"25c694","BonesPushOutOfWalls_25C694"},
      {"25c900","BonesWallAboveCheck_25C900"},
      {"256b60","PANCOLLfindfloor_256B60"},
      {"256c08","PANCOLLfindtop_256C08"}
    };
    for (String[] x: targets) {
      Address a = toAddr(x[0]);
      out.println("\n=== "+x[1]+" @ "+a+" ===");
      Function f = getFunctionAt(a);
      if (f==null){ disassemble(a); analyzeChanges(currentProgram); f=createFunction(a,x[1]); }
      if (f==null){ out.println("NO FUN"); continue; }
      out.println("BODY "+f.getBody());
      DecompileResults dr = decomp.decompileFunction(f,90,monitor);
      if (dr.decompileCompleted()) out.println(dr.getDecompiledFunction().getC());
      else out.println("FAIL "+dr.getErrorMessage());
    }
    out.println("\n=== pool GetHixel @256AD0 ===");
    out.println("ad0(Xbias)="+Integer.toHexString(getShort(toAddr("256ad0"))&0xffff));
    out.println("ad2(Xmax)="+Integer.toHexString(getShort(toAddr("256ad2"))&0xffff));
    out.println("ad4(Ymax)="+Integer.toHexString(getShort(toAddr("256ad4"))&0xffff));
    out.println("ad8(pMap)="+Integer.toHexString(getInt(toAddr("256ad8"))));
    out.println("adc(BitExtract)="+Integer.toHexString(getInt(toAddr("256adc"))));
    out.close(); decomp.dispose();
    println("wrote "+outPath);
  }
}
