#pragma once
#include <optional>
#include <string>

namespace usage {
std::optional<int> ParseRefreshInterval(const std::wstring& text);
std::wstring DefaultIniPath();
int LoadRefreshInterval(const std::wstring& iniPath);
bool SaveRefreshInterval(const std::wstring& iniPath, int seconds, std::wstring& error);
int LoadQuotaFontSize(const std::wstring& iniPath, const wchar_t* key);
bool SaveQuotaFontSize(const std::wstring& iniPath, const wchar_t* key, int size);
} // namespace usage
