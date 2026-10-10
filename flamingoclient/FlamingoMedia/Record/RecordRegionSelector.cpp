/**
 *  独立录屏的框选遮罩，说明见 RecordRegionSelector.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "RecordRegionSelector.h"

#include <windowsx.h>
#include <string.h>

#include "RecordMessages.h"
#include "RecordPolicy.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenrecord {

namespace {

//遮罩的不透明度：选区外（半透明黑）与选区内（1/255，肉眼看不见、仍能接住鼠标）
const BYTE kDimAlpha = 110;
const BYTE kClearAlpha = 1;
//选区边框：宽度（像素）与颜色（品牌蓝）
const int kBorderPx = 2;
const BYTE kBorderR = 45;
const BYTE kBorderG = 108;
const BYTE kBorderB = 223;
//标签（尺寸、提示）的底色、内边距、距选区的距离与字号
const BYTE kLabelBg = 40;
const int kLabelPadX = 8;
const int kLabelPadY = 4;
const int kLabelGap = 6;
const int kLabelFontPx = 18;
//提示条距显示器顶部的距离
const int kHintTop = 40;
//32 位 DIB 的位深与每像素字节数
const WORD kBitsPerPixel32 = 32;
const int kBgra = 4;
//字体名：当前界面语言的界面字体（多语言第 3e 步，DLL 经宿主接口取得）
const wchar_t* FontFace()
{
    return i18n::UiFontName();
}

//把 DIB 里一块矩形的像素设成同一个预乘颜色
void fillRect(uint8_t* bits, int w, int h, const RECT& rc, BYTE r, BYTE g, BYTE b, BYTE a)
{
    const int x0 = rc.left < 0 ? 0 : rc.left;
    const int y0 = rc.top < 0 ? 0 : rc.top;
    const int x1 = rc.right > w ? w : rc.right;
    const int y1 = rc.bottom > h ? h : rc.bottom;
    for (int y = y0; y < y1; ++y)
    {
        uint8_t* p = bits + (static_cast<size_t>(y) * w + x0) * kBgra;
        for (int x = x0; x < x1; ++x, p += kBgra)
        {
            p[0] = static_cast<BYTE>(b * a / 255);
            p[1] = static_cast<BYTE>(g * a / 255);
            p[2] = static_cast<BYTE>(r * a / 255);
            p[3] = a;
        }
    }
}

//在 DIB 上画一块不透明的深色标签，文字白色；GDI 写过的像素透明度为 0，画完把整块的透明度补成不透明
void drawLabel(HDC dc, uint8_t* bits, int w, int h, int x, int y, const wchar_t* text, HFONT font)
{
    HGDIOBJ oldFont = ::SelectObject(dc, font);
    RECT tr = { 0, 0, 0, 0 };
    ::DrawTextW(dc, text, -1, &tr, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    RECT box = { x, y, x + (tr.right - tr.left) + 2 * kLabelPadX, y + (tr.bottom - tr.top) + 2 * kLabelPadY };
    if (box.right > w)
        ::OffsetRect(&box, w - box.right, 0);
    if (box.bottom > h)
        ::OffsetRect(&box, 0, h - box.bottom);
    if (box.left < 0)
        ::OffsetRect(&box, -box.left, 0);
    if (box.top < 0)
        ::OffsetRect(&box, 0, -box.top);
    fillRect(bits, w, h, box, kLabelBg, kLabelBg, kLabelBg, 255);
    ::GdiFlush();
    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, RGB(255, 255, 255));
    ::DrawTextW(dc, text, -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ::GdiFlush();
    const int x0 = box.left < 0 ? 0 : box.left;
    const int x1 = box.right > w ? w : box.right;
    const int y0 = box.top < 0 ? 0 : box.top;
    const int y1 = box.bottom > h ? h : box.bottom;
    for (int yy = y0; yy < y1; ++yy)
    {
        uint8_t* p = bits + (static_cast<size_t>(yy) * w + x0) * kBgra;
        for (int xx = x0; xx < x1; ++xx, p += kBgra)
            p[3] = 255;
    }
    ::SelectObject(dc, oldFont);
}

} // namespace

CRecordRegionSelector::CRecordRegionSelector()
    : m_notifyWnd(NULL)
    , m_dragging(false)
    , m_finished(true)
{
    memset(&m_monitor, 0, sizeof(m_monitor));
    memset(&m_selection, 0, sizeof(m_selection));
    m_anchor.x = 0;
    m_anchor.y = 0;
}

CRecordRegionSelector::~CRecordRegionSelector()
{
    Close();
}

bool CRecordRegionSelector::Open(const RECT& monitor, HWND notifyWnd)
{
    Close();
    m_monitor = monitor;
    m_notifyWnd = notifyWnd;
    memset(&m_selection, 0, sizeof(m_selection));
    m_dragging = false;
    m_finished = false;
    const DWORD ex = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    if (Create(NULL, CWindow::rcDefault, _T("FlamingoRecordRegion"), WS_POPUP, ex) == NULL)
    {
        LOG_ERROR("record: create region selector failed.");
        m_finished = true;
        return false;
    }
    ::SetWindowPos(m_hWnd, HWND_TOPMOST, monitor.left, monitor.top, monitor.right - monitor.left,
                   monitor.bottom - monitor.top, SWP_SHOWWINDOW);
    render();
    ::SetForegroundWindow(m_hWnd);
    SetFocus();
    return true;
}

void CRecordRegionSelector::Close()
{
    m_finished = true;
    if (::GetCapture() == m_hWnd && m_hWnd != NULL)
        ::ReleaseCapture();
    if (IsWindow())
        DestroyWindow();
}

POINT CRecordRegionSelector::toScreen(LPARAM lParam) const
{
    POINT p = { GET_X_LPARAM(lParam) + m_monitor.left, GET_Y_LPARAM(lParam) + m_monitor.top };
    return p;
}

void CRecordRegionSelector::render()
{
    if (!IsWindow())
        return;
    const int w = m_monitor.right - m_monitor.left;
    const int h = m_monitor.bottom - m_monitor.top;
    if (w <= 0 || h <= 0)
        return;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = kBitsPerPixel32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* raw = NULL;
    HDC screen = ::GetDC(NULL);
    HBITMAP dib = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &raw, NULL, 0);
    if (dib == NULL)
    {
        ::ReleaseDC(NULL, screen);
        return;
    }
    uint8_t* bits = static_cast<uint8_t*>(raw);
    HDC mem = ::CreateCompatibleDC(screen);
    HGDIOBJ oldBmp = ::SelectObject(mem, dib);
    HFONT font = ::CreateFontW(-kLabelFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, FontFace());

    const RECT all = { 0, 0, w, h };
    fillRect(bits, w, h, all, 0, 0, 0, kDimAlpha);
    const bool hasSel = (m_selection.right > m_selection.left && m_selection.bottom > m_selection.top);
    if (hasSel)
    {
        RECT sel = m_selection;
        ::OffsetRect(&sel, -m_monitor.left, -m_monitor.top);
        fillRect(bits, w, h, sel, 0, 0, 0, kClearAlpha);
        //边框画在选区外侧，不挡住要录的内容
        RECT top = { sel.left - kBorderPx, sel.top - kBorderPx, sel.right + kBorderPx, sel.top };
        RECT bottom = { sel.left - kBorderPx, sel.bottom, sel.right + kBorderPx, sel.bottom + kBorderPx };
        RECT left = { sel.left - kBorderPx, sel.top, sel.left, sel.bottom };
        RECT right = { sel.right, sel.top, sel.right + kBorderPx, sel.bottom };
        fillRect(bits, w, h, top, kBorderR, kBorderG, kBorderB, 255);
        fillRect(bits, w, h, bottom, kBorderR, kBorderG, kBorderB, 255);
        fillRect(bits, w, h, left, kBorderR, kBorderG, kBorderB, 255);
        fillRect(bits, w, h, right, kBorderR, kBorderG, kBorderB, 255);
        CString size;
        size.Format(TR(L"%d × %d   回车确认，Esc 取消"), static_cast<int>(m_selection.right - m_selection.left),
                    static_cast<int>(m_selection.bottom - m_selection.top));
        //尺寸标签放在选区上方；上方放不下时放进选区左上角
        int ly = sel.top - kLabelGap - (kLabelFontPx + 2 * kLabelPadY) - kBorderPx;
        if (ly < 0)
            ly = sel.top + kLabelGap;
        drawLabel(mem, bits, w, h, sel.left, ly, size.GetString(), font);
    }
    else
    {
        const wchar_t* hint = TR(L"按住鼠标左键拖出要录制的区域；回车或双击确认，Esc 或右键取消");
        drawLabel(mem, bits, w, h, w / 3, kHintTop, hint, font);
    }
    ::GdiFlush();

    POINT dst = { m_monitor.left, m_monitor.top };
    SIZE size = { w, h };
    POINT src = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    ::UpdateLayeredWindow(m_hWnd, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);

    ::DeleteObject(font);
    ::SelectObject(mem, oldBmp);
    ::DeleteDC(mem);
    ::DeleteObject(dib);
    ::ReleaseDC(NULL, screen);
}

void CRecordRegionSelector::finish(bool ok)
{
    if (m_finished)
        return;
    m_finished = true;
    RECT* result = NULL;
    if (ok && IsSelectionUsable(m_selection))
        result = new RECT(m_selection);
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
    {
        if (!::PostMessage(m_notifyWnd, kMsgRecordRegionResult, result != NULL ? 1 : 0,
                           reinterpret_cast<LPARAM>(result)))
            delete result;
    }
    else
    {
        delete result;
    }
    Close();
}

LRESULT CRecordRegionSelector::OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)bHandled;
    m_anchor = toScreen(lParam);
    m_dragging = true;
    SetCapture();
    memset(&m_selection, 0, sizeof(m_selection));
    render();
    return 0;
}

LRESULT CRecordRegionSelector::OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)bHandled;
    if (!m_dragging)
        return 0;
    m_selection = NormalizeSelection(m_anchor, toScreen(lParam), m_monitor);
    render();
    return 0;
}

LRESULT CRecordRegionSelector::OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)bHandled;
    if (!m_dragging)
        return 0;
    m_dragging = false;
    if (::GetCapture() == m_hWnd)
        ::ReleaseCapture();
    m_selection = NormalizeSelection(m_anchor, toScreen(lParam), m_monitor);
    //太小的选区当作没选，等用户重新拖
    if (!IsSelectionUsable(m_selection))
        memset(&m_selection, 0, sizeof(m_selection));
    render();
    return 0;
}

LRESULT CRecordRegionSelector::OnLButtonDblClk(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)bHandled;
    const POINT p = toScreen(lParam);
    if (IsSelectionUsable(m_selection) && ::PtInRect(&m_selection, p))
        finish(true);
    return 0;
}

LRESULT CRecordRegionSelector::OnRButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    finish(false);
    return 0;
}

LRESULT CRecordRegionSelector::OnKeyDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    (void)bHandled;
    //回车：有可用的选区才确认
    if (wParam == VK_RETURN)
    {
        if (IsSelectionUsable(m_selection))
            finish(true);
        return 0;
    }
    //Esc：取消
    if (wParam == VK_ESCAPE)
        finish(false);
    return 0;
}

LRESULT CRecordRegionSelector::OnSetCursor(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    ::SetCursor(::LoadCursor(NULL, IDC_CROSS));
    return TRUE;
}

} // namespace screenrecord
