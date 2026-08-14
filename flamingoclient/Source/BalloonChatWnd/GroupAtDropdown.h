/**
 *  GroupAtDropdown.h
 *
 *  群聊输入框的「@ 成员选择」浮层 —— 在输入框里键入 `@`（或点「@提醒」按钮）时
 *  弹出，列出本群成员，支持继续键入做前缀过滤，选中后由宿主把 `@显示名 ` 插进
 *  输入框。
 *
 *  形态与实现都对齐已有的 `BalloonSearch/SearchDropdown`：无边框顶层浮窗
 *  （WS_POPUP + WS_EX_TOOLWINDOW 不进任务栏 + WS_EX_NOACTIVATE 点击不抢激活，
 *  保证输入框焦点不丢），owner 传 NULL 让 DuiHost 的通知自路由回本窗口。
 *  列表本体直接用 balloonui 的 DuiListBox，不自绘（遵循开发约定文档「新增控件须
 *  最大化复用 balloonui」）。
 *
 *  落点必须经 balloonmain::ClampPopupMenuToWorkArea 夹取，保证不跑到桌面外
 *  （开发约定文档的菜单不越屏规则）。
 *
 *  整体方案见 docs/群聊@提醒方案.md §6。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-07-31
 */
#pragma once

#include <vector>
#include <string>

#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace balloonwjui { class DuiListBox; }

namespace balloonchatwnd {

/**
 *  浮层里的一个候选成员。
 */
struct AtCandidate
{
    int64_t m_uid;    // 成员 userid
    CString m_name;   // 显示名（群名片优先→账号昵称）

    AtCandidate()
        : m_uid(0)
    {
    }
};

/**
 *  用户在浮层里选中某个成员时的回调。
 *    userdata：SetPickCallback 时透传的用户数据（一般是群聊窗 this）。
 *    uid：     选中成员的 userid。
 *    name：    选中成员的显示名（不含 '@'）。
 */
typedef void (*AtPickCallback)(void* userdata, int64_t uid, const CString& name);

/**
 *  CGroupAtDropdown —— 群聊「@ 成员选择」浮层。
 *
 *  用法（宿主群聊窗）：
 *      m_atDropdown.EnsureCreated();
 *      m_atDropdown.SetPickCallback(&OnAtPickThunk, this);
 *      m_atDropdown.Show(anchorScreenPt, members, prefix);   // 键入 '@' 时
 *      m_atDropdown.UpdateFilter(prefix);                    // 之后每键入一个字
 *      m_atDropdown.Hide();                                  // 收起
 */
class CGroupAtDropdown : public balloonwjui::DuiHost
{
public:
    CGroupAtDropdown();
    ~CGroupAtDropdown();

    /**
     *  惰性创建浮窗（已创建则直接返回 true）。
     *  @return 创建成功返回 true。
     */
    bool EnsureCreated();

    /**
     *  设置选中回调。
     *    cb：      选中成员时触发；传 NULL 表示不关心。
     *    userdata：回调透传的用户数据（借用，不持有）。
     */
    void SetPickCallback(AtPickCallback cb, void* userdata);

    /**
     *  按锚点弹出浮层并填入候选成员。
     *    anchorScreenPt：期望的左上角落点（屏幕坐标，一般取输入框插入符位置）。
     *                    实际落点会经 ClampPopupMenuToWorkArea 夹进工作区。
     *    members：       候选成员全集（拷贝一份存下，供后续前缀过滤复用）。
     *    prefix：        初始前缀（键入 '@' 时为空串）。
     */
    void Show(const POINT& anchorScreenPt,
              const std::vector<AtCandidate>& members,
              const CString& prefix);

    /**
     *  更新前缀过滤并重排浮层高度。过滤后无任何候选时自动收起 —— 用户显然
     *  不是在 @ 人，继续挂着一个空浮层只会挡住输入。
     *    prefix：当前已键入的前缀（不含 '@'）。
     */
    void UpdateFilter(const CString& prefix);

    /**
     *  收起浮层（不销毁，下次 Show 复用）。
     */
    void Hide();

