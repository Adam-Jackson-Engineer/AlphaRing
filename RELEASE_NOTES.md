> **Pre-release:** builds and passes its automated tests (including an end-to-end test against the website), but hasn't been played in a live MCC match yet. Try it before a Halo night, not during one.

**For Halo MCC 1.3528.0.0 (Steam).** Install: download `AlphaRing-RingChief-*.zip`, unzip,
run `install-ringchief.ps1` (or copy `WTSAPI32.dll` into
`Halo The Master Chief Collection\MCC\Binaries\Win64\`). Full steps: [INSTALL.md](https://github.com/Adam-Jackson-Engineer/AlphaRing/blob/ringchief/INSTALL.md).

Launch MCC with **Anti-Cheat Disabled**, then sign in in the Ring Chief window with your
Group ID and password (server `halo.dronedude.app`).

### In this build
- Ring Chief sign-in (Server / Group ID / Password, remember on this PC, play offline).
- Profiles, armor, controls and teams come from the group on halo.dronedude.app; phone edits apply live.
- One PC connection shared by every MCC window on that PC (works with Nucleus Co-op), with failover.
- Players can pick their screen in the overlay or tap "That's me" on their phone.
- Local backups of the group's profiles on each PC (Ring Chief window → Local backups): play offline from any of the last 20.

### New in pre3
- Default: player N uses controller N, keyboard & mouse off (choose per screen in the Ring Chief window).
- Ring Chief and Session Details open at launch; "Splitscreen" is now "Advanced settings".
- Fixed controller layouts binding Sprint, Equipment, Scoreboard and left-weapon to the wrong actions.
- Layouts changed on the website ("Modded Zoom & Shoot") apply in game.
- Mouse: the multi-window blur fix only runs with Nucleus; overlay only keeps clicks on its own windows.

### Known limits
- Kills/deaths aren't read from the game yet; the host records scores on the website.
- Armor item numbers from the website haven't been confirmed in every game yet.

Check the download against `SHA256SUMS.txt`.
