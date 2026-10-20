/**
 *  音视频类信令命令号白名单的实现，见 MediaSignalPolicy.h。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "MediaSignalPolicy.h"
#include "Protocol.h"

namespace net {

bool IsMediaSignalCmd(uint16_t cmd)
{
    //一对一通话 cmd_call_invite（1701）~ cmd_call_media_changed_notify（1709）与远程桌面
    //cmd_rd_invite（1710）~ cmd_rd_relay_info（1716），两段首尾相接，合成一个连续区间判断
    if (cmd >= cmd_call_invite && cmd <= cmd_rd_relay_info)
    {
        return true;
    }
    //远程桌面控制权限 cmd_rd_control（1718）、cmd_rd_control_notify（1719），2026-10-12 起。夹在中间的 1717 是一对一
    //通话卡片推送，由 exe 的 CIUSocket 自己处理，不交给 FlamingoMedia
    if (cmd == cmd_rd_control || cmd == cmd_rd_control_notify)
    {
        return true;
    }
    //屏幕共享 cmd_screenshare_start（1720）~ cmd_screenshare_transfer_host（1739，第 22 期多人共享）。
    //房间中转的 1859 / 1860 走 imgserver 连接，不经主连接，也不在这里
    if (cmd >= cmd_screenshare_start && cmd <= cmd_screenshare_transfer_host)
    {
        return true;
    }
    //群通话 cmd_groupcall_start（1740）~ cmd_groupcall_manage（1751），docs/协议设计-v2.md 第 21 节。与屏幕共享之间的
    //1737~1739 是空号，不放行；群通话的声音与画面复用屏幕共享的房间中转（1859 / 1860），不经主连接
    if (cmd >= cmd_groupcall_start && cmd <= cmd_groupcall_manage)
    {
        return true;
    }
    //屏幕共享的分享链接 cmd_screenshare_link（1752）、cmd_screenshare_link_resolve（1753），第 25 期，接在群通话段之后；
    //云端录制 cmd_cloudrecord_control（1754）~ cmd_cloudrecord_notify（1757），第 26 期，接着分享链接，一并交给 FlamingoMedia
    if (cmd >= cmd_screenshare_link && cmd <= cmd_cloudrecord_notify)
    {
        return true;
    }
    return false;
}

} // namespace net
