/**
 *  通话控制器与屏幕共享控制器之间的两个接口（docs/屏幕共享与录制方案.md 第 8 期：通话中共享屏幕）。
 *
 *  两个控制器都由 MediaModule 持有、互不认识：通话控制器经 ICallShareLink 把「通话接通 / 结束」与
 *  「通话窗里点了共享屏幕」告诉共享控制器；共享控制器经 ICallShareObserver 把「是否正在与通话对端共享」
 *  告诉通话控制器，由它刷新通话窗按钮的文字。所有方法只在 UI 线程调用。
 *
 *  典型用法（MediaModule 初始化时）：
 *      m_call->SetShareLink(m_share.get());
 *      m_share->SetCallObserver(m_call.get());
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>

namespace call {

//通话控制器调用共享控制器的接口（CShareController 实现）
class ICallShareLink
{
public:
    /**
     *  一对一通话已接通。共享控制器记下通话对端：此后与他在单聊里的共享不开房间麦克风与摄像头，
     *  他发起的共享不弹提示、直接加入观看。
     *  @param peerId   通话对端 userid（>0）
     *  @param peerName 通话对端显示名（发起共享时作为会话名）；可为空
     */
    virtual void OnCallConnected(int64_t peerId, const std::wstring& peerName) = 0;

    //通话已结束（挂断、对方挂断、断线、登出都算）。正在进行的共享不受影响，只是不再走通话的声音。可重复调用。
    virtual void OnCallEnded() = 0;

    //通话窗的「共享屏幕 / 停止共享」按钮被点击：没在共享时发起与通话对端的共享，正在共享时结束它
    virtual void ToggleShareInCall() = 0;

protected:
    ~ICallShareLink()
    {
    }
};

//共享控制器回告通话控制器的接口（CCallController 实现）
class ICallShareObserver
{
public:
    /**
     *  本端「是否正在与通话对端共享」变化了（发起、结束都会通知）。
     *  @param hostingWithPeer true 表示正在共享（含选择内容、等服务端应答），按钮显示「停止共享」
     */
    virtual void OnInCallShareChanged(bool hostingWithPeer) = 0;

protected:
    ~ICallShareObserver()
    {
    }
};

} // namespace call
