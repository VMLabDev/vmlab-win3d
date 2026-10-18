// Ghidra headless post-script: decompile every function whose full name
// (namespace::name) matches one of the comma-separated regexes in $DECOMP,
// appending the C to the file named by $DECOMP_OUT. Used by analyse.sh.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.regex.*;
public class Decomp extends GhidraScript {
  public void run() throws Exception {
    String[] pats = System.getenv("DECOMP").split(",");
    String outp = System.getenv("DECOMP_OUT");
    PrintWriter pw = new PrintWriter(new FileWriter(outp, true));
    DecompInterface di = new DecompInterface();
    di.openProgram(currentProgram);
    for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
      for (String p : pats) {
        if (Pattern.compile(p).matcher(f.getName(true)).find()) {
          DecompileResults r = di.decompileFunction(f, 120, monitor);
          pw.println("// ==== " + f.getName() + " @ " + f.getEntryPoint());
          if (r.decompileCompleted()) pw.println(r.getDecompiledFunction().getC());
          break;
        }
      }
    }
    pw.close();
  }
}
