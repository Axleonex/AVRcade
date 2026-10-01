"""B7 / PERF-01 — static complement to the timing overhead ceiling.

Zero-flake, deterministic regression gate for the EXACT class of defect the
Part 2 overhead ceiling targets (a new heap alloc / lock / blocking call in a
per-frame controller update), but with NO timing dependency at all. It greps
the per-frame method bodies of the perf controllers for blocked patterns and
fails if any appear.

Auto-picked-up by scripts/ci/build-and-test.ps1 Step 7 (it runs every
tests/native/**/*.py), bumping the validator count, and is the recommended
PRIMARY gross-regression detector (the timing ceiling is the coarse secondary).

Per-frame methods audited (the hot, every-frame decision paths):
  * DynamicResolutionController::recordFrame   (dynamic_resolution.cpp)
  * FramePacingGuardrails::shouldEnterDegradedState (frame_pacing.cpp)
  * FramePacingGuardrails::shouldRecover        (frame_pacing.cpp)
  * makeFoveationSettings                       (foveation.cpp)

Reuses the BLOCKED_PATTERNS taxonomy from hot_path_audit.py verbatim (minus
the diagnostics-specific patterns that do not apply to these pure controllers).
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
PERF = ROOT / "src" / "native" / "runtime" / "perf"

# (file, fully-qualified method signature fragment that opens the body)
PER_FRAME_METHODS = [
    (PERF / "dynamic_resolution.cpp", "DynamicResolutionController::recordFrame"),
    (PERF / "frame_pacing.cpp", "FramePacingGuardrails::shouldEnterDegradedState"),
    (PERF / "frame_pacing.cpp", "FramePacingGuardrails::shouldRecover"),
    (PERF / "foveation.cpp", "makeFoveationSettings"),
]

# Mirrors hot_path_audit.py:13-21 (the allocation/lock/blocking/I-O class).
BLOCKED_PATTERNS = {
    "console logging": re.compile(r"\b(std::cout|std::cerr|printf|fprintf)\b"),
    "file I/O": re.compile(
        r"\b(std::ifstream|std::ofstream|fopen|CreateFile|ReadFile|WriteFile)\b"
    ),
    "heap allocation": re.compile(
        r"(\bnew\b|\bdelete\b|\bmalloc\b|\bcalloc\b|\brealloc\b|\bfree\b)"
    ),
    "blocking OS wait": re.compile(
        r"\b(Sleep|WaitForSingleObject|WaitForMultipleObjects)\b"
    ),
    "blocking lock": re.compile(
        r"\b(std::mutex|std::lock_guard|std::unique_lock|CRITICAL_SECTION)\b"
    ),
}


def extract_body(text: str, signature: str) -> str:
    """Return the brace-balanced body of the function whose definition contains
    `signature`. Raises if the signature or its opening brace is not found."""
    idx = text.find(signature)
    if idx == -1:
        raise AssertionError(f"signature not found: {signature}")
    brace = text.find("{", idx)
    if brace == -1:
        raise AssertionError(f"opening brace not found for: {signature}")
    depth = 0
    for pos in range(brace, len(text)):
        ch = text[pos]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return text[brace : pos + 1]
    raise AssertionError(f"unbalanced braces for: {signature}")


def main() -> int:
    audited = []
    for path, signature in PER_FRAME_METHODS:
        text = path.read_text(encoding="utf-8")
        body = extract_body(text, signature)
        for label, pattern in BLOCKED_PATTERNS.items():
            if pattern.search(body):
                raise AssertionError(
                    f"{label} found in per-frame method {signature} "
                    f"({path.relative_to(ROOT).as_posix()}) — PERF-01 requires the "
                    "perf-controller per-frame updates stay allocation/lock/"
                    "blocking/I-O free"
                )
        audited.append(signature)

    print("PERF-01 static audit passed (alloc/lock/blocking-free): " + ", ".join(audited))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
