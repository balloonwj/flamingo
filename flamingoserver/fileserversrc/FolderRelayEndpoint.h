/**
 *  文件夹传输中转端点，FolderRelayEndpoint.h
 *
 *  一条 fileserver 连接在「单聊文件夹传输中转」这件事上的全部状态与动作都收在本类里：
 *  中转握手（cmd_folder_relay_hello）的令牌校验与配对表登记、转发数据帧
 *  （cmd_folder_relay_data）时按 (sessionid, lane) 定位配对端、以及连接断开时把本端从
 *  配对表摘除并算出需要连带关闭的其余通道。FileSession 因而只在自己的 cmd 分发里多几行
 *  转调，不会因为多出一套中转业务而膨胀成两套业务。出处见 docs/单聊文件夹传输方案.md
 *  第 6.2 节与 docs/协议设计-v2.md 第 12.3 节。
 *
 *  与 imgserver 的 RelaySession 的差别在于职责边界：那边一个类既是会话又是中转端点，
 *  因为 imgserver 的中转连接只干中转这一件事；fileserver 的连接则可能是上传、下载、
 *  中转三者之一（在同一个监听端口上按帧 cmd 分流，不新开端口），故把中转那一份职责单独
 *  拆成本类，由 FileSession 持有一个实例。令牌校验、配对、转发时「用本端密钥解密、用配对
 *  端密钥重新加密」、断开时连带关闭对端这几点，做法与 RelaySession 一致。
 *
 *  本类不碰帧的编解码与加解密：握手应答帧与转发帧都由 FileSession 发出，因为包体要用各条
 *  连接各自的会话密钥加密，只有会话对象本身做得到。本类也不关闭连接，只把「该关哪些通道」
 *  算出来交给调用方（FileServer::onDisconnected）去关，分工与 FolderRelayHub.h、
 *  FileSessionRemoval.h 一致。
 *
 *  线程安全：配对表内部自带互斥量，故 handleHello / getPeer / detachByConnection 允许被不同
 *  I/O 工作线程并发调用；本类自身的四个成员（m_joined / m_sessionId / m_lane / m_role）只在
 *  本连接所属的那一个 I/O 线程里写入与读取，不跨线程，因而不再另加锁。
 *
 *  典型用法（见 FileSession.cpp 的两个 case 分支与 FileServer::onDisconnected）：
 *      //收到 cmd_folder_relay_hello：校验并登记，返回 error_code_ok 才算配对成功
 *      int code = m_relayEndpoint.handleHello(body, shared_from_this(), peerAddr);
 *
 *      //收到 cmd_folder_relay_data：取配对端，交给它重新加密后发出
 *      std::shared_ptr<FileSession> peer = m_relayEndpoint.getPeer(conn);
 *
 *      //连接断开：摘除本端，并把同会话其余通道全部关掉
 *      FolderRelayEndpoint::detachByConnection(hub, conn, sessionid, survivors);
 *
 *  balloonwj@qq.com   2026-08-14
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include "../net/TcpConnection.h"
#include "FolderRelayHub.h"

//本端点转发时要拿到对端的会话对象，而 FileSession.h 反过来要包含本文件（每个会话持有一个
//端点），故此处只前向声明；需要完整定义的地方都在 FolderRelayEndpoint.cpp 里
class FileSession;

/**
 *  文件夹中转配对表在 fileserver 里的具体实例化类型。
 *
 *  会话类型取 FileSession、连接指针类型取 std::shared_ptr<TcpConnection>，即
 *  FolderRelayHub.h 类注释给出的接入方式。整个进程只有一份实例，由 FileServer 持有，
 *  各条连接的端点只保存指向它的裸指针（不拥有）。
 */
typedef fileserver::FolderRelayHubT<FileSession, std::shared_ptr<net::TcpConnection>> FolderRelayHub;

/**
 *  一条 fileserver 连接的文件夹传输中转端点。
 *
 *  生命周期与所属的 FileSession 相同：会话建立时构造（此时尚未握手，不占配对表的位置），
 *  会话销毁时随之析构。析构<u>不</u>主动摘表——连接断开的摘除统一在
 *  FileServer::onDisconnected 里做，那里才能拿到需要连带关闭的其余通道并逐个关闭。
 */
class FolderRelayEndpoint
{
public:
    //中转会话指针类型，与 FileServer 持有会话的方式一致
    typedef std::shared_ptr<FileSession> SessionPtr;
    //连接指针类型，与配对表实例化时用的一致
    typedef std::shared_ptr<net::TcpConnection> ConnectionPtr;

    /**
     *  @param hub      配对表指针；由 FileServer 持有，生命周期长于本端点。本类<u>不</u>
     *                  获取其所有权，也不释放它。传 NULL 时握手一律失败（属编程错误，会记日志）
     *  @param tokenKey 中转令牌的 HMAC 校验密钥，来自 fileserver.conf 的 relaytokenkey，
     *                  须与 chatserver.conf 的同名项一致。<u>空串表示本服务不承载文件夹
     *                  中转</u>（imgserver 即如此），此时握手一律拒绝
     */
    FolderRelayEndpoint(FolderRelayHub* hub, const std::string& tokenKey);
    ~FolderRelayEndpoint();

