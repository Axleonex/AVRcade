from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
HELPER_H = ROOT / "src/native/adapters/unreal/meccha_detour_transaction.h"
HELPER_CPP = ROOT / "src/native/adapters/unreal/meccha_detour_transaction.cpp"
OBSERVER = ROOT / "src/native/adapters/unreal/meccha_observer_payload.cpp"
ORIENTATION = ROOT / "src/native/adapters/unreal/meccha_orientation_service.cpp"
CMAKE = ROOT / "CMakeLists.txt"


def function_body(text: str, signature: str) -> str:
    start = text.rfind(signature)
    if start < 0:
        raise AssertionError(f"could not isolate {signature}")
    opening = text.find("{", start + len(signature))
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    raise AssertionError(f"unterminated body for {signature}")


def main() -> int:
    helper = HELPER_CPP.read_text(encoding="utf-8")
    observer = OBSERVER.read_text(encoding="utf-8")
    orientation = ORIENTATION.read_text(encoding="utf-8")
    cmake = CMAKE.read_text(encoding="utf-8")

    assert HELPER_H.exists()
    assert "CreateToolhelp32Snapshot" in helper
    assert "Thread32First" in helper and "Thread32Next" in helper
    assert "DetourUpdateThread(GetCurrentThread())" in helper
    assert "GetExitCodeThread" in helper
    assert "DetourTransactionAbort" in helper
    assert "commitMecchaDetourTransaction" in observer
    assert "commitMecchaDetourTransaction" in orientation
    assert "DetourTransactionBegin" not in observer
    assert "DetourUpdateThread" not in observer
    assert "DetourTransactionBegin" not in orientation
    assert "DetourUpdateThread" not in orientation
    assert cmake.count("src/native/adapters/unreal/meccha_detour_transaction.cpp") >= 3

    install_orientation = function_body(
        orientation, "bool installMecchaOrientationOnlyHook("
    )
    assert "CreateFileW" in install_orientation
    assert re.search(
        r"CreateFileW\([\s\S]*?GENERIC_READ,\s*FILE_SHARE_READ,",
        install_orientation,
    )
    assert "fileSha256(executable_file)" in install_orientation
    assert "loadedModuleMatchesPinnedPrologue" in install_orientation
    assert install_orientation.index("loadedModuleMatchesPinnedPrologue") < (
        install_orientation.index("commitMecchaDetourTransaction")
    )
    assert "CloseHandle(executable_file)" in install_orientation

    swapchain_hooks = function_body(observer, "void installSwapchainHooks(")
    factory_hooks = function_body(observer, "void installFactoryHooks(")
    assert re.search(r"MecchaDetourAttachment,\s*2", swapchain_hooks)
    assert "attachments.data(), attachments.size()" in swapchain_hooks
    assert "attachDetour(" not in swapchain_hooks
    assert re.search(r"MecchaDetourAttachment,\s*4", factory_hooks)
    assert "attachments.data(), attachments.size()" in factory_hooks
    assert "attachDetour(" not in factory_hooks

    print("Meccha all-thread Detours transaction static checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
