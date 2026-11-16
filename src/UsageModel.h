#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "../third_party/json.hpp"

namespace usage {

struct Window {
    std::optional<double> remaining;
    std::optional<int64_t> resetsAt;
};

struct Detail {
    std::wstring type, status, issued, expires, name, description;
};

struct Snapshot {
    Window fiveHour, weekly;
    std::optional<int64_t> resetCount;
    std::vector<Detail> details;
    // False means the backend supplied no detailed snapshot; keep the previous list.
    bool detailsIncluded = false;
};

Snapshot ParseSnapshot(const nlohmann::json& result, bool full);
std::wstring FormatTime(std::optional<int64_t> epochSeconds);
std::wstring FormatRemaining(std::optional<double> remaining);

} // namespace usage
