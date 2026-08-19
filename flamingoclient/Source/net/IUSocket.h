#pragma once

#include <thread>
#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include <condition_variable>
#include <atomic>
#include <stdint.h>

#include "MessageProtocolClient.h"   // net::TextFont（发送文本可带字体设置）
#include "TransferOwnerRegistry.h"   // net::TransferOwner / net::TransferOwnerRegistry（1801/1802 应答路由）
#include "GroupProtocolClient.h"     // net::UpdateGroupInfoParams（SendUpdateGroupInfo 按值传引用，需完整类型）
#include "FrameCrypto.h"             // net::IDENTITY_PUBLICKEY_BYTES（服务端公钥成员需完整尺寸）


//v2 协议帧头前置声明。本头文件被众多 .cpp 引用，故不直接包含 Protocol.h ——
//原先是因为它与 v1 的 net/Msg.h 枚举同名冲突（v1 协议头已于 2026-08-14 删除，
//该限制不复存在），现在的理由是不把整个协议头传染给所有使用方。这里只前置
//声明 frame_header 供成员函数签名使用，完整定义由 IUSocket.cpp 内经 FrameCodec.h 引入。
struct frame_header;

//注册请求参数前置声明。Register 只按 const 引用收它，无需完整类型；
//完整定义在 net/RegisterProtocolClient.h，由 IUSocket.cpp 引入。
namespace net { struct RegisterRequestParams; }

//网络通信层只负责数据传输和接收
class CIUSocket
{
private:
	CIUSocket();
    ~CIUSocket(void);

    CIUSocket(const CIUSocket& rhs) = delete;
    CIUSocket& operator = (const CIUSocket& rhs) = delete;

public:
    static CIUSocket&  GetInstance();

    /**
     *  登记 UI 代理窗口句柄。网络线程收到帧后一律用 PostMessage 把结果投给它，
     *  再由 CFlamingoClient::ProxyWndProc 转到具体窗口 —— 网络线程不直接碰界面。
     *  由 CFlamingoClient 在创建代理窗口后调用一次。
     *    hWnd：代理窗口句柄；NULL 表示尚未创建，此后所有投递都会被静默跳过。
     *
     *  这两样东西（代理窗句柄、下面的网络错误标志）此前寄存在 v1 的
     *  CRecvMsgThread 上，那个线程在 v2 下已收不到任何数据、2026-08-14 随 v1
     *  清理一并删除，故搬到这里。
     */
    void SetUIProxyWnd(HWND hWnd);

    /**
     *  上报"网络已断"。向 UI 代理窗口投一条 FMG_MSG_NET_ERROR，由主窗口决定
     *  是提示掉线还是起自动重连。
     *
     *  内部有去重标志：一次断线只上报一次，避免收发两侧同时发现断线时重复触发
     *  重连。标志由 ResetNetError 在每次登录成功后复位。
     */
    void NotifyNetError();

    /**
     *  复位"网络已断"标志，使后续真实断线能再次经 NotifyNetError 上报。
     *  由 CIUSocket::Login 收到成功应答帧后调用 —— 不复位的话，一次掉线或主动
     *  离线之后标志一直是真，之后真断线就不再触发自动重连。
     */
    void ResetNetError();

    bool Init();
    void Uninit();

    void Join();
	
	void	LoadConfig();
	void	SetServer(PCTSTR lpszServer);
	void	SetFileServer(PCTSTR lpszFileServer);
    void	SetImgServer(PCTSTR lpszImgServer);
	void	SetProxyServer(PCTSTR lpszProxyServer);
	void	SetPort(short nPort);
	void    SetFilePort(short nFilePort);
    void    SetImgPort(short nImgPort);
	void	SetProxyPort(short nProxyPort);
	void    SetProxyType(long nProxyType);

    //开启和关闭聊天服务器断线自动重连
    void    EnableReconnect(bool bEnable);

    /**
     *  取 flamingo.ini 里配置的服务端身份公钥，供各条链路发起握手时验签用。
     *
     *  四条链路（主连接、文件传输、图片传输、中转通道）共用同一把公钥——服务端
     *  三个服务也共用同一份身份密钥文件，见 docs/协议加密方案.md 第 6 节。文件传输
     *  与中转通道的实现不在本类里，故以只读访问器的形式提供出去。
     *
     *  @return 指向 IDENTITY_PUBLICKEY_BYTES 字节公钥的指针；<u>未配置 serverpubkey
     *          时返回 NULL</u>，调用方据此判断本链路走明文还是先握手。
     *          返回的内存由本单例持有，进程存续期间一直有效，调用方不得释放。
     *  副作用：无。线程安全：公钥只在 LoadConfig 里写一次，之后只读。
     */
    const unsigned char* GetServerPublicKey() const;

    /**
     *  取文件传输共享连接（m_hFileSocket）的帧体加解密器。
     *
     *  文件传输的组帧逻辑在 net/FileStreamTransfer 里，socket 与加密器却都归本类
     *  持有，故以访问器的形式交出去。握手未完成时返回的对象其 ready() 为假，
     *  组帧会自动走明文，调用方不必自己判断。
     *
     *  @return 指向本类持有的加解密器，恒非空；生命周期同本单例，调用方不得释放。
     *  副作用：无。
     */
    net::FrameCipher* GetFileCipher();

    /**
     *  取图片传输共享连接（m_hImgSocket）的帧体加解密器。语义同 GetFileCipher。
     *  @return 指向本类持有的加解密器，恒非空；生命周期同本单例，调用方不得释放。
     *  副作用：无。
     */
    net::FrameCipher* GetImgCipher();

    /** 
    *@param timeout 超时时间，单位为s
    **/
	bool	Connect(int timeout = 3);
    bool    Reconnect(int timeout = 3);

    /**
     *  探测某服务器地址是否可连通（网络设置的"测试连接"用）。
     *  用一个临时 socket 做带超时的 TCP 连接探测后立即关闭，**不影响**
     *  聊天主连接（m_hSocket）；可在登录前调用。
     *  @param lpszHost    服务器地址（域名或 IP）
     *  @param nPort       端口号
     *  @param nTimeoutSec 连接超时时间，单位秒
     *  @return 能在超时内建立 TCP 连接返回 true，否则返回 false
     */
    bool    TestConnect(PCTSTR lpszHost, short nPort, int nTimeoutSec = 3);
    bool	ConnectToFileServer(int timeout = 3);
    bool	ConnectToImgServer(int timeout = 3);
	
	bool	IsClosed();
	bool	IsFileServerClosed();
    bool	IsImgServerClosed();
	
	void	Close();
	void	CloseFileServerConnection();
    void	CloseImgServerConnection();

    /** 判断普通Socket上是否收到数据
     * @return -1出错， 0无数据 1有数据
     */
	int    CheckReceivedData();

    /**
     *  发送一条单聊文本消息（v2，cmd_send_message=1503）。线程安全，可在 UI
     *  线程调用：内部组帧后追加进发送缓冲、唤醒发送线程。发送回执经
     *  cmd_send_message 应答异步回投到 UI（FMG_MSG_V2_SEND_CHATMSG_ACK）。
     *  @param peerUserId  对端用户 userid（>0）
     *  @param clientMsgId 客户端消息ID（UUID，非空），用于回执回填与去重
     *  @param text        文本正文（UTF-8 字节）
     */
    void    SendChatTextMessage(int64_t peerUserId, const std::string& clientMsgId,
                                const std::string& text,
                                const net::TextFont& font = net::TextFont(),
                                const net::ReplyQuote& quote = net::ReplyQuote());

    /**
     *  发送一条单聊"窗口抖动"消息（v2，cmd_send_message=1503，msgtype=13）。线程
     *  安全，可在 UI 线程调用：内部组帧（content 为空对象）后追加进发送缓冲、
     *  唤醒发送线程。回执经 cmd_send_message 应答异步回投（FMG_MSG_V2_SEND_CHATMSG_ACK）。
     *  @param peerUserId  对端用户 userid（>0）
     *  @param clientMsgId 客户端消息ID（UUID，非空），用于回执回填与去重
     */
    void    SendChatShakeMessage(int64_t peerUserId, const std::string& clientMsgId);

    /**
     *  发送一条群聊文本消息（v2，cmd_send_message=1503，ctype=群聊）。线程安全。
     *  回执经 cmd_send_message 应答异步回投（FMG_MSG_V2_SEND_CHATMSG_ACK）；服务端
     *  随后向全群 fan-out cmd_message_notify。
     *  @param groupId     目标群 id（>0）
     *  @param clientMsgId 客户端消息ID（UUID，非空），用于回执回填与去重
     *  @param text        文本正文（UTF-8 字节）
     */
    void    SendGroupTextMessage(int64_t groupId, const std::string& clientMsgId,
                                 const std::string& text,
                                 const net::TextFont& font = net::TextFont(),
                                 const net::ReplyQuote& quote = net::ReplyQuote());

