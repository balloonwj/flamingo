/**
 *  RegisterLayout.cpp
 *
 *  注册对话框客户区控件树的构造实现。注册窗分三步，每步的客户区不同 ——
 *  本文件为三步各备一段内嵌 XML（随 exe 出货、0 部署步骤），交给
 *  balloonui 的 DuiXmlBuilder 解析。
 *
 *  本文件还就地实现注册窗自用的几个小控件：
 *    · RegRootPanel    —— 透明底根面板（窗口皮肤的橙色 banner 透过它）
 *    · RegBannerPanel  —— 顶部橙色渐变 banner（火烈鸟线条 + 品牌字）
 *    · RegBackButton   —— 圆形返回按钮（白底 + 阴影 + 左箭头）
 *    · RegText         —— 单行文本（副标题 / 标签 / 协议文字）
 *    · RegStepper      —— 三步步骤指示器
 *    · RegInput        —— 圆角输入框（圆角底 + 左图标，内嵌一个 DuiEdit 子控件）
 *    · RegActionButton —— 底部主 / 次按钮
 *    · RegLinkButton   —— "获取验证码"文字按钮
 *    · RegSuccessIcon  —— 步骤 3 的圆形成功对勾图标
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-05-22
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（等价 NewChatDemo/stdafx.h）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0200
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <stdlib.h>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "RegisterLayout.h"
#include "RegisterModel.h"
#include "RegisterIcons.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"   // 服务协议 / 隐私政策超链接（ModeLink）
#include "../../../third-party/balloonui/balloonui/DuiXmlBuilder.h"

namespace balloonregister {

using balloonwjui::DuiControl;
using balloonwjui::DuiXmlBuilder;

namespace {

// =====================================================================
// 设计稿数值常量（design/_decoded/auth.jsx 与 components.jsx）
// =====================================================================

// ---- 主题色板（design 的 CSS 变量，RGB）-----------------------------
const Gdiplus::Color kClrAccent      (255, 255, 122,  69);   // accent #FF7A45
const Gdiplus::Color kClrAccentHover (255, 255, 106,  48);   // accent-hover #FF6A30
const Gdiplus::Color kClrAccentSoft  (255, 255, 230, 214);   // accent-soft #FFE6D6
const Gdiplus::Color kClrBannerTop   (255, 255, 183, 147);   // banner 渐变起点 #FFB793
const Gdiplus::Color kClrWhite       (255, 255, 255, 255);   // 纯白

const COLORREF kClrText      = RGB(26, 26, 26);     // text #1A1A1A
const COLORREF kClrTextMuted = RGB(107, 107, 107);  // text-muted #6B6B6B
const COLORREF kClrTextFaint = RGB(154, 154, 154);  // text-faint #9A9A9A
const COLORREF kClrAccentRef = RGB(255, 122, 69);   // accent（COLORREF 形态）
const COLORREF kClrInputBg   = RGB(245, 244, 241);  // 输入框底色 #F5F4F1
const COLORREF kClrBorder    = RGB(237, 237, 237);  // border
const COLORREF kClrBorderStrong = RGB(224, 224, 224);  // border-strong
const COLORREF kClrStepIdle  = RGB(238, 238, 238);  // 未达步圆点底 #EEE
const COLORREF kClrWhiteRef  = RGB(255, 255, 255);  // 纯白（COLORREF 形态）

// ---- banner 几何（auth.jsx AuthShell）-------------------------------
const int           kBannerHeight    = 160;    // 完整橙色 banner 高度（从窗口顶算）
// 标题栏高度 —— 标题栏透明，橙色 banner 经皮肤底图透出其下。
const int           kRegTitleBarH    = 32;
// 橙色 banner 在客户区内的可见高度（完整 banner 160 − 标题栏 32）。
const int           kBannerVisibleH  = kBannerHeight - kRegTitleBarH;  // 128
// banner 装饰层（RegBannerPanel）的控件高度。它需比橙色可见高度更高 ——
// design 里返回行 marginTop -32 跨越 banner 与表单区的边界，故把返回行
// 整体纳入本控件矩形（多出的 kBackRowOverhang 段落在 banner 下方的白色
// 区上）。这样返回按钮由本控件自己绘制，不会被相邻表单区覆盖。
const int           kBackRowHeight   = 48;     // 返回行高度
const int           kBackRowOverhang = 32;     // 返回行越过 banner 下沿的悬挑量
const int           kBannerPanelH    = kBannerVisibleH + kBackRowOverhang;  // 160
// 三段内嵌 XML 里 reg-banner 的 fixedHeight 必须与 kBannerPanelH 一致 ——
// 此处编译期校验，防止两边数值漂移。
static_assert(kBannerPanelH == 160,
              "reg-banner XML fixedHeight 与 kBannerPanelH 不一致");
const Gdiplus::REAL kFlamingoSize    = 180.0f; // 火烈鸟线条画外接方块边长
const Gdiplus::REAL kFlamingoStroke  = 3.0f;   // 火烈鸟描边宽度
const int           kBrandTextLeft   = 24;     // 品牌字距 banner 左
const int           kBrandTextBottom = 18;     // 品牌字距 banner 下
const int           kBrandTitleSize  = 22;     // "FlamingoIM" 字号
const int           kBrandSubSize    = 12;     // "让沟通像火烈鸟一样优雅" 字号（11.5 取整）
const int           kBrandGap        = 2;      // 品牌主副标题间距

// ---- 内容区（auth.jsx AuthShell 的 padding:'22px 28px 24px'）--------
const int           kContentPadX     = 28;     // 内容区左右内边距

// ---- 返回行（auth.jsx RegisterScreen 顶部）--------------------------
const int           kBackBtnSize     = 36;     // 圆形返回按钮直径
const int           kBackTitleSize   = 15;     // "注册新账号" 字号

// ---- 步骤指示器（auth.jsx stepper）---------------------------------
const int           kStepDotSize     = 22;     // 步骤圆点直径
const int           kStepDotTextSize = 12;     // 圆点内序号字号（11.5 取整）
const int           kStepLabelSize   = 12;     // 圆点右侧标签字号
const int           kStepLineHeight  = 1;      // 步骤连接线高度
const int           kStepGap         = 8;      // 步骤内部 / 步骤间间距

// ---- 输入框（components.jsx Input）---------------------------------
const int           kInputHeight     = 40;     // 输入框高度
const int           kInputPadX       = 12;     // 输入框左右内边距
const Gdiplus::REAL kInputRadius     = 10.0f;  // 输入框圆角
const int           kInputIconSize   = 16;     // 输入框左侧图标尺寸
const int           kInputTextSize   = 14;     // 输入文字字号（13.5 取整）
const int           kInputIconGap    = 8;      // 图标与文字间距

// ---- 服务协议 / 隐私政策一行 ---------------------------------------
// 说明文字与两条超链接同字号，这样一行读起来是连贯的一句话。
const int           kAgreementTextSize = 12;    // 该行文字字号（像素）
// 超链接三态颜色。normal 用主题 accent（与"提交注册"按钮同色，一眼看出可点）；
// hover 略加深给出反馈；visited 刻意与 normal 相同 —— 服务协议 / 隐私政策不是
// "读过就不用再读"的东西，用 DuiLabel 默认的紫色反而显脏。
const COLORREF      kClrLinkNormal   = RGB(255, 122,  69);  // #FF7A45，主题 accent
const COLORREF      kClrLinkHover    = RGB(230,  98,  48);  // accent 加深
const COLORREF      kClrLinkVisited  = kClrLinkNormal;

// ---- 按钮（components.jsx Btn）-------------------------------------
const int           kBtnHeightLg     = 42;     // lg 尺寸按钮高度
const Gdiplus::REAL kBtnRadiusLg     = 12.0f;  // lg 尺寸按钮圆角
const int           kBtnTextSizeLg   = 15;     // lg 尺寸按钮字号（14.5 取整）

// ---- 成功图标（auth.jsx step 3）------------------------------------
const int           kSuccessIconBox  = 64;     // 圆形成功图标直径
const Gdiplus::REAL kSuccessCheckPad = 16.0f;  // 对勾在圆内的内边距
const Gdiplus::REAL kSuccessCheckW   = 3.4f;   // 对勾描边宽度（粗 stroke）

// 步骤标签文字。design 原稿是 ['填写资料','手机验证','完成'] 三步，
// 现在注册不再收短信验证码，中间那步整个去掉，手机号并入第一步。
const wchar_t* const kStepLabels[kRegStepCount] =
{
    L"填写资料",
    L"完成",
};

// =====================================================================
// 公共绘制小工具
// =====================================================================

// 向 path 添加一个圆角矩形。
void AddRoundRect(Gdiplus::GraphicsPath& path,
                  Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f)
    {
        r = w / 2.0f;
    }
    if (r > h / 2.0f)
    {
        r = h / 2.0f;
    }
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

// 取服务协议 / 隐私政策超链接用的字体（12px 常规字重，与相邻说明文字同字号）。
//
// balloonui 的 DuiLabel 用 HFONT 绘制，且明确规定字体由调用方持有、控件不会
// DeleteObject（见 DuiLabel::SetFont 注释）。本模块的其余文字走 DrawGdiText，
// 每次绘制现建现删，没有一份可长期持有的 HFONT，故这里单独备一份：函数内
// 静态、随进程存活，所有超链接共用，避免每个控件各持一份字体句柄。
HFONT GetRegLinkFont()
{
    static HFONT s_hLinkFont = ::CreateFontW(
        -kAgreementTextSize, 0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    return s_hLinkFont;
}

// 用 GDI 绘制单行文字。pixelHeight 为字号（像素，正值）。
void DrawGdiText(HDC hdc, LPCWSTR text, const RECT& rc,
                 int pixelHeight, bool bold, COLORREF color, UINT dtFlags)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    if (hFont == NULL)
    {
        return;
    }
    HFONT    hOldFont  = (HFONT)::SelectObject(hdc, hFont);
    int      oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
    COLORREF oldColor  = ::SetTextColor(hdc, color);
    RECT r = rc;
    ::DrawTextW(hdc, text, -1, &r, dtFlags);
    ::SetTextColor(hdc, oldColor);
    ::SetBkMode(hdc, oldBkMode);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
}

// 测量单行文字宽度（像素）。
int MeasureGdiTextWidth(HDC hdc, LPCWSTR text, int pixelHeight, bool bold)
{
    if (text == NULL || text[0] == L'\0' || pixelHeight <= 0)
    {
        return 0;
    }
    HFONT hFont = ::CreateFontW(
        -pixelHeight, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    if (hFont == NULL)
    {
        return 0;
    }
    HFONT hOldFont = (HFONT)::SelectObject(hdc, hFont);
    SIZE sz = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)::wcslen(text), &sz);
    ::SelectObject(hdc, hOldFont);
    ::DeleteObject(hFont);
    return sz.cx;
}

// =====================================================================
// 自用小控件
// =====================================================================

/**
 *  RegRootPanel —— 注册窗客户区根面板。
 *
 *  本身不绘制任何背景 —— 窗口（CBalloonRegisterWindow）用 9-grid 皮肤
 *  底图铺出"顶部 160px 橙色 banner + 其下白色"，根面板透出底图即可。
 *  仅交基类（DuiVBox）做纵向布局。
 */
