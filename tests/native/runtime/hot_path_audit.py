from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
HOT_PATH_FILES = [
    ROOT / "src" / "native" / "runtime" / "openxr" / "openxr_runtime.cpp",
    ROOT / "src" / "native" / "runtime" / "frame" / "test_scene_renderer.cpp",
]

BLOCKED_PATTERNS = {
    "console logging": re.compile(r"\b(std::cout|std::cerr|printf|fprintf)\b"),
    "diagnostics logging": re.compile(r"\bdiagnostics_\.log|diagnostics::"),
    "file I/O": re.compile(r"\b(std::ifstream|std::ofstream|fopen|CreateFile|ReadFile|WriteFile)\b"),
    "heap allocation": re.compile(r"(\bnew\b|\bdelete\b|\bmalloc\b|\bcalloc\b|\brealloc\b|\bfree\b)"),
    "blocking OS wait": re.compile(r"\b(Sleep|WaitForSingleObject|WaitForMultipleObjects)\b"),
    "blocking lock": re.compile(r"\b(std::mutex|std::lock_guard|std::unique_lock|CRITICAL_SECTION)\b"),
    "diagnostic failure path": re.compile(r"\bfail\s*\("),
}


def hot_sections(text: str) -> list[str]:
    sections: list[str] = []
    start_marker = "// HOT PATH BEGIN"
    end_marker = "// HOT PATH END"
    cursor = 0
    while True:
        start = text.find(start_marker, cursor)
        if start == -1:
            break
        end = text.find(end_marker, start)
        if end == -1:
            raise AssertionError("HOT PATH BEGIN without HOT PATH END")
        sections.append(text[start:end])
        cursor = end + len(end_marker)
    return sections


def main() -> int:
    checked = []
    for path in HOT_PATH_FILES:
        text = path.read_text(encoding="utf-8")
        sections = hot_sections(text)
        if not sections:
            raise AssertionError(f"{path.relative_to(ROOT)} has no HOT PATH markers")

        for index, section in enumerate(sections, start=1):
            for label, pattern in BLOCKED_PATTERNS.items():
                if pattern.search(section):
                    raise AssertionError(
                        f"{label} found in hot path section {index} of {path.relative_to(ROOT)}"
                    )
        checked.append(path.relative_to(ROOT).as_posix())

    print("hot-path audit passed for: " + ", ".join(checked))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