    /**
     *  发送一条<u>带 @提醒</u>的群聊文本消息（v2，cmd_send_message=1503）。线程安全。
     *  与上面的重载同路径，只是 content 里多一个 at 对象；ats 空且 atAll 为假时
     *  等价于普通群聊文本消息。协议格式见 docs/群聊@提醒方案.md §3。
     *  @param groupId     目标群 id（>0）
     *  @param clientMsgId 客户端消息ID（UUID，非空）
     *  @param text        文本正文（UTF-8 字节）
     *  @param ats         被 @ 的人及其片段（偏移为 text 的 UTF-8 字节口径，调用方
     *                     须已用 RescanMentions 按最终文本重算过）
     *  @param atAll       是否 @所有人
     *  @param font        文本字体设置
     *  @param quote       引用回复快照；m_valid 为真时写入 content.quote
     */
    void    SendGroupTextMessageWithMentions(
                int64_t groupId, const std::string& clientMsgId,
                const std::string& text,
                const std::vector<balloonchatwnd::MentionSpan>& ats,
                bool atAll,
                const net::TextFont& font = net::TextFont(),
                const net::ReplyQuote& quote = net::ReplyQuote());

    /**
     *  发起一次远程桌面邀请（v2，cmd_rd_invite=1710）。线程安全。应答（成功带
     *  sessionid，失败如对方离线 / 忙）经 cmd_rd_invite 异步回投 FMG_MSG_V2_RD_INVITE_ACK。
     *  @param peerUserId 被控端用户 userid（>0）
     */
    void    SendRemoteDesktopInvite(int64_t peerUserId);

    /**
     *  被控端应答远程桌面邀请（v2，cmd_rd_response=1712）。线程安全。接受后双方收
     *  cmd_rd_relay_info，拒绝时控制端收 cmd_rd_state_notify。
     *  @param sessionId 邀请推送里带来的会话 id
     *  @param accept    true 接受 / false 拒绝
     */
    void    SendRemoteDesktopResponse(const std::string& sessionId, bool accept);

    /**
     *  控制端在被控端应答前取消远程桌面邀请（v2，cmd_rd_cancel=1713）。线程安全。
     *  @param sessionId 会话 id
     */
    void    SendRemoteDesktopCancel(const std::string& sessionId);

    /**
     *  结束远程桌面会话（v2，cmd_rd_close=1714），控制端 / 被控端均可调。线程安全。
     *  @param sessionId 会话 id
     */
    void    SendRemoteDesktopClose(const std::string& sessionId);

    /**
     *  发起一次文件夹传输邀请（v2，cmd_folder_invite=1810）。线程安全。组帧发出后立即
     *  返回所用的帧 seq；应答（成功带 sessionid，失败如规模超限 / 对方不在线 / 服务器
     *  繁忙）经 cmd_folder_invite 异步回投 FMG_MSG_V2_FOLDER_INVITE_ACK，<u>wParam 即
     *  本次的 seq</u>，调用方据它把应答对回是哪一笔发起的。
     *
     *  之所以要把 seq 交出去：应答包体里只有结果码与服务端分配的会话 id，没有任何能对回
     *  发起方的字段，而本机同时可以有多笔传输在等应答。按 seq 关联与 RequestUpload /
     *  RequestDownload / SendCheckUpdate 的做法一致，不依赖"应答顺序等于请求顺序"。
     *  @param peerUserId 接收方 userid（>0）
     *  @param folderName 根目录名（只有名字、不含路径，UTF-8，非空）
     *  @param totalDirs  目录总数（含空目录、不含根目录自身），单位：个
     *  @param totalFiles 文件总数，单位：个
     *  @param totalSize  全部文件字节数合计，单位：字节
     *  @return 本次邀请所用的帧 seq；包体构造不出来（入参不合契约）时返回 0 并只记一条
     *          错误日志、不发包，调用方据此当作"这次没发出去"
     */
    uint32_t SendFolderInvite(int64_t peerUserId, const std::string& folderName,
                              int64_t totalDirs, int64_t totalFiles, int64_t totalSize);

    /**
     *  接收方应答文件夹传输邀请（v2，cmd_folder_response=1812）。线程安全。同意后双方
     *  收 cmd_folder_relay_info，拒绝时发送方收 cmd_folder_state_notify。
     *  @param sessionId 邀请推送里带来的会话 id
     *  @param accept    true 接收 / false 拒绝
     */
    void    SendFolderResponse(const std::string& sessionId, bool accept);

    /**
     *  发送方在接收方应答前撤回邀请（v2，cmd_folder_cancel=1813）。线程安全。
     *  传输已经开始之后的取消不走这条，而是按失败流程发 SendFolderClose，
     *  错误码取 error_code_folder_canceled。
     *  @param sessionId 会话 id
     */
    void    SendFolderCancel(const std::string& sessionId);

    /**
     *  结束一次文件夹传输（v2，cmd_folder_close=1814），收发两端均可调。线程安全。
     *  成功结束时服务端立即销毁会话；失败结束时会话保留 folderretrywindow（默认 10 分钟）
     *  供重试续传，失败原因由服务端原样透传给对端，两端因而看到同一条原因。
     *  @param sessionId 会话 id
     *  @param success   true 成功结束 / false 失败结束
     *  @param errCode   失败原因，取值须落在 error_code_folder_*（606~620）之内；
     *                   success 为 true 时本参数被忽略
     *  @param errMsg    英文诊断串，可为空；success 为 true 时本参数被忽略
     */
    void    SendFolderClose(const std::string& sessionId, bool success,
                            int errCode, const std::string& errMsg);

    /**
     *  保留期内重试续传（v2，cmd_folder_retry=1817），由发送方发起。线程安全。
     *  服务端校验会话未过期、双方仍在线后重签令牌并向双方各再推一帧
     *  cmd_folder_relay_info，全程不再打扰接收方。
     *  @param sessionId 失败会话的会话 id
     */
    void    SendFolderRetry(const std::string& sessionId);

    /**
     *  发起一次音视频通话邀请（v2，cmd_call_invite=1701）。线程安全。应答（成功带
     *  sessionid，失败如对方离线 / 忙）经 cmd_call_invite 异步回投 FMG_MSG_V2_CALL_INVITE_ACK。
     *  @param peerUserId 被叫用户 userid（>0）
     *  @param media      媒体类型（net::call_media；第一期固定纯语音 call_media_audio）
     */
    void    SendCallInvite(int64_t peerUserId, int media);

    /**
     *  被叫应答音视频通话邀请（v2，cmd_call_response=1703）。线程安全。接听后双方收
     *  cmd_call_signaling 与接通态，拒绝时主叫收 cmd_call_state_notify。
     *  @param sessionId 邀请推送里带来的会话 id
     *  @param accept    true 接听 / false 拒绝
     */
    void    SendCallResponse(const std::string& sessionId, bool accept);

    /**
     *  主叫在被叫应答前取消呼叫（v2，cmd_call_cancel=1704）。线程安全。
     *  @param sessionId 会话 id
     */
    void    SendCallCancel(const std::string& sessionId);

    /**
     *  挂断音视频通话（v2，cmd_call_hangup=1705），主叫 / 被叫均可调。线程安全。
     *  @param sessionId 会话 id
     */
    void    SendCallHangup(const std::string& sessionId);

    /**
     *  通话中切换媒体类型（v2，cmd_call_switch_media=1708）。线程安全。
     *
     *  仅通话已接通时可用。应答经 FMG_MSG_V2_CALL_SWITCH_MEDIA_RESULT 回投 UI（失败时
     *  据 code 提示原因）；服务端切换成功后还会向<u>双方（含本端）</u>推送
     *  cmd_call_media_changed_notify，客户端统一在收到<u>推送</u>时才改界面与引擎 ——
     *  这样本端与对端看到的媒体类型必然一致。
     *
     *  @param sessionId 本次会话 id；为空则本方法直接跳过不发包
     *  @param media     目标媒体类型，取值见 net::call_media；非法值同样跳过不发包
     */
    void    SwitchCallMedia(const std::string& sessionId, int media);

    /**
     *  申请上传一个文件（v2，cmd_request_upload=1801）。线程安全。组帧发出后立即
     *  返回所用的帧 seq，并按 owner 在归属表登记该 seq；应答（秒传判断 / 上传令牌 /
     *  目标服务器）经 cmd_request_upload 异步回到 HandleV2Frame，再按 seq 查归属转交
     *  给 ImageTransfer 或 FileTransfer 的等待槽。一般由二者的工作线程调用，不直接
     *  面向 UI。
     *  @param scene 文件用途场景，取值见 enum file_scene（图片消息传 file_scene_image_msg，
     *               聊天文件传 file_scene_chat_file）
     *  @param md5   文件内容MD5（小写十六进制，32 位）
     *  @param size  文件总字节数（>0）
     *  @param name  原始文件名（图片场景可空，文件场景须带含扩展名的原始名）
     *  @param ext   扩展名，不含点（可空）
     *  @param owner 传输归属（见 enum net::TransferOwner），决定应答转交给图片还是文件
     *               传输管理器；缺省为图片，使既有 ImageTransfer 调用无需改动
     *  @return 本次申请所用的帧 seq，调用方据此关联应答
     */
    uint32_t RequestUpload(int scene, const std::string& md5, int64_t size,
                           const std::string& name, const std::string& ext,
                           int owner = net::transfer_owner_image);

