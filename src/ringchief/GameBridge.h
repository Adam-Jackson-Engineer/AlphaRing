#pragma once
// Glue between the Ring Chief session and MCC: owns the Session, implements GameAdapter
// with ProfileManager / CGameManager, and draws the Ring Chief overlay window.
namespace RingChief {
    class Session;
}

namespace RingChief::Game {
    void Initialize();                  // once, from Splitscreen::Initialize
    void Tick();                        // every frame, overlay visible or not
    void Shutdown();

    void DrawWindow(bool* open);        // the "Ring Chief" ImGui window
    void DrawMenuStatus();              // compact status in the main menu bar
    bool WantsAttention();              // true until the player logs in or picks offline

    void ReloadProfiles();              // re-push the group's profiles into ProfileManager
    void ReportMatchEnd(int match_epoch);
    void SlotsChanged();                // a slot's profile was picked in the overlay

    Session* Get();
}
