#Requires -Version 5.1
<#
.SYNOPSIS
    Locates CMake, Ninja, and the MSVC Build Tools on this machine and exposes them to the
    CURRENT PowerShell session (MSVC INCLUDE/LIB/PATH via vcvars64, plus VRCLIENT_* env vars).

.DESCRIPTION
    Bolt-on Phase B1, requirement BUILD-01 (reproducible toolchain).

    Resolution order (per .planning/bolt-on/recon/env.md and tools/toolchain.md):
      1. vswhere.exe (C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe)
         to enumerate VS installs that carry the VC x64 toolset.
      2. Recon-proven pinned paths on this machine:
         - MSVC:        VS Build Tools 2026 (v18) vcvars64.bat
         - CMake/Ninja: VS 2022 Community bundled CMake 3.31.6 + Ninja 1.12.1 (the exact
           binaries used by the proven build/phase4..6 validation builds), with the
           VS Build Tools 2026 bundle as the alternate.
      3. PATH fallback (Get-Command).

    The MSVC environment is imported by running vcvars64.bat in a child cmd.exe and copying
    the resulting process environment into this PowerShell process.

    IDEMPOTENT: re-running is a no-op when VRCLIENT_BUILD_ENV_READY=1 and cl.exe still
    resolves (prevents PATH growth from repeated vcvars imports).

    NEVER INSTALLS ANYTHING. If a tool is missing the script fails loudly with an actionable
    message; pass -Install to get exact install instructions printed (it still does not
    auto-download anything).

.PARAMETER Install
    When a required tool is missing, print exact installation instructions (winget commands /
    VS installer component IDs) instead of just the short error. No automatic download or
    install is ever performed.

.PARAMETER Quiet
    Suppress informational output (errors still surface).

.PARAMETER Compiler
    Which C/C++ compiler to expose for the build: msvc | clang-cl | auto.
      msvc      (default) — the existing behavior, byte-identical: CMake auto-detects
                cl.exe from the vcvars-imported PATH. No LLVM is required.
      clang-cl  — additionally resolve LLVM's clang-cl.exe (+ sibling lld-link.exe) via the
                portable order below and prepend LLVM's bin to PATH. The MSVC env (vcvars)
                is STILL imported because clang-cl runs in MSVC-compatible mode and needs the
                Windows SDK + MSVC CRT headers/libs (INCLUDE/LIB). Fails loudly if clang-cl
                is missing.
      auto      — prefer clang-cl if a portable LLVM install is found, else fall back to msvc.

    clang-cl detection order (portable; never hardcodes one user's path as the only option):
      1. PATH (Get-Command clang-cl.exe) — an on-PATH LLVM wins.
      2. C:\Program Files\LLVM\bin
      3. VS-bundled LLVM: ...\VC\Tools\Llvm\x64\bin (and the non-x64 ...\VC\Tools\Llvm\bin)
      4. user installs: %USERPROFILE%\LLVM\bin and %LOCALAPPDATA%\Programs\LLVM\bin
    lld-link.exe is resolved as the sibling of the chosen clang-cl.exe (same bin/).

.NOTES
    [PowerShell]  . .\tools\setup-build-env.ps1           # dot-source (recommended)
    [PowerShell]  . .\tools\setup-build-env.ps1 -Compiler clang-cl
    [PowerShell]  & .\tools\setup-build-env.ps1           # also works: env vars are process-wide
    Running via `powershell -File ...` only validates the environment; the imported
    variables die with that child process.

    Exposed on success:
      $env:VRCLIENT_BUILD_ENV_READY = 1
      $env:VRCLIENT_CMAKE_EXE / VRCLIENT_CTEST_EXE / VRCLIENT_NINJA_EXE / VRCLIENT_PYTHON_EXE
      $env:VRCLIENT_VCVARS_BAT
      $env:VRCLIENT_COMPILER (msvc|clang-cl, the RESOLVED selection)
      when clang-cl resolved: $env:VRCLIENT_CLANG_CL_EXE / $env:VRCLIENT_LLD_LINK_EXE,
        with LLVM's bin prepended to PATH (clang-cl.exe / lld-link.exe resolvable).
      cl.exe / cmake.exe / ninja.exe / ctest.exe resolvable on PATH; INCLUDE/LIB set
        (vcvars is imported for ALL compilers — clang-cl rides on the MSVC SDK/CRT).
#>
[CmdletBinding()]
param(
    [switch]$Install,
    [switch]$Quiet,
    [ValidateSet('msvc', 'clang-cl', 'auto')][string]$Compiler = 'msvc'
)

$ErrorActionPreference = 'Stop'

function Write-EnvLog {
    param([string]$Message)
    if (-not $Quiet) { Write-Host "[setup-build-env] $Message" }
}

# ---------------------------------------------------------------------------
# Candidate locations (recon-pinned; see tools/toolchain.md)
# ---------------------------------------------------------------------------
$VsWhereCandidates = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
)

