#include "MainDialog.h"
#include "Settings.h"
#include <algorithm>
#include <array>
#include <cmath>

BEGIN_MESSAGE_MAP(MainDialog, CDialogEx)
    ON_BN_CLICKED(IDC_APPLY, &MainDialog::OnApply)
    ON_BN_CLICKED(IDC_REFRESH, &MainDialog::OnRefresh)
    ON_WM_CLOSE()
    ON_WM_SIZE()
    ON_WM_GETMINMAXINFO()
    ON_MESSAGE(usage::WM_USAGE_UPDATE, &MainDialog::OnUsageUpdate)
END_MESSAGE_MAP()

MainDialog::MainDialog() : CDialogEx(IDD_MAIN) {}
MainDialog::~MainDialog() = default;

BOOL MainDialog::OnInitDialog() {
    CDialogEx::OnInitDialog();
    // 分别加载系统需要的大小，避免标题栏图标直接缩小后变模糊。
    const auto loadIcon = [](int width, int height) {
        return static_cast<HICON>(::LoadImageW(AfxGetResourceHandle(),
            MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, width, height, LR_SHARED));
    };
    SetIcon(loadIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON)), TRUE);
    SetIcon(loadIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON)), FALSE);
    details_.SubclassDlgItem(IDC_DETAILS, this);
    details_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    const std::array<const wchar_t*, 6> headings = {
        L"类型", L"状态", L"发放时间", L"到期时间", L"名称", L"说明"
    };
    const std::array<int, 6> widths = {120, 100, 165, 165, 190, 340};
    for (int column = 0; column < static_cast<int>(headings.size()); ++column)
        details_.InsertColumn(column, headings[column], LVCFMT_LEFT, widths[column]);
    fiveProgress_.SubclassDlgItem(IDC_FIVE_PROGRESS, this);
    weekProgress_.SubclassDlgItem(IDC_WEEK_PROGRESS, this);
    fiveProgress_.SetRange32(0, 1000);
    weekProgress_.SetRange32(0, 1000);
    static_cast<CEdit*>(GetDlgItem(IDC_INTERVAL))->SetLimitText(12);
    iniPath_ = usage::DefaultIniPath();
    const int interval = usage::LoadRefreshInterval(iniPath_);
    SetDlgItemInt(IDC_INTERVAL, static_cast<UINT>(interval), FALSE);
    RememberLayout();
    worker_ = std::make_unique<usage::UsageWorker>(GetSafeHwnd());
    worker_->Start(interval);
    return TRUE;
}

void MainDialog::RememberLayout() {
    CRect rect;
    GetClientRect(rect);
    originalClient_ = rect.Size();
    GetWindowRect(rect);
    minimumWindow_ = rect.Size();
    const std::array<int, 18> ids = {
        IDC_FIVE_GROUP, IDC_FIVE_REMAINING, IDC_FIVE_PROGRESS, IDC_FIVE_RESET,
        IDC_WEEK_GROUP, IDC_WEEK_REMAINING, IDC_WEEK_PROGRESS, IDC_WEEK_RESET,
        IDC_RESET_COUNT, IDC_DETAILS_TIME, IDC_DETAILS, IDC_INTERVAL_LABEL,
        IDC_INTERVAL, IDC_APPLY, IDC_REFRESH, IDC_READONLY, IDC_STATUS, IDC_LAST_SUCCESS
    };
    for (const int id : ids) {
        GetDlgItem(id)->GetWindowRect(rect);
        ScreenToClient(rect);
        placements_.push_back({id, rect});
    }
}

void MainDialog::OnSize(UINT type, int width, int height) {
    CDialogEx::OnSize(type, width, height);
    if (placements_.empty() || type == SIZE_MINIMIZED) return;
    const int dx = width - originalClient_.cx;
    const int dy = height - originalClient_.cy;
    const int half = dx / 2;
    for (const auto& placement : placements_) {
        CRect rect = placement.rect;
        switch (placement.id) {
        case IDC_FIVE_GROUP: case IDC_FIVE_REMAINING:
        case IDC_FIVE_PROGRESS: case IDC_FIVE_RESET:
            rect.right += half;
            break;
        case IDC_WEEK_GROUP: case IDC_WEEK_REMAINING:
        case IDC_WEEK_PROGRESS: case IDC_WEEK_RESET:
            rect.left += half;
            rect.right += dx;
            break;
        case IDC_DETAILS:
            rect.right += dx;
            rect.bottom += dy;
            break;
        case IDC_RESET_COUNT: case IDC_DETAILS_TIME:
            rect.right += dx;
            break;
        case IDC_STATUS: case IDC_LAST_SUCCESS: case IDC_READONLY:
            rect.right += dx;
            rect.OffsetRect(0, dy);
            break;
        default:
            rect.OffsetRect(0, dy);
            break;
        }
        GetDlgItem(placement.id)->MoveWindow(rect, TRUE);
    }
    Invalidate(FALSE);
}

void MainDialog::OnGetMinMaxInfo(MINMAXINFO* info) {
    CDialogEx::OnGetMinMaxInfo(info);
    if (minimumWindow_.cx > 0) {
        info->ptMinTrackSize.x = minimumWindow_.cx;
        info->ptMinTrackSize.y = minimumWindow_.cy;
    }
}

