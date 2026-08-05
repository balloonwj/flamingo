/**
 *  GroupAtDropdown.cpp
 *
 *  群聊「@ 成员选择」浮层的实现。形态与建窗方式对齐 BalloonSearch/SearchDropdown，
 *  列表本体复用 balloonui 的 DuiListBox。方案见 docs/群聊@提醒方案.md §6。
 *
 *  balloonwj@qq.com   2026-07-31
 */

#include "stdafx.h"
#include "GroupAtDropdown.h"

#include <memory>

#include "../IULog.h"
#include "../BalloonMain/PopupMenuPlacement.h"   // ClampPopupMenuToWorkArea（浮层不越屏）
#include "../../balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../balloonui/balloonui/Controls/List/DuiListBox.h"

namespace balloonchatwnd {

namespace {

// ---- 尺寸与配色（逻辑像素）------------------------------------------------

const int kDropdownWidth   = 220;   // 浮层宽
const int kItemHeight      = 30;    // 每个成员一行的行高
const int kMaxVisibleItems = 8;     // 最多同时显示几行，超出由列表自身滚动
const int kCardPadding     = 4;     // 卡片内边距

const COLORREF kCardBg     = RGB(0xFF, 0xFF, 0xFF);   // 卡片白底
const COLORREF kCardBorder = RGB(0xE3, 0xE0, 0xDC);   // 卡片描边浅灰

// 列表控件在浮层内的 ctrl id（浮层内唯一即可）。
const UINT kIdMemberList = 1;

// 浮窗窗口类名。带 CS_DROPSHADOW 让浮层有系统投影（DuiHost 的 __DuiHost__ 类无
// 投影），CS_SAVEBITS 弹出时保存底图、收起后免重绘底层。与搜索下拉同款做法。
const TCHAR* kAtDropdownClassName = _T("__FlamingoGroupAtDropdown__");

// 注册浮窗窗口类（幂等）。窗口过程用 DefWindowProc 占位 —— 真正的消息处理由随后
// 的 SubclassWindow 交给 DuiHost 接管。
void EnsureAtDropdownClassRegistered()
{
    static bool s_registered = false;
    if (s_registered)
    {
        return;
    }
    WNDCLASSEX wc;
    ::ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DROPSHADOW | CS_SAVEBITS;
    wc.lpfnWndProc   = ::DefWindowProc;
    wc.hInstance     = ::GetModuleHandle(NULL);
    wc.hCursor       = ::LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kAtDropdownClassName;
    ::RegisterClassEx(&wc);
    s_registered = true;
}

// 「点某一行就选中」用的自定义通知码。为什么不能直接用 DuiListBox 自带的
// DUIN_VALUECHANGED：那条通知只在<u>选中项发生变化</u>时才发
// （DuiListBox.cpp:184 的 `if (notify && changed)`）。浮层弹出时会默认高亮第一项，
// 用户点的又往往正是第一项 —— 选中没变，一条通知都不发，点了没反应。
// 故子类化 DuiListBox，在鼠标抬起时无条件补一条"这一行被点了"。
const UINT DUIN_AT_ROW_PICKED = balloonwjui::DUIN_CUSTOM + 31;

/**
 *  AtMemberList —— @ 浮层专用的成员列表。
 *
 *  只比 DuiListBox 多一件事：鼠标在某一行抬起时，无条件发一条 DUIN_AT_ROW_PICKED
 *  （extra = 行下标），不管这一行是不是已经处于选中态。除此之外行为完全同基类。
 *
 *  选在<u>抬起</u>而不是按下时发：浮层收到后会立刻隐藏自己，若在按下时就提交，
 *  这一次点击的鼠标抬起会落到浮层底下的窗口上，造成误触。
 */
class AtMemberList : public balloonwjui::DuiListBox
{
public:
    bool OnLButtonUp(POINT pt, UINT mkFlags) override
    {
        // 先让基类走完它的选中 / 拖拽收尾逻辑 —— 按下时基类已把 m_curSel 设成了
        // 被点的那一行，所以这里直接读 GetCurSel() 就是"用户点的行"，不必自己按
        // 坐标算行号（DuiListBox 的 IndexFromPoint 与滚动偏移都是私有的，算不了，
        // 硬抄一份还会跟基类的滚动实现脱节）。
        const bool handled = balloonwjui::DuiListBox::OnLButtonUp(pt, mkFlags);

        const int idx = GetCurSel();
        if (GetItemCount() > 0 && idx >= 0)
        {
            NotifyParent(DUIN_AT_ROW_PICKED, (LPARAM)idx);
        }
        return handled;
    }
};

// 大小写不敏感的前缀匹配。群名片 / 昵称常含中文，中文没有大小写之分，这里主要
// 是让英文名 "Alice" 能被 "al" 匹配到。
bool StartsWithNoCase(const CString& text, const CString& prefix)
{
    if (prefix.IsEmpty())
    {
        return true;
    }
    if (text.GetLength() < prefix.GetLength())
    {
        return false;
    }
    CString head = text.Left(prefix.GetLength());
    return head.CompareNoCase(prefix) == 0;
}

} // anonymous namespace

// =======================================================================
// 构造 / 析构
// =======================================================================

CGroupAtDropdown::CGroupAtDropdown()
    : m_built(false)
    , m_list(NULL)
    , m_pickCb(NULL)
    , m_pickUserdata(NULL)
{
    m_anchor.x = 0;
    m_anchor.y = 0;
}

CGroupAtDropdown::~CGroupAtDropdown()
{
    // 客户区控件树由 DuiHost 基类析构释放，但<u>窗口本身必须在这里显式销毁</u>：
    //
    //   1) 本浮层是「无 parent、无 owner」的顶层 WS_POPUP 窗口（见 EnsureCreated），
    //      宿主群聊窗被 DestroyWindow 时系统不会连带销毁它；Hide() 也只是 SW_HIDE。
    //   2) 基类 DuiHost 的析构<u>不</u>兜底 DestroyWindow（DuiHost.cpp:19），这一点与
    //      DuiFrameWindow（DuiFrameWindow.cpp:815）、DuiPopupHost（DuiPopupHost.cpp:11）
    //      不同 —— 那两个基类都会在析构里销毁自己的窗口。
    //
    // 于是本对象析构时 m_hWnd 仍然有效，一路走到 ATL CWindowImplRoot 的析构断言
    // （atlwin.h:3238 "Object deleted before window was destroyed"），表现为「开过
    // 群聊窗之后从托盘菜单退出即崩溃」。注意浮窗是在群聊窗客户区初始化时就提前
    // 建好的（BalloonGroupChatWindow.cpp 里的 EnsureCreated），所以哪怕用户一次
    // '@' 都没敲过也照样触发。
    //
    // 收尾方式与 CBalloonMainPanel 对 m_searchDropdown、CMainDlg 对 m_trayMsgTipPopup
    // 的处理同源，区别是就近放在本类自己的析构里，这样所有宿主都自动受益。
    if (m_hWnd != NULL)
    {
        // 先摘掉选中回调再销毁：销毁过程中控件树被释放，万一有残留通知冒上来，
        // 回调里的 userdata（宿主群聊窗）此刻可能已经进入析构，不能再回调过去。
        m_pickCb       = NULL;
        m_pickUserdata = NULL;

        LOG_INFO(_T("群聊 @ 浮层析构时窗口仍存活，显式销毁以避免 ATL 析构断言。"));
        DestroyWindow();
    }
}

// =======================================================================
// 创建 / 控件树
// =======================================================================

bool CGroupAtDropdown::EnsureCreated()
{
    if (m_hWnd != NULL)
    {
        return true;
    }

    EnsureAtDropdownClassRegistered();

    // 顶层无边框浮窗：WS_POPUP；WS_EX_TOOLWINDOW 不进任务栏；WS_EX_NOACTIVATE
    // 点击不抢激活（关键 —— 否则一点浮层，输入框就失焦、插入位置就丢了）。
    // owner 传 NULL：让 DuiHost::SendNotify 的 GetParent 为空、通知自路由回本窗口。
    HWND hWnd = ::CreateWindowEx(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kAtDropdownClassName, NULL,
        WS_POPUP,
        0, 0, 0, 0,
        NULL, NULL, ::GetModuleHandle(NULL), NULL);
    if (hWnd == NULL)
    {
        LOG_WARNING(_T("群聊 @ 浮层创建失败。"));
        return false;
    }

    if (!SubclassWindow(hWnd))
    {
        ::DestroyWindow(hWnd);
        LOG_WARNING(_T("群聊 @ 浮层 SubclassWindow 失败。"));
        return false;
    }

    BuildContentOnce();
    return true;
}

void CGroupAtDropdown::BuildContentOnce()
{
    if (m_built)
    {
        return;
    }

    // 根：白底卡片（1px 浅灰描边），里面就一个成员列表。
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(kCardPadding);
    root->SetGap(0);
    root->SetBgColor(kCardBg);
    root->SetBorderColor(kCardBorder);
    root->SetBorderWidth(1.0f);

    std::unique_ptr<balloonwjui::DuiListBox> list(new AtMemberList());
    list->SetCtrlId(kIdMemberList);
    list->SetItemHeight(kItemHeight);
    m_list = list.get();
    root->AddChild(std::move(list), balloonwjui::DuiLayout::Hint().Weight(1));

    SetRoot(std::move(root));
    m_built = true;
}

// =======================================================================
// 显示 / 过滤 / 收起
// =======================================================================

void CGroupAtDropdown::SetPickCallback(AtPickCallback cb, void* userdata)
{
    m_pickCb       = cb;
    m_pickUserdata = userdata;
}

void CGroupAtDropdown::Show(const POINT& anchorScreenPt,
                            const std::vector<AtCandidate>& members,
                            const CString& prefix)
{
    if (!EnsureCreated())
    {
        return;
    }
    m_members = members;
    m_prefix  = prefix;
    ApplyFilter();
    RefillAndLayout(&anchorScreenPt);
}

void CGroupAtDropdown::UpdateFilter(const CString& prefix)
{
    if (m_hWnd == NULL)
    {
        return;
    }
    // 前缀没变就什么都不做 —— 尤其<u>不能</u>重填列表。
    //
    // 宿主是在"输入框内容可能变了"时调进来的，而"可能"二字很宽：按方向键、在输入框
    // 里点一下挪光标，都会走到这里，可它们根本没改动文本、前缀原样不变。若照样重填，
    // RefillAndLayout 末尾的 SetCurSel(0) 会把高亮打回第一项 —— 实测表现就是"按一下
    // 方向键，高亮跳一格又立刻弹回列表最上面"，方向键等于废掉。
    //
    // 前缀不变则过滤结果必然不变（候选全集只在 Show 时设一次），所以直接返回是安全的。
    if (prefix == m_prefix)
    {
        return;
    }

    m_prefix = prefix;
    ApplyFilter();
    // 过滤后可能一个候选都不剩，此时 RefillAndLayout 会自行收起浮层。
    RefillAndLayout(NULL);
}

void CGroupAtDropdown::Hide()
{
    if (m_hWnd != NULL && ::IsWindowVisible(m_hWnd))
    {
        ::ShowWindow(m_hWnd, SW_HIDE);
    }
}

bool CGroupAtDropdown::IsShowing() const
{
    return m_hWnd != NULL && ::IsWindowVisible(m_hWnd) != FALSE;
}

void CGroupAtDropdown::ApplyFilter()
{
    m_filtered.clear();
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        if (StartsWithNoCase(m_members[i].m_name, m_prefix))
        {
            m_filtered.push_back(m_members[i]);
        }
    }
}

