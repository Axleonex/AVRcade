# Release Profiles and Variants

Release profiles contain user-owned preferences only: controller convention, HUD visibility/scale, turn preference, and vignette preference. They do not write a game profile or replace per-game control/config logic.

Variants are evaluated separately from the UI:

- `Flat` and `VrClient` variants require a supported build, safety approval, and all required shared packages.
- An `ExternalVrLaunchOnly` variant may launch its local external-VR setup but can never request VRClient injection.
- Unsupported, unsafe, or incomplete variants remain explainable through a stable reason code rather than disappearing from the library.

The JSON persistence contract is `config/schemas/release-profile.schema.json`.
