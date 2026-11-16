#include "UsageWorker.h"
#include "../third_party/json.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace usage {
namespace {
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;
int64_t Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
struct Failure : std::runtime_error {
    std::wstring text;
    explicit Failure(std::wstring message) : std::runtime_error("transport failure"), text(std::move(message)) {}
};
void Close(HANDLE& h) { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); h = nullptr; }

// The sole owner of the child tree. Reader threads block in ReadFile; killing
// the job closes the child ends before they are joined.
class Transport {
    HANDLE job_ = nullptr, process_ = nullptr, input_ = nullptr, output_ = nullptr, error_ = nullptr;
    std::thread stdoutThread_, stderrThread_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::string> lines_;
    bool broken_ = false;
    std::atomic<bool>& stop_;
    uint64_t nextId_ = 0;
    std::atomic<bool> accessDenied_{false};
    void ReadOutput() {
        std::string partial;
        char buffer[8192]; DWORD n;
        while (ReadFile(output_, buffer, sizeof buffer, &n, nullptr) && n) {
            partial.append(buffer, n);
            if (partial.size() > 8 * 1024 * 1024) break;
            size_t pos = 0, end;
            while ((end = partial.find('\n', pos)) != std::string::npos) {
                std::string line = partial.substr(pos, end - pos);
                pos = end + 1;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (lines_.size() >= 256) { broken_ = true; ready_.notify_all(); return; }
                    lines_.push_back(std::move(line));
                    ready_.notify_one();
                }
            }
            partial.erase(0, pos);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        broken_ = true; ready_.notify_all();
    }
    void Send(const json& message) {
        if (stop_) throw Failure(L"正在关闭");
        std::string data = message.dump() + "\n";
        DWORD written = 0;
        if (!WriteFile(input_, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) || written != data.size())
            throw Failure(L"连接管道写入失败，将重新连接");
    }
public:
    explicit Transport(std::atomic<bool>& stop) : stop_(stop) {}
    ~Transport() { Disconnect(); }
    void Wake() { std::lock_guard<std::mutex> lock(mutex_); ready_.notify_all(); }
    void Disconnect() {
        if (job_) TerminateJobObject(job_, 0);
        if (process_) WaitForSingleObject(process_, 5000);
        Close(input_);
        if (stdoutThread_.joinable()) stdoutThread_.join();
        if (stderrThread_.joinable()) stderrThread_.join();
        Close(output_); Close(error_); Close(process_); Close(job_);
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.clear(); broken_ = false;
        accessDenied_ = false;
    }
    bool Connected() const { return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT; }
    void Connect() {
        Disconnect();
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE childIn = nullptr, childOut = nullptr, childErr = nullptr;
        auto fail = [&]() {
            Close(childIn); Close(childOut); Close(childErr); Disconnect();
            throw Failure(L"无法启动隐藏的 PowerShell / Codex，请确认已安装 Codex 并加入 PATH");
        };
        if (!CreatePipe(&childIn, &input_, &sa, 0) || !CreatePipe(&output_, &childOut, &sa, 0) ||
            !CreatePipe(&error_, &childErr, &sa, 0)) fail();
        SetHandleInformation(input_, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(output_, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(error_, HANDLE_FLAG_INHERIT, 0);
        job_ = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
        limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job_ || !SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limit, sizeof(limit))) fail();

        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<unsigned char> storage(bytes);
        auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) fail();
        HANDLE inherited[]{childIn, childOut, childErr};
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) {
            DeleteProcThreadAttributeList(attributes); fail();
        }
        STARTUPINFOEXW si{}; si.StartupInfo.cb = sizeof(si);
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.StartupInfo.wShowWindow = SW_HIDE;
        si.StartupInfo.hStdInput = childIn; si.StartupInfo.hStdOutput = childOut; si.StartupInfo.hStdError = childErr;
        si.lpAttributeList = attributes;
        wchar_t systemDir[MAX_PATH]{}; GetSystemDirectoryW(systemDir, MAX_PATH);
        std::wstring shell = std::wstring(systemDir) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
        std::wstring command = L"\"" + shell + L"\" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \""
            L"[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false); "
            L"[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false); "
            L"$OutputEncoding = [Console]::OutputEncoding; $ErrorActionPreference = 'Stop'; "
            L"& codex app-server --stdio; exit $LASTEXITCODE\"";
        PROCESS_INFORMATION pi{};
        BOOL created = CreateProcessW(shell.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi);
        DeleteProcThreadAttributeList(attributes);
        Close(childIn); Close(childOut); Close(childErr);
        if (!created) fail();
        process_ = pi.hProcess;
        if (!AssignProcessToJobObject(job_, process_)) {
            TerminateProcess(process_, 1); Close(pi.hThread); fail();
        }
        ResumeThread(pi.hThread); Close(pi.hThread);
        stdoutThread_ = std::thread([this] { ReadOutput(); });
        stderrThread_ = std::thread([this] {
            char buffer[4096]; DWORD n;
            std::string tail;
            while (ReadFile(error_, buffer, sizeof buffer, &n, nullptr) && n) {
                tail.append(buffer, n);
                if (tail.find("os error 5") != std::string::npos || tail.find("Access is denied") != std::string::npos)
                    accessDenied_ = true;
                // Keep only a short overlap for matching a split diagnostic;
                // never persist stderr, which can contain account information.
                if (tail.size() > 128) tail.erase(0, tail.size() - 128);
            }
        });
        Request("initialize", {{"clientInfo", {{"name", "codex_usage_viewer"}, {"title", "Codex Usage Viewer"}, {"version", "1.0.0"}}}});
        Send({{"method", "initialized"}});
    }
    json Request(const char* method, const json& params) {
        const auto id = ++nextId_;
        Send({{"id", id}, {"method", method}, {"params", params}});
        auto deadline = Clock::now() + std::chrono::seconds(15);
        for (;;) {
            if (Clock::now() >= deadline) throw Failure(L"请求超过 15 秒未响应，将重新连接");
            std::string line;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!ready_.wait_until(lock, deadline, [&] { return stop_ || broken_ || !lines_.empty(); }))
                    throw Failure(L"请求超过 15 秒未响应，将重新连接");
                if (stop_) throw Failure(L"正在关闭");
                if (lines_.empty()) {
                    if (accessDenied_) throw Failure(L"Codex 启动被拒绝访问，请检查本机 Codex 目录权限");
                    throw Failure(L"Codex 进程退出或连接管道断开，将重新连接");
                }
                line = std::move(lines_.front()); lines_.pop_front();
            }
            auto message = json::parse(line, nullptr, false);
            if (message.is_discarded() || !message.is_object())
                throw Failure(L"接口不兼容：收到无效 JSON 消息");
            if (!message.contains("id") || message["id"] != id) continue;
            if (message.contains("error")) {
                int code = 0;
                const auto& error = message["error"];
                if (error.is_object() && error.contains("code") && error["code"].is_number_integer()) code = error["code"].get<int>();
                if (code == -32601 || code == -32602) throw Failure(L"接口不兼容：请使用支持重置次数查询的 Codex CLI 0.169.0 或兼容版本");
                throw Failure(L"查询失败：请检查网络及 Codex 登录状态（错误码 " + std::to_wstring(code) + L"）");
            }
            if (!message.contains("result") || !message["result"].is_object())
                throw Failure(L"接口不兼容：响应缺少有效 result");
            return message["result"];
        }
    }
};
}