    /**
     *  申请下载一个文件（v2，cmd_request_download=1802）。线程安全。组帧发出后立即
     *  返回所用的帧 seq，并按 owner 在归属表登记该 seq；应答（下载令牌 / 目标服务器 /
     *  md5 / 大小）经 cmd_request_download 异步回到 HandleV2Frame，再按 seq 查归属转交
     *  给 ImageTransfer 或 FileTransfer 的下载等待槽。一般由二者的工作线程调用。
     *  @param fileid 待下载文件的文件ID（字符串编码，来自图片 / 文件消息 content）
     *  @param owner  传输归属（见 enum net::TransferOwner）；缺省为图片，使既有
     *                ImageTransfer 调用无需改动
     *  @return 本次申请所用的帧 seq，调用方据此关联应答
     */
    uint32_t RequestDownload(const std::string& fileid, int owner = net::transfer_owner_image);

    //发 cmd_check_update(1905)，返回本次请求的 seq；未连上返回 0。
    //  body：请求包体，由 net::UpdateProtocolClient::buildCheckUpdateRequest 组。
    //应答异步回到 HandleV2Frame，再按 seq 转交 update::UpdateManager。
    uint32_t SendCheckUpdate(const std::string& body);

    /**
     *  异步收帧线程是否已经在跑。
     *
     *  @return 已启动返回 true。
     *
     *  它在 Init() 里启动，而 Init() 要<u>登录成功之后</u>才被调用（见
     *  会话控制器 OnLoginResult 的 LOGIN_SUCCESS 分支）。登录前这条线程不能起 ——
     *  它会把 Login() 正等着同步收取的那一帧应答抢走。所以凡是"登录前也要能用"的
     *  请求（目前只有检查更新），都必须据本函数判断走同步收发那条路。
     */
    bool    IsAsyncRecvRunning() const;

    /**
     *  同步发一次检查更新请求并等应答（登录前专用）。
     *
     *    reqBody：   已组好的请求体 JSON（见 net::UpdateProtocolClient::buildCheckUpdateRequest）
     *    respBody：  [出参] 应答体 JSON；返回 false 时内容未定义
     *    nTimeoutSec：超时秒数，与登录取同一口径
     *
     *  @return 收到完整应答返回 true；连不上、发不出去、超时都返回 false。
     *
     *  与 Login() 同构：Connect → 编帧 → SendData → RecvOneFrame。
     *  <u>只能在异步收帧线程未启动时调用</u>，否则会与它抢同一条连接上的帧。
     */
    bool    CheckUpdateSync(const std::string& reqBody, std::string& respBody, int nTimeoutSec);

    /**
     *  发送一条图片消息（v2，cmd_send_message=1503，msgtype=2）。线程安全。图片须
     *  已上传到 imgserver（先经 cmd_request_upload + cmd_file_upload 拿到 fileid）。
     *  回执沿用 cmd_send_message 应答路径（FMG_MSG_V2_SEND_CHATMSG_ACK）。一般由
     *  ImageTransfer 工作线程在字节流上传完成后调用。
     *  @param ctype       会话类型（单聊=1、群聊=2，见 enum conversation_type）
     *  @param cid         会话ID（单聊=对端 userid，群聊=群id）
     *  @param clientMsgId 客户端消息ID（UUID，非空）
     *  @param fileid      图片文件ID（字符串编码，来自 cmd_request_upload 应答）
     *  @param md5         图片内容MD5（小写十六进制）
     *  @param width       图片像素宽（>0）
     *  @param height      图片像素高（>0）
     *  @param size        图片字节数（>0）
     */
    void    SendChatImageMessage(int ctype, int64_t cid, const std::string& clientMsgId,
                                 const std::string& fileid, const std::string& md5,
                                 int width, int height, int64_t size);

    /**
     *  发送一条文件消息（v2，cmd_send_message=1503，msgtype=3）。线程安全。文件须已
     *  上传到 fileserver（先经 cmd_request_upload + cmd_file_upload 拿到 fileid）。回执
     *  沿用 cmd_send_message 应答路径（FMG_MSG_V2_SEND_CHATMSG_ACK）。一般由 FileTransfer
     *  工作线程在字节流上传完成后调用。
     *  @param ctype       会话类型（单聊=1、群聊=2，见 enum conversation_type）
     *  @param cid         会话ID（单聊=对端 userid，群聊=群id）
     *  @param clientMsgId 客户端消息ID（UUID，非空）
     *  @param fileid      文件ID（字符串编码，来自 cmd_request_upload 应答）
     *  @param filename    原始文件名（含扩展名，UTF-8，非空）
     *  @param size        文件字节数（>0）
     *  @param md5         文件内容MD5（小写十六进制）
     */
    void    SendChatFileMessage(int ctype, int64_t cid, const std::string& clientMsgId,
                                const std::string& fileid, const std::string& filename,
                                int64_t size, const std::string& md5);

    /**
     *  取 UI 代理窗口句柄，供 ImageTransfer 把发图进度 / 结果异步回投到 UI。
     *  @return UI 代理窗口句柄；接收线程未就绪时返回 NULL
     */
    HWND    GetUIProxyWnd();

    /**
     *  请求某群的群资料（v2，cmd_get_group_info=1303）。线程安全。应答经
     *  cmd_get_group_info 异步回投到 UI（FMG_MSG_V2_GROUP_INFO）。
     *  @param groupId 目标群 id（>0）
     */
    void    RequestGroupInfo(int64_t groupId);

    /**
     *  请求某群的群成员列表（v2，cmd_get_group_members=1304）。线程安全。应答经
     *  cmd_get_group_members 异步回投到 UI（FMG_MSG_V2_GROUP_MEMBERS）。
     *  @param groupId 目标群 id（>0）
     */
    void    RequestGroupMembers(int64_t groupId);

    /**
     *  请求某群的群文件库列表的一页（v2，cmd_get_group_files=1803）。线程安全。应答经
     *  cmd_get_group_files 异步回投到 UI（FMG_MSG_V2_GROUP_FILES）。客户端按 Option B
     *  全量缓存：群聊窗首次打开「群文件」面板时从 offset=0 起逐页拉取，应答里 hasmore
     *  为真则继续拉下一页，直到拉全。
     *  @param groupId  目标群 id（>0）
     *  @param filetype 服务端类型筛选（见 group_file_type）；客户端全量缓存传 0=全部
     *  @param offset   分页偏移（>=0）
     *  @param count    本页期望条数（服务端上限 50）
     */
    void    RequestGroupFiles(int64_t groupId, int filetype, int offset, int count);

    /**
     *  把一个已上传到 fileserver（scene=群文件）的文件登记进群文件库（v2，
     *  cmd_add_group_file=1804）。线程安全。服务端登记成功后会向全群（含本人）推
     *  cmd_group_files_changed_notify(1806)，各端据此刷新群文件库；本请求的应答仅作
     *  成功 / 失败日志，不单独路由。一般在群文件上传完成回投后由群聊窗调用。
     *  @param groupId 目标群id（>0）
     *  @param fileid  已上传文件的文件ID（来自上传结果，字符串编码int64）
     */
    void    RequestAddGroupFile(int64_t groupId, const std::string& fileid);

    /**
     *  删除一个群文件登记（v2，cmd_delete_group_file=1805）。线程安全。服务端校验
     *  权限（群主 / 管理员 / 上传者本人）后删 t_group_file 登记行，并向全群（含本人）推
     *  cmd_group_files_changed_notify(1806, changetype=removed)，各端据此把该文件移出
     *  群文件库。本请求的应答仅作成功 / 失败日志，不单独路由 UI（UI 的移除靠 1806）。
     *  一般由群聊窗在用户确认删除后调用；调用方须自行做权限门控（与服务端同规则）。
     *  @param groupId 目标群id（>0）
     *  @param entryid 待删除的群文件条目ID（字符串编码int64，来自列表项 m_entryid）
     */
    void    RequestDeleteGroupFile(int64_t groupId, const std::string& entryid);

    /**
     *  请求拉取本人加入的全部群列表（v2，cmd_get_user_groups=1320）。线程安全。
     *  应答经 cmd_get_user_groups 异步回投到 UI（FMG_MSG_V2_USER_GROUPS）。一般在
     *  应答经 cmd_get_user_groups 异步回投到 UI（FMG_MSG_V2_USER_GROUPS）。一般在
     *  登录成功、主面板显示后调用一次，用于填"群"Tab（含无消息的空群）。
     */
    void    RequestUserGroups();