    /**
     *  浮层当前是否可见。宿主据此决定方向键 / 回车要不要交给浮层处理。
     */
    bool IsShowing() const;

    /**
     *  把当前高亮项作为选中项提交（宿主在浮层可见时按回车 / Tab 调用）。
     *  提交后浮层自动收起。没有任何候选时什么都不做。
     */
    void CommitCurrent();

    /**
     *  上下移动高亮项（宿主在浮层可见时按方向键调用）。移动后会把新高亮项滚进
     *  可视范围 —— 候选多于一屏时，否则高亮会跑到看不见的地方。
     *    delta：-1 上移，+1 下移；越界时在首尾之间循环。
     */
    void MoveSelection(int delta);

    /**
     *  按滚轮滚动候选列表（<u>不</u>改变高亮项，与方向键分工不同）。
     *
     *  为什么要宿主转发而不是本浮层自己收：WM_MOUSEWHEEL 由系统投给<u>当前有焦点
     *  的</u>窗口，而本浮层是 WS_EX_NOACTIVATE、从不取焦点 —— 焦点始终在输入框里，
     *  滚轮消息也就都落到输入框上（表现为"滚轮滚的是输入框，不是候选列表"）。
     *  Windows 10 的"悬停即滚动非活动窗口"设置<u>可能</u>把它改投给光标下的窗口，
     *  但那是用户可关的系统选项，不能依赖。故由输入框的键盘钩子拦下来转给本函数。
     *    zDelta：滚轮增量，正数向上滚、负数向下滚（同 WM_MOUSEWHEEL）。
     */
    void ScrollByWheel(short zDelta);

    /**
     *  取浮窗 HWND（未创建返回 NULL）。供宿主做"点击是否落在浮层上"的命中判断。
     */
    HWND GetHwnd() const { return m_hWnd; }

    BEGIN_MSG_MAP(CGroupAtDropdown)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnChildNotify)
        MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
        CHAIN_MSG_MAP(balloonwjui::DuiHost)
    END_MSG_MAP()

private:
    // 构建一次客户区（白底卡片 + 列表），跨多次显示复用。
    void BuildContentOnce();

    // 用 m_filtered 重填列表并按项数算出浮层高度、重新定位。
    //   anchorScreenPt：期望落点；传 NULL 表示沿用上次的落点。
    void RefillAndLayout(const POINT* anchorScreenPt);

    // 按 m_prefix 从 m_members 过滤出 m_filtered（大小写不敏感的前缀匹配）。
    void ApplyFilter();

    // WM_DUI_NOTIFY：列表选中变化 / 双击 → 提交选中项。
    LRESULT OnChildNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_MOUSEACTIVATE：一律返回 MA_NOACTIVATE，让点击本浮层<u>既不激活也不夺取
     *  键盘焦点</u>，焦点始终留在输入框里。
     *
     *  这是本浮层能用的关键。WS_EX_NOACTIVATE 只阻止窗口被"激活"，<u>并不阻止它
     *  在被点击时拿走焦点</u>——实测点浮层时 GetFocus() 会从输入框变成本窗口，宿主
     *  随即收到 WM_ACTIVATE(WA_INACTIVE) 与输入框的 WM_KILLFOCUS，那些"收起浮层"的
     *  逻辑就会在鼠标<u>按下</u>那一刻把浮层藏掉，抬起自然落空，表现为"列表点不动"。
     */
    LRESULT OnMouseActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    bool                     m_built;        // 客户区是否已构建
    balloonwjui::DuiListBox* m_list;         // 成员列表（客户区树持有，本类只借指针）
    std::vector<AtCandidate> m_members;      // 候选全集（Show 时拷入）
    std::vector<AtCandidate> m_filtered;     // 当前前缀过滤后的候选，与列表项一一对应
    CString                  m_prefix;       // 当前前缀（不含 '@'）
    POINT                    m_anchor;       // 上次的期望落点（屏幕坐标）
    AtPickCallback           m_pickCb;       // 选中回调
    void*                    m_pickUserdata; // 回调透传数据（借用）
};

} // namespace balloonchatwnd
