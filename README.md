# AlphaRing: Ring Chief edition

A fork of [AlphaRing](https://github.com/WinterSquire/AlphaRing), the modding tool for
Halo: The Master Chief Collection, set up for **Halo LAN nights**:

- **Split-screen** for up to 4 players per MCC window (all games), from upstream AlphaRing.
- **Ring Chief**: sign in to your group on [halo.dronedude.app](https://halo.dronedude.app)
  with a Group ID and password. Everyone's Spartan (gamertag, service tag, armor, controls,
  team) is loaded from the group, and changes made on a phone apply in game.
- **Teams and scores**: the host sets teams on the website; results go back to the group's
  scoreboard, stats and voting.
- Works on one PC by itself, and with [Nucleus Co-op](https://github.com/SplitScreen-Me/splitscreenme-nucleus)
  for several MCC windows and several PCs on a LAN.

## Download and install

Get the latest `AlphaRing-RingChief-*.zip` from
**[Releases](https://github.com/Adam-Jackson-Engineer/AlphaRing/releases)** and follow
**[INSTALL.md](INSTALL.md)**. Short version: copy `WTSAPI32.dll` into
`Halo The Master Chief Collection\MCC\Binaries\Win64\` (or run the included
`install-ringchief.ps1`), launch MCC with **Anti-Cheat Disabled**, and sign in.

Supported MCC version: **1.3528.0.0**.

## Build

Visual Studio 2022 (Desktop C++), CMake 3.27+:

```
cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=release
cmake --build build --config Release
build\Release\ringchief_tests.exe
```

`build\Release\WTSAPI32.dll` is the mod. `ringchief_tests.exe` runs the unit tests; the
end-to-end test against a local copy of the website lives with the website code.

Ring Chief code is in `src/ringchief/` (login, WebSocket hub, loopback relay between MCC
windows, profile conversion, overlay window). The protocol is documented with the website.

## Credits

- [WinterSquire](https://github.com/WinterSquire/AlphaRing): AlphaRing.
- [wouter51](https://github.com/wouter51/AlphaRing): MCC 1.3528 update.
- Ring Chief additions: Adam Jackson.

Licence: see [LICENCE.txt](LICENCE.txt) (MIT, from upstream).
