/**
 *  BalloonChatLayout.cpp
 *
 *  单聊 / 群聊客户区控件树的构造实现。布局以内嵌 XML 常量为唯一真值源
 *  （随 exe 出货、0 部署步骤），交给 balloonui 的 DuiXmlBuilder 解析；
 *  控件树建好后再用真实 PNG 皮肤资源（Skins\Skin0\...）给工具栏 / 底部
 *  按钮换肤，使外观贴合设计稿。
 *
 *  本 .cpp 故意<u>不</u> include Flamingo 的 stdafx.h —— 它要被
 *  Flamingo.vcxproj 与 FlamingoUnit.vcxproj 两个工程逐字共用，所以自己
 *  把 balloonui 头文件所需的 ATL/WTL 前置依赖引进来。在 Flamingo.vcxproj
 *  里本文件需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026.05.21
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
#include <gdiplus.h>   // PNG 皮肤 / 头像加载

#include "BalloonChatLayout.h"
#include "../../../third-party/balloonui/balloonui/DuiXmlBuilder.h"
#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"   // 工具栏 tooltip

namespace balloonchat {

using balloonwjui::DuiControl;
using balloonwjui::DuiXmlBuilder;
using balloonwjui::DuiHost;
using balloonwjui::DuiButton;
using balloonwjui::DuiToolTipMgr;

namespace {

// =====================================================================
// 聊天窗客户区布局（内嵌 XML）
//
// 单聊 / 群聊都是外层 hbox：左半"聊天主列" + 右半"侧栏"。聊天主列里
// "接收区 + 中部工具栏 + 输入区 + 底部按钮栏"两者相同，抽成 kConvCoreXml
// 片段复用；顶部工具栏与右侧栏各异。BuildXxxChatBody 把片段拼成完整 XML。
//
// 工具栏按钮在 XML 里只占位（带文字、统一尺寸），控件树建好后由
// ApplyToolbarButtonStyles 贴真实图标 PNG、清空文字。尺寸约定：
//   · 工具栏 hbox fixedHeight=30、纵向 padding=0  → 按钮高 = 30
//   · 工具栏按钮 fixedWidth=34                    → 图标单元 = 34 x 30
// 这样图标位图与按钮矩形 1:1，不产生拉伸。
// =====================================================================

// 聊天主列的中段 —— 接收区 / 中部工具栏 / 输入区 / 底部按钮栏。单聊群聊
// 一致。
//
// 接收区与"工具栏+输入区"用一个横向 DuiSplitter 分隔（orientation=
// horizontal：分隔条横向、两 pane 上下叠）：
//   pane0 = 接收 richedit
//   pane1 = vbox[ 中部工具栏 + 发送 richedit ]
// 分隔条落在接收区与工具栏之间（即工具栏上边缘），拖它即可在收 / 发两
// 个文本框之间重新分配高度；工具栏固定 30px、贴在发送框上方随动。
//
// 底部"关闭/发送"按钮高 27 < 底部蓝色条带高度，alignCross=far 贴在条带上。
const char* const kConvCoreXml = u8R"xml(
  <splitter weight="1" orientation="horizontal" bar-thickness="1"
            split-fraction="0.62" min-size-0="80" min-size-1="100">
    <richedit id="100" read-only="true" multi-line="true" word-wrap="true"
              margins="8,6,8,6"/>
    <vbox>
      <hbox fixedHeight="30" padding="4,0,4,0" gap="2">
        <button id="301" text="字"   fixedWidth="34"/>
        <button id="302" text="表情" fixedWidth="34"/>
        <button id="303" text="抖动" fixedWidth="34"/>
        <button id="304" text="图片" fixedWidth="34"/>
        <button id="305" text="截图" fixedWidth="34"/>
        <vbox weight="1"/>
        <button id="306" text="记录" fixedWidth="34"/>
      </hbox>
      <richedit id="101" read-only="false" multi-line="true" word-wrap="true"
                placeholder="输入消息..." weight="1" margins="8,6,8,6"/>
    </vbox>
  </splitter>
  <hbox fixedHeight="34" padding="10,0,12,3" gap="5">
    <vbox weight="1"/>
    <button id="401" text="关闭(C)" fixedWidth="72" fixedHeight="27" alignCross="far"/>
    <button id="402" text="发送(S)" fixedWidth="64" fixedHeight="27" alignCross="far"/>
    <button id="403" text=""        fixedWidth="27" fixedHeight="27" alignCross="far"/>
  </hbox>
)xml";

// 单聊顶部工具栏片段：文件 / 远程。
const char* const kBuddyTopToolbarXml = u8R"xml(
  <hbox fixedHeight="30" padding="6,0,6,0" gap="4">
    <button id="201" text="文件" fixedWidth="34"/>
    <button id="202" text="远程" fixedWidth="34"/>
    <vbox weight="1"/>
  </hbox>
)xml";

// 群聊顶部工具栏片段：与单聊同为 文件 / 远程（群专属入口后续补）。
const char* const kGroupTopToolbarXml = u8R"xml(
  <hbox fixedHeight="30" padding="6,0,6,0" gap="4">
    <button id="201" text="文件" fixedWidth="34"/>
    <button id="202" text="远程" fixedWidth="34"/>
    <vbox weight="1"/>
  </hbox>
)xml";

// 单聊右侧"消息记录"面板片段：竖分隔条 + 固定宽 vbox（标题 / 记录
// richedit / 翻页栏）。本阶段仅布局占位。
const char* const kBuddyRightPanelXml = u8R"xml(
  <separator id="131" orientation="vertical" fixedWidth="1"/>
  <vbox id="130" fixedWidth="236">
    <hbox fixedHeight="30" padding="10,5,10,3">
      <label text="消息记录"/>
      <vbox weight="1"/>
    </hbox>
    <separator fixedHeight="1"/>
    <richedit id="120" read-only="true" multi-line="true" word-wrap="true"
              weight="1" margins="6,4,6,4"/>
    <separator fixedHeight="1"/>
    <hbox fixedHeight="30" padding="8,3,8,3" gap="4">
      <label text="第 0/0 页"/>
      <vbox weight="1"/>
    </hbox>
  </vbox>
)xml";

// 群聊右侧栏片段：竖分隔条 + 固定宽 vbox（群公告 / 群成员）。
const char* const kGroupRightPanelXml = u8R"xml(
  <separator orientation="vertical" fixedWidth="1"/>
  <vbox fixedWidth="212">
    <hbox fixedHeight="30" padding="10,5,10,3">
      <label text="群公告"/>
      <vbox weight="1"/>
    </hbox>
    <separator fixedHeight="1"/>
    <richedit id="110" read-only="true" multi-line="true" word-wrap="true"
              fixedHeight="118" margins="6,4,6,4"/>
    <separator fixedHeight="1"/>
    <hbox fixedHeight="30" padding="10,5,10,3" gap="4">
      <label text="群成员" fixedWidth="52"/>
      <label id="112" text="(0/0)"/>
      <vbox weight="1"/>
    </hbox>
    <separator fixedHeight="1"/>
    <treeview id="111" weight="1">
      <column title="成员" width="196"/>
    </treeview>
  </vbox>
)xml";

// =====================================================================
// 9-grid 皮肤背景（背景图 Skins\Skin0\ChatDlgBg.png，587x535）
// 内距依据像素分带：顶 101 / 底 34 / 左右各 8。
// =====================================================================
const int kChatBgInsetTop    = 101;
const int kChatBgInsetBottom = 34;
const int kChatBgInsetLeft   = 8;
const int kChatBgInsetRight  = 8;
const TCHAR* const kChatBgRelativePath = _T("Skins\\Skin0\\ChatDlgBg.png");

// =====================================================================
// PNG 皮肤资源加载
//
// balloonui 的 DuiButton 通过 SetBgBitmap 接受 4 状态背景位图，OnPaint
// 一旦发现有背景位图就只画位图、不再画实心圆角 —— 借此把按钮换成真实
// 皮肤外观。位图必须是 32bpp 预乘 alpha，才能在 DuiNinePatch 的 alpha
// 混合路径下正确透出底下的聊天背景。
// =====================================================================

// 工具栏图标单元尺寸（与 XML 工具栏按钮 fixedWidth / hbox fixedHeight 对齐）。
const int kIconCellW = 34;
const int kIconCellH = 30;

// 把一张 GDI+ 位图转成 32bpp 预乘 alpha 的 HBITMAP。
//
// <u>用 bottom-up DIB（biHeight 取正）</u>：balloonui 的 DuiNinePatch::Draw
// 里那段 32bpp DIBSection 处理是按 bottom-up（biHeight>0）写并验证的 ——
// 它对 biHeight>0 才做"指到末行 + 负 stride"的正确换算；若给它 top-down
// DIB（biHeight<0），它会按 bottom-up 解读导致整图上下颠倒。故此处刻意
// 造 bottom-up DIB：源的视觉第 y 行写到 DIB 的第 (h-1-y) 行。
HBITMAP BitmapToPremulHBitmap(Gdiplus::Bitmap& bmp)
{
    const int w = static_cast<int>(bmp.GetWidth());
    const int h = static_cast<int>(bmp.GetHeight());
    if (w <= 0 || h <= 0)
    {
        return NULL;
    }

    BITMAPINFO bi;
    ::ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = h;    // 正 = bottom-up（对齐 DuiNinePatch）
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hbm = ::CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (hbm == NULL || pBits == NULL)
    {
        return hbm;
    }

    // 用 PixelFormat32bppPARGB 锁位 —— GDI+ 直接给出预乘 alpha 像素。
    Gdiplus::Rect rc(0, 0, w, h);
    Gdiplus::BitmapData bd;
    if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead,
                     PixelFormat32bppPARGB, &bd) == Gdiplus::Ok)
    {
        const int rowBytes = w * 4;
        for (int y = 0; y < h; ++y)
        {
            // 源视觉第 y 行 → bottom-up DIB 的第 (h-1-y) 行。
            ::memcpy(static_cast<BYTE*>(pBits) + (LONG_PTR)(h - 1 - y) * rowBytes,
                     static_cast<BYTE*>(bd.Scan0) + (LONG_PTR)y * bd.Stride,
                     rowBytes);
        }
        bmp.UnlockBits(&bd);
    }
    return hbm;
}

// 从磁盘加载 PNG，返回原生尺寸的预乘 alpha HBITMAP；失败返回 NULL。
HBITMAP LoadPngAsHBitmap(LPCTSTR absPath)
{
    Gdiplus::Bitmap bmp(absPath);
    if (bmp.GetLastStatus() != Gdiplus::Ok)
    {
        return NULL;
    }
    return BitmapToPremulHBitmap(bmp);
}

// 把图标 PNG 居中、原生尺寸（不缩放）合成到 kIconCellW x kIconCellH 的
// 透明画布上，返回该画布的 HBITMAP；图标缺失时返回一张全透明画布。
HBITMAP MakeIconCell(LPCTSTR iconAbsPath)
{
    Gdiplus::Bitmap canvas(kIconCellW, kIconCellH, PixelFormat32bppPARGB);
    if (canvas.GetLastStatus() != Gdiplus::Ok)
    {
        return NULL;
    }
    {
        Gdiplus::Graphics g(&canvas);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));   // 透明底
        Gdiplus::Bitmap icon(iconAbsPath);
        if (icon.GetLastStatus() == Gdiplus::Ok)
        {
            const int iw = static_cast<int>(icon.GetWidth());
            const int ih = static_cast<int>(icon.GetHeight());
            // 居中、原生尺寸绘制 —— 不缩放，避免图标变形。
            g.DrawImage(&icon, (kIconCellW - iw) / 2, (kIconCellH - ih) / 2,
                        iw, ih);
        }
    }
    return BitmapToPremulHBitmap(canvas);
}

// 生成一张抗锯齿圆角按钮位图（实色填充 + 1px 描边 + 居中文字），用于
// "关闭"这类需要在彩色背景上清晰可辨、又不宜用品牌蓝实心的次级按钮。
//
// 为什么把文字也烘焙进位图：balloonui 的 DuiButton 对 StylePushButton
// <u>强制</u>用白色 label（见 DuiButton::OnPaint），SetTextColor 不生效；
// 白底按钮配强制白字会看不见。故这里直接把深色文字画进按钮位图，调用
// 方再把按钮自身的 text 清空，避免 DuiButton 再叠一层白字。
//
//   w/h：        位图尺寸；须与按钮实际尺寸一致（insets=0、1:1 绘制，
//                文字才不被 9-grid 拉伸变形）。
//   fill/border：填充色 / 描边色。
//   text：       居中文字；nullptr / 空串则只画按钮形状。
//   textColor：  文字颜色。
// 返回预乘 alpha 的 HBITMAP（圆角外为透明）。
HBITMAP MakeRoundButtonBitmap(int w, int h, COLORREF fill, COLORREF border,
                              LPCTSTR text, COLORREF textColor)
{
    Gdiplus::Bitmap canvas(w, h, PixelFormat32bppPARGB);
    if (canvas.GetLastStatus() != Gdiplus::Ok)
    {
        return NULL;
    }
    {
        Gdiplus::Graphics g(&canvas);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // 圆角半径 3px；四角各加一段 90° 弧拼成圆角矩形路径。
        const int kRadius = 3;
        const Gdiplus::REAL d = static_cast<Gdiplus::REAL>(kRadius * 2);
        // 内缩 0.5px，让 1px 描边完整落在位图内、不被边缘裁掉。
        const Gdiplus::REAL x0 = 0.5f;
        const Gdiplus::REAL y0 = 0.5f;
        const Gdiplus::REAL ww = static_cast<Gdiplus::REAL>(w) - 1.0f;
        const Gdiplus::REAL hh = static_cast<Gdiplus::REAL>(h) - 1.0f;

        Gdiplus::GraphicsPath path;
        path.AddArc(x0,          y0,          d, d, 180.0f, 90.0f);
        path.AddArc(x0 + ww - d, y0,          d, d, 270.0f, 90.0f);
        path.AddArc(x0 + ww - d, y0 + hh - d, d, d,   0.0f, 90.0f);
        path.AddArc(x0,          y0 + hh - d, d, d,  90.0f, 90.0f);
        path.CloseFigure();

        Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(fill),
                                                 GetGValue(fill), GetBValue(fill)));
        g.FillPath(&brush, &path);
        Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(border),
                                        GetGValue(border), GetBValue(border)), 1.0f);
        g.DrawPath(&pen, &path);

        // 居中文字（微软雅黑 9pt，与界面默认字体一致）。
        if (text != NULL && text[0] != 0)
        {
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            Gdiplus::Font font(L"\x5fae\x8f6f\x96c5\x9ed1", 9.0f,
                               Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush textBrush(
                Gdiplus::Color(255, GetRValue(textColor),
                               GetGValue(textColor), GetBValue(textColor)));
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::RectF rcText(0.0f, 0.0f,
                                  static_cast<Gdiplus::REAL>(w),
                                  static_cast<Gdiplus::REAL>(h));
            g.DrawString(text, -1, &font, rcText, &fmt, &textBrush);
        }
    }
    return BitmapToPremulHBitmap(canvas);
}

// 皮肤位图按 (绝对路径) 全局缓存：每张只加载一次，按 app 生命周期常驻
// （DuiButton 不持有所有权、不释放；进程退出由 OS 回收）。这样对话框
// 反复开关也不会反复加载、不会泄漏。
HBITMAP GetCachedBitmap(const CString& absPath, bool asIconCell)
{
    static std::map<std::wstring, HBITMAP> s_cache;
    std::wstring key = (asIconCell ? L"cell|" : L"raw|");
    key += absPath.GetString();

    std::map<std::wstring, HBITMAP>::iterator it = s_cache.find(key);
    if (it != s_cache.end())
    {
        return it->second;
    }
    HBITMAP hbm = asIconCell ? MakeIconCell(absPath) : LoadPngAsHBitmap(absPath);
    s_cache[key] = hbm;
    return hbm;
}

// id → (工具栏图标 PNG 相对路径, 悬停 tooltip 文字) 的映射表。
struct ToolbarIconEntry
{
    UINT          id;
    const TCHAR*  relPath;
    const TCHAR*  tooltip;
};
const ToolbarIconEntry kToolbarIcons[] =
{
    { kIdTbFile,          _T("Skins\\Skin0\\BuddyTopToolBar\\sendfile.png"),            _T("发送文件") },
    { kIdTbRemoteDesktop, _T("Skins\\Skin0\\BuddyTopToolBar\\remote_desktop.png"),      _T("远程桌面") },
    { kIdTbFont,          _T("Skins\\Skin0\\MidToolBar\\aio_quickbar_font.png"),        _T("字体设置") },
    { kIdTbEmoji,         _T("Skins\\Skin0\\MidToolBar\\aio_quickbar_face.png"),        _T("表情") },
    { kIdTbShake,         _T("Skins\\Skin0\\MidToolBar\\aio_quickbar_flirtationface.png"), _T("窗口抖动") },
    { kIdTbImage,         _T("Skins\\Skin0\\MidToolBar\\aio_quickbar_sendpic.png"),     _T("发送图片") },
    { kIdTbScreenshot,    _T("Skins\\Skin0\\MidToolBar\\aio_quickbar_cut.png"),         _T("屏幕截图") },
    { kIdTbMsgHistory,    _T("Skins\\Skin0\\MidToolBar\\aio_quickbar_msglog.png"),      _T("消息记录") },
};

// 给底部按钮（关闭 / 发送）贴 4 状态 PNG 皮肤。
//   id：        目标按钮 ctrlId。
//   skinBase：  皮肤文件名前缀，如 "btn_close" → btn_close_normal.png 等。
//   textColor： 按钮文字颜色（发送按钮皮肤是蓝底，需配白字才清晰）。
void ApplyBottomButtonSkin(DuiControl* root, UINT id, const TCHAR* skinBase,
                           COLORREF textColor)
{
    DuiButton* btn = dynamic_cast<DuiButton*>(root->FindCtrlById(id));
    if (btn == NULL)
    {
        return;
    }

    CString dir = _T("Skins\\Skin0\\Button\\");
    CString relNormal, relHover, relDown;
    relNormal.Format(_T("%s%s_normal.png"),    dir.GetString(), skinBase);
    relHover.Format (_T("%s%s_highlight.png"), dir.GetString(), skinBase);
    relDown.Format  (_T("%s%s_down.png"),      dir.GetString(), skinBase);

    HBITMAP n = GetCachedBitmap(DuiXmlBuilder::ResolveAssetPath(relNormal), false);
    HBITMAP h = GetCachedBitmap(DuiXmlBuilder::ResolveAssetPath(relHover),  false);
    HBITMAP d = GetCachedBitmap(DuiXmlBuilder::ResolveAssetPath(relDown),   false);
    if (n != NULL)
    {
        // 按钮实际尺寸略小于皮肤原生尺寸，用 9-grid（四角 7px 不缩放）
        // 让圆角在缩放时保持清晰、不被压扁；文字仍由按钮画在皮肤之上。
        btn->SetBgBitmap(n, h, d, n);
        btn->SetBgInsets(7, 7, 7, 7);
    }
    btn->SetTextColor(textColor);
}

} // anonymous namespace

void ApplyChatWindowBackground(DuiHost* host)
{
    if (host == NULL)
    {
        return;
    }
    CString absPath = DuiXmlBuilder::ResolveAssetPath(kChatBgRelativePath);
    RECT insets = { kChatBgInsetLeft, kChatBgInsetTop,
                    kChatBgInsetRight, kChatBgInsetBottom };
    host->LoadBgImageFromFile(absPath, insets);
}

HBITMAP LoadChatAvatar(int faceId)
{
    CString rel;
    rel.Format(_T("Image\\UserThumbs\\%d.png"), faceId);
    CString absPath = DuiXmlBuilder::ResolveAssetPath(rel);

    Gdiplus::Bitmap bmp(absPath);
    if (bmp.GetLastStatus() != Gdiplus::Ok)
    {
        return NULL;
    }
    HBITMAP hBitmap = NULL;
    bmp.GetHBITMAP(Gdiplus::Color(255, 255, 255), &hBitmap);
    return hBitmap;
}

void ApplyToolbarButtonStyles(DuiControl* root,
                              std::vector<DuiControl*>& outTooltipCtrls)
{
    if (root == NULL)
    {
        return;
    }

    // 工具栏按钮：贴真实图标 PNG（居中合成到 34x30 单元），清空占位文字
    // 使按钮只显示图标，并注册悬停 tooltip（图标按钮无文字，靠 tooltip
    // 告诉用户用途）。
    const int kEntryCount = sizeof(kToolbarIcons) / sizeof(kToolbarIcons[0]);
    for (int i = 0; i < kEntryCount; ++i)
    {
        DuiButton* btn = dynamic_cast<DuiButton*>(
            root->FindCtrlById(kToolbarIcons[i].id));
        if (btn == NULL)
        {
            continue;
        }
        CString abs = DuiXmlBuilder::ResolveAssetPath(kToolbarIcons[i].relPath);
        HBITMAP cell = GetCachedBitmap(abs, true);
        if (cell != NULL)
        {
            btn->SetButtonType(DuiButton::StylePushButton);
            btn->SetText(_T(""));                       // 只留图标
            btn->SetBgBitmap(cell, cell, cell, cell);   // 4 状态同图
            btn->SetBgInsets(0, 0, 0, 0);
        }
        // 注册 tooltip（不论图标是否加载成功，按钮都需要悬停提示）。
        // DuiHost 会在 hover 控件变化时自动驱动 DuiToolTipMgr 弹出。
        DuiToolTipMgr::Inst().Register(btn, kToolbarIcons[i].tooltip);
        outTooltipCtrls.push_back(btn);
    }

    // 关闭按钮：原 btn_close 皮肤是浅色，叠在底部蓝色条带上几乎糊成一片、
    // 背景与文字都看不清。改用程序生成的"白底 + 灰描边"圆角按钮 —— 在蓝
    // 条上轮廓清晰，配深色文字可读。3 状态位图按 app 生命周期常驻。
    DuiButton* btnClose = dynamic_cast<DuiButton*>(root->FindCtrlById(kIdBtnClose));
    if (btnClose != NULL)
    {
        // 位图尺寸须与按钮 XML 尺寸一致（id=401：fixedWidth=72 /
        // fixedHeight=27）—— insets=0 按 1:1 绘制，烘焙进去的文字才不会
        // 被 9-grid 横向拉伸变形。
        const int      kCloseW    = 72;
        const int      kCloseH    = 27;
        const COLORREF kCloseText = RGB(51, 51, 51);   // 深灰文字
        const CString  closeLabel = btnClose->GetText();   // "关闭(C)"（来自 XML）

        static HBITMAP s_closeNormal  = MakeRoundButtonBitmap(
            kCloseW, kCloseH, RGB(255, 255, 255), RGB(140, 150, 165),
            closeLabel, kCloseText);
        static HBITMAP s_closeHover   = MakeRoundButtonBitmap(
            kCloseW, kCloseH, RGB(236, 243, 250), RGB(120, 140, 170),
            closeLabel, kCloseText);
        static HBITMAP s_closePressed = MakeRoundButtonBitmap(
            kCloseW, kCloseH, RGB(222, 232, 244), RGB(110, 130, 160),
            closeLabel, kCloseText);

        btnClose->SetButtonType(DuiButton::StylePushButton);
        if (s_closeNormal != NULL)
        {
            btnClose->SetBgBitmap(s_closeNormal, s_closeHover,
                                  s_closePressed, s_closeNormal);
            btnClose->SetBgInsets(0, 0, 0, 0);   // 1:1 绘制、不拉伸
        }
        // 文字已烘焙进位图；清空按钮自身 label —— 否则 DuiButton 会再叠
        // 一层 label，而 StylePushButton 的 label 被强制为白色、在白底
        // 位图上看不见（这正是之前文字消失的根因）。
        btnClose->SetText(_T(""));
    }

    // 发送按钮：蓝底皮肤配白字。
    ApplyBottomButtonSkin(root, kIdBtnSend,  _T("btn_send"),  RGB(255, 255, 255));

    // 发送右侧 ▾ 下拉：贴 Button\btnright_*.png（蓝底白三角，三角已烘焙
    // 在皮肤里），替代默认蓝色 RoundRect（GDI 非抗锯齿、边角有锯齿）。
    // 该按钮 XML 文字已为空，故文字色无影响。
    ApplyBottomButtonSkin(root, kIdBtnSendArrow, _T("btnright"), RGB(255, 255, 255));
}

void UnregisterTooltips(const std::vector<DuiControl*>& ctrls)
{
    for (size_t i = 0; i < ctrls.size(); ++i)
    {
        DuiToolTipMgr::Inst().Unregister(ctrls[i]);
    }
}

std::unique_ptr<DuiControl> BuildBuddyChatBody()
{
    // 单聊 = 外层 hbox 包住 [左半 vbox：单聊顶栏 + 主列中段] + [右侧消息
    // 记录面板]。左半 vbox 自带 weight="1"，吃掉右面板固定宽外的剩余宽度。
    std::string xml;
    xml += "<hbox weight=\"1\"><vbox weight=\"1\">";
    xml += kBuddyTopToolbarXml;
    xml += kConvCoreXml;
    xml += "</vbox>";
    xml += kBuddyRightPanelXml;
    xml += "</hbox>";
    return DuiXmlBuilder::FromString(xml.c_str());
}

std::unique_ptr<DuiControl> BuildGroupChatBody()
{
    // 群聊 = 外层 hbox 包住 [左半 vbox：群聊顶栏 + 主列中段] + [右侧栏：
    // 群公告 / 群成员]。结构与单聊同形，仅顶栏与右侧栏片段不同。
    std::string xml;
    xml += "<hbox weight=\"1\"><vbox weight=\"1\">";
    xml += kGroupTopToolbarXml;
    xml += kConvCoreXml;
    xml += "</vbox>";
    xml += kGroupRightPanelXml;
    xml += "</hbox>";
    return DuiXmlBuilder::FromString(xml.c_str());
}

} // namespace balloonchat
