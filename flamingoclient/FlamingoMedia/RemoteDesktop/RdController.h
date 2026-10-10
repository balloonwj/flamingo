/**
 *  远程桌面控制器：FlamingoMedia.dll 里把一次远程桌面会话从邀请到结束的全过程集中在一个类里。
 *
 *  由 exe 的远程桌面协调器（UI/BalloonMain/MainPanelRdCoordinator，第 1d 步删除）改写而来，行为保持一致，
 *  只是依赖的来源换了：
 *    - 信令：收到的是主连接原样转交的 JSON（IMediaModule::OnSignal → OnSignal），用
 *      RemoteDesktopProtocolClient 解析；发出时用它编好包体，经宿主接口 IMediaHost::SendSignal 从主连接发出；
 *    - 提示框属主（主窗口）与中转握手用的服务端公钥改从宿主接口取；
 *    - 受控提示条的「结束控制」由本类自己的消息窗口接收（仅接收消息的 HWND_MESSAGE 窗口，消息号见 RdMessages.h）。
 *
 *  P2P（docs/P2P通信方案.md 第 6 期）：Init 时经宿主取 P2P 服务，登记用途 rd（环境变量 FLAMINGO_RD_P2P 为 0 时不登记）。
 *  1716 带 p2psessionid 时为本次会话认领链路槽，连同中转参数交给推流器 / 观看窗，由 RdTransport 决定走 P2P 还是中转。
 *  连接在推流线程 / 观看窗的连接线程上进行，被控端连接失败时推流器投 kMsgRdStreamerConnectFailed 回本类收尾。
 *
 *  一次会话涉及两端四个对象：发起端弹「呼叫中」对话框、被邀请端弹授权对话框（两者是同一个
 *  CBalloonRemoteDesktopDlg 的两种形态）；双方谈妥后，被控端起推流器并在屏幕顶部挂受控提示条，控制端开
 *  观看窗收画面。四个对象都归本类持有，登出、关闭模块时的收尾写在一处（ResetForLogout）。会话窗口里的
 *  用户操作（取消 / 接受 / 拒绝 / 结束）经 IRdSessionActions 交回本类发信令。
 *
 *  线程模型：所有公开方法只在 UI 线程调用（与 exe 的消息循环同一线程）。
 *
 *  典型用法（见 Host/MediaModule.cpp）：
 *      m_rd.reset(new remotedesktop::CRdController());
 *      m_rd->Init(host);
 *      ...
 *      m_rd->OnSignal(cmd, body);        //主连接转交来的 1710~1716
 *      m_rd->StartRemoteDesktop(peer, name);
 *      ...
 *      m_rd->Shutdown();
 *
 *  包含本头文件之前调用方须已引入 ATL / WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <string>
#include "BalloonRemoteDesktopDlg.h"
#include "RdControlledStreamer.h"
#include "RdViewerWindow.h"
#include "RdControlledBar.h"
#include "RdSessionActions.h"
#include "RdMessages.h"
#include "RemoteDesktopProtocolClient.h"
#include "../Api/FlamingoMediaApi.h"

namespace remotedesktop {

class CRdController : public CWindowImpl<CRdController, CWindow, CWinTraits<0, 0> >,
                      public IRdSessionActions
{
public:
    DECLARE_WND_CLASS(_T("FlamingoMediaRdController"))

    CRdController();

    //析构时收尾（等同 Shutdown）：会话对话框、观看窗、受控提示条与消息窗口都是 ATL 窗口，析构时仍持有 HWND 会触发断言。
    ~CRdController();

    CRdController(const CRdController&) = delete;
    CRdController& operator=(const CRdController&) = delete;

    /**
     *  初始化：登记宿主接口，建本类的消息窗口，把本类登记为会话对话框与观看窗的操作接收方。
     *  @param host 宿主接口（不拥有；须在 Shutdown 返回之前一直有效），不可为 NULL
     *  @return 成功返回 true；host 为 NULL 或消息窗口建不出来返回 false
     */
    bool Init(flamingomedia::IMediaHost* host);

    //关闭：先按登出收尾（停推流、关观看窗、关提示条与对话框），再销毁消息窗口。未初始化或重复调用都安全。
    void Shutdown();

    /**
     *  把远程桌面收尾到「没有会话」的状态：停被控端推流、关控制端观看窗、隐藏受控提示条、关掉呼叫 / 授权对话框、
     *  清空会话 id。幂等，没有会话时调用也安全。用户登出、被踢下线与模块关闭时都要调它 —— 正在被控时登出，
     *  屏幕仍在往中转服务器推送，而用户看到的已经是登录窗，会以为早就断开了。
     */
    void ResetForLogout();

    /**
     *  锁屏开始或结束（docs/锁屏方案.md 第 4.9 节）。锁屏开始时本机若正被远程控制，立即结束控制；
     *  锁屏期间收到的请求在 HandleInviteNotify 里直接拒绝。界面线程调用。
     *  @param locked true 表示锁屏开始
     */
    void OnScreenLockChanged(bool locked);

    /**
     *  判断一个命令号是否归远程桌面处理（cmd_rd_invite 1710 ~ cmd_rd_relay_info 1716）。
     *  @param cmd 命令号
     *  @return 属于远程桌面返回 true
     */
    static bool IsRdSignal(uint16_t cmd);

    /**
     *  处理主连接转交来的一条远程桌面信令。
     *  @param cmd  命令号，须满足 IsRdSignal
     *  @param body 包体（UTF-8 JSON）
     *  @return 命令号归远程桌面处理返回 true（包体解析失败也返回 true，只记错误日志）；未初始化或命令号不归远程桌面返回 false
     */
    bool OnSignal(uint16_t cmd, const std::string& body);

    /**
     *  向某个联系人发起远程桌面（本端为控制端）：发 cmd_rd_invite，并弹出「呼叫中」对话框。
     *  @param peerUserId 对方用户号（>0）
     *  @param peerName   对方显示名（对话框提示文字用）
     *  @return 已发出邀请并打开对话框返回 true；未初始化、参数非法或信令发送失败返回 false
     */
    bool StartRemoteDesktop(int64_t peerUserId, const CString& peerName);

    // ---- IRdSessionActions：会话窗口里的用户操作，编好包体后经宿主发出 ----

    //发起方在被控端应答前取消邀请（cmd_rd_cancel）。
    virtual void CancelInvite(const std::string& sessionId) override;

    //被控端接受 / 拒绝邀请（cmd_rd_response）。
    virtual void RespondInvite(const std::string& sessionId, bool accept) override;

    //控制端关闭观看窗时结束会话（cmd_rd_close）。
    virtual void CloseSession(const std::string& sessionId) override;

    BEGIN_MSG_MAP(CRdController)
        MESSAGE_HANDLER(kMsgRdControlledEndByUser, OnControlledEndByUserMsg)   //被控端点受控提示条的「结束控制」
        MESSAGE_HANDLER(kMsgRdStreamerConnectFailed, OnStreamerConnectFailedMsg)   //被控端推流器建立传输失败
    END_MSG_MAP()

    //远程桌面用途的 P2P 业务方是否已登记（P2P 可用），测试与日志用
    bool IsP2PRegistered() const { return m_p2pBroker.IsRegistered(); }

