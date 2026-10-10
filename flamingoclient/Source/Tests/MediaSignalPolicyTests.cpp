/**
 *  音视频类信令命令号白名单单元测试（Suite MSP，标签 [mediadll]），MediaSignalPolicyTests.cpp
 *  覆盖（对应 docs/屏幕共享与录制方案.md 第 1c / 1d 步，2026-10-02 与用户确认的用例 S1）：
 *    - 一对一通话的 1701~1709、远程桌面的 1710~1716 全部放行；
 *    - 段外的 1700、1717、无关的 9999 与 0 一律拒绝。
 *  第 2 期 2g 补充 S2：屏幕共享的 1720~1731 全部放行，两段之间的空号 1717~1719 与段后的 1732 拒绝。
 *  第 14 期：屏幕共享段延到 1732（移出观看者），段后的号改为 1733。
 *  第 15 期：屏幕共享段延到 1734（修改标题与描述的 1733 应答、1734 推送），段后的号改为 1735。
 *  第 18 期：屏幕共享段延到 1736（控制键鼠的 1735 应答、1736 推送），段后的号改为 1737。
 *  第 22 期：屏幕共享段延到 1739（多人共享的 1737 应答、1738 推送、1739 移交主持人的应答），与群通话段（1740 起）相接。
 *  被测函数是纯函数（net/MediaSignalPolicy.cpp）。框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "catch_amalgamated.hpp"

#include "../net/MediaSignalPolicy.h"
#include "../net/Protocol.h"

TEST_CASE("MSP-S1 音视频信令白名单", "[mediadll]")
{
    //一对一通话：邀请、邀请推送、应答、取消、挂断、状态推送、中转参数、切换媒体、媒体已变更推送
    //远程桌面（1d 迁入）：邀请、邀请推送、应答、取消、结束、状态推送、中转参数
    const uint16_t allowed[] = { cmd_call_invite, cmd_call_invite_notify, cmd_call_response, cmd_call_cancel,
                                 cmd_call_hangup, cmd_call_state_notify, cmd_call_signaling, cmd_call_switch_media,
                                 cmd_call_media_changed_notify,
                                 cmd_rd_invite, cmd_rd_invite_notify, cmd_rd_response, cmd_rd_cancel, cmd_rd_close,
                                 cmd_rd_state_notify, cmd_rd_relay_info };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
    {
        INFO("cmd " << allowed[i]);
        CHECK(net::IsMediaSignalCmd(allowed[i]));
    }
    //按号段核对：1701~1709 是通话的 9 条，1710~1716 是远程桌面的 7 条
    CHECK(cmd_call_invite == 1701);
    CHECK(cmd_call_media_changed_notify == 1709);
    CHECK(cmd_rd_invite == 1710);
    CHECK(cmd_rd_relay_info == 1716);

    //段外与尚未开发的命令一律拒绝
    CHECK_FALSE(net::IsMediaSignalCmd(1700));
    CHECK_FALSE(net::IsMediaSignalCmd(1717));
    CHECK_FALSE(net::IsMediaSignalCmd(9999));
    CHECK_FALSE(net::IsMediaSignalCmd(0));
}

TEST_CASE("MSP-S2 屏幕共享信令放行", "[mediadll]")
{
    //屏幕共享：发起、开始推送、加入、离开、结束、房间状态、中转参数、查询、成员媒体、结束推送、设置权限、录制状态、
    //移出观看者（第 14 期）、修改标题与描述及其推送（第 15 期）
    const uint16_t allowed[] = { cmd_screenshare_start, cmd_screenshare_started_notify, cmd_screenshare_join,
                                 cmd_screenshare_leave, cmd_screenshare_stop, cmd_screenshare_state_notify,
                                 cmd_screenshare_relay_info, cmd_screenshare_query, cmd_screenshare_member_media,
                                 cmd_screenshare_ended_notify, cmd_screenshare_set_permission,
                                 cmd_screenshare_record_state, cmd_screenshare_remove_member,
                                 cmd_screenshare_set_info, cmd_screenshare_info_notify,
                                 cmd_screenshare_control, cmd_screenshare_control_notify, cmd_screenshare_sharing,
                                 cmd_screenshare_sharing_notify, cmd_screenshare_transfer_host,
                                 cmd_screenshare_link, cmd_screenshare_link_resolve, cmd_cloudrecord_control,
                                 cmd_cloudrecord_list, cmd_cloudrecord_op, cmd_cloudrecord_notify };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
    {
        INFO("cmd " << allowed[i]);
        CHECK(net::IsMediaSignalCmd(allowed[i]));
    }
    CHECK(cmd_screenshare_start == 1720);
    CHECK(cmd_screenshare_record_state == 1731);
    CHECK(cmd_screenshare_remove_member == 1732);
    CHECK(cmd_screenshare_set_info == 1733);
    CHECK(cmd_screenshare_info_notify == 1734);
    CHECK(cmd_screenshare_control == 1735);
    CHECK(cmd_screenshare_control_notify == 1736);
    CHECK(cmd_screenshare_sharing == 1737);
    CHECK(cmd_screenshare_sharing_notify == 1738);
    CHECK(cmd_screenshare_transfer_host == 1739);
    //分享链接（第 25 期）接在群通话段之后，云端录制（第 26 期）接着分享链接；其后的 1758 起暂未放行
    CHECK(cmd_screenshare_link == 1752);
    CHECK(cmd_screenshare_link_resolve == 1753);
    CHECK(cmd_cloudrecord_control == 1754);
    CHECK(cmd_cloudrecord_notify == 1757);
    CHECK_FALSE(net::IsMediaSignalCmd(1758));

    //远程桌面与屏幕共享两段之间的空号一律拒绝（屏幕共享段第 22 期起延到 1739，与群通话段相接，段后没有空号）
    CHECK_FALSE(net::IsMediaSignalCmd(1717));
    CHECK_FALSE(net::IsMediaSignalCmd(1719));
    //房间中转的 1859 / 1860 走 imgserver 连接，不经主连接，同样拒绝
    CHECK_FALSE(net::IsMediaSignalCmd(cmd_screenshare_relay_hello));
    CHECK_FALSE(net::IsMediaSignalCmd(cmd_screenshare_relay_data));
}

TEST_CASE("MSP-S3 群通话信令放行", "[mediadll][groupcall]")
{
    //群通话（docs/协议设计-v2.md 第 21 节）：发起、邀请推送、加入、拒绝、离开、房间状态、中转参数、追加邀请、成员媒体、
    //查询、群内状态推送、管理
    const uint16_t allowed[] = { cmd_groupcall_start, cmd_groupcall_invite_notify, cmd_groupcall_join,
                                 cmd_groupcall_decline, cmd_groupcall_leave, cmd_groupcall_state_notify,
                                 cmd_groupcall_relay_info, cmd_groupcall_invite_more, cmd_groupcall_member_media,
                                 cmd_groupcall_query, cmd_groupcall_status_notify, cmd_groupcall_manage };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
    {
        INFO("cmd " << allowed[i]);
        CHECK(net::IsMediaSignalCmd(allowed[i]));
    }
    CHECK(cmd_groupcall_start == 1740);
    CHECK(cmd_groupcall_manage == 1751);

    //群通话段之后是分享链接与云端录制（1752~1757，见 MSP-S2），再往后的号一律拒绝
    CHECK_FALSE(net::IsMediaSignalCmd(1758));
}
