/**
 *  flamingoAdmin —— 调试控制台窗口实现。
 *
 *  本文件实现 DebugConsoleWindow.h 声明的独立 Win32/ATL 顶层窗口：
 *  子控件创建与布局、虚拟列表（LVS_OWNERDATA）取文本、状态圆点 ImageList
 *  绘制、选中行 → 详情面板回填、清空 / 暂停 / 导出 工具条命令，以及对
 *  DebugConsole sink 通知（WM_DC_REFRESH）的处理。
 *
 *  全部代码均在 UI 线程执行，故可安全调用非线程安全的 Logger（FA_LOGI）。
 *
 *  典型用法见 DebugConsoleWindow.h 头部注释。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#include "stdafx.h"
#include "Util/DebugConsoleWindow.h"
#include "Util/DebugConsole.h"

#include "Util/Logger.h"
#include "Widgets/IconRenderer.h"   // ui::DrawIcon / ui::Icon —— 渲染窗口图标
#include "Theme/AdminTheme.h"       // theme::kInk500 —— 窗口图标描边色

#include <commctrl.h>
#include <commdlg.h>
#include <string>

namespace flamingoadmin {

// ======================== 命名常量（杜绝魔数） ========================

// ---- 窗口几何 --------------------------------------------------------
const int kWindowDefaultWidth  = 960;   // 窗口默认宽度（像素）
const int kWindowDefaultHeight = 700;   // 窗口默认高度（像素）
const int kWindowOffsetFromOwner = 48;  // 相对宿主窗口左上角的偏移（像素），错开避免完全遮挡

// ---- 窗口图标 --------------------------------------------------------
// 窗口图标（标题栏 / 任务栏 / Alt+Tab）的终端图标描边色。取主题中性灰
// theme::kInk500，与顶栏「调试控制台」按钮的图标同色，视觉统一。
const COLORREF kWindowIconColor = theme::kInk500;

// ---- 工具条布局 ------------------------------------------------------
const int kToolbarHeight   = 36;   // 顶部工具条整条高度（像素）
const int kToolbarMargin   = 6;    // 工具条内控件与边缘 / 控件间距（像素）
const int kToolbarBtnTop   = 6;    // 工具条内控件距条顶的间距（像素）
const int kToolbarCtrlH    = 24;   // 工具条内各控件高度（像素）
const int kBtnWidthClear   = 64;   // 「清空」按钮宽度（像素）
const int kBtnWidthPause   = 64;   // 「暂停 / 继续」按钮宽度（像素）
const int kBtnWidthExport  = 72;   // 「导出…」按钮宽度（像素）
const int kChkWidthAuto    = 96;   // 「自动滚动」复选框宽度（像素）
const int kChkWidthError   = 96;   // 「仅看错误」复选框宽度（像素）
const int kCountStaticW    = 140;  // 「共 N 条」计数静态文本宽度（像素）

// ---- 主体上下分割 ----------------------------------------------------
// 中部 ListView 占客户区（去掉工具条后）高度的 55%，底部详情区占其余 45%。
const int kListAreaPercent   = 55;  // ListView 区域占主体高度的百分比
const int kDetailAreaPercent = 45;  // 底部详情区占主体高度的百分比
const int kPercentBase       = 100; // 百分比换算基数
const int kDetailCaptionH    = 18;  // 详情区「请求」/「响应」标题文字高度（像素）
const int kPaneGap           = 6;   // 左右两栏 EDIT 之间的水平间隙（像素）

// ---- ListView 列宽 --------------------------------------------------
const int kColWidthSeq     = 56;   // 「#」列宽（像素）
const int kColWidthTime    = 110;  // 「时间」列宽（像素）
const int kColWidthApi     = 320;  // 「接口」列宽（像素）
const int kColWidthStatus  = 220;  // 「状态」列宽（像素）
const int kColWidthElapsed = 80;   // 「耗时」列宽（像素）
const int kColWidthBizCode = 90;   // 「业务码」列宽（像素）

// ---- 状态圆点 / ImageList -------------------------------------------
const int kIconSize       = 16;   // 状态圆点图标边长（像素，16x16）
const int kIconDotInset   = 3;    // 圆点距 16x16 图标边缘的内缩（像素），留白更美观
const COLORREF kIconBkColor = RGB(255, 0, 255); // ImageList 透明色键（洋红，不会与圆点色撞）

// 四种状态圆点颜色，与 DebugEntryKind 顺序一一对应。
const COLORREF kDotColorGrey   = RGB(160, 160, 160); // 灰：进行中
const COLORREF kDotColorGreen  = RGB( 40, 167,  69); // 绿：业务成功
const COLORREF kDotColorOrange = RGB(255, 153,   0); // 橙：业务失败
const COLORREF kDotColorRed    = RGB(220,  53,  69); // 红：传输失败

// ---- 字体 ------------------------------------------------------------
const int kMonoFontHeight = -13;  // 等宽字体字号（负值表字符高度，约 10pt @96DPI）

// ---- 导出文件 --------------------------------------------------------
// UTF-8 BOM 三字节，写文件时置于正文之前，便于文本编辑器识别为 UTF-8。
const BYTE kUtf8Bom[3] = { 0xEF, 0xBB, 0xBF };
const wchar_t* const kExportFilter   = L"文本文件 (*.txt)\0*.txt\0全部文件 (*.*)\0*.*\0";
const wchar_t* const kExportDefExt   = L"txt";
const int kSaveFileNameBufLen = 260; // GetSaveFileName 文件名缓冲长度（MAX_PATH）

// ---- 控件文案 --------------------------------------------------------
const wchar_t* const kTextWindowTitle = L"调试控制台 — flamingoAdmin";
const wchar_t* const kTextClear       = L"清空";
const wchar_t* const kTextPause       = L"暂停";
const wchar_t* const kTextResume      = L"继续";
const wchar_t* const kTextExport      = L"导出…";
const wchar_t* const kTextAutoScroll  = L"自动滚动";
const wchar_t* const kTextErrorOnly   = L"仅看错误";
const wchar_t* const kTextReqCaption  = L"请求";
const wchar_t* const kTextRespCaption = L"响应";
const wchar_t* const kTextDash        = L"—"; // 「无值」占位符（破折号）
const wchar_t* const kTextPendingDots = L"…"; // 「进行中」状态占位符

// 列表列总数。DebugConsoleWindow::ColumnIndex 内的 kColumnCount 为类私有成员，
// 文件作用域的下列两个数组无法引用它，故在此另立一个同值常量（须与之保持一致）。
const int kColumnCount = 6;

// 列表 6 个列的表头文案，下标与 ColumnIndex 对应。
const wchar_t* const kColumnTitles[kColumnCount] =
{
    L"#", L"时间", L"接口", L"状态", L"耗时", L"业务码",
};
// 列表 6 个列的列宽，下标与 ColumnIndex 对应。
const int kColumnWidths[kColumnCount] =
{
    kColWidthSeq, kColWidthTime, kColWidthApi,
    kColWidthStatus, kColWidthElapsed, kColWidthBizCode,
};

// ============================ 构造 / 析构 ============================

DebugConsoleWindow::DebugConsoleWindow()
    : m_hwndOwner(nullptr)
    , m_hwndBtnClear(nullptr)
    , m_hwndBtnPause(nullptr)
    , m_hwndBtnExport(nullptr)
    , m_hwndChkAuto(nullptr)
    , m_hwndChkError(nullptr)
    , m_hwndCount(nullptr)
    , m_hwndList(nullptr)
    , m_hwndReqCap(nullptr)
    , m_hwndRespCap(nullptr)
    , m_hwndEditReq(nullptr)
    , m_hwndEditResp(nullptr)
    , m_hMonoFont(nullptr)
    , m_hImageList(nullptr)
    , m_hIconBig(nullptr)
    , m_hIconSmall(nullptr)
    , m_autoScroll(true)       // 默认开启自动滚动：调试时通常关注最新一条
    , m_errorOnly(false)       // 默认不过滤：先看全量
    , m_selectedIndex(-1)
{
}

DebugConsoleWindow::~DebugConsoleWindow()
{
    // 兜底：即便宿主漏调 Destroy()，单例析构时也确保 sink 与窗口已收回。
    Destroy();
}

DebugConsoleWindow& DebugConsoleWindow::Instance()
{
    static DebugConsoleWindow s_instance;
    return s_instance;
}

// ============================ 公有接口 ==============================

void DebugConsoleWindow::Toggle(HWND owner)
{
    // 窗口已存在：仅在显示 / 隐藏间切换，不重建。
    if (m_hWnd != nullptr && ::IsWindow(m_hWnd))
    {
        if (IsVisible())
        {
            ShowWindow(SW_HIDE);
        }
        else
        {
            ShowWindow(SW_SHOW);
            ::SetForegroundWindow(m_hWnd);
        }
        return;
    }

    // 首次调用：记下宿主窗口，计算初始位置（相对宿主左上角错开偏移）。
    m_hwndOwner = owner;

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (owner != nullptr && ::IsWindow(owner))
    {
        RECT rcOwner = { 0, 0, 0, 0 };
        ::GetWindowRect(owner, &rcOwner);
        x = rcOwner.left + kWindowOffsetFromOwner;
        y = rcOwner.top + kWindowOffsetFromOwner;
    }

    RECT rcWnd = { x, y, x + kWindowDefaultWidth, y + kWindowDefaultHeight };

    // 创建顶层重叠窗口，owner 设为宿主主窗口 —— 浮在其上、随其最小化。
    // CWindowImpl::Create 内部完成窗口类注册。
    Create(nullptr, rcWnd, kTextWindowTitle,
           WS_OVERLAPPEDWINDOW, 0, 0U, owner);
    if (m_hWnd == nullptr)
    {
        FA_LOGE(L"DebugConsoleWindow: 创建窗口失败");
        return;
    }

    FA_LOGI(L"DebugConsoleWindow: 调试控制台窗口已创建");

    // 窗口句柄已就绪，向数据层登记 sink —— 此后缓冲改动会回投 WM_DC_REFRESH。
    DebugConsole::Instance().SetSink(m_hWnd, WM_DC_REFRESH);

    // 首次刷新一遍当前缓冲内容，再显示。
    RefreshView();
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
}

bool DebugConsoleWindow::IsVisible() const
{
    if (m_hWnd == nullptr || !::IsWindow(m_hWnd))
        return false;

    return ::IsWindowVisible(m_hWnd) != FALSE;
}

void DebugConsoleWindow::Destroy()
{
    // 第一步：先摘除 sink —— 避免工作线程继续向即将失效的句柄投递消息。
    DebugConsole::Instance().SetSink(nullptr, 0);

    // 第二步：销毁窗口（WM_DESTROY 处理函数会释放 GDI / ImageList / 字体）。
    if (m_hWnd != nullptr && ::IsWindow(m_hWnd))
        DestroyWindow();

    // 第三步：复位状态，使本函数可被重复、安全调用。
    m_hwndOwner = nullptr;
    m_selectedIndex = -1;
    m_view.clear();
}

// ============================ 消息处理 ==============================

LRESULT DebugConsoleWindow::OnCreate(UINT /*msg*/, WPARAM /*wParam*/,
                                     LPARAM /*lParam*/, BOOL& /*handled*/)
{
    HINSTANCE hInst = _Module.GetModuleInstance();

    // ---- 创建等宽字体（Consolas），失败则后续退回控件默认字体 ----
    m_hMonoFont = ::CreateFont(kMonoFontHeight, 0, 0, 0, FW_NORMAL,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN,
                               L"Consolas");

    // ---- 工具条：清空 / 暂停 / 导出 三个普通按钮 ----
    m_hwndBtnClear = ::CreateWindowEx(
        0, L"BUTTON", kTextClear,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdBtnClear)),
        hInst, nullptr);

    m_hwndBtnPause = ::CreateWindowEx(
        0, L"BUTTON", kTextPause,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdBtnPause)),
        hInst, nullptr);

    m_hwndBtnExport = ::CreateWindowEx(
        0, L"BUTTON", kTextExport,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdBtnExport)),
        hInst, nullptr);

    // ---- 工具条：自动滚动 / 仅看错误 两个复选框 ----
    m_hwndChkAuto = ::CreateWindowEx(
        0, L"BUTTON", kTextAutoScroll,
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdChkAutoScroll)),
        hInst, nullptr);

    m_hwndChkError = ::CreateWindowEx(
        0, L"BUTTON", kTextErrorOnly,
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, 0, 0, 0, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdChkErrorOnly)),
        hInst, nullptr);

    // 复选框初值与成员状态对齐。
    ::SendMessage(m_hwndChkAuto, BM_SETCHECK,
                  m_autoScroll ? BST_CHECKED : BST_UNCHECKED, 0);
    ::SendMessage(m_hwndChkError, BM_SETCHECK,
                  m_errorOnly ? BST_CHECKED : BST_UNCHECKED, 0);

    // ---- 工具条：「共 N 条」计数静态文本 ----
    m_hwndCount = ::CreateWindowEx(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_CENTERIMAGE,
        0, 0, 0, 0, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStaticCount)),
        hInst, nullptr);

    // ---- 中部：报表式虚拟 ListView ----
    m_hwndList = ::CreateWindowEx(
        0, WC_LISTVIEW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER
            | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
        0, 0, 0, 0, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdListView)),
        hInst, nullptr);
    if (m_hwndList != nullptr)
    {
        ListView_SetExtendedListViewStyle(
            m_hwndList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        // 建 6 个列。
        for (int col = 0; col < kColumnCount; ++col)
        {
            LVCOLUMN lvc = { 0 };
            lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            lvc.iSubItem = col;
            lvc.cx = kColumnWidths[col];
            lvc.pszText = const_cast<LPWSTR>(kColumnTitles[col]);
            ListView_InsertColumn(m_hwndList, col, &lvc);
        }

        // 建状态圆点 ImageList 并赋给列表。
        BuildStatusImageList();
    }

    // ---- 底部：两栏标题静态文本 + 两栏只读 EDIT ----
    m_hwndReqCap = ::CreateWindowEx(
        0, L"STATIC", kTextReqCaption,
        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
        0, 0, 0, 0, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStaticReqCap)),
        hInst, nullptr);

    m_hwndRespCap = ::CreateWindowEx(
        0, L"STATIC", kTextRespCaption,
        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
        0, 0, 0, 0, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStaticRespCap)),
        hInst, nullptr);

    const DWORD kEditStyle = WS_CHILD | WS_VISIBLE | WS_BORDER
        | WS_VSCROLL | WS_HSCROLL
        | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL;

    m_hwndEditReq = ::CreateWindowEx(
        0, L"EDIT", L"", kEditStyle,
        0, 0, 0, 0, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEditReq)),
        hInst, nullptr);

    m_hwndEditResp = ::CreateWindowEx(
        0, L"EDIT", L"", kEditStyle,
        0, 0, 0, 0, m_hWnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEditResp)),
        hInst, nullptr);

    // 等宽字体创建成功时套到两栏 JSON EDIT 上，使 JSON 缩进对齐。
    if (m_hMonoFont != nullptr)
    {
        if (m_hwndEditReq != nullptr)
            ::SendMessage(m_hwndEditReq, WM_SETFONT,
                          reinterpret_cast<WPARAM>(m_hMonoFont), TRUE);
        if (m_hwndEditResp != nullptr)
            ::SendMessage(m_hwndEditResp, WM_SETFONT,
                          reinterpret_cast<WPARAM>(m_hMonoFont), TRUE);
    }

    // 设置窗口图标（标题栏 / 任务栏 / Alt+Tab），与顶栏按钮同款终端图标。
    ApplyWindowIcon();

    // 子控件就位后排一次版。
    LayoutChildren();
    return 0;
}

