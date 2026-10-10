/**
 *  ScreenCaptureOverlay.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-06-04
 */
#include "stdafx.h"
#include "ScreenCaptureOverlay.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY / DuiNotify / DUIN_CLICK
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"    // 截图期间临时改全局 DPI，结束后恢复
#include "../../Source/Common/Log/IULog.h"                               // LOG_INFO（截图 DPI 诊断）
#include "CaptureExclusion.h"                                     // 截图时把发起截图的聊天窗排除在外

#include <gdiplus.h>
#include <commdlg.h>
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Source/Common/Util/FileDialogFilter.h"   // filedlg::MakeFilter：文件对话框过滤串的说明文字逐段翻译
#include "../Host/ShotHostServices.h"             // 「识别文字」：关闭浮层后经宿主交给 exe 的识别结果窗口

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")

namespace balloonscreenshot {

namespace {

// ---- 线程级 DPI 上下文切换（截图期间用，Win10 1607+）----
//
// Flamingo.exe 是 DPI-unaware 进程：直接抓屏 / 建窗会落在被 DWM 拉伸的逻辑像素空间，
// 截图被放大。把【本线程】临时切到 Per-Monitor-V2，使抓屏与浮层都在物理像素、窗口不被
// DWM 拉伸、1:1 呈现。窗口创建后其 DPI 上下文即固定，随后恢复线程上下文不影响主程序。
typedef HANDLE (WINAPI* FN_SetThreadCtx)(HANDLE);

// DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 的哨兵值（避免依赖较新 SDK 头）。
const HANDLE kPmv2Context = (HANDLE)(INT_PTR)-4;

// 截图界面里「截取全屏」的按键：与 Ctrl 一起按下（Ctrl+A）
const WPARAM kSelectAllKey = 'A';
// GetKeyState 返回值里表示「该键正按着」的那一位
const SHORT kKeyDownMask = (SHORT)0x8000;

// 本进程当前是否正有一次截图在进行。截图界面的局部消息循环会把 WM_HOTKEY 照常派发给
// 主面板，若不加判断，截图过程中再按一次截图快捷键就会在当前截图界面之上再套一层。
// 只在 UI 线程读写。
bool g_overlayRunning = false;

// 在作用域内把 g_overlayRunning 置为 true，离开作用域（含提前返回）时复位。
class ScopedRunningFlag
{
public:
    ScopedRunningFlag()
    {
        g_overlayRunning = true;
    }
    ~ScopedRunningFlag()
    {
        g_overlayRunning = false;
    }
};

// EnumDisplayMonitors 的回调：把每块显示器的矩形（屏幕坐标）追加到 lParam 指向的表里
BOOL CALLBACK CollectMonitorProc(HMONITOR hMon, HDC /*hdc*/, LPRECT /*rcClip*/, LPARAM lParam)
{
    std::vector<RECT>* out = reinterpret_cast<std::vector<RECT>*>(lParam);
    MONITORINFO mi;
    ::ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfoW(hMon, &mi))
    {
        out->push_back(mi.rcMonitor);
    }
    return TRUE;
}

// 取 SetThreadDpiAwarenessContext；老系统（API 不存在）返回 NULL，调用方退回原行为。
FN_SetThreadCtx GetSetThreadCtxFn()
{
    HMODULE u = ::GetModuleHandleW(L"user32.dll");
    if (u == NULL)
    {
        return NULL;
    }
    return (FN_SetThreadCtx)::GetProcAddress(u, "SetThreadDpiAwarenessContext");
}

// 取指定 MIME 类型（这里只用 image/png）的 GDI+ 编码器 CLSID。
// 标准做法：枚举所有编码器，按 MimeType 匹配。失败返回 false。
bool GetPngEncoderClsid(CLSID& clsidOut)
{
    UINT num = 0;
    UINT size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0)
    {
        return false;
    }
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    if (Gdiplus::GetImageEncoders(num, size, codecs) != Gdiplus::Ok)
    {
        return false;
    }
    for (UINT i = 0; i < num; ++i)
    {
        if (::wcscmp(codecs[i].MimeType, L"image/png") == 0)
        {
            clsidOut = codecs[i].Clsid;
            return true;
        }
    }
    return false;
}

