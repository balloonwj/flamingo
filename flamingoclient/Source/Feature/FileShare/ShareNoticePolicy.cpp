/**
 *  文件共享弹窗通知在主程序一侧的判定，见 ShareNoticePolicy.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "ShareNoticePolicy.h"

#include "../../../FlamingoFileShare/FileShareApi.h"   //FileShareNoticePolicy
#include "../Lock/LockSuppressPolicy.h"                 //LockEventAction
#include "../../Common/I18n/I18n.h"                      //TRF：界面文字的多语言查找

namespace balloonfileshare {

bool IsInDoNotDisturbPeriod(int localHour)
{
    //时段跨过午夜：晚于起点或早于终点都算
    return localHour >= kDoNotDisturbStartHour || localHour < kDoNotDisturbEndHour;
}

int DecideShareNoticePolicy(bool dndEnabled, int localHour, int lockAction)
{
    //勿扰时段：不弹，记录照常留在「最近操作」与传输列表里
    if (dndEnabled && IsInDoNotDisturbPeriod(localHour))
        return fileshare::fileshare_notice_drop;
    //锁屏期间：按锁屏策略推迟（解锁后补弹）或丢弃
    if (lockAction == balloonlock::kLockActDefer)
        return fileshare::fileshare_notice_defer;
    if (lockAction == balloonlock::kLockActDrop)
        return fileshare::fileshare_notice_drop;
    return fileshare::fileshare_notice_show;
}

std::wstring FileShareTransferExitWarning(uint32_t count, bool logout)
{
    if (count == 0)
        return std::wstring();
    const unsigned long long n = count;
    return logout ? TRF(L"还有 {0} 个文件共享传输没有完成，登出会中断。下载下次可以接着传，上传需要重新传。", n)
                  : TRF(L"还有 {0} 个文件共享传输没有完成，退出会中断。下载下次可以接着传，上传需要重新传。", n);
}

} // namespace balloonfileshare