LRESULT DebugConsoleWindow::OnSize(UINT /*msg*/, WPARAM /*wParam*/,
                                   LPARAM /*lParam*/, BOOL& /*handled*/)
{
    LayoutChildren();
    return 0;
}

LRESULT DebugConsoleWindow::OnClose(UINT /*msg*/, WPARAM /*wParam*/,
                                    LPARAM /*lParam*/, BOOL& /*handled*/)
{
    // 关闭按钮只隐藏窗口，不销毁 —— 保留已加载状态供下次 Toggle 复用。
    ShowWindow(SW_HIDE);
    return 0;
}

// 用矢量图标渲染一张带 alpha 通道的 HICON，供 ApplyWindowIcon 生成窗口图标。
//   size：图标边长（像素，正方形，须 > 0）。
//   icon：要渲染的矢量图标种类。
//   color：图标描边颜色。
//   返回：新建的 HICON，调用方负责 DestroyIcon；失败返回 nullptr。
// 实现：建一张 32bpp 自顶向下的 ARGB DIB（CreateDIBSection 会把像素清零，
// 即初始全透明），用 ui::DrawIcon 在其上以 GDI+ 抗锯齿描边绘出图标，再经
// CreateIconIndirect 包装成图标 —— 边缘像素带正确的部分 alpha，图标在任意
// 背景（标题栏 / 任务栏）上都干净无底色。
static HICON RenderVectorHIcon(int size, ui::Icon icon, COLORREF color)
{
    if (size <= 0)
        return nullptr;

    // 32bpp ARGB DIB 的位图头：负高度表示像素自顶向下排布。
    BITMAPV5HEADER header;
    ::ZeroMemory(&header, sizeof(header));
    header.bV5Size        = sizeof(BITMAPV5HEADER);
    header.bV5Width       = size;
    header.bV5Height      = -size;
    header.bV5Planes      = 1;
    header.bV5BitCount    = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask     = 0x00FF0000;
    header.bV5GreenMask   = 0x0000FF00;
    header.bV5BlueMask    = 0x000000FF;
    header.bV5AlphaMask   = 0xFF000000;

    HDC hdcScreen = ::GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP hbmColor = ::CreateDIBSection(
        hdcScreen, reinterpret_cast<BITMAPINFO*>(&header),
        DIB_RGB_COLORS, &bits, nullptr, 0);
    ::ReleaseDC(nullptr, hdcScreen);
    if (hbmColor == nullptr)
        return nullptr;

    // 把矢量图标画到 DIB 上（DrawIcon 内部以 GDI+ 抗锯齿绘制描边）。
    HDC hdcMem = ::CreateCompatibleDC(nullptr);
    HGDIOBJ hbmOld = ::SelectObject(hdcMem, hbmColor);
    RECT rc = { 0, 0, size, size };
    ui::DrawIcon(hdcMem, icon, rc, color);
    ::SelectObject(hdcMem, hbmOld);
    ::DeleteDC(hdcMem);

    // ICONINFO 必须带一张掩码位图；32bpp alpha 图标渲染时系统以 alpha 通道
    // 为准、掩码不参与显示，建一张同尺寸单色位图占位即可。
    HBITMAP hbmMask = ::CreateBitmap(size, size, 1, 1, nullptr);

    ICONINFO iconInfo;
    ::ZeroMemory(&iconInfo, sizeof(iconInfo));
    iconInfo.fIcon    = TRUE;
    iconInfo.hbmColor = hbmColor;
    iconInfo.hbmMask  = hbmMask;
    HICON hIcon = ::CreateIconIndirect(&iconInfo);

    // CreateIconIndirect 已复制位图数据，原始位图可立即释放。
    ::DeleteObject(hbmColor);
    ::DeleteObject(hbmMask);
    return hIcon;
}

