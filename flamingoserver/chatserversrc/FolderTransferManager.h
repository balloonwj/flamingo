/**
 *  单聊文件夹传输会话管理器，FolderTransferManager.h
 *
 *  chatserver 端持有文件夹传输域的运行配置（relay 令牌密钥、fileserver 中转对外
 *  地址、数据通道条数、规模与并发上限、邀请超时与失败保留期），并维护"活跃文件夹
 *  传输会话表"。一次文件夹传输从发送端发起邀请开始，由一个 sessionid 贯穿全程：
 *  邀请时建会话(createSession)，接收端应答 / 任一端取消 / 传输结束时按 sessionid
 *  查会话以定位对端、校验参与方、签发 relay 令牌，会话终结时清除。
 *
 *  与远程桌面（RemoteDesktopManager）、音视频通话（CallManager）相比，本类多出
 *  三件事：
 *    1. 规模校验：文件数与总字节数各有服务端配置的上限，超限直接拒绝邀请，应答里
 *       回带上限值供客户端提示（见 checkScale / getMaxFiles / getMaxSize）。
 *    2. 并发限制：每个用户同时可参与的传输笔数有上限（配置项 foldermaxperuser，
 *       默认 5 笔，作发送方与作接收方各算一笔）；整台服务器的活跃会话总数另有上限
 *       （见 getUserSessionCount / isServerBusy / checkInvite）。
 *    3. 失败会话保留期：传输失败后会话不立即销毁，保留 folderretrywindow 毫秒供
 *       发送端点「重试」断点续传，期满才彻底销毁（见 canRetry / tick）。
 *    4. 尝试序号：一次会话可能经历多次尝试（首次 + 若干次重试），会话表为此记一个
 *       从 1 起算的序号（见 beginRetryAttempt）。聊天记录里的传输记录条以「会话 id +
 *       尝试序号」为幂等键，因而"每次尝试各留一条记录"—— 先失败后重试成功的一笔传输，
 *       双方聊天记录里能看到失败与完成两条，过程完整。
 *
 *  接收端同意后给双方各签一枚 RelayToken（同 sessionid、相反 role：
 *  role_folder_sender / role_folder_receiver，uid 与 peeruid 对调），双方据此各建
 *  1 条控制通道 + N 条数据通道连到 fileserver 中转，按字节双向转发文件内容与回执
 *  （中转层见 fileserversrc 的中转配对模块，本类不参与中转数据）。
 *
 *  全局单例，经 Singleton<FolderTransferManager> 访问；会话表受内部互斥量保护，
 *  可被多个 EventLoop 工作线程并发访问。本类为纯逻辑（不触网、不查库、不解析
 *  JSON），且所有时间一律由调用方以参数传入、内部不取系统时间，便于单元测试，
 *  见 tests/test_foldertransfermanager.cpp。
 *
 *  balloonwj@qq.com   2026-08-13
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <map>
#include <mutex>
#include <vector>

/**
 *  一次文件夹传输的目录名与规模统计。发送端扫描本地目录后随 cmd_folder_invite
 *  上报，服务端据此做规模上限校验，并原样带进 cmd_folder_invite_notify 推给接收端
 *  （接收端要在气泡上显示"共 N 个文件、合计 X 字节"）。
 */
struct FolderTransferScale
{
    std::string folderName;   //根目录名（只有名字，不含所在路径），如 "项目资料"
    int64_t     totalDirs;    //目录数，单位：个；含空目录，不含根目录本身
    int64_t     totalFiles;   //文件数，单位：个
    int64_t     totalSize;    //全部文件的字节数之和，单位：字节

    FolderTransferScale()
        : totalDirs(0), totalFiles(0), totalSize(0)
    {
    }
};

