/**
 *  ChatFileCardControl.h
 *
 *  可以单独放进任意布局里的聊天文件卡片控件：外观、操作链接与聊天窗文件气泡里那张卡片
 *  完全一致（绘制与点击判断都转调 ChatFileCard.h 的共用函数）。聊天记录查看窗的文件行用它，
 *  让转发来的文件也能直接下载、打开。
 *
 *  点了卡片底部的操作链接时，与聊天气泡一样向宿主窗口投递 WM_CHAT_FILE_ACTION（lParam 为
 *  堆上的 ChatFileActionData，宿主处理后 delete），本控件不碰传输与文件。状态变化（下载中、
 *  进度、完成等）由宿主通过 SetCardMessage 交回来。
 *
 *  存在理由：balloonui 里没有能画这张卡片的控件，聊天气泡的绘制又与整行消息（头像、名字、
 *  气泡方向）绑在一起。本类只包一层 DuiControl，内容全部来自与气泡共用的 ChatFileCard，
 *  不另起一套绘制（已与用户确认此做法，2026-10-01）。
 *
 *  典型用法：
 *      std::unique_ptr<ChatFileCardControl> card(new ChatFileCardControl());
 *      card->SetCardMessage(msg);
 *      layout->AddChild(std::move(card), Hint().Fixed(FileCardHeight(msg), kFileCardWidth));
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-10-01
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "ChatMockData.h"    // ChatMessage
#include "ChatFileCard.h"    // FileCardLinks

namespace balloonchatwnd {

/**
 *  聊天文件卡片控件。宽高由布局给定，应为 kFileCardWidth × FileCardHeight(消息)。
 */
class ChatFileCardControl : public balloonwjui::DuiControl
{
public:
    ChatFileCardControl();

    /**
     *  设置卡片显示的文件消息并重画。只改进度这类不影响卡片高度的更新可以直接调用；
     *  状态变化导致高度变化时（例如从「未下载」变成「下载中」多出进度条），调用方要
     *  重新排版所在列表。
     *  @param m 文件消息（kind 应为 kMsgFile），按值保存
     */
    void SetCardMessage(const ChatMessage& m);

    /**
     *  取卡片当前显示的文件消息。
     *  @return 消息的引用，生命周期同本控件
     */
    const ChatMessage& CardMessage() const;

    //DuiControl 覆写：画卡片底色与内容，同时记下各操作链接的点击矩形。
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    //DuiControl 覆写：按光标位置更新悬停的链接（颜色加深、加下划线），变化时重画。返回 true。
    bool OnMouseMove(POINT pt, UINT mkFlags) override;
    //DuiControl 覆写：光标离开时撤掉悬停效果。返回 true。
    bool OnMouseLeave() override;
    //DuiControl 覆写：光标在操作链接上时显示手形并返回 true；否则返回 false。
    bool OnSetCursor(POINT pt) override;
    //DuiControl 覆写：按在操作链接上时向宿主投递 WM_CHAT_FILE_ACTION 并返回 true；否则返回 false。
    bool OnLButtonDown(POINT pt, UINT mkFlags) override;
    //DuiControl 覆写：忽略双击并返回 true。双击的第一下已经作为单击处理过链接；不忽略的话
    //双击会继续传给所在的行，查看窗把行双击当作「打开 / 下载文件」，同一个动作会执行两次。
    bool OnLButtonDblClk(POINT pt, UINT mkFlags) override;

private:
    //组好动作负载投给宿主窗口；宿主窗口无效时什么都不做。
    //  action：动作，取值见 ChatFileActionKind
    void PostAction(int action);

private:
    ChatMessage   m_msg;        //卡片显示的文件消息（按值保存，由 SetCardMessage 更新）
    FileCardLinks m_links;      //最近一次绘制排出的操作链接点击矩形；绘制前为空，点击不响应
    int           m_hoverIdx;   //光标悬停的链接下标；-1 表示没有悬停
};

} // namespace balloonchatwnd