class RegRootPanel : public balloonwjui::DuiVBox
{
public:
    bool OnSetCursor(POINT /*pt*/) override
    {
        return false;
    }
    // 不覆写 OnPaint —— 直接画子控件，背景由窗口皮肤底图负责。
};

/**
 *  RegBannerPanel —— 顶部橙色 banner 装饰层 +（兼）返回行容器。
 *
 *  橙色 135° 渐变本身由窗口的 9-grid 皮肤底图负责（让标题栏区域也透出
 *  橙色），本控件只在其上绘制白色火烈鸟线条画与左下角品牌主副标题，
 *  自身背景透明。对照 design/_decoded/auth.jsx 的 AuthShell banner 块。
 *
 *  本控件同时是一个 DuiVBox 容器 —— design 里返回按钮 + "注册新账号"
 *  标题压在 banner 上（marginTop -32），故把返回行作为本控件的子控件
 *  纵向排在底部（XML 里：一个 weight 弹性空白把返回行顶到 banner 下沿），
 *  使返回行得以与 banner 视觉重叠，且不需要 balloonui 支持负 margin。
 *
 *  注意 banner 的设计高度是 160（从窗口顶算），而本控件位于客户区、
 *  其顶端在窗口标题栏之下 —— 故 SetBannerTopOffset 告知标题栏高度，
 *  绘制时把 flamingo / 品牌字按"完整 160px banner"换算坐标。
 */
class RegBannerPanel : public balloonwjui::DuiVBox
{
public:
    RegBannerPanel()
        : m_topOffset(0)
    {
    }

    // 设置 banner 顶端（窗口坐标 0）到本控件顶端之间的距离 —— 即窗口
    // 标题栏高度。绘制时据此把"完整 banner"坐标平移到本控件坐标系。
    void SetBannerTopOffset(int px)
    {
        m_topOffset = px;
        Invalidate();
    }