void DebugConsoleWindow::ApplyWindowIcon()
{
    // 大图标用于任务栏 / Alt+Tab，小图标用于标题栏左上角；两者都用顶栏
    // 「调试控制台」按钮同款的终端矢量图标，按系统图标尺寸分别渲染以求清晰。
    const int bigSize   = ::GetSystemMetrics(SM_CXICON);
    const int smallSize = ::GetSystemMetrics(SM_CXSMICON);

    m_hIconBig   = RenderVectorHIcon(bigSize,   ui::Icon::Terminal, kWindowIconColor);
    m_hIconSmall = RenderVectorHIcon(smallSize, ui::Icon::Terminal, kWindowIconColor);

    if (m_hIconBig != nullptr)
        ::SendMessage(m_hWnd, WM_SETICON, ICON_BIG,
                      reinterpret_cast<LPARAM>(m_hIconBig));
    if (m_hIconSmall != nullptr)
        ::SendMessage(m_hWnd, WM_SETICON, ICON_SMALL,
                      reinterpret_cast<LPARAM>(m_hIconSmall));
}

LRESULT DebugConsoleWindow::OnDestroy(UINT /*msg*/, WPARAM /*wParam*/,
                                      LPARAM /*lParam*/, BOOL& /*handled*/)
{
    // 释放本窗口持有的资源。子控件随父窗口自动销毁，无需逐个 DestroyWindow。
    if (m_hImageList != nullptr)
    {
        ImageList_Destroy(m_hImageList);
        m_hImageList = nullptr;
    }
    if (m_hMonoFont != nullptr)
    {
        ::DeleteObject(m_hMonoFont);
        m_hMonoFont = nullptr;
    }
    if (m_hIconBig != nullptr)
    {
        ::DestroyIcon(m_hIconBig);
        m_hIconBig = nullptr;
    }
    if (m_hIconSmall != nullptr)
    {
        ::DestroyIcon(m_hIconSmall);
        m_hIconSmall = nullptr;
    }

    // 子控件句柄随窗口销毁失效，统一清空。
    m_hwndBtnClear = nullptr;
    m_hwndBtnPause = nullptr;
    m_hwndBtnExport = nullptr;
    m_hwndChkAuto = nullptr;
    m_hwndChkError = nullptr;
    m_hwndCount = nullptr;
    m_hwndList = nullptr;
    m_hwndReqCap = nullptr;
    m_hwndRespCap = nullptr;
    m_hwndEditReq = nullptr;
    m_hwndEditResp = nullptr;
    return 0;
}

