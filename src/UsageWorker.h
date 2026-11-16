#pragma once
#include <Windows.h>
#include <memory>
#include "UsageModel.h"

namespace usage {
constexpr UINT WM_USAGE_UPDATE = WM_APP + 41;
struct WorkerUpdate {
    Snapshot snapshot;
    bool hasData = false;
    std::wstring status;
    std::optional<int64_t> lastSuccess, detailsUpdated;
    bool detailsUpdating = false;
};
class UsageWorker {
public:
    explicit UsageWorker(HWND window);
    ~UsageWorker();
    void Start(int interval);
    void SetInterval(int seconds);
    void Refresh();
    void Stop();
    WorkerUpdate TakeUpdate();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