    // 自定义布局：本控件含唯一一个子控件（返回行 hbox），把它锚定在本
    // 控件矩形底部。本控件矩形比橙色 banner 可见区高出 kBackRowOverhang，
    // 故返回行跨越橙色 / 白色边界，呈现 design 返回行"压在 banner 上"的
    // 视觉重叠效果；又因返回行始终在本控件矩形内，由本控件自己绘制，
    // 不会被相邻表单区覆盖。不走 DuiVBox 的纵向均分逻辑。
    void Layout(const RECT& rcAvail) override
    {
        m_rcItem = rcAvail;
        const std::vector<std::unique_ptr<DuiControl>>& kids = Children();
        if (kids.empty())
        {
            return;
        }
        // 返回行锚定本控件底部，左右按内容区内边距内缩。
        RECT rcRow;
        rcRow.left   = rcAvail.left + kContentPadX;
        rcRow.right  = rcAvail.right - kContentPadX;
        rcRow.bottom = rcAvail.bottom;
        rcRow.top    = rcRow.bottom - kBackRowHeight;
        kids[0]->SetRect(rcRow);
    }

    void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        const int x = m_rcItem.left;
        const int w = m_rcItem.right - m_rcItem.left;
        if (w > 0)
        {
            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

            // 完整 banner 的顶端在本控件之上 m_topOffset 像素处。
            const int bannerTop = m_rcItem.top - m_topOffset;
            // 完整 banner 高度固定 160（design AuthShell）。
            const int bannerBottom = bannerTop + kBannerHeight;

            // ---- 右上角白色火烈鸟线条画 ----
            // design：viewBox 200，绘制区 right:-20 top:10、宽高 180。
            DrawFlamingoSilhouette(g, x, bannerTop, w);

            // ---- 左下角品牌主 / 副标题 ----
            {
                // 主标题 "FlamingoIM"。
                RECT rcTitle;
                rcTitle.left   = x + kBrandTextLeft;
                rcTitle.right  = x + w;
                rcTitle.bottom = bannerBottom - kBrandTextBottom;
                rcTitle.top    = rcTitle.bottom - (kBrandTitleSize + 6);
                DrawGdiText(hdc, L"FlamingoIM", rcTitle, kBrandTitleSize, true,
                            kClrWhiteRef,
                            DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);

                // 副标题 "让沟通像火烈鸟一样优雅"（design opacity .85 ——
                // 这里用纯白近似，banner 已是浅橙底，视觉差异可忽略）。
                RECT rcSub;
                rcSub.left   = x + kBrandTextLeft;
                rcSub.right  = x + w;
                rcSub.top    = rcTitle.bottom + kBrandGap;
                rcSub.bottom = rcSub.top + (kBrandSubSize + 4);
                DrawGdiText(hdc, L"让沟通像火烈鸟一样优雅", rcSub,
                            kBrandSubSize, false, kClrWhiteRef,
                            DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            }
        }

        // banner 装饰画完后，交基类把返回行子控件（返回按钮 + 标题）
        // 画上来 —— 它们被纵向排在本控件底部，从而压在 banner 上。
        DuiVBox::OnPaint(hdc, rcDirty);
    }

    bool OnSetCursor(POINT /*pt*/) override
    {
        return false;
    }

private:
    // 在 banner 右上角绘制白色火烈鸟线条剪影。
    //   bannerX / bannerY：完整 banner 左上角；bannerW：banner 宽度。
    void DrawFlamingoSilhouette(Gdiplus::Graphics& g,
                                int bannerX, int bannerY, int bannerW)
    {
        // design：svg viewBox 200，绘制区 right:-20 top:10、宽高 180。
        // 即绘制方块左上角 = (bannerRight - 180 + 20, bannerTop + 10)。
        const Gdiplus::REAL kViewBox      = 200.0f;
        const Gdiplus::REAL kRightOverflow = 20.0f;   // svg 右溢出 banner 20px
        const Gdiplus::REAL kTopOffset     = 10.0f;   // svg 距 banner 顶 10px

        const Gdiplus::REAL boxLeft =
            (Gdiplus::REAL)(bannerX + bannerW) - kFlamingoSize + kRightOverflow;
        const Gdiplus::REAL boxTop = (Gdiplus::REAL)bannerY + kTopOffset;
        const Gdiplus::REAL scale  = kFlamingoSize / kViewBox;

        Gdiplus::GraphicsState st = g.Save();
        g.TranslateTransform(boxLeft, boxTop);
        g.ScaleTransform(scale, scale);

        // 火烈鸟身体路径 —— 取自 auth.jsx 第 31 行的 SVG path
        // "M120 30c18 0 28 12 28 28 ..."，用三次贝塞尔近似各段曲线。
        Gdiplus::GraphicsPath body;
        // M120 30 c18 0 28 12 28 28  —— 起点到颈根。
        body.AddBezier(120.0f, 30.0f, 138.0f, 30.0f, 148.0f, 42.0f,
                       148.0f, 58.0f);
        // c0 13 -7 22 -18 26  —— 头颈向下收。
        body.AddBezier(148.0f, 58.0f, 148.0f, 71.0f, 141.0f, 80.0f,
                       130.0f, 84.0f);
        // c-7 2 -11 7 -11 14  —— 颈接身体。
        body.AddBezier(130.0f, 84.0f, 123.0f, 86.0f, 119.0f, 91.0f,
                       119.0f, 98.0f);
        // v7 —— 直下一段。
        body.AddLine(119.0f, 98.0f, 119.0f, 105.0f);
        // c0 32 -14 56 -42 67  —— 身体大弧。
        body.AddBezier(119.0f, 105.0f, 119.0f, 137.0f, 105.0f, 161.0f,
                       77.0f, 172.0f);
        // c-14 6 -25 -3 -21 -17  —— 腿部回钩。
        body.AddBezier(77.0f, 172.0f, 63.0f, 178.0f, 52.0f, 169.0f,
                       56.0f, 155.0f);
        // c4 -18 18 -26 28 -33  —— 腿向上收。
        body.AddBezier(56.0f, 155.0f, 60.0f, 137.0f, 74.0f, 129.0f,
                       84.0f, 122.0f);
        // c10 -7 14 -14 14 -25  —— 身体内侧。
        body.AddBezier(84.0f, 122.0f, 94.0f, 115.0f, 98.0f, 108.0f,
                       98.0f, 97.0f);
        // v-7 —— 直上一段。
        body.AddLine(98.0f, 97.0f, 98.0f, 90.0f);
        // c-18 -4 -35 -14 -35 -35  —— 收束回喙部一侧。
        body.AddBezier(98.0f, 90.0f, 80.0f, 86.0f, 63.0f, 76.0f,
                       63.0f, 55.0f);

        Gdiplus::Pen pen(kClrWhite, kFlamingoStroke);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        g.DrawPath(&pen, &body);

        // 眼睛：design 的 <circle cx138 cy55 r2.6>。
        Gdiplus::SolidBrush eye(kClrWhite);
        g.FillEllipse(&eye, 138.0f - 2.6f, 55.0f - 2.6f, 5.2f, 5.2f);

        g.Restore(st);
    }

private:
    int m_topOffset;   // 窗口标题栏高度 —— 完整 banner 顶端到本控件顶端的距离
};