LRESULT DebugConsoleWindow::OnDcRefresh(UINT /*msg*/, WPARAM /*wParam*/,
                                        LPARAM /*lParam*/, BOOL& /*handled*/)
{
    // 数据层缓冲有改动，做一次全量刷新。
    RefreshView();
    return 0;
}

LRESULT DebugConsoleWindow::OnNotify(UINT /*msg*/, WPARAM /*wParam*/,
                                     LPARAM lParam, BOOL& handled)
{
    NMHDR* hdr = reinterpret_cast<NMHDR*>(lParam);
    if (hdr == nullptr || hdr->hwndFrom != m_hwndList)
    {
        handled = FALSE;
        return 0;
    }

    switch (hdr->code)
    {
    case LVN_GETDISPINFO:
        // 虚拟列表请求某单元格的文本 / 图标。
        OnGetDispInfo(reinterpret_cast<NMLVDISPINFO*>(lParam));
        return 0;

    case LVN_ITEMCHANGED:
        {
            // 选中行变化：仅在「新被选中」时回填详情面板。
            NMLISTVIEW* nmlv = reinterpret_cast<NMLISTVIEW*>(lParam);
            bool becameSelected =
                ((nmlv->uChanged & LVIF_STATE) != 0)
                && ((nmlv->uNewState & LVIS_SELECTED) != 0)
                && ((nmlv->uOldState & LVIS_SELECTED) == 0);
            if (becameSelected)
            {
                m_selectedIndex = nmlv->iItem;
                UpdateDetailPanes(m_selectedIndex);
            }
        }
        return 0;

    default:
        handled = FALSE;
        return 0;
    }
}

