/**
 *  GroupLinkDialog.h
 *
 *  「生成加群链接」窗口（docs/邀请入群方案.md §12）：从主面板群列表或会话列表的右键菜单「生成加群链接」打开。
 *  选有效期（1 天 / 7 天 / 永久有效，默认 7 天）后点「生成链接」发 cmd_create_group_link，应答由主面板经
 *  OnLinkCreated 交回本窗口显示链接与到期时间；「复制链接」把链接写进剪贴板。
 *
 *  下半部分「我生成的链接」（2026-10-07 补，普通成员进不了群管理窗，在这里管理自己生成的链接）：打开窗口、生成
 *  或作废之后发 cmd_get_group_links（1341），只列本人生成、仍然有效的链接，每行可「复制」「作废」。作废前弹确认框，
 *  发 cmd_revoke_group_link（1342）；1342 的应答体里没有链接ID，主面板先问 TakeRevokeResult 是不是本窗口在等，
 *  不是才交给群管理窗。
 *
 *  同一时刻只存在一个实例（主面板持有）；已打开时换一个群再打开，内容换成新的群。
 *
 *  典型用法：
 *      m_groupLinkDlg.Show(m_hWnd, groupId, groupName, selfUserId);
 *      //主面板收到 FMG_MSG_V2_GROUP_LINK_CREATED：
 *      m_groupLinkDlg.OnLinkCreated(*pResult);
 *      //主面板收到 FMG_MSG_V2_GROUP_LINKS（群号与 GroupId() 相同时）：
 *      m_groupLinkDlg.OnLinksArrived(*pResult);
 *      //主面板收到 1342 的应答：
 *      if (m_groupLinkDlg.TakeRevokeResult(code)) return;
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <string>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "TitleBarIcon.h"                          // ProgramWindowIcons：标题栏左侧的程序图标
#include "GroupLinkFlow.h"                         // MyGroupLinkList、GroupLinkRevokeTracker
#include "../../net/GroupLinkProtocolClient.h"     // net::CreateGroupLinkResult、net::GroupLinksResult

// 窗口投给自己、留到当前控件通知处理完再做的事（wParam 为 GroupLinkDeferredAction）。
// 「作废」会弹确认框，确认框的模态循环里可能先处理到 1341 应答、重建「我生成的链接」，被点的按钮就在列表里，
// 所以不能在按钮自己的点击通知里执行（开发约定文档 UI 约定 13）
#define WM_GROUP_LINK_DEFERRED (WM_APP + 0xFA)

// WM_GROUP_LINK_DEFERRED 的 wParam
enum GroupLinkDeferredAction
{
    // 「我生成的链接」某一行点了「作废」：要作废的链接ID 已记在 m_pendingRevokeId
    kGroupLinkDeferredRevoke = 1
};

/**
 *  CGroupLinkDialog —— 生成并复制加群链接、查看并作废本人生成的链接的窗口（非模态）。
 */
class CGroupLinkDialog : public balloonwjui::DuiFrameWindow
{
public:
    CGroupLinkDialog();
    ~CGroupLinkDialog();

    /**
     *  打开窗口（已打开则换成这个群并前置），并重新拉取「我生成的链接」。换群时清掉上一个群生成的链接与列表，
     *  并撤销上一个群还没有收到结果的作废登记。
     *    owner：     主面板窗口，本窗口居中到它；
     *    groupId：   群ID；
     *    groupName： 群名，显示在顶部；
     *    selfUserId：本人 userid，「我生成的链接」只列生成者是本人的链接。
     */
    void Show(HWND owner, int64_t groupId, const CString& groupName, int64_t selfUserId);

    /**
     *  收到生成链接的应答（主面板转来）：成功且是本窗口这个群的链接时显示链接与到期时间、启用「复制链接」，
     *  并重拉「我生成的链接」；失败时在状态行显示原因。窗口没打开时什么都不做。
     *    result：1340 应答解析结果。
     */
    void OnLinkCreated(const net::CreateGroupLinkResult& result);

    /**
     *  收到本群加群链接列表的应答（主面板按群号转来，群号与 GroupId() 相同）：更新「我生成的链接」。
     *  应答失败时，已加载过的列表保留原内容。
     *    result：1341 应答解析结果。
     */
    void OnLinksArrived(const net::GroupLinksResult& result);

    /**
     *  收到一条作废链接的应答（1342）时由主面板先调用：本窗口正在等作废结果时在状态行显示结果、重拉列表，
     *  并返回 true；没在等时什么都不做，返回 false，主面板再把应答交给群管理窗。
     *    code：1342 应答的结果码。
     */
    bool TakeRevokeResult(int code);

    /**
     *  本窗口当前显示的群ID；窗口没建过或已销毁时为 0（只是隐藏时仍返回该群）。主面板据此判断应答是不是给本窗口的。
     */
    int64_t GroupId() const;

    BEGIN_MSG_MAP(CGroupLinkDialog)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_GROUP_LINK_DEFERRED, OnDeferred)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDeferred(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 「生成链接」：按下拉选中的有效期发 1340，状态行显示「正在生成…」，应答回来之前按钮不可用
    void OnCreate();

    // 「复制链接」：把当前链接写进剪贴板，状态行提示已复制
    void OnCopy();

    // 「我生成的链接」第 index 行的「复制」：把那一行的链接写进剪贴板（不重建列表，可以在点击通知里直接做）
    void OnCopyRow(size_t index);

    // 「我生成的链接」第 index 行的「作废」：记下链接ID 后投 WM_GROUP_LINK_DEFERRED，等点击通知处理完再弹确认框
    void OnRevokeRowClicked(size_t index);

    // 弹确认框，确认后登记并发 1342；由 OnDeferred 调用
    void RevokePendingLink();

    // 发 1341 拉取本群的加群链接（应答经主面板交给 OnLinksArrived）
    void RequestMyLinks();

    // 设置链接文字、状态行与两个按钮的可用状态
    void SetUrlText(LPCTSTR text);
    void SetStatusText(LPCTSTR text);
    void SetButtonEnabled(UINT ctrlId, bool enabled);

private:
    int64_t      m_groupId;           // 当前显示的群ID；窗口没建过时为 0
    int64_t      m_selfUserId;        // 本人 userid，每次 Show 时更新；「我生成的链接」按它筛选
    std::wstring m_url;               // 已生成的链接；还没生成时为空
    bool         m_creating;          // 是否已发出生成请求、正在等应答
    int64_t      m_pendingRevokeId;   // 点了「作废」、还没弹确认框的链接ID；0 表示没有
    balloonmain::MyGroupLinkList        m_myLinks;         // 「我生成的链接」的数据，换群时清空
    balloonmain::GroupLinkRevokeTracker m_revokeTracker;   // 本窗口发出、还没收到应答的作废请求
    balloonmain::ProgramWindowIcons m_programIcons;   // 标题栏左侧的程序图标（本类持有）
};
