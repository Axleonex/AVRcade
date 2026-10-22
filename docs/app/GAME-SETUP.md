# Game setup presentation

Every VR catalogue entry uses the same setup card above the launch modes.
Installed package files, required Unreal profiles, and detected headset software
are separate facts. Package installation never implies a completed headset test.
Flat-only entries do not advertise an installer for a missing VR conversion.

The card calls the existing game installer. Unity routes keep their one-click
installation; UEVR preparation downloads tools and its reviewed game profile in
sequence. Cyberpunk retains its Nexus authorization and scoped ZIP detection.
Manual profile import captures the game and install root before the file picker
opens, then reports validation/import results to that game's card.

Transient preparation and import state is indexed by game slug and install root,
survives catalogue refreshes, and cannot move to a different selected game.
An explicit Recheck setup clears finished operation messages and reads current
installation facts. Active operations retain their progress. The progress bar
is indeterminate; messages identify the operation rather than inventing a percent.

`config/game-setup-guides.json` contains recovery steps for routes with extra
requirements. Adding a guide does not change package versions or installers.
Guides appear in a collapsed section below the main controls. Ordinary routes
show generic recovery help after a failure. UEVR ZIP import and source links
live there as a fallback to the automatic profile download. Cyberpunk keeps its
existing specialized download recovery section.

YAPYAP and the native Fallout routes retain their existing preparation paths;
the generic Unity installer is not offered as a substitute for those packages.
Credits and controller preferences use their existing independent metadata.

## Game updates and VR compatibility

Compatibility belongs to each game's VR package and the user's selected installation.
Cyberpunk reads the executable's product version against the supported versions in
`game_compatibility`, with the author's versioned requirements URL as provenance.
Recorded developer Steam build IDs and executable hashes are observations, not
universal identity requirements for this package. Exact-binary native bridges,
such as classic San Andreas, keep their separate adapter checks.

Update mods remains available when VR is blocked. It checks author releases and
reports whether a newer release exists before any installation, transaction recovery
or game-file writes. Unknown compatibility never authorizes an arbitrary new package.
Recheck and the setup card retain the actual compatibility reason even when file
and controller checks pass. Steam validation restores game files; it does not prove
VR compatibility or downgrade the game. Monitor launches do not require VR's game
version or VR dependencies. No launcher can guarantee support for future game
updates before its third-party VR backend supports them.
