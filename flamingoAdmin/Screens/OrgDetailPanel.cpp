/**
 *  flamingoAdmin —— 组织详情面板 OrgDetailPanel 实现。
 *
 *  balloonwj@qq.com   2026.05.21
 */
#include "stdafx.h"
#include "Screens/OrgDetailPanel.h"
#include "Theme/AdminTheme.h"
#include "Widgets/UiHelpers.h"
#include "Widgets/IconRenderer.h"
#include "Widgets/CommonWidgets.h"
#include "Widgets/OverlayScrollbar.h"   // 面板右缘的悬浮 auto-hide 滚动条子控件
#include "Controls/Input/DuiEditHost.h" // 基本信息各值字段：只读 DuiEditHost
#include "DuiHost.h"                    // GetHost()->m_hWnd（EnsureCreated 的父 HWND）
#include "Mock/DataStore.h"

#include <windowsx.h>                   // GET_X_LPARAM / GET_Y_LPARAM
#include <commctrl.h>                   // SetWindowSubclass / DefSubclassProc
#pragma comment(lib, "comctl32.lib")

namespace flamingoadmin {

// 本文件在 namespace flamingoadmin 内，theme:: 直接解析到 flamingoadmin::theme。

namespace {

// ---- 详情面板布局常量（96 dpi 逻辑像素）----
const int kSidePad     = 24;   // 内容区左 / 右内距
const int kTopPad      = 20;   // 内容区顶部内距
const int kBottomPad   = 32;   // 内容区底部内距
const int kCardPad     = 20;   // 卡片内距
const int kSectionGap  = 18;   // 区块间距
const int kHeadCardH   = 104;  // 资料头卡片高度
const int kHeadAvatar  = 60;   // 资料头头像 / 图标边长
const int kBtnH        = 32;   // 操作按钮高度
const int kRowH        = 30;   // 滚轮一格滚动量参考行高

// 基本信息值字段 EDIT 的可视高度（像素）。单行 EDIT 文字顶对齐，给它一个
// 约一行文字高、在自绘圆角框内垂直居中的子矩形，使文字视觉上居中（对齐
// 自绘兜底的 DT_VCENTER）。框内高 34，取 22 居中可容纳 9pt 微软雅黑而不裁切。
const int kBasicEditH  = 22;

// 基本信息值字段 EDIT 滚轮转发子类过程的 uIdSubclass（唯一标识，避免与
// balloonui 内部子类 id 冲突）。
const UINT_PTR kFieldWheelSubclassId = 0xC001;

const int kBigTitlePt  = 16;   // 资料头名称字号
const int kTabBarH     = 38;   // 员工详情 tab 条高度

// 员工详情 6 个 tab 的名称。
const TCHAR* const kUserTabs[] =
{
    _T("基本信息"), _T("权限与角色"), _T("服务号"),
    _T("消息与会话"), _T("设备登录"), _T("操作历史"),
};
const int kUserTabCount = sizeof(kUserTabs) / sizeof(kUserTabs[0]);

// 量算文字宽度。
int MeasureW(LPCTSTR text, HFONT font)
{
    HDC   dc = ::GetDC(nullptr);
    HFONT of = static_cast<HFONT>(::SelectObject(dc, font));
    SIZE  sz = { 0, 0 };
    ::GetTextExtentPoint32(dc, text, lstrlen(text), &sz);
    ::SelectObject(dc, of);
    ::ReleaseDC(nullptr, dc);
    return sz.cx;
}

// 画一个区块小标题（大写灰字）。
void DrawSectionTitle(HDC hdc, int x, int y, int w, LPCTSTR text, bool danger)
{
    RECT rc = { x, y, x + w, y + 18 };
    ui::DrawTextLine(hdc, text, rc,
                     danger ? theme::kColorDanger : theme::kInk400,
                     ui::UiFont(theme::kFontSizeSmall, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// 基本信息字段数：与 PaintUserBasic 内 fields[] 数组**同步**维护。
// 改字段数量时必须同时改这里 + fields[] + RenderBasicField 调用点。
// 构造函数按此数量创建只读 DuiEditHost 值字段子控件。
constexpr int kBasicFieldCount = 11;

// 画一个「标签在上、值在下框内」的字段单元格。
void DrawKVCell(HDC hdc, int x, int y, int w, LPCTSTR label, LPCTSTR value)
{
    RECT rcLabel = { x, y, x + w, y + 16 };
    ui::DrawTextLine(hdc, label, rcLabel, theme::kInk400,
                     ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT rcBox = { x, y + 18, x + w, y + 18 + 34 };
    ui::FillRoundRect(hdc, rcBox, theme::kInk0, theme::kRadiusSm);
    ui::StrokeRoundRect(hdc, rcBox, theme::kInk100, theme::kRadiusSm, 1.0f);
    RECT rcVal = { rcBox.left + 10, rcBox.top, rcBox.right - 10, rcBox.bottom };
    const bool empty = (value == nullptr || value[0] == _T('\0'));
    ui::DrawTextLine(hdc, empty ? _T("—") : value, rcVal,
                     empty ? theme::kInk400 : theme::kInk700,
                     ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                     | DT_END_ELLIPSIS);
}

// 试点接入辅助：画 KV 单元格的 label + 空白圆角框，但**不画 value**。
// 返回 value 文本应当落位的 rcVal（caller 决定用只读 EDIT 子控件或自绘兜底）。
// 与 DrawKVCell 共享同一布局常量，确保两路视觉一致。
RECT DrawKVCellLabelAndBoxOnly(HDC hdc, int x, int y, int w, LPCTSTR label)
{
    RECT rcLabel = { x, y, x + w, y + 16 };
    ui::DrawTextLine(hdc, label, rcLabel, theme::kInk400,
                     ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT rcBox = { x, y + 18, x + w, y + 18 + 34 };
    ui::FillRoundRect(hdc, rcBox, theme::kInk0, theme::kRadiusSm);
    ui::StrokeRoundRect(hdc, rcBox, theme::kInk100, theme::kRadiusSm, 1.0f);
    return RECT{ rcBox.left + 10, rcBox.top, rcBox.right - 10, rcBox.bottom };
}

// 自绘 value 文本（值字段 EDIT 因滚动跨边界 / host 未就绪等无法接入时的兜底）。
void DrawKVCellValueOnly(HDC hdc, const RECT& rcVal, LPCTSTR value)
{
    const bool empty = (value == nullptr || value[0] == _T('\0'));
    ui::DrawTextLine(hdc, empty ? _T("—") : value, rcVal,
                     empty ? theme::kInk400 : theme::kInk700,
                     ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                     | DT_END_ELLIPSIS);
}

// 统计部门子树的员工 / 子部门数。
void CountSub(const OrgNode& n, int& users, int& depts)
{
    if (n.m_kind == OrgNodeKind::User)
    {
        ++users;
        return;
    }
    for (const auto& c : n.m_children)
    {
        if (c.m_kind == OrgNodeKind::Dept)
        {
            ++depts;
        }
        CountSub(c, users, depts);
    }
}

// ---------------------------------------------------------------------------
// 「权限与角色」tab 用的权限矩阵
// ---------------------------------------------------------------------------

// 一条权限项。
struct PermItem
{
    LPCTSTR m_group;   // 所属分组
    LPCTSTR m_label;   // 权限名
    LPCTSTR m_key;     // 权限键（如 msg.send）
    bool    m_defOn;   // 普通成员默认是否开启
};

// 权限矩阵，对应设计稿 PERMISSION_GROUPS（同组连续排列）。
const PermItem kPerms[] =
{
    { _T("通讯"),      _T("发送消息"),             _T("msg.send"),         true  },
    { _T("通讯"),      _T("撤回消息（5 分钟内）"), _T("msg.recall"),       true  },
    { _T("通讯"),      _T("发起音视频通话"),       _T("msg.audio_video"),  true  },
    { _T("通讯"),      _T("添加外部联系人"),       _T("msg.external"),     false },
    { _T("文件与会议"), _T("上传文件 (≤500MB)"),   _T("file.upload"),      true  },
    { _T("文件与会议"), _T("上传大文件 (>500MB)"), _T("file.large"),       false },
    { _T("文件与会议"), _T("创建会议室"),          _T("meet.create"),      true  },
    { _T("文件与会议"), _T("会议录制"),            _T("meet.record"),      false },
    { _T("群组"),      _T("创建群组"),             _T("group.create"),     true  },
    { _T("群组"),      _T("邀请外部成员入群"),     _T("group.invite_ext"), false },
    { _T("群组"),      _T("发送群公告"),           _T("group.broadcast"),  false },
    { _T("数据"),      _T("导出个人聊天记录"),     _T("data.export_chat"), true  },
    { _T("数据"),      _T("导出企业通讯录"),       _T("data.export_org"),  false },
};
const int kPermCount = sizeof(kPerms) / sizeof(kPerms[0]);

// 画一个 pill 形开关。on 时品牌色底、滑块在右；off 时灰底、滑块在左。
void DrawToggle(HDC hdc, const RECT& rc, bool on)
{
    const int h = rc.bottom - rc.top;
    ui::FillRoundRect(hdc, rc, on ? theme::kFlamingo500 : theme::kInk200, h / 2);
    const int knob = h - 4;
    RECT k;
    k.top    = rc.top + 2;
    k.bottom = k.top + knob;
    k.left   = on ? (rc.right - 2 - knob) : (rc.left + 2);
    k.right  = k.left + knob;
    ui::FillRoundRect(hdc, k, theme::kInk0, knob / 2);
}

// 画一个圆角进度条：浅灰底 + 按 pct 着色的填充。
void DrawProgressBar(HDC hdc, const RECT& rc, int pct, COLORREF color)
{
    if (pct < 0)
    {
        pct = 0;
    }
    if (pct > 100)
    {
        pct = 100;
    }
    const int h = rc.bottom - rc.top;
    ui::FillRoundRect(hdc, rc, theme::kInk100, h / 2);
    RECT fill = rc;
    fill.right = rc.left + (rc.right - rc.left) * pct / 100;
    if (fill.right > fill.left + h)
    {
        ui::FillRoundRect(hdc, fill, color, h / 2);
    }
}

// 画一组迷你柱（活跃度示意，类似 sparkline）。
void DrawMiniBars(HDC hdc, const RECT& rc, const int* data, int n, COLORREF color)
{
    if (n <= 0)
    {
        return;
    }
    int maxV = 1;
    for (int i = 0; i < n; ++i)
    {
        if (data[i] > maxV)
        {
            maxV = data[i];
        }
    }
    const int areaH = rc.bottom - rc.top;
    const int gap   = 2;
    const int bw    = (rc.right - rc.left - gap * (n - 1)) / n;
    for (int i = 0; i < n; ++i)
    {
        int bh = data[i] * areaH / maxV;
        if (bh < 2)
        {
            bh = 2;
        }
        RECT b = { rc.left + i * (bw + gap), rc.bottom - bh,
                   rc.left + i * (bw + gap) + bw, rc.bottom };
        ui::FillRoundRect(hdc, b, color, 2);
    }
}

// loginrecords.do 的 devices 元素 clienttype 取值。
const int kClientTypePc     = 1;   // PC 客户端
const int kClientTypeMobile = 2;   // 移动端客户端

// activity.do 的 severity 取值。
const int kActivitySeverityWarn   = 1;   // 警告级
const int kActivitySeverityDanger = 2;   // 危险级

// 把 activity.do 的 severity 整数映射为 Severity 枚举。
Severity SeverityFromInt(int severity)
{
    if (severity == kActivitySeverityWarn)
    {
        return Severity::Warn;
    }
    if (severity == kActivitySeverityDanger)
    {
        return Severity::Danger;
    }
    return Severity::Info;
}

// 画「危险操作」卡里的一行：左侧标题 + 描述，右侧一个按钮。
//   top：本行顶端 y；btnDanger：true=红底实心按钮，false=描边按钮。
//   返回按钮矩形，供调用方登记命中区。
RECT DrawDangerRow(HDC hdc, int left, int right, int top,
                   LPCTSTR title, LPCTSTR desc, LPCTSTR btnText, bool btnDanger)
{
    RECT t = { left + 16, top + 12, right - 120, top + 32 };
    ui::DrawTextLine(hdc, title, t, theme::kInk700,
                     ui::UiFont(theme::kFontSizeNormal, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT d = { left + 16, top + 32, right - 120, top + 52 };
    ui::DrawTextLine(hdc, desc, d, theme::kInk400,
                     ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT btn = { right - 16 - 88, top + 16, right - 16, top + 16 + kBtnH };
    if (btnDanger)
    {
        ui::FillRoundRect(hdc, btn, theme::kColorDanger, theme::kRadiusMd);
        ui::DrawTextLine(hdc, btnText, btn, theme::kInk0,
                         ui::UiFont(theme::kFontSizeNormal, true),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    else
    {
        ui::FillRoundRect(hdc, btn, theme::kInk0, theme::kRadiusMd);
        ui::StrokeRoundRect(hdc, btn, theme::kInk150, theme::kRadiusMd, 1.0f);
        ui::DrawTextLine(hdc, btnText, btn, theme::kInk600,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    return btn;
}

} // anonymous namespace

OrgDetailPanel::OrgDetailPanel()
{
    // ---- 基本信息 11 个值字段：只读 DuiEditHost，让每个值都是真正的只读
    // 文本框（原生拖选 + 右键菜单复制/全选 + Ctrl+C/A）。先加它们、再加
    // 滚动条 —— 这样滚动条在子控件序列最末，base OnPaint 最后绘制它。
    // 位置 / 文本 / 可见性由 PaintUserBasic 每帧重新设置；这里仅做一次性
    // 初始化（只读 + 无边框 + 底色对齐自绘框 + 默认隐藏）。EDIT 子 HWND 不在
    // 此创建（host 尚未就绪），由 EnsureBasicEditsCreated 在首次绘制时懒创建。
    m_basicValueEdits.reserve(kBasicFieldCount);
    for (int i = 0; i < kBasicFieldCount; ++i)
    {
        auto edit = std::make_unique<balloonwjui::DuiEditHost>();
        edit->SetReadOnly(true);
        edit->SetShowBorder(false);          // 外观靠自绘圆角框，EDIT 自身不画边框
        edit->SetBgColor(theme::kInk0);      // 底色与自绘框填充（kInk0）一致
        // 只读 EDIT 的底色由 OS 经 WM_CTLCOLORSTATIC 向父窗口索取画刷。
        // DuiEditHost::SetBgColor 在"目标色 == 其默认白底 RGB(255,255,255)"时会
        // 提前 return（见 balloonui DuiEditHost::SetBgColor），而 kInk0 恰是纯白，
        // 于是底色没被传到 WM_CTLCOLOR* 实际使用的 m_ctlBg，m_ctlBg 留在
        // CLR_INVALID，只读 EDIT 便落回系统默认灰底（COLOR_3DFACE）。这里显式再
        // 设一次控件底色，保证 WM_CTLCOLORSTATIC 返回白色画刷、与自绘框白底一致。
        edit->SetCtlBgColor(theme::kInk0);
        edit->SetMargins(0, 0, 0, 0);        // 去掉默认内边距，文字左缘贴齐自绘值位置
        edit->SetVisible(false);             // 没节点时不显示
        m_basicValueEdits.push_back(edit.get());
        AddChild(std::move(edit));
    }
    // 本帧显隐标记表，与值字段同长；OnPaint 开头清零、末尾据此 reconcile。
    m_basicEditShown.assign(kBasicFieldCount, 0);

    // ---- 面板右缘的悬浮 auto-hide 滚动条 —— 最后入子，保证绘制在最上层。
    auto sb = std::make_unique<OverlayScrollbar>();
    m_panelSb = sb.get();
    m_panelSb->SetOnScroll(&OrgDetailPanel::OnPanelSbScroll, this);
    AddChild(std::move(sb));
}

void OrgDetailPanel::SetCallbacks(SelectFn selFn, ActionFn actFn, void* user)
{
    m_selFn  = selFn;
    m_actFn  = actFn;
    m_cbUser = user;
}

void OrgDetailPanel::SetNode(const OrgNode* node, const Enterprise* ent)
{
    const bool isUser = (node != nullptr && node->m_kind == OrgNodeKind::User);
    // 是否仍是同一员工（如组织树刷新后重设节点）。用 ID 比较而非指针 ——
    // 组织树重建后旧节点指针已失效，不能解引用比较。
    const bool sameUser = isUser && (node->m_id == m_curUserId);

    m_node    = node;
    m_ent     = ent;
    m_scrollY = 0;
    if (!sameUser)
    {
        m_userTab = 0;   // 切到不同节点才回到第一个 tab
    }
    if (isUser && !sameUser)
    {
        // 切到「别的」员工才清空已加载数据；同一员工（仅刷新）保留，避免
        // 写操作后刷新导致权限 / 登录 / 操作历史等 tab 闪空。真实数据由
        // user/getpermissions.do、loginrecords、activity 的应答填入。
        m_userRole = 0;
        m_userPerms.assign(kPermCount, 0);
        m_permDefault.assign(kPermCount, 0);
        m_loginDevices.clear();
        m_loginRecords.clear();
        m_activity.clear();
        m_msgSent = 0;
        m_msgReceived = 0;
        m_msgConversations = 0;
        m_msgStatsLoaded = false;
        m_msgTrend.clear();
    }
    m_curUserId = isUser ? node->m_id : CString();
    Invalidate();
}

void OrgDetailPanel::SetPermissionData(int role,
                                       const std::vector<PermissionEntry>& items)
{
    m_userRole = role;   // 服务端角色编码直接存（0 普通成员/1 负责人/2 管理员）
    m_userPerms.assign(kPermCount, 0);
    m_permDefault.assign(kPermCount, 0);
    // 按权限 key 把服务端返回的各项匹配到内置的 13 项权限（kPerms 同序）。
    for (size_t k = 0; k < items.size(); ++k)
    {
        for (int i = 0; i < kPermCount; ++i)
        {
            if (items[k].m_key == kPerms[i].m_key)
            {
                m_userPerms[i]   = items[k].m_effective   ? 1 : 0;
                m_permDefault[i] = items[k].m_roleDefault ? 1 : 0;
                break;
            }
        }
    }
    Invalidate();
}

std::vector<PermOverride> OrgDetailPanel::GetPermOverrides() const
{
    // 覆盖项 = 当前生效值与角色默认值不同的权限。
    std::vector<PermOverride> out;
    for (int i = 0; i < kPermCount; ++i)
    {
        const char eff = (i < static_cast<int>(m_userPerms.size()))
                             ? m_userPerms[i] : 0;
        const char def = (i < static_cast<int>(m_permDefault.size()))
                             ? m_permDefault[i] : 0;
        if (eff != def)
        {
            PermOverride o;
            o.m_key     = kPerms[i].m_key;
            o.m_granted = (eff != 0);
            out.push_back(o);
        }
    }
    return out;
}

void OrgDetailPanel::SetLoginData(const std::vector<LoginDevice>& devices,
                                  const std::vector<LoginRecord>& records)
{
    m_loginDevices = devices;
    m_loginRecords = records;
    Invalidate();
}

void OrgDetailPanel::SetMessageStats(int sent, int received, int conversations,
                                     const std::vector<MessageTrendPoint>& trend)
{
    m_msgSent          = sent;
    m_msgReceived      = received;
    m_msgConversations = conversations;
    m_msgTrend         = trend;
    m_msgStatsLoaded   = true;
    Invalidate();
}

void OrgDetailPanel::SetActivityData(const std::vector<ActivityEntry>& items)
{
    m_activity = items;
    Invalidate();
}

void OrgDetailPanel::ClampScroll()
{
    const int viewH = m_rcItem.bottom - m_rcItem.top;
    int maxScroll = m_contentH - viewH;
    if (maxScroll < 0) { maxScroll = 0; }
    if (m_scrollY < 0) { m_scrollY = 0; }
    if (m_scrollY > maxScroll) { m_scrollY = maxScroll; }
}

void OrgDetailPanel::OnPanelSbScroll(void* ctx, int newScroll)
{
    // 悬浮滚动条拖动滑块的回调：把新滚动量写回本面板并请求重绘。
    OrgDetailPanel* self = static_cast<OrgDetailPanel*>(ctx);
    self->m_scrollY = newScroll;
    self->Invalidate();
}

void OrgDetailPanel::Layout(const RECT& rcAvail)
{
    m_rcItem = rcAvail;
    ClampScroll();

    // 悬浮滚动条摆在面板右内缘的细竖条上：宽度取 kSuggestedWidth、距右缘
    // kSbInset 像素，高度 = 可视区高度（与 ClampScroll 的 viewH 一致，即整个
    // 面板矩形的高），悬浮于内容之上、不挤占内容宽度。
    const int kSbInset = 3;   // 滚动条距面板右内缘的间距（像素）
    m_panelSb->ForceLayout(RECT{
        m_rcItem.right - kSbInset - OverlayScrollbar::kSuggestedWidth,
        m_rcItem.top,
        m_rcItem.right - kSbInset,
        m_rcItem.bottom });
}

void OrgDetailPanel::OnPaint(HDC hdc, const RECT& /*rcDirty*/)
{
    if (!m_bVisible)
    {
        return;
    }
    ui::FillSolidRect(hdc, m_rcItem, theme::kInk25);
    m_hits.clear();

    // 清零本帧"应显示的基本信息值字段"标记；下面 PaintUserBasic 命中显形的
    // 字段把对应位置 1。真正的 EDIT 显隐放到本函数末尾 ApplyBasicEditVisibility
    // 统一 reconcile（幂等，不变化不触发 ShowWindow）—— 避免在这里先 hide-all
    // 再 show-visible 造成 HWND 每帧 hide→show 闪烁（无窗口 DuiLabel 无此问题，
    // 真 EDIT 有）。其它路径（PaintEmpty / PaintDept / 非基本信息 tab）不置位，
    // reconcile 时即全部隐藏。
    for (size_t i = 0; i < m_basicEditShown.size(); ++i)
    {
        m_basicEditShown[i] = 0;
    }

    // 内容绘制裁剪到面板矩形，避免滚动时内容画出界、压到相邻区域。
    int savedClip = ::SaveDC(hdc);
    ::IntersectClipRect(hdc, m_rcItem.left, m_rcItem.top,
                        m_rcItem.right, m_rcItem.bottom);
    if (m_node == nullptr)
    {
        m_contentH = PaintEmpty(hdc);
    }
    else if (m_node->m_kind == OrgNodeKind::Dept)
    {
        m_contentH = PaintDept(hdc);
    }
    else
    {
        m_contentH = PaintUser(hdc);
    }
    ::RestoreDC(hdc, savedClip);

    // 把最新内容总高 / 滚动量同步给悬浮滚动条子控件。
    m_panelSb->SetMetrics(m_contentH, m_scrollY);

    // 绘制子控件（悬浮滚动条）——它悬浮于面板内容右缘、auto-hide。base
    // OnPaint 以脏矩形与子控件求交后才绘制，故传整个面板矩形作脏矩形。
    balloonwjui::DuiControl::OnPaint(hdc, m_rcItem);

    // 帧末统一同步基本信息值字段 EDIT 的显隐（按本帧标记，幂等）。
    ApplyBasicEditVisibility();
}

int OrgDetailPanel::PaintEmpty(HDC hdc)
{
    const int cx = (m_rcItem.left + m_rcItem.right) / 2;
    const int cy = (m_rcItem.top + m_rcItem.bottom) / 2;

    // 图标方块。
    const int kBox = 72;
    RECT rcBox = { cx - kBox / 2, cy - kBox / 2 - 30,
                   cx + kBox / 2, cy + kBox / 2 - 30 };
    ui::FillRoundRect(hdc, rcBox, theme::kInk50, 18);
    RECT rcIcon = { cx - 16, cy - 16 - 30, cx + 16, cy + 16 - 30 };
    ui::DrawIcon(hdc, ui::Icon::User, rcIcon, theme::kInk300);

    RECT rcTitle = { m_rcItem.left, cy + 16, m_rcItem.right, cy + 40 };
    ui::DrawTextLine(hdc, _T("选择左侧组织树中的节点"), rcTitle, theme::kInk500,
                     ui::UiFont(theme::kFontSizeTitle, true),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT rcSub = { m_rcItem.left, cy + 42, m_rcItem.right, cy + 62 };
    ui::DrawTextLine(hdc, _T("选择部门以查看其下属成员，或选择员工以管理资料与权限"),
                     rcSub, theme::kInk400,
                     ui::UiFont(theme::kFontSizeNormal, false),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    return m_rcItem.bottom - m_rcItem.top;   // 占位内容不滚动
}

int OrgDetailPanel::PaintDept(HDC hdc)
{
    const int left  = m_rcItem.left + kSidePad;
    const int right = m_rcItem.right - kSidePad;
    const int w     = right - left;
    int y = m_rcItem.top + kTopPad - m_scrollY;

    const bool isRoot = m_node->m_isCompanyRoot;   // 公司节点，资料头的呈现与普通部门不同
    int users = 0, depts = 0;
    CountSub(*m_node, users, depts);

    // ---- 资料头卡片 ----
    RECT head = { left, y, right, y + kHeadCardH };
    ui::DrawCard(hdc, head);
    // 文件夹图标方块。
    RECT iconBox = { left + kCardPad, y + kCardPad,
                     left + kCardPad + kHeadAvatar, y + kCardPad + kHeadAvatar };
    ui::FillRoundRect(hdc, iconBox, theme::kFlamingo50, 14);
    RECT fi = { (iconBox.left + iconBox.right) / 2 - 14,
                (iconBox.top + iconBox.bottom) / 2 - 14,
                (iconBox.left + iconBox.right) / 2 + 14,
                (iconBox.top + iconBox.bottom) / 2 + 14 };
    ui::DrawIcon(hdc, ui::Icon::Folder, fi, theme::kFlamingo600);

    const int txtX = iconBox.right + 16;
    // 操作按钮（右上）：子部门 + 新增员工。
    const int btnY = y + kCardPad;
    RECT btnAddUser = { right - kCardPad - 96, btnY, right - kCardPad, btnY + kBtnH };
    RECT btnAddDept = { btnAddUser.left - 8 - 84, btnY, btnAddUser.left - 8, btnY + kBtnH };
    ui::FillRoundRect(hdc, btnAddDept, theme::kInk0, theme::kRadiusMd);
    ui::StrokeRoundRect(hdc, btnAddDept, theme::kInk150, theme::kRadiusMd, 1.0f);
    ui::DrawTextLine(hdc, _T("子部门"), btnAddDept, theme::kInk600,
                     ui::UiFont(theme::kFontSizeNormal, false),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ui::FillRoundRect(hdc, btnAddUser, theme::kFlamingo500, theme::kRadiusMd);
    ui::DrawTextLine(hdc, _T("新增员工"), btnAddUser, theme::kInk0,
                     ui::UiFont(theme::kFontSizeNormal, true),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    {
        Hit h1; h1.m_rc = btnAddDept; h1.m_kind = 2;
        h1.m_arg = static_cast<int>(OrgTree::Action::AddDept); h1.m_nodeId = m_node->m_id;
        m_hits.push_back(h1);
        Hit h2; h2.m_rc = btnAddUser; h2.m_kind = 2;
        h2.m_arg = static_cast<int>(OrgTree::Action::AddUser); h2.m_nodeId = m_node->m_id;
        m_hits.push_back(h2);
    }

    // 部门名（根部门用企业名）。
    CString deptName = (isRoot && m_ent != nullptr) ? m_ent->m_name : m_node->m_name;
    RECT rcName = { txtX, y + kCardPad + 4, btnAddDept.left - 16, y + kCardPad + 30 };
    ui::DrawTextLine(hdc, deptName, rcName, theme::kInk700,
                     ui::UiFont(kBigTitlePt, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    CString meta;
    meta.Format(_T("负责人：%s · 子部门 %d · 总计员工 %d"),
                static_cast<LPCTSTR>(m_node->m_leader.IsEmpty()
                    ? CString(_T("—")) : m_node->m_leader),
                depts, users);
    RECT rcMeta = { txtX, y + kCardPad + 34, right - kCardPad, y + kCardPad + 54 };
    ui::DrawTextLine(hdc, meta, rcMeta, theme::kInk500,
                     ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    y += kHeadCardH + kSectionGap;

    // ---- 下属部门 ----
    std::vector<const OrgNode*> subDepts, members;
    for (const auto& c : m_node->m_children)
    {
        if (c.m_kind == OrgNodeKind::Dept) { subDepts.push_back(&c); }
        else                               { members.push_back(&c); }
    }

    if (!subDepts.empty())
    {
        DrawSectionTitle(hdc, left, y, w, _T("下属部门"), false);
        y += 26;
        const int kCardW = 250, kCardGap = 10, kCardH = 64;
        int cols = (w + kCardGap) / (kCardW + kCardGap);
        if (cols < 1) { cols = 1; }
        const int cellW = (w - kCardGap * (cols - 1)) / cols;
        for (size_t i = 0; i < subDepts.size(); ++i)
        {
            const OrgNode* d = subDepts[i];
            const int col = static_cast<int>(i) % cols;
            const int rowi = static_cast<int>(i) / cols;
            const int cx = left + col * (cellW + kCardGap);
            const int cyTop = y + rowi * (kCardH + kCardGap);
            RECT card = { cx, cyTop, cx + cellW, cyTop + kCardH };
            ui::DrawCard(hdc, card);
            RECT ib = { cx + 14, cyTop + 15, cx + 14 + 34, cyTop + 15 + 34 };
            ui::FillRoundRect(hdc, ib, theme::kInk50, 8);
            RECT ii = { (ib.left + ib.right) / 2 - 9, (ib.top + ib.bottom) / 2 - 9,
                        (ib.left + ib.right) / 2 + 9, (ib.top + ib.bottom) / 2 + 9 };
            ui::DrawIcon(hdc, ui::Icon::Folder, ii, theme::kInk500);
            RECT dn = { ib.right + 12, cyTop + 13, card.right - 14, cyTop + 31 };
            ui::DrawTextLine(hdc, d->m_name, dn, theme::kInk700,
                             ui::UiFont(theme::kFontSizeNormal, true),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            int du = 0, dd = 0;
            CountSub(*d, du, dd);
            CString dm;
            dm.Format(_T("负责人 %s · %d 员工"),
                      static_cast<LPCTSTR>(d->m_leader.IsEmpty()
                          ? CString(_T("—")) : d->m_leader), du);
            RECT dmr = { ib.right + 12, cyTop + 32, card.right - 14, cyTop + 50 };
            ui::DrawTextLine(hdc, dm, dmr, theme::kInk400,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            Hit h; h.m_rc = card; h.m_kind = 1; h.m_arg = 0; h.m_nodeId = d->m_id;
            m_hits.push_back(h);
        }
        const int rows = (static_cast<int>(subDepts.size()) + cols - 1) / cols;
        y += rows * (kCardH + kCardGap) - kCardGap + kSectionGap;
    }

    // ---- 直接挂载员工 ----
    CString memTitle;
    memTitle.Format(_T("直接挂载员工（%d）"), static_cast<int>(members.size()));
    DrawSectionTitle(hdc, left, y, w, memTitle, false);
    y += 26;
    if (members.empty())
    {
        RECT box = { left, y, right, y + 80 };
        ui::FillRoundRect(hdc, box, theme::kInk0, theme::kRadiusLg);
        ui::StrokeRoundRect(hdc, box, theme::kInk150, theme::kRadiusLg, 1.0f);
        ui::DrawTextLine(hdc, _T("该部门下暂无直接挂载的员工"), box, theme::kInk400,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += 80;
    }
    else
    {
        const int kMemRow = 52;
        RECT listCard = { left, y, right,
                          y + kMemRow * static_cast<int>(members.size()) };
        ui::DrawCard(hdc, listCard);
        for (size_t i = 0; i < members.size(); ++i)
        {
            const OrgNode* u = members[i];
            const int top = y + static_cast<int>(i) * kMemRow;
            if (i > 0)
            {
                ui::FillLine(hdc, left + 14, top, w - 28, 1, theme::kInk100);
            }
            const int mcy = top + kMemRow / 2;
            RECT av = { left + 14, mcy - 14, left + 14 + 28, mcy + 14 };
            ui::DrawAvatar(hdc, av, ui::AvatarColor(u->m_name),
                           ui::Initials(u->m_name));
            RECT nm = { av.right + 10, top + 8, left + 230, top + 28 };
            ui::DrawTextLine(hdc, u->m_name, nm, theme::kInk700,
                             ui::UiFont(theme::kFontSizeNormal, true),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT tt = { av.right + 10, top + 26, left + 230, top + 44 };
            ui::DrawTextLine(hdc, u->m_title, tt, theme::kInk400,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT em = { left + 244, top, right - 120, top + kMemRow };
            ui::DrawTextLine(hdc, u->m_email, em, theme::kInk500,
                             ui::UiFont(theme::kFontSizeNormal, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            // 在职状态徽章。
            LPCTSTR stTxt = (u->m_userStatus == UserStatus::OnLeave) ? _T("休假")
                          : (u->m_userStatus == UserStatus::Disabled) ? _T("已停用")
                          : _T("在职");
            COLORREF stClr = (u->m_userStatus == UserStatus::OnLeave) ? theme::kColorWarn
                           : (u->m_userStatus == UserStatus::Disabled) ? theme::kInk400
                           : theme::kColorOk;
            const int cw = ui::ChipWidth(hdc, stTxt, true);
            RECT chip = { right - 14 - cw, mcy - 10, right - 14, mcy + 10 };
            ui::DrawChip(hdc, chip, stTxt, theme::kInk600, theme::kInk50, true, stClr);
            RECT rowRc = { left, top, right, top + kMemRow };
            Hit h; h.m_rc = rowRc; h.m_kind = 1; h.m_arg = 0; h.m_nodeId = u->m_id;
            m_hits.push_back(h);
        }
        y += kMemRow * static_cast<int>(members.size());
    }
    y += kBottomPad;
    return y - (m_rcItem.top - m_scrollY);
}

int OrgDetailPanel::PaintUser(HDC hdc)
{
    const int left  = m_rcItem.left + kSidePad;
    const int right = m_rcItem.right - kSidePad;
    const int w     = right - left;
    int y = m_rcItem.top + kTopPad - m_scrollY;

    // ---- 资料头卡片 ----
    RECT head = { left, y, right, y + kHeadCardH };
    ui::DrawCard(hdc, head);
    RECT av = { left + kCardPad, y + kCardPad,
                left + kCardPad + kHeadAvatar, y + kCardPad + kHeadAvatar };
    ui::DrawAvatar(hdc, av, ui::AvatarColor(m_node->m_name),
                   ui::Initials(m_node->m_name));
    const int txtX = av.right + 16;

    // 姓名 + 状态徽章。
    RECT rcName = { txtX, y + kCardPad + 2, right - kCardPad, y + kCardPad + 28 };
    ui::DrawTextLine(hdc, m_node->m_name, rcName, theme::kInk700,
                     ui::UiFont(kBigTitlePt + 2, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    {
        const int nameW = MeasureW(m_node->m_name, ui::UiFont(kBigTitlePt + 2, true));
        LPCTSTR stTxt = (m_node->m_userStatus == UserStatus::OnLeave) ? _T("休假")
                      : (m_node->m_userStatus == UserStatus::Disabled) ? _T("已停用")
                      : _T("在职");
        COLORREF stClr = (m_node->m_userStatus == UserStatus::OnLeave) ? theme::kColorWarn
                       : (m_node->m_userStatus == UserStatus::Disabled) ? theme::kInk400
                       : theme::kColorOk;
        const int cw = ui::ChipWidth(hdc, stTxt, true);
        RECT chip = { txtX + nameW + 12, y + kCardPad + 5,
                      txtX + nameW + 12 + cw, y + kCardPad + 25 };
        ui::DrawChip(hdc, chip, stTxt, theme::kInk600, theme::kInk50, true, stClr);
    }
    // 职位。
    RECT rcTitle = { txtX, y + kCardPad + 32, right - kCardPad, y + kCardPad + 50 };
    ui::DrawTextLine(hdc, m_node->m_title, rcTitle, theme::kInk500,
                     ui::UiFont(theme::kFontSizeNormal, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    // 邮箱 · 手机 · 工号。
    CString line;
    line.Format(_T("%s    %s    #%s"),
                static_cast<LPCTSTR>(m_node->m_email),
                static_cast<LPCTSTR>(m_node->m_phone),
                static_cast<LPCTSTR>(m_node->m_employeeNo));
    RECT rcLine = { txtX, y + kCardPad + 54, right - kCardPad, y + kCardPad + 74 };
    ui::DrawTextLine(hdc, line, rcLine, theme::kInk500,
                     ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    y += kHeadCardH + 14;

    // ---- tab 条 ----
    int tx = left;
    for (int i = 0; i < kUserTabCount; ++i)
    {
        const bool active = (i == m_userTab);
        const int tw = MeasureW(kUserTabs[i], ui::UiFont(theme::kFontSizeNormal, active))
                       + 28;
        RECT tab = { tx, y, tx + tw, y + kTabBarH };
        ui::DrawTextLine(hdc, kUserTabs[i], tab,
                         active ? theme::kFlamingo600 : theme::kInk400,
                         ui::UiFont(theme::kFontSizeNormal, active),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (active)
        {
            RECT underline = { tx + 6, y + kTabBarH - 2, tx + tw - 6, y + kTabBarH };
            ui::FillSolidRect(hdc, underline, theme::kFlamingo500);
        }
        Hit h; h.m_rc = tab; h.m_kind = 0; h.m_arg = i;
        m_hits.push_back(h);
        tx += tw;
    }
    // tab 条底部分隔线。
    ui::FillLine(hdc, left, y + kTabBarH - 1, w, 1, theme::kInk100);
    y += kTabBarH + kSectionGap;

    switch (m_userTab)
    {
    case 1:  y = PaintUserRoles(hdc, y);    break;
    case 2:  y = PaintUserServices(hdc, y); break;
    case 3:  y = PaintUserSessions(hdc, y); break;
    case 4:  y = PaintUserDevices(hdc, y);  break;
    case 5:  y = PaintUserHistory(hdc, y);  break;
    case 0:
    default: y = PaintUserBasic(hdc, y);    break;
    }

    y += kBottomPad;
    return y - (m_rcItem.top - m_scrollY);
}

// ===========================================================================
// 员工详情 —— 基本信息 tab
// ===========================================================================
int OrgDetailPanel::PaintUserBasic(HDC hdc, int y)
{
    const int left  = m_rcItem.left + kSidePad;
    const int right = m_rcItem.right - kSidePad;
    const int w     = right - left;

    DrawSectionTitle(hdc, left, y, w, _T("基本信息"), false);
    if (m_node != nullptr)
    {
        // 标题行右侧：调部门 + 编辑资料 两个描边按钮。
        const int btnW   = 96;
        const int btnTop = y - 4;
        const int btnBot = y - 4 + 26;

        // 编辑资料（最右）。
        RECT editBtn = { right - btnW, btnTop, right, btnBot };
        ui::FillRoundRect(hdc, editBtn, theme::kInk0, theme::kRadiusSm);
        ui::StrokeRoundRect(hdc, editBtn, theme::kInk150, theme::kRadiusSm, 1.0f);
        ui::DrawTextLine(hdc, _T("编辑资料"), editBtn, theme::kFlamingo600,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        Hit he;
        he.m_rc = editBtn;
        he.m_kind = 2;
        he.m_arg = static_cast<int>(OrgTree::Action::EditUser);
        he.m_nodeId = m_node->m_id;
        m_hits.push_back(he);

        // 调部门（编辑资料左侧）。
        RECT moveBtn = { editBtn.left - 8 - btnW, btnTop,
                         editBtn.left - 8,        btnBot };
        ui::FillRoundRect(hdc, moveBtn, theme::kInk0, theme::kRadiusSm);
        ui::StrokeRoundRect(hdc, moveBtn, theme::kInk150, theme::kRadiusSm, 1.0f);
        ui::DrawTextLine(hdc, _T("调部门"), moveBtn, theme::kFlamingo600,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        Hit hm;
        hm.m_rc = moveBtn;
        hm.m_kind = 2;
        hm.m_arg = static_cast<int>(OrgTree::Action::MoveUser);
        hm.m_nodeId = m_node->m_id;
        m_hits.push_back(hm);
    }
    y += 24;
    struct KV { LPCTSTR label; CString value; };
    KV fields[] =
    {
        { _T("姓名"),     m_node->m_name },
        { _T("职位"),     m_node->m_title },
        { _T("登录账号"), m_node->m_loginAccount },
        { _T("工号"),     m_node->m_employeeNo },
        { _T("入职日期"), m_node->m_joined },
        { _T("邮箱"),     m_node->m_email },
        { _T("手机号"),   m_node->m_phone },
        { _T("座机"),     m_node->m_landline },
        { _T("工位地址"), m_node->m_workstation },
        { _T("性别"),     m_node->m_gender },
        { _T("个性签名"), m_node->m_signature },
    };
    // 编译期校验 fields[] 数量与 kBasicFieldCount 同步；任一方改了另一方
    // 必跟改，否则构造函数创建的 EDIT 值字段数量与字段数对不上。
    static_assert(sizeof(fields) / sizeof(fields[0]) == kBasicFieldCount,
                  "fields[] size out of sync with kBasicFieldCount; "
                  "update kBasicFieldCount + ctor loop + m_basicValueEdits");
    const int kCount  = kBasicFieldCount;
    const int kColGap = 14;
    const int colW    = (w - kColGap) / 2;
    const int kCellH  = 52 + 10;
    const int nonFull = kCount - 1;
    // 11 个值字段全部用只读 DuiEditHost 子控件渲染（原生拖选 + 右键菜单）：
    // 前 10 个排成 2 列网格，第 11 个（个性签名）单独跨整行。
    for (int i = 0; i < nonFull; ++i)
    {
        const int cx = left + (i % 2) * (colW + kColGap);
        const int cy = y + (i / 2) * kCellH;
        RenderBasicField(hdc, i, cx, cy, colW, fields[i].label, fields[i].value);
    }
    const int gridRows = (nonFull + 1) / 2;
    RenderBasicField(hdc, kCount - 1, left, y + gridRows * kCellH, w,
                     fields[kCount - 1].label, fields[kCount - 1].value);
    y += (gridRows + 1) * kCellH + kSectionGap;

    // 危险操作（三行：启用/停用、重置密码、删除员工）。
    DrawSectionTitle(hdc, left, y, w, _T("危险操作"), true);
    y += 24;
    const int dangerRowH = 64;
    RECT danger = { left, y, right, y + dangerRowH * 3 };
    ui::DrawCard(hdc, danger);

    // 行 1：启用 / 停用账号（按当前状态切换文案）。
    const bool disabled = (m_node->m_userStatus == UserStatus::Disabled);
    RECT btnStatus = DrawDangerRow(hdc, left, right, y,
        disabled ? _T("启用账号") : _T("停用账号"),
        disabled ? _T("启用后该员工可重新登录各客户端")
                 : _T("停用后该员工将无法登录任何客户端"),
        disabled ? _T("启用") : _T("停用"), false);
    ui::FillLine(hdc, left + 16, y + dangerRowH, w - 32, 1, theme::kInk100);

    // 行 2：重置登录密码。
    RECT btnReset = DrawDangerRow(hdc, left, right, y + dangerRowH,
        _T("重置登录密码"), _T("生成新密码，该员工的旧密码立即失效"),
        _T("重置"), false);
    ui::FillLine(hdc, left + 16, y + dangerRowH * 2, w - 32, 1, theme::kInk100);

    // 行 3：删除员工。
    RECT btnDelete = DrawDangerRow(hdc, left, right, y + dangerRowH * 2,
        _T("删除员工"),
        _T("永久删除该员工及其会话索引（聊天记录服务端归档 180 天）"),
        _T("删除"), true);

    {
        Hit hs;
        hs.m_rc = btnStatus;
        hs.m_kind = 2;
        hs.m_arg = static_cast<int>(OrgTree::Action::SetUserStatus);
        hs.m_nodeId = m_node->m_id;
        m_hits.push_back(hs);

        Hit hr;
        hr.m_rc = btnReset;
        hr.m_kind = 2;
        hr.m_arg = static_cast<int>(OrgTree::Action::ResetPassword);
        hr.m_nodeId = m_node->m_id;
        m_hits.push_back(hr);

        Hit hd;
        hd.m_rc = btnDelete;
        hd.m_kind = 2;
        hd.m_arg = static_cast<int>(OrgTree::Action::Delete);
        hd.m_nodeId = m_node->m_id;
        m_hits.push_back(hd);
    }
    y += dangerRowH * 3;
    return y;
}

void OrgDetailPanel::RenderBasicField(HDC hdc, int idx, int x, int y, int w,
                                      LPCTSTR label, const CString& value)
{
    // 自绘字段 label + 圆角框（无值文本）；返回值字段的矩形 rcVal。
    RECT rcVal = DrawKVCellLabelAndBoxOnly(hdc, x, y, w, label);

    // 确保 11 个值字段 EDIT 已创建并挂好滚轮转发子类（幂等；host 就绪后只跑一次）。
    EnsureBasicEditsCreated();

    balloonwjui::DuiEditHost* edit = nullptr;
    if (idx >= 0 && idx < static_cast<int>(m_basicValueEdits.size()))
    {
        edit = m_basicValueEdits[idx];
    }

    // 只有当 rcVal 完整落在面板可见区、且 EDIT 子 HWND 已就绪时，才用真 EDIT
    // 渲染：HWND 子窗口不随面板 GDI 裁剪，部分落外会画到面板之外，故滚动跨
    // 边界（或 host 尚未就绪）时退回自绘兜底。EDIT 的实际显隐由本帧标记
    // 在 OnPaint 末尾统一 reconcile，这里只置位、不直接 SetVisible（防闪）。
    const bool fullyInside = (rcVal.top    >= m_rcItem.top    &&
                              rcVal.bottom <= m_rcItem.bottom);
    if (edit != nullptr && fullyInside && edit->GetHostedHwnd() != nullptr)
    {
        const bool empty = value.IsEmpty();
        // SetText 已是幂等的（护栏在 balloonui DuiEditHost::SetText 内：文本与
        // 当前缓存一致时不重复 SetWindowText、不 Invalidate），故这里每帧调用
        // 安全，不会因每帧失效而触发自激重绘。空值写占位符"—"。
        edit->SetText(empty ? _T("—") : static_cast<LPCTSTR>(value));
        // 值文本色：有值 kInk700、空值占位"—" kInk400（对齐自绘两路）。
        // SetCtlTextColor 内部同样有"颜色未变则不失效"的护栏，每帧调用安全。
        edit->SetCtlTextColor(empty ? theme::kInk400 : theme::kInk700);
        // 单行 EDIT 文字顶对齐，给它一个约一行高、在自绘框内垂直居中的子矩形，
        // 让文字视觉上居中（与自绘兜底的 DT_VCENTER 对齐）。
        const int cy = (rcVal.top + rcVal.bottom) / 2;
        RECT rcEdit = { rcVal.left, cy - kBasicEditH / 2,
                        rcVal.right, cy - kBasicEditH / 2 + kBasicEditH };
        edit->SetRect(rcEdit);
        if (idx >= 0 && idx < static_cast<int>(m_basicEditShown.size()))
        {
            m_basicEditShown[idx] = 1;   // 本帧显形；帧末 reconcile 据此 SetVisible(true)
        }
    }
    else
    {
        DrawKVCellValueOnly(hdc, rcVal, value);
    }
}

void OrgDetailPanel::EnsureBasicEditsCreated()
{
    if (m_basicEditsReady)
    {
        return;
    }
    balloonwjui::DuiHost* host = GetHost();
    if (host == nullptr || host->m_hWnd == nullptr || !::IsWindow(host->m_hWnd))
    {
        return;   // host HWND 尚未就绪，下次重绘再试
    }
    bool allReady = true;
    for (balloonwjui::DuiEditHost* edit : m_basicValueEdits)
    {
        if (edit == nullptr)
        {
            continue;
        }
        if (edit->GetHostedHwnd() == nullptr)
        {
            if (!edit->EnsureCreated(host->m_hWnd))
            {
                allReady = false;
                continue;
            }
        }
        HWND h = edit->GetHostedHwnd();
        if (h != nullptr)
        {
            // 字体设成与自绘值文本一致（微软雅黑 10pt = kFontSizeNormal）。否则
            // EDIT 用 balloonui 默认 9pt，与滚动跨边界时的自绘兜底大小不一致。
            // UiFont 返回进程级缓存 HFONT，本控件不持有所有权、无需释放。
            HFONT valueFont = ui::UiFont(theme::kFontSizeNormal, false);
            if (valueFont != nullptr)
            {
                ::SendMessage(h, WM_SETFONT,
                              reinterpret_cast<WPARAM>(valueFont),
                              MAKELPARAM(TRUE, 0));
            }
            // 挂滚轮转发子类（SetWindowSubclass 对"同 proc + 同 id"幂等）。
            ::SetWindowSubclass(h, &OrgDetailPanel::FieldEditWheelProc,
                                kFieldWheelSubclassId,
                                reinterpret_cast<DWORD_PTR>(this));
        }
        else
        {
            allReady = false;
        }
    }
    if (allReady)
    {
        m_basicEditsReady = true;
    }
}

void OrgDetailPanel::SetBasicEditsSuppressed(bool suppressed)
{
    if (m_basicEditsSuppressed == suppressed)
    {
        return;
    }
    m_basicEditsSuppressed = suppressed;

    // 立刻按新状态 reconcile 一次，不等下一帧：弹窗打开这条路径上，AdminFrame
    // 调完本函数就会去显示弹窗，中间未必还有本面板的重绘，靠等下一帧会漏。
    // 关闭弹窗那条路径同理 —— 立刻把该显示的字段恢复出来。
    ApplyBasicEditVisibility();
}

void OrgDetailPanel::ApplyBasicEditVisibility()
{
    for (size_t i = 0; i < m_basicValueEdits.size(); ++i)
    {
        balloonwjui::DuiEditHost* edit = m_basicValueEdits[i];
        if (edit == nullptr)
        {
            continue;
        }
        // 有弹窗压在上面时一律隐藏，忽略本帧标记表。真 EDIT 子窗恒画在 DUI
        // 绘制之上，留着就会浮到弹窗表面（见 SetBasicEditsSuppressed 的说明）。
        const bool show = !m_basicEditsSuppressed
                          && (i < m_basicEditShown.size() && m_basicEditShown[i] != 0);
        // SetVisible 幂等：状态未变不触发 ShowWindow，避免每帧闪烁。
        edit->SetVisible(show);
    }
}

LRESULT CALLBACK OrgDetailPanel::FieldEditWheelProc(HWND hwnd, UINT msg,
                                                    WPARAM wParam, LPARAM lParam,
                                                    UINT_PTR /*uIdSubclass*/,
                                                    DWORD_PTR dwRefData)
{
    if (msg == WM_MOUSEWHEEL)
    {
        OrgDetailPanel* self = reinterpret_cast<OrgDetailPanel*>(dwRefData);
        if (self != nullptr)
        {
            // 把滚轮转交本面板滚动逻辑（OnMouseWheel 只用 zDelta）。消费掉，
            // 不再交默认过程（只读单行 EDIT 自身也无内容可滚）。
            const short zDelta = static_cast<short>(HIWORD(wParam));
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            self->OnMouseWheel(pt, zDelta, static_cast<UINT>(LOWORD(wParam)));
            return 0;
        }
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ===========================================================================
// 员工详情 —— 权限与角色 tab
// ===========================================================================
int OrgDetailPanel::PaintUserRoles(HDC hdc, int y)
{
    const int left  = m_rcItem.left + kSidePad;
    const int right = m_rcItem.right - kSidePad;
    const int w     = right - left;

    // ---- 角色 ----
    DrawSectionTitle(hdc, left, y, w, _T("角色"), false);
    y += 24;
    struct Role { LPCTSTR label; LPCTSTR desc; };
    const Role roles[] =
    {
        { _T("企业管理员"), _T("全权管理本企业组织、成员、设置") },
        { _T("部门负责人"), _T("管理所属部门成员与权限") },
        { _T("普通成员"),   _T("仅查看与协作") },
    };
    const int kRoleGap = 10;
    const int roleW = (w - kRoleGap * 2) / 3;
    const int roleH = 78;
    for (int i = 0; i < 3; ++i)
    {
        const int rx = left + i * (roleW + kRoleGap);
        RECT card = { rx, y, rx + roleW, y + roleH };
        // 卡片下标 i（0 管理员/1 负责人/2 普通）与服务端角色编码相反，
        // 故选中判定用 (2 - i)。
        const bool sel = (m_userRole == 2 - i);
        ui::FillRoundRect(hdc, card, sel ? theme::kFlamingo50 : theme::kInk0,
                          theme::kRadiusMd);
        ui::StrokeRoundRect(hdc, card, sel ? theme::kFlamingo300 : theme::kInk150,
                            theme::kRadiusMd, 1.0f);
        RECT radio = { rx + 14, y + 14, rx + 28, y + 28 };
        ui::StrokeRoundRect(hdc, radio, sel ? theme::kFlamingo500 : theme::kInk300,
                            7, 1.5f);
        if (sel)
        {
            RECT dot = { radio.left + 4, radio.top + 4,
                         radio.right - 4, radio.bottom - 4 };
            ui::FillRoundRect(hdc, dot, theme::kFlamingo500, 4);
        }
        RECT rl = { rx + 34, y + 12, rx + roleW - 10, y + 30 };
        ui::DrawTextLine(hdc, roles[i].label, rl,
                         sel ? theme::kFlamingo700 : theme::kInk700,
                         ui::UiFont(theme::kFontSizeNormal, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT rd = { rx + 14, y + 36, rx + roleW - 12, y + roleH - 8 };
        ui::DrawTextLine(hdc, roles[i].desc, rd, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
        Hit h; h.m_rc = card; h.m_kind = 4; h.m_arg = i;
        m_hits.push_back(h);
    }
    y += roleH + kSectionGap;

    // ---- 权限矩阵 ----
    DrawSectionTitle(hdc, left, y, w, _T("权限矩阵"), false);
    y += 24;
    const int permRowH = 38;
    const int groupH   = 30;
    int cardH = 0;
    {
        CString g;
        for (int i = 0; i < kPermCount; ++i)
        {
            if (CString(kPerms[i].m_group) != g)
            {
                g = kPerms[i].m_group;
                cardH += groupH;
            }
            cardH += permRowH;
        }
    }
    RECT permCard = { left, y, right, y + cardH };
    ui::DrawCard(hdc, permCard);
    {
        CString g;
        int cy = y;
        for (int i = 0; i < kPermCount; ++i)
        {
            if (CString(kPerms[i].m_group) != g)
            {
                g = kPerms[i].m_group;
                RECT gh = { left, cy, right, cy + groupH };
                ui::FillSolidRect(hdc, gh, theme::kInk25);
                ui::FillLine(hdc, left, cy, w, 1, theme::kInk100);
                RECT gt = { left + 16, cy, right - 16, cy + groupH };
                ui::DrawTextLine(hdc, g, gt, theme::kInk500,
                                 ui::UiFont(theme::kFontSizeSmall, true),
                                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                cy += groupH;
            }
            const bool on = (i < static_cast<int>(m_userPerms.size()))
                          && m_userPerms[i] != 0;
            RECT rl = { left + 16, cy, right - 210, cy + permRowH };
            ui::DrawTextLine(hdc, kPerms[i].m_label, rl, theme::kInk700,
                             ui::UiFont(theme::kFontSizeNormal, false),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT rk = { right - 200, cy, right - 70, cy + permRowH };
            ui::DrawTextLine(hdc, kPerms[i].m_key, rk, theme::kInk300,
                             ui::UiFont(theme::kFontSizeSmall, false),
                             DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT tog = { right - 16 - 36, cy + (permRowH - 20) / 2,
                         right - 16,      cy + (permRowH + 20) / 2 };
            DrawToggle(hdc, tog, on);
            Hit h; h.m_rc = tog; h.m_kind = 3; h.m_arg = i;
            m_hits.push_back(h);
            cy += permRowH;
        }
    }
    y += cardH + kSectionGap;

    // ---- 所属群组 ----
    DrawSectionTitle(hdc, left, y, w, _T("所属群组"), false);
    y += 24;
    const TCHAR* const groups[] =
    {
        _T("全员"), _T("技术周会"), _T("前端组"),
        _T("产品评审"), _T("OKR 同步"), _T("跨部门协同"),
    };
    int gx = left;
    for (int i = 0; i < 6; ++i)
    {
        const int cw = ui::ChipWidth(hdc, groups[i], false) + 6;
        RECT chip = { gx, y, gx + cw, y + 24 };
        ui::DrawChip(hdc, chip, groups[i], theme::kInk600, theme::kInk50, false, 0);
        gx += cw + 8;
    }
    y += 24;
    return y;
}

// 子树（含 node 自身）里是否存在 id 为 uid 的员工。
static bool SubtreeHasUser(const OrgNode& node, const CString& uid)
{
    if (node.m_kind == OrgNodeKind::User)
    {
        return node.m_id == uid;
    }
    for (const auto& c : node.m_children)
    {
        if (SubtreeHasUser(c, uid))
        {
            return true;
        }
    }
    return false;
}

// ===========================================================================
// 员工详情 —— 服务号 tab
// ===========================================================================
int OrgDetailPanel::PaintUserServices(HDC hdc, int y)
{
    const int left  = m_rcItem.left + kSidePad;
    const int right = m_rcItem.right - kSidePad;
    const int w     = right - left;

    const CString entId = (m_ent != nullptr) ? m_ent->m_id : CString();
    const std::vector<Service>& svcs = DataStore::Instance().Services(entId);

    // 算出本员工能收到的服务号 + 主要覆盖原因。
    struct Covered { const Service* m_svc; CString m_reason; COLORREF m_clr; };
    std::vector<Covered> covered;
    for (const Service& s : svcs)
    {
        if (s.m_status == ServiceStatus::Draft)
        {
            continue;
        }
        CString  reason;
        COLORREF clr = theme::kInk400;
        for (const ServiceBinding& b : s.m_bindings)
        {
            if (b.m_kind == BindingKind::User && b.m_id == m_node->m_id)
            {
                reason = _T("直接绑定");
                clr    = theme::kColorOk;
                break;   // 直接绑定优先级最高
            }
            if (b.m_kind == BindingKind::Dept)
            {
                const OrgNode* d = DataStore::Instance().FindOrgNode(entId, b.m_id);
                if (d != nullptr && SubtreeHasUser(*d, m_node->m_id))
                {
                    reason = CString(_T("部门 · ")) + d->m_name;
                    clr    = theme::kColorInfo;
                }
            }
            else if (b.m_kind == BindingKind::Enterprise && reason.IsEmpty())
            {
                reason = _T("企业 · 全员");
                clr    = theme::kFlamingo600;
            }
        }
        if (!reason.IsEmpty())
        {
            Covered c;
            c.m_svc    = &s;
            c.m_reason = reason;
            c.m_clr    = clr;
            covered.push_back(c);
        }
    }

    // ---- 概览卡 ----
    RECT sum = { left, y, right, y + 72 };
    ui::DrawCard(hdc, sum);
    RECT ib = { left + 18, y + 16, left + 18 + 40, y + 16 + 40 };
    ui::FillRoundRect(hdc, ib, theme::kFlamingo50, theme::kRadiusMd);
    RECT ii = { (ib.left + ib.right) / 2 - 11, (ib.top + ib.bottom) / 2 - 11,
                (ib.left + ib.right) / 2 + 11, (ib.top + ib.bottom) / 2 + 11 };
    ui::DrawIcon(hdc, ui::Icon::Megaphone, ii, theme::kFlamingo600);
    RECT sl = { ib.right + 14, y + 16, right - 18, y + 34 };
    ui::DrawTextLine(hdc, _T("该员工客户端可见的服务号"), sl, theme::kInk400,
                     ui::UiFont(theme::kFontSizeSmall, false),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    CString cntText;
    cntText.Format(_T("%d 个服务号"), static_cast<int>(covered.size()));
    RECT sv = { ib.right + 14, y + 34, right - 18, y + 58 };
    ui::DrawTextLine(hdc, cntText, sv, theme::kInk700,
                     ui::UiFont(theme::kFontSizeHeading, true),
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    y += 72 + kSectionGap;

    // ---- 覆盖的服务号列表 ----
    DrawSectionTitle(hdc, left, y, w, _T("覆盖范围"), false);
    y += 24;
    if (covered.empty())
    {
        RECT box = { left, y, right, y + 70 };
        ui::FillRoundRect(hdc, box, theme::kInk0, theme::kRadiusLg);
        ui::StrokeRoundRect(hdc, box, theme::kInk150, theme::kRadiusLg, 1.0f);
        ui::DrawTextLine(hdc, _T("该员工当前未被任何服务号覆盖"), box, theme::kInk400,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += 70;
        return y;
    }
    const int svcRowH = 58;
    RECT listCard = { left, y, right,
                      y + svcRowH * static_cast<int>(covered.size()) };
    ui::DrawCard(hdc, listCard);
    for (size_t i = 0; i < covered.size(); ++i)
    {
        const Service* s = covered[i].m_svc;
        const int top = y + static_cast<int>(i) * svcRowH;
        if (i > 0)
        {
            ui::FillLine(hdc, left + 14, top, w - 28, 1, theme::kInk100);
        }
        const int cy = top + svcRowH / 2;
        RECT av = { left + 14, cy - 17, left + 14 + 34, cy + 17 };
        ui::DrawAvatar(hdc, av, s->m_color, s->m_glyph);
        RECT nm = { av.right + 12, top + 9, right - 220, top + 29 };
        ui::DrawTextLine(hdc, s->m_name, nm, theme::kInk700,
                         ui::UiFont(theme::kFontSizeNormal, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        RECT ds = { av.right + 12, top + 29, right - 220, top + 49 };
        ui::DrawTextLine(hdc, s->m_desc, ds, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        // 覆盖原因徽章（右对齐）。
        const int rw = ui::ChipWidth(hdc, covered[i].m_reason, true);
        RECT chip = { right - 16 - rw, cy - 10, right - 16, cy + 10 };
        ui::DrawChip(hdc, chip, covered[i].m_reason, theme::kInk600,
                     theme::kInk50, true, covered[i].m_clr);
    }
    y += svcRowH * static_cast<int>(covered.size());
    return y;
}

// ===========================================================================
// 员工详情 —— 消息与会话 tab
// ===========================================================================
int OrgDetailPanel::PaintUserSessions(HDC hdc, int y)
{
    const int left  = m_rcItem.left + kSidePad;
    const int right = m_rcItem.right - kSidePad;
    const int w     = right - left;

    DrawSectionTitle(hdc, left, y, w, _T("消息活跃度（近 14 天）"), false);
    y += 24;

    if (!m_msgStatsLoaded)
    {
        // 数据未到 —— 占位提示。
        RECT tip = { left, y, right, y + 40 };
        ui::DrawTextLine(hdc, _T("正在加载消息统计…"), tip, theme::kInk400,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return y + 40;
    }

    // 近 14 天发送趋势 → DrawMiniBars 需要的整型数组。
    int bars[14] = { 0 };
    const int trendN = (static_cast<int>(m_msgTrend.size()) < 14)
        ? static_cast<int>(m_msgTrend.size()) : 14;
    for (int i = 0; i < trendN; ++i)
    {
        bars[i] = m_msgTrend[i].m_count;
    }

    // ---- 统计卡（发送 / 接收 / 会话数）----
    const int tileGap = 12;
    const int tileW = (w - tileGap * 2) / 3;
    const int tileH = 96;
    struct Tile { LPCTSTR label; CString value; bool bars; COLORREF barClr; };
    CString vSent;
    CString vRecv;
    CString vConv;
    vSent.Format(_T("%d"), m_msgSent);
    vRecv.Format(_T("%d"), m_msgReceived);
    vConv.Format(_T("%d"), m_msgConversations);
    Tile tiles[] =
    {
        { _T("发送"),   vSent, true,  theme::kFlamingo500 },
        { _T("接收"),   vRecv, false, theme::kColorInfo   },
        { _T("会话数"), vConv, false, theme::kInk400      },
    };
    const int tileCount = static_cast<int>(sizeof(tiles) / sizeof(tiles[0]));
    for (int i = 0; i < tileCount; ++i)
    {
        const int tx = left + i * (tileW + tileGap);
        RECT card = { tx, y, tx + tileW, y + tileH };
        ui::DrawCard(hdc, card);
        RECT tl = { tx + 14, y + 12, tx + tileW - 14, y + 28 };
        ui::DrawTextLine(hdc, tiles[i].label, tl, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT tv = { tx + 14, y + 28, tx + tileW - 14, y + 56 };
        ui::DrawTextLine(hdc, tiles[i].value, tv, theme::kInk700,
                         ui::UiFont(theme::kFontSizeHeading, true),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (tiles[i].bars)
        {
            RECT br = { tx + 14, y + 60, tx + tileW - 14, y + tileH - 14 };
            DrawMiniBars(hdc, br, bars, 14, tiles[i].barClr);
        }
    }
    y += tileH + kSectionGap;
    return y;
}

// ===========================================================================
// 员工详情 —— 设备登录 tab
// ===========================================================================
int OrgDetailPanel::PaintUserDevices(HDC hdc, int y)
{
    const int left  = m_rcItem.left + kSidePad;
    const int right = m_rcItem.right - kSidePad;
    const int w     = right - left;

    // ---- 已登录设备（来自 user/loginrecords.do 的 devices）----
    DrawSectionTitle(hdc, left, y, w, _T("已登录设备"), false);
    if (!m_loginDevices.empty() && m_node != nullptr)
    {
        // 标题行右侧：强制下线全部设备按钮（仅在有在线设备时显示）。
        const int kickW = 132;
        RECT kickBtn = { right - kickW, y - 4, right, y - 4 + 26 };
        ui::FillRoundRect(hdc, kickBtn, theme::kInk0, theme::kRadiusSm);
        ui::StrokeRoundRect(hdc, kickBtn, theme::kInk150, theme::kRadiusSm, 1.0f);
        ui::DrawTextLine(hdc, _T("强制下线全部设备"), kickBtn, theme::kColorDanger,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        Hit h;
        h.m_rc = kickBtn;
        h.m_kind = 2;
        h.m_arg = static_cast<int>(OrgTree::Action::KickDevices);
        h.m_nodeId = m_node->m_id;
        m_hits.push_back(h);
    }
    y += 24;
    if (m_loginDevices.empty())
    {
        RECT box = { left, y, right, y + 64 };
        ui::FillRoundRect(hdc, box, theme::kInk0, theme::kRadiusLg);
        ui::StrokeRoundRect(hdc, box, theme::kInk150, theme::kRadiusLg, 1.0f);
        ui::DrawTextLine(hdc, _T("该员工当前没有在线设备"), box, theme::kInk400,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += 64 + kSectionGap;
    }
    else
    {
        const int devH = 56;
        for (size_t i = 0; i < m_loginDevices.size(); ++i)
        {
            const int top = y + static_cast<int>(i) * (devH + 10);
            RECT card = { left, top, right, top + devH };
            ui::DrawCard(hdc, card);
            RECT ib = { left + 16, top + 11, left + 16 + 34, top + 11 + 34 };
            ui::FillRoundRect(hdc, ib, theme::kInk50, theme::kRadiusMd);
            RECT ii = { (ib.left + ib.right) / 2 - 9, (ib.top + ib.bottom) / 2 - 9,
                        (ib.left + ib.right) / 2 + 9, (ib.top + ib.bottom) / 2 + 9 };
            ui::DrawIcon(hdc, ui::Icon::Shield, ii, theme::kFlamingo600);
            // 服务端只回 clienttype + status，按类型给出名称。
            LPCTSTR typeText =
                (m_loginDevices[i].m_clientType == kClientTypePc)
                    ? _T("PC 客户端")
                    : (m_loginDevices[i].m_clientType == kClientTypeMobile)
                        ? _T("移动客户端") : _T("客户端");
            RECT nm = { ib.right + 14, top, right - 90, top + devH };
            ui::DrawTextLine(hdc, typeText, nm, theme::kInk700,
                             ui::UiFont(theme::kFontSizeNormal, true),
                             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            const int cw = ui::ChipWidth(hdc, _T("在线"), true);
            RECT chip = { right - 16 - cw, top + (devH - 20) / 2,
                          right - 16,      top + (devH + 20) / 2 };
            ui::DrawChip(hdc, chip, _T("在线"), theme::kInk600, theme::kInk50,
                         true, theme::kColorOk);
        }
        y += static_cast<int>(m_loginDevices.size()) * (devH + 10) - 10
             + kSectionGap;
    }

    // ---- 登录历史（来自 user/loginrecords.do 的 records）----
    DrawSectionTitle(hdc, left, y, w, _T("登录历史"), false);
    y += 24;
    if (m_loginRecords.empty())
    {
        RECT box = { left, y, right, y + 64 };
        ui::FillRoundRect(hdc, box, theme::kInk0, theme::kRadiusLg);
        ui::StrokeRoundRect(hdc, box, theme::kInk150, theme::kRadiusLg, 1.0f);
        ui::DrawTextLine(hdc, _T("暂无登录记录"), box, theme::kInk400,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += 64;
        return y;
    }
    const int logH = 38;
    const int logN = static_cast<int>(m_loginRecords.size());
    RECT logCard = { left, y, right, y + 36 + logH * logN };
    ui::DrawCard(hdc, logCard);
    // 表头。
    int colTs = left + 16, colDev = left + 200, colIp = left + 320,
        colLoc = left + 470, colRes = right - 120;
    ui::FillLine(hdc, left, y + 35, w, 1, theme::kInk100);
    {
        HFONT hf = ui::UiFont(theme::kFontSizeSmall, false);
        ui::DrawTextLine(hdc, _T("时间"), RECT{ colTs, y, colDev, y + 35 },
                         theme::kInk400, hf, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ui::DrawTextLine(hdc, _T("设备"), RECT{ colDev, y, colIp, y + 35 },
                         theme::kInk400, hf, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ui::DrawTextLine(hdc, _T("IP"), RECT{ colIp, y, colLoc, y + 35 },
                         theme::kInk400, hf, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ui::DrawTextLine(hdc, _T("位置"), RECT{ colLoc, y, colRes, y + 35 },
                         theme::kInk400, hf, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ui::DrawTextLine(hdc, _T("结果"), RECT{ colRes, y, right - 16, y + 35 },
                         theme::kInk400, hf, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    for (int i = 0; i < logN; ++i)
    {
        const LoginRecord& rec = m_loginRecords[i];
        const int top = y + 36 + i * logH;
        if (i > 0)
        {
            ui::FillLine(hdc, left + 16, top, w - 32, 1, theme::kInk100);
        }
        HFONT rf = ui::UiFont(theme::kFontSizeSmall, false);
        ui::DrawTextLine(hdc, rec.m_ts, RECT{ colTs, top, colDev, top + logH },
                         theme::kInk500, rf, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ui::DrawTextLine(hdc, rec.m_device, RECT{ colDev, top, colIp, top + logH },
                         theme::kInk700, rf,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        ui::DrawTextLine(hdc, rec.m_ip, RECT{ colIp, top, colLoc, top + logH },
                         theme::kInk500, rf, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ui::DrawTextLine(hdc, rec.m_location, RECT{ colLoc, top, colRes, top + logH },
                         theme::kInk700, rf,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        // 登录结果：服务端 result 0=成功 1=失败。
        const bool ok = (rec.m_result == 0);
        LPCTSTR rtxt = ok ? _T("成功") : _T("失败");
        COLORREF rclr = ok ? theme::kColorOk : theme::kColorDanger;
        const int cw = ui::ChipWidth(hdc, rtxt, true);
        RECT chip = { colRes, top + (logH - 20) / 2, colRes + cw, top + (logH + 20) / 2 };
        ui::DrawChip(hdc, chip, rtxt, theme::kInk600, theme::kInk50, true, rclr);
    }
    y += 36 + logH * logN;
    return y;
}

// ===========================================================================
// 员工详情 —— 操作历史 tab
// ===========================================================================
int OrgDetailPanel::PaintUserHistory(HDC hdc, int y)
{
    const int left  = m_rcItem.left + kSidePad;
    const int right = m_rcItem.right - kSidePad;
    const int w     = right - left;

    DrawSectionTitle(hdc, left, y, w, _T("操作历史（近 30 天）"), false);
    y += 24;

    if (m_activity.empty())
    {
        RECT box = { left, y, right, y + 64 };
        ui::FillRoundRect(hdc, box, theme::kInk0, theme::kRadiusLg);
        ui::StrokeRoundRect(hdc, box, theme::kInk150, theme::kRadiusLg, 1.0f);
        ui::DrawTextLine(hdc, _T("最近 30 天暂无操作记录"), box, theme::kInk400,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        y += 64;
        return y;
    }

    const int actN = static_cast<int>(m_activity.size());
    const int rowH = 46;

    // 时间线竖轴。
    const int railX = left + 6;
    ui::FillLine(hdc, railX, y + 6, 2, rowH * actN - 12, theme::kInk100);

    for (int i = 0; i < actN; ++i)
    {
        const ActivityEntry& a = m_activity[i];
        const Severity sev = SeverityFromInt(a.m_severity);
        const int top = y + i * rowH;
        // 时间线圆点。
        RECT dot = { railX - 4, top + 12, railX + 8, top + 24 };
        ui::FillRoundRect(hdc, dot, theme::kInk0, 6);
        ui::StrokeRoundRect(hdc, dot, ui::SeverityColor(sev), 6, 2.0f);

        // 动作键徽章。
        const int kw = ui::ChipWidth(hdc, a.m_action, false);
        RECT keyChip = { left + 22, top + 10, left + 22 + kw, top + 30 };
        ui::DrawChip(hdc, keyChip, a.m_action, theme::kInk600, theme::kInk50,
                     false, 0);
        // 描述。
        RECT desc = { keyChip.right + 10, top, right - 140, top + rowH };
        ui::DrawTextLine(hdc, a.m_detail, desc, theme::kInk700,
                         ui::UiFont(theme::kFontSizeNormal, false),
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        // 时间。
        RECT ts = { right - 134, top, right, top + rowH };
        ui::DrawTextLine(hdc, a.m_ts, ts, theme::kInk400,
                         ui::UiFont(theme::kFontSizeSmall, false),
                         DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    y += rowH * actN;
    return y;
}

bool OrgDetailPanel::OnLButtonUp(POINT pt, UINT /*mkFlags*/)
{
    for (const Hit& h : m_hits)
    {
        if (!::PtInRect(&h.m_rc, pt))
        {
            continue;
        }
        if (h.m_kind == 0)            // 切 tab
        {
            if (h.m_arg != m_userTab)
            {
                m_userTab = h.m_arg;
                m_scrollY = 0;
                Invalidate();
            }
        }
        else if (h.m_kind == 1)       // 选节点
        {
            if (m_selFn != nullptr)
            {
                m_selFn(m_cbUser, h.m_nodeId);
            }
        }
        else if (h.m_kind == 2)       // 动作按钮
        {
            if (m_actFn != nullptr)
            {
                m_actFn(m_cbUser, static_cast<OrgTree::Action>(h.m_arg), h.m_nodeId);
            }
        }
        else if (h.m_kind == 3)       // 切换权限开关
        {
            if (h.m_arg >= 0 && h.m_arg < static_cast<int>(m_userPerms.size())
                && m_node != nullptr)
            {
                // 乐观翻转本地状态，并上报 SetPerms 由 AdminFrame 调
                // user/setpermissions.do 落库。
                m_userPerms[h.m_arg] = m_userPerms[h.m_arg] ? 0 : 1;
                Invalidate();
                if (m_actFn != nullptr)
                {
                    m_actFn(m_cbUser, OrgTree::Action::SetPerms, m_node->m_id);
                }
            }
        }
        else if (h.m_kind == 4)       // 选择角色
        {
            // 卡片下标转服务端角色编码（与下标相反）。
            const int newRole = 2 - h.m_arg;
            if (newRole != m_userRole && m_node != nullptr)
            {
                m_userRole = newRole;
                Invalidate();
                if (m_actFn != nullptr)
                {
                    m_actFn(m_cbUser, OrgTree::Action::SetRole, m_node->m_id);
                }
            }
        }
        return true;
    }
    return true;
}

bool OrgDetailPanel::OnMouseWheel(POINT /*pt*/, short zDelta, UINT /*mkFlags*/)
{
    m_scrollY -= (zDelta / WHEEL_DELTA) * kRowH * 3;
    ClampScroll();
    // 同步滚动量给悬浮滚动条并令其淡入（停止滚动约 1 秒后自动淡出）。
    m_panelSb->SetMetrics(m_contentH, m_scrollY);
    m_panelSb->Flash();
    Invalidate();
    return true;
}

bool OrgDetailPanel::OnSetCursor(POINT /*pt*/)
{
    ::SetCursor(::LoadCursor(nullptr, IDC_ARROW));
    return true;
}

} // namespace flamingoadmin
