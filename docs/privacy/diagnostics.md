# VRClient Diagnostics and Privacy

> **DRAFT — PENDING HUMAN LEGAL/PRIVACY REVIEW.**
> This document describes the platform's intended diagnostics and privacy behavior so the
> manager app can quote it. It is accurate to the current implementation but has **not** been
> reviewed as a formal privacy policy and must not be published as one until it completes
> human review. A separate, legally reviewed Privacy Policy must accompany any public
> distribution.

---

## 1. Core promise: local, user-owned, opt-in

- **Diagnostics stay local.** Logs and crash records are written to the user's own machine.
- **There is no automatic telemetry upload.** The platform does not send diagnostics, usage
  data, or any other information anywhere on its own. Nothing leaves the machine
  automatically.
- **Export is opt-in and user-triggered.** A diagnostics bundle is only ever created when the
  user chooses to create one. The manager app surfaces this as a deliberate "Export
  diagnostics" action — never a background or default behavior.
- **The user owns the export.** The resulting bundle is a folder on the user's disk. What
  happens to it next — keeping it, deleting it, or sharing it for support — is entirely the
  user's decision.

These properties are visible in the implementation: every export manifest records
`"user_triggered": true` and `"automatic_upload": false`, and there is no network/upload code
path in the diagnostics export module.

## 2. Optional local telemetry aggregation

Crash/telemetry aggregation is **off by default**. When enabled by explicit local
configuration or an explicit export request, the exporter writes a small local aggregate
summary into the user-triggered diagnostics bundle. The summary contains counts only, such
as local log file count, local log record count, and local crash artifact count. It does
not include user paths, secrets, game files, save data, raw log content, or crash dump
contents.

The aggregate file is written under `telemetry/summary.json` inside the local bundle and
records `"local_only": true`, `"automatic_upload": false`, and `"network_upload": false`.
No endpoint, upload client, background sender, or network dependency exists for this path.
Any future network/upload feature remains out of scope until a separate privacy design and
explicit user consent decision exists.

## 3. What an export contains

When the user triggers an export, the platform assembles a self-contained bundle directory
named `vrclient-diagnostics-<timestamp>-<session>` under a local output directory (default
`diagnostic-exports/`). A bundle may contain:

- **`logs/`** — copies of recent diagnostic log files (redacted, see below).
- **`crashes/`** — copies of crash records, if any (redacted).
- **`config/runtime-profile.json`** and **`config/diagnostics-profile.json`** — copies of the
  relevant configuration files, if present (redacted).
- **`environment.json`** — basic, non-identifying environment facts (runtime version, process
  architecture, and an explicit `"automatic_upload": false` marker). This file is included
  **only when the user opts in** to including environment details (`include_environment`); it
  can be left out.
- **`telemetry/summary.json`** - optional local aggregate counts, written only when
  telemetry aggregation is explicitly enabled for the user-triggered export. It records
  `local_only:true`, `automatic_upload:false`, and `network_upload:false`.
- **`manifest.json`** — a listing of the bundle: its type, the session id, the
  `user_triggered`/`automatic_upload` flags, whether environment and telemetry summary were
  included, and the file list.

The bundle contains only the diagnostic material described above. It is not a copy of the
user's games, save data, or personal files.

## 4. How redaction works

Before any log, crash record, or config file is written into the bundle, its text is passed
through a redaction step. Redaction:

- **Replaces the user's home/profile path** with the literal token `%USERPROFILE%`. This
  covers both the value of the `USERPROFILE` environment variable and any
  `C:\Users\<name>\...` style Windows user path found in the text, so the user's account name
  does not leak through file paths.
- **Masks secrets in key/value form.** Values that look like an `api_key`, `api-key`,
  `apikey`, `token`, `secret`, or `password` assignment are replaced with `[REDACTED]`, while
  keeping the key name so the log still makes structural sense.

Redaction is applied uniformly to every copied log, crash, and config file in the bundle.
Because redaction is best-effort pattern matching, users sharing a bundle for support are
still encouraged to glance over it first; the manager app should remind them that they
control whether to share it.

## 5. Safety refusal diagnostics

When the safety layer refuses a target, the refusal is recorded as a structured diagnostic
event carrying a machine `reason_code` (and identity fields such as `game_id` / `build_id`).
This makes refusals auditable in the local logs and is why refusal reasons can appear in an
exported bundle. These events follow the same local-only, no-auto-upload, redacted-on-export
rules as all other diagnostics. The human-readable meaning of each `reason_code` is documented
in [`../safety/safety-policy.md`](../safety/safety-policy.md).

## 6. What the platform does not do

- It does not upload diagnostics, logs, crashes, or usage data automatically.
- It does not phone home, beacon, or transmit data in the background.
- It does not enable telemetry aggregation by default.
- It does not collect personal files, save games, or game content into diagnostics.
- It does not require a diagnostics export to function; export exists only to help the user
  get support if they choose to ask for it.

## 7. Relationship to other documents

- Safety behavior and refusal reasons: [`../safety/safety-policy.md`](../safety/safety-policy.md)
- Legal disclaimer: [`../legal/disclaimer.md`](../legal/disclaimer.md)