private:
    //宿主主窗口（提示框属主、对话框居中参照）；取不到时为 NULL
    HWND MainWindow() const;

    /**
     *  远程桌面没有建成（对方拒绝、不在线、超时等）的提示（2026-10-07）：写进与对方的单聊窗（灰色提示行，聊天窗没开时由 exe
     *  打开并前置），不再弹消息框；取不到对方时仍弹消息框，免得提示丢掉。
     *  @param peerUserId 对方 userid；0 表示不知道
     *  @param tip        提示文字（已经过 TR 翻译）
     */
    void ShowResultTip(int64_t peerUserId, const wchar_t* tip);

    //经宿主发出一条远程桌面信令；包体为空或发送失败时记错误日志并返回 false
    bool SendRdSignal(uint16_t cmd, const std::string& body, const char* what);

    //四类有内容的信令各自的处理（解析成功后调用）
    void HandleInviteAck(const net::RDInviteResponse& resp);
    void HandleInviteNotify(const net::RDInviteNotify& notify);
    void HandleRelayInfo(const net::RDRelayInfo& info);
    void HandleStateNotify(const net::RDStateNotify& state);

    //被控端用户点受控提示条的「结束控制」：发 cmd_rd_close，停推流，关提示条
    LRESULT OnControlledEndByUserMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //被控端推流器建立传输失败：停推流、关提示条、发 cmd_rd_close 结束会话并提示用户（wParam 为推流器的启动序号）
    LRESULT OnStreamerConnectFailedMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //向宿主的 P2P 服务登记用途 rd；P2P 不可用或按诊断开关关闭时不登记
    void RegisterP2P();

    /**
     *  按 1716 填本次会话的传输参数：中转参数与服务端公钥；带 p2psessionid 且 P2P 可用时认领链路槽。
     *  @param info 解析后的 1716
     *  @param out  [出参] 传输参数
     */
    void BuildTransportParams(const net::RDRelayInfo& info, RdTransportParams& out);

    flamingomedia::IMediaHost*  m_host;                   //宿主接口（不拥有）；Init 登记，Shutdown 置空
    CBalloonRemoteDesktopDlg    m_rdDlg;                  //会话对话框，呼叫中 / 授权两种形态共用一个对象
    RdControlledStreamer        m_rdStreamer;             //被控端推流器：本端为被控端时连中转服务器抓屏并推送
    CRdViewerWindow             m_rdViewer;               //控制端观看窗：本端为控制端时连中转服务器收屏幕帧并显示
    CRdControlledBar            m_rdControlledBar;        //被控端受控提示条：屏幕顶部置顶显示「正在被控制」并提供结束按钮
    std::wstring                m_rdControlledPeerName;   //控制方昵称，由邀请推送带来，供受控提示条显示；生命周期到本次会话结束
    std::string                 m_rdControlledSessionId;  //本次会话 id，被控方主动结束时要用它发 cmd_rd_close；无会话时为空
    balloonp2p::P2PLinkBroker   m_p2pBroker;              //用途 rd 的 P2P 业务方：Init 时登记、Shutdown 时注销；
                                                          //为每次带 p2psessionid 的会话认领链路槽
};

} // namespace remotedesktop
