#pragma once

#include <string>
#include <windows.h>

namespace MCC {
namespace Display {

// Initialize the display broadcaster
bool Initialize();

// Shutdown the display broadcaster
void Shutdown();

// Broadcast current stats to the display application
// Called from render loop every broadcast interval
void BroadcastStats();

// Enable/disable broadcasting
void SetEnabled(bool enabled);
bool IsEnabled();

// Set broadcast interval in milliseconds (default 250ms = 4Hz)
void SetBroadcastInterval(int ms);
int GetBroadcastInterval();

// Get last error message
const std::string& GetLastError();

// Check if currently connected to display
bool IsConnected();

} // namespace Display
} // namespace MCC
