/**
 *  远程桌面观看端的等比显示与鼠标坐标换算（纯逻辑），说明见 RdViewFit.h。
 *  balloonwj@qq.com   2026-10-12
 */
#include "RdViewFit.h"

#include <stdint.h>
#include "../Share/ShareAspectFit.h"   // FitRectKeepAspect：与屏幕共享、云端合成共用的等比放置

namespace remotedesktop {

RECT ComputeRdFitRect(const RECT& area, int frameW, int frameH)
{
    //与屏幕共享、云端合成共用一份等比放置（2026-10-14）
    return screenshare::FitRectKeepAspect(area, frameW, frameH);
}

void MapRdPointClamped(POINT pt, const RECT& fit, int frameW, int frameH, int& remoteX, int& remoteY)
{
    const int fitW = fit.right - fit.left;
    const int fitH = fit.bottom - fit.top;
    remoteX = (fitW > 0) ? static_cast<int>(static_cast<int64_t>(pt.x - fit.left) * frameW / fitW) : 0;
    remoteY = (fitH > 0) ? static_cast<int>(static_cast<int64_t>(pt.y - fit.top) * frameH / fitH) : 0;
    //夹到被控端屏幕范围内
    if (remoteX < 0)
        remoteX = 0;
    if (remoteX > frameW - 1)
        remoteX = frameW - 1;
    if (remoteY < 0)
        remoteY = 0;
    if (remoteY > frameH - 1)
        remoteY = frameH - 1;
}

bool MapRdRemoteToView(POINT remote, int frameW, int frameH, const RECT& fit, POINT& out)
{
    out.x = 0;
    out.y = 0;
    if (frameW <= 0 || frameH <= 0)
        return false;
    //只采集主屏：坐标超出主屏范围说明指针在别的显示器上
    if (remote.x < 0 || remote.y < 0 || remote.x >= frameW || remote.y >= frameH)
        return false;
    const int fitW = fit.right - fit.left;
    const int fitH = fit.bottom - fit.top;
    out.x = fit.left + static_cast<int>(static_cast<int64_t>(remote.x) * fitW / frameW);
    out.y = fit.top + static_cast<int>(static_cast<int64_t>(remote.y) * fitH / frameH);
    return true;
}

RdMouseMapper::RdMouseMapper()
{
}

bool RdMouseMapper::Map(UINT msg, POINT pt, const RECT& fit, int frameW, int frameH, int& remoteX, int& remoteY)
{
    if (frameW <= 0 || frameH <= 0)
        return false;
    //黑边上按下不发也不记、按住拖到黑边上照发、记过的按钮在哪里松开都发、滚轮只在画面内发（与屏幕共享共用，
    //规则见 Share/ViewerMouseButtons.h）；要发的消息坐标夹到画面边缘再换成被控端像素
    const bool inside = (::PtInRect(&fit, pt) != FALSE);
    if (!m_buttons.Decide(msg, inside))
        return false;
    MapRdPointClamped(pt, fit, frameW, frameH, remoteX, remoteY);
    return true;
}

void RdMouseMapper::Reset()
{
    m_buttons.Reset();
}

int RdMouseMapper::ButtonsDown() const
{
    return m_buttons.ButtonsDown();
}

} // namespace remotedesktop
