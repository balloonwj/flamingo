/**
 *  屏幕共享与独立录制的互斥规则，说明见 ShareRecordMutex.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "ShareRecordMutex.h"

#include "../../Source/Common/I18n/I18n.h"   // TR：提示文字的多语言查找

namespace screenshare {

int CheckScreenStartConflict(int kind, bool recordBusy, bool shareBusy)
{
    switch (kind)
    {
    //发起共享：独立录制占用中时拦下
    case screen_start_share:
        return recordBusy ? screen_start_blocked_by_record : screen_start_ok;
    //发起独立录制：自己正在共享时拦下
    case screen_start_record:
        return shareBusy ? screen_start_blocked_by_share : screen_start_ok;
    //未知的种类：不在本规则管辖范围内，放行
    default:
        return screen_start_ok;
    }
}

int CheckShareStartConflict(bool hosting, bool watching, bool recordBusy)
{
    if (hosting)
        return screen_start_blocked_by_hosting;
    if (watching)
        return screen_start_blocked_by_watching;
    return CheckScreenStartConflict(screen_start_share, recordBusy, false);
}

std::wstring ScreenStartConflictText(int conflict)
{
    switch (conflict)
    {
    //正在录制时发起共享
    case screen_start_blocked_by_record:
        return TR(L"正在录制屏幕，请先结束录制再发起屏幕共享");
    //正在共享时开始录制
    case screen_start_blocked_by_share:
        return TR(L"正在共享屏幕，请先结束共享再开始录制");
    //正在共享时再发起共享（文字与 2026-10-07 之前的消息框相同，已有译文）
    case screen_start_blocked_by_hosting:
        return TR(L"你正在共享屏幕，请先结束当前的共享。");
    //正在观看别人的共享时发起共享（同上）
    case screen_start_blocked_by_watching:
        return TR(L"你正在观看屏幕共享，请先离开后再发起。");
    //放行或未知取值：没有提示
    default:
        return std::wstring();
    }
}

} // namespace screenshare