void CGroupAtDropdown::RefillAndLayout(const POINT* anchorScreenPt)
{
    if (m_list == NULL)
    {
        return;
    }

    // 一个候选都不剩：收起浮层，绝不能摆一个空框出来。
    //
    // 此时用户敲的那个 "@某某" 匹配不到任何成员，就是一段普通文字而已（典型：
    // 键入 "@xdggdfff..." 这类不存在的名字），挂个空白候选框既没意义又挡住输入。
    // 这道判断<u>必须</u>放在这里而不是各调用点：Show 与 UpdateFilter 都会走到本函数，
    // 早先只有 UpdateFilter 自己挡了一道，于是「候选耗尽 → 浮层收起 → 再敲一个字
    // 走 Show 这一支」时就漏了，空框重新冒出来（见 snapshots/201623.png）。
    if (m_filtered.empty())
    {
        Hide();
        return;
    }

    // 重填列表。列表项与 m_filtered 一一对应，选中项按下标回查，故 LPARAM 不用
    // 存 userid（LPARAM 在 Win32 上只有 32 位，装不下 int64_t 的 userid）。
    m_list->DeleteAllItems();
    for (size_t i = 0; i < m_filtered.size(); ++i)
    {
        m_list->AddItem(m_filtered[i].m_name, (LPARAM)i);
    }
    if (!m_filtered.empty())
    {
        // 默认高亮第一项，回车即选它。
        // notify 必须传 false —— DuiListBox::SetCurSel 默认会发 DUIN_VALUECHANGED，
        // 而本类把该通知当作"用户点选了某项"来提交。程序化设高亮若也发通知，就成了
        // "浮层一弹出就自动选中第一个人"，插入又改动输入框文本、触发重算再弹浮层、
        // 再设高亮……绕成死循环，表现为直接选中 + 列表闪烁。
        m_list->SetCurSel(0, false);
    }

    if (anchorScreenPt != NULL)
    {
        m_anchor = *anchorScreenPt;
    }

    // 高度按项数算，最多显示 kMaxVisibleItems 行（再多由列表自身滚动）。
    int visible = (int)m_filtered.size();
    if (visible > kMaxVisibleItems)
    {
        visible = kMaxVisibleItems;
    }
    if (visible < 1)
    {
        visible = 1;
    }
    const int height = visible * kItemHeight + kCardPadding * 2;

    // 落点必须夹进锚点所在显示器的工作区 —— 输入框靠近屏幕底部时，浮层若直接
    // 向下展开就会跑到桌面外（开发约定文档的菜单不越屏规则，统一走这个函数）。
    SIZE  want;
    want.cx = kDropdownWidth;
    want.cy = height;
    const POINT pos = balloonmain::ClampPopupMenuToWorkArea(m_anchor, want);

    // 位置与尺寸都没变、且已经显示着时，只重绘内容，不再 SetWindowPos ——
    // 每键入一个字都重新摆一次窗口会肉眼可见地闪。
    RECT cur;
    ::GetWindowRect(m_hWnd, &cur);
    const bool sameGeom = ::IsWindowVisible(m_hWnd)
                       && cur.left == pos.x && cur.top == pos.y
                       && (cur.right - cur.left) == kDropdownWidth
                       && (cur.bottom - cur.top) == height;
    if (!sameGeom)
    {
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, pos.x, pos.y, kDropdownWidth, height,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    ::InvalidateRect(m_hWnd, NULL, FALSE);
}

// =======================================================================
// 键盘驱动（宿主在浮层可见时转过来）
// =======================================================================

void CGroupAtDropdown::MoveSelection(int delta)
{
    if (m_list == NULL || m_filtered.empty())
    {
        return;
    }
    const int count = (int)m_filtered.size();
    int cur = m_list->GetCurSel();
    if (cur < 0)
    {
        cur = 0;
    }
    // 首尾之间循环，符合下拉候选的一般手感。
    int next = (cur + delta) % count;
    if (next < 0)
    {
        next += count;
    }
    // 同样不发通知：方向键只移动高亮，提交由回车 / Tab / 鼠标点击触发。
    m_list->SetCurSel(next, false);
    // 把新高亮项滚进可视范围。浮层最多同时显示 kMaxVisibleItems 行，候选更多时
    // 列表自身会滚动；少了这一步，方向键按过第 8 个之后高亮就跑到看不见的地方，
    // 用户完全不知道当前选到了谁（首尾循环时尤其明显 —— 从末项跳回首项）。
    m_list->EnsureVisible(next);
    ::InvalidateRect(m_hWnd, NULL, FALSE);
}

void CGroupAtDropdown::ScrollByWheel(short zDelta)
{
    if (m_list == NULL || m_filtered.empty())
    {
        return;
    }
    // DuiListBox 自己就会按 zDelta 滚动并夹紧范围，这里只需给它一个"落在列表内"
    // 的点即可 —— 取列表矩形的中心，保证命中；真实光标位置对滚动结果没有影响。
    const RECT rc = m_list->GetRect();
    POINT pt;
    pt.x = (rc.left + rc.right) / 2;
    pt.y = (rc.top + rc.bottom) / 2;

    if (m_list->OnMouseWheel(pt, zDelta, 0))
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CGroupAtDropdown::CommitCurrent()
{
    if (m_list == NULL || m_filtered.empty())
    {
        return;
    }
    int cur = m_list->GetCurSel();
    if (cur < 0 || cur >= (int)m_filtered.size())
    {
        cur = 0;
    }
    const AtCandidate picked = m_filtered[cur];

    // 先收起再回调：回调里宿主要往输入框插字并把焦点交回 EDIT，浮层留着会碍事。
    Hide();

    if (m_pickCb != NULL)
    {
        m_pickCb(m_pickUserdata, picked.m_uid, picked.m_name);
    }
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CGroupAtDropdown::OnMouseActivate(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM /*lParam*/, BOOL& bHandled)
{
    // 点本浮层既不激活窗口、也不夺焦点，鼠标消息照常投递 —— 这样输入框始终持有
    // 键盘焦点，宿主那些"失焦 / 失活就收起浮层"的逻辑不会在鼠标按下时误触发。
    bHandled = TRUE;
    return MA_NOACTIVATE;
}

LRESULT CGroupAtDropdown::OnChildNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                        LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify == NULL || pNotify->ctrlId != kIdMemberList)
    {
        bHandled = FALSE;
        return 0;
    }

    // 点某一行即提交 —— @ 浮层是"点一下就选人"的交互，不需要再双击确认。
    // 只认子类补发的 DUIN_AT_ROW_PICKED：基类自带的 DUIN_VALUECHANGED 在"点的
    // 正是已高亮那一行"时根本不发，靠它会漏掉最常见的一次点击。
    if (pNotify->code == DUIN_AT_ROW_PICKED)
    {
        const int idx = (int)pNotify->extra;
        if (idx >= 0 && idx < (int)m_filtered.size())
        {
            m_list->SetCurSel(idx, false);
            CommitCurrent();
        }
        return 0;
    }

    bHandled = FALSE;
    return 0;
}

} // namespace balloonchatwnd