LRESULT DebugConsoleWindow::OnCommand(UINT /*msg*/, WPARAM wParam,
                                      LPARAM /*lParam*/, BOOL& handled)
{
    int ctrlId = LOWORD(wParam);
    switch (ctrlId)
    {
    case kIdBtnClear:
        DebugConsole::Instance().Clear();
        FA_LOGI(L"DebugConsoleWindow: 已清空调试记录");
        RefreshView();
        return 0;

    case kIdBtnPause:
        {
            // 翻转暂停态，并据新态切换按钮文案（暂停 ↔ 继续）。
            bool nowPaused = !DebugConsole::Instance().IsPaused();
            DebugConsole::Instance().SetPaused(nowPaused);
            if (m_hwndBtnPause != nullptr)
                ::SetWindowText(m_hwndBtnPause,
                                nowPaused ? kTextResume : kTextPause);
        }
        return 0;

    case kIdBtnExport:
        DoExport();
        return 0;

    case kIdChkAutoScroll:
        // 读复选框当前勾选态写回成员，并刷新（可能立即触发滚到末行）。
        m_autoScroll =
            (::SendMessage(m_hwndChkAuto, BM_GETCHECK, 0, 0) == BST_CHECKED);
        RefreshView();
        return 0;

    case kIdChkErrorOnly:
        // 切换过滤后 m_view 内容变化，须重建并复位选中。
        m_errorOnly =
            (::SendMessage(m_hwndChkError, BM_GETCHECK, 0, 0) == BST_CHECKED);
        m_selectedIndex = -1;
        RefreshView();
        return 0;

    default:
        handled = FALSE;
        return 0;
    }
}

// ============================ 内部辅助 ==============================

