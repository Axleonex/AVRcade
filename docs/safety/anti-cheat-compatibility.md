# Anti-Cheat Compatibility and Launch-Only Policy

VRClient does not inject into anti-cheat-protected games. It does not tamper with,
disable, hide from, patch, spoof, or evade anti-cheat products, services, drivers,
processes, or modules.

B8 adds a compatibility route for games where the safe and legitimate behavior is
to launch or orchestrate the game without touching its process. When anti-cheat is
detected, the route can be `launch-only` only if configuration explicitly records a
legitimate non-injection path, such as a normal launch, an official VR mode, or a
sanctioned external profile. The injector remains disabled for that route.

If anti-cheat is detected and no legitimate launch-only path is configured, VRClient
blocks. If anti-cheat state is uncertain, VRClient blocks. The priority is:

1. Block on uncertainty.
2. Use launch-only when anti-cheat is detected and a legitimate non-injection path exists.
3. Permit injector-capable routing only after anti-cheat is positively clear and the
   normal Phase 7 safety verdict still allows or warns.

The launch-only router is a decision layer. It does not start games, inspect live
processes, attach, inject, hook, patch, or change anti-cheat state. It consumes
already-collected Phase 7 observations and data under `config/safety/`.

Manager-facing states and reason codes:

| Route | Reason code | Meaning |
|---|---|---|
| `launch-only` | `anti_cheat_launch_only_injection_disabled` | Anti-cheat was detected; a legitimate non-injection path exists; injector is disabled. |
| `blocked` | `anti_cheat_no_sanctioned_launch_path` | Anti-cheat was detected and no legitimate non-injection path is configured. |
| `blocked` | `anti_cheat_signal_uncertain` | Anti-cheat state could not be established; default-block. |
| `injector-capable` | `injector_capable_no_anti_cheat_detected` | Anti-cheat is positively clear; this is only routing eligibility and still requires the normal safety verdict. |
