// Ghidra headless script: print bounded cross-reference evidence for the
// retail Fallout 3 camera/renderer types and the 1.7.0.3 interface singleton.
// @category VRClient

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

public class Fallout3ProfileEvidence extends GhidraScript {
    private static final String[] TARGETS = {
        "00F73084", // RTTI_NiCamera in the published FOSE 1.7.0.3 map
        "00F75B60", // RTTI_NiDX9Renderer in the published FOSE 1.7.0.3 map
        "01075B24", // InterfaceManager** in the published FOSE 1.7.0.3 map
        "0107A224", // owner of the active camera pointer at +0xAC
        "006ECBA0"  // bounded world-render dispatcher
    };

    private void printReferences(Address target, int depth, int limit) {
        ReferenceIterator iterator = currentProgram.getReferenceManager().getReferencesTo(target);
        int count = 0;
        while (iterator.hasNext() && count < limit) {
            Reference reference = iterator.next();
            Address from = reference.getFromAddress();
            Function function = getFunctionContaining(from);
            Instruction instruction = getInstructionAt(from);
            println("  ref depth=" + depth + " from=" + from +
                " type=" + reference.getReferenceType() +
                " function=" + (function == null ? "<data>" : function.getName(true)) +
                " instruction=" + (instruction == null ? "<none>" : instruction));
            if (depth > 0 && function == null) {
                printReferences(from, depth - 1, Math.min(limit - count, 20));
            }
            count++;
        }
        if (iterator.hasNext()) println("  ... references truncated at " + limit);
    }

    @Override
    public void run() throws Exception {
        println("program=" + currentProgram.getName() + " imageBase=" + currentProgram.getImageBase());
        for (String text : TARGETS) {
            Address address = toAddr(text);
            Symbol symbol = getSymbolAt(address);
            println("target=" + address + " symbol=" +
                (symbol == null ? "<none>" : symbol.getName(true)));
            printReferences(address, 2, 80);
        }

        String[] terms = {"NiCamera", "NiDX9Renderer", "SceneGraph", "NiRenderer"};
        SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
        int matches = 0;
        while (symbols.hasNext() && matches < 400) {
            Symbol symbol = symbols.next();
            String qualified = symbol.getName(true);
            for (String term : terms) {
                if (qualified.contains(term)) {
                    println("symbol=" + symbol.getAddress() + " type=" + symbol.getSymbolType() +
                        " name=" + qualified);
                    matches++;
                    break;
                }
            }
        }
        if (matches == 400) println("symbol matches truncated at 400");
    }
}
