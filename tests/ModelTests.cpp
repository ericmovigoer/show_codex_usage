#include "../src/UsageModel.h"
#include "../src/Settings.h"
#include <Windows.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void Require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template <typename F> void Reject(F test, const char* message) {
    bool rejected = false;
    try { test(); } catch (const std::exception&) { rejected = true; }
    Require(rejected, message);
}

void TestModels() {
    using nlohmann::json;
    using namespace usage;
    const auto response = json::parse(R"({
      "rateLimits":{"primary":{"usedPercent":99,"windowDurationMins":300,"resetsAt":1}},
      "rateLimitsByLimitId":{"codex":{
        "primary":{"usedPercent":12.5,"windowDurationMins":10080,"resetsAt":1700000000},
        "secondary":{"usedPercent":30,"windowDurationMins":300,"resetsAt":1700003600}}},
      "rateLimitResetCredits":{"availableCount":9,"credits":[{
        "id":"test", "resetType":"codexRateLimits", "status":"available",
        "grantedAt":1700000000,"expiresAt":null,"title":"Codex rate limit reset",
        "description":"来自服务端的描述"}]}
    })");
    const auto data = ParseSnapshot(response, true);
    Require(data.fiveHour.remaining == 70.0, "codex bucket takes priority over legacy");
    Require(data.weekly.remaining == 87.5, "identify weekly by duration rather than primary order");
    Require(data.fiveHour.resetsAt == 1700003600, "preserve reset epoch");
    Require(data.resetCount == 9, "count must not be derived from the one returned row");
    Require(data.detailsIncluded && data.details.size() == 1, "full response includes detail snapshot");
    const auto& detail = data.details.front();
    Require(detail.type == L"Codex 额度重置", "translate known type");
    Require(detail.status == L"可用", "translate status");
    Require(detail.expires == L"长期有效", "explicit null expiry means no expiration");
    Require(detail.name == L"Codex 额度重置", "translate known title");
    Require(detail.description == L"来自服务端的描述（服务端原文）", "preserve Unicode free text");
    Require(detail.issued == FormatTime(1700000000), "format granted time");
    Require(FormatRemaining(data.weekly.remaining) == L"87.5%", "percentage formatting");

    auto fallback = ParseSnapshot(json::parse(R"({"rateLimits":{"primary":{
      "usedPercent":25,"windowDurationMins":300,"resetsAt":0}}})"), true);
    Require(fallback.fiveHour.remaining == 75, "legacy fallback");
    Require(!fallback.weekly.remaining, "missing weekly is unknown");
    Require(fallback.fiveHour.resetsAt == 0, "epoch zero is valid rather than missing");
    Require(!fallback.resetCount, "missing count is unknown rather than zero");
    Require(!fallback.detailsIncluded, "missing summary must not erase cached details");
    Require(FormatRemaining({}) == L"未提供" && FormatTime({}) == L"未提供", "missing display");

    auto sparse = ParseSnapshot(json::parse(R"({"rateLimits":null,
      "rateLimitResetCredits":{"availableCount":null,"credits":null}})"), true);
    Require(!sparse.fiveHour.remaining && !sparse.resetCount, "null values are unknown");
    Require(!sparse.detailsIncluded, "null details must not erase cache");
    auto nullSummary = ParseSnapshot(json::parse(R"({"rateLimits":null,"rateLimitResetCredits":null})"), true);
    Require(!nullSummary.resetCount && !nullSummary.detailsIncluded, "null summary keeps count unknown and details unavailable");
    auto missingCount = ParseSnapshot(json::parse(R"({"rateLimitResetCredits":{"credits":[]}})"), true);
    Require(!missingCount.resetCount && missingCount.detailsIncluded, "missing count remains unknown even when detail snapshot is empty");
    auto nullCodex = ParseSnapshot(json::parse(R"({"rateLimitsByLimitId":{"codex":null},
      "rateLimits":{"primary":{"usedPercent":12,"windowDurationMins":300}}})"), true);
    Require(nullCodex.fiveHour.remaining == 88, "null codex bucket uses compatible legacy fallback");
    auto empty = ParseSnapshot(json::parse(R"({"rateLimitResetCredits":{"availableCount":0,"credits":[]}})"), true);
    Require(empty.resetCount == 0 && empty.detailsIncluded && empty.details.empty(), "explicit empty list replaces cache");
    auto poll = ParseSnapshot(response, false);
    Require(!poll.detailsIncluded && poll.details.empty(), "ordinary poll does not replace detail snapshot");

    auto unknownWindow = ParseSnapshot(json::parse(R"({"rateLimits":{"primary":{
      "usedPercent":1,"windowDurationMins":60,"resetsAt":1},"secondary":{
      "usedPercent":null,"windowDurationMins":300,"resetsAt":null}}})"), true);
    Require(!unknownWindow.weekly.remaining && !unknownWindow.fiveHour.remaining, "unsupported duration must not be mislabeled");
    auto clamp = ParseSnapshot(json::parse(R"({"rateLimits":{"primary":{
      "usedPercent":150,"windowDurationMins":300},"secondary":{
      "usedPercent":-3,"windowDurationMins":10080}}})"), true);
    Require(clamp.fiveHour.remaining == 0 && clamp.weekly.remaining == 100, "clamp numeric percentages");
    auto absentExpiry = response;
    absentExpiry["rateLimitResetCredits"]["credits"][0].erase("expiresAt");
    absentExpiry["rateLimitResetCredits"]["credits"][0]["resetType"] = "futureType";
    absentExpiry["rateLimitResetCredits"]["credits"][0]["status"] = "futureState";
    auto future = ParseSnapshot(absentExpiry, true);
    Require(future.details[0].expires == L"未提供", "missing expiry differs from null");
    Require(future.details[0].type == L"futureType（服务端原文）", "preserve future enum");
    Require(future.details[0].status == L"futureState（服务端原文）", "preserve future status");
    Require(FormatTime(253402300800LL) == L"时间无效", "reject timestamp overflow");

    // Derive the local calendar result independently from a fixed UTC calendar value.
    SYSTEMTIME utc{1970, 1, 4, 1, 0, 0, 0, 0}, local{};
    Require(SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local) != FALSE, "Windows timezone conversion available");
    wchar_t expected[64]{};
    swprintf_s(expected, L"%04u-%02u-%02u %02u:%02u:%02u", local.wYear,
        local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond);
    Require(FormatTime(0) == expected, "convert Unix epoch using current Windows timezone");

    Reject([] { ParseSnapshot(json::array(), true); }, "reject nonobject protocol response");
    Reject([] { ParseSnapshot(json{{"unknownProtocol", true}}, true); }, "reject incompatible protocol fields");
    Reject([] { ParseSnapshot(json::parse(R"({"rateLimits":{"primary":{"usedPercent":"50","windowDurationMins":300}}})"), true); }, "reject mistyped percentage");
    Reject([] { ParseSnapshot(json::parse(R"({"rateLimitResetCredits":{"availableCount":-1}})"), true); }, "reject negative count");
    Reject([] { ParseSnapshot(json::parse(R"({"rateLimitResetCredits":{"availableCount":1.5}})"), true); }, "reject fractional count");
    Reject([] { ParseSnapshot(json::parse(R"({"rateLimitResetCredits":{"availableCount":"9"}})"), true); }, "reject string count rather than treating it as unknown");
    Reject([] { ParseSnapshot(json::parse(R"({"rateLimitResetCredits":{"availableCount":18446744073709551615}})"), true); }, "reject overflowing count");
    Reject([] { ParseSnapshot(json::parse(R"({"rateLimitResetCredits":{"credits":{}}})"), true); }, "reject incompatible details shape");
}

