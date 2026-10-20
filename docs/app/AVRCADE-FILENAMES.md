# AVRcade filenames and existing installations

The desktop launcher is `AVRcade.exe`; its command-line companion is
`avrcade-cli.exe`. Windows ignores filename case, so those names are distinct.
New per-user installations use `%LOCALAPPDATA%\Programs\AVRcade`.
The canonical local build/install command is `scripts/install-local-avrcade.ps1`;
the previous script name forwards its arguments for compatibility.

Desktop and Start menu shortcuts must target `AVRcade.exe` in the new install.
Keep the previous installation until existing Steam wrapper references have
been migrated. On the next explicit wrapper setup, AVRcade replaces a leading
legacy `vrclient.exe wrap --` prefix instead of adding a second wrapper.
The launcher prefers `avrcade-cli.exe` and can still locate a legacy CLI.
`AVRCADE_REPO_ROOT` is the configuration override; `VRCLIENT_REPO_ROOT` remains
accepted for existing scripts.

New settings use `%LOCALAPPDATA%\AVRcade`; theme preferences use
`%APPDATA%\AVRcade`. Existing controller mappings, preferred defaults, OBS
connection preferences and mod-manager selections are copied only when the new
setting is absent. Existing settings are never overwritten during migration.
Old settings remain as a rollback source. Existing tool caches and managed
profile directories can be reused in place to preserve absolute references.

Internal C# project/namespace names, native mod DLL/ASI names, native streaming
IPC paths and installed ownership/schema markers retain their technical IDs.
Renaming those requires a coordinated native/protocol migration; a cosmetic
rename could break installed mods or make the app lose track of owned files.
Historical snapshots and update receipts retain their original filenames.
