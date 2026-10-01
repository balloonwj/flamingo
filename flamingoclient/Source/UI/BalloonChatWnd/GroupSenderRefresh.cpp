/**
 *  GroupSenderRefresh.cpp
 *
 *  群聊气泡发送者显示信息的解析与刷新，见 GroupSenderRefresh.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与 FlamingoUnit.vcxproj
 *  共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-09-30
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（与 ChatMessageList.cpp 相同）----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限
#define _WTL_NO_CSTRING
#define _WTL_NO_WTYPES

#include <atlbase.h>
#include <atlstr.h>
#include <atltypes.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>
#include <atlcrack.h>
#include <atlmisc.h>
#include <atlgdi.h>

#include <vector>

#include "GroupSenderRefresh.h"
#include "ChatBubble.h"
#include "ChatMessageList.h"
#include "../../Common/Log/IULog.h"   // 刷新结果的日志

namespace balloonchatwnd {

GroupSenderRender ResolveGroupSender(int64_t userId, const GroupSenderRender* found,
                                     COLORREF fallbackColor)
{
    GroupSenderRender r;
    r.m_color = fallbackColor;
    if (found != NULL)
    {
        r = *found;
    }
    if (r.m_name.IsEmpty())
    {
        r.m_name.Format(_T("用户%lld"), (long long)userId);
    }
    if (r.m_glyph.IsEmpty())
    {
        r.m_glyph = r.m_name.Left(1);
    }
    return r;
}

int ApplyGroupSendersToList(ChatMessageList& list, GroupSenderLookup lookup, void* ctx)
{
    if (lookup == NULL)
    {
        return 0;
    }

    int updated = 0;
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list.Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        ChatBubble* b = dynamic_cast<ChatBubble*>(kids[i].get());
        if (b == NULL)
        {
            continue;
        }
        const ChatMessage& msg = b->GetMessage();
        //只刷别人发的、带发送者 userid 的真实消息；自己发的气泡不显示发送者名，
        //时间分隔行与撤回提示行没有发送者
        if (msg.kind == kMsgTime || msg.fromMe || msg.senderUserId == 0)
        {
            continue;
        }
        GroupSenderRender found;
        if (!lookup(ctx, msg.senderUserId, found))
        {
            continue;   //已退群等查不到的人：保持原样，不把已有的备用信息清掉
        }
        const GroupSenderRender r = ResolveGroupSender(msg.senderUserId, &found, found.m_color);
        if (b->SetSenderInfo(r.m_name, r.m_glyph, r.m_color, r.m_imagePath))
        {
            ++updated;
        }
    }

    if (updated > 0)
    {
        //名字从"用户<id>"换成真名不改变行高，但重排一次代价很低（折行结果有缓存），
        //省得哪天名字行的排版规则变了、这里静悄悄地留下错位
        list.ForceLayout(list.GetRect());
        list.Invalidate();
    }
    LOG_INFO("group sender refresh: %d bubble(s) updated.", updated);
    return updated;
}

} // namespace balloonchatwnd
