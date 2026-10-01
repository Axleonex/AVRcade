# Release Doctor and Diagnostics

The release doctor evaluates only explicit caller-supplied shared state: OpenXR runtime availability, active package health, rollback availability, and diagnostic output access. It does not discover games, inspect game directories, or override a title safety policy.

The diagnostic writer accepts explicit entries and redacts a supplied user-profile root plus Windows user-name segments before export. It is local-only and does not upload data.

Every supported conversion should record the same qualification gate: flat launch, VR launch, stereo, input, safety/rollback, and diagnostics.
