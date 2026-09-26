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

        auto* g = AlphaRing::Global::Global();

        // Optionally hide focus loss from MCC (prevents the pause blur when several MCC
        // windows share a screen). Only for Nucleus instances by default: for a single
        // window it can confuse MCC's own mouse handling.
        if (g->keep_focus) {
            switch (uMsg) {
                case WM_ACTIVATE:
                    if (LOWORD(wParam) == WA_INACTIVE) return 0;
                    break;
                case WM_ACTIVATEAPP:
                    if (wParam == FALSE) return 0;
                    break;
                case WM_KILLFOCUS:
                    return 0;
            }
        }

        switch (uMsg) {
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
        bool click = uMsg == WM_LBUTTONDOWN;
        if (click) g->clicks_seen++;

        // The overlay keeps mouse input only while the pointer is over one of its windows.
        if (io.WantCaptureMouse && g->show_imgui && uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST) {
            if (click) g->clicks_to_overlay++;
            return true;
        }
        if (io.WantCaptureKeyboard && g->show_imgui && (uMsg == WM_KEYDOWN || uMsg == WM_KEYUP || uMsg == WM_CHAR))
            return true;

        if (click) g->clicks_to_game++;
        return CallWindowProc(oldWndProc, hWnd, uMsg, wParam, lParam);
    }

    bool Initialize() {
        oldWndProc = (WNDPROC)SetWindowLongPtr(Graphics()->hwnd, GWLP_WNDPROC, (LONG_PTR)dWndProc);

        return oldWndProc != nullptr;
    }
}