void DebugConsoleWindow::LayoutChildren()
{
    RECT rc = { 0, 0, 0, 0 };
    ::GetClientRect(m_hWnd, &rc);
    int clientW = rc.right - rc.left;
    int clientH = rc.bottom - rc.top;
    if (clientW <= 0 || clientH <= 0)
        return;

    // ---- 工具条：从左到右依次摆放各控件 ----
    int x = kToolbarMargin;
    const int top = kToolbarBtnTop;
    const int h = kToolbarCtrlH;

    if (m_hwndBtnClear != nullptr)
    {
        ::MoveWindow(m_hwndBtnClear, x, top, kBtnWidthClear, h, TRUE);
        x += kBtnWidthClear + kToolbarMargin;
    }
    if (m_hwndBtnPause != nullptr)
    {
        ::MoveWindow(m_hwndBtnPause, x, top, kBtnWidthPause, h, TRUE);
        x += kBtnWidthPause + kToolbarMargin;
    }
    if (m_hwndBtnExport != nullptr)
    {
        ::MoveWindow(m_hwndBtnExport, x, top, kBtnWidthExport, h, TRUE);
        x += kBtnWidthExport + kToolbarMargin;
    }
    if (m_hwndChkAuto != nullptr)
    {
        ::MoveWindow(m_hwndChkAuto, x, top, kChkWidthAuto, h, TRUE);
        x += kChkWidthAuto + kToolbarMargin;
    }
    if (m_hwndChkError != nullptr)
    {
        ::MoveWindow(m_hwndChkError, x, top, kChkWidthError, h, TRUE);
        x += kChkWidthError + kToolbarMargin;
    }
    if (m_hwndCount != nullptr)
    {
        // 计数静态文本贴右边缘摆放。
        int countX = clientW - kToolbarMargin - kCountStaticW;
        if (countX < x)
            countX = x;
        ::MoveWindow(m_hwndCount, countX, top, kCountStaticW, h, TRUE);
    }

    // ---- 主体：去掉工具条后，按 55% / 45% 上下分割 ----
    int bodyTop = kToolbarHeight;
    int bodyH = clientH - bodyTop;
    if (bodyH < 0)
        bodyH = 0;
    int listH = bodyH * kListAreaPercent / kPercentBase;
    int detailTop = bodyTop + listH;
    int detailH = bodyH - listH;

    if (m_hwndList != nullptr)
        ::MoveWindow(m_hwndList, 0, bodyTop, clientW, listH, TRUE);

    // ---- 详情区：标题占一行，其下左右两栏 EDIT 各占一半宽 ----
    int paneTop = detailTop + kDetailCaptionH;
    int paneH = detailH - kDetailCaptionH;
    if (paneH < 0)
        paneH = 0;
    int halfW = (clientW - kPaneGap) / 2;
    if (halfW < 0)
        halfW = 0;

    if (m_hwndReqCap != nullptr)
        ::MoveWindow(m_hwndReqCap, kToolbarMargin, detailTop,
                     halfW, kDetailCaptionH, TRUE);
    if (m_hwndRespCap != nullptr)
        ::MoveWindow(m_hwndRespCap, halfW + kPaneGap + kToolbarMargin,
                     detailTop, halfW, kDetailCaptionH, TRUE);
    if (m_hwndEditReq != nullptr)
        ::MoveWindow(m_hwndEditReq, 0, paneTop, halfW, paneH, TRUE);
    if (m_hwndEditResp != nullptr)
        ::MoveWindow(m_hwndEditResp, halfW + kPaneGap, paneTop,
                     clientW - halfW - kPaneGap, paneH, TRUE);
}

