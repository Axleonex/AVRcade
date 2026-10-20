#Requires -Version 5.1
# Compatibility entry point for existing local build commands.
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'install-local-avrcade.ps1') @args
