# MCC Live Game Stats - Research Notes & Implementation Plan

## Overview

This document outlines findings and recommendations for implementing live game statistics (kills, deaths, assists, score) in AlphaRing for Halo MCC.

---

## Research Findings

### 1. MCC Stats API Limitations

- **Halo Waypoint API**: Third-party services report that Halo Waypoint stopped reliably updating MCC stats. ([Reddit discussion](https://www.reddit.com/r/HaloMCC/comments/1ffuk3b/is_there_a_way_to_view_stats_from_my_previous_5/))
- **In-game Career Stats**: MCC does track kills/deaths and displays them in Player Details > Career > Multiplayer screens ([Gaming StackExchange](https://gaming.stackexchange.com/questions/362649/are-deaths-tracked-in-halo-mcc), [Steam Community](https://steamcommunity.com/app/976730/discussions/0/1752394042378518346/))
- **External Trackers**: Services like [MCC Stats](https://mccstats.pythonanywhere.com/) and [Halo Tracker](https://tracker.gg/halo-mcc) provide stats via APIs, but these are for online matchmaking, not local/LAN games.

### 2. Cheat Engine / Memory Approach

**Existing Resources:**
- [FearLess Revolution - Halo MCC Tables](https://fearlessrevolution.com/viewtopic.php?t=30533) - Community cheat tables with various player values
- [Open Cheat Tables - Halo MCC](https://opencheattables.com/viewtopic.php?t=1100) - Consolidated tables
- [MCC Toolbox](https://guidedhacking.com/threads/halo-mcc-cheat-toolbox-v2-6-cheats-for-all-halo-games.16860/) - Multi-game cheat tool with documented addresses

**Key Findings from Cheat Engine Community:**
- Pointers are "not very reliable" in Halo MCC and change with updates (~3x/year)
- For values like score/kills, use "What writes to this address" to find stable code addresses
- Challenge stats (e.g., "31 out of 100 kills") can be found by value scanning
- Lone Wolf score is stored as a **float** value
- **NoEAC (No Easy Anti-Cheat)** required for memory access

### 3. Halo Engine Structures

**From AlphaRing codebase analysis:**

```cpp
// In lib/game/src/halo3/simulation/game_interface/simulation_game_engine_player.h
struct game_engine_player_state {
    int waypoint_action;
    float respawn_timer1;
    Vector3 dead_location;
    c_player_traits player_traits;
    bool blocking_teleporter;
    char un;
    __int16 lives_remaining;  // Only stat-like field present
    int last_betrayer;
    // ... no kills/deaths/assists fields
};
```

**Missing in current codebase:**
- `game_engine_globals` structure definition
- Player scoreboard/stats structure
- Kill/death event hooks

### 4. Technical Documentation

- [c20 Reclaimers Wiki](https://c20.reclaimers.net/general/mod-tools/) - Halo modding technical docs
- [Invader Toolkit](https://github.com/SnowyMouse/invader) - Open-source Halo tools (may have structure info)
- OpenSauce extended scripting functions (legacy CE modding)

---

## Recommended Approach

### Option A: Memory Scanning (Most Feasible)

**Workflow:**
1. Start a match with known score (e.g., get 1 kill = score of 100)
2. Use Cheat Engine to scan for value 100 (try both int32 and float)
3. Get another kill, scan for new value
4. Narrow down to 1-2 addresses
5. Find pointer chain to base address using "Pointer Scan"
6. Map the structure around the score address (kills/deaths/assists likely nearby)

**Implementation:**
```cpp
// Pseudo-code for reading live stats
struct PlayerScoreboard {
    int32_t score;      // +0x00 (hypothetical)
    int16_t kills;      // +0x04
    int16_t deaths;     // +0x06
    int16_t assists;    // +0x08
    // ... discover actual layout via CE
};

// Base pointer (needs to be found via pointer scan)
uintptr_t player_scoreboard_base = FindPattern("...");
PlayerScoreboard* GetPlayerStats(int player_index) {
    return (PlayerScoreboard*)(player_scoreboard_base + player_index * sizeof(PlayerScoreboard));
}
```

**Pros:**
- Can work without game source code
- Direct access to live values

**Cons:**
- Pointers break with MCC updates
- Requires maintenance per update
- May differ per Halo game (Reach vs H3 vs H4)

### Option B: Hook Game Events (More Robust)

**Concept:**
Instead of reading memory, hook the functions that INCREMENT kill/death counters.

**Workflow:**
1. Find the function called when a player dies (likely in game simulation code)
2. Hook it to capture: killer_id, victim_id, weapon_type
3. Maintain our own stats in AlphaRing

**Potential Hook Points (from codebase):**
- `CGameEngine::set_event()` - Already hooked for team changes
- `game_setup` hook in CGameManager - Already present
- Kill event likely goes through network/simulation layer

**Pros:**
- More stable than raw pointers
- Can capture additional context (weapon, location, etc.)

**Cons:**
- Requires finding the exact function signature
- May miss stats from game start

### Option C: UI Scraping (Fallback)

**Concept:**
Read the scoreboard UI data that MCC already displays.

**Workflow:**
1. Find where MCC populates the in-game scoreboard texture/text
2. Hook the data source, not the rendered output

**Likely Location:**
- Scoreboard is populated when player presses TAB/Select
- Data source is the same as what we need

---

## Implementation Plan

### Phase 1: Research & Discovery (1-2 hours)
1. Download Cheat Engine table from FearLess Revolution
2. Start a local Reach match (NoEAC mode)
3. Get some kills, scan for score/kill values
4. Document found addresses and offsets
5. Attempt pointer scan for stable base

### Phase 2: Prototype Memory Reader (2-3 hours)
1. Add memory reading utility to AlphaRing
2. Implement `GetPlayerStats(slot)` function
3. Test stability across match restart
4. Log stats every 0.5 seconds to validate

### Phase 3: Integrate with UI (1-2 hours)
1. Update `Game Stats` tab to poll live stats
2. Show kills/deaths/assists/score per player
3. Handle "not available" gracefully

### Phase 4: Match End Capture (1 hour)
1. On `IsInGame()` transition false → snapshot final stats
2. Write to JSONL history file
3. Update career stats in profile

### Phase 5: Maintenance (Ongoing)
1. After MCC updates, verify addresses still work
2. Update pointer offsets as needed
3. Consider adding auto-detection via pattern scanning

---

## Known Challenges

1. **Per-Game Differences**: Reach, H3, H4 may store stats differently
2. **EAC**: Easy Anti-Cheat blocks memory access; requires NoEAC launch
3. **Update Breakage**: 343i updates MCC ~3x/year, breaking pointers
4. **Multiplayer Sync**: LAN stats may be tracked differently than local

---

## Alternative: Manual Stats Entry

If live stats hooking proves too fragile, implement:
- Manual "Log Stats" button that user fills in after match
- Import from screenshot (future: OCR the scoreboard)
- Rely on match history for progression

---

## References

- [FearLess Revolution - Halo MCC Cheat Tables](https://fearlessrevolution.com/viewtopic.php?t=30533)
- [Gaming StackExchange - MCC Stats Tracking](https://gaming.stackexchange.com/questions/362649/are-deaths-tracked-in-halo-mcc)
- [Steam Community - Reach K/D Tracking](https://steamcommunity.com/app/976730/discussions/0/3909619202494875690/)
- [c20 Reclaimers Wiki - Halo Modding](https://c20.reclaimers.net/general/mod-tools/)
- [GitHub - Invader Toolkit](https://github.com/SnowyMouse/invader)
- [MCC Stats Tracker](https://mccstats.pythonanywhere.com/)
- [Halo Tracker](https://tracker.gg/halo-mcc)

---

## Practical Next Step Checklist (NoEAC)

This is a hands-on checklist for finding live stats memory addresses in Halo Reach.

### Prerequisites

- [ ] **Install Cheat Engine** (latest from cheatengine.org)
- [ ] **Launch MCC in NoEAC mode**:
  ```
  # Steam launch option, or rename/remove EasyAntiCheat folder
  # Or use "-noeac" command line flag if available
  ```
- [ ] **Download FearLess Revolution Halo MCC Table** (optional but helpful):
  - https://fearlessrevolution.com/viewtopic.php?t=30533
  - Load the .CT file in Cheat Engine for reference pointers

### Step 1: Find the Score Address

1. [ ] Start **Halo Reach** (Firefight or custom game for easy testing)
2. [ ] Open Cheat Engine and attach to `MCC-Win64-Shipping.exe`
3. [ ] Reset scan, set Value Type to **4 Bytes** (also try **Float** if no results)
4. [ ] Scan for your current score (e.g., 0 at start)
5. [ ] Get a kill, scan for new score value (e.g., 100)
6. [ ] Get another kill, scan again
7. [ ] Narrow down to **1-3 addresses**
8. [ ] **Right-click → "Find out what writes to this address"**
   - This finds the instruction that updates score, which is more stable than the address

### Step 2: Map the Scoreboard Structure

1. [ ] Right-click the found score address → **"Browse this memory region"**
2. [ ] Look at surrounding bytes for patterns:
   - Score is often near kills/deaths/assists
   - Watch bytes change as you play
3. [ ] Document the structure:
   ```
   Offset    Type      Field         Example Value
   +0x00     int32     score         300
   +0x04     int16     kills         3
   +0x06     int16     deaths        1
   +0x08     int16     assists       0
   +0x0A     ???       ???           ???
   ```

### Step 3: Find the Base Pointer

1. [ ] With the score address selected, run **Pointer Scan** (Ctrl+P)
2. [ ] Set max level to 4, max offset to 0x2000
3. [ ] Get another kill (score changes), rescan
4. [ ] Look for stable pointer chains like:
   ```
   MCC-Win64-Shipping.exe+XXXXXXX -> +0x8 -> +0x10 -> +0x4 (score)
   ```
5. [ ] Verify pointer survives match restart

### Step 4: Find Per-Player Offset (Splitscreen)

1. [ ] Start 2-player splitscreen match
2. [ ] Find score for Player 1 (slot 0) using steps above
3. [ ] Find score for Player 2 (slot 1) separately
4. [ ] Calculate the offset between them:
   ```
   Player 1 base: 0x12345678
   Player 2 base: 0x12345700
   Player offset: 0x88 (136 bytes per player)
   ```

### Step 5: Test for Stability

1. [ ] End match, start new match
2. [ ] Verify pointer chain still resolves
3. [ ] If broken, use **instruction scanning** instead:
   - Find the MOV/ADD that writes score
   - Read from the register it targets

### Notes

- **Halo Reach** uses different structures than H3/H4 - repeat for each game
- Values may be **signed** (deaths can be negative in some modes)
- Score may be stored as **float** in some contexts
- **Firefight** stats may differ from PvP multiplayer

### If All Else Fails

- Hook the function that writes to score (via assembly patching)
- Scrape the in-game scoreboard UI
- Use manual stats entry with OCR for screenshots

---

## Next Steps

1. **Immediate**: Use Cheat Engine to find scoreboard addresses in Halo Reach
2. **If Successful**: Implement memory reader in AlphaRing
3. **If Blocked**: Fall back to manual stats entry or event hooking

**Document Created**: 2026-01-25
**Status**: Research phase - awaiting memory structure discovery
