/**
 *  ChatRecordMenu.cpp —— 见同名头文件说明。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "ChatRecordMenu.h"

#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"
#include "../../Common/I18n/I18n.h"   // TR：菜单文字的多语言查找
#include "../../Common/Log/IULog.h"   // LOG_INFO

namespace balloonchatwnd {

namespace {

// 菜单与按钮下边之间的间隙（像素）
const int kMenuGapPx = 4;

} // anonymous namespace

void BuildRecordMenu(balloonwjui::DuiMenu& menu, bool recording, bool withManager)
{
    menu.Clear();
    if (recording)
        menu.AppendItem(kRecordMenuIdStop, TR(L"停止录制"));
    else
        menu.AppendItem(kRecordMenuIdStart, TR(L"开始录制…"));
    if (withManager)
        menu.AppendItem(kRecordMenuIdManager, TR(L"录制管理"));
}

RecordMenuChoice RecordMenuChoiceFromId(UINT id)
{
    switch (id)
    {
    //「开始录制…」
    case kRecordMenuIdStart:
        return kRecordMenuStart;
    //「停止录制」
    case kRecordMenuIdStop:
        return kRecordMenuStop;
    //「录制管理」
    case kRecordMenuIdManager:
        return kRecordMenuManager;
    //0（没有选）或不认识的 id
    default:
        return kRecordMenuNone;
    }
}

RecordMenuChoice TrackRecordMenu(HWND owner, const RECT& anchorScreen, bool recording, bool withManager)
{
    balloonwjui::DuiMenu menu;
    BuildRecordMenu(menu, recording, withManager);
    // 落点超出工作区时由 DuiMenu 自己翻向上方 / 左方（UI 约定第 3 条），这里只给期望位置
    UINT chosen = menu.TrackPopup(anchorScreen.left, anchorScreen.bottom + kMenuGapPx, owner);
    RecordMenuChoice choice = RecordMenuChoiceFromId(chosen);
    LOG_INFO("chat record menu: chosen id=%u, choice=%d.", chosen, (int)choice);
    return choice;
}

} // namespace balloonchatwnd
