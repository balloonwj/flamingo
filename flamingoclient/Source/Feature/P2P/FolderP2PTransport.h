/**
 *  单聊文件夹传输的 P2P 通道实现（docs/P2P通信方案.md 10.1 节，第 5 期），FolderP2PTransport.h
 *
 *  服务端为一笔文件夹传输建了 P2P 会话时，1816 带 p2psessionid，随后两端各收到用途为 folder、业务会话号为本次传输
 *  会话号的 2302，由 FlamingoP2P.dll 建链。本文件把这条链路接到文件夹传输的通道抽象（FolderLaneTransport.h）上，
 *  链路槽、业务方与帧通道用的是各业务共用的 P2PLinkBroker.h（第 6 期从本文件抽出）：
 *    - FolderP2PLinkBroker  用途 folder 的业务方（单例），程序启动加载 P2P 模块后立即登记：2302 可能比 1816 先处理完，
 *                           链路可能先于传输就绪，先到的链路暂存在链路槽里等传输认领；
 *    - FolderP2PTransportFactory 一次传输的通道工厂：第一条通道连接时等链路结果（最多 kFolderP2PLinkWaitMs），就绪就
 *                           走 P2P，失败或超时退回 1816 给的 fileserver 中转；之后各条通道沿用同一决定，两端各自决定，
 *                           链路的结果对两端对称；
 *    - P2P 上的一条通道是链路上通道号等于 lane 的帧通道（P2PStreamChannel），单帧上限是中转的应用帧上限
 *      kFolderRelayMaxAppFrameBytes；每条通道一个转交线程把应用帧交给会话，排队的数据量受发送端
 *      FolderSendFlowWindow 的在途分片上限约束。
 *  同一笔传输重试时业务会话号不变：新的 1816 只替换已被认领过的旧槽，没被认领的槽（新链路先到了）留给这一轮。
 *
 *  主程序用单例登记用途 folder，登记与注销经 FolderP2PRegistration.h 的两个函数（那个头文件不带 winsock，使用预编译头
 *  的 AppShell 也能包含）。FlamingoFileShare.dll 也编译本文件（第 6 期），自建一个用途为 fileshare 的实例接文件共享的
 *  传输，功能会话号是 2222 的 transferid；DLL 里不调用那两个文件夹专用的登记函数。
 *
 *  典型用法（FolderTransferManager::OnRelayInfo）：
 *      std::shared_ptr<FolderP2PTransportFactory> f = FolderP2PLinkBroker::GetInstance().CreateFactory(
 *          sessionId, relayIp, relayPort, token, serverPubKey);
 *      params.m_transportFactory = f.get();     //record 持有 f，直到会话结束
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

//FolderLaneTransport.h 经 FolderRelayClient.h 带进 <winsock2.h>，必须排在带进 <windows.h> 的 P2PLinkBroker.h 之前；
//同理包含本文件的编译单元不能使用预编译头（stdafx.h 第一句就是 atlbase.h），见 FolderTransferManager.cpp 的说明
#include "../FolderTransfer/FolderLaneTransport.h"
#include "P2PLinkBroker.h"

namespace balloonp2p {

//等链路结果的最长时间（毫秒）：打洞超时（服务端下发，默认 3 秒）+ 地址探测预留 1.5 秒 + 回落中转最多 10 秒，再留余量
const int kFolderP2PLinkWaitMs = 20000;

//发送队列满时等可写通知的最长时间（毫秒）：与发送端等分片回执的 60 秒一致，超过即判通道异常
const int kFolderP2PSendWaitMs = 60000;

//单聊文件夹传输登记的用途：与服务端 ChatSession::pushFolderRelayInfo 建会话时的 purpose 一致
const char* const kFolderP2PPurpose = "folder";

//文件共享传输登记的用途：与服务端 ChatSession::onShareRequestTransfer 建会话时的 purpose 一致（第 6 期）
const char* const kFileShareP2PPurpose = "fileshare";

//文件夹传输沿用的链路槽名字：第 6 期起就是各业务共用的 P2PLinkSlot
typedef P2PLinkSlot FolderP2PLinkSlot;

/**
 *  一次传输的通道工厂（IFolderLaneTransportFactory 的实现）。由 FolderTransferManager 的会话记录持有，生命周期
 *  长于发送 / 接收会话。
 */
class FolderP2PTransportFactory : public foldertransfer::IFolderLaneTransportFactory
{
public:
    /**
     *  @param slot         链路槽
     *  @param relayIp      退回中转时的中转地址（1816 的 relayip）
     *  @param relayPort    退回中转时的中转端口
     *  @param token        退回中转时的令牌
     *  @param serverPubKey 服务器长期公钥（不拥有，进程内常驻）；NULL 表示中转全程明文
     */
    FolderP2PTransportFactory(const std::shared_ptr<FolderP2PLinkSlot>& slot, const std::string& relayIp, int relayPort,
                              const std::string& token, const unsigned char* serverPubKey);

    /**
     *  创建一条通道（IFolderLaneTransportFactory 的实现）：返回的对象在 Connect 时才决定走 P2P 还是中转。
     *  @param lane 通道序号
     *  @return 新的通道对象，所有权交给调用方
     */
    virtual foldertransfer::IFolderLaneTransport* CreateLane(int lane) override;