    /**
     *  请求拉取某会话的历史消息（v2，cmd_sync_messages=1502）。线程安全。应答经
     *  cmd_sync_messages 异步回投到 UI（FMG_MSG_V2_SYNC_MESSAGES）。聊天窗打开时
     *  调用一次拉最近 N 条（beforeMsgId=0、limit=100）。
     *  @param ctype       会话类型（1 单聊 2 群聊）
     *  @param cid         会话ID（单聊=对端userid，群聊=群id）
     *  @param beforeMsgId 翻历史锚点：取此 msgid 之前的消息；0=从最新开始
     *  @param limit       本次最多拉取条数（服务端上限 100）
     */
    void    RequestSyncMessages(int ctype, int64_t cid, int64_t beforeMsgId, int limit);

    /**
     *  发送创建群聊请求（v2，cmd_create_group=1301）。线程安全，可在 UI 线程调用：
     *  内部组帧后追加进发送缓冲、唤醒发送线程。应答经 cmd_create_group 异步回投到
     *  UI（FMG_MSG_V2_CREATE_GROUP_RESULT）。
     *  @param groupName  群名称（UTF-8，非空；调用方须保证）
     *  @param memberIds  初始成员 userid 列表（不含创建者；可为空）
     */
    void    SendCreateGroup(const std::string& groupName, const std::vector<int64_t>& memberIds);

    /**
     *  发送"会话设置"请求（v2，cmd_set_conversation=1513）：改置顶 / 免打扰。线程安全，
     *  可在 UI 线程调用。只有 hasPinned / hasMute 为 true 的字段会写进请求，未指定的
     *  字段服务端保持原值。应答（通用 {code,msg}）经 cmd_set_conversation 异步回投到
     *  UI（FMG_MSG_V2_SET_CONVERSATION_RESULT）。
     *  @param ctype     会话类型（1单聊 2群聊）
     *  @param cid       会话ID（群聊=群id）
     *  @param hasPinned true=本次要改置顶，按 pinned 写入；false 则不带 pinned 字段
     *  @param pinned    置顶目标值（hasPinned 为 true 时有效）
     *  @param hasMute   true=本次要改免打扰，按 mute 写入；false 则不带 mute 字段
     *  @param mute      免打扰目标值（hasMute 为 true 时有效）
     */
    void    SendSetConversation(int ctype, int64_t cid,
                                bool hasPinned, bool pinned,
                                bool hasMute, bool mute);

    /**
     *  发送"删除会话"请求（v2，复用 cmd_set_conversation=1513 的 deleted 字段）。线程
     *  安全，可在 UI 线程调用。语义是把该会话从最近会话列表移除，<u>不删除聊天消息
     *  本体</u>；该会话之后再来新消息时会重新出现在列表里。应答（通用 {code,msg}）经
     *  cmd_set_conversation 异步回投到 UI（FMG_MSG_V2_SET_CONVERSATION_RESULT）。
     *  @param ctype  会话类型（1单聊 2群聊）
     *  @param cid    会话ID（单聊=对端userid，群聊=群id；>0）
     */
    void    SendDeleteConversation(int ctype, int64_t cid);

    /**
     *  发送"加入 / 取消收藏"请求（v2，cmd_set_favorite=1215）。线程安全，可在 UI
     *  线程调用。收藏对象既可以是联系人也可以是群，用 (ctype, cid) 区分，口径与会话
     *  一致。应答经 cmd_set_favorite 异步回投到 UI（FMG_MSG_V2_SET_FAVORITE_RESULT，
     *  lParam 为堆上的 net::SetFavoriteResult*），主面板据其中原样回带的 ctype/cid/favorite
     *  判断是哪一条请求的结果，失败时回滚对应的乐观更新。
     *  @param ctype     收藏对象类型（1联系人 2群组）
     *  @param cid       收藏对象ID（联系人=对端userid，群组=群id；>0）
     *  @param favorite  true=加入收藏，false=移出收藏
     */
    void    SendSetFavorite(int ctype, int64_t cid, bool favorite);

    /**
     *  发送"特别关注 / 取消特别关注"请求（v2，cmd_set_starred=1216）。线程安全，可在
     *  UI 线程调用。与收藏不同，特别关注<u>只能是人</u>（群没有上下线，关注它没有意义），
     *  故没有 ctype 参数。应答经 cmd_set_starred 异步回投到 UI
     *  （FMG_MSG_V2_SET_STARRED_RESULT，lParam 为堆上的 net::SetStarredResult*），
     *  主面板据其中原样回带的 target/starred 判断是哪一条请求的结果，失败时回滚
     *  对应的乐观更新。
     *  @param target   被关注的联系人 userid（>0，须与本人同企业、且不能是自己）
     *  @param starred  true=标记特别关注，false=取消
     */
    void    SendSetStarred(int64_t target, bool starred);

    /**
     *  发送"设置好友备注名"请求（v2，cmd_set_friend_remark=1209）。线程安全，可在
     *  UI 线程调用。应答经同一 cmd 异步回投到 UI（FMG_MSG_V2_SET_REMARK_RESULT，
     *  lParam 为堆上的 net::SetFriendRemarkResult*），主面板据其中原样回带的 userid
     *  判断是哪一条请求的结果，失败时回滚对应的乐观更新。
     *
     *  备注名是单向的（只影响本人看该好友的显示），改完<u>不走推送</u>；本端的显示
     *  靠发起时的乐观更新，重登后靠通讯录应答里的 contacts[].remark 读回。
     *
     *  @param userid      目标好友 userid（>0）
     *  @param remarkUtf8  新备注名（UTF-8）；<u>空串表示清除备注</u>，是合法取值
     */
    void    SendSetFriendRemark(int64_t userid, const std::string& remarkUtf8);

    /**
     *  发送"设置联系人标签"请求（v2，cmd_set_contact_tags=1217）。线程安全，可在
     *  UI 线程调用。应答经同一 cmd 异步回投到 UI（FMG_MSG_V2_SET_TAGS_RESULT，
     *  lParam 为堆上的 net::SetContactTagsResult*），主面板据其中原样回带的 userid
     *  判断是哪一条请求的结果。
     *
     *  本请求是<u>整份覆盖</u>：传的是该联系人在本人视角下的完整标签列表，服务端把
     *  旧的全删掉再按本列表重建。增、改、删三种操作共用本接口，客户端不必自己算差异。
     *
     *  标签是单向的（只影响本人看该联系人的显示），改完<u>不走推送</u>；本端的显示
     *  靠发起时的乐观更新加应答回带的归一结果，重登后靠通讯录应答里的
     *  contacts[].tags 读回。
     *
     *  @param userid    目标联系人 userid（>0，且不能是自己 —— 服务端会拒绝）
     *  @param tagsUtf8  完整标签列表（UTF-8）；<u>空列表表示清空</u>，是合法取值。
     *                   本接口不做归一（不 trim、不去重、不判长度），原样发出，
     *                   归一由服务端完成并随应答回带。
     */
    void    SendSetContactTags(int64_t userid, const std::vector<std::string>& tagsUtf8);

    /**
     *  发送「拉取发给本人的平台公告」请求（v2，cmd_get_announcements=2101）。线程安全，
     *  可在 UI 线程调用。应答经同一 cmd 异步回投到 UI（FMG_MSG_V2_ANNOUNCEMENTS，
     *  lParam 为堆上的 net::AnnouncementsResult*）。
     *
     *  这是主面板「消息盒子」的数据来源：登录后全量拉一遍（sinceId 传 0）落本地库，
     *  之后靠 cmd_announcement_notify 推送收增量；应答里 hasmore 为 true 时，应以本批
     *  最后一条的公告ID作新的 sinceId 再拉一次。
     *
     *  @param sinceId  增量游标，只要ID大于它的公告；0 表示全量拉取
     *  @param limit    本次最多要多少条；服务端会规整到 [1,100]，传 0 即用服务端默认值
     */
    void    SendGetAnnouncements(int64_t sinceId, int limit);

    /**
     *  发送「某条平台公告已读」上报（v2，cmd_mark_announcement_read=2102）。线程安全，
     *  可在 UI 线程调用。应答经同一 cmd 异步回投到 UI（FMG_MSG_V2_ANN_MARK_READ_RESULT，
     *  lParam 为堆上的 net::AnnounceMarkReadResult*，其中原样回带 announcementid）。
     *
     *  用户在消息盒子里点开某条公告时调用。服务端据此写 t_announcement_read，管理控制台
     *  的「已读进度」统计的就是这张表。重复上报是安全的，服务端靠唯一键吸收。
     *
     *  @param announcementId  被读的公告ID（>0）
     */
    void    SendMarkAnnouncementRead(int64_t announcementId);

    /**
     *  发送"修改本人群名片"请求（v2，cmd_set_member_card=1315）。线程安全，可在 UI
     *  线程调用。应答（通用 {code,msg}）经 cmd_set_member_card 异步回投到 UI
     *  （FMG_MSG_V2_SET_MEMBER_CARD_RESULT）。
     *  @param groupId  目标群ID（>0）
     *  @param cardUtf8 新群名片（UTF-8，可为空=清空名片）
     */
    void    SendSetMemberCard(int64_t groupId, const std::string& cardUtf8);