//一条活跃文件夹传输会话，对应一次"发送端 → 接收端"的整目录传输
struct FolderTransferSessionInfo
{
    std::string sessionid;   //会话 id（UUID 风格，只含数字与连字符，不含令牌分隔符 '.' / '|'）
    int64_t     senderId;    //发送端 userid（选中目录、发起邀请的一方）
    int64_t     receiverId;  //接收端 userid（被邀请、同意后收文件的一方）
    std::string folderName;  //根目录名，随邀请推送给接收端并显示在双方气泡上
    int64_t     totalDirs;   //目录数，单位：个
    int64_t     totalFiles;  //文件数，单位：个
    int64_t     totalSize;   //总字节数，单位：字节
    int         state;       //会话状态，取值见 Protocol.h 的 enum folder_transfer_state
    int64_t     createTime;  //会话创建（即发出邀请）时间，Unix 毫秒；邀请超时以它为基准
    int64_t     stateTime;   //最近一次状态变更时间，Unix 毫秒；失败会话的保留期以它为基准
    int         attemptSeq;  //本次会话的尝试序号，单位：次；建会话时为 1，此后每受理一次
                             //cmd_folder_retry（校验通过、真正要重新签发令牌）加一。
                             //聊天记录里的传输记录条以「会话 id + 本序号」为幂等键，故它
                             //决定了"每次尝试各留一条记录"：先失败后重试成功的一笔传输，
                             //会留下 result=7 与 result=8 两条记录。生命周期与会话相同

    FolderTransferSessionInfo()
        : senderId(0), receiverId(0), totalDirs(0), totalFiles(0), totalSize(0)
        , state(0), createTime(0), stateTime(0), attemptSeq(0)
    {
    }
};

//单聊文件夹传输会话管理器；全局单例，经 Singleton<FolderTransferManager> 访问
class FolderTransferManager final
{
public:
    FolderTransferManager();
    ~FolderTransferManager();

    FolderTransferManager(const FolderTransferManager& rhs) = delete;
    FolderTransferManager& operator=(const FolderTransferManager& rhs) = delete;

    //relay 令牌有效期（毫秒）：仅覆盖"拿令牌去连 fileserver 建立中转"这一步，连上后
    //中转层不再校验令牌；5 分钟足够覆盖"下发中转参数 → 双方建满 N+1 条通道"的间隔。
    //取值与远程桌面、音视频通话一致（见 CallManager::kRelayTokenTtlMs）
    static const int64_t kRelayTokenTtlMs = 5 * 60 * 1000;

    //数据通道条数的硬上限（条，不含 lane 0 控制通道）：一次传输在服务端要占
    //2 × (N + 1) 条连接，Windows 的 select 有连接数硬约束，故封死在 8。
    //出处：docs/单聊文件夹传输方案.md 第 4.2 节
    static const int kMaxDataLanes = 8;
    //数据通道条数的默认值（条）：2~4 条时多通道的提速收益最明显，再往上收益迅速
    //变平、且接收端并行写多个文件在机械硬盘上会退化成随机写。
    //出处：docs/单聊文件夹传输方案.md 第 4.2 节
    static const int kDefaultDataLanes = 4;

    //单次文件夹传输的文件数上限默认值（个），对应配置项 foldermaxfiles
    static const int64_t kDefaultMaxFiles = 10000;
    //单次文件夹传输的总字节数上限默认值（字节）= 50 GiB，对应配置项 foldermaxsize
    static const int64_t kDefaultMaxSize = 50LL * 1024 * 1024 * 1024;
    //整台服务器同时进行的文件夹传输会话数上限默认值（个），对应配置项 foldermaxsessions。
    //取 Windows 的建议值 24（较保守的那一侧）；Linux 部署经配置项调大到 128
    static const int kDefaultMaxSessions = 24;
    //单个用户同时可参与的文件夹传输会话数上限默认值（笔），对应配置项 foldermaxperuser。
    //作发送方与作接收方各算一笔，失败待重试的会话在保留期内仍占名额。
    //取 5 的依据：一个人常见的并行需求是同时给几位同事各发一份，5 笔在服务端占用
    //5 × 2 × (数据通道数 + 1) 条连接（默认 4 条数据通道时为 50 条），仍远小于整台
    //服务器的会话总数闸门 kDefaultMaxSessions 所对应的连接量。
    //出处：docs/单聊文件夹传输方案.md 第 2 节
    static const int kDefaultMaxSessionsPerUser = 5;

    //邀请无应答的超时默认值（毫秒）= 30 分钟，对应配置项 folderinvitetimeout。
    //取 30 分钟而不是更短：接收方常常只是暂时离开座位，超时过短会让邀请在对方回到
    //座位前就被作废、发送方不得不反复重发。发送方随时可以主动撤回（cmd_folder_cancel），
    //不必等超时，故把超时放长的代价很小。
    //出处：docs/单聊文件夹传输方案.md 第 2 节
    static const int64_t kDefaultInviteTimeoutMs = 30 * 60 * 1000;
    //失败会话的保留默认值（毫秒）= 10 分钟，对应配置项 folderretrywindow；
    //期内发送端可点「重试」断点续传，期满会话销毁、只能重新发起
    static const int64_t kDefaultRetryWindowMs = 10 * 60 * 1000;

