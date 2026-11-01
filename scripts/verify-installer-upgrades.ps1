#Requires -Version 5.1
<# Compile the production installer with an isolated AppId and tiny versioned
   executables. Never installs into, registers, or uninstalls the real AVRcade. #>
[CmdletBinding()]
param(
    [string]$InnoCompiler = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$parent = Join-Path $repo 'artifacts\installer-upgrades'
if (!$OutputDirectory) { $OutputDirectory = Join-Path $parent ([guid]::NewGuid().ToString('N')) }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (!$output.StartsWith($parent + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Test output must remain under artifacts/installer-upgrades.' }
if (Test-Path -LiteralPath $output) { throw 'Choose a new test output directory.' }
New-Item -ItemType Directory -Path $output -Force | Out-Null
$testId = [guid]::NewGuid().ToString().ToUpperInvariant()
$key = "Software\Microsoft\Windows\CurrentVersion\Uninstall\{$testId}_is1"
$registry = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, [Microsoft.Win32.RegistryView]::Registry32)
if ($registry.OpenSubKey($key)) { throw 'Isolated test registry key already exists.' }
$target = Join-Path $output 'custom-install'
$defaultTarget = Join-Path $output 'default-install'
$script = [IO.File]::ReadAllText((Join-Path $repo 'installer\AVRcade.iss'))
$script = $script.Replace('{D427E392-4F59-4BE7-A2A8-29AF8E60CB29}', ('{' + $testId + '}'))
$script = $script.Replace('DefaultDirName={localappdata}\Programs\AVRcade', "DefaultDirName=$defaultTarget")
# Do not create or change the user's real Start menu or desktop shortcuts.
$script = [regex]::Replace($script, '(?m)^Name: "\{auto(?:programs|desktop)\}[^\r\n]*', '; Isolated test: shortcuts omitted')
$iss = Join-Path $output 'AVRcade.iss'
[IO.File]::WriteAllText($iss, $script)
Copy-Item -LiteralPath (Join-Path $repo 'installer\INSTALL-NOTES.txt') -Destination $output
$compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$versions = @('0.5.6', '0.5.7', '0.5.10')
foreach ($version in $versions) {
    $payload = Join-Path $output "payload-$version"
    New-Item -ItemType Directory -Path $payload | Out-Null
    $cs = Join-Path $payload 'fixture.cs'
    '[assembly: System.Reflection.AssemblyFileVersion("' + $version + '.0")] public static class Fixture { public static void Main() {} }' | Set-Content -LiteralPath $cs -Encoding ASCII
    & $compiler /nologo /target:exe ("/out:" + (Join-Path $payload 'AVRcade.exe')) $cs
    if ($LASTEXITCODE -ne 0) { throw 'Fixture compilation failed.' }
    Copy-Item -LiteralPath (Join-Path $payload 'AVRcade.exe') -Destination (Join-Path $payload 'avrcade-cli.exe')
    & $InnoCompiler /Qp "/DAppVersion=$version" "/DSourceRoot=$payload" "/DOutputDir=$output" $iss | Out-File (Join-Path $output "compile-$version.log")
    if ($LASTEXITCODE -ne 0) { throw "Installer compilation failed for $version." }
}
$checks = New-Object 'Collections.Generic.List[object]'
function Run-Setup([string]$Version, [string]$Name, [string]$Directory, [bool]$Allowed = $true) {
    $log = Join-Path $output ($Name + '.log')
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/NOICONS', ('/LOG="' + $log + '"'))
    if ($Directory) { $arguments += '/DIR="' + $Directory + '"' }
    $process = Start-Process -FilePath (Join-Path $output "AVRcade-Setup-$Version.exe") -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(45000)) { throw "Installer test timed out: $Name (PID $($process.Id))." }
    $exit = $process.ExitCode
    if ($Allowed -and $exit -ne 0) { throw "Expected successful $Name, got exit $exit." }
    if (!$Allowed -and ($exit -ne 7 -or (Get-Content -LiteralPath $log -Raw) -notmatch 'AVRcade downgrade refused:')) { throw "Expected downgrade refusal for $Name, got exit $exit." }
    $checks.Add([pscustomobject]@{name=$Name;exit=$exit;passed=$true})
}
function Assert-Version([string]$Directory, [string]$Version) {
    foreach ($file in @('AVRcade.exe', 'avrcade-cli.exe')) {
        if ((Get-Item -LiteralPath (Join-Path $Directory $file)).VersionInfo.FileVersion -ne "$Version.0") { throw "Wrong installed $file version after $Version." }
    }
}
function Registered-Version {
    $registration = $registry.OpenSubKey($key)
    if (!$registration) { throw 'Isolated uninstall registration is missing.' }
    try { return $registration.GetValue('DisplayVersion') } finally { $registration.Dispose() }
}
try {
    Run-Setup '0.5.6' 'clean-install-custom-directory' $target
    Assert-Version $target '0.5.6'
    $preference = Join-Path $target 'user-preferences.json'
    '{"keep":"user settings"}' | Set-Content -LiteralPath $preference -Encoding ASCII
    $preferenceHash = (Get-FileHash -LiteralPath $preference).Hash
    Run-Setup '0.5.7' 'upgrade-reuses-custom-directory' ''
    Assert-Version $target '0.5.7'
    if (Test-Path -LiteralPath $defaultTarget) { throw 'Upgrade created a second install instead of reusing the previous directory.' }
    'damaged CLI fixture' | Set-Content -LiteralPath (Join-Path $target 'avrcade-cli.exe') -Encoding ASCII
    Run-Setup '0.5.7' 'same-version-repairs-damaged-cli' ''
    Assert-Version $target '0.5.7'
    Run-Setup '0.5.10' 'numeric-version-upgrade' ''
    Assert-Version $target '0.5.10'
    Run-Setup '0.5.7' 'registered-downgrade-refused' '' $false
    Assert-Version $target '0.5.10'
    if ((Registered-Version) -ne '0.5.10') { throw 'Downgrade modified the uninstall version.' }
    $registration = $registry.OpenSubKey($key, $true)
    try { $registration.SetValue('DisplayVersion', '0.5.6') } finally { $registration.Dispose() }
    Run-Setup '0.5.7' 'stale-registration-checks-installed-binaries' '' $false
    Assert-Version $target '0.5.10'
    if ((Get-FileHash -LiteralPath $preference).Hash -ne $preferenceHash) { throw 'Upgrade changed user preferences.' }
    # Portable copies have no registration. Keep a newer CLI alone to prove
    # the selected directory is checked independently of the registered app.
    $portable = Join-Path $output 'portable-install'
    New-Item -ItemType Directory -Path $portable | Out-Null
    Copy-Item -LiteralPath (Join-Path $output 'payload-0.5.10\avrcade-cli.exe') -Destination $portable
    $uninstall = Start-Process -FilePath (Join-Path $target 'unins000.exe') -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -Wait -PassThru
    if ($uninstall.ExitCode -ne 0) { throw 'Isolated uninstall failed.' }
    Run-Setup '0.5.7' 'portable-cli-downgrade-refused' $portable $false
    if ((Get-Item -LiteralPath (Join-Path $portable 'avrcade-cli.exe')).VersionInfo.FileVersion -ne '0.5.10.0') { throw 'Downgrade replaced portable CLI.' }
    Move-Item -LiteralPath (Join-Path $portable 'avrcade-cli.exe') -Destination (Join-Path $output 'preserved-portable-cli.exe')
    Copy-Item -LiteralPath (Join-Path $output 'payload-0.5.10\AVRcade.exe') -Destination $portable
    Run-Setup '0.5.7' 'portable-app-downgrade-refused' $portable $false
    if ((Get-Item -LiteralPath (Join-Path $portable 'AVRcade.exe')).VersionInfo.FileVersion -ne '0.5.10.0') { throw 'Downgrade replaced portable app.' }
    Write-Output "RESULT: installer-upgrades passed=$($checks.Count) isolated_app_id=$testId"
} finally {
    $checks | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'results.json') -Encoding UTF8
    # Only this random test identity may be uninstalled. No recursive deletion.
    if ($registry.OpenSubKey($key) -and (Test-Path -LiteralPath (Join-Path $target 'unins000.exe'))) {
        Start-Process -FilePath (Join-Path $target 'unins000.exe') -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -Wait | Out-Null
    }
    $registry.Dispose()
}
