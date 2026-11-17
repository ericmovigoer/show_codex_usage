#pragma once

#include <afxwin.h>
#include <afxcmn.h>
#include <afxdialogex.h>
#include <memory>
#include <vector>
#include "resource.h"
#include "UsageWorker.h"

class MainDialog final : public CDialogEx {
public:
    MainDialog();
    ~MainDialog() override;

protected:
    BOOL OnInitDialog() override;
    void OnOK() override;
    void OnCancel() override;
    afx_msg void OnApply();
    afx_msg void OnRefresh();
    afx_msg void OnSettings();
    afx_msg void OnClose();
    afx_msg void OnSize(UINT type, int width, int height);
    afx_msg void OnGetMinMaxInfo(MINMAXINFO* info);
    afx_msg LRESULT OnUsageUpdate(WPARAM, LPARAM);
    DECLARE_MESSAGE_MAP()

private:
    void CloseDialog();
    void Render(const usage::WorkerUpdate& update);
    void SetTextIfChanged(int id, const std::wstring& value);
    void RenderDetails(const std::vector<usage::Detail>& details);
    void RememberLayout();
    void ApplyQuotaFonts();

    struct ControlPlacement { int id; CRect rect; };
    std::vector<ControlPlacement> placements_;
    CSize originalClient_{0, 0};
    CSize minimumWindow_{0, 0};
    std::unique_ptr<usage::UsageWorker> worker_;
    CListCtrl details_;
    CProgressCtrl fiveProgress_;
    CProgressCtrl weekProgress_;
    std::wstring iniPath_;
    CFont fiveFont_;
    CFont weekFont_;
    int fiveFontSize_ = 9;
    int weekFontSize_ = 9;
    bool closing_ = false;
};