    //新建会话时的尝试序号（次）。从 1 而不是 0 起算，是为了让聊天记录里的传输记录条
    //在人读日志与包体时能直接对应"第几次尝试"，不必在心里加一
    static const int kFirstAttemptSeq = 1;

    /**
     *  配置文件夹传输域运行参数；main.cpp 读取 etc/chatserver.conf 后调用一次。
     *  约定只在服务启动阶段调用（此后各 get 方法只读不加锁），不要在运行期改配置。
     *
     *  @param relayTokenKey   relay 令牌 HMAC 密钥，与 fileserver 共享（relaytokenkey 配置项）
     *  @param relayHost       fileserver 中转通道对客户端公布的主机地址
     *  @param relayPort       fileserver 中转通道端口
     *  @param dataLanes       数据通道条数（folderlanes），有效范围 1~kMaxDataLanes；
     *                         大于上限按 kMaxDataLanes 截断，小于 1 按 kDefaultDataLanes
     *                         取默认值，两种越界都会记一条日志
     *  @param maxFiles        文件数上限（foldermaxfiles），单位：个；小于 1 时取 kDefaultMaxFiles
     *  @param maxSize         总字节数上限（foldermaxsize），单位：字节；小于 1 时取 kDefaultMaxSize
     *  @param maxSessions     整台服务器的并发会话数上限（foldermaxsessions），单位：个；
     *                         小于 1 时取 kDefaultMaxSessions
     *  @param maxSessionsPerUser 单个用户同时可参与的会话数上限（foldermaxperuser），单位：笔；
     *                         小于 1 时取 kDefaultMaxSessionsPerUser
     *  @param inviteTimeoutMs 邀请无应答的超时（folderinvitetimeout），单位：毫秒；
     *                         小于 1 时取 kDefaultInviteTimeoutMs
     *  @param retryWindowMs   失败会话的保留期（folderretrywindow），单位：毫秒；
     *                         小于 1 时取 kDefaultRetryWindowMs
     */
    void configure(const std::string& relayTokenKey,
                   const std::string& relayHost, int relayPort,
                   int dataLanes,
                   int64_t maxFiles, int64_t maxSize,
                   int maxSessions, int maxSessionsPerUser,
                   int64_t inviteTimeoutMs, int64_t retryWindowMs);

    //取 fileserver 中转对外主机地址，供构造 cmd_folder_relay_info
    const std::string& getRelayHost() const;
    //取 fileserver 中转对外端口，供构造 cmd_folder_relay_info
    int getRelayPort() const;
    //取数据通道条数（条，不含控制通道），随 cmd_folder_relay_info 下发给双方；
    //已在 configure 里截断到 1~kMaxDataLanes，调用方直接用即可
    int getDataLanes() const;
    //取文件数上限（个），超限应答里回带本值供客户端提示
    int64_t getMaxFiles() const;
    //取总字节数上限（字节），超限应答里回带本值供客户端提示
    int64_t getMaxSize() const;
    //取整台服务器的并发会话数上限（个）
    int getMaxSessions() const;
    //取单个用户同时可参与的会话数上限（笔）
    int getMaxSessionsPerUser() const;
    //取邀请无应答的超时（毫秒）
    int64_t getInviteTimeoutMs() const;
    //取失败会话的保留期（毫秒）
    int64_t getRetryWindowMs() const;

    /**
     *  校验一次文件夹传输的规模是否在服务端上限之内。规模上限由服务端权威判定，
     *  客户端不硬编码，被拒时按应答回带的 maxfiles / maxsize 提示用户。
     *  @param totalFiles 本次要传的文件数，单位：个
     *  @param totalSize  本次要传的总字节数，单位：字节
     *  @return 0 表示通过；error_code_folder_too_many_files(609) 文件数超限；
     *          error_code_folder_too_large(610) 总字节数超限。文件数与总大小同时
     *          超限时先报文件数
     */
    int checkScale(int64_t totalFiles, int64_t totalSize) const;

