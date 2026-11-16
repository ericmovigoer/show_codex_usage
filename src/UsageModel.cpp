#include "UsageModel.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace usage {
namespace {
using nlohmann::json;
const std::wstring kMissing = L"未提供";

const json* Field(const json& object, const char* key) {
    auto it = object.find(key);
    return it == object.end() || it->is_null() ? nullptr : &*it;
}

std::optional<int64_t> Integer(const json& object, const char* key) {
    const auto* value = Field(object, key);
    if (!value) return {};
    if (value->is_number_unsigned()) {
        const auto number = value->get<uint64_t>();
        if (number > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)()))
            throw std::runtime_error("Incompatible account/rateLimits/read integer range");
        return static_cast<int64_t>(number);
    }
    if (!value->is_number_integer())
        throw std::runtime_error("Incompatible account/rateLimits/read integer field");
    return value->get<int64_t>();
}

std::wstring Wide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!count) throw std::runtime_error("Invalid UTF-8 in account/rateLimits/read");
    std::wstring out(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), out.data(), count);
    return out;
}

std::optional<std::string> String(const json& object, const char* key) {
    const auto* value = Field(object, key);
    if (!value) return {};
    if (!value->is_string())
        throw std::runtime_error("Incompatible account/rateLimits/read string field");
    return value->get<std::string>();
}

std::wstring Original(const std::optional<std::string>& value) {
    if (!value || value->empty()) return kMissing;
    return Wide(*value) + L"（服务端原文）";
}

std::wstring Type(const json& credit) {
    auto text = String(credit, "resetType");
    if (!text) return kMissing;
    if (*text == "codexRateLimits") return L"Codex 额度重置";
    if (*text == "unknown") return L"未知";
    return Original(text);
}

std::wstring Status(const json& credit) {
    auto text = String(credit, "status");
    if (!text) return kMissing;
    if (*text == "available") return L"可用";
    if (*text == "redeeming") return L"正在兑换";
    if (*text == "redeemed") return L"已兑换";
    if (*text == "unknown") return L"未知";
    return Original(text);
}

std::wstring DisplayText(const json& credit, const char* key) {
    const auto text = String(credit, key);
    if (!text || text->empty()) return kMissing;
    // Translate only exact, unambiguous known phrases. Other free text is preserved.
    if (*text == "Codex rate limit reset") return L"Codex 额度重置";
    if (*text == "Rate limit reset") return L"额度重置";
    if (*text == "One-time rate limit reset") return L"一次性额度重置";
    if (*text == "Reset your Codex rate limits.") return L"重置 Codex 额度。";
    return Original(text);
}

void ReadWindow(const json& bucket, const char* key, Snapshot& out) {
    const auto* item = Field(bucket, key);
    if (!item) return;
    if (!item->is_object()) throw std::runtime_error("Incompatible rate limit window");
    const auto duration = Integer(*item, "windowDurationMins");
    Window window;
    if (const auto* used = Field(*item, "usedPercent")) {
        if (!used->is_number()) throw std::runtime_error("Incompatible usedPercent field");
        const double percent = used->get<double>();
        if (std::isfinite(percent)) window.remaining = std::clamp(100.0 - percent, 0.0, 100.0);
    }
    window.resetsAt = Integer(*item, "resetsAt");
    if (duration == 300) out.fiveHour = window;
    else if (duration == 10080) out.weekly = window;
}
} // namespace

Snapshot ParseSnapshot(const json& result, bool full) {
    if (!result.is_object()) throw std::runtime_error("Incompatible account/rateLimits/read response");
    if (!result.contains("rateLimits") && !result.contains("rateLimitsByLimitId") &&
        !result.contains("rateLimitResetCredits"))
        throw std::runtime_error("Unsupported account/rateLimits/read response fields");
    Snapshot out;
    const json* bucket = nullptr;
    if (const auto* groups = Field(result, "rateLimitsByLimitId")) {
        if (!groups->is_object()) throw std::runtime_error("Incompatible rateLimitsByLimitId field");
        bucket = Field(*groups, "codex");
    }
    if (!bucket) bucket = Field(result, "rateLimits");
    if (bucket) {
        if (!bucket->is_object()) throw std::runtime_error("Incompatible rateLimits field");
        ReadWindow(*bucket, "primary", out);
        ReadWindow(*bucket, "secondary", out);
    }
    if (const auto* summary = Field(result, "rateLimitResetCredits")) {
        if (!summary->is_object()) throw std::runtime_error("Incompatible reset-credit summary");
        out.resetCount = Integer(*summary, "availableCount");
        if (out.resetCount && *out.resetCount < 0)
            throw std::runtime_error("Invalid negative availableCount");
        if (const auto* credits = Field(*summary, "credits")) {
            if (!credits->is_array()) throw std::runtime_error("Incompatible reset-credit details");
            if (full) {
                out.detailsIncluded = true;
                for (const auto& credit : *credits) {
                    if (!credit.is_object()) throw std::runtime_error("Incompatible reset-credit row");
                    Detail detail;
                    detail.type = Type(credit);
                    detail.status = Status(credit);
                    detail.issued = FormatTime(Integer(credit, "grantedAt"));
                    // Explicit null means no expiry; absence means the information was omitted.
                    detail.expires = credit.contains("expiresAt") && credit["expiresAt"].is_null()
                        ? L"长期有效" : FormatTime(Integer(credit, "expiresAt"));
                    detail.name = DisplayText(credit, "title");
                    detail.description = DisplayText(credit, "description");
                    out.details.push_back(std::move(detail));
                }
            }
        }
    }
    return out;
}

std::wstring FormatTime(std::optional<int64_t> epochSeconds) {
    if (!epochSeconds) return kMissing;
    // FILETIME is in 100 ns ticks since 1601. Check the range before arithmetic.
    constexpr int64_t offset = 11644473600LL;
    if (*epochSeconds < -offset || *epochSeconds > 253402300799LL) return L"时间无效";
    const uint64_t ticks = static_cast<uint64_t>(*epochSeconds + offset) * 10000000ULL;
    FILETIME fileTime{ static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32) };
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&fileTime, &utc) ||
        !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return L"时间无效";
    wchar_t text[64]{};
    swprintf_s(text, L"%04u-%02u-%02u %02u:%02u:%02u", local.wYear,
        local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond);
    return text;
}

std::wstring FormatRemaining(std::optional<double> remaining) {
    if (!remaining || !std::isfinite(*remaining)) return kMissing;
    wchar_t text[48]{};
    swprintf_s(text, L"%.1f%%", std::clamp(*remaining, 0.0, 100.0));
    return text;
}

} // namespace usage
