#include "mcc.h"

#include <offset_mcc.h>

#include "CGameManager.h"
#include "CGameGlobal.h"

#include "mcc/module/Module.h"
#include "mcc/network/Network.h"
#include "mcc/splitscreen/Splitscreen.h"
#include "mcc/display/DisplayBroadcaster.h"
#include "mcc/server/RingChiefClient.h"

namespace MCC {
    static bool* bIsInGame;
    static float (__fastcall* deltaTime)(long long qpc);

    float DeltaTime(__int64 a1) {
        return deltaTime(a1);
    }

    bool IsInGame() {
        return *bIsInGame;
    }

    bool Initialize() {
        bool result;
        CGameEngine** ppGameEngine;
        CGameManager* game_manager;
        CDeviceManager** device_manager;

        AlphaRing::Hook::Offset({
            {0x4000BA0/*0x3FFCAA8*/ , 0x3E4F9F8/*0x3E4B048*/, (void**)&ppGameEngine},
            {0x3F7B190/*0x3F76E50*/ , 0x3DCA200/*0x3DC54D0*/, (void**)&game_manager},
            {0x4001B78/*0x3FFFFF8*/ , 0x3E509C0/*0x3E4E590*/, (void**)&device_manager},
            {OFFSET_MCC_PF_DELTA_TIME, OFFSET_MCC_WS_PF_DELTA_TIME, (void**)&deltaTime},
            {0x4000B9F/*0x3FFCAA7*/ ,0x3E4F9F7/*0x3E4B047*/, (void**)&bIsInGame},
            {0x4000BC8/*0x3FFCAC0*/ , 0x3E4FA18/*0x3E4B060*/, (void**)&g_ppGameGlobal},
        });

        if (ppGameEngine == nullptr) { LOG_ERROR("MCC: failed to get ppGameEngine"); return false; }
        if (game_manager == nullptr) { LOG_ERROR("MCC: failed to get pGameManager"); return false; }
        if (device_manager == nullptr) { LOG_ERROR("MCC: failed to get ppDeviceManager"); return false; }

        result = CGameEngine::Initialize(ppGameEngine);
        if (!result) { LOG_ERROR("MCC: failed to initialize GameEngine"); return false; }

        result = CGameManager::Initialize(game_manager);
        if (!result) { LOG_ERROR("MCC: failed to initialize GameManager"); return false; }

        if (GameManager() == nullptr) { LOG_ERROR("MCC: GameManager is null"); return false; }

        result = CDeviceManager::Initialize(device_manager);
        if (!result) { LOG_ERROR("MCC: failed to initialize DeviceManager"); return false; }

        if (!Module::Initialize())
        {
            LOG_ERROR("MCC: failed to initialize Module");
            return false;
        }

        if (!Splitscreen::Initialize())
        {
            LOG_ERROR("MCC: failed to initialize Splitscreen");
            return false;
        }

        if (!Network::Initialize())
            return false;

        if (!Display::Initialize())
        {
            LOG_WARNING("MCC: failed to initialize Display (non-fatal)");
            // Non-fatal - display is optional
        }

        if (!Server::Client::Initialize())
        {
            LOG_WARNING("MCC: failed to initialize Ring Chief Server client (non-fatal)");
            // Non-fatal - server connection is optional
        }

        return true;
    }
}