    /**
     *  发送"退出群聊"请求（v2，cmd_leave_group=1309）。线程安全，可在 UI 线程调用。
     *  应答（通用 {code,msg}）经 cmd_leave_group 异步回投到 UI
     *  （FMG_MSG_V2_LEAVE_GROUP_RESULT）。
     *  @param groupId  目标群ID（>0）
     */
    void    SendLeaveGroup(int64_t groupId);

    // ================================================================
    // 群管理（群管理窗口用）
    // ================================================================
    //
    // 以下写操作的应答体都是通用 { code, msg }，统一经
    // FMG_MSG_V2_GROUP_OP_RESULT 回投，wParam 带上对应的 cmd 协议号。
    // 界面收到成功应答时<u>不要</u>直接改界面 —— 服务端在写成功后会向全体成员推
    // 1317 / 1318，本端也在推送范围内，等推送统一刷新即可，否则同一次操作会
    // 重排两遍界面。只有失败路径需要靠应答给提示（失败没有推送）。

    /**
     *  拉取待审批的加群申请列表（cmd_get_group_join_requests=1319）。仅群主 /
     *  管理员有权，普通成员会收到 error_code_not_group_admin。
     *  应答经 FMG_MSG_V2_GROUP_JOIN_REQUESTS 回投。
     *  @param groupId 目标群ID（>0）
     */
    void    SendGetGroupJoinRequests(int64_t groupId);

    /**
     *  审批一条加群申请（cmd_handle_join_group=1308）。
     *  @param requestId 申请ID（>0），来自 1319 应答或 1307 推送
     *  @param accept    true 同意入群，false 拒绝
     */
    void    SendHandleJoinGroup(int64_t requestId, bool accept);

    /**
     *  邀请好友入群（cmd_invite_to_group=1305）。群主 / 管理员直接拉人；普通成员
     *  在群关闭"成员邀请免审批"时，邀请会被服务端转成待审批申请（仍回 ok）。
     *  @param groupId 目标群ID（>0）
     *  @param userIds 被邀请人 userid 列表，不可为空
     */
    void    SendInviteToGroup(int64_t groupId, const std::vector<int64_t>& userIds);

    /**
     *  移出群成员（cmd_kick_group_member=1310）。仅群主 / 管理员有权；管理员不能
     *  移出其他管理员，任何人不能移出群主。
     *  @param groupId 目标群ID（>0）
     *  @param userIds 被移出的 userid 列表，不可为空
     */
    void    SendKickGroupMembers(int64_t groupId, const std::vector<int64_t>& userIds);

    /**
     *  设置 / 取消管理员（cmd_set_member_role=1312）。仅群主有权。
     *  @param groupId 目标群ID（>0）
     *  @param userId  目标成员userid
     *  @param isAdmin true 设为管理员，false 降为普通成员
     */
    void    SendSetMemberRole(int64_t groupId, int64_t userId, bool isAdmin);

    /**
     *  禁言 / 解禁某成员（cmd_mute_member=1313）。仅群主 / 管理员有权。
     *  @param groupId  目标群ID（>0）
     *  @param userId   目标成员userid
     *  @param muted    true 禁言，false 解禁
     *  @param duration 禁言时长（秒）；0 表示永久禁言。muted 为 false 时忽略
     *  @param reason   禁言原因（UTF-8），可为空。muted 为 false 时忽略
     */
    void    SendMuteMember(int64_t groupId, int64_t userId, bool muted,
                           int duration, const std::string& reason);

    /**
     *  开启 / 关闭全员禁言（cmd_mute_all=1314）。仅群主 / 管理员有权；开启后
     *  管理员及以上仍可发言。
     *  @param groupId 目标群ID（>0）
     *  @param muteAll true 开启，false 关闭
     */
    void    SendMuteAll(int64_t groupId, bool muteAll);

    /**
     *  修改群资料与群权限（cmd_update_group_info=1311）。仅群主 / 管理员有权。
     *  只有 params 里 m_hasXxx 为 true 的项会被发送与更新。
     *  @param params 待修改项集合，m_groupid 必填
     */
    void    SendUpdateGroupInfo(const net::UpdateGroupInfoParams& params);

    /**
     *  修改群头像（复用 cmd_update_group_info=1311 的 facetype / customface 两项）。
     *  仅群主 / 管理员有权，普通成员会拿回 error_code_not_group_admin。
     *
     *  单独开这个口子而不是让调用方自己填 UpdateGroupInfoParams：改头像与基本
     *  信息页的「保存修改」是两个独立入口，混在一起就得在保存逻辑里比对头像有没有
     *  变过。这里只发头像两项，别的项一律不带。
     *
     *  @param groupId        目标群ID（>0）
     *  @param facetype       头像类型（见 BalloonMain/AvatarSpec.h AvatarKind：
     *                        0 自动 / 1 字形 / 2 自定义图）
     *  @param customfaceUtf8 头像标识（UTF-8）：字形头像为 "glyph|colorIdx"，
     *                        自定义图为 fileid；恢复默认头像时传空串 —— 空串<u>会</u>
     *                        照常发出去（服务端据此把库里旧值清掉），不会被省略
     */
    void    SendChangeGroupAvatar(int64_t groupId, int facetype,
                                  const std::string& customfaceUtf8);

    /**
     *  转让群主（cmd_transfer_owner=1316）。仅群主有权。
     *  @param groupId  目标群ID（>0）
     *  @param newOwner 新群主userid，须为本群现有成员且不是自己
     */
    void    SendTransferOwner(int64_t groupId, int64_t newOwner);

    /**
     *  解散群聊（cmd_dismiss_group=1302）。仅群主有权，不可撤销。
     *  @param groupId 目标群ID（>0）
     */
    void    SendDismissGroup(int64_t groupId);

    /**
     *  拉取本群敏感词库（cmd_get_sensitive_words=1321）。仅群主 / 管理员有权。
     *  应答经 FMG_MSG_V2_SENSITIVE_WORDS 回投。
     *  @param groupId 目标群ID（>0）
     */
    void    SendGetSensitiveWords(int64_t groupId);

    /**
     *  新增一条敏感词（cmd_operate_sensitive_word=1322）。仅群主 / 管理员有权。
     *  重复添加同一个词服务端会静默忽略并仍回 ok。
     *  @param groupId 目标群ID（>0）
     *  @param wordUtf8 敏感词（UTF-8），不可为空
     */
    void    SendAddSensitiveWord(int64_t groupId, const std::string& wordUtf8);

    /**
     *  删除一条敏感词（cmd_operate_sensitive_word=1322）。仅群主 / 管理员有权。
     *  @param groupId 目标群ID（>0）
     *  @param wordId  词条ID，来自 1321 应答
     */
    void    SendDeleteSensitiveWord(int64_t groupId, int64_t wordId);

    /**
     *  拉取本群公告列表（cmd_get_group_announcements=1323）。全体群成员都有权。
     *  应答经 FMG_MSG_V2_GROUP_ANNOUNCEMENTS 回投。
     *  @param groupId 目标群ID（>0）
     *  @param offset  分页起始下标，从 0 起
     *  @param count   本页最多几条；服务端按 100 封顶
     */
    void    SendGetGroupAnnouncements(int64_t groupId, int offset, int count);

    /**
     *  发布一条新公告（cmd_publish_group_announcement=1324）。仅群主 / 管理员有权。
     *  应答走通用的 FMG_MSG_V2_GROUP_OP_RESULT；成功后服务端会推 1326 与 1318，
     *  界面靠推送刷新，不必在应答里自己改。
     *  @param groupId     目标群ID（>0）
     *  @param titleUtf8   标题（UTF-8），非空、不超过 128 个字符
     *  @param contentUtf8 正文（UTF-8），可空、不超过 2048 个字符
     *  @param severity    优先级，取值见 group_announcement_severity
     */
    void    SendPublishGroupAnnouncement(int64_t groupId, const std::string& titleUtf8,
                                         const std::string& contentUtf8, int severity);

    /**
     *  把一条历史公告重新发布一遍（cmd_publish_group_announcement=1324 的另一种用法）。
     *  标题正文由服务端读回原件补齐，结果是历史里多出一条新记录。
     *  @param groupId    目标群ID（>0）
     *  @param announceId 原公告ID，来自 1323 应答
     */
    void    SendRepublishGroupAnnouncement(int64_t groupId, int64_t announceId);

    /**
     *  删除一条公告（cmd_delete_group_announcement=1325）。仅群主 / 管理员有权，
     *  且可删任意一条（不限于自己发的）。<u>不可恢复</u>，调用方须先让用户确认。
     *  @param groupId    目标群ID（>0）
     *  @param announceId 公告ID，来自 1323 应答
     */
    void    SendDeleteGroupAnnouncement(int64_t groupId, int64_t announceId);

    /**
     *  发送修改登录密码请求（v2，cmd_change_password=1105）。线程安全，可在 UI 线程
     *  调用：内部组帧后追加进发送缓冲、唤醒发送线程。应答经 cmd_change_password 异步
     *  回投到 UI（FMG_MSG_V2_CHANGE_PWD_RESULT）。
     *  @param oldHashHex 旧口令的 SHA-256 小写 hex（调用方须已哈希，明文不入网络/日志）
     *  @param newHashHex 新口令的 SHA-256 小写 hex（调用方须已哈希）
     */
    void    SendChangePassword(const std::string& oldHashHex, const std::string& newHashHex);

