# SteamOS/Linux Compatibility Boundary

VRClient remains Windows-first. The shared release layer now represents three explicit package states:

| Host | Package declaration | Result |
|---|---|---|
| Windows | Windows supported | Compatible |
| Linux/SteamOS | Native Linux supported | Compatible |
| SteamOS | Proton explicitly supported | Compatible via Proton |
| Any other combination | No matching declaration | Unsupported |

This is a capability contract, not an execution claim. A compatible result still requires package integrity, title safety, runtime readiness, and title-level Proton/headset testing. No Linux process launch, OpenXR runtime selection, or game-specific behavior is introduced by this phase.

Package paths must be relative and traversal-free so manifests can use a single logical path form across Windows and Linux.
