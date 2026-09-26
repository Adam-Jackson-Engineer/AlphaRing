#include "common.h"

#include "input/Input.h"
#include "hook/Hook.h"
#include "render/Render.h"
#include "mcc/mcc.h"
#include "filesystem/Filesystem.h"

// Global DLL handle - set in DllMain
static HMODULE g_hDllModule = nullptr;

HMODULE AlphaRing::GetDllHandle() {
    return g_hDllModule;
}

static bool Initialize() {
    bool result;

    result = AlphaRing::Log::Init();
    if (!result) return false;

    result = AlphaRing::Hook::Initialize();
    if (!result) { LOG_ERROR("failed to initialize hook"); return false; }

    result = AlphaRing::Filesystem::Init();
    if (!result) { LOG_ERROR("failed to initialize filesystem"); return false; }

    result = AlphaRing::Input::Init();
    if (!result) { LOG_ERROR("failed to initialize input"); return false; }

    result = AlphaRing::Render::Initialize();
    if (!result) { LOG_ERROR("failed to initialize render"); return false; }

    result = MCC::Initialize();
    if (!result) { LOG_ERROR("failed to initialize mcc"); return false; }

    return true;
}

static bool Shutdown() {
    LOG_INFO("Shutting down");

    AlphaRing::Filesystem::Shutdown();
    AlphaRing::Input::Shutdown();
    AlphaRing::Hook::Shutdown();
    AlphaRing::Log::Shutdown();

    return true;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hDllModule = hModule;
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)Initialize, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (reserved == nullptr)
            return Shutdown();
    }

    return true;
}