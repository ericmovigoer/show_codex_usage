#include <afxwin.h>
#include <afxcmn.h>
#include "MainDialog.h"

#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")

class CodexUsageApp final : public CWinApp {
public:
    BOOL InitInstance() override {
        // 系统 DPI 感知可保证常见缩放比例下的文字清晰，MFC 对话框按字体换算尺寸。
        SetProcessDPIAware();
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&controls);
        CWinApp::InitInstance();
        MainDialog dialog;
        m_pMainWnd = &dialog;
        dialog.DoModal();
        m_pMainWnd = nullptr;
        return FALSE;
    }
};

CodexUsageApp theApp;