    /**
     *  发送"更新个性签名"请求（v2，cmd_update_profile=1104，仅含 signature 字段）。
     *  线程安全，可在 UI 线程调用：内部组帧后追加进发送缓冲、唤醒发送线程。应答经
     *  cmd_update_profile 异步回投到 UI（FMG_MSG_V2_UPDATE_PROFILE_RESULT）。
     *  @param signatureUtf8 新个性签名（UTF-8；可为空串，表示清空签名）
     */
    void    SendUpdateSignature(const std::string& signatureUtf8);

    /**
     *  发送"更新本人资料（全字段）"请求（v2，cmd_update_profile=1104）。线程安全，可在
     *  UI 线程调用。应答同样经 cmd_update_profile 异步回投到 UI
     *  （FMG_MSG_V2_UPDATE_PROFILE_RESULT）。各串均为 UTF-8。
     *  @param nicknameUtf8  昵称   @param signatureUtf8 个性签名
     *  @param gender        性别（0 男 / 1 女）  @param birthday YYYYMMDD 整数（0 未设）
     *  @param addressUtf8   所在地  @param mailUtf8 邮箱
     */
    void    SendUpdateProfile(const std::string& nicknameUtf8,
                              const std::string& signatureUtf8,
                              int gender, long birthday,
                              const std::string& addressUtf8,
                              const std::string& mailUtf8);

    /**
     *  发送"修改头像"请求（v2，cmd_change_avatar=1106）。线程安全，可在 UI 线程
     *  调用：内部组帧后追加进发送缓冲、唤醒发送线程。系统头像 / 首字母 / 自定义图
     *  都走这条，facetype 区分类型、customface 承载标识（编码约定见
     *  BalloonMain/AvatarSpec.h）。应答（通用 {code,msg}）经 cmd_change_avatar 异步
     *  回投到 UI（FMG_MSG_V2_CHANGE_AVATAR_RESULT）。
     *  @param facetype       头像类型（0 自动 / 1 字形 / 2 自定义图）
     *  @param customfaceUtf8 头像标识（UTF-8；字形为 "glyph|idx"，自定义图为 fileid，
     *                        自动可为空串）
     */
    void    SendChangeAvatar(int facetype, const std::string& customfaceUtf8);

    /**
     *  发送"修改在线状态"请求（v2，cmd_change_presence=1107）。线程安全，可在 UI 线程
     *  调用：内部组帧后追加进发送缓冲、唤醒发送线程；并把 presence 记入
     *  m_nRecordOnlineStatus，供断线重连 / 重新上线时沿用。应答经 cmd_change_presence
     *  异步回投到 UI（FMG_MSG_V2_CHANGE_PRESENCE_RESULT）。
     *  仅在已连接、已登录时调用才有意义（离线态切换状态由上层走 GoOffline/GoOnline）。
     *  @param presence 目标在线状态（v2 presence_type，见 net/Protocol.h）
     */
    void    SendChangePresence(int presence);

    /**
     *  读取 / 写入"期望在线状态"（v2 presence_type）。本字段是客户端"我想以什么状态在线"
     *  的唯一真相源：每次实际登录（Login）会写入登录时的 presence；SendChangePresence 改状态
     *  时也会写入；断线重连 / 主动重新上线时由 CFlamingoClient::Login 读取它作为登录 presence，
     *  从而"本次会话沿用上次选择的状态"。进程重启后回到构造默认值（在线）。线程安全性：仅做
     *  单个 int 读写，调用方多在 UI 线程，足够。
     */
    int     GetRecordOnlineStatus() const { return m_nRecordOnlineStatus; }
    void    SetRecordOnlineStatus(int presence) { m_nRecordOnlineStatus = presence; }

    /**
     *  读取 / 写入"用户已主动离线"标志。置真后本类<u>拒绝一切自发的联网行为</u>：
     *    · EnqueueFrame 直接丢弃业务帧，不再往发送缓冲里堆（否则离线期间攒下的帧
     *      会在下次连上时被批量补发出去，对端会突然收到一堆过期消息）。
     *    · 发送线程在 Send() 里<u>不再自动 Connect</u> —— 那里原本有一句
     *      "IsClosed() && !Connect()"，只要发送缓冲里有数据就会自己把连接重建起来，
     *      是一条绕开上层登录态判断的隐藏重连路径，用户主动离线时必须堵上。
     *
     *  由 CFlamingoClient::GoOffline 置真、Login 置假。<u>不影响</u>登录帧本身 ——
     *  登录走 SendData 直发，不经过 EnqueueFrame（见 CIUSocket::Login）。
     *
     *  线程安全性：仅单个 bool 读写，写在 UI 线程、读在发送线程，足够。
     */
    bool    IsUserOffline() const { return m_bUserOffline; }
    void    SetUserOffline(bool offline) { m_bUserOffline = offline; }

    /**
     *  清空尚未发出的发送缓冲。用户主动离线时调用，丢弃排队中的业务帧。
     *
     *  <u>不能</u>把这件事塞进 Close()：Close() 会被发送线程在持有 m_mtSendBuf 的
     *  情况下调用（见 CIUSocket::Send 的错误分支），在里面再去锁同一把非递归互斥量
     *  会死锁。故单列一个方法，由上层在 UI 线程显式调用。
     */
    void    ClearPendingSendData();

    /**
     *  请求拉取通讯录（v2，cmd_get_contacts=1201）。线程安全。应答经
     *  cmd_get_contacts 异步回投到 UI（FMG_MSG_V2_CONTACTS）。一般在登录
     *  成功、主面板显示后调用一次。
     */
    void    RequestContacts();

    /**
     *  请求同步最近会话列表（v2，cmd_sync_conversations=1501）。线程安全。
     *  应答经 cmd_sync_conversations 异步回投到 UI（FMG_MSG_V2_CONVERSATIONS）。
     *  一般在登录成功、主面板显示后调用一次。
     */
    void    RequestConversations();

    /**
     *  上报会话已读位点（v2，cmd_mark_read=1506）。线程安全。用户查看某会话后
     *  调用，告诉服务端"本会话已读至 readmsgid"，服务端据此更新 t_conversation
     *  的 f_readmsgid，使重新登录时该会话未读数能正确清零。应答仅记日志、不回投
     *  UI（已读位点更新对界面无即时影响）。
     *  @param ctype     会话类型（1 单聊 2 群聊）
     *  @param cid       会话ID（单聊=对端userid，群聊=群id）
     *  @param readmsgid 已读到的最大消息msgid；<=0（会话尚无已知消息）时本方法
     *                   直接跳过，不发包（服务端对 readmsgid<=0 会判为非法请求）
     */
    void    ReportConversationRead(int ctype, int64_t cid, int64_t readmsgid);

    /**
     *  撤回一条已发出的消息（v2，cmd_recall_message=1508）。线程安全。
     *
     *  应答经 FMG_MSG_V2_RECALL_RESULT 回投 UI（失败时据 code 提示原因）；服务端撤回
     *  成功后还会向相关方（单聊双方 / 全体群成员，含撤回者本人）推送
     *  cmd_message_recalled_notify，客户端统一在收到<u>推送</u>时才把气泡改成提示行、
     *  更新会话列表与本地消息库 —— 这样本端、对端、多端看到的结果始终一致。
     *
     *  @param ctype 会话类型（1 单聊 2 群聊）
     *  @param cid   会话ID（单聊=对端userid，群聊=群id）
     *  @param msgid 被撤回消息的服务器msgid；与 cid 一样须 >0，否则本方法直接跳过
     *               不发包（服务端对 <=0 会判为非法请求）
     */
    void    RecallMessage(int ctype, int64_t cid, int64_t msgid);

    /**
     *  Pin / 取消 Pin 一条消息（v2，cmd_pin_message=1518）。线程安全。
     *
     *  Pin 是<u>会话内共享</u>的状态：单聊里对方也看得到，群里全体成员都看得到。
     *  应答经 FMG_MSG_V2_PIN_RESULT 回投 UI（只在失败时据 code 提示原因）；服务端落库
     *  成功后还会向相关方（单聊双方 / 全体群成员，含操作者本人）推送
     *  cmd_message_pinned_notify，客户端统一在收到<u>推送</u>时才改本地标记与界面，
     *  与撤回同口径 —— 这样本端与对端看到的结果必然一致。
     *
     *  @param ctype 会话类型（见 enum conversation_type，1 单聊 2 群聊）
     *  @param cid   会话ID（单聊=对端userid，群聊=群id）；须 >0，否则本方法直接跳过不发
     *  @param msgid 目标消息的服务器msgid；与 cid 一样须 >0，否则本方法直接跳过不发
     *               （典型：本端刚发出、服务器回执还没回填就点了 Pin）
     *  @param pin   true 钉住、false 取消
     */
    void    PinMessage(int ctype, int64_t cid, int64_t msgid, bool pin);