/**
 *  RegBackButton —— 圆形返回按钮。
 *
 *  白底圆形 + 柔和投影 + 居中左箭头（chevron 旋转 180°）。继承 DuiButton
 *  复用点击 / DUIN_CLICK 通知，只覆写 OnPaint 自绘。
 */
class RegBackButton : public balloonwjui::DuiButton
{
public:
    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        const int x = m_rcItem.left;
        const int y = m_rcItem.top;
        const int w = m_rcItem.right  - m_rcItem.left;
        const int h = m_rcItem.bottom - m_rcItem.top;
        if (w <= 0 || h <= 0)
        {
            return;
        }

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 圆直径取控件宽高较小者，居中。
        const int diameter = (w < h) ? w : h;
        const Gdiplus::REAL cx = (Gdiplus::REAL)(x + w / 2);
        const Gdiplus::REAL cy = (Gdiplus::REAL)(y + h / 2);
        const Gdiplus::REAL r  = (Gdiplus::REAL)diameter / 2.0f - 1.0f;

        // ---- 柔和投影（design：0 2px 8px rgba(0,0,0,.1)）----
        // 用一圈半透明黑椭圆向下偏移近似软阴影。
        {
            const int   kShadowLayers = 4;     // 阴影叠加层数
            const Gdiplus::REAL kShadowDy = 2.0f;   // 阴影下偏移
            for (int i = kShadowLayers; i >= 1; --i)
            {
                const BYTE alpha = (BYTE)(10 + i * 4);   // 由外到内渐浓
                Gdiplus::SolidBrush sh(Gdiplus::Color(alpha, 0, 0, 0));
                const Gdiplus::REAL grow = (Gdiplus::REAL)i;
                g.FillEllipse(&sh,
                              cx - r - grow,
                              cy - r - grow + kShadowDy,
                              (r + grow) * 2.0f,
                              (r + grow) * 2.0f);
            }
        }

        // ---- 白底圆 ----
        {
            Gdiplus::SolidBrush white(
                IsHover() ? Gdiplus::Color(255, 250, 250, 250) : kClrWhite);
            g.FillEllipse(&white, cx - r, cy - r, r * 2.0f, r * 2.0f);
        }

        // ---- 左箭头：chevron 绕中心旋转 180° ----
        {
            const Gdiplus::REAL kArrowSize = 18.0f;   // 箭头 viewBox 映射尺寸
            Gdiplus::GraphicsState st = g.Save();
            g.TranslateTransform(cx, cy);
            g.RotateTransform(180.0f);
            DrawRegIcon(g, kRegIconChevron,
                        -kArrowSize / 2.0f, -kArrowSize / 2.0f,
                        kArrowSize, kClrAccent, 1.8f);
            g.Restore(st);
        }
    }

    bool OnSetCursor(POINT /*pt*/) override
    {
        return false;
    }
};

/**
 *  RegText —— 单行 / 多行文本控件。
 *
 *  字号 / 颜色 / 粗细 / 对齐 / 是否换行由 XML 属性决定。用于返回行标题、
 *  注册成功文案、协议提示等。纯绘制控件。
 */
class RegText : public DuiControl
{
public:
    RegText()
        : m_size(kInputTextSize)
        , m_color(kClrText)
        , m_bold(false)
        , m_align(DT_LEFT)
        , m_wrap(false)
    {
    }

    // 配置文本内容与样式。
    //   text：文本；size：字号（像素）；color：文字色；bold：是否粗体；
    //   align：水平对齐（DT_LEFT / DT_CENTER / DT_RIGHT）；
    //   wrap：true 时允许自动换行（用于协议长文本）。
    void Configure(LPCTSTR text, int size, COLORREF color, bool bold,
                   UINT align, bool wrap)
    {
        m_text  = (text != NULL) ? text : _T("");
        m_size  = size;
        m_color = color;
        m_bold  = bold;
        m_align = align;
        m_wrap  = wrap;
    }

    // 只改文本、保留既有样式。用于注册完成页按服务端应答回填账号与公司名。
    //   text：新文本；NULL 按空串处理。
    void SetText(LPCTSTR text)
    {
        m_text = (text != NULL) ? text : _T("");
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        UINT flags = m_align | DT_NOPREFIX;
        if (m_wrap)
        {
            flags |= DT_WORDBREAK | DT_TOP;
        }
        else
        {
            flags |= DT_VCENTER | DT_SINGLELINE;
        }
        DrawGdiText(hdc, (LPCWSTR)m_text, m_rcItem, m_size, m_bold,
                    m_color, flags);
    }

    bool OnSetCursor(POINT /*pt*/) override
    {
        return false;
    }

private:
    CString  m_text;    // 文本内容
    int      m_size;    // 字号（像素）
    COLORREF m_color;   // 文字颜色
    bool     m_bold;    // 是否粗体
    UINT     m_align;   // 水平对齐
    bool     m_wrap;    // 是否自动换行
};

/**
 *  RegStepper —— 三步步骤指示器。
 *
 *  横向排布"圆点 + 标签"三组，相邻两组间一条连接线。当前步及已过步
 *  圆点为 accent 实底白字（已完成步圆点内画对勾），未到步为浅灰底
 *  灰字。对照 design/_decoded/auth.jsx 的 stepper 块。纯绘制控件。
 */
class RegStepper : public DuiControl
{
public:
    RegStepper()
        : m_currentStep(kRegStepFillInfo)
    {
    }

