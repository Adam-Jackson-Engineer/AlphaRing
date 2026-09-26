#include "Filesystem.h"

#include <filesystem>
#include <fstream>

// Cached DLL directory path
static std::string s_dllAlphaRingDir;

std::string AlphaRing::Filesystem::GetDllAlphaRingDir() {
    if (!s_dllAlphaRingDir.empty()) {
        return s_dllAlphaRingDir;
    }

    // Get the DLL's full path
    HMODULE hModule = AlphaRing::GetDllHandle();
    if (hModule) {
        char dllPath[MAX_PATH] = {};
        DWORD len = GetModuleFileNameA(hModule, dllPath, MAX_PATH);
        if (len > 0) {
            // DLL is at: <instance>/MCC/Binaries/Win64/WTSAPI32.dll
            // alpha_ring is at: <instance>/MCC/Binaries/Win64/alpha_ring
            std::filesystem::path p(dllPath);
            std::filesystem::path alphaRingDir = p.parent_path() / "alpha_ring";
            s_dllAlphaRingDir = alphaRingDir.string();

            // Write to a debug file to help diagnose multi-instance issues
            std::ofstream debugFile(s_dllAlphaRingDir + "/dll_path_debug.txt");
            if (debugFile.is_open()) {
                debugFile << "DLL Path: " << dllPath << std::endl;
                debugFile << "Alpha Ring Dir: " << s_dllAlphaRingDir << std::endl;
                debugFile << "PID: " << GetCurrentProcessId() << std::endl;
                debugFile.close();
            }

            return s_dllAlphaRingDir;
        }
    }

    // Fallback to CWD-relative path
    s_dllAlphaRingDir = "./alpha_ring";
    return s_dllAlphaRingDir;
}

bool AlphaRing::Filesystem::Init() {
    const char* home_dir = "../../../alpha_ring";

    if (!std::filesystem::exists(home_dir)) {
        std::filesystem::create_directories(home_dir);
    }

    return true;
}

bool AlphaRing::Filesystem::Shutdown() {
    return true;
}

void AlphaRing::Filesystem::GetDir(const wchar_t *path_in, wchar_t *path_out) {
    std::wcscpy(path_out, std::filesystem::absolute(path_in).c_str());
}

void AlphaRing::Filesystem::GetDir(const char *path_in, wchar_t *path_out) {
    std::wcscpy(path_out, std::filesystem::absolute(path_in).c_str());
}

void AlphaRing::Filesystem::GetResource(const char *path_in, wchar_t *path_out) {
    std::wcscpy(path_out,
                std::filesystem::absolute(std::filesystem::path("./alpha_ring").append(path_in)).c_str());
}

bool AlphaRing::Filesystem::Exist(const char *path) {
    return std::filesystem::exists(std::filesystem::path(path));
}

bool AlphaRing::Filesystem::Exist(const wchar_t *path) {
    return std::filesystem::exists(std::filesystem::path(path));
}

bool AlphaRing::Filesystem::Save(const char *file_name, const char *data, size_t size) {
    std::ofstream file(file_name, std::ios::binary);
    if (!file.is_open()) return false;

    file.write(data, size);
    file.close();

    return true;
}
