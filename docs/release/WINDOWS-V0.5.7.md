# AVRcade 0.5.7 — installer upgrade protection

This installer update retains the [0.5.6 Cyberpunk startup recovery](WINDOWS-V0.5.6.md).

New installers reuse the registered installation directory, including a custom drive, so an upgrade updates the existing copy. Newer versions replace older packaged files, and reinstalling the same version can repair damaged files. User-created files are retained.

Before replacing files or closing applications, setup compares its numeric version with the registered version and the app and CLI binaries in the registered and selected directories. It refuses a downgrade with a clear message and preserves the existing installation. Checking both executables also covers portable copies and stale registration data.

Installers downloaded before this change cannot gain its downgrade check retroactively. Avoid running those older installers after upgrading. Separate portable folders are independent installations; an installer does not search drives and update every copy.

## Validation

An isolated test compiles the production installer with a random test AppId and small versioned executables. It verifies custom-folder installation, an upgrade without a directory override, same-version repair of a damaged CLI, numeric version ordering (0.5.7 to 0.5.10), registered downgrade refusal, stale-registration binary checks, and portable app-only and CLI-only downgrade refusal. User preferences and newer binaries must remain intact. The test never installs into or registers the real AVRcade installation.

Run `scripts/verify-installer-upgrades.ps1` from PowerShell. Logs, installer fixtures and result receipts remain under `artifacts/installer-upgrades/`.

## Install

Close AVRcade, then run the [v0.5.7 installer](https://github.com/Axleonex/AVRcade/releases/download/v0.5.7/AVRcade-Setup-0.5.7.exe). The installer defaults to the previous installation folder. Portable users can download the complete [v0.5.7 ZIP](https://github.com/Axleonex/AVRcade/releases/download/v0.5.7/AVRcade-0.5.7-win-x64.zip). [Checksums](https://github.com/Axleonex/AVRcade/releases/download/v0.5.7/SHA256SUMS.txt) cover both versioned and permanent download filenames. In-headset gameplay acceptance remains pending.