# Recon-proven vcvars64 (preferred first: VS Build Tools 2026 — the compiler used by all
# prior validated builds), then VS 2022 Community as alternate.
$VcVarsCandidates = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
)

# Recon-proven CMake/Ninja (preferred first: VS 2022 Community bundle 3.31.6 / 1.12.1 —
# byte-identical with the proven phase4/5/6 validation builds).
$CMakeCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
)
$NinjaCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe',
    'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
)
$PythonCandidates = @(
    'C:\Python314\python.exe'
)

# clang-cl (LLVM) — portable resolution order. PATH is tried FIRST (below, via
# Get-Command) so an on-PATH LLVM wins; these are the well-known fixed install
# roots, ordered Program Files -> VS-bundled Llvm -> user installs. NOTE: do NOT
# hardcode one user's path as the only option — the %USERPROFILE%\LLVM entry is a
# generic per-user install location, not a machine-specific pin.
$ClangClCandidates = @(
    'C:\Program Files\LLVM\bin\clang-cl.exe',
    # VS-bundled LLVM (the "C++ Clang tools for Windows" VS component), x64 toolset:
    'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\Llvm\x64\bin\clang-cl.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe',
    'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\Llvm\bin\clang-cl.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin\clang-cl.exe',
    # per-user installs (generic locations, not a specific user's pin):
    (Join-Path $env:USERPROFILE 'LLVM\bin\clang-cl.exe'),
    (Join-Path $env:LOCALAPPDATA 'Programs\LLVM\bin\clang-cl.exe')
)

# ---------------------------------------------------------------------------
# Discovery helpers
# ---------------------------------------------------------------------------
function Find-VsWhere {
    foreach ($candidate in $VsWhereCandidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
    }
    return $null
}

function Get-VsInstallPaths {
    param([string]$VsWhereExe)
    if (-not $VsWhereExe) { return @() }
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $paths = & $VsWhereExe -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>&1 |
            ForEach-Object { "$_" } | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
        return @($paths)
    }
    finally { $ErrorActionPreference = $prev }
}

function Resolve-FirstExisting {
    param([string[]]$Candidates)
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
    }
    return $null
}

function Import-VcVarsEnv {
    param([Parameter(Mandatory = $true)][string]$VcVarsBat)

    if ($env:VRCLIENT_BUILD_ENV_READY -eq '1' -and $env:INCLUDE -and (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        Write-EnvLog 'MSVC environment already imported in this session (VRCLIENT_BUILD_ENV_READY=1); skipping vcvars re-import.'
        return
    }

    $marker = '__VRCLIENT_VCVARS_OK__'
    $tempBat = Join-Path $env:TEMP ("vrclient-vcvars-{0}.bat" -f ([guid]::NewGuid().ToString('N').Substring(0, 8)))
    @(
        '@echo off',
        ('call "{0}" >nul 2>&1' -f $VcVarsBat),
        'if errorlevel 1 exit /b 1',
        ('echo {0}' -f $marker),
        'set'
    ) | Set-Content -LiteralPath $tempBat -Encoding ASCII

    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $lines = & "$env:ComSpec" /d /c $tempBat 2>&1 | ForEach-Object { "$_" }
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $prev
        Remove-Item -LiteralPath $tempBat -ErrorAction SilentlyContinue
    }

    if ($exitCode -ne 0 -or -not ($lines -contains $marker)) {
        throw "vcvars64.bat failed (exit $exitCode) at: $VcVarsBat. The MSVC Build Tools install may be broken; re-run the Visual Studio Installer and repair the 'Desktop development with C++' workload."
    }

    $seenMarker = $false
    foreach ($line in $lines) {
        if (-not $seenMarker) {
            if ($line -eq $marker) { $seenMarker = $true }
            continue
        }
        $eq = $line.IndexOf('=')
        if ($eq -gt 0) {
            $name = $line.Substring(0, $eq)
            $value = $line.Substring($eq + 1)
            [System.Environment]::SetEnvironmentVariable($name, $value, 'Process')
        }
    }
    Write-EnvLog "Imported MSVC environment from: $VcVarsBat"
}

function Add-PathDir {
    param([Parameter(Mandatory = $true)][string]$Directory)
    $parts = $env:Path -split ';' | Where-Object { $_ }
    if ($parts -notcontains $Directory) {
        $env:Path = "$Directory;$env:Path"
        Write-EnvLog "Prepended to PATH: $Directory"
    }
}

