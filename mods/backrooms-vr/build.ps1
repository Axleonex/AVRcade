# [PowerShell] powershell -ExecutionPolicy Bypass -File mods/backrooms-vr/build.ps1
param([switch]$RunTests)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$output = Join-Path $repo 'build\backrooms-vr'
$headers = Join-Path $output 'include\uevr'
New-Item -ItemType Directory -Path $headers -Force | Out-Null
# SDK headers are MIT licensed and pinned to the installed nightly backend.
$revision = '1b1693e820413a8afb1fdfd8f9da43a19870042e'
foreach ($header in @('API.h', 'API.hpp', 'Plugin.hpp')) {
    Invoke-WebRequest -Uri "https://raw.githubusercontent.com/praydog/UEVR/$revision/include/uevr/$header" -OutFile (Join-Path $headers $header)
}
. (Join-Path $repo 'tools\setup-build-env.ps1') -Quiet
Push-Location $output
try {
    & cl.exe /nologo /std:c++20 /EHsc /O2 /MT /LD /W4 /external:W0 "/external:I$(Join-Path $output 'include')" /DWIN32_LEAN_AND_MEAN /DNOMINMAX (Join-Path $PSScriptRoot 'BackroomsTextInput.cpp') user32.lib /link /OUT:BackroomsTextInput.dll
    if ($LASTEXITCODE -ne 0) { throw 'Backrooms text input helper build failed.' }
    if ($RunTests) {
        & cl.exe /nologo /std:c++20 /EHsc /O2 /MT /W4 /external:W0 "/external:I$(Join-Path $output 'include')" /DWIN32_LEAN_AND_MEAN /DNOMINMAX (Join-Path $PSScriptRoot 'tests\text_input_tests.cpp') user32.lib /Fe:BackroomsTextInputTests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Backrooms text input test build failed.' }
        & .\BackroomsTextInputTests.exe
        if ($LASTEXITCODE -ne 0) { throw 'Backrooms text input tests failed.' }
    }
} finally { Pop-Location }
Write-Output (Join-Path $output 'BackroomsTextInput.dll')
