/**
 *  PresenceTreeMarks.cpp
 *
 *  主面板树上在线状态标记（头像右下角的在线状态角标与头像灰彩）的写入。整体说明见 PresenceTreeMarks.h。
 *
 *  本 .cpp 不 include Flamingo 的 stdafx.h —— 它要被 Flamingo.vcxproj 与
 *  FlamingoUnit.vcxproj 共用。在 Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-10-03
 */

// ---- balloonui 头文件要求的 ATL/WTL 前置依赖（与 MainPanelTreeView.cpp 相同） ----
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601
#define _WIN32_IE       0x0601
#define _RICHEDIT_VER   0x0300      // RichEdit 3.0，WTL 10 要求的下限（窗口类仍为 RichEdit20W）
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

#include "PresenceTreeMarks.h"

namespace balloonmain {

namespace {

// 消息列表映射里单聊会话的会话类型取值（conversation_single，见 net/Protocol.h）。
const int kSingleConversationType = 1;

// 往一个节点写入在线状态与灰彩。presence 为 kPresenceMarkKeep 时只写灰彩。
void ApplyMarksToNode(MainPanelTreeView* tv, int node, int presence, bool gray)
{
    if (presence != kPresenceMarkKeep)
    {
        tv->SetItemPresence(node, presence);
    }
    tv->SetItemIconGrayed(node, gray);
}

} // anonymous namespace

int ApplyUserPresenceMarks(MainPanelTreeView* tv,
                           const std::map<int, UINT>& nodeUserIds,
                           UINT userid, int presence, bool gray)
{
    if (tv == NULL)
    {
        return 0;
    }
    int hit = 0;
    for (std::map<int, UINT>::const_iterator it = nodeUserIds.begin();
         it != nodeUserIds.end(); ++it)
    {
        if (it->second == userid)
        {
            ApplyMarksToNode(tv, it->first, presence, gray);
            ++hit;
        }
    }
    return hit;
}

int ApplyConversationPresenceMarks(MainPanelTreeView* tv,
                                   const std::map<int, std::pair<int, int64_t> >& nodeConv,
                                   int64_t userid, int presence, bool gray)
{
    if (tv == NULL)
    {
        return 0;
    }
    int hit = 0;
    for (std::map<int, std::pair<int, int64_t> >::const_iterator it = nodeConv.begin();
         it != nodeConv.end(); ++it)
    {
        if (it->second.first == kSingleConversationType && it->second.second == userid)
        {
            ApplyMarksToNode(tv, it->first, presence, gray);
            ++hit;
        }
    }
    return hit;
}

} // namespace balloonmain