# ---------------------------------------------------------------------------
# Locate everything
# ---------------------------------------------------------------------------
$missing = New-Object System.Collections.Generic.List[string]

$vswhere = Find-VsWhere
if ($vswhere) {
    Write-EnvLog "vswhere: $vswhere"
} else {
    Write-EnvLog 'vswhere.exe not found (will rely on recon-pinned paths only).'
}

# Build candidate lists augmented by vswhere results.
$vsInstalls = Get-VsInstallPaths -VsWhereExe $vswhere
# Prefer the recon-proven VS Build Tools 2026 install if vswhere reports it.
$vsInstallsOrdered = @($vsInstalls | Where-Object { $_ -like '*\18\BuildTools*' }) + @($vsInstalls | Where-Object { $_ -notlike '*\18\BuildTools*' })

$vcvarsFromVsWhere = @($vsInstallsOrdered | ForEach-Object { Join-Path $_ 'VC\Auxiliary\Build\vcvars64.bat' })
$cmakeFromVsWhere = @($vsInstallsOrdered | ForEach-Object { Join-Path $_ 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' })
$ninjaFromVsWhere = @($vsInstallsOrdered | ForEach-Object { Join-Path $_ 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' })

# 1) MSVC (vcvars64.bat): vswhere-derived first (still prefers \18\BuildTools), then pins.
$vcvars = Resolve-FirstExisting -Candidates ($VcVarsCandidates + $vcvarsFromVsWhere)
if (-not $vcvars) {
    $missing.Add('MSVC Build Tools (vcvars64.bat): not found via vswhere nor at the pinned paths. Install Visual Studio Build Tools with the "Desktop development with C++" workload (component Microsoft.VisualStudio.Component.VC.Tools.x86.x64 + a Windows 11 SDK).')
}

# 2) CMake: recon pins first (Community 3.31.6 bundle proven by prior builds), then vswhere, then PATH.
$cmake = Resolve-FirstExisting -Candidates ($CMakeCandidates + $cmakeFromVsWhere)
if (-not $cmake) {
    $pathCmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($pathCmake) { $cmake = $pathCmake.Source }
}
if (-not $cmake) {
    $missing.Add('CMake (>= 3.24): not found in any VS CMake bundle nor on PATH. Install via the VS Installer component "C++ CMake tools for Windows" or winget install Kitware.CMake.')
}

# 3) Ninja: same order.
$ninja = Resolve-FirstExisting -Candidates ($NinjaCandidates + $ninjaFromVsWhere)
if (-not $ninja) {
    $pathNinja = Get-Command ninja.exe -ErrorAction SilentlyContinue
    if ($pathNinja) { $ninja = $pathNinja.Source }
}
if (-not $ninja) {
    $missing.Add('Ninja: not found in any VS CMake bundle nor on PATH. Install via the VS Installer component "C++ CMake tools for Windows" (bundles ninja.exe) or winget install Ninja-build.Ninja.')
}

# 4) ctest: lives next to the chosen cmake.exe.
$ctest = $null
if ($cmake) {
    $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
    if (-not (Test-Path -LiteralPath $ctest)) {
        $ctest = $null
        $missing.Add("ctest.exe: expected next to cmake.exe at $(Split-Path -Parent $cmake) but not found. The CMake install is incomplete; reinstall it.")
    }
}

# 5) Python 3 (required unconditionally by CMakeLists.txt find_package(Python3) and by the
#    tests/native/**/*.py validators).
$python = $null
$pathPython = Get-Command python.exe -ErrorAction SilentlyContinue
if ($pathPython -and $pathPython.Source -notlike '*\WindowsApps\*') { $python = $pathPython.Source }
if (-not $python) { $python = Resolve-FirstExisting -Candidates $PythonCandidates }
if (-not $python) {
    $missing.Add('Python 3: not found on PATH (excluding the WindowsApps stub) nor at C:\Python314\python.exe. Install Python 3.x (winget install Python.Python.3.14) and then: python -m pip install jsonschema')
}

# 6) clang-cl (LLVM) — only resolved/required when requested. -Compiler auto prefers
#    clang-cl when a portable install is found and silently falls back to msvc otherwise;
#    -Compiler clang-cl makes a missing clang-cl a HARD failure. lld-link is the sibling.
$clangcl = $null
$lldlink = $null
$compilerResolved = 'msvc'
if ($Compiler -ne 'msvc') {
    $pathClang = Get-Command clang-cl.exe -ErrorAction SilentlyContinue
    if ($pathClang) { $clangcl = $pathClang.Source }
    if (-not $clangcl) { $clangcl = Resolve-FirstExisting -Candidates $ClangClCandidates }

    if ($clangcl) {
        $compilerResolved = 'clang-cl'
        $siblingLld = Join-Path (Split-Path -Parent $clangcl) 'lld-link.exe'
        if (Test-Path -LiteralPath $siblingLld) {
            $lldlink = $siblingLld
        }
        elseif ($Compiler -eq 'clang-cl') {
            $missing.Add("lld-link.exe: expected next to clang-cl.exe at $(Split-Path -Parent $clangcl) but not found. This LLVM install is incomplete; reinstall LLVM (winget install --id LLVM.LLVM) or the VS 'C++ Clang tools for Windows' component.")
        }
    }
    else {
        $compilerResolved = 'msvc'
        if ($Compiler -eq 'clang-cl') {
            $missing.Add('clang-cl (LLVM): -Compiler clang-cl was requested but clang-cl.exe was not found on PATH, in C:\Program Files\LLVM\bin, in any VS-bundled VC\Tools\Llvm\(x64\)bin, nor in %USERPROFILE%\LLVM\bin / %LOCALAPPDATA%\Programs\LLVM\bin. Install LLVM (winget install --id LLVM.LLVM) or add the VS component "C++ Clang tools for Windows" (Microsoft.VisualStudio.Component.VC.Llvm.Clang + ...ClangToolset), then re-run.')
        }
        else {
            # -Compiler auto: clang-cl absent is NOT fatal; fall back to msvc.
            Write-EnvLog 'Compiler auto: no portable LLVM/clang-cl install found; falling back to msvc.'
        }
    }
}

# ---------------------------------------------------------------------------
# Fail loudly on anything missing
# ---------------------------------------------------------------------------
if ($missing.Count -gt 0) {
    $header = "setup-build-env: $($missing.Count) required tool(s) missing."
    if ($Install) {
        Write-Host ''
        Write-Host '=== Install instructions (nothing is auto-downloaded) ==='
        Write-Host ''
        Write-Host 'This machine needs (see tools/toolchain.md for the pinned manifest):'
        Write-Host '  1. Visual Studio Build Tools (2022 v17+ or 2026 v18) with:'
        Write-Host '       - workload: Desktop development with C++'
        Write-Host '       - component: Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
        Write-Host '       - component: a Windows 11 SDK (e.g. 10.0.26100)'
        Write-Host '       - component: C++ CMake tools for Windows (bundles cmake.exe + ninja.exe)'
        Write-Host '     [Either] winget install --id Microsoft.VisualStudio.2022.BuildTools'
        Write-Host '     then add the components above via the Visual Studio Installer UI, or:'
        Write-Host '     [CMD] vs_BuildTools.exe --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
        Write-Host '  2. (alternative to the VS CMake bundle) standalone CMake >= 3.24 and Ninja:'
        Write-Host '     [Either] winget install --id Kitware.CMake'
        Write-Host '     [Either] winget install --id Ninja-build.Ninja'
        Write-Host '  3. Python 3.x + jsonschema:'
        Write-Host '     [Either] winget install --id Python.Python.3.14'
        Write-Host '     [Either] python -m pip install jsonschema'
        Write-Host '  4. (only for -Compiler clang-cl / -Compiler auto) LLVM/clang-cl + lld-link (the "open compiler"):'
        Write-Host '     [Either] winget install --id LLVM.LLVM'
        Write-Host '     or add the VS Installer components:'
        Write-Host '       Microsoft.VisualStudio.Component.VC.Llvm.Clang'
        Write-Host '       Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset'
        Write-Host '     (clang-cl still rides on the MSVC SDK/CRT above — install both.)'
        Write-Host ''
        Write-Host 'Missing right now:'
        foreach ($m in $missing) { Write-Host "  - $m" }
        Write-Host ''
    }
    throw ($header + ' ' + ($missing -join ' | ') + ' (re-run with -Install for full install instructions; this script never auto-installs)')
}

# ---------------------------------------------------------------------------
# Import MSVC env, expose tools
# ---------------------------------------------------------------------------
Import-VcVarsEnv -VcVarsBat $vcvars

Add-PathDir -Directory (Split-Path -Parent $cmake)
Add-PathDir -Directory (Split-Path -Parent $ninja)
Add-PathDir -Directory (Split-Path -Parent $python)

# When clang-cl is the resolved compiler, prepend LLVM's bin so clang-cl.exe /
# lld-link.exe resolve. The vcvars import above STAYS — clang-cl runs in
# MSVC-compatible mode and needs the Windows SDK + MSVC CRT (INCLUDE/LIB) from it.
if ($compilerResolved -eq 'clang-cl' -and $clangcl) {
    Add-PathDir -Directory (Split-Path -Parent $clangcl)
    $env:VRCLIENT_CLANG_CL_EXE = $clangcl
    if ($lldlink) { $env:VRCLIENT_LLD_LINK_EXE = $lldlink }
}
else {
    # Make selection unambiguous for re-runs that flip back to msvc within a session.
    $env:VRCLIENT_CLANG_CL_EXE = $null
    $env:VRCLIENT_LLD_LINK_EXE = $null
}
$env:VRCLIENT_COMPILER = $compilerResolved

$env:VRCLIENT_VCVARS_BAT = $vcvars
$env:VRCLIENT_CMAKE_EXE = $cmake
$env:VRCLIENT_CTEST_EXE = $ctest
$env:VRCLIENT_NINJA_EXE = $ninja
$env:VRCLIENT_PYTHON_EXE = $python

# ---------------------------------------------------------------------------
# Verify the session is actually usable
# ---------------------------------------------------------------------------
$problems = New-Object System.Collections.Generic.List[string]

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $problems.Add("cl.exe not resolvable on PATH after importing $vcvars")
}
if (-not $env:INCLUDE) {
    $problems.Add("INCLUDE is empty after importing $vcvars (vcvars import failed silently)")
}
if (-not $env:LIB) {
    $problems.Add("LIB is empty after importing $vcvars (vcvars import failed silently)")
}

# NOTE: capture full output BEFORE taking the first line. Piping a live native command
# into Select-Object -First 1 makes PS 5.1 stop the pipeline and kill the process (exit -1).
$prev = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $cmakeOut = @(& $cmake --version 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 0) { $problems.Add("'$cmake --version' failed (exit $LASTEXITCODE)") }
    $cmakeVersionLine = ($cmakeOut | Select-Object -First 1)
    $ninjaOut = @(& $ninja --version 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 0) { $problems.Add("'$ninja --version' failed (exit $LASTEXITCODE)") }
    $ninjaVersionLine = ($ninjaOut | Select-Object -First 1)
    $pythonOut = @(& $python --version 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 0) { $problems.Add("'$python --version' failed (exit $LASTEXITCODE)") }
    $pythonVersionLine = ($pythonOut | Select-Object -First 1)

    # clang-cl version capture + liveness assert (only when clang-cl is the resolved
    # compiler). clang-cl --version prints the clang banner; exit 0 means the LLVM
    # install is usable in this (vcvars-imported) session.
    $clangVersionLine = $null
    if ($compilerResolved -eq 'clang-cl' -and $clangcl) {
        $clangOut = @(& $clangcl --version 2>&1 | ForEach-Object { "$_" })
        if ($LASTEXITCODE -ne 0) { $problems.Add("'$clangcl --version' failed (exit $LASTEXITCODE)") }
        $clangVersionLine = ($clangOut | Where-Object { $_ -match 'clang version' } | Select-Object -First 1)
        if (-not $clangVersionLine) { $clangVersionLine = ($clangOut | Select-Object -First 1) }
    }
}
finally { $ErrorActionPreference = $prev }

if ($cmakeVersionLine -match 'cmake version (\d+)\.(\d+)\.(\d+)') {
    $cmakeVer = New-Object System.Version([int]$matches[1], [int]$matches[2], [int]$matches[3])
    if ($cmakeVer -lt (New-Object System.Version(3, 24, 0))) {
        $problems.Add("CMake $cmakeVer is below the required minimum 3.24 (CMakeLists.txt cmake_minimum_required)")
    }
}

if ($problems.Count -gt 0) {
    throw ("setup-build-env: environment verification failed: " + ($problems -join ' | '))
}

$env:VRCLIENT_BUILD_ENV_READY = '1'

Write-EnvLog "MSVC vcvars64 : $vcvars"
Write-EnvLog "cmake         : $cmake ($cmakeVersionLine)"
Write-EnvLog "ninja         : $ninja (version $ninjaVersionLine)"
Write-EnvLog "ctest         : $ctest"
Write-EnvLog "python        : $python ($pythonVersionLine)"
Write-EnvLog "compiler      : $compilerResolved (requested: $Compiler)"
if ($compilerResolved -eq 'clang-cl') {
    Write-EnvLog "clang-cl      : $clangcl ($clangVersionLine)"
    if ($lldlink) { Write-EnvLog "lld-link      : $lldlink" }
}
Write-EnvLog 'Build environment ready (VRCLIENT_BUILD_ENV_READY=1).'