// 把 HBITMAP 存成 PNG 文件。成功返回 true。
bool SaveBitmapAsPng(HBITMAP hbmp, const wchar_t* path)
{
    if (hbmp == NULL || path == NULL)
    {
        return false;
    }
    CLSID clsid;
    if (!GetPngEncoderClsid(clsid))
    {
        return false;
    }
    Gdiplus::Bitmap bmp(hbmp, (HPALETTE)NULL);
    if (bmp.GetLastStatus() != Gdiplus::Ok)
    {
        return false;
    }
    return bmp.Save(path, &clsid, NULL) == Gdiplus::Ok;
}

} // anonymous namespace

bool CScreenCaptureOverlay::IsRunning()
{
    return g_overlayRunning;
}

CScreenCaptureOverlay::CScreenCaptureOverlay()
    : m_frozen(NULL)
    , m_resultCrop(NULL)
    , m_vx(0)
    , m_vy(0)
    , m_vw(0)
    , m_vh(0)
    , m_canvas(NULL)
    , m_done(false)
    , m_action(kActNone)
    , m_sink(NULL)
    , m_hasSendTarget(false)
    , m_ownerHwnd(NULL)
{
}

CScreenCaptureOverlay::~CScreenCaptureOverlay()
{
    if (m_resultCrop != NULL)
    {
        ::DeleteObject(m_resultCrop);
        m_resultCrop = NULL;
    }
    if (m_frozen != NULL)
    {
        ::DeleteObject(m_frozen);
        m_frozen = NULL;
    }
}

bool CScreenCaptureOverlay::CaptureVirtualDesktop()
{
    m_vx = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    m_vy = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    m_vw = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    m_vh = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (m_vw <= 0 || m_vh <= 0)
    {
        return false;
    }

    HDC screen = ::GetDC(NULL);
    if (screen == NULL)
    {
        return false;
    }
    // 记录抓屏区域与 DPI（PMv2 上下文下应为物理像素）；便于排查不同机器的 DPI 问题。
    LOG_INFO("screenshot: capturing virtual desktop=(%d,%d,%dx%d), dpi=%d.",
             m_vx, m_vy, m_vw, m_vh, ::GetDeviceCaps(screen, LOGPIXELSX));
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, m_vw, m_vh);
    bool ok = false;
    if (mem != NULL && bmp != NULL)
    {
        HBITMAP old = (HBITMAP)::SelectObject(mem, bmp);
        // CAPTUREBLT 让分层 / 半透明窗口也能正确抓到。
        ok = ::BitBlt(mem, 0, 0, m_vw, m_vh, screen, m_vx, m_vy, SRCCOPY | CAPTUREBLT) != FALSE;
        ::SelectObject(mem, old);
    }
    if (mem != NULL)
    {
        ::DeleteDC(mem);
    }
    ::ReleaseDC(NULL, screen);

    if (!ok)
    {
        if (bmp != NULL)
        {
            ::DeleteObject(bmp);
        }
        return false;
    }
    m_frozen = bmp;
    return true;
}

