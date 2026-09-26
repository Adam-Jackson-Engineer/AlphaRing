#include <tchar.h>
#include "Window.h"

#include "common.h"

#include "global/Global.h"

#include "imgui.h"

#include "../D3d11/D3d11.h"

LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace AlphaRing::Render::Window {
    WNDPROC oldWndProc = nullptr;

    //todo: WM_IME_COMPOSITION Support
    static LRESULT dWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
            return true;

        // Block activation/deactivation messages to prevent MCC's pause blur
        // This prevents the "weirdly shaded" screen when clicking other windows
        switch (uMsg) {
            case WM_ACTIVATE:
                // Block deactivation (WA_INACTIVE = 0), allow activation
                if (LOWORD(wParam) == WA_INACTIVE)
                    return 0;
                break;
            case WM_ACTIVATEAPP:
                // Block app deactivation (wParam = FALSE)
                if (wParam == FALSE)
                    return 0;
                break;
            case WM_KILLFOCUS:
                // Block focus loss entirely
                return 0;
            case WM_KEYDOWN: {
                switch (wParam) {
                    case VK_F4:
                        AlphaRing::Global::Global()->show_imgui = !AlphaRing::Global::Global()->show_imgui;
                        break;
                }
                break;
            }
        }

        auto& io = ImGui::GetIO();

        if (io.WantCaptureMouse)
            if(AlphaRing::Global::Global()->show_imgui)
                return true;

        return CallWindowProc(oldWndProc, hWnd, uMsg, wParam, lParam);
    }

    bool Initialize() {
        oldWndProc = (WNDPROC)SetWindowLongPtr(Graphics()->hwnd, GWLP_WNDPROC, (LONG_PTR)dWndProc);

        return oldWndProc != nullptr;
    }
}
