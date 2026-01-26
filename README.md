# Ring Chief (AlphaRing Mod for Halo MCC) — Install & Use

This mod adds a persistent local profile system for Halo: The Master Chief Collection (Steam) with multi-controller splitscreen support:
- Player profiles (name, service tag, armor/colors, button layout, team preference)
- Session tools (teams, roster/session details)
- Works well for "LAN night" style play on a single PC

> IMPORTANT: This mod requires launching MCC **without Easy Anti-Cheat (NoEAC)**.

---

## Requirements

- Halo: The Master Chief Collection (Steam)
- Windows (Steam Deck/Linux also supported — see below)
- Latest [Microsoft Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe)
- 1–4 controllers (Xbox controllers recommended)

---

## Install

### Files you need
1) `WTSAPI32.dll`
2) The `alpha_ring\` folder (contains profiles/config/resources)

### Copy locations (Steam install path example)
Copy the DLL to:

```
...\Halo The Master Chief Collection\MCC\Binaries\Win64\WTSAPI32.dll
```

Copy the resources folder to game root:

```
...\Halo The Master Chief Collection\alpha_ring\
```

Result should look like:
```
Halo The Master Chief Collection\
  alpha_ring\
    profiles\
      Rookie.json
      Spartan_Red.json
      Spartan_Blue.json
      Elite_Gold.json
      ...
    instance_config.json
    init.lua
    patch.xml
  MCC\
    Binaries\
      Win64\
        WTSAPI32.dll          <-- mod DLL
        MCC-Win64-Shipping.exe
        ...
```

### Steam Deck / Linux

Add the following command in the Steam Game Launch Options:
```
WINEDLLOVERRIDES="WTSAPI32=n,b" %command%
```

---

## Launch (NoEAC)

You must launch MCC without EAC for the DLL injection to work.

**Recommended:** use the official "No Anti-Cheat" launch option from Steam (if available in your MCC install).
If you don't have that option, you must use a NoEAC method appropriate for your setup (do not use the mod online).

---

## Usage

1) Launch MCC (NoEAC)
2) Open the mod UI — default hotkeys:
   - **Insert** key, or
   - **Controller Back + Start** simultaneously
3) Enable splitscreen and set player count (2–4)
4) For each player slot:
   - Select a profile from the dropdown
   - Confirm controller input assignment (defaults: Controller 1→P1, Controller 2→P2, etc.)
   - Adjust team preference / button layout / armor as desired
5) Click **Save** to persist profile changes to disk
6) Start your match via **Custom Games → System Link / LAN**

### Controller Navigation
When the menu is open, use **Right Stick** to move the cursor and **RB** to click. Game input is disabled while the menu is open.

---

## Profile System

### Creating a Profile
- Open the mod UI → select a player slot tab
- Click **New Profile**
- Enter a name → the profile is saved as `<Name>.json` in the profiles folder
- Customize armor, colors, service tag, button layout, team, etc.

### Editing a Profile
- Select the profile from the dropdown
- Change any settings (armor, colors, name, team, etc.)
- Click **Save** to write changes to disk
- The `*unsaved` indicator shows when you have pending changes

### Profile Fields
| Field | Description |
|-------|-------------|
| Display Name | Shown in-game for this player |
| Service Tag | 3–4 character tag displayed in-game |
| Team Preference | Auto-assigned team when match starts |
| Controller Preset | Button layout (Default, Bumper Jumper, Recon, etc.) |
| Armor / Colors | Full Spartan customization (helmet, chest, shoulders, visor, colors) |
| Emblem | Foreground, background, and color configuration |

### Profile Storage
Profiles are stored as JSON files in:
```
...\Halo The Master Chief Collection\alpha_ring\profiles\
```
You can back up, copy, or share profile files freely.

---

## Splitscreen Setup

### Controller Assignment
- Each player slot is assigned a controller (1–4)
- Defaults: Controller 1→Player 1, Controller 2→Player 2, etc.
- Change via the **Input** dropdown on each player slot
- Controller assignment is runtime-only and not saved to profiles

### Keyboard / Mouse
- Disabled by default for Player 1
- Can be enabled via Settings menu → "Enable K/M for player1"

### Teams
- Set per-profile via the **Team Preference** dropdown
- Teams auto-apply when a match starts
- Use **Session Details → Set Teams Now** to force-apply mid-match

### Armor
- Armor changes are applied automatically at match start (two-shot apply for reliability)
- If armor doesn't appear to update, use **Session Details → Apply All Armor**

---

## Session Details Window

Access via the menu bar → **Session Details**

Shows:
- Session status (Enabled/Disabled, In Game/In Menu, player count)
- Roster table (all players, names, tags, teams, profiles)
- Quick actions: Set Teams Now, Refresh Profiles, Apply All Armor

---

## UI Sections (per player slot)

All sections start collapsed and can be expanded by clicking:

| Section | Contents |
|---------|----------|
| **Profile Settings** | Display name, service tag, button layout, team preference |
| **Spartan Appearance** | Model type, armor pieces, colors, emblem, randomize buttons |
| **Customize Button Mapping** | Custom gamepad mapping, axis options (Invert Y Look) |
| **Advanced Settings** | Sensitivity, deadzone, FOV, HUD scale, brightness, accessibility |

---

## Use with Nucleus Co-op

AlphaRing works with [Nucleus Co-op](https://github.com/SplitScreen-Me/splitscreenme-nucleus) for multi-instance splitscreen across multiple monitors.

When using Nucleus:
1. Install AlphaRing to your MCC directory as described above
2. Nucleus will launch multiple MCC instances
3. Each instance loads its own `instance_config.json` for slot/controller assignment
4. Profiles are shared across all instances from the same `alpha_ring\profiles\` folder

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Mod UI doesn't appear | Ensure `WTSAPI32.dll` is in `MCC\Binaries\Win64\` and you launched NoEAC |
| Profiles missing after restart | Check that `alpha_ring\profiles\` is at the **game root** (not inside `MCC\Binaries\Win64\`) |
| Player 1 armor not applying | Open Session Details → click "Apply All Armor" |
| Controller not detected | Check Input dropdown; ensure controller is connected before launching |
| K/M not working for Player 1 | Enable via Settings menu → "Enable K/M for player1" |

---

## Sample Profiles

The mod ships with several sample profiles:
- **Rookie** — Default Spartan
- **Spartan_Red** — Red team preset
- **Spartan_Blue** — Blue team preset
- **Spartan_Green** — Green team preset
- **Elite_Gold** — Elite model with gold colors

---

## Notes

- LAN/System Link must be selected manually in MCC menus (no auto-LAN)
- Profiles persist across sessions (names, teams, armor/colors, button layout)
- Use only in offline / NoEAC environments — do not use with matchmaking
- The mod does not modify any game files; removing `WTSAPI32.dll` fully uninstalls it

---

## AlphaRing Upstream

This is a fork of [AlphaRing](https://github.com/WinterSquire/AlphaRing) — a modding tool for MCC.

### Additional Features (upstream)
- Camera Tool (Halo 3)
- Object Browser (Halo 3)

### Credits
- [Assembly](https://github.com/XboxChaos/Assembly) for the tag group research
- [Blender](https://github.com/blender/blender) for the bezier curve calculation
- [Priception](https://github.com/Priception) for adding UI controller support and helping with the interface and crash issue
