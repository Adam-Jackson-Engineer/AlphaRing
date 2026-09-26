#pragma once

#include <string>

#define DefGlobal(name) \
    struct name##_t;    \
    extern name##_t s_##name; \
    inline name##_t* name() {return &s_##name;} \
    struct name##_t

#define ImplGlobal(name) \
    name##_t s_##name;

namespace AlphaRing::Global {
    DefGlobal(Global) {
        bool wireframe;
        bool wireframe_model;
        bool wireframe_structure;
        bool show_imgui = true;
        // on menu
        bool show_imgui_mouse = true;
        bool pause_game_on_menu_shown = true;
        bool disable_input_on_menu_shown = true;
        // Hide focus loss from MCC (stops the pause blur when several MCC windows share a
        // screen). Turned on automatically for Nucleus instances; off for a single window.
        bool keep_focus = false;
        // Left-click diagnostics (Advanced settings): seen by the window / passed to MCC /
        // kept by the overlay because the pointer was over an overlay window.
        int clicks_seen = 0;
        int clicks_to_game = 0;
        int clicks_to_overlay = 0;
    };

    namespace Halo3 {
        DefGlobal(Physics) {
            bool enable_bump_possession;
        };

        DefGlobal(Render) {
            bool model;
            bool structure;
        };
    }

    namespace MCC {
        DefGlobal(Splitscreen) {
            bool b_override;
            int player_count = 4;

            // player 0
            bool b_player0_use_km = false;  // Default OFF - user must enable manually
            bool b_override_profile = false;
            bool b_use_player0_profile = true;
        };

    }

}