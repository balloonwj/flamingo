/**
 *  远程桌面被控端何时发关键帧（纯逻辑），说明见 RdKeyframePolicy.h。
 *  balloonwj@qq.com   2026-10-12
 */
#include "RdKeyframePolicy.h"

namespace remotedesktop {

bool RdShouldSendKeyframe(bool requested, bool periodic, int framesSinceKeyframe, int framesPerKeyframe,
                          bool encoderNeedsKeyframe)
{
    if (requested)
        return true;
    //编码器没有可用的基准帧（第一帧、或分辨率变了）：控制端要靠新的 BITMAPINFO 重建画面，不论走不走 P2P
    if (encoderNeedsKeyframe)
        return true;
    return periodic && framesSinceKeyframe >= framesPerKeyframe;
}

} // namespace remotedesktop