struct UsageWorker::Impl {
    HWND window;
    std::atomic<bool> stop{false};
    Transport transport{stop};
    std::thread thread;
    std::mutex mutex;
    std::condition_variable wake;
    int interval = 3;
    bool pending = false, intervalChanged = false;
    WorkerUpdate update;
    explicit Impl(HWND target) : window(target) {}
    void Publish(const std::function<void(WorkerUpdate&)>& change) {
        { std::lock_guard<std::mutex> lock(mutex); change(update); }
        PostMessageW(window, WM_USAGE_UPDATE, 0, 0);
    }
    void Run() {
        int failures = 0;
        auto lastDetails = Clock::time_point::min();
        bool forceFull = true;
        while (!stop) {
            try {
                if (!transport.Connected()) {
                    Publish([](auto& u) { u.status = u.hasData ? L"正在连接 Codex…；数据未更新" : L"正在连接 Codex…；暂无数据"; });
                    transport.Connect(); forceFull = true;
                }
                bool full = forceFull || lastDetails == Clock::time_point::min() || Clock::now() - lastDetails >= std::chrono::seconds(60);
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (pending) { full = true; pending = false; }
                }
                if (full) Publish([](auto& u) { u.detailsUpdating = true; u.status = L"已连接，正在查询…"; });
                auto data = ParseSnapshot(transport.Request("account/rateLimits/read", {{"excludeResetCreditDetails", !full}}), full);
                bool countChanged;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    countChanged = update.hasData && data.resetCount != update.snapshot.resetCount;
                }
                const auto now = Now();
                Publish([&](auto& u) {
                    if (!data.detailsIncluded) data.details = u.snapshot.details;
                    u.snapshot = std::move(data); u.hasData = true; u.lastSuccess = now;
                    u.status = L"已连接";
                    u.detailsUpdating = !u.snapshot.detailsIncluded && (full || countChanged);
                    if (full && u.snapshot.detailsIncluded) { u.detailsUpdated = now; u.detailsUpdating = false; }
                    else if (full) { u.status = L"已连接；服务端未提供重置明细"; u.detailsUpdating = false; }
                });
                if (full) lastDetails = Clock::now();
                failures = 0;
                forceFull = countChanged && !full;
                if (forceFull) continue;
            } catch (const Failure& error) {
                if (stop) break;
                transport.Disconnect();
                Publish([&](auto& u) { u.status = error.text + (u.hasData ? L"；数据未更新" : L"；暂无数据"); u.detailsUpdating = false; });
                ++failures; forceFull = true;
            } catch (const std::exception&) {
                if (stop) break;
                transport.Disconnect();
                Publish([](auto& u) { u.status = L"接口不兼容或数据解析失败" + std::wstring(u.hasData ? L"；数据未更新" : L"；暂无数据"); u.detailsUpdating = false; });
                ++failures; forceFull = true;
            }
            std::unique_lock<std::mutex> lock(mutex);
            const auto completed = Clock::now();
            constexpr int backoff[]{3, 6, 12, 24, 60};
            for (;;) {
                int seconds = failures ? backoff[(std::min)(failures - 1, 4)] : interval;
                auto due = completed + std::chrono::seconds(seconds);
                if (!failures && lastDetails != Clock::time_point::min())
                    due = (std::min)(due, lastDetails + std::chrono::seconds(60));
                if (stop || pending || Clock::now() >= due) break;
                intervalChanged = false;
                wake.wait_until(lock, due, [&] { return stop || pending || intervalChanged; });
            }
            if (pending) { forceFull = true; pending = false; }
        }
        transport.Disconnect();
    }
};

UsageWorker::UsageWorker(HWND window) : impl_(std::make_unique<Impl>(window)) {}
UsageWorker::~UsageWorker() { Stop(); }
void UsageWorker::Start(int interval) {
    if (impl_->thread.joinable()) return;
    impl_->interval = interval; impl_->stop = false;
    impl_->thread = std::thread([this] { impl_->Run(); });
}
void UsageWorker::SetInterval(int seconds) {
    { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->interval = seconds; impl_->intervalChanged = true; }
    impl_->wake.notify_one();
}
void UsageWorker::Refresh() {
    { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->pending = true; }
    impl_->wake.notify_one();
}
void UsageWorker::Stop() {
    { std::lock_guard<std::mutex> lock(impl_->mutex); impl_->stop = true; }
    impl_->wake.notify_all(); impl_->transport.Wake();
    if (impl_->thread.joinable()) impl_->thread.join();
}
WorkerUpdate UsageWorker::TakeUpdate() {
    std::lock_guard<std::mutex> lock(impl_->mutex); return impl_->update;
}
}