    /**
     *  拉取某会话当前被 Pin 的消息全集（v2，cmd_get_pinned_messages=1520）。线程安全。
     *
     *  应答经 FMG_MSG_V2_PINNED_MESSAGES 回投 UI，带完整消息体：UI 把本地消息库里缺的
     *  那些补齐，并与这一份对账。之所以要有这一步 —— 本地库里只有本机同步过的消息，
     *  同事钉了一条三个月前的老消息时本机很可能压根没有那一行，"消息记录"窗的 Pin
     *  分类就会漏掉它。发起时机是打开会话窗与打开消息记录窗，详见
     *  docs/消息Pin接入方案.md §4.3。
     *
     *  服务端单次最多返回最近 PIN_FETCH_LIMIT 条，应答的 total 带回真实总数 ——
     *  UI 据此判断是否被截断，进而决定本地对账的时间下界。
     *
     *  @param ctype 会话类型（1 单聊 2 群聊）
     *  @param cid   会话ID；须 >0，否则本方法直接跳过不发
     */
    void    GetPinnedMessages(int ctype, int64_t cid);

    /**
     *  上报文件收取状态（v2，cmd_file_transfer_ack=1517）。线程安全，无应答。
     *  由文件<u>接收方</u>在点「接收」、下载成功、点「拒绝」这三处各调一次；服务端
     *  校验本端确为该消息的收件人后落库，并转推给发送方，使其气泡上能显示对方收到
     *  什么程度了。发送方不在线时不推，等它下次拉历史时从库里带出，故不会丢。
     *  @param peerUserId 该文件消息的发送方userid（>0），即本会话的对端
     *  @param msgid      被收取的文件消息的服务器msgid；与 peerUserId 一样须 >0，
     *                    否则本方法直接跳过不发包（服务端会判为非法请求）
     *  @param state      收取状态，取值见 enum file_ack_state
     */
    void    ReportFileTransferAck(int64_t peerUserId, int64_t msgid, int state);

    /**
     *  上报「正在输入」状态（v2，cmd_typing_notify=1510）。线程安全，无应答。
     *
     *  这是一条<u>不可靠</u>的提示：服务端收到后直接转给对端、既不落库也不应答，
     *  对端不在线就丢弃。因此调用方不必关心结果，发送频率则须由调用方自行节流
     *  （见 BalloonChatWnd/TypingIndicator.h 的 TypingSendThrottle），不要每敲
     *  一个键就调一次。
     *
     *  @param peerUserId 对端userid（>0），即这条提示发给谁；非正时直接跳过不发包
     *  @param state      输入状态，取值见 enum typing_state：1 输入中、0 停止
     */
    void    ReportTyping(int64_t peerUserId, int state);

   	//同步接口
    //nTimeout单位是秒
    /**
     *  同步发起一次 v2 注册（cmd_register）。内部自行建连、发帧、等应答，
     *  与 Login 同为阻塞式调用，须在工作线程里调，不能在 UI 线程调。
     *
     *  @param params        注册参数；其中 password 传<u>明文</u>，本函数内部做
     *                       SHA-256 后再上网，明文不出本机。各字符串均须是
     *                       UTF-8 字节序列。
     *  @param nTimeout      收发超时，单位秒。
     *  @param strReturnData [出参] 服务端应答的 JSON 包体原文（已解压），
     *                       交由调用方用 RegisterProtocolClient 解析。
     *  @return 成功收到一个完整应答帧返回 true（<u>不代表注册成功</u>，注册
     *          结果看应答里的 code）；建连 / 发送 / 收取 / 解压失败返回 false。
     */
    bool    Register(const net::RegisterRequestParams& params, int nTimeout, std::string& strReturnData);
    bool    Login(const char* pszUser, const char* pszPassword, int nClientType, int nOnlineStatus, int nTimeout, std::string& strReturnData);

    /**
     *  取文件服务器地址 / 端口（只读快照）。供群文件并发传输池里每条独立连接
     *  （net::FileServerConn）建连用——它不复用本类那条共享的 m_hFileSocket，而是
     *  各自新建 socket 连到这同一个 fileserver。返回值是配置加载 / 网络设置写入后的
     *  当前值的拷贝，会话内稳定；按值返回避免跨线程读到正在改写的串。
     */
    std::string GetFileServerAddr() const { return m_strFileServer; }
    short       GetFilePort() const { return m_nFilePort; }

    //超时时间,单位为秒
	bool    SendOnFilePort(const char* pBuffer, int64_t nSize, int nTimeout = 30);
    bool	RecvOnFilePort(char* pBuffer, int64_t nSize, int nTimeout = 30);
    //bool    CheckRecvDataOnFilePort(int nTimeout = 3000);

    bool    SendOnImgPort(const char* pBuffer, int64_t nSize, int nTimeout = 30);
    bool	RecvOnImgPort(char* pBuffer, int64_t nSize, int nTimeout = 30);
    //bool    CheckRecvDataOnImgPort(int nTimeout = 3000);

private:   
    void    SendThreadProc();
    void    RecvThreadProc();

    void    SendHeartbeatPackage();

    bool	Send();
    bool	Recv();

    //发送nBuffSize长度的字节，如果发不出去，则就认为失败
    bool    SendData(const char* pBuffer, int nBuffSize, int nTimeout);
    //收取nBufferSize长度的字节，如果收不到，则认为失败
    bool    RecvData(char* pszBuff, int nBufferSize, int nTimeout);

    //解包
    bool    DecodePackages();

    //——以下为 v2 协议相关的内部方法——

    /**
     *  解析并保存 flamingo.ini 里读到的服务端公钥文本，由 LoadConfig 调用。
     *  拆成单独一个函数是为了让解析与配置读取分开，解析逻辑可单独测试。
     *
     *  @param pubKeyText serverpubkey 的原始值（base64）。空串表示未配置，
     *                    此时各链路走明文；非空但解析失败时记错误日志并同样
     *                    不置公钥——但那属于配置写错，各链路会连不上服务端
     *  副作用：改写 m_serverPubKey 与 m_bHasServerPubKey，并输出一条日志。
     */
    //握手收发要转接到哪条链路的收发函数上，见 HandshakeChannel。
    //本类持有三条各自独立的连接，它们的收发函数不同，但握手流程完全一样，
    //故用一个适配器加这个标识区分，而不是写三份几乎相同的适配器。
    enum handshake_link_kind
    {
        //主连接（chatserver），走 SendData / RecvData
        handshake_link_main = 0,
        //文件传输共享连接（fileserver），走 SendOnFilePort / RecvOnFilePort
        handshake_link_file = 1,
        //图片传输共享连接（imgserver），走 SendOnImgPort / RecvOnImgPort
        handshake_link_img  = 2,
    };

    //握手期间的字节收发适配器，把 net::IHandshakeChannel 转接到本类相应链路的
    //收发方法上。定义在 IUSocket.cpp 里——做成嵌套类是因为它要访问本类的私有
    //收发方法，放在匿名 namespace 里够不着。
    class HandshakeChannel;

    void    ApplyServerPublicKey(const std::string& pubKeyText);

    /**
     *  在刚建好的主连接上同步走完三帧协议加密握手。
     *
     *  <u>只能由 Connect() 在把 m_bConnected 置真之前调用</u>。这个时序是必须的：
     *  收取线程循环顶部判的是 IsClosed()，而它就是 !m_bConnected，所以标志置真
     *  之前收取线程一直在睡，本函数才能安全地同步收服务端那一帧应答；一旦挪到
     *  Connect() 外面（例如放进 Login），就会与收取线程抢同一份数据。
     *
     *  未配置服务端公钥时直接返回成功且不做任何收发，此时本连接走明文。
     *
     *  @param nTimeout 每一次收发的超时时间，单位秒
     *  @return 成功返回 true。失败表示对端不是我们认识的服务器、或握手被中途破坏，
     *          调用方<u>必须放弃这条连接</u>，不得退回明文继续用
     *  副作用：在 socket 上收发三帧；改写 m_handshake 的状态。
     */
    bool    PerformHandshake(int nTimeout);

    /**
     *  在刚建好的文件传输共享连接上同步走完三帧握手。
     *
     *  <u>只能由 ConnectToFileServer 在 m_bConnectedOnFileSocket 置真之后调用</u>，
     *  与主连接的顺序相反：SendOnFilePort / RecvOnFilePort 一进来就判
     *  IsFileServerClosed，标志没置真的话它们会直接拒绝收发。这样放没有风险，
     *  因为文件链路是纯同步收发，不存在会来抢数据的独立收取线程。
     *
     *  未配置服务端公钥时直接返回成功且不做任何收发，此时本连接走明文。
     *
     *  @param nTimeout 每次收发的超时时间，单位秒
     *  @return 成功返回 true；失败时调用方须关闭这条连接，不得退回明文继续用
     *  副作用：在文件连接上收发三帧；改写 m_fileHandshake 的状态。
     */
    bool    PerformFileHandshake(int nTimeout);