    FolderRelayEndpoint(const FolderRelayEndpoint& rhs) = delete;
    FolderRelayEndpoint& operator=(const FolderRelayEndpoint& rhs) = delete;

    /**
     *  处理一帧中转握手（cmd_folder_relay_hello），校验通过则把本端登记进配对表。
     *
     *  校验顺序为：配对表可用 → 未重复报到 → 本服务已配置中转密钥 → 包体是合法 JSON →
     *  令牌验签且未过期 → 角色是文件夹传输的两个角色之一 → 通道序号在合法区间内 →
     *  登记进配对表。任一步不过即返回对应错误码，且<u>表里不会留下任何登记</u>。
     *
     *  本函数不发送应答帧、不关闭连接：应答与关连接由调用方（FileSession）按返回的错误码
     *  处理，因为应答帧要用本连接的会话密钥加密。
     *
     *  @param body     已还原成明文的握手包体，形如 {"token":"...","lane":0}
     *  @param self     本端会话；配对表只保存其弱引用，故这里必须传<u>持有本会话的那个
     *                  shared_ptr</u>（FileSession 用 shared_from_this() 取得），不能临时新建
     *  @param peerAddr 对端地址（ip:port），只用于写日志
     *  @return 成功返回 error_code_ok（0）；失败返回 chatserversrc/Protocol.h 中 enum
     *          error_code 的一个取值，调用方把它填进握手应答的 code 字段后关闭连接
     *  副作用：成功时本端被登记进配对表，并记一条 LOGI；各失败分支各记一条 LOGE
     */
    int handleHello(const std::string& body, const SessionPtr& self, const std::string& peerAddr);

    /**
     *  本端是否已完成握手并登记进配对表。
     *  @return 已登记返回 true；尚未握手、或握手被拒返回 false
     */
    bool joined() const
    {
        return m_joined;
    }

    /**
     *  本次传输的会话 id（来自中转令牌）。
     *  @return 已登记时为会话 id；尚未登记时为空串
     */
    const std::string& sessionId() const
    {
        return m_sessionId;
    }

    /**
     *  本连接占用的通道序号。
     *  @return 已登记时为 0（控制通道）或 1..N（数据通道）；尚未登记时为一个越界值
     */
    int lane() const
    {
        return m_lane;
    }

    /**
     *  本端在本次传输里的角色。
     *  @return 已登记时为 RelayToken::role_folder_sender / role_folder_receiver；
     *          尚未登记时为 0
     */
    int role() const
    {
        return m_role;
    }

    /**
     *  取本条通道上的配对端会话，供转发数据帧时定位。
     *
     *  @param self 本端连接，用于在该通道的两端中区分自己与对端。本函数不获取、也不释放
     *              该指针的所有权
     *  @return 配对端会话；本端尚未握手、对端那条同序号通道还没连上、或对端已断开时返回
     *          空指针，调用方应丢弃本帧而不是关闭连接（对端可能只是还没连上来）
     */
    SessionPtr getPeer(const ConnectionPtr& self);

    /**
     *  连接断开时把该连接从配对表摘除，并给出需要连带关闭的其余通道。
     *
     *  「任一条通道断开即整个文件夹传输失败」是方案定死的语义（不做自动重连，见
     *  docs/单聊文件夹传输方案.md 第 8.2 节），故这里不只摘掉断开的那一条：本连接确实属于
     *  某次文件夹传输时，把该次传输当前登记着的<u>全部</u>通道都取出来交给调用方关闭，并把
     *  整条会话从表里清掉，使后续各条连接断开时都变成无匹配的空操作。
     *
     *  做成静态方法是因为连接断开时拿不到会话对象上的那个端点实例——FileServer 的断开回调
     *  只收到一个连接指针，而配对表由它自己持有。
     *
     *  @param hub           配对表
     *  @param conn          已断开的连接，按指针相等定位
     *  @param sessionIdOut  [出参] 本函数先清空它；本连接确属某次文件夹传输时回填其会话 id，
     *                       供调用方写日志
     *  @param survivorsOut  [出参] 结果<u>追加</u>到该容器尾部（本函数不清空它）：本次传输
     *                       除断开的这一条之外、当前仍登记着的全部通道端，调用方须逐个取出
     *                       其连接并关闭
     *  @return 本连接确属某次文件夹传输、已摘除返回 true；本连接与文件夹中转无关（普通上传
     *          下载连接、或握手未通过的连接）返回 false，此时两个出参不含有意义的内容
     */
    static bool detachByConnection(FolderRelayHub& hub, const ConnectionPtr& conn,
                                   std::string& sessionIdOut, std::vector<SessionPtr>& survivorsOut);

private:
    FolderRelayHub* m_hub;        //配对表（不拥有，由 FileServer 持有，生命周期长于本端点）
    std::string     m_tokenKey;   //中转令牌 HMAC 校验密钥，空串表示本服务不承载文件夹中转
    bool            m_joined;     //是否已完成握手并登记进配对表；决定本连接能否转发数据帧
    std::string     m_sessionId;  //本次传输的会话 id（握手成功后填，来自令牌）
    int             m_lane;       //本连接的通道序号：0 控制通道，1..N 数据通道（握手成功后填）
    int             m_role;       //本端角色，见 RelayToken::role_folder_*（握手成功后填）
};
