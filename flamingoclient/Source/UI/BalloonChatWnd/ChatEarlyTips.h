/**
 *  ChatEarlyTips.h
 *
 *  记录单聊窗在首屏历史渲染之前插入的居中提示，首屏渲染完后把它们补回列表末尾。
 *
 *  背景（2026-10-07 用户反馈，代码路径据实证）：从主面板联系人右键菜单发起通话、对方不在线时，exe 先打开与对方的单聊窗，
 *  紧接着插入「对方当前无法接听（不在线或正在通话中）。」。聊天窗打开时请求的首屏历史是异步到达的
 *  （BalloonChatWindow.cpp 建窗处的 RequestSyncMessages），OnHistoryMessages 按首屏处理时先 ClearAllMessages
 *  再按服务端快照重建；居中提示只存在于列表里、不入库，随之被清除，用户只看到它闪一下。通话结束的时长提示、
 *  远程桌面的失败提示等经 AppendCallTip 插入的提示都受同样影响。
 *
 *  做法：首屏渲染之前插入的提示照常立即显示，同时记录下来；首屏渲染清空列表之后，按插入顺序补回末尾。
 *  拉取历史失败时主面板不通知聊天窗（只记日志），提示因为已经立即显示过，仍然留在列表里。
 *  只记录首屏之前的提示；首屏渲染之后插入的提示不受影响，不记录。只在界面线程上使用，不加锁。
 *
 *  典型用法（单聊窗）：
 *      插入提示之后：      m_earlyTips.OnTipShown(text);
 *      首屏渲染完：        std::vector<std::wstring> tips = m_earlyTips.TakeTipsOnFirstPageRendered();
 *                          逐条重新插入
 *      重新请求最新一页：  m_earlyTips.OnReloadRequested();
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <deque>
#include <string>
#include <vector>

#include "ChatMockData.h"   // ChatMessage

namespace balloonchatwnd {

/**
 *  首屏历史渲染之前插入的居中提示的记录。单聊窗持有一个。
 *  为防止首屏一直不到（例如拉取失败）时记录无限增长，最多保留 kMaxTips 条，超出时丢弃最早的。
 */
class EarlySystemTipTracker
{
public:
    // 最多保留的提示条数，超出时丢弃最早记录的
    static const size_t kMaxTips = 20;

    EarlySystemTipTracker();

    /**
     *  聊天窗插入了一条居中提示之后调用。首屏已渲染时这条会正常留在列表里，不记录。
     *  @param text 提示文字；为空时不记录
     */
    void OnTipShown(const std::wstring& text);

    /**
     *  首屏历史渲染完成（列表已清空并按服务端快照重建）后调用：转为「首屏已渲染」，交出需要补回的提示并清空记录。
     *  调用方随后逐条重新插入；此时已是「首屏已渲染」，重新插入时的 OnTipShown 不会再记录。
     *  @return 首屏之前插入的提示，按插入顺序排列；没有时为空
     */
    std::vector<std::wstring> TakeTipsOnFirstPageRendered();

    // 聊天窗重新请求最新一页（之后到达的应答会再按首屏处理、再清空一次列表）时调用：回到「首屏未渲染」
    void OnReloadRequested();

    // 首屏历史是否已渲染。初始为 false
    bool IsFirstPageRendered() const;

    // 记录里还有多少条，供日志与单测使用
    size_t PendingCount() const;

private:
    bool m_firstPageRendered;            // 首屏历史是否已渲染；初始为 false，OnReloadRequested 时恢复为 false
    std::deque<std::wstring> m_tips;     // 首屏渲染之前插入的提示，按插入顺序；最多 kMaxTips 条
};

/**
 *  构造一条居中提示行：灰色、居中、不带头像，与时间分隔行同一种 kind（kMsgTime）。
 *  单聊窗插入提示与补回提示都用它，保证两次插入的样子一致。
 *  @param text 提示文字
 *  @return 可直接交给 ChatMessageList::AppendMessage 的消息
 */
ChatMessage MakeSystemTipMessage(const CString& text);

/**
 *  从首屏前记录的提示里去掉首屏历史里已经出现过的同文字提示：shown 里每有一条，就从 tips 里去掉一条文字相同的
 *  （从前往后找第一条）。用于窗口抖动这类既在收到时插入了即时提示、又作为消息出现在首屏历史里的提示，
 *  不去掉的话首屏渲染后补回，同一句提示会出现两次。
 *  @param tips  TakeTipsOnFirstPageRendered 交回的提示，按插入顺序
 *  @param shown 首屏历史里已经画出的提示文字
 *  @return 去掉之后剩下的提示，保持原顺序
 */
std::vector<std::wstring> DropTipsShownInHistory(const std::vector<std::wstring>& tips,
                                                 const std::vector<std::wstring>& shown);

} // namespace balloonchatwnd
