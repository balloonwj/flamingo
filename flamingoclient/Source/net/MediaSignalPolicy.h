/**
 *  音视频类信令的命令号白名单：哪些 v2 命令归 FlamingoMedia.dll 处理。
 *
 *  主连接（CIUSocket）据此做两件事：
 *    - 发送：DLL 经宿主接口 IMediaHost::SendSignal 要发的信令，命令号不在白名单内一律拒绝，
 *      防止 DLL 借主连接发出任意命令；
 *    - 接收：白名单内的推送 / 应答不在 exe 里解析，原样转交 DLL（IMediaModule::OnSignal）。
 *  白名单随迁移推进：第 1c 步加入一对一通话（1701~1709），第 1d 步加入远程桌面（1710~1716），
 *  第 2 期 2g 加入屏幕共享（1720~1731）。
 *
 *  纯函数，便于单元测试（Tests/MediaSignalPolicyTests.cpp）。可在任意线程调用。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <string>

namespace net {

//主连接收到一条白名单内的信令后，投给主面板的堆对象（FMG_MSG_MEDIA_SIGNAL 的 lParam）。
//由收件方（主面板，或代理窗在主面板未就绪时）delete。
struct MediaSignalPacket
{
    uint16_t    m_cmd;    //命令号，取值见 Protocol.h 的 cmd_type
    std::string m_body;   //包体（UTF-8 JSON 明文，已解密解压），原样交给 IMediaModule::OnSignal

    MediaSignalPacket()
        : m_cmd(0)
    {
    }
};

/**
 *  判断一个 v2 命令号是否归 FlamingoMedia.dll 处理。
 *  @param cmd 命令号，取值见 Protocol.h 的 cmd_type
 *  @return 属于白名单返回 true
 */
bool IsMediaSignalCmd(uint16_t cmd);

} // namespace net
