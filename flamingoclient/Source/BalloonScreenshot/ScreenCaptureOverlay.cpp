/**
 *  ScreenCaptureOverlay.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-06-04
 */
#include "stdafx.h"
#include "ScreenCaptureOverlay.h"
#include "../../balloonui/balloonui/DuiNotify.h"   // WM_DUI_NOTIFY / DuiNotify / DUIN_CLICK
#include "../../balloonui/balloonui/DuiResMgr.h"    // 截图期间临时改全局 DPI，结束后恢复
#include "../IULog.h"                               // LOG_INFO（截图 DPI 诊断）

#include <gdiplus.h>
#include <commdlg.h>

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
    , m_sendFn(NULL)
    , m_stageFn(NULL)
    , m_sendCtx(NULL)
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
    LOG_INFO("截图: 抓取虚拟桌面=(%d,%d,%dx%d) dpi=%d.",
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
    ::wcscpy_s(file, MAX_PATH, L"截图.png");
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = NULL;
    ofn.lpstrFilter = L"PNG 图片\0*.png\0所有文件\0*.*\0";
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
    // 按最终动作选回调：发送到对话框→stageFn，立即发送→sendFn。
    ScreenshotSendFn fn = (m_action == kActStage) ? m_stageFn : m_sendFn;
    if (m_resultCrop == NULL || fn == NULL)
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
    fn(m_sendCtx, path);
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

    switch (n->ctrlId)
    {
    case kIdToolReselect:
        // 重选：清掉当前选区，回到等待框选状态（不结束）。
        if (m_canvas != NULL)
        {
            m_canvas->ClearSelection();
        }
        break;
    case kIdToolCancel:
        m_action = kActNone;
        m_done = true;
        break;
    case kIdToolCopy:
        // 点动作瞬间画布尚存活，先把选区裁出来留到关窗后用。
        m_resultCrop = CropSelection();
        m_action = kActCopy;
        m_done = true;
        break;
    case kIdToolSave:
        m_resultCrop = CropSelection();
        m_action = kActSave;
        m_done = true;
        break;
    case kIdToolStage:
    case kIdCanvasDblConfirm:   // 选区内双击 = 等同点「发送到对话框」
        m_resultCrop = CropSelection();
        m_action = kActStage;
        m_done = true;
        break;
    case kIdToolSend:
        m_resultCrop = CropSelection();
        m_action = kActSend;
        m_done = true;
        break;
    default:
        // 其余通知（标注工具 / 颜色 / 线宽 / 撤销重做）交画布处理。
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
    bHandled = FALSE;
    return 0;
}

bool CScreenCaptureOverlay::Run(HWND /*ownerHwnd*/, ScreenshotSendFn sendFn,
                                ScreenshotSendFn stageFn, void* ctx)
{
    m_sendFn = sendFn;
    m_stageFn = stageFn;
    m_sendCtx = ctx;
    m_action = kActNone;
    m_done = false;

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
            // balloonui 窗口类无 CS_DBLCLKS，默认收不到双击；开启 DuiHost 的双击合成，
            // 让画布能收到 OnLButtonDblClk（选区内双击 = 发送到对话框）。
            EnableDoubleClick(true);

            // 画布根控件 + 冻结背景。SetRoot 会按客户区 SetRect → 画布 Layout。
            std::unique_ptr<CaptureCanvas> canvas(new CaptureCanvas());
            m_canvas = canvas.get();
            m_canvas->SetFrozenBitmap(m_frozen);
            SetRoot(std::move(canvas));

            ::SetWindowPos(m_hWnd, HWND_TOPMOST, m_vx, m_vy, m_vw, m_vh, SWP_SHOWWINDOW);
            ::SetForegroundWindow(m_hWnd);
            ::SetFocus(m_hWnd);
            created = true;
        }
    }

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

    switch (m_action)
    {
    case kActCopy:
        DoCopyToClipboard();
        break;
    case kActSave:
        DoSaveToFile();
        break;
    case kActSend:
    case kActStage:
        DoSendToChat();
        break;
    case kActNone:
    default:
        break;
    }
    return m_action == kActSend || m_action == kActStage;
}

} // namespace balloonscreenshot
