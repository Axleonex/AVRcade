# RV There Yet? release checklist

Automated preparation is implemented; headset and clean-machine gates remain.

- [ ] Exact game build fingerprint is in `supported_builds`.
- [ ] Profile archive import rejects changed, missing, extra, code, and traversal entries.
- [ ] Existing profile is backed up; rollback restores unchanged files and preserves edits.
- [ ] VDXR route passes the headset protocol.
- [ ] SteamVR/OpenXR route result is recorded separately.
- [ ] Complete gamepad/comfort matrix passes after a cold launch.
- [ ] Private two-player co-op matrix passes, or remains explicitly unsupported.
- [ ] Update, interrupted import, repair, rollback, runtime restart, headset reconnect, controller reconnect, game crash, and clean exit are exercised.
- [ ] Frame-time behavior is recorded on foot, driving, winch, busy physics, and co-op.
- [ ] `dotnet test client/VrClient.sln -c Release` passes.
- [ ] `python tests/native/tooling/validate_orchestration_catalog.py` passes.

Do not change `verification.profile_status` to `verified` until evidence exists,
and never infer motion controls from generic UEVR controller enablement.
