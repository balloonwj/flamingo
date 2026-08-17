/**
 *  NetSettingsLayout.cpp
 *
 *  网络设置窗口客户区控件树的构造实现。布局以内嵌 XML 常量为唯一真值
 *  源（随 exe 出货、0 部署步骤），交给 balloonui 的 DuiXmlBuilder 解析。
 *  本文件还就地实现三个仅布局自用的小控件：白底根面板、单行文本、
 *  主 / 次操作按钮。
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
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
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
#include <stdlib.h>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "NetSettingsLayout.h"
#include "NetSettingsModel.h"
#include "NetServerCard.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/DuiXmlBuilder.h"

namespace balloonnetsettings {

using balloonwjui::DuiControl;
using balloonwjui::DuiXmlBuilder;

namespace {

// =====================================================================
// 公共绘制小工具
// =====================================================================

// 向 path 添加一个圆角矩形。
void AddRoundRect(Gdiplus::GraphicsPath& path,
                  Gdiplus::REAL x, Gdiplus::REAL y,
                  Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius)
{
    Gdiplus::REAL r = radius;
    if (r > w / 2.0f) { r = w / 2.0f; }
    if (r > h / 2.0f) { r = h / 2.0f; }
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f,   90.0f);
    path.AddArc(x,         y + h - d, d, d, 90.0f,  90.0f);
    path.CloseFigure();
}

// 用 GDI 绘制单行文字。
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
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
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

// =====================================================================
// 布局自用小控件
// =====================================================================

/**
 *  NetSettingsRootPanel —— 网络设置窗口客户区根面板。铺纯白底，再交
 *  基类（DuiVBox）纵向排布子控件。
 */
class NetSettingsRootPanel : public balloonwjui::DuiVBox
{
public:
    void OnPaint(HDC hdc, const RECT& rcDirty) override
    {
        HBRUSH white = ::CreateSolidBrush(RGB(255, 255, 255));
        ::FillRect(hdc, &m_rcItem, white);
        ::DeleteObject(white);
        DuiVBox::OnPaint(hdc, rcDirty);
    }
};

/**
 *  NetText —— 单行文本控件。字号 / 颜色 / 粗细 / 对齐由 XML 属性决定，
 *  用于副标题与表单标签。纯绘制控件。
 */
class NetText : public balloonwjui::DuiControl
{
public:
    NetText()
        : m_size(11)
        , m_color(RGB(0x2A, 0x2A, 0x2A))
        , m_bold(false)
        , m_align(DT_LEFT)
    {
    }

    // 配置文本内容与样式。
    void Configure(LPCTSTR text, int size, COLORREF color, bool bold, UINT align)
    {
        m_text  = (text != NULL) ? text : _T("");
        m_size  = size;
        m_color = color;
        m_bold  = bold;
        m_align = align;
    }