void TestSettings() {
    using namespace usage;
    Require(ParseRefreshInterval(L"3") == 3, "default value parser");
    Require(ParseRefreshInterval(L"1") == 1 && ParseRefreshInterval(L"3600") == 3600, "interval boundaries");
    Require(ParseRefreshInterval(L"  30 \t") == 30, "surrounding whitespace");
    for (const auto* invalid : {L"", L"0", L"-1", L"3601", L"1.5", L"3seconds", L"+1", L"1e2", L"999999999999999999999", L"１"})
        Require(!ParseRefreshInterval(invalid), "invalid refresh interval");
    Require(DefaultIniPath().find(L"CodexUsage.ini") != std::wstring::npos, "exe-side ini filename");

    wchar_t tempDirectory[MAX_PATH]{}, temporary[MAX_PATH]{};
    Require(GetTempPathW(MAX_PATH, tempDirectory) > 0, "temporary directory available");
    Require(GetTempFileNameW(tempDirectory, L"cux", 0, temporary) != 0, "temporary name available");
    DeleteFileW(temporary);
    const std::wstring ini = temporary;
    Require(LoadRefreshInterval(ini) == 3, "missing INI defaults to three seconds");
    std::wstring error;
    Require(SaveRefreshInterval(ini, 17, error), "save interval");
    Require(error.empty() && LoadRefreshInterval(ini) == 17, "reload saved interval");
    Require(SaveRefreshInterval(ini, 1, error) && LoadRefreshInterval(ini) == 1, "apply new interval persists immediately");
    WritePrivateProfileStringW(L"Settings", L"RefreshIntervalSeconds", L"invalid", ini.c_str());
    Require(LoadRefreshInterval(ini) == 3, "invalid INI defaults to three seconds");
    DeleteFileW(ini.c_str());
    Require(CreateDirectoryW(ini.c_str(), nullptr) != FALSE, "create unwritable target fixture");
    Require(!SaveRefreshInterval(ini, 5, error) && !error.empty(), "surface INI write failure when path is a directory");
    RemoveDirectoryW(ini.c_str());
    Require(!SaveRefreshInterval(L"", 3, error), "reject absent executable path");
}
} // namespace

int main() {
    try {
        TestModels();
        TestSettings();
        std::cout << "PASS: " << checks << " model/settings checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
