/**
 *  flamingoAdmin —— 客户端版本界面 ClientVersionsScreen 实现。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#include "stdafx.h"
#include "Screens/ClientVersionsScreen.h"
#include "UiIds.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/Widgets.h"
#include "Widgets/CommonWidgets.h"   // ui::DrawCard
#include "DuiNotify.h"
#include "Widgets/OverlayScrollbar.h"
#include "Controls/Basic/DuiButton.h"

using namespace balloonwjui;

namespace flamingoadmin {

namespace {

// ---- 布局常量（96 dpi 逻辑像素）----
const int kHeaderH   = 96;   // 页头高度
const int kSidePad   = 24;   // 内容区左 / 右 / 下统一外边距
//提示条高度。条内排两行：第一行讲升级包放哪儿，第二行讲"发布后不要再动这个包"
//（见 OnPaint 里的说明）。两行各 20 像素，加上下各 8 像素内边距。
const int kHintH     = 64;
const int kHintLineH = 20;   // 提示条内单行文字行高
const int kHintPadY  = 8;    // 提示条内上 / 下内边距
const int kRowH      = 48;   // 数据行高
const int kTableHdrH = 38;   // 表头行高

const int kEyebrowPt   = 8;   // 页头小标题字号（磅）
const int kPageTitlePt = 17;  // 页头大标题字号（磅）

// 悬浮滚动条距表格区右边缘的内缩间距（像素）。
const int kSbInset = 3;

// 页头按钮尺寸。
const int kBtnW = 88;   // 按钮宽度
const int kBtnH = 34;   // 按钮高度
const int kBtnGap = 8;  // 两个按钮之间的间距

// 表格各列相对表格区左边的偏移。最后一列「操作」右对齐，不在此列。
const int kColVersion = 16;   // 版本号
const int kColMinVer  = 140;  // 最低版本
const int kColSize    = 250;  // 包大小
const int kColTime    = 350;  // 发布时间
const int kColStatus  = 520;  // 状态

// 行内操作按钮的尺寸与间距。
const int kActBtnW = 56;   // 单个操作按钮宽度
const int kActBtnH = 26;   // 单个操作按钮高度
const int kActGap  = 6;    // 操作按钮之间的间距

// 一行最多几个操作按钮（发布/撤回、编辑）。用于算「操作」列的宽度。
const int kMaxActions = 3;

// 平台号 → 中文。取值见服务端 Protocol.h 的 enum platform_type。
LPCTSTR PlatformText(int platform)
{
    switch (platform)
    {
    case 1:
        return _T("Windows PC");
    case 2:
        return _T("Android");
    case 3:
        return _T("iOS");
    case 4:
        return _T("macOS");
    case 5:
        return _T("Web");
    default:
        return _T("未知");
    }
}

// 把 Unix 毫秒格式化成 "YYYY-MM-DD HH:MM"。取 0 表示还没发布过，显示为 "—"。
CString TimeText(long long unixMs)
{
    if (unixMs <= 0)
    {
        return CString(_T("—"));
    }

    const time_t seconds = static_cast<time_t>(unixMs / 1000);
    struct tm parts;
    if (localtime_s(&parts, &seconds) != 0)
    {
        return CString(_T("—"));
    }

    CString text;
    text.Format(_T("%04d-%02d-%02d %02d:%02d"),
                parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
                parts.tm_hour, parts.tm_min);
    return text;
}

} // namespace

ClientVersionsScreen::ClientVersionsScreen()
    : m_selected(-1)
    , m_scroll(0)
    , m_scrollMax(0)
    , m_scrollbar(nullptr)
    , m_btnRefresh(nullptr)
    , m_btnNew(nullptr)
{
    // 行内操作经本控件的 ctrlId 上报 AdminFrame，故给本控件设 ctrlId。
    SetCtrlId(kIdCvRowAction);
    ::SetRectEmpty(&m_tableRc);

    auto refreshBtn = std::make_unique<DuiButton>();
    refreshBtn->SetCtrlId(kIdCvRefresh);
    refreshBtn->SetText(_T("刷新"));
    refreshBtn->SetVariant(DuiButton::Variant::Default);
    refreshBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnRefresh = refreshBtn.get();
    AddChild(std::move(refreshBtn));

    auto newBtn = std::make_unique<DuiButton>();
    newBtn->SetCtrlId(kIdCvNew);
    newBtn->SetText(_T("新建版本"));
    newBtn->SetVariant(DuiButton::Variant::Primary);
    newBtn->SetTextPointSize(theme::kFontSizeNormal);
    m_btnNew = newBtn.get();
    AddChild(std::move(newBtn));

    auto sb = std::make_unique<OverlayScrollbar>();
    m_scrollbar = sb.get();
    AddChild(std::move(sb));
}

void ClientVersionsScreen::SetVersions(const std::vector<adminapi::ClientVersionItem>& list)
{
    m_list = list;
    // 换一批数据后原来的选中行下标多半已经指向别的记录，直接清掉
    m_selected = m_list.empty() ? -1 : 0;
    m_scroll = 0;
    ClampScroll();
}

void ClientVersionsScreen::SetPackageDir(const CString& dir)
{
    m_dir = dir;
}

CvPendingAction ClientVersionsScreen::TakePendingAction()
{
    const CvPendingAction taken = m_pending;
    m_pending = CvPendingAction();
    return taken;
}

const adminapi::ClientVersionItem& ClientVersionsScreen::GetSelected() const
{
    if (m_selected >= 0 && m_selected < static_cast<int>(m_list.size()))
    {
        return m_list[m_selected];
    }
    return m_empty;
}

void ClientVersionsScreen::ClampScroll()
{
    const int viewH = (m_tableRc.bottom - m_tableRc.top) - kTableHdrH;
    const int contentH = static_cast<int>(m_list.size()) * kRowH;
    m_scrollMax = (contentH > viewH) ? (contentH - viewH) : 0;
    if (m_scroll > m_scrollMax)
    {
        m_scroll = m_scrollMax;
    }
    if (m_scroll < 0)
    {
        m_scroll = 0;
    }
}

void ClientVersionsScreen::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;

    // 页头右侧两个按钮：「新建版本」在最右，「刷新」在它左边。
    const int cy = rcAvail.top + 52;
    RECT rcNew = { rcAvail.right - kSidePad - kBtnW, cy - kBtnH / 2,
                   rcAvail.right - kSidePad,         cy + kBtnH / 2 };
    RECT rcRefresh = { rcNew.left - kBtnGap - kBtnW, cy - kBtnH / 2,
                       rcNew.left - kBtnGap,         cy + kBtnH / 2 };

    if (m_btnRefresh != nullptr)
    {
        m_btnRefresh->ForceLayout(rcRefresh);
    }
    if (m_btnNew != nullptr)
    {
        m_btnNew->ForceLayout(rcNew);
    }

    const int bodyL = rcAvail.left + kSidePad;
    const int bodyR = rcAvail.right - kSidePad;
    const int bodyT = rcAvail.top + kHeaderH + kHintH + 12;
    const int bodyB = rcAvail.bottom - kSidePad;
    m_tableRc = RECT{ bodyL, bodyT, bodyR, bodyB };

    if (m_scrollbar != nullptr)
    {
        m_scrollbar->ForceLayout(RECT{
            m_tableRc.right - kSbInset - OverlayScrollbar::kSuggestedWidth,
            m_tableRc.top + kTableHdrH,
            m_tableRc.right - kSbInset,
            m_tableRc.bottom });
    }

    ClampScroll();
}

void ClientVersionsScreen::OnPaint(HDC hdc, const RECT& rcDirty)
{
    const RECT rc = m_rcItem;

    // ---- 页头 ----
    ui::DrawTextLine(hdc, _T("客户端版本"),
                     RECT{ rc.left + kSidePad, rc.top + 22,
                           rc.left + kSidePad + 300, rc.top + 38 },
                     theme::kFlamingo600, ui::UiFont(kEyebrowPt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    ui::DrawTextLine(hdc, _T("客户端升级发布"),
                     RECT{ rc.left + kSidePad, rc.top + 38,
                           rc.left + kSidePad + 400, rc.top + 68 },
                     theme::kInk700, ui::UiFont(kPageTitlePt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    ui::DrawTextLine(hdc, _T("发布 PC 客户端升级包，控制发布与撤回"),
                     RECT{ rc.left + kSidePad, rc.top + 68,
                           rc.left + kSidePad + 600, rc.top + 88 },
                     theme::kInk400, ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // ---- 提示条：升级包该放到哪儿、发布后为什么不能再动它 ----
    // 控制台不负责上传，包由运维手工放进服务器目录，第一行是唯一的指路牌，
    // 没有它管理员会不知道"新建版本"里的候选包从哪来。
    //
    // 第二行讲的是发布记录与包文件之间的绑定关系，这一层不写出来管理员多半想不到：
    // 发布时存进数据库的是包内容的 MD5，客户端下载时服务端也是拿 MD5 反查文件，
    // 文件名全程不参与。所以只要重新打了一次包（哪怕文件名一模一样、内容只差一个
    // 字节），MD5 就变了，旧发布记录里那个 MD5 在目录里再也找不到对应文件，客户端
    // 下载会拿到"文件不存在"。换包的正确做法是撤回该版本、重新发布一次。
    const RECT hint = { rc.left + kSidePad, rc.top + kHeaderH,
                        rc.right - kSidePad, rc.top + kHeaderH + kHintH - 8 };
    ui::FillSolidRect(hdc, hint, theme::kFlamingo50);

    CString hintText;
    if (m_dir.IsEmpty())
    {
        hintText = _T("升级包目录尚未获取 —— 点「新建版本」会自动扫描一次");
    }
    else
    {
        hintText.Format(_T("把升级包（.zip）放到服务器的 %s 目录下，再点「新建版本」选择它"),
                        m_dir.GetString());
    }

    HFONT hintFont = ui::UiFont(theme::kFontSizeSmall, false);
    const int line1T = hint.top + kHintPadY;
    ui::DrawTextLine(hdc, hintText,
                     RECT{ hint.left + 12, line1T, hint.right - 12, line1T + kHintLineH },
                     theme::kInk700, hintFont,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    const int line2T = line1T + kHintLineH;
    ui::DrawTextLine(hdc,
                     _T("发布记录绑定的是包内容的 MD5，不是文件名 —— 发布后重新打包会让 MD5 变化，")
                     _T("客户端将下载失败。要换包请先撤回该版本，再重新发布一次。"),
                     RECT{ hint.left + 12, line2T, hint.right - 12, line2T + kHintLineH },
                     theme::kInk400, hintFont,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // ---- 表格 ----
    ui::DrawCard(hdc, m_tableRc);
    PaintTable(hdc, m_tableRc);

    // 子控件（两个页头按钮 + 悬浮滚动条）画在最上层
    balloonwjui::DuiControl::OnPaint(hdc, rcDirty);
}

void ClientVersionsScreen::PaintTable(HDC hdc, const RECT& rc)
{
    m_hits.clear();

    // ---- 表头 ----
    struct Col
    {
        LPCTSTR m_text;
        int     m_x;
    };
    const Col cols[] = {
        { _T("版本号"),   kColVersion },
        { _T("最低版本"), kColMinVer  },
        { _T("包大小"),   kColSize    },
        { _T("发布时间"), kColTime    },
        { _T("状态"),     kColStatus  },
    };

    HFONT hdrFont = ui::UiFont(theme::kFontSizeSmall, false);
    for (int i = 0; i < static_cast<int>(sizeof(cols) / sizeof(cols[0])); ++i)
    {
        const RECT r = { rc.left + cols[i].m_x, rc.top,
                         rc.left + cols[i].m_x + 200, rc.top + kTableHdrH };
        ui::DrawTextLine(hdc, cols[i].m_text, r, theme::kInk400, hdrFont,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    // 「操作」列表头右对齐，与行内按钮列对齐
    const int actionsW = kActBtnW * kMaxActions + kActGap * (kMaxActions - 1);
    ui::DrawTextLine(hdc, _T("操作"),
                     RECT{ rc.right - 16 - actionsW, rc.top,
                           rc.right - 16, rc.top + kTableHdrH },
                     theme::kInk400, hdrFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    ui::FillLine(hdc, rc.left, rc.top + kTableHdrH - 1,
                 rc.right - rc.left, 1, theme::kInk100);

    const RECT rowsArea = { rc.left, rc.top + kTableHdrH, rc.right, rc.bottom };

    // ---- 空态 ----
    if (m_list.empty())
    {
        ui::DrawTextLine(hdc, _T("还没有发布过任何客户端版本"), rowsArea,
                         theme::kInk400, ui::UiFont(theme::kFontSizeNormal, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    // ---- 数据行 ----
    const int oldClip = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, rowsArea.left, rowsArea.top, rowsArea.right, rowsArea.bottom);

    HFONT rowFont = ui::UiFont(theme::kFontSizeSmall, false);
    HFONT boldFont = ui::UiFont(theme::kFontSizeSmall, true);

    for (int i = 0; i < static_cast<int>(m_list.size()); ++i)
    {
        const int top = rowsArea.top + i * kRowH - m_scroll;
        if (top + kRowH < rowsArea.top || top > rowsArea.bottom)
        {
            continue;   //完全在可视区外的行不画，也不建命中区
        }

        const adminapi::ClientVersionItem& v = m_list[i];
        const RECT row = { rc.left, top, rc.right, top + kRowH };

        if (i == m_selected)
        {
            ui::FillSolidRect(hdc, row, theme::kFlamingo50);
            ui::FillSolidRect(hdc, RECT{ rc.left, top, rc.left + 3, top + kRowH },
                              theme::kFlamingo500);
        }
        ui::FillLine(hdc, rc.left + 16, row.bottom - 1,
                     rc.right - rc.left - 32, 1, theme::kInk100);

        // 版本号（当前生效的那条加粗）+ 平台
        CString versionText;
        versionText.Format(_T("%s  ·  %s"), v.m_version.GetString(),
                           PlatformText(v.m_platform));
        ui::DrawTextLine(hdc, versionText,
                         RECT{ rc.left + kColVersion, top, rc.left + kColMinVer - 8, row.bottom },
                         theme::kInk700, v.m_isCurrent ? boldFont : rowFont,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        ui::DrawTextLine(hdc, v.m_minVersion.IsEmpty() ? _T("—") : v.m_minVersion.GetString(),
                         RECT{ rc.left + kColMinVer, top, rc.left + kColSize - 8, row.bottom },
                         theme::kInk400, rowFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        ui::DrawTextLine(hdc, adminapi::FormatPackageSize(v.m_size),
                         RECT{ rc.left + kColSize, top, rc.left + kColTime - 8, row.bottom },
                         theme::kInk400, rowFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        ui::DrawTextLine(hdc, TimeText(v.m_publishTime),
                         RECT{ rc.left + kColTime, top, rc.left + kColStatus - 8, row.bottom },
                         theme::kInk400, rowFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // 状态：当前生效 / 已发布 / 草稿
        LPCTSTR statusText = _T("草稿");
        COLORREF statusColor = theme::kInk400;
        if (v.m_isCurrent)
        {
            statusText = _T("当前生效");
            statusColor = theme::kColorOk;
        }
        else if (v.m_enabled == 1)
        {
            statusText = _T("已发布");
            statusColor = theme::kInk700;
        }
        ui::DrawTextLine(hdc, statusText,
                         RECT{ rc.left + kColStatus, top,
                               rc.right - 16 - actionsW - 8, row.bottom },
                         statusColor, v.m_isCurrent ? boldFont : rowFont,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // ---- 行内操作按钮 ----
        // 草稿：发布 / 编辑；已发布：撤回 / 编辑。
        // 把某个旧版本重新顶成"当前生效"用「撤回 + 发布」两步 —— 发布会把发布时间
        // 刷到当前，取数规则按发布时间取最新，效果与从前那个「回滚」按钮完全一样。
        struct Act
        {
            LPCTSTR     m_text;
            CvRowAction m_action;
        };
        Act acts[kMaxActions];
        int actCount = 0;
        if (v.m_enabled != 1)
        {
            acts[actCount].m_text = _T("发布");
            acts[actCount].m_action = CvRowAction::Publish;
            ++actCount;
        }
        else
        {
            acts[actCount].m_text = _T("撤回");
            acts[actCount].m_action = CvRowAction::Disable;
            ++actCount;
        }
        acts[actCount].m_text = _T("编辑");
        acts[actCount].m_action = CvRowAction::Edit;
        ++actCount;

        int bx = rc.right - 16 - actionsW;
        const int by = top + (kRowH - kActBtnH) / 2;
        for (int a = 0; a < actCount; ++a)
        {
            const RECT br = { bx, by, bx + kActBtnW, by + kActBtnH };
            ui::FillSolidRect(hdc, br, theme::kInk50);
            ui::DrawTextLine(hdc, acts[a].m_text, br, theme::kInk700, rowFont,
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            Hit hit;
            hit.m_rc = br;
            hit.m_action = acts[a].m_action;
            hit.m_id = v.m_id;
            m_hits.push_back(hit);

            bx += kActBtnW + kActGap;
        }

        // 整行（操作按钮之外的部分）作为选中命中区
        Hit rowHit;
        rowHit.m_rc = RECT{ rc.left, top, rc.right - 16 - actionsW - 8, row.bottom };
        rowHit.m_action = CvRowAction::None;
        rowHit.m_id = v.m_id;
        m_hits.push_back(rowHit);
    }

    ::RestoreDC(hdc, oldClip);
}

bool ClientVersionsScreen::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    // 不要在这里调基类 DuiControl::OnLButtonUp —— 它对落在本控件矩形内的<u>任何</u>
    // 点击都返回 true 并发一条空的 DUIN_CLICK（DuiControl.cpp:268-276），于是下面的
    // 命中判定永远走不到，表格里的行内操作按钮全都点不动。子控件（页头按钮、滚动条）
    // 的点击由宿主经 HitTest 直接派发给它们自己，不经过本函数，故无需在此转交。
    for (size_t i = 0; i < m_hits.size(); ++i)
    {
        if (!::PtInRect(&m_hits[i].m_rc, pt))
        {
            continue;
        }

        // 选中行：只改高亮，不上报
        if (m_hits[i].m_action == CvRowAction::None)
        {
            for (int r = 0; r < static_cast<int>(m_list.size()); ++r)
            {
                if (m_list[r].m_id == m_hits[i].m_id)
                {
                    m_selected = r;
                    break;
                }
            }
            Invalidate();
            return true;
        }

        // 行内操作：暂存后上报，由 AdminFrame 取走并发请求
        m_pending.m_action = m_hits[i].m_action;
        m_pending.m_id = m_hits[i].m_id;
        for (int r = 0; r < static_cast<int>(m_list.size()); ++r)
        {
            if (m_list[r].m_id == m_hits[i].m_id)
            {
                m_selected = r;
                break;
            }
        }
        Invalidate();
        NotifyParent(DUIN_CLICK);
        return true;
    }

    return false;
}

bool ClientVersionsScreen::OnMouseWheel(POINT pt, short zDelta, UINT /*mkFlags*/)
{
    if (!::PtInRect(&m_tableRc, pt) || m_scrollMax <= 0)
    {
        return false;
    }

    // 一格滚轮走三行，与工程内其它列表一致
    m_scroll -= (zDelta / WHEEL_DELTA) * kRowH * 3;
    ClampScroll();

    if (m_scrollbar != nullptr)
    {
        m_scrollbar->SetMetrics(static_cast<int>(m_list.size()) * kRowH, m_scroll);
        m_scrollbar->Flash();
    }

    Invalidate();
    return true;
}

bool ClientVersionsScreen::OnSetCursor(POINT pt)
{
    for (size_t i = 0; i < m_hits.size(); ++i)
    {
        if (m_hits[i].m_action != CvRowAction::None && ::PtInRect(&m_hits[i].m_rc, pt))
        {
            ::SetCursor(::LoadCursor(NULL, IDC_HAND));
            return true;
        }
    }
    return false;
}

} // namespace flamingoadmin