    /**
     *  统计某用户当前参与着几笔活跃的文件夹传输（作发送端或接收端都各算一笔）。
     *  并发限制据此判定：达到 foldermaxperuser 即拒绝其新的邀请 / 应答。
     *  失败待重试的会话在保留期内仍占着名额（重试要接着往同一个目录续传），故计入。
     *  @param userId 待查询用户 userid
     *  @return 该用户参与的活跃会话数，单位：笔；一笔也没有时返回 0
     */
    int getUserSessionCount(int64_t userId);

    /**
     *  按 userid 反查该用户当前参与的<u>全部</u>活跃会话（作发送端或接收端都算），逐条
     *  拷贝出整条会话信息。与 getUserSessionCount 共用同一处遍历，两者的命中口径完全
     *  一致：进行中的会话与失败待重试的会话都算数（后者在保留期内仍占着该用户的名额）。
     *
     *  用途是连接断开 / 退出登录时定位待收尾的会话。发送端与"已同意的接收端"都会把
     *  会话 id 记在自己那条连接上，可直接按 id 收尾；唯独<u>尚未应答邀请的接收端</u>
     *  手里没有会话 id（会话是发送端那条连接建的），只能靠 userid 反查 —— 没有本方法
     *  时，接收端在响铃阶段掉线，发送端要一直等到邀请超时才收到通知。
     *
     *  必须查出全部而不是其中一笔：一个用户可同时参与至多 foldermaxperuser 笔传输，
     *  只收尾其中一笔的话，其余几笔的对端要一直干等到超时才知道这一方已经离线。
     *
     *  线程安全前提与本类其余方法一致：内部对会话表加锁，可在任意 EventLoop 工作
     *  线程调用；但不得在已持有本类 m_mutex 时调用（该锁不可重入）。
     *
     *  @param userId 待查询用户 userid
     *  @param out    [出参] 把命中的会话按 sessionid 字典序<u>追加</u>进去；本方法不预先
     *                清空它，是否清空由调用方决定。一笔也没查到时不改动
     *  @return 本次追加进 out 的会话数，单位：笔；一笔也没查到时返回 0
     */
    int findSessionsByUser(int64_t userId, std::vector<FolderTransferSessionInfo>& out);

    /**
     *  查询整台服务器的活跃会话数是否已达 foldermaxsessions 上限。
     *  @return 已达上限返回 true（此时新邀请应回 error_code_folder_server_busy），否则 false
     */
    bool isServerBusy();

    //取当前活跃会话数（个），供监控命令与单元测试观察会话表规模
    size_t getActiveSessionCount();

    /**
     *  邀请的全部准入校验，集中在本方法内，调用方按返回码回话即可，不必自己逐项判，
     *  避免各处口径不一致。校验顺序与失败码（见 enum error_code）：
     *    1. 规模超限                        -> 609 / 610（见 checkScale）
     *    2. 发送方或接收方的名额已满         -> error_code_folder_busy(620)
     *       （该用户参与的会话数已达 foldermaxperuser，先判发送方再判接收方）
     *    3. 服务器活跃会话总数已达上限       -> error_code_folder_server_busy(619)
     *  先报"某一方名额已满"再报"服务器繁忙"：两道闸门同时到顶时报前者，因为它对用户
     *  更有指导性（等自己那几笔里有一笔传完即可），后者只能干等。
     *
     *  本方法只做判定，不建会话；通过后调用方再调 createSession。判定与建会话之间
     *  没有加锁，理论上存在并发抢入的窗口，实际邀请频率极低，且抢入的后果只是多跑
     *  一笔传输，不做加锁合并。
     *
     *  @param senderId   发起邀请的用户 userid
     *  @param receiverId 被邀请的用户 userid
     *  @param totalFiles 本次要传的文件数，单位：个
     *  @param totalSize  本次要传的总字节数，单位：字节
     *  @return 0 表示允许发起；非 0 为上述失败码
     */
    int checkInvite(int64_t senderId, int64_t receiverId, int64_t totalFiles, int64_t totalSize);

    /**
     *  新建一次文件夹传输会话：生成 sessionid、以 folder_state_ringing 登记会话表。
     *  调用方须先用 checkInvite 判定通过。
     *  @param senderId   发送端 userid（发起方）
     *  @param receiverId 接收端 userid（被邀请方）
     *  @param scale      目录名与规模统计，原样记进会话供后续推送使用
     *  @param now        当前时间，Unix 毫秒（同时作 createTime 与 stateTime）
     *  @return 新会话的 sessionid（只含数字与连字符，不含 '.' / '|'，可直接放进 relay 令牌）
     */
    std::string createSession(int64_t senderId, int64_t receiverId,
                              const FolderTransferScale& scale, int64_t now);