HBITMAP CScreenCaptureOverlay::CropSelection()
{
    if (m_canvas == NULL || !m_canvas->HasUsableSelection() || m_frozen == NULL)
    {
        return NULL;
    }
    RECT sel = m_canvas->GetSelectionRect();   // 客户区坐标 = 冻结位图像素坐标
    int w = sel.right - sel.left;
    int h = sel.bottom - sel.top;
    if (w <= 0 || h <= 0)
    {
        return NULL;
    }

    HDC screen = ::GetDC(NULL);
    HDC srcDc = ::CreateCompatibleDC(screen);
    HDC dstDc = ::CreateCompatibleDC(screen);
    HBITMAP crop = ::CreateCompatibleBitmap(screen, w, h);
    ::ReleaseDC(NULL, screen);

    HBITMAP oldSrc = (HBITMAP)::SelectObject(srcDc, m_frozen);
    HBITMAP oldDst = (HBITMAP)::SelectObject(dstDc, crop);
    ::BitBlt(dstDc, 0, 0, w, h, srcDc, sel.left, sel.top, SRCCOPY);
    // 背景已拷完，先把冻结位图脱离 srcDc 释放掉——烤标注时马赛克要再次采样它，而
    // 同一 HBITMAP 不能同时选进两个 DC（否则马赛克会裁出黑块，与早先黑块同源）。
    ::SelectObject(srcDc, oldSrc);
    ::DeleteDC(srcDc);
    // 把标注烤进裁剪结果（所见即所得：复制 / 保存 / 发送 / 发送到对话框 都带标注 + 马赛克）。
    if (m_canvas != NULL)
    {
        m_canvas->BakeAnnotationsInto(dstDc, sel);
    }
    ::SelectObject(dstDc, oldDst);
    ::DeleteDC(dstDc);
    return crop;
}

void CScreenCaptureOverlay::DoCopyToClipboard()
{
    if (m_resultCrop == NULL)
    {
        return;
    }
    if (!::OpenClipboard(NULL))
    {
        return;
    }
    ::EmptyClipboard();
    // 复制一份给剪贴板（剪贴板取得所有权，不能交出 m_resultCrop 本体——它由本类析构释放）。
    HDC screen = ::GetDC(NULL);
    HDC srcDc = ::CreateCompatibleDC(screen);
    HDC dstDc = ::CreateCompatibleDC(screen);
    BITMAP bm = { 0 };
    ::GetObject(m_resultCrop, sizeof(bm), &bm);
    HBITMAP dib = ::CreateCompatibleBitmap(screen, bm.bmWidth, bm.bmHeight);
    HBITMAP oldSrc = (HBITMAP)::SelectObject(srcDc, m_resultCrop);
    HBITMAP oldDst = (HBITMAP)::SelectObject(dstDc, dib);
    ::BitBlt(dstDc, 0, 0, bm.bmWidth, bm.bmHeight, srcDc, 0, 0, SRCCOPY);
    ::SelectObject(srcDc, oldSrc);
    ::SelectObject(dstDc, oldDst);
    ::DeleteDC(srcDc);
    ::DeleteDC(dstDc);
    ::ReleaseDC(NULL, screen);

    ::SetClipboardData(CF_BITMAP, dib);   // 剪贴板接管 dib 所有权
    ::CloseClipboard();
}

void CScreenCaptureOverlay::DoSaveToFile()
{
    if (m_resultCrop == NULL)
    {
        return;
    }
    wchar_t file[MAX_PATH] = { 0 };
    ::wcscpy_s(file, MAX_PATH, TR(L"截图.png"));
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = NULL;
    //文件类型过滤串：说明文字按界面语言翻译后再拼成带 \0 的过滤串，须活到对话框关闭
    const std::wstring fileFilter = filedlg::MakeFilter(TR(L"PNG 图片"), L"*.png", TR(L"所有文件"), L"*.*");
    ofn.lpstrFilter = fileFilter.c_str();
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"png";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!::GetSaveFileNameW(&ofn))
    {
        return;   // 用户取消保存
    }
    SaveBitmapAsPng(m_resultCrop, file);
}

void CScreenCaptureOverlay::DoSendToChat()
{
    // 不绑定会话时操作栏没有「发送」，走不到这里；这里再判一次
    if (m_resultCrop == NULL || m_sink == NULL)
    {
        return;
    }
    // 存到临时目录的唯一 PNG 文件，再把路径交回发起方（文件留在临时目录）。
    wchar_t dir[MAX_PATH] = { 0 };
    DWORD n = ::GetTempPathW(MAX_PATH, dir);
    if (n == 0 || n >= MAX_PATH)
    {
        return;
    }
    wchar_t path[MAX_PATH] = { 0 };
    ::swprintf_s(path, MAX_PATH, L"%sflamingo_shot_%lu.png", dir, ::GetTickCount());
    if (!SaveBitmapAsPng(m_resultCrop, path))
    {
        return;
    }
    // 按最终动作回调接收方：发送到对话框→OnStage，立即发送→OnSend
    if (m_action == kActStage)
    {
        m_sink->OnStage(path);
    }
    else
    {
        m_sink->OnSend(path);
    }
}