    /**
     *  在刚建好的图片传输共享连接上同步走完三帧握手。
     *  时序与注意事项同 PerformFileHandshake，只是作用在图片连接上。
     *
     *  @param nTimeout 每次收发的超时时间，单位秒
     *  @return 成功返回 true；失败时调用方须关闭这条连接，不得退回明文继续用
     *  副作用：在图片连接上收发三帧；改写 m_imgHandshake 的状态。
     */
    bool    PerformImgHandshake(int nTimeout);

    /**
     *  把一条消息编码成一个完整的 v2 帧。<u>主连接上发出的每一帧都必须经过这里</u>。
     *
     *  之所以强制走同一个出口，是因为压缩与（后续的）加密两件事的判断都只写在这里
     *  一处。别处再自行调 net::encodeFrame，就会出现同一条连接上明文帧与密文帧混发，
     *  服务端收到后会直接断开连接，而使用者看到的现象只是"登录偶尔失败"，很难往
     *  加密上想。
     *
     *  包体达到 COMPRESS_THRESHOLD 且压缩后确实更小时才采用压缩。登录 / 注册 /
     *  检查更新这几个同步请求的包体通常远小于阈值，与收敛之前逐字节一致；个别
     *  超过阈值的情形会被压缩，服务端的解压是在按 cmd 分发之前统一做的，不区分
     *  cmd，故行为等价。
     *
     *  @param cmd      消息类型，取值见 net/Protocol.h 的 enum msg_type
     *  @param seq      序列号，应答时由服务端回填
     *  @param body     包体明文（UTF-8 JSON 文本）
     *  @param frameOut [出参] 编码结果，整帧字节；函数内部会先清空它
     *  @return 成功返回 true；编码失败返回 false 并已记录日志
     */
    bool    BuildFrame(uint16_t cmd, uint32_t seq, const std::string& body,
                       std::string& frameOut);

    /**
     *  把一个收到的帧的线上包体还原成明文。<u>主连接上收到的每一帧都必须经过这里</u>，
     *  理由与 BuildFrame 相同。
     *
     *  @param header   该帧的帧头，其中 flags 与 originlen 决定如何还原
     *  @param rawBody  线上包体字节
     *  @param plainOut [出参] 还原出的明文包体；函数内部会先清空它
     *  @return 成功返回 true；还原失败返回 false 并已记录日志，
     *          调用方应视作连接数据已错乱并关闭连接
     */
    bool    RestoreFrameBody(const frame_header& header, const std::string& rawBody,
                             std::string& plainOut);

    //同步收取一个完整的 v2 帧，供登录这类同步请求使用。
    //@param header  [出参] 解析出的帧头
    //@param body    [出参] 包体<u>明文</u>，内部已调 RestoreFrameBody 还原
    //@param nTimeout 收取超时时间，单位秒
    //@return 成功返回 true；收取失败、帧头非法或包体还原失败返回 false
    bool    RecvOneFrame(frame_header& header, std::string& body, int nTimeout);

    //把一条消息编码成 v2 帧，追加进发送缓冲并唤醒发送线程。编码走 BuildFrame。
    //cmd 取值见 net/Protocol.h。
    void    EnqueueFrame(uint16_t cmd, uint32_t seq, const std::string& body);

    //分发一个收到的 v2 帧（在接收线程中调用）。本期只处理心跳/被踢/
    //设备变更通知，其余 cmd 仅记日志（对应功能尚未迁移到 v2）。
    void    HandleV2Frame(uint16_t cmd, uint32_t seq, const std::string& body);

    //取得本机设备唯一标识：首次调用生成 GUID 写入 flamingo.ini，
    //之后调用直接读回。返回 ASCII 字符串（保证 flamingo.ini 仍为 ASCII）。
    std::string GetOrCreateDeviceId();

    //取得本机设备名称（计算机名），以 UTF-8 编码返回，供 v2 登录请求使用。
    std::string GetDeviceName();

private:
	SOCKET							m_hSocket;				//一般用途Socket（非阻塞socket）
	SOCKET							m_hFileSocket;			//传文件的Socket（阻塞socket）
    SOCKET							m_hImgSocket;			//传聊天的Socket（阻塞socket）
	short							m_nPort;
	short							m_nFilePort;
    short							m_nImgPort;
	std::string						m_strServer;			//服务器地址
    std::string						m_strFileServer;		//文件服务器地址
    std::string						m_strImgServer;		    //图片服务器地址
	
	long							m_nProxyType;			//代理类型（0不使用代理；）
    std::string						m_strProxyServer;		//代理服务器地址
	short							m_nProxyPort;			//代理服务器端口号

    long                            m_nLastDataTime;        //最近一次收发数据的时间
    std::mutex                      m_mutexLastDataTime;    //保护m_nLastDataTime的互斥体
    long                            m_nHeartbeatInterval;   //心跳包时间间隔，单位秒,默认为10秒
    int32_t                         m_nHeartbeatSeq;        //心跳包序列号
	
	bool							m_bConnected;
	bool							m_bConnectedOnFileSocket;
    bool                            m_bConnectedOnImgSocket;

    std::unique_ptr<std::thread>    m_spSendThread;
    std::unique_ptr<std::thread>    m_spRecvThread;

    std::string                     m_strSendBuf;
    std::string                     m_strRecvBuf;

    std::mutex                      m_mtSendBuf;

    std::condition_variable         m_cvSendBuf;
    std::condition_variable         m_cvRecvBuf;

    bool                            m_bStop;

    bool                            m_bEnableReconnect;     //聊天服务器断线是否重连

    //服务端身份公钥（Ed25519），取自 flamingo.ini 的 serverpubkey，进程启动时读一次。
    //m_bHasServerPubKey 为假表示没配，此时各条链路一律走明文——客户端<u>没有</u>独立的
    //加密开关，"配了公钥"本身就是"要加密"，这样与服务端 .conf 里的 encryption 天然配对。
    //生命周期同本单例；仅 LoadConfig 写入，之后只读，故各链路线程可无锁读取。
    unsigned char                   m_serverPubKey[net::IDENTITY_PUBLICKEY_BYTES];
    bool                            m_bHasServerPubKey;

    //主连接的协议加密状态。每次 Connect 真正新建 socket 之后就地走完三帧握手，
    //握手成功前 m_handshake.cipher().ready() 为假，收发自然是明文——握手帧本身
    //正是靠这一点走明文的。Close 时调 reset()，因为重连是一条全新的连接、
    //必须重新协商密钥，沿用旧密钥会让每一帧都解密失败。
    //做成值成员而不是指针，是为了不给发送线程与收取线程留下对象被销毁的窗口，
    //详见 net::ClientHandshake::reset 的注释。生命周期同本单例。
    net::ClientHandshake            m_handshake;

    //文件传输共享连接（m_hFileSocket）的协议加密状态。与主连接各持一个：两条是
    //互相独立的连接，各有各的会话密钥。这条连接是一笔传输一建一断，故在
    //CloseFileServerConnection 里调 reset()，否则下一笔会拿着上一条连接的密钥去解帧。
    net::ClientHandshake            m_fileHandshake;

    //图片传输共享连接（m_hImgSocket）的协议加密状态。与前两条各持一个，理由同上。
    //同样是一张图一建一断，故在 CloseImgServerConnection 里调 reset()。
    net::ClientHandshake            m_imgHandshake;

    std::string                     m_strRecordUser;
    std::string                     m_strRecordPassword;
    int                             m_nRecordClientType;
    int                             m_nRecordOnlineStatus;
    //用户已主动离线（托盘 / 状态菜单选了"离线"）。置真期间本类拒绝一切自发联网：
    //EnqueueFrame 丢弃业务帧、发送线程不再自动 Connect。GoOffline 置真、Login 置假。
    //生命周期同本单例；进程重启后回到构造默认值 false。
    bool                            m_bUserOffline;
    //v2 帧序号分配器。UI 线程与各传输线程（图片 / 文件 / 群文件并发池）都会 ++ 它
    //来取下一个 seq，故必须原子自增，否则并发分配会撞号、导致按 seq 路由的应答串槽。
    std::atomic<int>                m_seq;

    //1801/1802 信令归属表：发请求时按 seq 记下属于图片还是文件传输，收应答时按 seq
    //取回归属、转交给对应管理器（见 HandleV2Frame）。m_transferOwnerMtx 保护其访问。
    net::TransferOwnerRegistry      m_transferOwners;
    std::mutex                      m_transferOwnerMtx;

    //UI 代理窗口句柄。网络线程把收到的消息 PostMessage 给它，由 ProxyWndProc
    //转到具体窗口。由 CFlamingoClient 创建代理窗口后经 SetUIProxyWnd 登记，
    //整个进程生命周期内只设一次；未设时为 NULL，投递会被静默跳过。
    HWND                            m_hProxyWnd;

    //"网络已断"去重标志：置真后 NotifyNetError 不再重复投递 FMG_MSG_NET_ERROR，
    //由每次登录成功时的 ResetNetError 复位。收发两个方向可能同时发现断线，
    //没有这个标志会重复触发自动重连。
    bool                            m_bNetError;
};