#include "Storage.h"

#include <windows.h>
#include <shlobj.h>
#include <wincrypt.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace fs = std::filesystem;

namespace RingChief::Storage {

    namespace {
        const char kEntropy[] = "RingChief-login-v1";
        std::mutex g_dir_mutex;
        std::string g_dir_override;
        std::string g_dir_cached;

        bool ReadJson(const fs::path& p, json& out) {
            std::ifstream f(p, std::ios::binary);
            if (!f) return false;
            try { out = json::parse(f); return true; } catch (...) { return false; }
        }

        // Temp file + rename, so a crash never leaves half a file behind.
        bool WriteJson(const fs::path& p, const json& j) {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            fs::path tmp = p;
            tmp += ".tmp";
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                if (!f) return false;
                f << j.dump(1);
                f.flush();
                if (!f) return false;
            }
            fs::rename(tmp, p, ec);
            if (ec) { fs::remove(tmp, ec); return false; }
            return true;
        }

        std::string UtcStamp(bool for_file) {
            // Include milliseconds so two backups in one second don't collide.
            SYSTEMTIME st;
            GetSystemTime(&st);
            char buf[40];
            if (for_file) snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d-%03d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
            else snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
            return buf;
        }

        fs::path GroupDir(const std::string& group_id) {
            std::string dir = Dir();
            if (dir.empty() || GroupKey(group_id).empty()) return {};
            return fs::path(dir) / "groups" / GroupKey(group_id);
        }
    }

    std::string Dir() {
        std::lock_guard<std::mutex> lock(g_dir_mutex);
        if (!g_dir_override.empty()) {
            std::error_code ec;
            fs::create_directories(g_dir_override, ec);
            return g_dir_override;
        }
        if (!g_dir_cached.empty()) return g_dir_cached;
        PWSTR base = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &base))) return "";
        fs::path dir = fs::path(base) / L"RingChief";
        CoTaskMemFree(base);
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (!fs::is_directory(dir)) return "";
        g_dir_cached = dir.string();
        return g_dir_cached;
    }

    void SetDirForTests(const std::string& dir) {
        std::lock_guard<std::mutex> lock(g_dir_mutex);
        g_dir_override = dir;
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

    std::string GroupKey(const std::string& group_id) {
        std::string key;
        for (char c : group_id) {
            unsigned char u = static_cast<unsigned char>(c);
            if (std::isalnum(u)) key += static_cast<char>(std::tolower(u));
            else if (c == '-' || c == '_') key += c;
        }
        return key.substr(0, 64);
    }

    bool IsValidSnapshot(const json& j) {
        return j.is_object() && j.contains("profiles") && j["profiles"].is_array();
    }

    std::string ContentHash(const json& welcome) {
        // FNV-1a over the canonical dump of profiles + night: fine for "did anything change".
        std::string s = (welcome.contains("profiles") ? welcome["profiles"].dump() : "") + "|" +
                        (welcome.contains("night") ? welcome["night"].dump() : "");
        uint64_t h = 1469598103934665603ull;
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
        char buf[20];
        snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
        return buf;
    }

    bool SaveGroupCache(const std::string& group_id, const json& welcome) {
        fs::path dir = GroupDir(group_id);
        if (dir.empty() || !IsValidSnapshot(welcome)) return false;
        json j = welcome;
        j["savedAt"] = UtcStamp(false);
        return WriteJson(dir / "cache.json", j);
    }

    bool LoadGroupCache(const std::string& group_id, json& welcome) {
        fs::path dir = GroupDir(group_id);
        if (dir.empty()) return false;
        json j;
        if (ReadJson(dir / "cache.json", j) && IsValidSnapshot(j)) { welcome = j; return true; }
        for (const auto& b : ListBackups(group_id)) {
            if (LoadBackup(b.path, j)) { welcome = j; return true; }
        }
        return false;
    }

    std::vector<BackupInfo> ListBackups(const std::string& group_id) {
        std::vector<BackupInfo> list;
        fs::path dir = GroupDir(group_id);
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir / "backups", ec)) return list;
        for (const auto& e : fs::directory_iterator(dir / "backups", ec)) {
            if (!e.is_regular_file() || e.path().extension() != ".json") continue;
            std::string name = e.path().filename().string();
            if (name.rfind("profiles-", 0) != 0) continue;
            json j;
            if (!ReadJson(e.path(), j) || !IsValidSnapshot(j)) continue;
            BackupInfo b;
            b.path = e.path().string();
            b.saved_at = j.value("savedAt", "");
            b.profile_count = j["profiles"].size();
            b.bytes = e.file_size(ec);
            list.push_back(b);
        }
        // File names sort by time; newest first.
        std::sort(list.begin(), list.end(), [](const BackupInfo& a, const BackupInfo& b) { return a.path > b.path; });
        return list;
    }

    bool LoadBackup(const std::string& path, json& welcome) {
        json j;
        if (!ReadJson(path, j) || !IsValidSnapshot(j)) return false;
        welcome = j;
        return true;
    }

    std::string SaveBackup(const std::string& group_id, const json& welcome, size_t keep) {
        fs::path dir = GroupDir(group_id);
        if (dir.empty() || !IsValidSnapshot(welcome)) return "";
        auto existing = ListBackups(group_id);
        std::string hash = ContentHash(welcome);
        if (!existing.empty()) {
            json newest;
            if (LoadBackup(existing.front().path, newest) && ContentHash(newest) == hash) return "";
        }
        json j = welcome;
        j["type"] = "welcome";
        j["savedAt"] = UtcStamp(false);
        // Names sort by time; never reuse one (two backups in the same millisecond).
        fs::path file = dir / "backups" / ("profiles-" + UtcStamp(true) + ".json");
        for (int i = 0; i < 50 && (fs::exists(file) || (!existing.empty() && file.string() <= existing.front().path)); i++) {
            Sleep(1);
            file = dir / "backups" / ("profiles-" + UtcStamp(true) + ".json");
        }
        if (!WriteJson(file, j)) return "";
        // Prune: keep the newest `keep` (including the one just written).
        existing = ListBackups(group_id);
        std::error_code ec;
        for (size_t i = keep; i < existing.size(); i++) fs::remove(existing[i].path, ec);
        return file.string();
    }
}
