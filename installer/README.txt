AVRcade

Open AVRcade from the Start menu. The library lists the supported games; games
you own on Steam show their cover art and whether they are ready for VR.

Open a game and choose how to play. Every game offers the same four ways:

  VR only       - the game in your headset with just its VR conversion.
  VR + mods     - VR together with the mods from your mod manager.
  Mods, no VR   - on your monitor with your mods.
  Vanilla       - the game as it shipped: no mods, no VR.

A mode that is not ready says why and gives you its one next step (for example
"Install VR mod" or "Set up mods") instead of a greyed-out button.

Before the first VR launch
  1. Install the game in Steam.
  2. Start SteamVR or Virtual Desktop and connect your headset.
  3. Open the game in AVRcade and press the button on "VR only".

AVRcade downloads third-party files (VR mods, UEVR) only when you press the
matching setup button. Supported port packages use their configured versions
and verification rules. Update mods checks this game's supported updates;
Thunderstore routes resolve current dependencies when you request an update.
AVRcade never downloads games and never installs mods into a mod manager
profile; your manager keeps full ownership of its profiles.

Modded play is for private sessions with friends who agreed to it.

Troubleshooting guides are installed under docs\.

Stream or record with OBS (optional)
  1. Install OBS Studio 28 or newer separately.
  2. In OBS, enable Tools > WebSocket Server Settings and copy its password.
  3. Open a game page, choose OBS Stream (Stream on native routes), and connect.
  4. Select an OBS scene with the game window or VR runtime mirror as a source.
  5. Check the picture and audio before choosing Record or Go live.

For SteamVR, open Display VR View / Show VR View from its desktop menu,
then select VR View in an OBS Window Capture source. If Automatic freezes
the image, choose Windows 10 (1903 and up) as the capture method.
Keep VR View open. Configure game audio and microphone separately in OBS.
The full guide is installed at docs\STREAMING.md.
