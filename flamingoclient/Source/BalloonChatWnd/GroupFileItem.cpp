/**
 *  GroupFileItem.cpp
 *
 *  群文件行的绘制实现。彩色类型角标 / 行悬停底 / 下载箭头悬停底用 GDI+
 *  抗锯齿，文字用 GDI DrawText（清晰）。下载箭头单独命中（hover 高亮 + 手型 +
 *  点击触发下载动作，本增量仅记日志）。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记
 *  PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-06-08
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

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include "GroupFileItem.h"
#include "../CustomMsgDef.h"                          // WM_GROUP_FILE_DOWNLOAD / WM_GROUP_FILE_UPLOAD_CANCEL
#include "../../../third-party/balloonui/balloonui/DuiHost.h"        // GetHost()->m_hWnd（向宿主投递下载 / 取消上传消息）
#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiToolTip.h"  // 文件名/说明放不下时挂 tooltip

namespace balloonchatwnd {

namespace {

// ---- 几何 / 配色 -------------------------------------------------------

const int kEdgePad      = 9;    // 行左右内边距
const int kBadgeSize    = 28;   // 类型角标边长
const int kBadgeGap     = 8;    // 角标与文字间距
const int kNamePx       = 11;   // 文件名字号
const int kMetaPx       = 9;    // 说明行字号
const int kTextLineGap  = 2;    // 文件名与说明两行间距
const int kDownloadZone = 24;   // 下载箭头命中区宽度
const int kDownloadGlyph = 13;  // 下载箭头图形边长
const int kTrashGap     = 2;    // 垃圾桶命中区与下载箭头命中区之间的间隙
const int kTrashGlyph   = 11;   // 垃圾桶图形边长

const Gdiplus::REAL kBadgeRadius = 4.0f;   // 角标圆角
const Gdiplus::REAL kHoverRadius = 6.0f;   // 行悬停底圆角

const Gdiplus::Color kHoverFill   (255, 0xF2, 0xF1, 0xEE);   // 行悬停底
const Gdiplus::Color kDlHoverFill (255, 0xFF, 0xF0, 0xE9);   // 下载箭头悬停底（淡橙）
const COLORREF       kNameColor   = RGB(0x40, 0x40, 0x40);   // 文件名（干净的中性深色，不washed也不纯黑）
const COLORREF       kMetaColor   = RGB(0x9A, 0x9A, 0x9A);   // 说明行
const COLORREF       kBadgeText   = RGB(255, 255, 255);      // 角标文字
const COLORREF       kDlColor     = RGB(0x9A, 0x9A, 0x9A);   // 下载箭头常态
const COLORREF       kDlHoverColor= RGB(0xFF, 0x7A, 0x45);   // 下载箭头悬停（主题橙）

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

// 用 GDI 绘制文字。
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
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Microsoft YaHei");
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

// 按扩展名给类型角标定底色。设计稿区分 PDF 红 / 表格绿 / 文档蓝 / 设计紫，
// 此处按扩展名归类上色；未知归灰。颜色与 design/群文件.png 的角标一致取向。
COLORREF BadgeColorForExt(const CString& extLower)
{
    if (extLower == _T("pdf"))
    {
        return RGB(0xE5, 0x4D, 0x42);   // PDF 红
    }
    if (extLower == _T("xls") || extLower == _T("xlsx") || extLower == _T("csv"))
    {
        return RGB(0x21, 0xA3, 0x66);   // 表格 绿
    }
    if (extLower == _T("doc") || extLower == _T("docx") ||
        extLower == _T("txt") || extLower == _T("rtf") || extLower == _T("md"))
    {
        return RGB(0x33, 0x7A, 0xE5);   // 文档 蓝
    }
    if (extLower == _T("ppt") || extLower == _T("pptx"))
    {
        return RGB(0xE8, 0x73, 0x2C);   // 演示 橙
    }
    if (extLower == _T("fig") || extLower == _T("psd") || extLower == _T("ai") ||
        extLower == _T("sketch") || extLower == _T("xd") || extLower == _T("cdr"))
    {
        return RGB(0x8B, 0x5C, 0xF6);   // 设计 紫
    }
    if (extLower == _T("png") || extLower == _T("jpg") || extLower == _T("jpeg") ||
        extLower == _T("gif") || extLower == _T("bmp") || extLower == _T("webp"))
    {
        return RGB(0x14, 0xB8, 0xA6);   // 图片 青
    }
    if (extLower == _T("zip") || extLower == _T("rar") || extLower == _T("7z") ||
        extLower == _T("tar") || extLower == _T("gz"))
    {
        return RGB(0xF5, 0x9E, 0x0B);   // 压缩包 琥珀
    }
    return RGB(0x9A, 0xA0, 0xA6);       // 其它 灰
}

} // anonymous namespace

GroupFileItem::GroupFileItem()
    : m_typeColor(RGB(0x9A, 0xA0, 0xA6))
    , m_downloadHover(false)
    , m_downloading(false)
    , m_downloadPercent(0)
    , m_uploading(false)
    , m_uploadId(0)
    , m_uploadPercent(0)
    , m_canDelete(false)
    , m_deleteHover(false)
{
}

GroupFileItem::~GroupFileItem()
{
#if BUI_FEATURE_TOOLTIP
    // 行被销毁（换页 / 推送重渲染都会销毁旧行）时反注册，杜绝 tooltip 管理器悬空指针。
    balloonwjui::DuiToolTipMgr::Inst().Unregister(this);
#endif
}

void GroupFileItem::SetDeletable(bool canDelete)
{
    if (m_canDelete != canDelete)
    {
        m_canDelete = canDelete;
        Invalidate();
    }
}

void GroupFileItem::SetUploading(int uploadId, int percent)
{
    if (percent < 0)
    {
        percent = 0;
    }
    if (percent > 100)
    {
        percent = 100;
    }
    m_uploading     = true;
    m_uploadId      = uploadId;
    m_uploadPercent = percent;
    Invalidate();
}

void GroupFileItem::SetDownloadProgress(int percent)
{
    if (percent < 0)
    {
        percent = 0;
    }
    if (percent > 100)
    {
        percent = 100;
    }
    m_downloading     = true;
    m_downloadPercent = percent;
    Invalidate();
}

void GroupFileItem::ClearDownloadProgress()
{
    if (m_downloading)
    {
        m_downloading     = false;
        m_downloadPercent = 0;
        Invalidate();
    }
}

void GroupFileItem::SetFile(const CString& name, const CString& metaLine, const CString& ext)
{
    m_name     = name;
    m_metaLine = metaLine;

    // 角标底色按扩展名分类；角标文字取扩展名大写、最多 4 字符（XLSX/DOCX/PPTX 等正好）。
    CString extLower = ext;
    extLower.MakeLower();
    m_typeColor = BadgeColorForExt(extLower);

    CString label = ext;
    label.MakeUpper();
    if (label.GetLength() > 4)
    {
        label = label.Left(4);
    }
    if (label.IsEmpty())
    {
        label = _T("FILE");
    }
    m_typeLabel = label;

#if BUI_FEATURE_TOOLTIP
    // 文件名在窄边栏里常被截断省略；给本行挂上完整文件名的 tooltip，悬停可见全名。
    // tooltip 浮窗按 DT_SINGLELINE 单行渲染，故只放文件名（时间 / 大小很短不易截断）。
    balloonwjui::DuiToolTipMgr::Inst().Register(this, m_name);
#endif

    Invalidate();
}

void GroupFileItem::SetIds(const CString& entryid, const CString& fileid)
{
    m_entryid = entryid;
    m_fileid  = fileid;
}

RECT GroupFileItem::DownloadHitRect() const
{
    // 下载箭头命中区：贴右内边距、占整行高（纵向命中宽松，便于点击）。
    RECT rc;
    rc.right  = m_rcItem.right - kEdgePad + 6;   // 略向右贴边（+6 抵消内边距视觉留白）
    rc.left   = rc.right - kDownloadZone;
    rc.top    = m_rcItem.top;
    rc.bottom = m_rcItem.bottom;
    return rc;
}

RECT GroupFileItem::TrashHitRect() const
{
    // 垃圾桶命中区：紧贴下载箭头命中区左侧、等宽、占整行高。
    const RECT dl = DownloadHitRect();
    RECT rc;
    rc.right  = dl.left - kTrashGap;
    rc.left   = rc.right - kDownloadZone;
    rc.top    = m_rcItem.top;
    rc.bottom = m_rcItem.bottom;
    return rc;
}

void GroupFileItem::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
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

    // 整行悬停：浅灰圆角底（行内缩边留呼吸位）。
    if (IsHover())
    {
        Gdiplus::GraphicsPath hp;
        AddRoundRect(hp, (Gdiplus::REAL)(x + 3), (Gdiplus::REAL)(y + 2),
                     (Gdiplus::REAL)(w - 6), (Gdiplus::REAL)(h - 4), kHoverRadius);
        Gdiplus::SolidBrush hb(kHoverFill);
        g.FillPath(&hb, &hp);
    }

    // 类型角标（圆角方块，纵向居中），内嵌扩展名大写文字。
    const int badgeX = x + kEdgePad;
    const int badgeY = y + (h - kBadgeSize) / 2;
    {
        Gdiplus::GraphicsPath bp;
        AddRoundRect(bp, (Gdiplus::REAL)badgeX, (Gdiplus::REAL)badgeY,
                     (Gdiplus::REAL)kBadgeSize, (Gdiplus::REAL)kBadgeSize, kBadgeRadius);
        Gdiplus::SolidBrush bb(Gdiplus::Color(255,
            GetRValue(m_typeColor), GetGValue(m_typeColor), GetBValue(m_typeColor)));
        g.FillPath(&bb, &bp);

        // 角标文字字号随长度调整：4 字符偏小、≤2 字符偏大，保证不溢出。
        int badgePx = 9;
        const int labelLen = m_typeLabel.GetLength();
        if (labelLen >= 4)
        {
            badgePx = 7;
        }
        else if (labelLen <= 2)
        {
            badgePx = 11;
        }
        RECT badgeRc = { badgeX, badgeY, badgeX + kBadgeSize, badgeY + kBadgeSize };
        DrawGdiText(hdc, (LPCWSTR)m_typeLabel, badgeRc, badgePx, true, kBadgeText,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    }

    // 下载区（右侧）：传输中（上传 / 下载）统一显示 ✕ 取消按钮（点它取消该笔）；否则显示
    // 下载箭头。进度以「说明行 NN% + 行底进度条」呈现，故此处只需 ✕ 或箭头。
    const RECT dlRc = DownloadHitRect();
    if (m_uploading || m_downloading)
    {
        // ✕ 取消按钮：常态灰、悬停红（并叠淡红圆底）。
        const COLORREF xClr = m_downloadHover ? RGB(0xE5, 0x4D, 0x42) : kDlColor;
        const int cx = (dlRc.left + dlRc.right) / 2;
        const int cy = (dlRc.top + dlRc.bottom) / 2;
        if (m_downloadHover)
        {
            const int d = kDownloadGlyph + 10;
            Gdiplus::SolidBrush hb(Gdiplus::Color(255, 0xFF, 0xEC, 0xEA));   // 淡红悬停底
            g.FillEllipse(&hb, cx - d / 2, cy - d / 2, d, d);
        }
        Gdiplus::Pen pen(Gdiplus::Color(255,
            GetRValue(xClr), GetGValue(xClr), GetBValue(xClr)), 1.6f);
        const int r = kDownloadGlyph / 2 - 2;
        g.DrawLine(&pen, cx - r, cy - r, cx + r, cy + r);
        g.DrawLine(&pen, cx - r, cy + r, cx + r, cy - r);
    }
    else
    {
        const COLORREF arrowClr = m_downloadHover ? kDlHoverColor : kDlColor;
        const int cx = (dlRc.left + dlRc.right) / 2;
        const int cy = (dlRc.top + dlRc.bottom) / 2;

        if (m_downloadHover)
        {
            // 悬停底：淡橙圆形，直径略大于箭头。
            const int d = kDownloadGlyph + 10;
            Gdiplus::SolidBrush hb(kDlHoverFill);
            g.FillEllipse(&hb, cx - d / 2, cy - d / 2, d, d);
        }

        // 箭头：竖杆 + 下箭头 + 底部托盘横线（"下载到本地"语义）。
        Gdiplus::Pen pen(Gdiplus::Color(255,
            GetRValue(arrowClr), GetGValue(arrowClr), GetBValue(arrowClr)), 1.6f);
        const int half = kDownloadGlyph / 2;
        const int shaftTop = cy - half + 1;
        const int shaftBot = cy + half - 4;        // 箭头尖（竖杆下端）
        // 竖杆
        g.DrawLine(&pen, cx, shaftTop, cx, shaftBot);
        // 箭头左右两撇
        const int ah = 5;   // 箭头臂长
        g.DrawLine(&pen, cx, shaftBot, cx - ah, shaftBot - ah);
        g.DrawLine(&pen, cx, shaftBot, cx + ah, shaftBot - ah);
        // 底部托盘横线
        const int trayY = cy + half;
        g.DrawLine(&pen, cx - half + 1, trayY, cx + half - 1, trayY);
    }

    // 垃圾桶图标：可删行（群主 / 管理员 / 上传者本人）+ 鼠标悬停本行时，在下载箭头左侧
    // 显示；点它删除。常态灰、悬停红（叠淡红圆底）。普通态才画（上传中 / 下载中不画）。
    if (TrashActive() && IsHover())
    {
        const RECT trRc = TrashHitRect();
        const COLORREF tClr = m_deleteHover ? RGB(0xE5, 0x4D, 0x42) : kDlColor;
        const int tcx = (trRc.left + trRc.right) / 2;
        const int tcy = (trRc.top + trRc.bottom) / 2;
        if (m_deleteHover)
        {
            const int d = kTrashGlyph + 12;
            Gdiplus::SolidBrush hb(Gdiplus::Color(255, 0xFF, 0xEC, 0xEA));   // 淡红悬停底
            g.FillEllipse(&hb, tcx - d / 2, tcy - d / 2, d, d);
        }
        Gdiplus::Pen pen(Gdiplus::Color(255,
            GetRValue(tClr), GetGValue(tClr), GetBValue(tClr)), 1.5f);
        const int hw  = kTrashGlyph / 2;     // 半宽
        const int top = tcy - hw + 2;        // 桶盖 y
        // 桶盖横线 + 上方提手
        g.DrawLine(&pen, tcx - hw, top, tcx + hw, top);
        g.DrawLine(&pen, tcx - 3, top - 3, tcx + 3, top - 3);
        g.DrawLine(&pen, tcx - 3, top - 3, tcx - 3, top);
        g.DrawLine(&pen, tcx + 3, top - 3, tcx + 3, top);
        // 桶身两侧 + 桶底
        const int bodyTop = top + 2;
        const int bodyBot = tcy + hw;
        g.DrawLine(&pen, tcx - hw + 2, bodyTop, tcx - hw + 3, bodyBot);
        g.DrawLine(&pen, tcx + hw - 2, bodyTop, tcx + hw - 3, bodyBot);
        g.DrawLine(&pen, tcx - hw + 3, bodyBot, tcx + hw - 3, bodyBot);
        // 内部两道竖线
        g.DrawLine(&pen, tcx - 2, bodyTop + 1, tcx - 2, bodyBot - 1);
        g.DrawLine(&pen, tcx + 2, bodyTop + 1, tcx + 2, bodyBot - 1);
    }

    // 文字块（角标右侧、下载区左侧之间）：文件名（上）+ 说明（下），整体纵向居中。
    // 可删行始终为垃圾桶预留出其命中区宽度（即便此刻未悬停未画），避免悬停时文本回流抖动。
    const int textLeft  = badgeX + kBadgeSize + kBadgeGap;
    const int rightZoneLeft = TrashActive() ? TrashHitRect().left : dlRc.left;
    const int textRight = rightZoneLeft - 6;
    if (textRight > textLeft)
    {
        const int nameH = kNamePx + 4;
        const int metaH = kMetaPx + 3;
        const int blockH = nameH + kTextLineGap + metaH;
        int blockTop = y + (h - blockH) / 2;

        RECT nameRc = { textLeft, blockTop, textRight, blockTop + nameH };
        DrawGdiText(hdc, (LPCWSTR)m_name, nameRc, kNamePx, true, kNameColor,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // 说明行：上传中显示「上传中 NN%」、下载中显示「下载中 NN%」，否则显示「上传者·时间·大小」。
        CString metaText = m_metaLine;
        if (m_uploading)
        {
            metaText.Format(_T("上传中 %d%%"), m_uploadPercent);
        }
        else if (m_downloading)
        {
            metaText.Format(_T("下载中 %d%%"), m_downloadPercent);
        }
        const COLORREF metaClr = (m_uploading || m_downloading) ? kDlHoverColor : kMetaColor;
        RECT metaRc = { textLeft, blockTop + nameH + kTextLineGap,
                        textRight, blockTop + nameH + kTextLineGap + metaH };
        DrawGdiText(hdc, (LPCWSTR)metaText, metaRc, kMetaPx, false, metaClr,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    // 上传中 / 下载中：行底叠一条细进度条（淡灰底 + 橙色填充到当前百分比）。
    if (m_downloading || m_uploading)
    {
        const int pct     = m_uploading ? m_uploadPercent : m_downloadPercent;
        const int barH    = 3;
        const int barLeft = badgeX;                       // 与角标左对齐
        const int barRight = m_rcItem.right - kEdgePad;
        const int barTop  = m_rcItem.bottom - barH - 2;   // 贴近行底
        if (barRight > barLeft)
        {
            Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, 0xEC, 0xEC, 0xEC));
            g.FillRectangle(&trackBrush, barLeft, barTop, barRight - barLeft, barH);
            const int fillW = (barRight - barLeft) * pct / 100;
            if (fillW > 0)
            {
                Gdiplus::SolidBrush fillBrush(Gdiplus::Color(255, 0xFF, 0x7A, 0x45));
                g.FillRectangle(&fillBrush, barLeft, barTop, fillW, barH);
            }
        }
    }
}

bool GroupFileItem::OnMouseMove(POINT pt, UINT /*mkFlags*/)
{
    // 右侧动作区命中 → 悬停态；变化时重绘（高亮 + 后续手型）。该区在普通态是下载箭头、
    // 传输中（上传 / 下载）是 ✕ 取消按钮，两者都可点，故都进入悬停。
    const RECT dlRc = DownloadHitRect();
    const bool over = (::PtInRect(&dlRc, pt) != FALSE);
    if (over != m_downloadHover)
    {
        m_downloadHover = over;
        Invalidate();
    }
    // 垃圾桶命中区（仅可删 + 普通态）悬停态。
    bool overTrash = false;
    if (TrashActive())
    {
        const RECT trRc = TrashHitRect();
        overTrash = (::PtInRect(&trRc, pt) != FALSE);
    }
    if (overTrash != m_deleteHover)
    {
        m_deleteHover = overTrash;
        Invalidate();
    }
    return false;   // 不拦截，行悬停等仍交基类/宿主处理
}

