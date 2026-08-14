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

namespace balloonwjui {
class DuiLabel;
class DuiBadge;
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

private:
    HWND    m_panelHwnd;   // 主面板窗口句柄（居中到它、「查看全部」发消息给它）
    int64_t m_groupId;     // 当前显示的这条公告所属群ID
    bool    m_built;       // 骨架是否已构建

    // 控件指针（借用，控件树持有）：每次显示新公告时改写它们的文本。
    balloonwjui::DuiLabel* m_groupLabel;   // 顶部群名
    balloonwjui::DuiBadge* m_severity;     // 优先级徽章
    balloonwjui::DuiLabel* m_title;        // 公告标题
    balloonwjui::DuiLabel* m_content;      // 公告正文
    balloonwjui::DuiLabel* m_meta;         // 「发布人 · 时间」

    // 标题栏图标位图（程序图标合成到标题栏底色上）。SetIcon 仅借用，本类持有、
    // 析构时 DeleteObject。
    HBITMAP m_titleIconBmp;
};
