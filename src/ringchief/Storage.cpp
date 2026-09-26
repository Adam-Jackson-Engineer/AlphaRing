#include "Storage.h"

#include <windows.h>
#include <shlobj.h>
#include <wincrypt.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace RingChief::Storage {

    namespace {
        const char kEntropy[] = "RingChief-login-v1";

        bool ReadJson(const fs::path& p, json& out) {
            std::ifstream f(p);
            if (!f) return false;
            try { out = json::parse(f); return true; } catch (...) { return false; }
        }

        bool WriteJson(const fs::path& p, const json& j) {
            fs::path tmp = p; tmp += ".tmp";
            {
                std::ofstream f(tmp, std::ios::trunc);
                if (!f) return false;
                f << j.dump(1);
                if (!f) return false;
            }
            std::error_code ec;
            fs::rename(tmp, p, ec);
            return !ec;
        }
    }

    std::string Dir() {
        static std::string cached;
        if (!cached.empty()) return cached;
        PWSTR base = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &base))) return "";
        fs::path dir = fs::path(base) / L"RingChief";
        CoTaskMemFree(base);
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (!fs::is_directory(dir)) return "";
        cached = dir.string();
        return cached;
    }

    std::string Protect(const std::string& plain) {
        DATA_BLOB in{ (DWORD)plain.size(), (BYTE*)plain.data() };
        DATA_BLOB entropy{ (DWORD)sizeof(kEntropy), (BYTE*)kEntropy };
        DATA_BLOB out{};
        if (!CryptProtectData(&in, L"RingChief", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return "";
        DWORD len = 0;
        CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &len);
        std::string b64(len, '\0');
        CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &len);
        LocalFree(out.pbData);
        b64.resize(len);
        return b64;
    }

    std::string Unprotect(const std::string& b64) {
        if (b64.empty()) return "";
        DWORD len = 0;
        if (!CryptStringToBinaryA(b64.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &len, nullptr, nullptr)) return "";
        std::vector<BYTE> bin(len);
        if (!CryptStringToBinaryA(b64.c_str(), 0, CRYPT_STRING_BASE64, bin.data(), &len, nullptr, nullptr)) return "";
        DATA_BLOB in{ len, bin.data() };
        DATA_BLOB entropy{ (DWORD)sizeof(kEntropy), (BYTE*)kEntropy };
        DATA_BLOB out{};
        if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return "";
        std::string plain((char*)out.pbData, out.cbData);
        SecureZeroMemory(out.pbData, out.cbData);
        LocalFree(out.pbData);
        return plain;
    }

    bool LoadLogin(SavedLogin& out) {
        std::string dir = Dir();
        json j;
        if (dir.empty() || !ReadJson(fs::path(dir) / "login.json", j) || !j.is_object()) return false;
        out.server = j.value("server", "");
        out.group_id = j.value("groupId", "");
        out.expires_at = j.value("expiresAt", "");
        out.token = Unprotect(j.value("token", ""));
        return !out.server.empty() && !out.group_id.empty();
    }

    bool SaveLogin(const SavedLogin& login) {
        std::string dir = Dir();
        if (dir.empty()) return false;
        json j{
            {"server", login.server}, {"groupId", login.group_id},
            {"token", login.token.empty() ? "" : Protect(login.token)}, {"expiresAt", login.expires_at},
        };
        return WriteJson(fs::path(dir) / "login.json", j);
    }

    void ClearLogin() {
        std::string dir = Dir();
        if (dir.empty()) return;
        std::error_code ec;
        fs::remove(fs::path(dir) / "login.json", ec);
    }

    bool LoadGroupCache(json& welcome) {
        std::string dir = Dir();
        return !dir.empty() && ReadJson(fs::path(dir) / "group-cache.json", welcome) && welcome.is_object();
    }

    void SaveGroupCache(const json& welcome) {
        std::string dir = Dir();
        if (!dir.empty()) WriteJson(fs::path(dir) / "group-cache.json", welcome);
    }
}