void DebugConsoleWindow::RefreshView()
{
    if (m_hwndList == nullptr)
        return;

    // 取数据层快照，据「仅看错误」筛出本次要展示的记录集合。
    std::vector<DebugEntry> snapshot = DebugConsole::Instance().Snapshot();
    m_view.clear();
    m_view.reserve(snapshot.size());
    for (size_t i = 0; i < snapshot.size(); ++i)
    {
        if (m_errorOnly)
        {
            DebugEntryKind kind = snapshot[i].Kind();
            if (kind != DebugEntryKind::BusinessFail
                && kind != DebugEntryKind::TransportFail)
            {
                continue;
            }
        }
        m_view.push_back(snapshot[i]);
    }

    // 虚拟列表：只设行数，单元格文本走 LVN_GETDISPINFO 回调。
    int count = static_cast<int>(m_view.size());
    ListView_SetItemCountEx(m_hwndList, count,
                            LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    ::InvalidateRect(m_hwndList, nullptr, FALSE);

    // 更新「共 N 条」计数文本。
    if (m_hwndCount != nullptr)
    {
        CString text;
        text.Format(L"共 %d 条", count);
        ::SetWindowText(m_hwndCount, text);
    }

    // 自动滚动：开启且有数据时把末行滚入可见区。
    if (m_autoScroll && count > 0)
        ListView_EnsureVisible(m_hwndList, count - 1, FALSE);

    // 选中行下标可能因筛选 / 清空而越界，越界则清空详情面板。
    if (m_selectedIndex >= count)
    {
        m_selectedIndex = -1;
        UpdateDetailPanes(-1);
    }
}

void DebugConsoleWindow::OnGetDispInfo(NMLVDISPINFO* dispInfo)
{
    if (dispInfo == nullptr)
        return;

    LVITEM& item = dispInfo->item;
    int row = item.iItem;
    if (row < 0 || row >= static_cast<int>(m_view.size()))
        return;

    const DebugEntry& entry = m_view[row];

    // 需要状态圆点图标时：据记录结果类别选取对应下标。
    if ((item.mask & LVIF_IMAGE) != 0)
    {
        switch (entry.Kind())
        {
        case DebugEntryKind::Pending:
            item.iImage = kIconGrey;
            break;
        case DebugEntryKind::Success:
            item.iImage = kIconGreen;
            break;
        case DebugEntryKind::BusinessFail:
            item.iImage = kIconOrange;
            break;
        case DebugEntryKind::TransportFail:
            item.iImage = kIconRed;
            break;
        default:
            item.iImage = kIconGrey;
            break;
        }
    }

    // 需要单元格文本时：据列下标拼出文本写入控件提供的缓冲。
    if ((item.mask & LVIF_TEXT) != 0 && item.pszText != nullptr)
    {
        CString cell;
        switch (item.iSubItem)
        {
        case kColSeq:
            cell.Format(L"%llu", entry.m_seq);
            break;

        case kColTime:
            cell = entry.m_reqTime;
            break;

        case kColApi:
            cell = entry.m_apiName;
            break;

        case kColStatus:
            if (entry.m_state == DebugEntryState::Pending)
            {
                // 进行中：响应尚未回来。
                cell = kTextPendingDots;
            }
            else if (!entry.m_transportOk)
            {
                // 传输失败：叉号 + 中文原因。
                cell.Format(L"✕ %s", entry.m_errorText.GetString());
            }
            else
            {
                // 传输成功：显示 HTTP 状态码数字。
                cell.Format(L"%d", entry.m_httpStatus);
            }
            break;

        case kColElapsed:
            if (entry.m_state == DebugEntryState::Pending)
                cell = kTextDash;
            else
                cell.Format(L"%u ms", entry.m_elapsedMs);
            break;

        case kColBizCode:
            if (entry.m_state == DebugEntryState::Pending
                || entry.m_businessCode == kDebugNoBusinessCode)
            {
                // 进行中、或响应体无顶层 code 字段：以破折号占位。
                cell = kTextDash;
            }
            else
            {
                cell.Format(L"%d", entry.m_businessCode);
            }
            break;

        default:
            cell.Empty();
            break;
        }

        // 拷入控件提供的缓冲，按 cchTextMax 截断防越界。
        ::lstrcpyn(item.pszText, cell, item.cchTextMax);
    }
}

void DebugConsoleWindow::UpdateDetailPanes(int index)
{
    // 越界（含 -1）时清空两栏 EDIT。
    if (index < 0 || index >= static_cast<int>(m_view.size()))
    {
        if (m_hwndEditReq != nullptr)
            ::SetWindowText(m_hwndEditReq, L"");
        if (m_hwndEditResp != nullptr)
            ::SetWindowText(m_hwndEditResp, L"");
        return;
    }

    const DebugEntry& entry = m_view[index];

    // ---- 请求栏：首行「POST <url>」，其后请求体 JSON ----
    if (m_hwndEditReq != nullptr)
    {
        CString reqText;
        reqText.Format(L"POST %s\r\n\r\n", entry.m_url.GetString());
        reqText += Utf8ToEditText(entry.m_reqBodyUtf8);
        ::SetWindowText(m_hwndEditReq, reqText);
    }

    // ---- 响应栏：首行一句话摘要，其后响应体 JSON ----
    if (m_hwndEditResp != nullptr)
    {
        CString summary;
        if (entry.m_state == DebugEntryState::Pending)
        {
            // 进行中：尚无应答。
            summary = kTextPendingDots;
        }
        else if (!entry.m_transportOk)
        {
            // 传输失败：摘要给出中文原因。
            summary.Format(L"传输失败 · %s", entry.m_errorText.GetString());
        }
        else
        {
            // 传输成功：摘要给出「HTTP <状态> · <耗时> ms」。
            summary.Format(L"HTTP %d · %u ms",
                           entry.m_httpStatus, entry.m_elapsedMs);
        }

        CString respText = summary;
        respText += L"\r\n\r\n";
        respText += Utf8ToEditText(entry.m_respBodyUtf8);
        ::SetWindowText(m_hwndEditResp, respText);
    }
}

bool DebugConsoleWindow::BuildStatusImageList()
{
    if (m_hwndList == nullptr)
        return false;

    // 建一个能容 4 张 16x16 32 位带掩码图标的 ImageList。
    m_hImageList = ImageList_Create(kIconSize, kIconSize,
                                    ILC_COLOR32 | ILC_MASK, kIconCount, 0);
    if (m_hImageList == nullptr)
        return false;

    // 四种圆点颜色，下标与 StatusIconIndex 对应。
    const COLORREF dotColors[kIconCount] =
    {
        kDotColorGrey, kDotColorGreen, kDotColorOrange, kDotColorRed,
    };

    HDC hScreenDc = ::GetDC(nullptr);
    if (hScreenDc == nullptr)
        return false;

    for (int i = 0; i < kIconCount; ++i)
    {
        // 在 16x16 位图上：先以透明色键铺底，再画一个实心圆。
        HDC hMemDc = ::CreateCompatibleDC(hScreenDc);
        HBITMAP hBmp = ::CreateCompatibleBitmap(hScreenDc,
                                                kIconSize, kIconSize);
        HBITMAP hOldBmp =
            static_cast<HBITMAP>(::SelectObject(hMemDc, hBmp));

        RECT rcAll = { 0, 0, kIconSize, kIconSize };
        HBRUSH hBkBrush = ::CreateSolidBrush(kIconBkColor);
        ::FillRect(hMemDc, &rcAll, hBkBrush);
        ::DeleteObject(hBkBrush);

        HBRUSH hDotBrush = ::CreateSolidBrush(dotColors[i]);
        HBRUSH hOldBrush =
            static_cast<HBRUSH>(::SelectObject(hMemDc, hDotBrush));
        HPEN hDotPen = ::CreatePen(PS_SOLID, 1, dotColors[i]);
        HPEN hOldPen = static_cast<HPEN>(::SelectObject(hMemDc, hDotPen));

        // 圆四周留 kIconDotInset 像素白边，视觉更精致。
        ::Ellipse(hMemDc, kIconDotInset, kIconDotInset,
                  kIconSize - kIconDotInset, kIconSize - kIconDotInset);

        ::SelectObject(hMemDc, hOldPen);
        ::DeleteObject(hDotPen);
        ::SelectObject(hMemDc, hOldBrush);
        ::DeleteObject(hDotBrush);
        ::SelectObject(hMemDc, hOldBmp);

        // 以透明色键加入 ImageList —— 色键处的像素被视作透明。
        ImageList_AddMasked(m_hImageList, hBmp, kIconBkColor);

        ::DeleteObject(hBmp);
        ::DeleteDC(hMemDc);
    }

    ::ReleaseDC(nullptr, hScreenDc);

    // 赋给 ListView 作小图标列表。
    ListView_SetImageList(m_hwndList, m_hImageList, LVSIL_SMALL);
    return true;
}

void DebugConsoleWindow::DoExport()
{
    // ---- 拼默认文件名：flamingoAdmin-debug-<时间戳>.txt ----
    SYSTEMTIME st;
    ::GetLocalTime(&st);
    CString defName;
    defName.Format(L"flamingoAdmin-debug-%04d%02d%02d-%02d%02d%02d.txt",
                   st.wYear, st.wMonth, st.wDay,
                   st.wHour, st.wMinute, st.wSecond);

    wchar_t fileName[kSaveFileNameBufLen] = { 0 };
    ::lstrcpyn(fileName, defName, kSaveFileNameBufLen);

    OPENFILENAME ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hWnd;
    ofn.lpstrFilter = kExportFilter;
    ofn.lpstrFile   = fileName;
    ofn.nMaxFile    = kSaveFileNameBufLen;
    ofn.lpstrDefExt = kExportDefExt;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!::GetSaveFileName(&ofn))
    {
        // 用户取消保存对话框：静默返回，不算错误。
        return;
    }

    // ---- 取导出文本并转 UTF-8 字节 ----
    CString content = DebugConsole::Instance().FormatAllForExport();
    int utf8Len = ::WideCharToMultiByte(CP_UTF8, 0, content, -1,
                                        nullptr, 0, nullptr, nullptr);
    // utf8Len 含结尾 '\0'，写文件时去掉它。
    std::string utf8;
    if (utf8Len > 1)
    {
        utf8.resize(static_cast<size_t>(utf8Len - 1));
        ::WideCharToMultiByte(CP_UTF8, 0, content, -1,
                              &utf8[0], utf8Len - 1, nullptr, nullptr);
    }

    // ---- 写文件：先 UTF-8 BOM，再正文字节 ----
    HANDLE hFile = ::CreateFile(fileName, GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        FA_LOGE(L"DebugConsoleWindow: 导出失败，无法创建文件 %s", fileName);
        ::MessageBox(m_hWnd, L"导出失败：无法创建目标文件。",
                     kTextWindowTitle, MB_OK | MB_ICONWARNING);
        return;
    }

    DWORD written = 0;
    ::WriteFile(hFile, kUtf8Bom, sizeof(kUtf8Bom), &written, nullptr);
    if (!utf8.empty())
    {
        ::WriteFile(hFile, utf8.data(),
                    static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    ::CloseHandle(hFile);

    FA_LOGI(L"DebugConsoleWindow: 已导出调试记录到 %s", fileName);
}

CString DebugConsoleWindow::Utf8ToEditText(const std::string& utf8)
{
    if (utf8.empty())
        return CString();

    // ---- UTF-8 字节 → UTF-16 ----
    int wideLen = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                        static_cast<int>(utf8.size()),
                                        nullptr, 0);
    if (wideLen <= 0)
        return CString();

    std::wstring wide;
    wide.resize(static_cast<size_t>(wideLen));
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                          static_cast<int>(utf8.size()),
                          &wide[0], wideLen);

    // ---- 把裸 '\n' 规整为 "\r\n" —— 普通 EDIT 只认 "\r\n" 才换行 ----
    // 已经是 "\r\n" 的不重复加 '\r'：遇到 '\n' 时仅当其前一字符不是 '\r' 才补。
    CString result;
    for (size_t i = 0; i < wide.size(); ++i)
    {
        wchar_t ch = wide[i];
        if (ch == L'\n')
        {
            if (i == 0 || wide[i - 1] != L'\r')
                result += L'\r';
            result += L'\n';
        }
        else if (ch == L'\r')
        {
            // 跳过裸 '\r'：留待其后若紧跟 '\n' 时由上一分支统一补。
            // 若 '\r' 后并非 '\n'，仍保留之以免丢字符。
            if (i + 1 < wide.size() && wide[i + 1] == L'\n')
                continue;
            result += ch;
        }
        else
        {
            result += ch;
        }
    }
    return result;
}

} // namespace flamingoadmin
