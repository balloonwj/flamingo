/**
 *  「新公告」桌面提示窗（右下角弹出），AnnouncePopupWindow.h
 *
 *  收到平台公告推送（cmd_announcement_notify，changetype=新发布）时，在桌面<u>右下角</u>
 *  弹一个置顶提示窗，让用户不必盯着主面板也能第一时间看到。内容：级别徽章 + 标题 +
 *  正文摘要 + 发布者·时间 + 底部「知道了」「查看全部」。
 *
 *  为什么不复用群公告的 CBalloonAnnouncePopupDlg：那个是<u>居中</u>弹在主面板上、且接口
 *  绑死了群（groupId / 群名 / 「查看全部」跳该群聊天窗的公告 Tab）。本窗要求右下角落点、
 *  字段与去处都不同，改造它会牵动已在使用的群公告行为，故另起一个；两者的骨架写法保持
 *  一致，日后若还要第三种公告提示，再考虑把共同部分抽出来。
 *
 *  同一时刻只存在一个实例（主面板持有）。上一条还没关就又来一条新公告时，直接把内容
 *  换成新的那条并前置 —— 弹一摞窗口出来比漏掉一条更糟（与群公告弹窗同策略）。
 *
 *  落点：贴桌面工作区（rcWork，已排除任务栏）的右下角内缩一段边距。按开发约定文档「UI
 *  约定」第 3 条，任何弹层都必须整体落在工作区内，不允许出现跑到屏幕外看不见的情况；
 *  本窗按工作区反推坐标，天然满足。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#pragma once

#include <stdint.h>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../CustomMsgDef.h"                    // WM_CHAT_OPEN_LINK（消息映射里要用）

namespace balloonwjui {
class DuiLabel;
class DuiBadge;
}

//公告正文用的内联文本控件（能把网址渲染成可点的链接）。只按指针持有，前向声明即可。
namespace balloonhistory { class HistoryContentView; }

/**
 *  CAnnouncePopupWindow —— 新公告桌面提示窗（非模态、置顶浮窗）。
 */
class CAnnouncePopupWindow : public balloonwjui::DuiFrameWindow
{
public:
    CAnnouncePopupWindow();
    ~CAnnouncePopupWindow();

    /**
     *  弹出一条公告（已弹着则换成这一条并前置）。
     *
     *    ownerPanel：    主面板窗口句柄；点「查看全部」时经它打开消息盒子。
     *    announcementId：公告ID，「查看全部」据它在消息盒子里定位到这一条。
     *    title：         公告标题。
     *    body：          公告正文（过长时窗口内只显示得下一部分，看全文去消息盒子）。
     *    severity：      级别，取值见 net/Protocol.h 的 announcement_severity；
     *                    决定右上角徽章的文字与颜色。
     *    senderName：    发布者名称；空串时底部只显示时间。
     *    timeText：      发布时间的展示文本（如「08-05 14:30」）。
     */
    void ShowAnnouncement(HWND ownerPanel, int64_t announcementId,
                          LPCTSTR title, LPCTSTR body, int severity,
                          LPCTSTR senderName, LPCTSTR timeText);

    BEGIN_MSG_MAP(CAnnouncePopupWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CHAT_OPEN_LINK, OnChatOpenLinkMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    /**
     *  WM_CHAT_OPEN_LINK 处理：正文里的超链接被点击。lParam 为堆上的 std::wstring*
     *  （规范化后的 URL），本函数负责 delete。打开前先过 IsSafeLinkUrl 的协议白名单。
     */
    LRESULT OnChatOpenLinkMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //构建一次静态骨架（徽章行 + 标题 + 正文 + 发布者时间 + 按钮栏）。
    //内容每次 ShowAnnouncement 时改写，骨架不重建。
    void BuildUiOnce();

    /**
     *  把窗口挪到桌面工作区的右下角（内缩一段边距）。
     *  取的是<u>鼠标所在显示器</u>的工作区，多显示器下弹在用户正看的那块屏上。
     */
    void MoveToWorkAreaCorner();

private:
    HWND    m_panelHwnd;       //主面板窗口句柄（「查看全部」发消息给它）
    int64_t m_announcementId;  //当前显示的这条公告的ID
    bool    m_built;           //骨架是否已构建

    //控件指针（借用，控件树持有）：每次显示新公告时改写它们的文本。
    balloonwjui::DuiBadge* m_severityBadge;  //右上角级别徽章
    balloonwjui::DuiLabel* m_title;          //公告标题
    //公告正文。用消息记录窗那个内联文本控件而不是 DuiLabel —— 后者画不出"把网址
    //渲染成可点的链接"（没有字符级排版模型，也就无从命中）。喂给它的是纯文本入口
    //（公告是服务端原样存的文本，不走聊天消息那套 '/' 转义）。
    balloonhistory::HistoryContentView* m_body;
    balloonwjui::DuiLabel* m_meta;           //「发布者 · 时间」

    //标题栏图标位图（程序图标合成到标题栏底色上）。SetIcon 仅借用，本类持有、析构时释放。
    HBITMAP m_titleIconBmp;
};
