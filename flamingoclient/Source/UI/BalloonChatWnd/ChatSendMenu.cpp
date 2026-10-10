/**
 *  ChatSendMenu.cpp
 *
 *  发送按钮 ▾ 菜单的实现，说明见 ChatSendMenu.h 与 docs/快捷回复方案.md §3.2。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "ChatSendMenu.h"

#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../BalloonChat/BalloonChatMsgFormat.h"   // BuildSendBody：快捷回复按输入框的规则编码成发送正文

namespace balloonchatwnd {

namespace {

// 菜单底边与发送按钮顶边之间的间隙（像素），与改动前两个聊天窗的写法一致
const int kMenuGapPx = 6;

// 截断后补在末尾的省略号
const wchar_t kEllipsis = L'\x2026';

// UTF-16 高位代理的取值范围：截断位置落在代理对中间时要往前退一个字符
bool IsHighSurrogate(wchar_t ch)
{
    return ch >= 0xD800 && ch <= 0xDBFF;
}

} // namespace

CString QuickReplyMenuText(const CString& reply)
{
    //换行变成空格，菜单里每条只占一行
    CString oneLine = reply;
    oneLine.Replace(L"\r\n", L" ");
    oneLine.Replace(L'\r', L' ');
    oneLine.Replace(L'\n', L' ');

    if (oneLine.GetLength() > kQuickReplyMenuMaxChars)
    {
        int cut = kQuickReplyMenuMaxChars;
        //不从代理对中间切开
        if (IsHighSurrogate(oneLine[cut - 1]))
        {
            --cut;
        }
        oneLine = oneLine.Left(cut);
        oneLine += kEllipsis;
    }

    //DuiMenu 把 '&' 当成助记符，原文里的 '&' 要写成 "&&" 才能原样显示
    oneLine.Replace(L"&", L"&&");
    return oneLine;
}

std::vector<ChatSendMenuItem> BuildChatSendMenuModel(const std::vector<CString>& quickReplies, bool enterToSend)
{
    std::vector<ChatSendMenuItem> items;

    //快捷回复一组：只列非空的位置，编号仍按配置里的位置
    std::vector<ChatSendMenuItem> replies;
    for (size_t i = 0; i < quickReplies.size(); ++i)
    {
        if (quickReplies[i].IsEmpty())
        {
            continue;
        }
        ChatSendMenuItem one;
        one.m_kind = kSendMenuQuickReply;
        one.m_cmdId = kSendMenuCmdQuickReplyBase + (UINT)i;
        one.m_text = QuickReplyMenuText(quickReplies[i]);
        one.m_replyIndex = (int)i;
        replies.push_back(one);
    }
    if (!replies.empty())
    {
        ChatSendMenuItem header;
        header.m_kind = kSendMenuHeader;
        header.m_text = TR(L"快捷回复");
        items.push_back(header);
        items.insert(items.end(), replies.begin(), replies.end());

        ChatSendMenuItem sep;
        sep.m_kind = kSendMenuSeparator;
        items.push_back(sep);
    }

    //发送键一组：文字与改动前两个聊天窗的菜单相同
    ChatSendMenuItem enter;
    enter.m_kind = kSendMenuEnterKey;
    enter.m_cmdId = kSendMenuCmdEnter;
    enter.m_text = TR(L"按 Enter 发送消息");
    enter.m_checked = enterToSend;
    items.push_back(enter);

    ChatSendMenuItem ctrlEnter;
    ctrlEnter.m_kind = kSendMenuCtrlEnterKey;
    ctrlEnter.m_cmdId = kSendMenuCmdCtrlEnter;
    ctrlEnter.m_text = TR(L"按 Ctrl+Enter 发送消息");
    ctrlEnter.m_checked = !enterToSend;
    items.push_back(ctrlEnter);

    ChatSendMenuItem sep2;
    sep2.m_kind = kSendMenuSeparator;
    items.push_back(sep2);

    ChatSendMenuItem settings;
    settings.m_kind = kSendMenuSettings;
    settings.m_cmdId = kSendMenuCmdSettings;
    settings.m_text = TR(L"设置快捷回复…");
    items.push_back(settings);

    return items;
}

ChatSendMenuCommand ResolveChatSendMenuCommand(UINT cmdId, int replyCount)
{
    ChatSendMenuCommand cmd;
    if (cmdId == kSendMenuCmdEnter)
    {
        cmd.m_kind = kSendMenuEnterKey;
    }
    else if (cmdId == kSendMenuCmdCtrlEnter)
    {
        cmd.m_kind = kSendMenuCtrlEnterKey;
    }
    else if (cmdId == kSendMenuCmdSettings)
    {
        cmd.m_kind = kSendMenuSettings;
    }
    else if (cmdId >= kSendMenuCmdQuickReplyBase
             && cmdId < kSendMenuCmdQuickReplyBase + (UINT)(replyCount > 0 ? replyCount : 0))
    {
        cmd.m_kind = kSendMenuQuickReply;
        cmd.m_replyIndex = (int)(cmdId - kSendMenuCmdQuickReplyBase);
    }
    return cmd;
}

CString QuickReplyBody(const CString& reply)
{
    if (reply.IsEmpty())
    {
        return CString();
    }
    std::vector<balloonchat::ChatSegment> segs;
    segs.push_back(balloonchat::ChatSegment::Text(reply));
    return balloonchat::BuildSendBody(segs);
}

ChatSendMenuCommand PopupChatSendMenu(HWND owner, POINT anchorTopLeft,
                                      const std::vector<CString>& quickReplies, bool enterToSend)
{
    const std::vector<ChatSendMenuItem> items = BuildChatSendMenuModel(quickReplies, enterToSend);

    balloonwjui::DuiMenu menu;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const ChatSendMenuItem& it = items[i];
        switch (it.m_kind)
        {
        //标题行：灰色小字，不可点
        case kSendMenuHeader:
            menu.AppendHeader(it.m_text);
            break;

        //分隔线
        case kSendMenuSeparator:
            menu.AppendSeparator();
            break;

        //两个发送键：勾选项
        case kSendMenuEnterKey:
        case kSendMenuCtrlEnterKey:
            menu.AppendChecked(it.m_cmdId, it.m_text, it.m_checked);
            break;

        //快捷回复与「设置快捷回复…」：普通项
        default:
            menu.AppendItem(it.m_cmdId, it.m_text);
            break;
        }
    }

    //菜单弹在发送按钮上方、不遮挡按钮：锚点从按钮顶边上移「菜单高 + 间隙」
    const SIZE size = menu.MeasureSize();
    const UINT chosen = menu.TrackPopup(anchorTopLeft.x, anchorTopLeft.y - size.cy - kMenuGapPx, owner);
    return ResolveChatSendMenuCommand(chosen, (int)quickReplies.size());
}

} // namespace balloonchatwnd