    /**
     *  决定本次传输走哪条路（第一次调用时等链路结果，之后直接返回同一决定）。在会话的线程上调用。
     *  @return true 走 P2P；false 退回中转
     */
    bool DecideP2P();

    //链路槽
    std::shared_ptr<FolderP2PLinkSlot> Slot() const { return m_slot; }

    /**
     *  本次传输的链路实际路径（2026-10-04 起，供「P2P」徽章与 cmd_folder_close 的 linkpath）。不等待，可在任意线程上调用。
     *  @return 已经决定走 P2P 时返回链路槽的 LinkPath()；还没决定、或已决定退回中转时返回 0（中转期间链路
     *          迟到建成也不算，数据实际走的是中转）
     */
    int LinkPath();

    /**
     *  是否允许建链失败时退回 1816 给的 fileserver 中转（2026-10-04 起）。默认允许（文件夹传输、文件共享）；单聊单文件
     *  直传设为不允许：P2P 链路（含 imgserver 一对一中转）建不起来时各条通道直接连接失败，由发送端改为上传服务器。
     *  在创建通道之前调用。
     *  @param allowed true 允许回落；false 不允许
     */
    void SetRelayFallback(bool allowed);

    //是否允许退回 fileserver 中转，见 SetRelayFallback
    bool RelayFallbackAllowed() const;

    /**
     *  这一轮是否已经决定走 P2P 链路（2026-10-04 起）。不等待，可在任意线程上调用。单文件直传失败时据此区分「从没走上
     *  P2P 链路」（报 622，改为离线文件）与「走上之后中途失败」（照报原因，可续传）。
     *  @return 已决定走 P2P 返回 true；还没决定或已决定退回中转返回 false
     */
    bool DecidedP2P() const;

    //退回中转时的参数
    const std::string& RelayIp() const { return m_relayIp; }
    int RelayPort() const { return m_relayPort; }
    const std::string& Token() const { return m_token; }
    const unsigned char* ServerPubKey() const { return m_serverPubKey; }

private:
    //决定的取值
    enum path_decision
    {
        decision_none  = 0,   //还没决定
        decision_p2p   = 1,   //走 P2P
        decision_relay = 2,   //退回中转
    };

    std::shared_ptr<FolderP2PLinkSlot> m_slot;          //链路槽
    std::string                        m_relayIp;       //中转地址
    int                                m_relayPort;     //中转端口
    std::string                        m_token;         //中转令牌
    const unsigned char*               m_serverPubKey;  //服务器长期公钥（不拥有）
    std::mutex                         m_mutex;         //保证只等一次链路结果（DecideP2P 持着它等待）
    std::atomic<int>                   m_decision;      //决定，取值见 path_decision；DecideP2P 在 m_mutex 内写，LinkPath 不拿锁读
    std::atomic<bool>                  m_relayFallback; //建链失败时是否退回 fileserver 中转，见 SetRelayFallback
};

/**
 *  文件夹传输与文件共享的业务方：在共用的 P2PLinkBroker 上加一个按会话号建通道工厂的方法。主程序用单例（用途
 *  folder），文件共享 DLL 自建一个用途为 fileshare 的实例。
 *  线程：同 P2PLinkBroker；CreateFactory 在 UI 线程上调用。
 */
class FolderP2PLinkBroker : public P2PLinkBroker
{
public:
    //主程序用的单例，用途 folder
    static FolderP2PLinkBroker& GetInstance();

    //用途 folder 的实例：单测里同一进程的两端各建一个（主程序只用单例）
    FolderP2PLinkBroker();

    /**
     *  指定用途的实例（第 6 期起，文件共享 DLL 用 kFileShareP2PPurpose）。
     *  @param purpose 用途名（ASCII，1~p2p::kMaxPurposeChars 个字符）
     */
    explicit FolderP2PLinkBroker(const std::string& purpose);

    /**
     *  为一次传输创建通道工厂：认领该会话号的链路槽（见 P2PLinkBroker::ClaimSlot）。
     *  @param featureSessionId 文件夹传输的会话号（文件共享为 2222 的 transferid）
     *  @param relayIp / relayPort / token / serverPubKey 退回中转时的参数，见 FolderP2PTransportFactory
     *  @return 通道工厂
     */
    std::shared_ptr<FolderP2PTransportFactory> CreateFactory(const std::string& featureSessionId, const std::string& relayIp,
                                                             int relayPort, const std::string& token,
                                                             const unsigned char* serverPubKey);

private:
    FolderP2PLinkBroker(const FolderP2PLinkBroker&);
    FolderP2PLinkBroker& operator=(const FolderP2PLinkBroker&);
};

/**
 *  主程序里单聊单文件直传的业务方（2026-10-04 起，用途 foldertransfer::kDirectFileP2PPurpose，即 file），与文件夹传输的
 *  单例分开：服务端为直传建的 P2P 会话用途是 file，两种传输的链路互不认领。登记与注销经 FolderP2PRegistration.h。
 *  @return 进程内唯一的实例
 */
FolderP2PLinkBroker& DirectFileLinkBroker();

} // namespace balloonp2p