void MainDialog::SetTextIfChanged(int id, const std::wstring& value) {
    CString old;
    GetDlgItemText(id, old);
    if (old != value.c_str()) SetDlgItemText(id, value.c_str());
}

void MainDialog::RenderDetails(const std::vector<usage::Detail>& details) {
    const int desired = static_cast<int>(details.size());
    bool drawingPaused = false;
    auto pauseDrawing = [&]() {
        if (!drawingPaused) { details_.SetRedraw(FALSE); drawingPaused = true; }
    };
    while (details_.GetItemCount() > desired) {
        pauseDrawing();
        details_.DeleteItem(details_.GetItemCount() - 1);
    }
    for (int row = 0; row < desired; ++row) {
        const auto& detail = details[static_cast<size_t>(row)];
        const std::array<const std::wstring*, 6> cells = {
            &detail.type, &detail.status, &detail.issued,
            &detail.expires, &detail.name, &detail.description
        };
        if (row >= details_.GetItemCount()) {
            pauseDrawing();
            details_.InsertItem(row, L"");
        }
        for (int column = 0; column < static_cast<int>(cells.size()); ++column) {
            const auto& cell = *cells[column];
            if (details_.GetItemText(row, column) != cell.c_str()) {
                pauseDrawing();
                details_.SetItemText(row, column, cell.c_str());
            }
        }
    }
    if (drawingPaused) {
        details_.SetRedraw(TRUE);
        details_.Invalidate(FALSE);
    }
}

void MainDialog::Render(const usage::WorkerUpdate& update) {
    SetTextIfChanged(IDC_STATUS, update.status);
    SetTextIfChanged(IDC_LAST_SUCCESS,
        L"最后成功更新：" + (update.lastSuccess ? usage::FormatTime(update.lastSuccess) : L"暂无数据"));
    std::wstring detailsText = L"重置明细：";
    detailsText += update.detailsUpdated ? L"更新于 " + usage::FormatTime(update.detailsUpdated) : L"暂无数据";
    if (update.detailsUpdating) detailsText += L" · 正在更新";
    else if (update.detailsUpdated && update.snapshot.details.empty()) detailsText += L" · 服务端未返回明细";
    SetTextIfChanged(IDC_DETAILS_TIME, detailsText);
    if (!update.hasData) return;
    const auto& snapshot = update.snapshot;
    SetTextIfChanged(IDC_FIVE_REMAINING, L"剩余：" + usage::FormatRemaining(snapshot.fiveHour.remaining));
    SetTextIfChanged(IDC_WEEK_REMAINING, L"剩余：" + usage::FormatRemaining(snapshot.weekly.remaining));
    SetTextIfChanged(IDC_FIVE_RESET, L"下次恢复：" + usage::FormatTime(snapshot.fiveHour.resetsAt));
    SetTextIfChanged(IDC_WEEK_RESET, L"下次恢复：" + usage::FormatTime(snapshot.weekly.resetsAt));
    SetTextIfChanged(IDC_RESET_COUNT, L"可用重置次数：" +
        (snapshot.resetCount ? std::to_wstring(*snapshot.resetCount) : L"未提供"));
    const auto renderProgress = [](CProgressCtrl& control, std::optional<double> remaining) {
        if (static_cast<bool>(control.IsWindowEnabled()) != remaining.has_value())
            control.EnableWindow(remaining.has_value());
        const int position = remaining ? static_cast<int>(std::lround(std::clamp(*remaining, 0.0, 100.0) * 10.0)) : 0;
        if (control.GetPos() != position) control.SetPos(position);
    };
    renderProgress(fiveProgress_, snapshot.fiveHour.remaining);
    renderProgress(weekProgress_, snapshot.weekly.remaining);
    RenderDetails(snapshot.details);
}

LRESULT MainDialog::OnUsageUpdate(WPARAM, LPARAM) {
    if (!closing_ && worker_) Render(worker_->TakeUpdate());
    return 0;
}

void MainDialog::OnApply() {
    CString text;
    GetDlgItemText(IDC_INTERVAL, text);
    const auto interval = usage::ParseRefreshInterval(std::wstring(text.GetString()));
    if (!interval) {
        MessageBox(L"请输入 1～3600 之间的整数秒数。", L"刷新间隔无效", MB_OK | MB_ICONINFORMATION);
        GetDlgItem(IDC_INTERVAL)->SetFocus();
        static_cast<CEdit*>(GetDlgItem(IDC_INTERVAL))->SetSel(0, -1);
        return;
    }
    if (worker_) worker_->SetInterval(*interval);
    SetDlgItemInt(IDC_INTERVAL, static_cast<UINT>(*interval), FALSE);
    std::wstring error;
    if (!usage::SaveRefreshInterval(iniPath_, *interval, error)) {
        const std::wstring message = L"刷新间隔已在本次运行中生效，但配置保存失败。\n\n" +
            error + L"\n\n配置位置：" + iniPath_;
        MessageBox(message.c_str(), L"无法保存设置", MB_OK | MB_ICONWARNING);
    }
}

void MainDialog::OnRefresh() { if (worker_) worker_->Refresh(); }
void MainDialog::OnOK() { OnApply(); }
void MainDialog::OnCancel() { CloseDialog(); }
void MainDialog::OnClose() { CloseDialog(); }

void MainDialog::CloseDialog() {
    if (closing_) return;
    closing_ = true;
    if (worker_) worker_->Stop();
    EndDialog(IDOK);
}
