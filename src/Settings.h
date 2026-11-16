#pragma once
#include <optional>
#include <string>

namespace usage {
std::optional<int> ParseRefreshInterval(const std::wstring& text);
std::wstring DefaultIniPath();
int LoadRefreshInterval(const std::wstring& iniPath);
bool SaveRefreshInterval(const std::wstring& iniPath, int seconds, std::wstring& error);
} // namespace usage
