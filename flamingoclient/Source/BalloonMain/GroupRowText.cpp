/**
 *  GroupRowText.cpp
 *
 *  群列表行文案拼装的实现。仅做字符串运算，不依赖 balloonui / GDI / 窗口 ——
 *  故与 FlamingoUnit 测试工程共用、不 include Flamingo 的 stdafx.h；在
 *  Flamingo.vcxproj 里需标记 PrecompiledHeader=NotUsing。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WINVER          0x0601      // 目标系统下限 = Windows 7
#define _WIN32_WINNT    0x0601

#include "GroupRowText.h"

namespace balloonmain {

namespace {

// 群名为空、但群号可用时的占位名格式。与会话列表 CBalloonMainPanel::GroupDisplayName
// 的占位名同口径，免得同一个无名群在群列表和会话列表里叫两个不同的名字。
const TCHAR* const kFmtFallbackName = _T("群聊(%I64d)");

// 群名与群号都取不到时的兜底名 —— 至少让这一行能读出"这是个群"。
const TCHAR* const kFallbackNameNoId = _T("群聊");

// 第一行右侧成员数的格式。数字与"人"之间留一个空格，与群信息对话框的
// "群号 N · M 人"（BalloonGroupInfoLayout.cpp）写法保持一致。
const TCHAR* const kFmtMemberCount = _T("%d 人");

// 第二行群号的格式。带"群号"前缀，否则脱离上下文看不出这串数字是什么；
// 前缀同样取自群信息对话框，两处口径一致。
const TCHAR* const kFmtGroupNo = _T("群号 %I64d");

} // anonymous namespace

GroupRowTexts BuildGroupRowTexts(LPCTSTR name, int memberCount, __int64 groupId)
{
    GroupRowTexts texts;

    // ---- 第一行左侧：群名 ----
    // 群名原样透传（含超长群名）：这一行显示得下多少、从哪里开始打省略号，
    // 由绘制层的 DT_END_ELLIPSIS 决定，本函数不替它做主。
    if (name != NULL && name[0] != _T('\0'))
    {
        texts.m_name = name;
    }
    else if (groupId > 0)
    {
        texts.m_name.Format(kFmtFallbackName, groupId);
    }
    else
    {
        texts.m_name = kFallbackNameNoId;
    }

    // ---- 第一行右侧：成员数 ----
    if (memberCount > 0)
    {
        texts.m_memberText.Format(kFmtMemberCount, memberCount);
    }

    // ---- 第二行：群号 ----
    if (groupId > 0)
    {
        texts.m_groupNoText.Format(kFmtGroupNo, groupId);
    }

    return texts;
}

} // namespace balloonmain
