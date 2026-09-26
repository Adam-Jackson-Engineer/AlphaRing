# Installing AlphaRing (Ring Chief edition)

AlphaRing is a mod for **Halo: The Master Chief Collection on Steam** that adds split-screen
and loads your Spartan (gamertag, armor, controls, team) from your Ring Chief group on
[halo.dronedude.app](https://halo.dronedude.app). It is a single file, `WTSAPI32.dll`.

> **Supported MCC version: 1.3528.0.0** (the Steam build since September 2025). If Steam
> updates MCC, wait for a new AlphaRing release before playing with the mod.

## Before you start

- Halo MCC installed through Steam.
- Your host sends you a **Group ID** and **password** (for example `WarGames2027`).
- Your own Spartan is set up from the invite link you were sent. You don't need anything
  else installed on your PC for that; it all happens on the website.

## Install (easy way)

1. Download `AlphaRing-RingChief-<version>.zip` from the
   [Releases page](https://github.com/Adam-Jackson-Engineer/AlphaRing/releases) and unzip it.
2. Right-click `install-ringchief.ps1` → **Run with PowerShell**.
   It finds MCC through Steam, backs up any existing `WTSAPI32.dll`, and copies the new one in.
   If Windows asks whether to run the script, choose **Open** / **Run once**.

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
2. The AlphaRing overlay opens. In the **Ring Chief** window enter:
   - **Server:** `halo.dronedude.app` (already filled in)
   - **Group ID:** what your host sent (for example `WarGames2027`)
   - **Password:** the group password
   - Tick **Remember on this PC** so you only do this once (the password isn't saved; the
     PC gets a login that lasts two weeks).
3. Pick who is playing on each screen in the Ring Chief window, or have each player open the
   group page on their phone and tap **"That's me"** next to their screen.
4. Press **Insert** (or **Back + Start** on a controller) to hide or show the overlay.

**Play offline** uses the last group this PC downloaded, if the internet is down.

## Several MCC windows on one PC (Nucleus Co-op)

The first MCC window signs in; the others connect through it automatically (you'll see
"via main window"). If the first window closes, another one takes over.

## Updating

Download the new release and run `install-ringchief.ps1` again (or copy the new DLL over the
old one). Your login and group stay.

## Uninstalling

Run `install-ringchief.ps1 -Uninstall`, or delete `WTSAPI32.dll` from the folder above (and
rename your backup back if you had one). To forget this PC's login, delete
`C:\ProgramData\RingChief`.

## Troubleshooting

| Problem | Fix |
|---|---|
| No overlay at all | Make sure you launched with **Anti-Cheat Disabled**, and that the DLL is in `MCC\Binaries\Win64`, not the game's top folder. |
| Antivirus deletes or blocks the DLL | It hooks into the game, which some antivirus flags. Download only from the Releases page above, check the SHA-256 in `SHA256SUMS.txt`, and allow the file. |
| "Wrong group ID or password" | Group IDs ignore capitals; check the password with your host. After 10 wrong tries wait 15 minutes. |
| "Your login for this PC expired" | Enter the group password again (logins last 14 days, and end early if the host changes the password). |
| Game crashes on start after an MCC update | MCC updated and AlphaRing doesn't support that version yet. Remove the DLL until a new release is out. |
| Your armor/controls didn't change | Changes apply at the next safe moment; if a match is running, they apply when the next one starts. |

Built from the `ringchief` branch of
[Adam-Jackson-Engineer/AlphaRing](https://github.com/Adam-Jackson-Engineer/AlphaRing), a fork of
[WinterSquire/AlphaRing](https://github.com/WinterSquire/AlphaRing) (with the MCC 1.3528 update
from [wouter51](https://github.com/wouter51/AlphaRing)).