LRESULT CScreenCaptureOverlay::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* n =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);

    // 文字编辑框每次内容变化（DUIN_VALUECHANGED）→ 让画布把编辑框高度长到刚好容下文字
    // （所有文字可见、不溢出，故竖直滚动条自动隐藏）。
    if (n != NULL && n->code == balloonwjui::DUIN_VALUECHANGED
        && m_canvas != NULL && m_canvas->IsTextEditing())
    {
        m_canvas->GrowTextEditToFit();
        return 0;
    }

    if (n == NULL || n->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }

    // 任何按钮点击前，先提交可能正在编辑的文字框：保证其内容被纳入随后的裁剪、不丢失
    // （CommitTextEdit 幂等，无编辑时为 no-op）。
    if (m_canvas != NULL)
    {
        m_canvas->CommitTextEdit();
    }

    // 选区内双击：换成本次操作栏对应的那个按钮（绑定会话时为「发送到对话框」，否则为「完成」）。
    UINT ctrlId = n->ctrlId;
    if (ctrlId == kIdCanvasDblConfirm)
    {
        ctrlId = DoubleClickActionId(m_hasSendTarget);
    }

    switch (ctrlId)
    {
    //重选：清掉当前选区，回到等待框选状态（不结束）
    case kIdToolReselect:
        if (m_canvas != NULL)
        {
            m_canvas->ClearSelection();
        }
        break;
    //取消：放弃本次截图
    case kIdToolCancel:
        m_action = kActNone;
        m_done = true;
        break;
    //复制，以及不绑定会话时的「完成」：都把选区图放进剪贴板
    case kIdToolCopy:
    case kIdToolDone:
        // 点动作瞬间画布尚存活，先把选区裁出来留到关窗后用。
        m_resultCrop = CropSelection();
        m_action = kActCopy;
        m_done = true;
        break;
    //保存为 PNG 文件
    case kIdToolSave:
        m_resultCrop = CropSelection();
        m_action = kActSave;
        m_done = true;
        break;
    //发送到对话框：插入聊天输入框，不立即发出
    case kIdToolStage:
        m_resultCrop = CropSelection();
        m_action = kActStage;
        m_done = true;
        break;
    //发送：立即作为图片消息发到当前会话
    case kIdToolSend:
        m_resultCrop = CropSelection();
        m_action = kActSend;
        m_done = true;
        break;
    //识别文字：用冻结位图里选区的原始像素（不含标注，标注会盖住文字），关窗后打开识别结果窗口
    case kIdToolOcr:
        if (m_canvas != NULL && m_canvas->HasUsableSelection() && m_frozen != NULL)
        {
            const RECT sel = m_canvas->GetSelectionRect();   // 客户区坐标 = 冻结位图像素坐标
            if (flamingoshot::ShotBitmapToBgra(m_frozen, &sel, m_ocrImage))
            {
                m_action = kActOcr;
                m_done = true;
            }
            else
            {
                LOG_WARNING("screenshot: failed to read the selection pixels for ocr.");
            }
        }
        break;
    //其余通知（标注工具 / 颜色 / 线宽 / 撤销重做）交画布处理
    default:
        if (m_canvas == NULL || !m_canvas->HandleToolbarNotify(n->ctrlId))
        {
            bHandled = FALSE;
        }
        break;
    }
    return 0;
}

LRESULT CScreenCaptureOverlay::OnKeyDown(UINT /*uMsg*/, WPARAM wParam,
                                         LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam == VK_ESCAPE)
    {
        // Esc：等同取消。
        m_action = kActNone;
        m_done = true;
        return 0;
    }
    // Ctrl+A：选区设为鼠标所在的整块显示器。正在输入文字标注时不拦截，交给输入框全选文字。
    if (wParam == kSelectAllKey && (::GetKeyState(VK_CONTROL) & kKeyDownMask) != 0
        && m_canvas != NULL && !m_canvas->IsTextEditing())
    {
        m_canvas->SelectFullMonitorAt(CursorInClient());
        return 0;
    }
    bHandled = FALSE;
    return 0;
}

