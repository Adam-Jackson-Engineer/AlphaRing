#pragma once
// JSON (de)serialization for MCC's CUserProfile and CGamepadMapping.
#include <string>
#include <nlohmann/json.hpp>

#include "CUserProfile.h"
#include "CGamepadMapping.h"

namespace MCC::Splitscreen {
    using json = nlohmann::json;

    std::string wstring_to_utf8(const std::wstring& wstr);
    std::wstring utf8_to_wstring(const std::string& str);

    void to_json(json& j, const CUserProfile& p);
    // Replaces p entirely (fields missing from j become zero).
    void from_json(const json& j, CUserProfile& p);
    // Overwrites only the fields present in j.
    void merge_json(const json& j, CUserProfile& p);

    void to_json(json& j, const CGamepadMapping& m);
    void from_json(const json& j, CGamepadMapping& m);
}