    void OnPaint(HDC hdc, const RECT& /*rcDirty*/) override
    {
        DrawGdiText(hdc, (LPCWSTR)m_text, m_rcItem, m_size, m_bold, m_color,
                    m_align | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    CString  m_text;    // 文本内容
    int      m_size;    // 字号（像素）
    COLORREF m_color;   // 文字颜色
    bool     m_bold;    // 是否粗体
    UINT     m_align;   // 水平对齐（DT_LEFT / DT_CENTER / DT_RIGHT）
};

/**
 *  NetActionButton —— 网络设置窗口底部的操作按钮。primary=true 为橙色
 *  实心主按钮（"保存"），false 为白底描边次按钮（"测试连接"）。继承
 *  DuiButton 复用点击 / 通知，只覆写 OnPaint。
 */
class NetActionButton : public balloonwjui::DuiButton
{
public:
    NetActionButton() : m_primary(false) {}

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
                     (Gdiplus::REAL)w - 1.0f, (Gdiplus::REAL)h - 1.0f, 7.0f);

        COLORREF textColor;
        if (m_primary)
        {
            // 橙色实心主按钮。
            const Gdiplus::Color fill = IsHover()
                ? Gdiplus::Color(255, 0xFF, 0x6A, 0x30)
                : Gdiplus::Color(255, 0xFF, 0x7A, 0x45);
            Gdiplus::SolidBrush brush(fill);
            g.FillPath(&brush, &path);
            textColor = RGB(255, 255, 255);
        }
        else
        {
            // 白底描边次按钮。
            const Gdiplus::Color fill = IsHover()
                ? Gdiplus::Color(255, 0xF6, 0xF5, 0xF2)
                : Gdiplus::Color(255, 0xFF, 0xFF, 0xFF);
            Gdiplus::SolidBrush brush(fill);
            g.FillPath(&brush, &path);
            Gdiplus::Pen border(Gdiplus::Color(255, 0xD0, 0xCE, 0xC9), 1.0f);
            g.DrawPath(&border, &path);
            textColor = RGB(0x55, 0x55, 0x55);
        }

        DrawGdiText(hdc, (LPCWSTR)GetText(), m_rcItem, 12, true, textColor,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    bool OnSetCursor(POINT /*pt*/) override { return false; }

private:
    bool m_primary;   // true=橙色主按钮，false=白底次按钮
};

// =====================================================================
// 内嵌 XML 布局
// =====================================================================
//
// 外层 net-root（白底 vbox）自上而下：
//   · 副标题行；
//   · 三张服务器卡片（net-card，单选）；
//   · 分隔线；
//   · 表单（服务器地址 / 端口），weight=1 吸收余高；
//   · 底部按钮行（测试连接 / 保存）。
// =====================================================================
const char* const kNetSettingsBodyXml = u8R"xml(
<net-root padding="0" gap="0">
  <hbox fixedHeight="31" padding="19,9,19,2" gap="0">
    <net-text text="分别配置三组服务器" size="10" color="142,140,135" weight="1"/>
  </hbox>
  <vbox fixedHeight="184" padding="19,2,19,5" gap="9">
    <net-card id="700" kind="chat"  fixedHeight="53"/>
    <net-card id="701" kind="image" fixedHeight="53"/>
    <net-card id="702" kind="file"  fixedHeight="53"/>
  </vbox>
  <separator fixedHeight="1" margin="19,7,19,9"/>
  <vbox weight="1" padding="19,7,19,7" gap="12">
    <hbox fixedHeight="32" gap="12">
      <net-text text="服务器地址" size="11" color="90,90,90" fixedWidth="66"/>
      <edit id="710" weight="1"/>
    </hbox>
    <hbox fixedHeight="32" gap="12">
      <net-text text="端口" size="11" color="90,90,90" fixedWidth="66"/>
      <edit id="711" fixedWidth="128"/>
      <control weight="1"/>
    </hbox>
    <control weight="1"/>
  </vbox>
  <hbox fixedHeight="61" padding="19,10,19,17" gap="12">
    <net-actionbtn id="720" text="测试连接" primary="false" weight="1"/>
    <net-actionbtn id="721" text="保存"     primary="true"  weight="1"/>
  </hbox>
</net-root>
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

// 把 net-card 的 kind 属性翻译成 NetServerKind。
int ServerKindFromAttr(const std::string& kind)
{
    if (kind == "image")
    {
        return kNetServerImage;
    }
    if (kind == "file")
    {
        return kNetServerFile;
    }
    return kNetServerChat;
}

// 把 net-text 的 align 属性翻译成 DrawText 对齐标志。
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

// =====================================================================
// 自定义标签工厂
// =====================================================================
//
// 接 net-root / net-card / net-text / net-actionbtn / control；其余标签
// （vbox / hbox / edit / switch / separator）走 builder 内置处理。
std::unique_ptr<DuiControl> CreateNetCustomControl(const DuiXmlBuilder::Node& n)
{
    if (n.tag == "net-root")
    {
        return std::unique_ptr<DuiControl>(new NetSettingsRootPanel());
    }

    if (n.tag == "net-card")
    {
        std::unique_ptr<NetServerCard> card(new NetServerCard());
        card->SetServerKind(ServerKindFromAttr(GetNodeAttr(n, "kind")));
        return std::unique_ptr<DuiControl>(card.release());
    }

    if (n.tag == "net-text")
    {
        std::unique_ptr<NetText> text(new NetText());
        const CString     strText = Utf8ToCString(GetNodeAttr(n, "text"));
        const std::string strSize = GetNodeAttr(n, "size", "13");
        const COLORREF color = ParseColor(GetNodeAttr(n, "color"),
                                          RGB(0x2A, 0x2A, 0x2A));
        const bool bold = (GetNodeAttr(n, "bold") == "true");
        const UINT align = TextAlignFromAttr(GetNodeAttr(n, "align"));
        text->Configure(strText, atoi(strSize.c_str()), color, bold, align);
        return std::unique_ptr<DuiControl>(text.release());
    }

    if (n.tag == "net-actionbtn")
    {
        std::unique_ptr<NetActionButton> btn(new NetActionButton());
        const CString btnText = Utf8ToCString(GetNodeAttr(n, "text"));
        btn->SetText(btnText);
        btn->SetPrimary(GetNodeAttr(n, "primary") == "true");
        return std::unique_ptr<DuiControl>(btn.release());
    }

    if (n.tag == "control")
    {
        return std::unique_ptr<DuiControl>(new DuiControl());
    }

    return nullptr;
}

} // anonymous namespace

std::unique_ptr<DuiControl> BuildNetSettingsBody()
{
    return DuiXmlBuilder::FromString(kNetSettingsBodyXml, &CreateNetCustomControl);
}

} // namespace balloonnetsettings
