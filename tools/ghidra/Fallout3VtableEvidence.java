// Ghidra headless script: enumerate the exact Fallout 3 1.7.0.3 camera and
// renderer vtables plus the constructor sites that install those vtables.
// @category VRClient

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class Fallout3VtableEvidence extends GhidraScript {
    private Address pointerAt(Address slot) throws Exception {
        long value = Integer.toUnsignedLong(currentProgram.getMemory().getInt(slot));
        return toAddr(value);
    }

    private void printTable(String name, String text, int entries) throws Exception {
        Address table = toAddr(text);
        println("vtable=" + name + " address=" + table);
        for (int index = 0; index < entries; index++) {
            Address slot = table.add(index * 4L);
            Address target = pointerAt(slot);
            Function function = getFunctionAt(target);
            if (function == null || !currentProgram.getMemory().contains(target)) break;
            println("  index=" + index + " slot=" + slot + " target=" + target +
                " function=" + function.getName(true));
        }
    }

    private void printConstructorReferences(String text) {
        Address table = toAddr(text);
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(table);
        int count = 0;
        while (references.hasNext() && count < 30) {
            Reference reference = references.next();
            Function function = getFunctionContaining(reference.getFromAddress());
            println("vtable_ref table=" + table + " from=" + reference.getFromAddress() +
                " function=" + (function == null ? "<data>" : function.getName(true)));
            count++;
        }
    }

    private void decompile(String text, DecompInterface decompiler) {
        Function function = getFunctionContaining(toAddr(text));
        if (function == null) {
            println("decompile address=" + text + " function=<none>");
            return;
        }
        DecompileResults results = decompiler.decompileFunction(function, 60, monitor);
        println("decompile function=" + function.getName(true) + " entry=" + function.getEntryPoint());
        if (results.decompileCompleted()) {
            println(results.getDecompiledFunction().getC());
        } else {
            println("  failed=" + results.getErrorMessage());
        }
    }

    @Override
    public void run() throws Exception {
        printTable("NiCamera", "00E22464", 100);
        printTable("NiRenderer", "00E20D44", 180);
        printTable("NiDX9Renderer", "00E29004", 180);
        printConstructorReferences("00E22464");
        printConstructorReferences("00E29004");

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        // Vtable write sites identified by the reference pass above.
        ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(toAddr("00E29004"));
        int count = 0;
        while (references.hasNext() && count < 8) {
            decompile(references.next().getFromAddress().toString(), decompiler);
            count++;
        }
        decompiler.dispose();
    }
}
