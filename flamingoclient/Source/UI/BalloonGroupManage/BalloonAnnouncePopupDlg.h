/**
 *  BalloonAnnouncePopupDlg.h
 *
 *  「有新群公告」提示窗 —— 群主 / 管理员发布一条公告后，全体成员各弹一次。
 *  对照 design/群管理7.png 页顶那句"发布后所有成员将收到弹窗通知"。
 *
 *  窗体内容：群名 + 优先级徽章 / 公告标题 / 公告正文（可滚动）/ 发布人 · 时间 /
 *  底部「知道了」与「查看全部」两个按钮。「查看全部」会请主面板打开该群的聊天窗
 *  并切到右侧边栏的「公告」Tab —— 弹窗只显示这一条，要翻别的得去那里。
 *
 *  弹不弹、弹哪一条由 AnnouncePopupPolicy 判定（纯逻辑、可单测），本类只管显示；
 *  "已经弹过哪条"的记账也在那个模块里。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

#include <stdint.h>
#include "../BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons：任务栏与 Alt+Tab 的程序图标

namespace balloonwjui {
class DuiLabel;
class DuiBadge;
class DuiButton;
}

/**
 *  CBalloonAnnouncePopupDlg —— 新群公告提示窗（非模态浮窗）。
 *
 *  同一时刻只存在一个实例（主面板持有）。若上一条还没关就又来一条新公告，
 *  直接把内容换成新的那条并前置 —— 弹一摞窗口出来比漏掉一条更糟。
 */
class CBalloonAnnouncePopupDlg : public balloonwjui::DuiFrameWindow
{
public:
    CBalloonAnnouncePopupDlg();
    ~CBalloonAnnouncePopupDlg();

    /**
     *  显示一条公告（已显示则换成这一条并前置）。
     *    ownerPanel：   主面板窗口句柄；窗口居中到它，「查看全部」也发消息给它；
     *    groupId：      公告所属群ID，「查看全部」据它打开对应群聊窗；
     *    groupName：    群名，显示在顶部；
     *    title：        公告标题；
     *    content：      公告正文；
     *    severity：     优先级，取值见 GroupManageModel.h 的 AnnounceSeverity；
     *    publisherName：发布人昵称；空串时底部只显示时间；
     *    timeText：     发布时间的展示文本（如「今天 14:32」）。
     */
    void ShowAnnouncement(HWND ownerPanel, int64_t groupId, LPCTSTR groupName,
                          LPCTSTR title, LPCTSTR content, int severity,
                          LPCTSTR publisherName, LPCTSTR timeText);

    /**
     *  设置窗口标题（标题栏与任务栏上的文字）。不调用时为「新群公告」——主面板收到新公告推送时弹的就是它；
     *  群聊窗点群公告卡片打开详情时设为「群公告」（2026-10-05，docs/群公告接入方案.md §13.9）。
     *  在 ShowAnnouncement 之前调用；窗口已显示时也立即生效。
     *    caption：标题文字；空串或 NULL 表示恢复为默认的「新群公告」。
     */
    void SetCaption(LPCTSTR caption);

    /**
     *  切换「入群必读」模式（2026-10-07 起，docs/群公告接入方案.md §14）。开启后隐藏「查看全部」，「知道了」换成
     *  「我已阅读」：点了之后向 ownerPanel（此时是群聊窗）投递 confirmMsg（wParam、lParam 为 0）并隐藏本窗口，
     *  由群聊窗发送确认请求。关闭后恢复为普通的公告详情。在 ShowAnnouncement 之前调用。
     *    on：        true 开启，false 关闭。
     *    confirmMsg：点「我已阅读」时投给宿主窗口的消息号；on 为 false 时忽略。
     */
    void SetMustReadMode(bool on, UINT confirmMsg);

    /**
     *  设置建窗时是否以 ShowAnnouncement 传入的宿主窗口为所有者（2026-10-08）。群聊窗弹「入群必读」时设为 true：
     *  有所有者的窗口总在所有者之上，不会被随后激活的群聊窗压在下面（实测出过）。不调用时为 false —— 主面板的
     *  「新群公告」弹窗不能有所有者，主面板常收在托盘里，有所有者的窗口会跟着它一起隐藏。
     *  窗口以 WS_OVERLAPPEDWINDOW 创建；有无所有者，控件通知都发回本窗口（开发约定文档 UI 约定第 11 条：只有
     *  WS_CHILD 宿主的通知发给父窗口）。
     *  只在第一次 ShowAnnouncement（建窗）之前调用才生效；窗口建好后再调用不改变已有的所有者。
     *    owned：true 以宿主窗口为所有者，false 不设所有者。
     */
    void SetOwnedByHost(bool owned);

    BEGIN_MSG_MAP(CBalloonAnnouncePopupDlg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 构建一次静态骨架（群名行 + 标题 + 正文 + 发布人时间 + 按钮栏）。
    // 内容每次 ShowAnnouncement 时重写，骨架不重建。
    void BuildUiOnce();

    // 把 m_caption（空串时为默认的「新群公告」）写到标题栏与窗口文字上；窗口还没创建时什么都不做。
    void ApplyCaption();

    // 按 m_mustReadMode 设置底部两个按钮的显隐与文字，并重排按钮栏；骨架还没建好时什么都不做。
    void ApplyButtonsForMode();

private:
    HWND    m_panelHwnd;   // 主面板窗口句柄（居中到它、「查看全部」发消息给它）
    int64_t m_groupId;     // 当前显示的这条公告所属群ID
    bool    m_built;       // 骨架是否已构建
    CString m_caption;     // 窗口标题；空串表示默认的「新群公告」（见 SetCaption）
    bool    m_mustReadMode; // 是否处于「入群必读」模式（见 SetMustReadMode）
    UINT    m_confirmMsg;   // 入群必读模式下点「我已阅读」时投给宿主窗口的消息号
    bool    m_ownedByHost;  // 建窗时是否以宿主窗口为所有者（见 SetOwnedByHost）；只在建窗时读取

    // 控件指针（借用，控件树持有）：每次显示新公告时改写它们的文本。
    balloonwjui::DuiLabel* m_groupLabel;   // 顶部群名
    balloonwjui::DuiBadge* m_severity;     // 优先级徽章
    balloonwjui::DuiLabel* m_title;        // 公告标题
    balloonwjui::DuiLabel* m_content;      // 公告正文
    balloonwjui::DuiLabel* m_meta;         // 「发布人 · 时间」
    balloonwjui::DuiButton* m_btnViewAll;  // 「查看全部」：入群必读模式下隐藏
    balloonwjui::DuiButton* m_btnGotIt;    // 「知道了」：入群必读模式下改为「我已阅读」

    // 标题栏图标位图（程序图标合成到标题栏底色上）。SetIcon 仅借用，本类持有、
    // 析构时 DeleteObject。
    HBITMAP m_titleIconBmp;
    //任务栏与 Alt+Tab 的程序图标（2026-10-07，UI 约定第 10 条：没有所有者、会出现在任务栏的窗口另设任务栏图标）；本对象持有，析构时释放
    balloonmain::ProgramWindowIcons m_taskIcons;
};
