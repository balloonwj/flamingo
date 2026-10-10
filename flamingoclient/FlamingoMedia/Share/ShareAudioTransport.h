/**
 *  屏幕共享房间里声音帧的发送通道，ShareAudioTransport.h
 *
 *  ShareAudioSession 编出的麦克风帧要经房间中转发出，而中转连接由发起人一侧的 ShareHostStreamer、观看者一侧的
 *  ShareViewerReceiver 持有；两者实现本接口，语音会话只认这个接口，不依赖具体是哪一方。
 *  单独成一个头文件，推流端、收流端的头文件包含它时不必带进整个音频引擎。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <string>

namespace screenshare {

//把一个房间中转包体发出去（由持有中转连接的一方实现）
class ShareAudioTransport
{
public:
    virtual ~ShareAudioTransport()
    {
    }

    /**
     *  发一个 1860 包体（已带 11 字节帧头）。可在采集线程上调用，实现须线程安全。
     *  @return 已交给中转连接返回 true；未连接时返回 false
     */
    virtual bool SendRoomBody(const std::string& body) = 0;
};

} // namespace screenshare
