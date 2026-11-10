import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import java.io.*;

/*
 * Movie flow entry points, chosen by the open program:
 *   MENU   03MENU/03CODE: 00239ae8, 002399b8, 002399cc, 0023cbc4
 *   LOADER /02:           0020b714 (_PlayNewDuckMovie), 0020b4ac (_LoadLevel)
 *
 * Output starts with a provenance line (program, language, image base, SHA-256),
 * then "=== <addr> ===" and the decompiled C of each target.
 *
 * Args: [outPath]  default docs/ghidra-menu-movie-flow.txt or
 *                  docs/ghidra-loader-movie-flow.txt
 *
 * Standalone: no other script in tools/ is needed.
 */
public class DumpMovieFlow extends GhidraScript {
  public void run() throws Exception {
    boolean menu = currentProgram.getName().contains("MENU");
    String[] targets = menu
        ? new String[] {"00239ae8", "002399b8", "002399cc", "0023cbc4"}
        : new String[] {"0020b714", "0020b4ac"};
    String[] args = getScriptArgs();
    String outPath = args.length > 0 ? args[0]
        : (menu ? "F:/MrBonesPort/docs/ghidra-menu-movie-flow.txt"
                : "F:/MrBonesPort/docs/ghidra-loader-movie-flow.txt");

    DecompInterface d = new DecompInterface();
    d.openProgram(currentProgram);
    try (PrintWriter out = new PrintWriter(new File(outPath))) {
      out.println("Program " + currentProgram.getName()
          + " language " + currentProgram.getLanguageID()
          + " imagebase " + currentProgram.getImageBase()
          + " sha256 " + currentProgram.getExecutableSHA256());
      for (String target : targets) {
        Address a = toAddr(target);
        Function f = getFunctionAt(a);
        if (f == null) {
          disassemble(a);
          analyzeChanges(currentProgram);
          f = createFunction(a, "movie_" + target);
        }
        out.println("=== " + target + " ===");
        if (f != null) {
          DecompileResults r = d.decompileFunction(f, 180, monitor);
          out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC()
                                             : r.getErrorMessage());
        } else {
          out.println("no function");
        }
        out.flush();
      }
    } finally {
      d.dispose();
    }
    println("Wrote " + outPath);
  }
}