    /**
     *  按 sessionid 查会话（拷贝出）。
     *  @param sessionid 会话 id
     *  @param out       [出参] 命中时回填
     *  @return 命中返回 true，否则 false
     */
    bool getSession(const std::string& sessionid, FolderTransferSessionInfo& out);

    /**
     *  更新会话状态，同时把 stateTime 刷成 now。失败会话的保留期以 stateTime 为
     *  基准计时，故置为 folder_state_failed 时务必传入准确的当前时间。
     *  @param sessionid 会话 id
     *  @param state     新状态，取值见 Protocol.h 的 enum folder_transfer_state
     *  @param now       当前时间，Unix 毫秒
     *  @return 会话存在并更新返回 true，否则 false
     */
    bool setState(const std::string& sessionid, int state, int64_t now);

    /**
     *  受理一次重试：把会话置回 folder_state_transferring 并把尝试序号加一，同时把
     *  stateTime 刷成 now。调用方须先用 canRetry 判定通过。
     *
     *  与直接调 setState(sessionid, folder_state_transferring, now) 的区别只有"序号加一"
     *  这一件事，但它必须与状态变更在同一个临界区内完成 —— 序号是聊天记录里那条传输记录条
     *  的幂等键的一部分，序号与状态若分两次改，中间恰好有一帧 cmd_folder_close 挤进来时，
     *  这一次尝试的记录会挂到上一次的序号上，从而被上一条记录去重掉、整条丢失。
     *
     *  调用时机是"校验通过、真正要重新签发令牌"之前。此后即便令牌签发失败、这一次尝试
     *  当场以失败告终，它也确实是一次独立的尝试，理应留下自己那条记录。
     *
     *  @param sessionid      会话 id
     *  @param now            当前时间，Unix 毫秒
     *  @param outAttemptSeq  [出参] 加一之后的尝试序号（第二次尝试即为 2）；
     *                        返回 false 时置 0
     *  @return 会话存在并更新返回 true，否则 false
     */
    bool beginRetryAttempt(const std::string& sessionid, int64_t now, int& outAttemptSeq);

    /**
     *  删除会话（拒绝 / 取消 / 对方离线 / 传输完成时调用）。
     *  注意传输失败时<u>不要</u>立即删：失败会话要保留 folderretrywindow 供重试续传，
     *  改用 setState(sessionid, folder_state_failed, now)，到期由 tick 销毁。
     *  @param sessionid 会话 id
     *  @return 删除了一条返回 true；本无该会话返回 false
     */
    bool removeSession(const std::string& sessionid);

    /**
     *  给会话的某一角色签发 relay 令牌。role==RelayToken::role_folder_sender 时
     *  uid=发送端、peeruid=接收端；role==role_folder_receiver 时两者对调。
     *  每端只签一枚，客户端拿它建 1 条控制通道 + N 条数据通道，各连接在中转握手帧
     *  里自报 lane 序号，lane 不写进令牌。
     *  @param sessionid 会话 id
     *  @param role      角色，取值 RelayToken::role_folder_sender / role_folder_receiver
     *  @param now       当前时间，Unix 毫秒（用于计算过期 = now + kRelayTokenTtlMs）
     *  @param outToken  [出参] 命中时回填签发出的令牌
     *  @return 会话存在并签发返回 true，否则 false
     */
    bool signTokenFor(const std::string& sessionid, int role, int64_t now, std::string& outToken);

