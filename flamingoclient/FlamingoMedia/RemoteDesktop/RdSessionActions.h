/**
 *  远程桌面会话窗口把用户操作交给远程桌面控制器的接口。
 *
 *  会话对话框（CBalloonRemoteDesktopDlg：发起方取消、被控端接受 / 拒绝）与控制端观看窗
 *  （CRdViewerWindow：关窗或工具条「断开」）只负责显示与收集用户操作，不直接发信令：
 *  这几项操作都经本接口交给远程桌面控制器（CRdController），由它编好信令包体再经宿主接口
 *  IMediaHost::SendSignal 从 exe 的主连接发出。迁入 DLL 之前这几处直接调用 CIUSocket 单例，DLL 里没有它。
 *
 *  所有方法只在 UI 线程调用。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <string>

namespace remotedesktop {

class IRdSessionActions
{
public:
    /**
     *  发起方在被控端应答之前取消邀请（cmd_rd_cancel）。
     *  @param sessionId 会话 id，为空时忽略（邀请应答还没回来）
     */
    virtual void CancelInvite(const std::string& sessionId) = 0;

    /**
     *  被控端接受或拒绝远程桌面邀请（cmd_rd_response）。
     *  @param sessionId 会话 id，为空时忽略
     *  @param accept    true 接受，false 拒绝
     */
    virtual void RespondInvite(const std::string& sessionId, bool accept) = 0;

    /**
     *  结束远程桌面会话（cmd_rd_close），控制端关闭观看窗时调用。
     *  @param sessionId 会话 id，为空时忽略
     */
    virtual void CloseSession(const std::string& sessionId) = 0;

protected:
    ~IRdSessionActions()
    {
    }
};

} // namespace remotedesktop