std::vector<RECT> CScreenCaptureOverlay::EnumMonitorRectsInClient() const
{
    std::vector<RECT> screenRects;
    ::EnumDisplayMonitors(NULL, NULL, CollectMonitorProc, reinterpret_cast<LPARAM>(&screenRects));
    // 本窗口铺在虚拟桌面原点 (m_vx, m_vy)，客户区坐标 = 屏幕坐标 - 原点
    std::vector<RECT> out;
    for (size_t i = 0; i < screenRects.size(); ++i)
    {
        RECT rc = screenRects[i];
        ::OffsetRect(&rc, -m_vx, -m_vy);
        out.push_back(rc);
    }
    return out;
}

POINT CScreenCaptureOverlay::CursorInClient() const
{
    // 窗口过程按本窗口的 DPI 上下文（Per-Monitor-V2）运行，这里取到的是物理像素坐标，
    // 与冻结位图、画布的坐标系一致
    POINT pt = { 0, 0 };
    ::GetCursorPos(&pt);
    pt.x -= m_vx;
    pt.y -= m_vy;
    return pt;
}

bool CScreenCaptureOverlay::Run(const flamingoshot::ScreenshotRequest& request, flamingoshot::IScreenshotSink* sink)
{
    // 同一时刻只允许一次截图（见 g_overlayRunning 的说明）。
    if (g_overlayRunning)
    {
        LOG_INFO("screenshot: another capture is in progress, request ignored.");
        return false;
    }
    ScopedRunningFlag runningFlag;

    m_sink = sink;
    m_hasSendTarget = (m_sink != NULL);
    m_ownerHwnd = request.m_owner;
    m_ocrImage = flamingoshot::ShotBgraImage();
    m_action = kActNone;
    m_done = false;
    LOG_INFO("screenshot: start, exclude hwnd=0x%p, full screen=%d, has send target=%d.",
             request.m_excludeHwnd, request.m_startFullScreen ? 1 : 0, m_hasSendTarget ? 1 : 0);

    // 抓屏之前把发起截图的聊天窗排除在外；截图界面显示之后（或失败时）恢复。
    // 对象析构时也会恢复，保证任何返回路径都不会让窗口一直处于被排除的状态。
    CCaptureExclusion exclusion;
    if (request.m_excludeHwnd != NULL)
    {
        exclusion.Begin(request.m_excludeHwnd);
    }

    // 把本线程临时切到 Per-Monitor-V2（物理像素、窗口不被 DWM 拉伸、1:1），并保存全局
    // DuiResMgr DPI——浮层的 DuiHost::OnCreate 会按其物理 DPI 改全局值，结束后须恢复，
    // 否则主程序（DPI-unaware、按 96 渲染）字号 / 尺寸会变大。
    FN_SetThreadCtx setCtx = GetSetThreadCtxFn();
    HANDLE oldCtx = NULL;
    if (setCtx != NULL)
    {
        oldCtx = setCtx(kPmv2Context);
    }
    int savedResDpi = balloonwjui::DuiResMgr::Inst().GetDpi();

    bool created = false;
    if (CaptureVirtualDesktop())
    {
        // 覆盖整个虚拟桌面的无边框置顶窗口。
        RECT rc = { m_vx, m_vy, m_vx + m_vw, m_vy + m_vh };
        if (Create(NULL, rc, NULL, WS_POPUP, WS_EX_TOPMOST) != NULL)
        {
            // 让画布能收到 OnLButtonDblClk（选区内双击 = 发送到对话框）。
            // （2026-10-06 更正：balloonui 窗口的窗口类其实带 CS_DBLCLKS，系统本就会投递双击，这里的双击合成实际不起作用，保留无害）
            EnableDoubleClick(true);

            // 画布根控件 + 冻结背景。SetRoot 会按客户区 SetRect → 画布 Layout。
            // 不绑定会话时操作栏改为「重选 / 保存 / 取消 / 完成」。
            std::unique_ptr<CaptureCanvas> canvas(new CaptureCanvas(m_hasSendTarget));
            m_canvas = canvas.get();
            m_canvas->SetFrozenBitmap(m_frozen);
            SetRoot(std::move(canvas));

            // 各显示器矩形（截取全屏、工具栏与提示条的落点用）。线程此时仍是
            // Per-Monitor-V2 上下文，取到的是与冻结位图一致的物理像素。
            std::vector<RECT> monitors = EnumMonitorRectsInClient();
            m_canvas->SetMonitorRects(monitors);
            POINT cursor = CursorInClient();
            m_canvas->SetHintAnchor(cursor);
            if (request.m_startFullScreen)
            {
                m_canvas->SelectFullMonitorAt(cursor);
            }
            LOG_INFO("screenshot: overlay created, monitors=%u, cursor=(%d,%d).",
                     (unsigned)monitors.size(), cursor.x, cursor.y);

            ::SetWindowPos(m_hWnd, HWND_TOPMOST, m_vx, m_vy, m_vw, m_vh, SWP_SHOWWINDOW);
            ::SetForegroundWindow(m_hWnd);
            ::SetFocus(m_hWnd);
            created = true;
        }
    }

    // 截图界面已盖住整个桌面（或创建失败），恢复被排除的聊天窗。
    exclusion.Restore();

    // 浮层窗口创建后其 DPI 上下文已固定，恢复本线程原上下文，避免影响主程序其余窗口。
    if (setCtx != NULL && oldCtx != NULL)
    {
        setCtx(oldCtx);
    }

    if (!created)
    {
        balloonwjui::DuiResMgr::Inst().SetDpi(savedResDpi);
        return false;
    }

    // 局部模态消息循环：直到用户做出决定（发送 / 复制 / 保存 / 取消 / Esc）。
    MSG msg;
    while (!m_done && ::GetMessage(&msg, NULL, 0, 0))
    {
        // 正在输入文字时，Esc 只取消当前文字框（不取消整个截图），并吞掉该键不传给 EDIT。
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE
            && m_canvas != NULL && m_canvas->IsTextEditing())
        {
            m_canvas->CancelTextEdit();
            continue;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }

    // 先收掉浮层窗口（连同画布），再执行剪贴板 / 文件对话框 / 发送，避免它们盖在遮罩上。
    if (::IsWindow(m_hWnd))
    {
        DestroyWindow();
    }
    m_canvas = NULL;

    // 恢复全局 DuiResMgr DPI（浮层 OnCreate 期间被改成物理 DPI）。
    balloonwjui::DuiResMgr::Inst().SetDpi(savedResDpi);

    LOG_INFO("screenshot: finished, action=%d.", (int)m_action);
    switch (m_action)
    {
    //复制，或不绑定会话时点了「完成」：放进剪贴板
    case kActCopy:
        DoCopyToClipboard();
        break;
    //保存：弹保存文件对话框
    case kActSave:
        DoSaveToFile();
        break;
    //发送、发送到对话框：存成临时 PNG 后经回调交回聊天窗
    case kActSend:
    case kActStage:
        DoSendToChat();
        break;
    //识别文字：打开识别结果窗口（系统不支持或没有识别语言时弹提示）
    case kActOcr:
        LOG_INFO("screenshot: ocr requested for a %ux%u selection.", m_ocrImage.m_width, m_ocrImage.m_height);
        flamingoshot::ShotLaunchOcr(m_ownerHwnd, m_ocrImage);
        m_ocrImage = flamingoshot::ShotBgraImage();
        break;
    //取消或 Esc：什么也不做
    case kActNone:
    default:
        break;
    }
    m_sink = NULL;
    return m_action == kActSend || m_action == kActStage;
}

} // namespace balloonscreenshot