    // 设置当前所处步骤（取值见 RegisterStep）。会触发重绘。
    void SetCurrentStep(int step)
    {
        m_currentStep = step;
        Invalidate();
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        const int x = m_rcItem.left;
        const int y = m_rcItem.top;
        const int w = m_rcItem.right  - m_rcItem.left;
        const int h = m_rcItem.bottom - m_rcItem.top;
        if (w <= 0 || h <= 0)
        {
            return;
        }

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 先量出各标签宽度，连接线吃掉剩余空间。
        int labelWidth[kRegStepCount] = { 0 };
        for (int i = 0; i < kRegStepCount; ++i)
        {
            labelWidth[i] =
                MeasureGdiTextWidth(hdc, kStepLabels[i], kStepLabelSize, false);
        }

        // 一组（圆点 + gap + 标签）的固定宽度之和。
        int groupsWidth = 0;
        for (int i = 0; i < kRegStepCount; ++i)
        {
            groupsWidth += kStepDotSize + kStepGap + labelWidth[i];
        }
        // 各条连接线平分剩余宽度（每条还各留 kStepGap 左右间距）。
        const int kLineCount = kRegStepCount - 1;
        int lineSpace = w - groupsWidth - kStepGap * 2 * kLineCount;
        if (lineSpace < 0)
        {
            lineSpace = 0;
        }
        const int lineLen = lineSpace / kLineCount;

        const int cy = y + h / 2;   // 步骤行垂直中线
        int penX = x;               // 当前绘制游标

        for (int i = 0; i < kRegStepCount; ++i)
        {
            // i 对应步骤号 (i + 1)。当前步及之前步算"激活"。
            const int  stepNo  = i + 1;
            const bool active  = (m_currentStep >= stepNo);
            const bool done    = (m_currentStep > stepNo);

            // ---- 圆点 ----
            const int dotTop = cy - kStepDotSize / 2;
            {
                Gdiplus::Color dotBg = active
                    ? kClrAccent
                    : Gdiplus::Color(255,
                          GetRValue(kClrStepIdle),
                          GetGValue(kClrStepIdle),
                          GetBValue(kClrStepIdle));
                Gdiplus::SolidBrush brush(dotBg);
                g.FillEllipse(&brush,
                              (Gdiplus::REAL)penX, (Gdiplus::REAL)dotTop,
                              (Gdiplus::REAL)kStepDotSize,
                              (Gdiplus::REAL)kStepDotSize);
            }
            // 圆点内容：已完成步画对勾，否则画序号。
            if (done)
            {
                const Gdiplus::REAL kCheckPad = 5.0f;   // 对勾在圆内的内边距
                DrawRegIcon(g, kRegIconCheck,
                            (Gdiplus::REAL)penX + kCheckPad,
                            (Gdiplus::REAL)dotTop + kCheckPad,
                            (Gdiplus::REAL)kStepDotSize - kCheckPad * 2.0f,
                            kClrWhite, 3.0f);
            }
            else
            {
                wchar_t num[2] = { (wchar_t)(L'0' + stepNo), L'\0' };
                RECT rcNum = { penX, dotTop,
                               penX + kStepDotSize, dotTop + kStepDotSize };
                DrawGdiText(hdc, num, rcNum, kStepDotTextSize, true,
                            active ? kClrWhiteRef : kClrTextMuted,
                            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            penX += kStepDotSize + kStepGap;

            // ---- 标签 ----
            {
                RECT rcLabel = { penX, y, penX + labelWidth[i], y + h };
                DrawGdiText(hdc, kStepLabels[i], rcLabel, kStepLabelSize, false,
                            active ? kClrText : kClrTextMuted,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            penX += labelWidth[i];

            // ---- 连接线（最后一组之后不画）----
            if (i < kRegStepCount - 1)
            {
                penX += kStepGap;
                Gdiplus::SolidBrush lineBrush(Gdiplus::Color(255,
                    GetRValue(kClrBorder),
                    GetGValue(kClrBorder),
                    GetBValue(kClrBorder)));
                g.FillRectangle(&lineBrush,
                                (Gdiplus::REAL)penX,
                                (Gdiplus::REAL)(cy - kStepLineHeight / 2),
                                (Gdiplus::REAL)lineLen,
                                (Gdiplus::REAL)kStepLineHeight);
                penX += lineLen + kStepGap;
            }
        }
    }

    bool OnSetCursor(POINT /*pt*/) override
    {
        return false;
    }

private:
    int m_currentStep;   // 当前步骤，取值见 RegisterStep
};

/**
 *  RegInput —— 圆角输入框。
 *
 *  对照 design/_decoded/components.jsx 的 Input：圆角 10、底色 #F5F4F1、
 *  左侧线条图标、聚焦时边框变 accent。本控件继承 DuiHBox 当布局容器，
 *  内嵌一个 <edit>（DuiEdit）子控件承载文本输入；图标与圆角底
 *  由本控件 OnPaint 自绘，HBox 左 padding 给图标让位。
 *
 *  ctrl-id 挂在内嵌的 <edit> 上（窗口代码按 id FindCtrlById 取到的是
 *  真正可编辑的 DuiEdit）。
 */
class RegInput : public balloonwjui::DuiHBox
{
public:
    RegInput()
        : m_iconKind(kRegIconUser)
    {
    }

    // 设置左侧图标类型（取值见 RegIconKind）。会触发重绘。
    void SetIconKind(int kind)
    {
        m_iconKind = kind;
        Invalidate();
    }
    int GetIconKind() const
    {
        return m_iconKind;
    }

    void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        const int x = m_rcItem.left;
        const int y = m_rcItem.top;
        const int w = m_rcItem.right  - m_rcItem.left;
        const int h = m_rcItem.bottom - m_rcItem.top;
        if (w <= 0 || h <= 0)
        {
            return;
        }

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // ---- 圆角底 #F5F4F1（design Input 底色）----
        {
            Gdiplus::GraphicsPath bg;
            AddRoundRect(bg, (Gdiplus::REAL)x + 0.5f, (Gdiplus::REAL)y + 0.5f,
                         (Gdiplus::REAL)w - 1.0f, (Gdiplus::REAL)h - 1.0f,
                         kInputRadius);
            Gdiplus::SolidBrush fill(Gdiplus::Color(255,
                GetRValue(kClrInputBg),
                GetGValue(kClrInputBg),
                GetBValue(kClrInputBg)));
            g.FillPath(&fill, &bg);

            // 悬停时描一条 border-strong 的细边，给一点交互反馈
            //（design 聚焦才变 accent，但 DUI 容器拿不到内嵌 EDIT 的焦点，
            // 这里以 hover 作近似反馈）。
            if (IsHover())
            {
                Gdiplus::Pen border(Gdiplus::Color(255,
                    GetRValue(kClrBorderStrong),
                    GetGValue(kClrBorderStrong),
                    GetBValue(kClrBorderStrong)), 1.0f);
                g.DrawPath(&border, &bg);
            }
        }

        // ---- 左侧图标（text-faint 色）----
        {
            const Gdiplus::REAL iconX = (Gdiplus::REAL)(x + kInputPadX);
            const Gdiplus::REAL iconY =
                (Gdiplus::REAL)(y + (h - kInputIconSize) / 2);
            DrawRegIcon(g, m_iconKind, iconX, iconY,
                        (Gdiplus::REAL)kInputIconSize,
                        Gdiplus::Color(255,
                            GetRValue(kClrTextFaint),
                            GetGValue(kClrTextFaint),
                            GetBValue(kClrTextFaint)),
                        1.6f);
        }

        // 自绘背景画完后，交基类把内嵌 <edit> 子控件画上来。
        DuiHBox::OnPaint(hdc, rcDirty);
    }

private:
    int m_iconKind;   // 左侧图标类型，取值见 RegIconKind
};

/**
 *  RegActionButton —— 注册窗底部的操作按钮。
 *
 *  对照 design/_decoded/components.jsx 的 Btn（lg 尺寸）：
 *    · primary（主按钮）：accent 实底、白字、accent 描边。
 *    · ghost（次按钮）  ：透明底、text 色字、border-strong 描边。
 *  继承 DuiButton 复用点击 / DUIN_CLICK 通知，只覆写 OnPaint。
 */
class RegActionButton : public balloonwjui::DuiButton
{
public:
    RegActionButton()
        : m_primary(true)
    {
    }

    // 设置按钮风格。primary=true 为主按钮，false 为 ghost 次按钮。
    void SetPrimary(bool primary)
    {
        m_primary = primary;
        Invalidate();
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        const int x = m_rcItem.left;
        const int y = m_rcItem.top;
        const int w = m_rcItem.right  - m_rcItem.left;
        const int h = m_rcItem.bottom - m_rcItem.top;
        if (w <= 0 || h <= 0)
        {
            return;
        }

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        Gdiplus::GraphicsPath path;
        AddRoundRect(path, (Gdiplus::REAL)x + 0.5f, (Gdiplus::REAL)y + 0.5f,
                     (Gdiplus::REAL)w - 1.0f, (Gdiplus::REAL)h - 1.0f,
                     kBtnRadiusLg);

        COLORREF textColor;
        if (m_primary)
        {
            // accent 实底主按钮，hover 变深。
            Gdiplus::SolidBrush brush(
                IsHover() ? kClrAccentHover : kClrAccent);
            g.FillPath(&brush, &path);
            Gdiplus::Pen border(kClrAccent, 1.0f);
            g.DrawPath(&border, &path);
            textColor = kClrWhiteRef;
        }
        else
        {
            // ghost 次按钮：透明底（hover 时浅灰）、border-strong 描边。
            if (IsHover())
            {
                Gdiplus::SolidBrush brush(Gdiplus::Color(255, 246, 245, 242));
                g.FillPath(&brush, &path);
            }
            Gdiplus::Pen border(Gdiplus::Color(255,
                GetRValue(kClrBorderStrong),
                GetGValue(kClrBorderStrong),
                GetBValue(kClrBorderStrong)), 1.0f);
            g.DrawPath(&border, &path);
            textColor = kClrText;
        }

        DrawGdiText(hdc, (LPCWSTR)GetText(), m_rcItem, kBtnTextSizeLg, true,
                    textColor,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    bool OnSetCursor(POINT /*pt*/) override
    {
        return false;
    }

private:
    bool m_primary;   // true=主按钮，false=ghost 次按钮
};

/**
 *  RegLinkButton —— "获取验证码"文字按钮。
 *
 *  无底无边、accent 色文字，作为验证码输入框的右侧后缀。继承 DuiButton
 *  复用点击 / DUIN_CLICK 通知，只覆写 OnPaint。
 */
class RegLinkButton : public balloonwjui::DuiButton
{
public:
    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        // design：accent 色、字号 12.5（取整 13）。hover 时略加深。
        const int kLinkTextSize = 13;
        const COLORREF clr = IsHover()
            ? RGB(GetRValue(kClrAccentRef) - 16 > 0
                      ? GetRValue(kClrAccentRef) - 16 : 0,
                  GetGValue(kClrAccentRef),
                  GetBValue(kClrAccentRef))
            : kClrAccentRef;
        DrawGdiText(hdc, (LPCWSTR)GetText(), m_rcItem, kLinkTextSize, false,
                    clr,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    bool OnSetCursor(POINT /*pt*/) override
    {
        return false;
    }
};

/**
 *  RegSuccessIcon —— 步骤 3 的圆形成功图标。
 *
 *  对照 design step 3：64×64 圆、accent-soft 底、accent 色粗对勾。
 *  控件居中绘制此圆。纯绘制控件。
 */
class RegSuccessIcon : public DuiControl
{
public:
    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        const int x = m_rcItem.left;
        const int y = m_rcItem.top;
        const int w = m_rcItem.right  - m_rcItem.left;
        const int h = m_rcItem.bottom - m_rcItem.top;
        if (w <= 0 || h <= 0)
        {
            return;
        }

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 圆居中，直径 = 设计稿 64。
        const int cx = x + w / 2;
        const int cy = y + h / 2;
        const Gdiplus::REAL r = (Gdiplus::REAL)kSuccessIconBox / 2.0f;

        // accent-soft 底圆。
        {
            Gdiplus::SolidBrush soft(kClrAccentSoft);
            g.FillEllipse(&soft,
                          (Gdiplus::REAL)cx - r, (Gdiplus::REAL)cy - r,
                          r * 2.0f, r * 2.0f);
        }
        // accent 色粗对勾，居中。
        {
            const Gdiplus::REAL checkBox =
                (Gdiplus::REAL)kSuccessIconBox - kSuccessCheckPad * 2.0f;
            DrawRegIcon(g, kRegIconCheck,
                        (Gdiplus::REAL)cx - checkBox / 2.0f,
                        (Gdiplus::REAL)cy - checkBox / 2.0f,
                        checkBox, kClrAccent, kSuccessCheckW);
        }
    }

    bool OnSetCursor(POINT /*pt*/) override
    {
        return false;
    }
};

// =====================================================================
// 内嵌 XML 布局 —— 两步各一段
// =====================================================================
//
// 公共结构（每步都有）：
//   <reg-root>
//     <reg-banner/>                          高 160，banner
//     <vbox>  内容区
//       <hbox> 返回行：reg-backbtn + reg-text "注册新账号"
//       <reg-stepper/>                       步骤指示器
//       ...各步专属内容...
//     </vbox>
//   </reg-root>
//
// 注意：banner 区域客户区控件透明（露出窗口皮肤橙色）；返回行的标题
// 文字压在 banner 下沿，故内容区 padding-top 取较小值让其上探。

// ---- 步骤 1：填写资料 ----
// 五个输入框依次是 昵称 / 密码 / 确认密码 / 手机号 / 公司名称。
// 手机号原先在已被去掉的"手机验证"那一步里（连同短信验证码），现在注册不再
// 下发验证码，手机号并到这里，同时新增公司名称；登录账号则由服务端自动生成，
// 界面上没有对应的输入框。
//
// 高度核算：窗口 610 - banner 160 - 步骤指示器行 64 = 386，减去本 vbox 的
// 上下 padding(4 + 16) 后可用 366；五个输入框 200 + 协议行 34 + 按钮 42
// = 276，八个子控件之间七道 gap(10) 共 70，合计 346，余下 20 给弹性占位。
// 改动这里的任何一项尺寸都要连带核对 BalloonRegisterWindow.cpp 的
// kRegWindowHeight，否则底部按钮会被挤出可视区。
const char* const kRegBodyStep1Xml = u8R"xml(
<reg-root padding="0" gap="0">
  <reg-banner fixedHeight="160" padding="0" gap="0">
    <hbox padding="0" gap="10">
      <reg-backbtn id="800" fixedWidth="36" fixedHeight="36" alignCross="center"/>
      <reg-text text="注册新账号" size="15" color="255,255,255" bold="true" weight="1"/>
    </hbox>
  </reg-banner>
  <vbox weight="1" padding="0,0,0,0" gap="0">
    <hbox fixedHeight="64" padding="28,24,28,18" gap="0">
      <reg-stepper weight="1"/>
    </hbox>
    <vbox weight="1" padding="28,4,28,16" gap="10">
      <reg-input icon="user"    fixedHeight="40">
        <edit id="810" weight="1"/>
      </reg-input>
      <reg-input icon="lock"    fixedHeight="40">
        <edit id="812" weight="1"/>
      </reg-input>
      <reg-input icon="lock"    fixedHeight="40">
        <edit id="813" weight="1"/>
      </reg-input>
      <reg-input icon="phone"   fixedHeight="40">
        <edit id="815" weight="1"/>
      </reg-input>
      <reg-input icon="company" fixedHeight="40">
        <edit id="816" weight="1"/>
      </reg-input>
      <hbox fixedHeight="34" gap="4">
        <reg-text text="注册即表示同意" size="12" color="107,107,107" fixedWidth="88"/>
        <reg-link id="818" text="FlamingoIM 服务协议" fixedWidth="120"/>
        <reg-text text="和" size="12" color="107,107,107" fixedWidth="16"/>
        <reg-link id="819" text="隐私政策" fixedWidth="54"/>
        <control weight="1"/>
      </hbox>
      <control weight="1"/>
      <reg-actionbtn id="817" text="提交注册" primary="true" fixedHeight="42"/>
    </vbox>
  </vbox>
</reg-root>
)xml";

// ---- 步骤 2：完成 ----
// 账号与所加入的公司都由服务端应答决定，XML 里留空，注册成功后由窗口代码
// 经 SetRegisterText 按 ctrl-id 回填（早期版本这里写死过一个假账号）。
//
// 账号这一行刻意用大一号的 accent 色粗体：它是服务端自动生成的、用户没见过
// 也没法自己设，是整个页面上唯一必须记住的信息。下面紧跟一句提醒。
const char* const kRegBodyStep2Xml = u8R"xml(
<reg-root padding="0" gap="0">
  <reg-banner fixedHeight="160" padding="0" gap="0">
    <hbox padding="0" gap="10">
      <reg-backbtn id="800" fixedWidth="36" fixedHeight="36" alignCross="center"/>
      <reg-text text="注册新账号" size="15" color="255,255,255" bold="true" weight="1"/>
    </hbox>
  </reg-banner>
  <vbox weight="1" padding="0,0,0,0" gap="0">
    <hbox fixedHeight="64" padding="28,24,28,18" gap="0">
      <reg-stepper weight="1"/>
    </hbox>
    <vbox weight="1" padding="28,24,28,24" gap="0">
      <reg-success fixedHeight="64"/>
      <reg-text text="注册成功！" size="16" color="26,26,26" bold="true" align="center" fixedHeight="38"/>
      <reg-text id="831" text="" size="18" color="255,122,69" bold="true" align="center" fixedHeight="32"/>
      <reg-text text="请牢记该账号，登录时需要用到" size="13" color="107,107,107" align="center" fixedHeight="24"/>
      <reg-text id="832" text="" size="13" color="107,107,107" align="center" fixedHeight="24"/>
      <control weight="1"/>
      <reg-actionbtn id="830" text="立即登录" primary="true" fixedHeight="42"/>
    </vbox>
  </vbox>
</reg-root>
)xml";

// =====================================================================
// XML 属性解析
// =====================================================================

// 读取节点属性值；属性不存在时返回 def。
std::string GetNodeAttr(const DuiXmlBuilder::Node& n, const char* key,
                        const char* def = "")
{
    std::map<std::string, std::string>::const_iterator it = n.attrs.find(key);
    return (it != n.attrs.end()) ? it->second : std::string(def);
}

// 把 UTF-8 字节串转成 CString（宽字符）。内嵌 XML 是 u8 字面量（UTF-8），
// 属性里的中文必须走本函数 —— 不能用 CA2T：CA2T 按系统 ANSI 代码页
// （中文系统为 GBK）解释，会把 UTF-8 字节当 GBK 读，导致界面文字乱码。
CString Utf8ToCString(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    const int wlen = ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (wlen <= 0)
    {
        return CString();
    }
    CString result;
    LPWSTR buf = result.GetBuffer(wlen);
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
                          buf, wlen);
    result.ReleaseBuffer(wlen);
    return result;
}

// 把 "r,g,b" 解析成 COLORREF；解析失败返回 def。
COLORREF ParseColor(const std::string& s, COLORREF def)
{
    int rgb[3] = { 0, 0, 0 };
    int idx = 0;
    int cur = 0;
    bool hasDigit = false;
    for (size_t i = 0; i <= s.size() && idx < 3; ++i)
    {
        const char c = (i < s.size()) ? s[i] : ',';
        if (c >= '0' && c <= '9')
        {
            cur = cur * 10 + (c - '0');
            hasDigit = true;
        }
        else if (c == ',')
        {
            if (!hasDigit)
            {
                return def;
            }
            if (cur > 255)
            {
                cur = 255;
            }
            rgb[idx] = cur;
            ++idx;
            cur = 0;
            hasDigit = false;
        }
    }
    if (idx < 3)
    {
        return def;
    }
    return RGB(rgb[0], rgb[1], rgb[2]);
}

// 把 reg-text 的 align 属性翻译成 DrawText 对齐标志。
UINT TextAlignFromAttr(const std::string& align)
{
    if (align == "center")
    {
        return DT_CENTER;
    }
    if (align == "right")
    {
        return DT_RIGHT;
    }
    return DT_LEFT;
}

// 把 reg-input 的 icon 属性翻译成 RegIconKind。
int InputIconFromAttr(const std::string& icon)
{
    if (icon == "qrcode")
    {
        return kRegIconQrcode;
    }
    if (icon == "lock")
    {
        return kRegIconLock;
    }
    if (icon == "phone")
    {
        return kRegIconPhone;
    }
    if (icon == "check")
    {
        return kRegIconCheck;
    }
    if (icon == "company")
    {
        return kRegIconCompany;
    }
    return kRegIconUser;
}

// =====================================================================
// 自定义标签工厂
// =====================================================================
//
// 接 reg-root / reg-banner / reg-backbtn / reg-text / reg-stepper /
// reg-input / reg-actionbtn / reg-linkbtn / reg-success / control；
// 其余标签（vbox / hbox / edit）走 builder 内置处理。
std::unique_ptr<DuiControl> CreateRegCustomControl(const DuiXmlBuilder::Node& n)
{
    if (n.tag == "reg-root")
    {
        return std::unique_ptr<DuiControl>(new RegRootPanel());
    }

    if (n.tag == "reg-banner")
    {
        return std::unique_ptr<DuiControl>(new RegBannerPanel());
    }

    if (n.tag == "reg-backbtn")
    {
        return std::unique_ptr<DuiControl>(new RegBackButton());
    }

    if (n.tag == "reg-text")
    {
        std::unique_ptr<RegText> text(new RegText());
        const CString     strText = Utf8ToCString(GetNodeAttr(n, "text"));
        const std::string strSize = GetNodeAttr(n, "size", "14");
        const COLORREF color = ParseColor(GetNodeAttr(n, "color"), kClrText);
        const bool bold = (GetNodeAttr(n, "bold") == "true");
        const UINT align = TextAlignFromAttr(GetNodeAttr(n, "align"));
        const bool wrap = (GetNodeAttr(n, "wrap") == "true");
        text->Configure(strText, atoi(strSize.c_str()), color, bold,
                        align, wrap);
        return std::unique_ptr<DuiControl>(text.release());
    }

    if (n.tag == "reg-stepper")
    {
        return std::unique_ptr<DuiControl>(new RegStepper());
    }

    if (n.tag == "reg-input")
    {
        std::unique_ptr<RegInput> input(new RegInput());
        input->SetIconKind(InputIconFromAttr(GetNodeAttr(n, "icon")));
        // HBox 左 padding 给图标让位（图标占 kInputPadX + 图标宽 + 间距），
        // 右侧也留 kInputPadX。builder 随后把 <edit> 子控件 AddChild 进来。
        input->SetPadding(kInputPadX + kInputIconSize + kInputIconGap,
                          0, kInputPadX, 0);
        input->SetGap(kInputIconGap);
        return std::unique_ptr<DuiControl>(input.release());
    }

    if (n.tag == "reg-actionbtn")
    {
        std::unique_ptr<RegActionButton> btn(new RegActionButton());
        btn->SetText(Utf8ToCString(GetNodeAttr(n, "text")));
        btn->SetPrimary(GetNodeAttr(n, "primary") != "false");
        return std::unique_ptr<DuiControl>(btn.release());
    }

    if (n.tag == "reg-link")
    {
        // 服务协议 / 隐私政策超链接。直接用 balloonui 的 DuiLabel 链接模式 ——
        // 下划线、三态颜色、hover 手型光标、点击发 DUIN_CLICK 都是它自带的，
        // 无需在本模块另画一个链接控件。
        std::unique_ptr<balloonwjui::DuiLabel> link(new balloonwjui::DuiLabel());
        link->SetMode(balloonwjui::DuiLabel::ModeLink);
        link->SetText(Utf8ToCString(GetNodeAttr(n, "text")));
        link->SetFont(GetRegLinkFont());
        link->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        link->SetLinkColor(kClrLinkNormal);
        link->SetHoverColor(kClrLinkHover);
        link->SetVisitedColor(kClrLinkVisited);
        // 点击语义由注册窗自己决定（协议页面尚未接入），不让控件去 ShellExecute。
        link->SetAutoNavigate(false);
        return std::unique_ptr<DuiControl>(link.release());
    }

    if (n.tag == "reg-linkbtn")
    {
        std::unique_ptr<RegLinkButton> btn(new RegLinkButton());
        btn->SetText(Utf8ToCString(GetNodeAttr(n, "text")));
        return std::unique_ptr<DuiControl>(btn.release());
    }

    if (n.tag == "reg-success")
    {
        return std::unique_ptr<DuiControl>(new RegSuccessIcon());
    }

    if (n.tag == "control")
    {
        return std::unique_ptr<DuiControl>(new DuiControl());
    }

    return nullptr;
}

// 按步骤号选取对应的内嵌 XML。
const char* RegBodyXmlForStep(int step)
{
    if (step == kRegStepDone)
    {
        return kRegBodyStep2Xml;
    }
    return kRegBodyStep1Xml;
}

// 递归遍历控件子树做构建后配置：
//   · RegStepper      —— 设为当前步（步骤指示器无 ctrl-id，按类型定位）
//   · RegBannerPanel  —— 告知标题栏高度，使其按"完整 160px banner"换算
//                        flamingo / 品牌字坐标
// 这两类控件窗口代码都不需要单独操作，故由布局层在构建完成后一次性
// 配置，无需占用 ctrl-id。
void ConfigureBuiltTree(DuiControl* node, int step)
{
    if (node == NULL)
    {
        return;
    }
    RegStepper* stepper = dynamic_cast<RegStepper*>(node);
    if (stepper != NULL)
    {
        stepper->SetCurrentStep(step);
    }
    RegBannerPanel* banner = dynamic_cast<RegBannerPanel*>(node);
    if (banner != NULL)
    {
        banner->SetBannerTopOffset(kRegTitleBarH);
    }
    const std::vector<std::unique_ptr<DuiControl>>& kids = node->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ConfigureBuiltTree(kids[i].get(), step);
    }
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildRegisterBody(int step)
{
    // 步骤号越界回退到步骤 1，保证 stepper / XML 都拿到合法值。
    int safeStep = step;
    if (safeStep < kRegStepFillInfo || safeStep > kRegStepCount)
    {
        safeStep = kRegStepFillInfo;
    }

    std::unique_ptr<DuiControl> root = DuiXmlBuilder::FromString(
        RegBodyXmlForStep(safeStep), &CreateRegCustomControl);

    // 构建后配置：步骤指示器高亮进度、banner 装饰层的标题栏偏移。
    ConfigureBuiltTree(root.get(), safeStep);
    return root;
}

bool SetRegisterText(DuiControl* root, int ctrlId, LPCTSTR text)
{
    if (root == NULL)
    {
        return false;
    }

    RegText* label = dynamic_cast<RegText*>(root->FindCtrlById(ctrlId));
    if (label == NULL)
    {
        return false;
    }

    label->SetText(text);
    return true;
}

} // namespace balloonregister
