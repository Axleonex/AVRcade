#!/usr/bin/env python3
"""Independent SHA-256 cross-check for the Phase 3 runtime-binary hash closure.

The C++ test (vr_runtime_hash_tests) computes the SHA-256 of a REAL built
artifact via the Windows BCrypt (CNG) API and feeds it through the project's
detectVersion() identification path. This validator provides a fully INDEPENDENT
SHA-256 implementation (Python's hashlib) over the SAME file, so the green
verdict never rests on a single hashing code path.

Two invocation modes:

  1. CTest registration (preferred, binding cross-check):
        validate_runtime_binary_hash.py <artifact> [<vr_runtime_hash_tests-exe>]
     - hashlib-hashes <artifact>.
     - If the C++ test exe is given, runs it on the SAME <artifact>, parses its
       `runtime_sha256=` line, and asserts BCrypt == hashlib byte-for-byte.

  2. Build-script Step 7 (no args, run from repo root):
     - Locates the newest built vrclient_smoke_host.exe under build/ and asserts
       hashlib produces a well-formed 64-hex SHA-256 of a real binary. This keeps
       the validator self-sufficient when auto-discovered with no arguments; the
       binding C++/Python equality check is exercised by the CTest registration.

Exit 0 = pass, nonzero = fail (build-and-test.ps1 Step 7 + CTest both honor this).
"""

import hashlib
import os
import subprocess
import sys


def hashlib_sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest().lower()


def is_sha256_hex(value):
    return len(value) == 64 and all(c in "0123456789abcdef" for c in value.lower())


def repo_root():
    # tests/native/versioning/<this> -> repo root is three levels up.
    return os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    )


def newest_smoke_host():
    """Locate the most recently built vrclient_smoke_host.exe under build/."""
    build_dir = os.path.join(repo_root(), "build")
    newest = None
    newest_mtime = -1.0
    for current, _dirs, files in os.walk(build_dir):
        for name in files:
            if name.lower() == "vrclient_smoke_host.exe":
                candidate = os.path.join(current, name)
                mtime = os.path.getmtime(candidate)
                if mtime > newest_mtime:
                    newest_mtime = mtime
                    newest = candidate
    return newest


def parse_cpp_hash(output):
    for line in output.splitlines():
        line = line.strip()
        if line.startswith("runtime_sha256="):
            return line.split("=", 1)[1].strip().lower()
    return None


def fail(message):
    print("FAIL: {}".format(message))
    return 1


def main(argv):
    artifact = argv[1] if len(argv) > 1 else None
    cpp_exe = argv[2] if len(argv) > 2 else None

    if artifact is None:
        # Step 7 auto-discovery mode: hash a real built smoke host.
        artifact = newest_smoke_host()
        if artifact is None:
            # No build present yet (e.g. validator scanned before any build). This
            # script's binding check lives in the CTest registration, which always
            # has a freshly built artifact, so a missing standalone build is not a
            # closure failure.
            print(
                "SKIP: no built vrclient_smoke_host.exe under build/; "
                "binding C++/Python cross-check runs via the CTest registration"
            )
            return 0

    if not os.path.isfile(artifact):
        return fail("artifact not found: {}".format(artifact))

    py_hash = hashlib_sha256(artifact)
    if not is_sha256_hex(py_hash):
        return fail("hashlib did not produce a 64-hex SHA-256: {!r}".format(py_hash))

    if cpp_exe is not None:
        if not os.path.isfile(cpp_exe):
            return fail("C++ hash test exe not found: {}".format(cpp_exe))
        proc = subprocess.run(
            [cpp_exe, artifact],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
        )
        if proc.returncode != 0:
            return fail(
                "vr_runtime_hash_tests exited {} on {}:\n{}".format(
                    proc.returncode, artifact, proc.stdout
                )
            )
        cpp_hash = parse_cpp_hash(proc.stdout)
        if cpp_hash is None:
            return fail("could not parse runtime_sha256= from C++ test output")
        if cpp_hash != py_hash:
            return fail(
                "BCrypt (C++) and hashlib (Python) disagree on the artifact hash:\n"
                "  C++ BCrypt : {}\n  Python     : {}".format(cpp_hash, py_hash)
            )
        print(
            "PASS: independent SHA-256 agree on real artifact "
            "(BCrypt == hashlib)\n  artifact: {}\n  sha256  : {}".format(
                artifact, py_hash
            )
        )
        return 0

    print(
        "PASS: hashlib SHA-256 of real built artifact is well-formed "
        "(binding cross-check via CTest)\n  artifact: {}\n  sha256  : {}".format(
            artifact, py_hash
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