    /**
     *  周期性清理入口，供 chatserver 的定时器调用。一次扫描处理两类会话，都从会话表
     *  移除，并把移除前的会话信息（含双方 userid）交给调用方，由调用方决定通知谁 ——
     *  本类不触网，不在这里发推送。
     *
     *    (1) 邀请超时：仍处于 folder_state_ringing 且 now - createTime >= 邀请超时。
     *        回填到 outTimedOut 时状态已改写为 folder_state_timeout，调用方直接照它
     *        向发送端（以及仍在响铃的接收端）推 cmd_folder_state_notify 即可。
     *    (2) 保留期届满：处于 folder_state_failed 且 now - stateTime >= 保留期。
     *        回填到 outExpired，此后重试一律回 error_code_folder_session_expired(618)。
     *        这类不必再推送（客户端的重试按钮到点自行置灰），列出来是供调用方清理
     *        自己那侧的关联状态与记录日志。
     *
     *  两个边界都取"大于等于"：正好到点即清理。
     *
     *  @param now         当前时间，Unix 毫秒
     *  @param outTimedOut [出参] 追加邀请超时被移除的会话；调用前是否清空由调用方决定
     *  @param outExpired  [出参] 追加保留期届满被销毁的会话；调用前是否清空由调用方决定
     */
    void tick(int64_t now,
              std::vector<FolderTransferSessionInfo>& outTimedOut,
              std::vector<FolderTransferSessionInfo>& outExpired);

    /**
     *  判定一次重试（cmd_folder_retry）请求是否可以受理。允许的条件是：会话仍在、
     *  请求者是本会话的发送方、会话处于失败态、且失败至今未超出保留期。
     *
     *  校验顺序与失败码：
     *    1. 会话不存在（已过保留期被销毁，或 sessionid 不对）
     *                                   -> error_code_folder_session_expired(618)
     *    2. 请求者不是本会话的发送方     -> error_code_permission_denied(6)
     *       （接收方也走本码：重试入口只在发送方的失败气泡上）
     *    3. 会话不处于 folder_state_failed 态（还在响铃 / 传输中 / 已完成）
     *                                   -> error_code_bad_request(3)
     *       （正常客户端只在失败气泡上给出重试入口，出现即为非法请求）
     *    4. 失败至今已达保留期           -> error_code_folder_session_expired(618)
     *  先判参与方再判状态，避免向非参与方泄露会话当前状态。
     *
     *  @param sessionid  会话 id
     *  @param uid        发起重试的用户 userid
     *  @param now        当前时间，Unix 毫秒
     *  @param outErrCode [出参] 返回 false 时回填上述失败码；返回 true 时置 0
     *  @return 允许重试返回 true，否则 false
     */
    bool canRetry(const std::string& sessionid, int64_t uid, int64_t now, int& outErrCode);

private:
    /**
     *  在会话表中收集某用户参与的全部活跃会话（作发送端或接收端都算）。本类"按用户
     *  查会话"的唯一一处遍历：计数（getUserSessionCount 与 checkInvite 的名额判定）
     *  与"查全部"（findSessionsByUser）都经由它，避免同一份遍历条件写两遍后口径分叉。
     *  同时也是并发限制与掉线收尾口径一致的保证 —— 凡是占名额的会话，掉线时必被收尾。
     *
     *  调用者须已持有 m_mutex（该锁不可重入）。
     *
     *  @param userId 待查询用户 userid
     *  @param out    [出参] 非空时把命中的会话逐条追加进去（容器由调用方持有，本方法
     *                只写不保存指针）；只需要笔数时传 NULL，此时不构造任何拷贝
     *  @return 命中的会话数，单位：笔
     */
    int collectUserSessionsLocked(int64_t userId,
                                  std::vector<FolderTransferSessionInfo>* out) const;

private:
    mutable std::mutex                                  m_mutex;          //保护会话表；配置项只在启动时写一次，不受它保护
    std::map<std::string, FolderTransferSessionInfo>    m_sessions;       //sessionid → 会话
    uint64_t                                            m_seqCounter;     //会话序号自增器，与时间 / 双方 id 拼成唯一 sessionid
    std::string                                         m_relayTokenKey;  //relay 令牌 HMAC 密钥，与 fileserver 共享
    std::string                                         m_relayHost;      //fileserver 中转对外主机地址
    int                                                 m_relayPort;      //fileserver 中转端口
    int                                                 m_dataLanes;      //数据通道条数（条，不含控制通道），已截断到 1~kMaxDataLanes
    int64_t                                             m_maxFiles;       //单次传输的文件数上限，单位：个
    int64_t                                             m_maxSize;        //单次传输的总字节数上限，单位：字节
    int                                                 m_maxSessions;    //整台服务器的并发会话数上限，单位：个
    int                                                 m_maxSessionsPerUser;//单个用户同时可参与的会话数上限，单位：笔
    int64_t                                             m_inviteTimeoutMs;//邀请无应答的超时，单位：毫秒
    int64_t                                             m_retryWindowMs;  //失败会话的保留期，单位：毫秒
};
