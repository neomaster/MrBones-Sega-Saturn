import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.io.*;
public class DumpMovieFlow extends GhidraScript {
 public void run() throws Exception {
  boolean menu=currentProgram.getName().contains("MENU");
  String[] targets=menu?new String[]{"00239ae8","002399b8","002399cc","0023cbc4"}:new String[]{"0020b714","0020b4ac"};
  PrintWriter out=new PrintWriter(new File(menu?"F:/MrBonesPort/docs/ghidra-menu-movie-flow.txt":"F:/MrBonesPort/docs/ghidra-loader-movie-flow.txt"));
  DecompInterface d=new DecompInterface();d.openProgram(currentProgram);
  for(String target:targets){Address a=toAddr(target);Function f=getFunctionAt(a);if(f==null){disassemble(a);analyzeChanges(currentProgram);f=createFunction(a,"movie_"+target);}out.println("=== "+target+" ===");if(f!=null){DecompileResults r=d.decompileFunction(f,180,monitor);out.println(r.decompileCompleted()?r.getDecompiledFunction().getC():r.getErrorMessage());}else out.println("no function");out.flush();}d.dispose();out.close();
 }
}

