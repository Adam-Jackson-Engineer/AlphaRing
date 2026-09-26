#pragma once

namespace MCC::Splitscreen {
    bool Initialize();
    void ImGuiContext();
    void Tick();   // every frame, overlay or not
}