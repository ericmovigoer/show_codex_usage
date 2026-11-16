#include "Settings.h"
#include <Windows.h>
#include <cwctype>
#include <vector>

namespace usage {
std::optional<int> ParseRefreshInterval(const std::wstring& text) {
    const auto begin = text.find_first_not_of(L" \t\r\n");
    if (begin == std::wstring::npos) return {};
    const auto end = text.find_last_not_of(L" \t\r\n");
    int value = 0;
    for (size_t i = begin; i <= end; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return {};
        value = value * 10 + (text[i] - L'0');
        if (value > 3600) return {};
    }
    if (value < 1) return {};
    return value;
}

std::wstring DefaultIniPath() {
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length) return {};
        if (length < buffer.size()) {
            std::wstring path(buffer.data(), length);
            const auto slash = path.find_last_of(L"\\/");
            return path.substr(0, slash + 1) + L"CodexUsage.ini";
        }
        buffer.resize(buffer.size() * 2);
        if (buffer.size() > 32768) return {};
    }
}

int LoadRefreshInterval(const std::wstring& iniPath) {
    if (iniPath.empty()) return 3;
    wchar_t text[128]{};
    const DWORD length = GetPrivateProfileStringW(L"Settings", L"RefreshIntervalSeconds",
        L"", text, static_cast<DWORD>(std::size(text)), iniPath.c_str());
    if (length == std::size(text) - 1) return 3;
    return ParseRefreshInterval(text).value_or(3);
}

bool SaveRefreshInterval(const std::wstring& iniPath, int seconds, std::wstring& error) {
    error.clear();
    if (iniPath.empty()) {
        error = L"无法确定程序目录。";
        return false;
    }
    if (seconds < 1 || seconds > 3600) {
        error = L"刷新间隔必须为 1～3600 秒的整数。";
        return false;
    }
    const auto text = std::to_wstring(seconds);
    if (WritePrivateProfileStringW(L"Settings", L"RefreshIntervalSeconds", text.c_str(), iniPath.c_str()))
        return true;
    const DWORD code = GetLastError();
    wchar_t message[512]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        code, 0, message, static_cast<DWORD>(std::size(message)), nullptr);
    error = L"无法保存 " + iniPath + L"（错误 " + std::to_wstring(code) + L"）：" + message;
    return false;
}
} // namespace usage
