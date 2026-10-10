/**
 *  HotkeyCaptureBox.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-05
 */
#include "stdafx.h"
#include "HotkeyCaptureBox.h"

#include <gdiplus.h>
#include "../../Feature/Screenshot/ScreenshotHotkey.h"   // ComposeHotkey / FormatHotkey / IsModifierVk
#include "../../Common/I18n/I18n.h"                       // TR、UiFontName
#include "../../Common/Log/IULog.h"

#pragma comment(lib, "gdiplus.lib")

namespace balloonsettings {

namespace {

// ---- 外观（与 BalloonSystemSettingDlg.cpp 的只读键帽 KeyCapChip 一致）----
const int      kRadius        = 6;                          // 圆角半径（像素）
const int      kFontPx        = 12;                         // 文字字号（像素）
const COLORREF kBgNormal      = RGB(0xF3, 0xF3, 0xF6);      // 平时：浅灰底
const COLORREF kBorderNormal  = RGB(0xDD, 0xDD, 0xE3);      // 平时：细描边
const COLORREF kBgCapture     = RGB(0xFF, 0xFF, 0xFF);      // 录入中：白底
const COLORREF kBorderCapture = RGB(0xF5, 0x6A, 0x3C);      // 录入中：品牌橙描边（与设置页开关打开态同色）
const COLORREF kTextKeys      = RGB(0x55, 0x55, 0x5C);      // 组合文字：深灰
const COLORREF kTextHint      = RGB(0x9A, 0x9A, 0xA0);      // 提示文字与「未设置」：浅灰
const COLORREF kBorderHover   = RGB(0xC8, 0xC8, 0xD0);      // 平时悬停：描边加深，提示可点击
const float    kBorderWidth   = 1.0f;                       // 平时描边宽度（像素）
const float    kCaptureBorderWidth = 1.5f;                  // 录入中描边宽度（像素）
const TCHAR* const kKeyFont   = _T("Consolas");             // 组合文字用的等宽字体

// GetKeyState 返回值里表示「该键正按着」的那一位
const SHORT kKeyDownMask = (SHORT)0x8000;

// COLORREF → GDI+ 颜色（不透明）
Gdiplus::Color ToGdi(COLORREF c)
{
    return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}

// 在 (x, y, w, h) 处加一条圆角矩形路径
void AddRoundRect(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r)
{
    float d = r * 2.0f;
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

} // anonymous namespace

CHotkeyCaptureBox::CHotkeyCaptureBox()
    : m_hotkey(0)
    , m_capturing(false)
{
    // 可获得 DUI 焦点：被点击时宿主把焦点交给它，按键才会派发过来
    SetTabStop(true);
}

void CHotkeyCaptureBox::SetHotkey(DWORD hotkey)
{
    m_hotkey = hotkey;
    Invalidate();
}

void CHotkeyCaptureBox::BeginCapture()
{
    if (m_capturing)
    {
        return;
    }
    m_capturing = true;
    SetFocus();
    LOG_INFO("hotkey capture box: start capturing, current=0x%08X.", (unsigned)m_hotkey);
    Invalidate();
}

void CHotkeyCaptureBox::EndCapture()
{
    m_capturing = false;
    Invalidate();
}

void CHotkeyCaptureBox::CancelCapture()
{
    if (m_capturing)
    {
        EndCapture();
    }
}

void CHotkeyCaptureBox::ReadModifierState(bool& ctrl, bool& alt, bool& shift)
{
    ctrl  = (::GetKeyState(VK_CONTROL) & kKeyDownMask) != 0;
    alt   = (::GetKeyState(VK_MENU) & kKeyDownMask) != 0;
    shift = (::GetKeyState(VK_SHIFT) & kKeyDownMask) != 0;
}

bool CHotkeyCaptureBox::HandleCaptureKey(UINT vk)
{
    // Esc：取消录入，组合不变
    if (vk == VK_ESCAPE)
    {
        EndCapture();
        NotifyParent(DUIN_HOTKEY_CAPTURE_CANCELLED);
        return true;
    }
    // Backspace / Delete：停用快捷键
    if (vk == VK_BACK || vk == VK_DELETE)
    {
        EndCapture();
        NotifyParent(balloonwjui::DUIN_VALUECHANGED, 0);
        return true;
    }
    // 只按了修饰键：还在等主键，刷新显示已按下的部分
    if (balloonscreenshot::IsModifierVk(vk))
    {
        Invalidate();
        return true;
    }
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    ReadModifierState(ctrl, alt, shift);
    DWORD hk = balloonscreenshot::ComposeHotkey(vk, ctrl, alt, shift);
    EndCapture();
    LOG_INFO("hotkey capture box: captured vk=0x%02X ctrl=%d alt=%d shift=%d -> 0x%08X.",
             vk, ctrl ? 1 : 0, alt ? 1 : 0, shift ? 1 : 0, (unsigned)hk);
    // 是否合规由设置窗用 CheckHotkey 判断，以便给出具体原因
    NotifyParent(balloonwjui::DUIN_VALUECHANGED, (LPARAM)hk);
    return true;
}

bool CHotkeyCaptureBox::OnKeyDown(UINT vk, UINT flags)
{
    if (!m_capturing)
    {
        return balloonwjui::DuiButton::OnKeyDown(vk, flags);
    }
    return HandleCaptureKey(vk);
}

bool CHotkeyCaptureBox::OnChar(TCHAR ch)
{
    if (m_capturing)
    {
        return true;
    }
    return balloonwjui::DuiButton::OnChar(ch);
}

bool CHotkeyCaptureBox::OnRawMessage(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& lResult)
{
    if (!m_capturing)
    {
        return balloonwjui::DuiButton::OnRawMessage(uMsg, wParam, lParam, lResult);
    }
    switch (uMsg)
    {
    //带 Alt 的按键（以及 F10）：与普通按键同样处理
    case WM_SYSKEYDOWN:
        lResult = 0;
        return HandleCaptureKey((UINT)wParam);
    //修饰键抬起：刷新已按下的部分；吞掉 WM_SYSKEYUP，免得松开 Alt 时窗口进入菜单模式
    case WM_KEYUP:
    case WM_SYSKEYUP:
        Invalidate();
        lResult = 0;
        return true;
    //吞掉 Alt 加字母产生的系统字符，避免系统提示音
    case WM_SYSCHAR:
        lResult = 0;
        return true;
    //其余消息（输入法等）照常交回系统
    default:
        return false;
    }
}

bool CHotkeyCaptureBox::OnKillFocus()
{
    if (m_capturing)
    {
        EndCapture();
        LOG_INFO("hotkey capture box: lost focus, capture cancelled.");
        NotifyParent(DUIN_HOTKEY_CAPTURE_CANCELLED);
    }
    return balloonwjui::DuiButton::OnKillFocus();
}

CString CHotkeyCaptureBox::DisplayText() const
{
    if (m_capturing)
    {
        bool ctrl = false;
        bool alt = false;
        bool shift = false;
        ReadModifierState(ctrl, alt, shift);
        if (!ctrl && !alt && !shift)
        {
            return CString(TR(L"请按下快捷键"));
        }
        // 已按下修饰键：用一个占位主键格式化出前缀，再把主键换成省略号，如 "Ctrl + Alt + …"
        const UINT kPlaceholderVk = 'A';
        CString prefix = balloonscreenshot::FormatHotkey(
            balloonscreenshot::ComposeHotkey(kPlaceholderVk, ctrl, alt, shift), balloonscreenshot::kHotkeyTextKeyCap);
        prefix.Delete(prefix.GetLength() - 1, 1);
        prefix += L"…";
        return prefix;
    }
    if (m_hotkey == 0)
    {
        return CString(TR(L"未设置"));
    }
    return balloonscreenshot::FormatHotkey(m_hotkey, balloonscreenshot::kHotkeyTextKeyCap);
}

void CHotkeyCaptureBox::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    const int x = m_rcItem.left;
    const int y = m_rcItem.top;
    const int w = m_rcItem.right - m_rcItem.left;
    const int h = m_rcItem.bottom - m_rcItem.top;
    if (w <= 0 || h <= 0)
    {
        return;
    }

    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        // 内缩半像素，让描边完整落在控件矩形内
        Gdiplus::GraphicsPath path;
        AddRoundRect(path, (float)x + 0.5f, (float)y + 0.5f, (float)w - 1.0f, (float)h - 1.0f, (float)kRadius);
        Gdiplus::SolidBrush bg(ToGdi(m_capturing ? kBgCapture : kBgNormal));
        g.FillPath(&bg, &path);
        COLORREF border = m_capturing ? kBorderCapture : (IsHover() ? kBorderHover : kBorderNormal);
        Gdiplus::Pen pen(ToGdi(border), m_capturing ? kCaptureBorderWidth : kBorderWidth);
        g.DrawPath(&pen, &path);
    }

    // 组合用等宽字体；提示文字与「未设置」用界面字体（等宽字体没有中文字形）
    bool showKeys = (!m_capturing && m_hotkey != 0);
    HFONT font = ::CreateFont(-kFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              showKeys ? (FIXED_PITCH | FF_MODERN) : (DEFAULT_PITCH | FF_SWISS),
                              showKeys ? kKeyFont : i18n::UiFontName());
    HFONT oldFont = (HFONT)::SelectObject(hdc, font);
    int oldBk = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldClr = ::SetTextColor(hdc, showKeys ? kTextKeys : kTextHint);
    RECT rc = m_rcItem;
    CString text = DisplayText();
    ::DrawText(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    ::SetTextColor(hdc, oldClr);
    ::SetBkMode(hdc, oldBk);
    ::SelectObject(hdc, oldFont);
    ::DeleteObject(font);
}

} // namespace balloonsettings
