// Ghidra headless script: decompile a bounded list of exact Fallout 3
// renderer construction and frame-path functions used while authoring a
// reviewed hook profile.
// @category VRClient

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class Fallout3DecompileTargets extends GhidraScript {
    private void decompile(Function function, DecompInterface decompiler) {
        if (function == null) return;
        DecompileResults results = decompiler.decompileFunction(function, 90, monitor);
        println("decompile function=" + function.getName(true) + " entry=" + function.getEntryPoint());
        println(results.decompileCompleted() ? results.getDecompiledFunction().getC()
            : "failed=" + results.getErrorMessage());
    }

    private void decompileAddress(String text, DecompInterface decompiler) {
        Address address = toAddr(text);
        decompile(getFunctionContaining(address), decompiler);
    }

    private void decompileCallers(String text, int limit, DecompInterface decompiler) {
        Address entry = toAddr(text);
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(entry);
        int count = 0;
        while (references.hasNext() && count < limit) {
            Reference reference = references.next();
            Function caller = getFunctionContaining(reference.getFromAddress());
            if (caller != null) {
                println("caller target=" + entry + " callsite=" + reference.getFromAddress());
                decompile(caller, decompiler);
                count++;
            }
        }
    }

    @Override
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        decompileAddress("008284D0", decompiler); // NiRenderer base constructor
        decompileAddress("0087AD70", decompiler); // NiDX9Renderer constructor
        decompileCallers("0087AD70", 12, decompiler);
        // Game-side frame path around the known InterfaceManager and renderer setup.
        decompileAddress("00629280", decompiler);
        decompileAddress("00629B00", decompiler);
        decompileAddress("006ECBA0", decompiler);
        decompileAddress("006ECC50", decompiler);
        decompileAddress("0083B460", decompiler);
        decompileCallers("006ECBA0", 20, decompiler);
        decompileCallers("0083B460", 20, decompiler);
        decompiler.dispose();
    }
}
