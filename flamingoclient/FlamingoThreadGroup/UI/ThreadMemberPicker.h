/**
 *  发帖窗口「@ 成员」的选人浮层 CThreadMemberPicker（docs/帖子群方案.md 4.2、4.6）：
 *  上面一个搜索框（按名字过滤），下面是群成员的名字列表（DuiListBox），单击一个名字即选中。
 *  成员名单由发帖窗口经宿主拉取后交进来；还没拉到时列表处显示「正在加载成员…」。
 *
 *  由 balloonui 现成控件组合而成（DuiPopupHost + DuiEdit + DuiListBox，UI 约定第 2 条）。所有者传 NULL 弹出
 *  （原为让控件通知发回本浮层；2026-10-14 起浮层内容的通知总是先到本浮层，由本类的 OnDuiNotify 处理，本类没处理的
 *  才由 DuiPopupHost 转给所有者，所有者为 NULL 时不转，见 DuiPopupHost::OnPopupNotify 与开发约定文档 UI 约定第 11 条）。
 *  选中时经回调交给发帖窗口，由它投递消息、在通知处理完后再插入。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法：
 *      m_memberPicker.SetPickCallback(&CThreadComposeWindow::MemberPickedThunk, this);
 *      m_memberPicker.SetMembers(entries, true);
 *      m_memberPicker.OpenAt(anchorScreen);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiPopupHost.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

namespace balloonwjui {
class DuiEdit;
class DuiLabel;
class DuiListBox;
}

namespace threadgroup {

//选人浮层里的一个成员
struct MemberPickEntry
{
    int64_t      m_userId;   //用户 ID
    std::wstring m_name;     //显示名（群名片优先，见宿主 GetUserDisplayName）

    MemberPickEntry()
        : m_userId(0)
    {
    }
};

/**
 *  CThreadMemberPicker —— 「@ 成员」的选人浮层。只在界面线程上使用，由发帖窗口持有。
 */
class CThreadMemberPicker : public balloonwjui::DuiPopupHost
{
public:
    /**
     *  选中回调。
     *  @param user   SetPickCallback 时传入的指针
     *  @param userId 选中的用户
     */
    typedef void (*PickFn)(void* user, int64_t userId);

    CThreadMemberPicker();

    //登记选中回调
    void SetPickCallback(PickFn fn, void* user);

    /**
     *  交进成员名单。
     *  @param members 成员，按显示顺序（调用方已去掉本人）
     *  @param loaded  名单是否已拉到；为 false 时列表处显示「正在加载成员…」
     */
    void SetMembers(const std::vector<MemberPickEntry>& members, bool loaded);

    /**
     *  在锚点矩形的上方弹出（放不下时由 DuiPopupHost 翻到下方），清空搜索框并把焦点交给它。
     *  @param anchorScreen 锚点（「@ 成员」按钮）的屏幕矩形
     */
    void OpenAt(const RECT& anchorScreen);

    BEGIN_MSG_MAP(CThreadMemberPicker)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiPopupHost)
    END_MSG_MAP()

private:
    //WM_DUI_NOTIFY：搜索框文字变了就重新过滤；列表选中了一行就回调；搜索框里按回车选第一行
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //建出浮层里的控件（只建一次）
    void BuildOnce();

    //按搜索框里的文字重建列表
    void ApplyFilter();

    //选中列表第 row 行：回调发帖窗口
    void PickRow(int row);

private:
    PickFn                        m_pickFn;    //选中回调
    void*                         m_pickUser;  //回调的 user 参数（借用）
    bool                          m_built;     //控件是否已建好
    bool                          m_loaded;    //成员名单是否已拉到
    std::vector<MemberPickEntry>  m_members;   //全部成员
    balloonwjui::DuiEdit*         m_search;    //搜索框（归本浮层的控件树所有）
    balloonwjui::DuiListBox*      m_list;      //成员列表（同上）；每行的 LPARAM 是 m_members 里的序号
    balloonwjui::DuiLabel*        m_hint;      //「正在加载成员…」「没有找到」的提示（同上）
};

} // namespace threadgroup
