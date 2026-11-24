# Installing AlphaRing (Ring Chief edition)

AlphaRing is a mod for **Halo: The Master Chief Collection on Steam** that adds split-screen
and loads your Spartan (gamertag, armor, controls, team) from your Ring Chief group on
[halo.dronedude.app](https://halo.dronedude.app). It is a single file, `WTSAPI32.dll`.

> **Supported MCC version: 1.3528.0.0** (the Steam build since September 2025). If Steam
> updates MCC, wait for a new AlphaRing release before playing with the mod.

## Before you start

- Halo MCC installed through Steam.
- Your own Spartan is set up from the invite link you were sent. You don't need anything
  else installed on your PC for that; it all happens on the website.

## Install (easy way)

1. Download `AlphaRing-RingChief-<version>.zip` from the
   [Releases page](https://github.com/Adam-Jackson-Engineer/AlphaRing/releases) and unzip it.
2. Close MCC, then double-click **`Install.bat`**.
   It finds MCC through Steam, backs up any existing `WTSAPI32.dll`, and copies the new one in.
   If Windows SmartScreen says "Windows protected your PC", click **More info** → **Run anyway**.
   If it says "Copy failed", right-click `Install.bat` → **Run as administrator**.
   (`Install.bat` just runs `install-ringchief.ps1`; you can run that yourself instead.)

## Install (by hand)

Copy `WTSAPI32.dll` into:

```
<Steam library>\steamapps\common\Halo The Master Chief Collection\MCC\Binaries\Win64\
```

(the folder that contains `MCC-Win64-Shipping.exe`). The usual Steam library is
`C:\Program Files (x86)\Steam`. If there's already a `WTSAPI32.dll` there, rename it first
so you can go back.

## Launch

1. In Steam press **Play** on Halo MCC and choose **"Play Halo: MCC Anti-Cheat Disabled"**.
   The mod can't run with Easy Anti-Cheat on, so online matchmaking is off while you use it.
   LAN / System Link and split-screen work.
2. The AlphaRing overlay opens. In the **Ring Chief** window press **Sign in with a code**.
   - On your phone open your personal link, tap **Enter a code**, and type the code on screen.
   - Choose a group, or no group (just you and friends on this PC).
   - Leave **Remember on this PC** ticked so you only do this once (lasts 90 days).
   - No phone? **Use a group password instead** takes the Group ID and password from your host
     (lasts two weeks; the password isn't saved).
3. Friends in the room open `halo.dronedude.app/code` on their phones and enter the code shown
   in the Ring Chief window. Their Spartan loads and their phone can control this PC.
4. Pick who is playing, and with which controller, in the Ring Chief window or with the phone
   **Remote**. The remote can also turn AlphaRing off and on.
5. Press **Insert** (or **Back + Start** on a controller) to hide or show the overlay.

**Play offline** uses the last group this PC downloaded, if the internet is down.

## Several MCC windows on one PC (Nucleus Co-op)

The first MCC window signs in; the others connect through it automatically (you'll see
"via main window"). If the first window closes, another one takes over.

## Updating

Download the new release and double-click `Install.bat` again (or copy the new DLL over the
old one). Your login and group stay.

## Uninstalling

Double-click **`Uninstall.bat`** (or run `install-ringchief.ps1 -Uninstall`), or delete `WTSAPI32.dll` from the folder above (and
rename your backup back if you had one). To forget this PC's login, delete
`C:\ProgramData\RingChief`.

## Troubleshooting

| Problem | Fix |
|---|---|
| No overlay at all | Make sure you launched with **Anti-Cheat Disabled**, and that the DLL is in `MCC\Binaries\Win64`, not the game's top folder. |
| Antivirus deletes or blocks the DLL | It hooks into the game, which some antivirus flags. Download only from the Releases page above, check the SHA-256 in `SHA256SUMS.txt`, and allow the file. |
| "Wrong group ID or password" | Group IDs ignore capitals; check the password with your host. After 10 wrong tries wait 15 minutes. |
| "This PC was signed out" | Sign in again. Logins run out, and end early if the host changes the group password, the owner signs the PC out, or gets a new personal link. |
| The code ran out or isn't accepted | Codes last 10 minutes. Press **Sign in with a code** for a new one. After 10 wrong codes wait 15 minutes. |
| Game crashes on start after an MCC update | MCC updated and AlphaRing doesn't support that version yet. Remove the DLL until a new release is out. |
| Your armor/controls didn't change | Changes apply at the next safe moment; if a match is running, they apply when the next one starts. |

Built from the `ringchief` branch of
[Adam-Jackson-Engineer/AlphaRing](https://github.com/Adam-Jackson-Engineer/AlphaRing), a fork of
[WinterSquire/AlphaRing](https://github.com/WinterSquire/AlphaRing) (with the MCC 1.3528 update
from [wouter51](https://github.com/wouter51/AlphaRing)).