bool GroupFileItem::OnMouseLeave()
{
    if (m_downloadHover || m_deleteHover)
    {
        m_downloadHover = false;
        m_deleteHover   = false;
        Invalidate();
    }
    return false;
}

bool GroupFileItem::OnSetCursor(POINT pt)
{
    // 垃圾桶命中区（可删 + 普通态）→ 手型。
    if (TrashActive())
    {
        const RECT trRc = TrashHitRect();
        if (::PtInRect(&trRc, pt))
        {
            ::SetCursor(::LoadCursor(NULL, IDC_HAND));
            return true;
        }
    }
    const RECT dlRc = DownloadHitRect();
    if (::PtInRect(&dlRc, pt))
    {
        // 传输中 ✕ 取消 / 常态下载箭头，均可点 → 手型。
        ::SetCursor(::LoadCursor(NULL, IDC_HAND));
        return true;
    }
    return false;   // 其余区域用普通箭头
}

bool GroupFileItem::OnLButtonDown(POINT pt, UINT /*mkFlags*/)
{
    const RECT dlRc = DownloadHitRect();

    // 可删行：点下载箭头左侧的垃圾桶 → 把 entryid（堆上 CString）投给宿主群聊窗，由其
    // 弹确认框并发起删除（宿主处理后 delete）。仅普通态（TrashActive）才命中。
    if (TrashActive())
    {
        const RECT trRc = TrashHitRect();
        if (::PtInRect(&trRc, pt))
        {
            balloonwjui::DuiHost* host = GetHost();
            HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
            if (hwnd != NULL && !m_entryid.IsEmpty())
            {
                CString* p = new CString(m_entryid);
                if (!::PostMessage(hwnd, WM_GROUP_FILE_DELETE, 0, (LPARAM)p))
                {
                    delete p;   // 投递失败（宿主已销毁）自行回收，杜绝泄漏
                }
            }
            return true;
        }
    }

    // 传输中：点右侧 ✕ → 取消本笔。上传按 uploadId（int 直接装 LPARAM），下载按 entryid
    // （堆上 CString，宿主处理后 delete）。点传输行的其它位置不触发任何动作。
    if (m_uploading || m_downloading)
    {
        if (::PtInRect(&dlRc, pt))
        {
            balloonwjui::DuiHost* host = GetHost();
            HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
            if (hwnd != NULL)
            {
                if (m_uploading)
                {
                    ::PostMessage(hwnd, WM_GROUP_FILE_UPLOAD_CANCEL, 0, (LPARAM)m_uploadId);
                }
                else if (!m_entryid.IsEmpty())
                {
                    CString* p = new CString(m_entryid);
                    if (!::PostMessage(hwnd, WM_GROUP_FILE_DOWNLOAD_CANCEL, 0, (LPARAM)p))
                    {
                        delete p;   // 投递失败（宿主已销毁）自行回收，杜绝泄漏
                    }
                }
            }
            return true;
        }
        return false;
    }

    if (::PtInRect(&dlRc, pt))
    {
        // 点击下载箭头：把本行 entryid（堆上 CString）投递给宿主群聊窗，由其查库取完整
        // 条目并发起下载（宿主处理后 delete）。本控件不直接联网。
        balloonwjui::DuiHost* host = GetHost();
        HWND hwnd = (host != NULL) ? host->m_hWnd : NULL;
        if (hwnd != NULL && !m_entryid.IsEmpty())
        {
            CString* p = new CString(m_entryid);
            if (!::PostMessage(hwnd, WM_GROUP_FILE_DOWNLOAD, 0, (LPARAM)p))
            {
                delete p;   // 投递失败（宿主已销毁）自行回收，杜绝泄漏
            }
        }
        return true;
    }
    return false;
}

} // namespace balloonchatwnd
