/**
 *  单聊文件夹传输会话管理单元测试（Suite FTM），test_foldertransfermanager.cpp
 *
 *  覆盖 FolderTransferManager 的会话表增删查改、按角色签发 relay 令牌（用
 *  RelayToken 校验角色 / uid / peeruid / sessionid 正确）、sessionid 不含令牌
 *  分隔符，以及相比远程桌面与音视频通话新增的四项：规模上限校验、并发限制（每个
 *  用户至多 foldermaxperuser 笔、整台服务器至多 foldermaxsessions 笔）、邀请超时
 *  与失败会话保留期的清理、重试请求的准入判定。
 *
 *  另覆盖按 userid 反查会话（findSessionsByUser）：连接断开 / 退出登录时，尚未
 *  应答邀请的接收端手里没有 sessionid，只能靠它定位待收尾的会话；一个用户可同时
 *  参与多笔传输，故它必须查出全部而不是其中一笔。
 *
 *  以及尝试序号（beginRetryAttempt）：新建会话为 1、每受理一次重试加一、非重试路径
 *  不改它。该序号是聊天记录里传输记录条的幂等键的一部分，决定了"每次尝试各留一条记录"。
 *
 *  时间一律由用例以参数传入，模块内部不取系统时间，故超时与保留期的边界（差 1
 *  毫秒 / 正好到点）都能精确断言。
 *
 *  FolderTransferManager 为 Singleton，测试间共享同一个实例，故各用例用独立
 *  userid、在末尾清理自建会话，断言以 sessionid 精确定位而非依赖全表规模；需要
 *  特殊配置的用例（并发上限、规模上限）用完把配置恢复成宽松值。
 *
 *  balloonwj@qq.com   2026-08-13
 */
#include "doctest.h"
#include "../chatserversrc/FolderTransferManager.h"
#include "../chatserversrc/Protocol.h"      // enum folder_transfer_state / enum error_code
#include "../chatserversrc/FolderTransferProtocol.h"   // enum folder_transfer_kind（单文件直传会话）
#include "../utils/RelayToken.h"
#include "../base/Singleton.h"

#include <atomic>
#include <string>
#include <thread>

namespace {

//测试统一使用的 relay 令牌 HMAC 密钥
const char* const kTestRelayKey = "folder-relay-test-key";
//测试统一使用的 fileserver 中转对外地址
const char* const kTestRelayHost = "192.168.1.200";
//测试统一使用的 fileserver 中转对外端口
const int kTestRelayPort = 20004;
//测试统一使用的数据通道条数（条）
const int kTestLanes = 4;
//宽松的文件数上限（个）：大到任何用例都不会因规模被拦
const int64_t kLooseMaxFiles = 1000000;
//宽松的总字节数上限（字节）：同上
const int64_t kLooseMaxSize = 1024LL * 1024 * 1024 * 1024;
//宽松的服务器会话数上限（个）：大到任何用例都不会因服务器总数被拦
const int kLooseMaxSessions = 100000;
//宽松的每用户会话数上限（笔）：大到任何用例都不会因每用户名额被拦
const int kLooseMaxPerUser = 100000;
//测试统一使用的邀请超时（毫秒）= 30 分钟，与配置项建议默认值一致
const int64_t kTestInviteTimeoutMs = 30 * 60 * 1000;
//测试统一使用的失败会话保留期（毫秒）= 10 分钟，与配置项建议默认值一致
const int64_t kTestRetryWindowMs = 10 * 60 * 1000;

//把单例恢复成"各项上限都足够宽松"的配置，供各用例开头与收尾调用，
//避免上一个用例故意调小的上限影响到下一个
void configureLoose(FolderTransferManager& mgr)
{
    mgr.configure(kTestRelayKey, kTestRelayHost, kTestRelayPort, kTestLanes,
                  kLooseMaxFiles, kLooseMaxSize, kLooseMaxSessions, kLooseMaxPerUser,
                  kTestInviteTimeoutMs, kTestRetryWindowMs);
}

//造一份规模统计，只有文件数与总字节数在用例里有意义时才逐项指定
FolderTransferScale makeScale(int64_t files, int64_t size)
{
    FolderTransferScale scale;
    scale.folderName = "project-docs";
    scale.totalDirs  = 2;
    scale.totalFiles = files;
    scale.totalSize  = size;
    return scale;
}

//判断一批反查结果里有没有指定的那条会话。反查结果的条数与顺序在多笔并存时才有意义，
//单独判"某一笔在不在里面"能让断言失败时的信息更直接
bool containsSession(const std::vector<FolderTransferSessionInfo>& sessions,
                     const std::string& sessionid)
{
    for (size_t i = 0; i < sessions.size(); ++i)
    {
        if (sessions[i].sessionid == sessionid)
            return true;
    }

    return false;
}

//FTM19 用：一轮并发终结的上下文。一批线程同时对同一个会话的同一次尝试调 finishAttempt，
//由 m_winners 统计有几个拿到 true —— 正确的实现下每一轮都必须恰好是 1 个
struct ConcurrentFinishContext
{
    FolderTransferManager* m_mgr;        //被测单例的地址（上下文不持有它的所有权）
    std::string            m_sessionid;  //本轮各线程一起终结的那个会话 id
    int                    m_attemptSeq; //本轮终结的尝试序号（次）
    int64_t                m_now;        //传给 finishAttempt 的时间，Unix 毫秒
    int                    m_threadCount;//本轮参与的线程数，自旋栅栏据它判断是否已全部就位
    std::atomic<int>*      m_ready;      //已到达栅栏的线程数（上下文不持有它的所有权）
    std::atomic<int>*      m_winners;    //拿到 true 的线程数（上下文不持有它的所有权）

    ConcurrentFinishContext()
        : m_mgr(NULL), m_attemptSeq(0), m_now(0), m_threadCount(0)
        , m_ready(NULL), m_winners(NULL)
    {
    }
};

//FTM19 的线程体：先在自旋栅栏上等齐所有线程，再一起调 finishAttempt。
//写成具名函数而不是 lambda，与本仓库的代码约定一致
void concurrentFinishWorker(ConcurrentFinishContext* ctx)
{
    //自旋而不是等条件变量：条件变量唤醒各线程的时刻本就有先有后，自旋能让各线程
    //尽量在同一个瞬间进入 finishAttempt，"查"与"写"之间的窗口才暴露得出来
    ctx->m_ready->fetch_add(1);
    while (ctx->m_ready->load() < ctx->m_threadCount)
        std::this_thread::yield();

    if (ctx->m_mgr->finishAttempt(ctx->m_sessionid, ctx->m_attemptSeq,
                                  folder_state_completed, ctx->m_now))
    {
        ctx->m_winners->fetch_add(1);
    }
}

} // namespace

TEST_CASE("FTM1 createSession 登记会话、状态为响铃中、sessionid 不含令牌分隔符且不重复")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    FolderTransferScale scale;
    scale.folderName = "project-docs";
    scale.totalDirs  = 3;
    scale.totalFiles = 12;
    scale.totalSize  = 4096;

    std::string sid = mgr.createSession(810001, 820001, scale, 1000);
    REQUIRE_FALSE(sid.empty());
    //sessionid 进 RelayToken 的 payload，须不含分隔符 '.' 与 '|'
    CHECK(sid.find('.') == std::string::npos);
    CHECK(sid.find('|') == std::string::npos);

    FolderTransferSessionInfo s;
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.sessionid == sid);
    CHECK(s.senderId == 810001);
    CHECK(s.receiverId == 820001);
    CHECK(s.folderName == "project-docs");
    CHECK(s.totalDirs == 3);
    CHECK(s.totalFiles == 12);
    CHECK(s.totalSize == 4096);
    CHECK(s.state == folder_state_ringing);
    CHECK(s.createTime == 1000);
    //新建时状态时间与创建时间相同，失败保留期从此后每次状态变更起算
    CHECK(s.stateTime == 1000);

    //同一对用户、同一毫秒再建一次（createSession 本身不做并发限制，那是 checkInvite
    //的职责）：自增序号保证两次拿到的 sessionid 不同
    std::string sid2 = mgr.createSession(810001, 820001, scale, 1000);
    CHECK(sid2 != sid);

    mgr.removeSession(sid);
    mgr.removeSession(sid2);
}

TEST_CASE("FTM2 removeSession 后查不到，重复删除返回 false")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    std::string sid = mgr.createSession(810002, 820002, makeScale(1, 1), 1000);

    REQUIRE(mgr.removeSession(sid));
    FolderTransferSessionInfo s;
    CHECK_FALSE(mgr.getSession(sid, s));
    //再次删除返回 false（本无该会话）
    CHECK_FALSE(mgr.removeSession(sid));
}

TEST_CASE("FTM3 不存在的 sessionid：查询 / 改状态 / 签令牌 / 重试均失败")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    FolderTransferSessionInfo s;
    std::string token;
    int errcode = 0;

    CHECK_FALSE(mgr.getSession("no-such-folder-session", s));
    CHECK_FALSE(mgr.setState("no-such-folder-session", folder_state_failed, 1000));
    CHECK_FALSE(mgr.signTokenFor("no-such-folder-session",
                                 RelayToken::role_folder_sender, 1000, token));

    //会话不存在与"会话已过保留期被销毁"在客户端看来是同一件事，都回已过期
    CHECK_FALSE(mgr.canRetry("no-such-folder-session", 810003, 1000, errcode));
    CHECK(errcode == error_code_folder_session_expired);
}

TEST_CASE("FTM4 signTokenFor 双方令牌：同会话、相反角色、uid 与 peeruid 对调")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    CHECK(mgr.getRelayHost() == kTestRelayHost);
    int port = mgr.getRelayPort();
    CHECK(port == kTestRelayPort);

    std::string sid = mgr.createSession(810004, 820004, makeScale(5, 500), 1000);

    //先把两个角色常量取到局部变量里再参与断言：doctest 的断言宏按<u>引用</u>捕获操作数，
    //直接把 RelayToken::role_folder_sender 写进 CHECK 会构成 odr-use，而
    //utils/RelayToken.cpp 目前只为 role_controller / role_controlled / role_caller /
    //role_callee 写了类外定义，这两个文件夹传输角色还没有，链接期会报未定义引用
    const int roleSender   = RelayToken::role_folder_sender;
    const int roleReceiver = RelayToken::role_folder_receiver;

    std::string tokSender;
    std::string tokReceiver;
    REQUIRE(mgr.signTokenFor(sid, roleSender, 1000, tokSender));
    REQUIRE(mgr.signTokenFor(sid, roleReceiver, 1000, tokReceiver));

    RelayTokenPayload a;
    RelayTokenPayload b;
    //令牌有效期 5 分钟，now=2000 仍在有效期内
    REQUIRE(RelayToken::verify(tokSender, kTestRelayKey, 2000, a));
    REQUIRE(RelayToken::verify(tokReceiver, kTestRelayKey, 2000, b));

    //同一会话
    CHECK(a.sessionid == sid);
    CHECK(b.sessionid == sid);
    //角色相反
    CHECK(a.role == roleSender);
    CHECK(b.role == roleReceiver);
    //发送端令牌 uid=发送端、peeruid=接收端；接收端令牌对调
    CHECK(a.uid == 810004);
    CHECK(a.peeruid == 820004);
    CHECK(b.uid == 820004);
    CHECK(b.peeruid == 810004);

    //一枚令牌要开 1 条控制通道 + N 条数据通道，令牌里不含 lane，通道条数单独下发
    int lanes = mgr.getDataLanes();
    CHECK(lanes == kTestLanes);

    mgr.removeSession(sid);
}

TEST_CASE("FTM5 setState 更新会话状态并刷新状态时间")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    std::string sid = mgr.createSession(810005, 820005, makeScale(5, 500), 1000);

    REQUIRE(mgr.setState(sid, folder_state_transferring, 3000));
    FolderTransferSessionInfo s;
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.state == folder_state_transferring);
    //创建时间不动，状态时间跟着每次状态变更走
    CHECK(s.createTime == 1000);
    CHECK(s.stateTime == 3000);

    REQUIRE(mgr.setState(sid, folder_state_failed, 8000));
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.state == folder_state_failed);
    CHECK(s.stateTime == 8000);

    mgr.removeSession(sid);
}

TEST_CASE("FTM6 规模上限边界：正好等于上限放行，超出一个即拒绝")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();

    //本用例专用的紧上限：100 个文件 / 1000 字节
    const int64_t maxFiles = 100;
    const int64_t maxSize  = 1000;
    mgr.configure(kTestRelayKey, kTestRelayHost, kTestRelayPort, kTestLanes,
                  maxFiles, maxSize, kLooseMaxSessions, kLooseMaxPerUser,
                  kTestInviteTimeoutMs, kTestRetryWindowMs);

    //上限值要能取出来回带给客户端提示
    int64_t gotMaxFiles = mgr.getMaxFiles();
    int64_t gotMaxSize  = mgr.getMaxSize();
    CHECK(gotMaxFiles == maxFiles);
    CHECK(gotMaxSize == maxSize);

    //正好等于上限：放行
    CHECK(mgr.checkScale(maxFiles, maxSize) == error_code_ok);
    //文件数多一个：拒绝
    CHECK(mgr.checkScale(maxFiles + 1, maxSize) == error_code_folder_too_many_files);
    //总字节数多一个字节：拒绝
    CHECK(mgr.checkScale(maxFiles, maxSize + 1) == error_code_folder_too_large);
    //两项同时超限：先报文件数
    CHECK(mgr.checkScale(maxFiles + 1, maxSize + 1) == error_code_folder_too_many_files);

    //checkInvite 里也走同一套判定
    CHECK(mgr.checkInvite(810006, 820006, maxFiles, maxSize) == error_code_ok);
    CHECK(mgr.checkInvite(810006, 820006, maxFiles + 1, maxSize)
          == error_code_folder_too_many_files);
    CHECK(mgr.checkInvite(810006, 820006, maxFiles, maxSize + 1)
          == error_code_folder_too_large);

    configureLoose(mgr);
}

TEST_CASE("FTM7 每用户并发上限：第 5 笔仍放行、第 6 笔回 620，失败态仍占名额，销毁即释放")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();

    //本用例把每用户上限设成配置项的默认值 5 笔，服务器总数上限保持宽松，
    //这样被拦下的原因只可能是每用户名额
    const int perUser = FolderTransferManager::kDefaultMaxSessionsPerUser;
    mgr.configure(kTestRelayKey, kTestRelayHost, kTestRelayPort, kTestLanes,
                  kLooseMaxFiles, kLooseMaxSize, kLooseMaxSessions, perUser,
                  kTestInviteTimeoutMs, kTestRetryWindowMs);

    //上限值要能取出来，供监控与配置核对
    int gotPerUser = mgr.getMaxSessionsPerUser();
    CHECK(gotPerUser == perUser);
    //默认值本身就是 5：产品口径「每人同时最多 5 笔」由这条断言钉住
    CHECK(perUser == 5);

    const int64_t sender       = 810007;
    //每笔各配一个不同的接收方，避免接收方那一侧先满额而混淆被拒原因
    const int64_t receiverBase = 820070;
    const int64_t other        = 830007;
    const int64_t other2       = 840007;

    std::vector<std::string> sids;
    for (int i = 0; i < perUser; ++i)
    {
        //满额之前逐笔放行，正好第 5 笔（i == 4）也仍然放行
        int code = mgr.checkInvite(sender, receiverBase + i, 1, 1);
        CHECK(code == error_code_ok);

        sids.push_back(mgr.createSession(sender, receiverBase + i, makeScale(5, 500), 1000));

        //每建一笔，该用户的参与笔数加一
        int count = mgr.getUserSessionCount(sender);
        CHECK(count == i + 1);
    }

    //第 6 笔：发送方名额已满，回 620
    CHECK(mgr.checkInvite(sender, other, 1, 1) == error_code_folder_busy);
    //别人发给这个已满额的用户同样被拒 —— 他作接收方也要占一笔
    CHECK(mgr.checkInvite(other, sender, 1, 1) == error_code_folder_busy);
    //与他无关的两个人不受影响
    CHECK(mgr.checkInvite(other, other2, 1, 1) == error_code_ok);
    //只参与了一笔的接收方还有 4 个名额，照常能再发
    int oneReceiverCount = mgr.getUserSessionCount(receiverBase);
    CHECK(oneReceiverCount == 1);
    CHECK(mgr.checkInvite(receiverBase, other, 1, 1) == error_code_ok);

    //失败待重试的会话仍占着名额（要留着续传），故此时笔数不变、第 6 笔照旧被拒
    REQUIRE(mgr.setState(sids[0], folder_state_failed, 2000));
    int countAfterFailed = mgr.getUserSessionCount(sender);
    CHECK(countAfterFailed == perUser);
    CHECK(mgr.checkInvite(sender, other, 1, 1) == error_code_folder_busy);

    //销毁其中一笔后名额释放，新的邀请重新放行
    REQUIRE(mgr.removeSession(sids[0]));
    int countAfterRemove = mgr.getUserSessionCount(sender);
    CHECK(countAfterRemove == perUser - 1);
    CHECK(mgr.checkInvite(sender, other, 1, 1) == error_code_ok);

    for (size_t i = 1; i < sids.size(); ++i)
    {
        mgr.removeSession(sids[i]);
    }

    //全部销毁后该用户回到零笔，与本用例无关的用户始终是零笔
    int countAfterCleanup = mgr.getUserSessionCount(sender);
    CHECK(countAfterCleanup == 0);
    int otherCount = mgr.getUserSessionCount(other);
    CHECK(otherCount == 0);

    configureLoose(mgr);
}

TEST_CASE("FTM7B 每用户名额按参与笔数计：作发送方 3 笔 + 作接收方 2 笔同样是满额 5 笔")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();

    const int perUser = FolderTransferManager::kDefaultMaxSessionsPerUser;
    mgr.configure(kTestRelayKey, kTestRelayHost, kTestRelayPort, kTestLanes,
                  kLooseMaxFiles, kLooseMaxSize, kLooseMaxSessions, perUser,
                  kTestInviteTimeoutMs, kTestRetryWindowMs);

    //本人作发送方的笔数（笔）：给 3 位同事各发一个文件夹
    const int asSenderCount   = 3;
    //本人作接收方的笔数（笔）：同时收 2 位同事发来的文件夹
    const int asReceiverCount = 2;

    const int64_t me        = 810071;
    const int64_t peerBase  = 820071;
    const int64_t other     = 830071;

    std::vector<std::string> sids;
    for (int i = 0; i < asSenderCount; ++i)
    {
        sids.push_back(mgr.createSession(me, peerBase + i, makeScale(1, 1), 1000));
    }
    for (int i = 0; i < asReceiverCount; ++i)
    {
        sids.push_back(mgr.createSession(peerBase + asSenderCount + i, me, makeScale(1, 1), 1000));
    }

    //发与收占的是同一份名额：3 + 2 即已满额
    int count = mgr.getUserSessionCount(me);
    CHECK(count == asSenderCount + asReceiverCount);
    CHECK(count == perUser);

    //满额后，他再发与别人再发给他都被拒
    CHECK(mgr.checkInvite(me, other, 1, 1) == error_code_folder_busy);
    CHECK(mgr.checkInvite(other, me, 1, 1) == error_code_folder_busy);

    //销毁他作接收方的那一笔，名额同样释放 —— 不区分角色
    REQUIRE(mgr.removeSession(sids[sids.size() - 1]));
    int countAfterRemove = mgr.getUserSessionCount(me);
    CHECK(countAfterRemove == perUser - 1);
    CHECK(mgr.checkInvite(me, other, 1, 1) == error_code_ok);

    for (size_t i = 0; i + 1 < sids.size(); ++i)
    {
        mgr.removeSession(sids[i]);
    }

    configureLoose(mgr);
}

TEST_CASE("FTM8 服务器活跃会话数达到上限时拒绝新邀请")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();

    //以当前活跃会话数为基准再放两个名额，避免受同文件其它用例遗留会话影响
    size_t base = mgr.getActiveSessionCount();
    const int limit = (int)base + 2;
    mgr.configure(kTestRelayKey, kTestRelayHost, kTestRelayPort, kTestLanes,
                  kLooseMaxFiles, kLooseMaxSize, limit, kLooseMaxPerUser,
                  kTestInviteTimeoutMs, kTestRetryWindowMs);

    int gotLimit = mgr.getMaxSessions();
    CHECK(gotLimit == limit);

    std::string sid1 = mgr.createSession(810081, 820081, makeScale(1, 1), 1000);
    CHECK_FALSE(mgr.isServerBusy());
    std::string sid2 = mgr.createSession(810082, 820082, makeScale(1, 1), 1000);

    //名额用满：每用户名额还很宽裕的第三对用户也发不了
    CHECK(mgr.isServerBusy());
    CHECK(mgr.checkInvite(810083, 820083, 1, 1) == error_code_folder_server_busy);

    //腾出一个名额后恢复
    REQUIRE(mgr.removeSession(sid1));
    CHECK_FALSE(mgr.isServerBusy());
    CHECK(mgr.checkInvite(810083, 820083, 1, 1) == error_code_ok);

    mgr.removeSession(sid2);
    configureLoose(mgr);
}

TEST_CASE("FTM8B 两道并发闸门同时到顶时先报每用户上限 620，而不是服务器繁忙 619")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();

    //把每用户上限压到 2 笔，服务器总数上限正好设成"这 2 笔建完之后的会话数"，
    //于是同一时刻两道闸门都到顶，可以观察 checkInvite 先报哪一个
    const int perUser = 2;
    size_t base = mgr.getActiveSessionCount();
    const int limit = (int)base + perUser;
    mgr.configure(kTestRelayKey, kTestRelayHost, kTestRelayPort, kTestLanes,
                  kLooseMaxFiles, kLooseMaxSize, limit, perUser,
                  kTestInviteTimeoutMs, kTestRetryWindowMs);

    const int64_t sender = 810084;

    std::string sid1 = mgr.createSession(sender, 820084, makeScale(1, 1), 1000);
    std::string sid2 = mgr.createSession(sender, 820085, makeScale(1, 1), 1000);

    //确认两道闸门确实都到顶了
    CHECK(mgr.isServerBusy());
    int senderCount = mgr.getUserSessionCount(sender);
    CHECK(senderCount == perUser);

    //已满额的那个用户再发起：回每用户上限的 620。口径是"名额已满比服务器繁忙更有
    //指导性"——等自己那几笔里有一笔传完就能继续，而"服务器繁忙"只能干等
    CHECK(mgr.checkInvite(sender, 830084, 1, 1) == error_code_folder_busy);
    //别人发给他同样先报 620（接收方名额也在服务器总数之前判）
    CHECK(mgr.checkInvite(830084, sender, 1, 1) == error_code_folder_busy);

    //换成一对各自都还没占名额的用户：这时才轮到服务器总数上限的 619
    CHECK(mgr.checkInvite(840084, 850084, 1, 1) == error_code_folder_server_busy);

    mgr.removeSession(sid1);
    mgr.removeSession(sid2);
    configureLoose(mgr);
}

TEST_CASE("FTM9 tick 邀请超时边界：差 1 毫秒不清、正好到点清")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    const int64_t created = 5000;

    //（1）差 1 毫秒：不清理
    {
        std::vector<FolderTransferSessionInfo> timedOut;
        std::vector<FolderTransferSessionInfo> expired;
        std::string sid = mgr.createSession(810091, 820091, makeScale(1, 1), created);

        mgr.tick(created + kTestInviteTimeoutMs - 1, timedOut, expired);

        FolderTransferSessionInfo s;
        CHECK(mgr.getSession(sid, s));      //仍在
        CHECK(s.state == folder_state_ringing);
        mgr.removeSession(sid);
    }

    //（2）正好到点：收集并移除，回填的状态已改写为超时态
    {
        std::vector<FolderTransferSessionInfo> timedOut;
        std::vector<FolderTransferSessionInfo> expired;
        std::string sid = mgr.createSession(810092, 820092, makeScale(1, 1), created);

        mgr.tick(created + kTestInviteTimeoutMs, timedOut, expired);

        FolderTransferSessionInfo s;
        CHECK_FALSE(mgr.getSession(sid, s));   //已被移除

        //用 find 而非 size 定位，避免单例里其它用例的遗留会话干扰
        bool found = false;
        for (size_t i = 0; i < timedOut.size(); ++i)
        {
            if (timedOut[i].sessionid == sid)
            {
                found = true;
                CHECK(timedOut[i].senderId == 810092);
                CHECK(timedOut[i].receiverId == 820092);
                //调用方照这个状态推 cmd_folder_state_notify，不必自己再改
                CHECK(timedOut[i].state == folder_state_timeout);
            }
        }
        CHECK(found);
    }

    //（3）已进入传输中：邀请超时对它无效，传多久都不清理
    {
        std::vector<FolderTransferSessionInfo> timedOut;
        std::vector<FolderTransferSessionInfo> expired;
        std::string sid = mgr.createSession(810093, 820093, makeScale(1, 1), created);
        REQUIRE(mgr.setState(sid, folder_state_transferring, created + 10));

        mgr.tick(created + kTestInviteTimeoutMs * 100, timedOut, expired);

        FolderTransferSessionInfo s;
        CHECK(mgr.getSession(sid, s));      //仍在
        mgr.removeSession(sid);
    }
}

TEST_CASE("FTM10 tick 失败会话保留期边界：差 1 毫秒不销毁、正好到点销毁")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    //失败保留期从进入失败态那一刻起算，与建会话的时间无关
    const int64_t created  = 1000;
    const int64_t failedAt = 20000;

    //（1）差 1 毫秒：保留，期内还能重试续传
    {
        std::vector<FolderTransferSessionInfo> timedOut;
        std::vector<FolderTransferSessionInfo> expired;
        std::string sid = mgr.createSession(810101, 820101, makeScale(1, 1), created);
        REQUIRE(mgr.setState(sid, folder_state_failed, failedAt));

        mgr.tick(failedAt + kTestRetryWindowMs - 1, timedOut, expired);

        FolderTransferSessionInfo s;
        CHECK(mgr.getSession(sid, s));      //仍在
        mgr.removeSession(sid);
    }

    //（2）正好到点：销毁并交给调用方
    {
        std::vector<FolderTransferSessionInfo> timedOut;
        std::vector<FolderTransferSessionInfo> expired;
        std::string sid = mgr.createSession(810102, 820102, makeScale(1, 1), created);
        REQUIRE(mgr.setState(sid, folder_state_failed, failedAt));

        mgr.tick(failedAt + kTestRetryWindowMs, timedOut, expired);

        FolderTransferSessionInfo s;
        CHECK_FALSE(mgr.getSession(sid, s));   //已被销毁

        bool found = false;
        for (size_t i = 0; i < expired.size(); ++i)
        {
            if (expired[i].sessionid == sid)
            {
                found = true;
                CHECK(expired[i].senderId == 810102);
                CHECK(expired[i].receiverId == 820102);
            }
        }
        CHECK(found);

        //销毁后再来重试，一律回"已过期"
        int errcode = 0;
        CHECK_FALSE(mgr.canRetry(sid, 810102, failedAt + kTestRetryWindowMs, errcode));
        CHECK(errcode == error_code_folder_session_expired);
    }
}

TEST_CASE("FTM11 canRetry 保留期边界：第 9 分 59 秒可重试、第 10 分 01 秒不可")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    //一分钟的毫秒数，用于把"第 9 分 59 秒"这类说法换算成毫秒
    const int64_t kOneMinuteMs = 60 * 1000;
    const int64_t kOneSecondMs = 1000;

    const int64_t failedAt = 100000;
    std::string sid = mgr.createSession(810011, 820011, makeScale(1, 1), 1000);
    REQUIRE(mgr.setState(sid, folder_state_failed, failedAt));

    int errcode = -1;

    //第 9 分 59 秒：仍在 10 分钟保留期内，允许重试续传
    CHECK(mgr.canRetry(sid, 810011, failedAt + 9 * kOneMinuteMs + 59 * kOneSecondMs, errcode));
    CHECK(errcode == error_code_ok);

    //正好第 10 分 00 秒：与 tick 的清理口径一致，到点即视为过期
    errcode = -1;
    CHECK_FALSE(mgr.canRetry(sid, 810011, failedAt + kTestRetryWindowMs, errcode));
    CHECK(errcode == error_code_folder_session_expired);

    //第 10 分 01 秒：同样不可（会话还在表里只是因为定时器尚未扫到）
    errcode = -1;
    CHECK_FALSE(mgr.canRetry(sid, 810011, failedAt + 10 * kOneMinuteMs + kOneSecondMs, errcode));
    CHECK(errcode == error_code_folder_session_expired);

    mgr.removeSession(sid);
}

TEST_CASE("FTM12 canRetry 只认本会话的发送方，接收方与第三方一律拒绝")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    const int64_t sender   = 810012;
    const int64_t receiver = 820012;
    const int64_t other    = 830012;
    const int64_t failedAt = 5000;

    std::string sid = mgr.createSession(sender, receiver, makeScale(1, 1), 1000);
    REQUIRE(mgr.setState(sid, folder_state_failed, failedAt));

    int errcode = -1;
    //接收方：重试入口只在发送方的失败气泡上
    CHECK_FALSE(mgr.canRetry(sid, receiver, failedAt + 1000, errcode));
    CHECK(errcode == error_code_permission_denied);

    //第三方即便拿到 sessionid 也无权重试
    errcode = -1;
    CHECK_FALSE(mgr.canRetry(sid, other, failedAt + 1000, errcode));
    CHECK(errcode == error_code_permission_denied);

    //发送方本人可以
    errcode = -1;
    CHECK(mgr.canRetry(sid, sender, failedAt + 1000, errcode));
    CHECK(errcode == error_code_ok);

    mgr.removeSession(sid);
}

TEST_CASE("FTM13 canRetry 只受理失败态的会话，其余状态按非法请求拒绝")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    const int64_t sender   = 810013;
    const int64_t receiver = 820013;

    std::string sid = mgr.createSession(sender, receiver, makeScale(1, 1), 1000);

    int errcode = -1;
    //还在响铃中：客户端根本不该有重试入口
    CHECK_FALSE(mgr.canRetry(sid, sender, 2000, errcode));
    CHECK(errcode == error_code_bad_request);

    //传输中：同样拒绝
    REQUIRE(mgr.setState(sid, folder_state_transferring, 2000));
    errcode = -1;
    CHECK_FALSE(mgr.canRetry(sid, sender, 3000, errcode));
    CHECK(errcode == error_code_bad_request);

    //转失败态后才放行
    REQUIRE(mgr.setState(sid, folder_state_failed, 4000));
    errcode = -1;
    CHECK(mgr.canRetry(sid, sender, 5000, errcode));
    CHECK(errcode == error_code_ok);

    mgr.removeSession(sid);
}

TEST_CASE("FTM14 configure 越界的 folderlanes 截断为硬上限 8，非法值回落默认值")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();

    const int hardLimit   = FolderTransferManager::kMaxDataLanes;
    const int defaultLane = FolderTransferManager::kDefaultDataLanes;

    //超过硬上限：按 8 截断
    mgr.configure(kTestRelayKey, kTestRelayHost, kTestRelayPort, hardLimit + 1,
                  kLooseMaxFiles, kLooseMaxSize, kLooseMaxSessions, kLooseMaxPerUser,
                  kTestInviteTimeoutMs, kTestRetryWindowMs);
    int lanes = mgr.getDataLanes();
    CHECK(lanes == hardLimit);

    //远超硬上限：同样是 8
    mgr.configure(kTestRelayKey, kTestRelayHost, kTestRelayPort, 100,
                  kLooseMaxFiles, kLooseMaxSize, kLooseMaxSessions, kLooseMaxPerUser,
                  kTestInviteTimeoutMs, kTestRetryWindowMs);
    lanes = mgr.getDataLanes();
    CHECK(lanes == hardLimit);

    //正好等于硬上限：原样保留
    mgr.configure(kTestRelayKey, kTestRelayHost, kTestRelayPort, hardLimit,
                  kLooseMaxFiles, kLooseMaxSize, kLooseMaxSessions, kLooseMaxPerUser,
                  kTestInviteTimeoutMs, kTestRetryWindowMs);
    lanes = mgr.getDataLanes();
    CHECK(lanes == hardLimit);

    //配置缺失或写成 0 / 负数：回落到默认 4 条
    mgr.configure(kTestRelayKey, kTestRelayHost, kTestRelayPort, 0,
                  kLooseMaxFiles, kLooseMaxSize, kLooseMaxSessions, kLooseMaxPerUser,
                  kTestInviteTimeoutMs, kTestRetryWindowMs);
    lanes = mgr.getDataLanes();
    CHECK(lanes == defaultLane);

    //上限类配置项写成 0 同样回落默认值，否则上限为 0 会把所有传输一律判超限
    mgr.configure(kTestRelayKey, kTestRelayHost, kTestRelayPort, kTestLanes,
                  0, 0, 0, 0, 0, 0);
    int64_t maxFiles      = mgr.getMaxFiles();
    int64_t maxSize       = mgr.getMaxSize();
    int     maxSessions   = mgr.getMaxSessions();
    int     maxPerUser    = mgr.getMaxSessionsPerUser();
    int64_t inviteTimeout = mgr.getInviteTimeoutMs();
    int64_t retryWindow   = mgr.getRetryWindowMs();

    const int64_t defaultMaxFiles      = FolderTransferManager::kDefaultMaxFiles;
    const int64_t defaultMaxSize       = FolderTransferManager::kDefaultMaxSize;
    const int     defaultMaxSessions   = FolderTransferManager::kDefaultMaxSessions;
    const int     defaultMaxPerUser    = FolderTransferManager::kDefaultMaxSessionsPerUser;
    const int64_t defaultInviteTimeout = FolderTransferManager::kDefaultInviteTimeoutMs;
    const int64_t defaultRetryWindow   = FolderTransferManager::kDefaultRetryWindowMs;

    CHECK(maxFiles == defaultMaxFiles);
    CHECK(maxSize == defaultMaxSize);
    CHECK(maxSessions == defaultMaxSessions);
    CHECK(maxPerUser == defaultMaxPerUser);
    CHECK(inviteTimeout == defaultInviteTimeout);
    CHECK(retryWindow == defaultRetryWindow);

    //foldermaxperuser 写成负数：与写成 0 一样回落到默认 5 笔，
    //否则上限为 0 或负数会把所有邀请一律判成"名额已满"
    mgr.configure(kTestRelayKey, kTestRelayHost, kTestRelayPort, kTestLanes,
                  kLooseMaxFiles, kLooseMaxSize, kLooseMaxSessions, -1,
                  kTestInviteTimeoutMs, kTestRetryWindowMs);
    maxPerUser = mgr.getMaxSessionsPerUser();
    CHECK(maxPerUser == defaultMaxPerUser);
    //回落后的默认值就是产品口径的 5 笔
    CHECK(maxPerUser == 5);

    //两个默认值也一并锁住：邀请超时 30 分钟、失败会话保留期 10 分钟
    const int64_t kThirtyMinutesMs = 30 * 60 * 1000;
    const int64_t kTenMinutesMs    = 10 * 60 * 1000;
    CHECK(defaultInviteTimeout == kThirtyMinutesMs);
    CHECK(defaultRetryWindow == kTenMinutesMs);

    configureLoose(mgr);
}

TEST_CASE("FTM15 findSessionsByUser 反查：收发双方都能查到整条会话，第三方查不到，销毁后查不到")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    const int64_t sender   = 810015;
    const int64_t receiver = 820015;
    const int64_t other    = 830015;

    FolderTransferScale scale;
    scale.folderName = "quarter-report";
    scale.totalDirs  = 4;
    scale.totalFiles = 9;
    scale.totalSize  = 8192;

    std::string sid = mgr.createSession(sender, receiver, scale, 1000);

    //发送方反查：回填的是整条会话，掉线收尾要据其中的 state 判断该给对端推哪种状态
    std::vector<FolderTransferSessionInfo> bySender;
    int senderHit = mgr.findSessionsByUser(sender, bySender);
    CHECK(senderHit == 1);
    REQUIRE(bySender.size() == 1);
    CHECK(bySender[0].sessionid == sid);
    CHECK(bySender[0].senderId == sender);
    CHECK(bySender[0].receiverId == receiver);
    CHECK(bySender[0].folderName == "quarter-report");
    CHECK(bySender[0].totalDirs == 4);
    CHECK(bySender[0].totalFiles == 9);
    CHECK(bySender[0].totalSize == 8192);
    //邀请尚未被应答，会话处于响铃态：此时一方掉线，对端应收到"对方离线"而非"已失败"
    CHECK(bySender[0].state == folder_state_ringing);
    CHECK(bySender[0].createTime == 1000);

    //接收方反查：这正是本接口存在的理由 —— 接收端在应答之前手里没有 sessionid，
    //它在响铃阶段掉线时只能按自己的 userid 定位这笔会话
    std::vector<FolderTransferSessionInfo> byReceiver;
    int receiverHit = mgr.findSessionsByUser(receiver, byReceiver);
    CHECK(receiverHit == 1);
    REQUIRE(byReceiver.size() == 1);
    CHECK(byReceiver[0].sessionid == sid);
    CHECK(byReceiver[0].senderId == sender);
    CHECK(byReceiver[0].receiverId == receiver);

    //与本会话无关的第三方查不到：他掉线不得牵连本会话
    std::vector<FolderTransferSessionInfo> byOther;
    int otherHit = mgr.findSessionsByUser(other, byOther);
    CHECK(otherHit == 0);
    CHECK(byOther.empty());

    //一笔也没查到时不改动出参：本方法只追加、不预先清空，调用方原有的元素要保持不变
    std::vector<FolderTransferSessionInfo> untouched;
    FolderTransferSessionInfo keep;
    keep.sessionid = "keep-me";
    untouched.push_back(keep);
    int untouchedHit = mgr.findSessionsByUser(other, untouched);
    CHECK(untouchedHit == 0);
    REQUIRE(untouched.size() == 1);
    CHECK(untouched[0].sessionid == "keep-me");

    //会话销毁后双方都查不到：掉线收尾据此判定"已无人需要通知"
    REQUIRE(mgr.removeSession(sid));
    bySender.clear();
    byReceiver.clear();
    CHECK(mgr.findSessionsByUser(sender, bySender) == 0);
    CHECK(mgr.findSessionsByUser(receiver, byReceiver) == 0);
}

TEST_CASE("FTM16 findSessionsByUser 与 getUserSessionCount 口径一致：失败待重试的会话仍能反查到")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    const int64_t sender   = 810016;
    const int64_t receiver = 820016;
    const int64_t other    = 830016;

    std::string sid = mgr.createSession(sender, receiver, makeScale(3, 300), 1000);

    //传输中的会话
    REQUIRE(mgr.setState(sid, folder_state_transferring, 2000));
    std::vector<FolderTransferSessionInfo> found;
    REQUIRE(mgr.findSessionsByUser(sender, found) == 1);
    CHECK(found[0].sessionid == sid);
    CHECK(found[0].state == folder_state_transferring);
    int senderCount = mgr.getUserSessionCount(sender);
    CHECK(senderCount == 1);

    //失败待重试的会话在保留期内仍占着双方的名额，反查同样要能命中 ——
    //否则失败态期间任一方掉线，对端就收不到"对方离线"的收尾通知
    REQUIRE(mgr.setState(sid, folder_state_failed, 3000));
    found.clear();
    REQUIRE(mgr.findSessionsByUser(sender, found) == 1);
    CHECK(found[0].state == folder_state_failed);
    CHECK(found[0].stateTime == 3000);
    found.clear();
    REQUIRE(mgr.findSessionsByUser(receiver, found) == 1);
    CHECK(found[0].sessionid == sid);
    int receiverCount = mgr.getUserSessionCount(receiver);
    CHECK(receiverCount == 1);

    //两个接口走同一处遍历，对同一批用户给出的笔数必须完全一致
    std::vector<FolderTransferSessionInfo> probe;
    int senderCountAgain = mgr.getUserSessionCount(sender);
    int senderFound      = mgr.findSessionsByUser(sender, probe);
    CHECK(senderCountAgain == senderFound);

    probe.clear();
    int otherCount = mgr.getUserSessionCount(other);
    int otherFound = mgr.findSessionsByUser(other, probe);
    CHECK(otherCount == otherFound);
    CHECK(otherFound == 0);

    //会话销毁后两个接口一起转为"查不到 / 零笔"
    REQUIRE(mgr.removeSession(sid));
    int senderCountAfter = mgr.getUserSessionCount(sender);
    CHECK(senderCountAfter == 0);
    found.clear();
    CHECK(mgr.findSessionsByUser(sender, found) == 0);
}

TEST_CASE("FTM17 findSessionsByUser 查出该用户的全部会话，含他既作发送方又作接收方的混合情形")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    const int64_t me    = 810017;
    const int64_t peer1 = 820017;
    const int64_t peer2 = 830017;
    const int64_t peer3 = 840017;
    const int64_t other = 850017;

    //本人参与三笔：两笔他作发送方（发给 peer1 / peer2），一笔他作接收方（peer3 发给他）。
    //掉线收尾必须把这三笔全部处理掉，只处理其中一笔的话另外两笔的对端会一直干等到超时
    std::string sid1 = mgr.createSession(me, peer1, makeScale(1, 1), 1000);
    std::string sid2 = mgr.createSession(me, peer2, makeScale(1, 1), 1000);
    std::string sid3 = mgr.createSession(peer3, me, makeScale(1, 1), 1000);

    std::vector<FolderTransferSessionInfo> mine;
    int hit = mgr.findSessionsByUser(me, mine);
    CHECK(hit == 3);
    REQUIRE(mine.size() == 3);

    //三笔一条不落，且与并发限制的计数完全一致
    CHECK(containsSession(mine, sid1));
    CHECK(containsSession(mine, sid2));
    CHECK(containsSession(mine, sid3));
    int myCount = mgr.getUserSessionCount(me);
    CHECK(myCount == hit);

    //角色混合：其中两笔他是发送方、一笔他是接收方。收尾时要据每条各自的
    //senderId / receiverId 算出该通知谁，故这两个字段必须逐条原样回填
    int asSender   = 0;
    int asReceiver = 0;
    for (size_t i = 0; i < mine.size(); ++i)
    {
        if (mine[i].senderId == me)
            ++asSender;
        if (mine[i].receiverId == me)
            ++asReceiver;
    }
    CHECK(asSender == 2);
    CHECK(asReceiver == 1);

    //会话表以 sessionid 为键有序存放，故追加进出参的顺序是 sessionid 字典序
    for (size_t i = 1; i < mine.size(); ++i)
    {
        CHECK(mine[i - 1].sessionid < mine[i].sessionid);
    }

    //本方法只追加不清空：同一个容器再查一次，原有的三条仍在，后面又多出三条
    mgr.findSessionsByUser(me, mine);
    CHECK(mine.size() == 6);

    //各对端只查到自己参与的那一笔，互不干扰
    std::vector<FolderTransferSessionInfo> peerSessions;
    REQUIRE(mgr.findSessionsByUser(peer1, peerSessions) == 1);
    CHECK(peerSessions[0].sessionid == sid1);
    peerSessions.clear();
    REQUIRE(mgr.findSessionsByUser(peer2, peerSessions) == 1);
    CHECK(peerSessions[0].sessionid == sid2);
    peerSessions.clear();
    REQUIRE(mgr.findSessionsByUser(peer3, peerSessions) == 1);
    CHECK(peerSessions[0].sessionid == sid3);

    //与三笔都无关的用户查不到任何会话
    peerSessions.clear();
    CHECK(mgr.findSessionsByUser(other, peerSessions) == 0);

    //逐笔销毁，反查结果同步减少 —— 收尾过程中每处理完一笔就少一笔
    REQUIRE(mgr.removeSession(sid1));
    mine.clear();
    CHECK(mgr.findSessionsByUser(me, mine) == 2);
    CHECK_FALSE(containsSession(mine, sid1));

    REQUIRE(mgr.removeSession(sid3));
    mine.clear();
    CHECK(mgr.findSessionsByUser(me, mine) == 1);
    CHECK(containsSession(mine, sid2));

    REQUIRE(mgr.removeSession(sid2));
    mine.clear();
    CHECK(mgr.findSessionsByUser(me, mine) == 0);
}

TEST_CASE("FTM18 尝试序号：新建会话为 1，每受理一次重试加一，其余路径不改它")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    const int64_t sender   = 810018;
    const int64_t receiver = 820018;

    //类内初始化的 static const 先复制到局部变量再参与断言：doctest 的 CHECK 宏按引用
    //捕获操作数，直接写会构成 ODR-use 而在链接期报未定义符号（做法与 FTM7 / FTM14 一致）
    const int firstAttemptSeq = FolderTransferManager::kFirstAttemptSeq;

    std::string sid = mgr.createSession(sender, receiver, makeScale(3, 300), 1000);

    //新建的会话就是第一次尝试。聊天记录里那条传输记录条的幂等键含本序号，
    //故它同时决定了"首次尝试的记录挂在序号 1 上"
    FolderTransferSessionInfo s;
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.attemptSeq == firstAttemptSeq);
    CHECK(s.attemptSeq == 1);

    //非重试路径不改序号：应答同意（转入传输中）与传输失败都只动状态
    REQUIRE(mgr.setState(sid, folder_state_transferring, 2000));
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.attemptSeq == 1);

    REQUIRE(mgr.setState(sid, folder_state_failed, 3000));
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.attemptSeq == 1);

    //第一次重试：序号加到 2，同时会话回到传输中、状态时间刷新
    int attempt = 0;
    REQUIRE(mgr.beginRetryAttempt(sid, 4000, attempt));
    CHECK(attempt == 2);

    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.attemptSeq == 2);
    CHECK(s.state == folder_state_transferring);
    CHECK(s.stateTime == 4000);
    //创建时间始终不动，邀请超时仍以它为基准
    CHECK(s.createTime == 1000);

    //第二次重试：继续加一，不会回到 1
    REQUIRE(mgr.setState(sid, folder_state_failed, 5000));
    attempt = 0;
    REQUIRE(mgr.beginRetryAttempt(sid, 6000, attempt));
    CHECK(attempt == 3);

    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.attemptSeq == 3);

    //反查路径拿到的序号与直接查会话一致 —— 掉线收尾正是靠反查定位会话、再据序号留记录
    std::vector<FolderTransferSessionInfo> byUser;
    REQUIRE(mgr.findSessionsByUser(sender, byUser) == 1);
    CHECK(byUser[0].attemptSeq == 3);

    //会话不存在时不受理，出参被置 0，调用方不会拿到一个看似合法的序号
    int bogus = -1;
    CHECK_FALSE(mgr.beginRetryAttempt("no-such-folder-session", 7000, bogus));
    CHECK(bogus == 0);

    mgr.removeSession(sid);

    //会话销毁后序号随之消失，重新建的会话又从 1 起算（新会话是全新的一笔传输）
    std::string sid2 = mgr.createSession(sender, receiver, makeScale(3, 300), 8000);
    REQUIRE(mgr.getSession(sid2, s));
    CHECK(s.attemptSeq == 1);

    mgr.removeSession(sid2);
}

TEST_CASE("FTM19 并发终结同一次尝试：多线程同时调用，恰好只有一个拿到 true")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    //参与并发的线程数（条）：线上一次传输正常收尾时双方各发一帧 cmd_folder_close，
    //两帧落在两个不同的 EventLoop 工作线程上，故真实并发度是 2；取 8 是为了留出余量，
    //让"查"与"写"之间的窗口更容易被撞上
    const int kFinishThreads = 8;
    //轮次（轮）：每一轮都是一次独立的抢入。取 200 轮是为了让本用例<u>稳定</u>复现，
    //而不是偶尔才红 —— 只要实现里还留着"先查后写"的窗口，几百轮里必然撞上若干次
    const int kFinishRounds = 200;

    const int64_t sender   = 810019;
    const int64_t receiver = 820019;
    //各轮统一使用的时间点（Unix 毫秒），本用例不关心时间推进
    const int64_t createTimeMs = 1000;
    const int64_t acceptTimeMs = 2000;
    const int64_t finishTimeMs = 3000;

    int duplicatedRounds = 0;   //出现了不止一个终结者的轮次数，正确实现下应为 0
    int missingRounds    = 0;   //一个终结者都没有的轮次数，正确实现下应为 0
    int maxWinners       = 0;   //单轮里最多有几个线程同时拿到 true

    for (int round = 0; round < kFinishRounds; ++round)
    {
        //每一轮各建一个会话并置为传输中，现场与线上一致（会话仍在表里、正在传输），
        //而不是对着一个不存在的会话空跑
        std::string sid = mgr.createSession(sender, receiver, makeScale(5, 500), createTimeMs);
        REQUIRE(mgr.setState(sid, folder_state_transferring, acceptTimeMs));

        std::atomic<int> ready(0);
        std::atomic<int> winners(0);

        ConcurrentFinishContext ctx;
        ctx.m_mgr         = &mgr;
        ctx.m_sessionid   = sid;
        ctx.m_attemptSeq  = FolderTransferManager::kFirstAttemptSeq;
        ctx.m_now         = finishTimeMs;
        ctx.m_threadCount = kFinishThreads;
        ctx.m_ready       = &ready;
        ctx.m_winners     = &winners;

        std::vector<std::thread> threads;
        threads.reserve(kFinishThreads);
        for (int i = 0; i < kFinishThreads; ++i)
        {
            threads.push_back(std::thread(&concurrentFinishWorker, &ctx));
        }

        for (size_t i = 0; i < threads.size(); ++i)
        {
            threads[i].join();
        }

        int got = winners.load();
        if (got > 1)
            ++duplicatedRounds;
        if (got < 1)
            ++missingRounds;
        if (got > maxWinners)
            maxWinners = got;

        //"进入终态"这件事同样只该发生一次，且落在传入的那个终态上
        FolderTransferSessionInfo s;
        REQUIRE(mgr.getSession(sid, s));
        CHECK(s.state == folder_state_completed);
        CHECK(s.stateTime == finishTimeMs);

        mgr.removeSession(sid);
    }

    //本条断言就是用户实测那个缺陷的复现点：两帧 cmd_folder_close 落在两个 I/O 线程上，
    //各自查到"还没终结过"，于是各留一条一模一样的记录条、各推一帧状态变更
    CHECK_MESSAGE(duplicatedRounds == 0,
                  "rounds with more than one winner: " << duplicatedRounds
                  << " out of " << kFinishRounds
                  << ", max winners in a single round: " << maxWinners
                  << ", threads per round: " << kFinishThreads);

    //一个终结者都没有同样是错的：那意味着这一次传输谁都不负责留记录，聊天记录里会缺一条
    CHECK_MESSAGE(missingRounds == 0,
                  "rounds with no winner at all: " << missingRounds
                  << " out of " << kFinishRounds);
}

TEST_CASE("FTM20 顺序终结同一次尝试：第一次 true，第二次及以后一律 false")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    const int64_t sender   = 810020;
    const int64_t receiver = 820020;

    std::string sid = mgr.createSession(sender, receiver, makeScale(5, 500), 1000);
    REQUIRE(mgr.setState(sid, folder_state_transferring, 2000));

    //第一个终结者：拿到 true，负责留记录与推送
    CHECK(mgr.finishAttempt(sid, 1, folder_state_completed, 3000));

    //会话状态被同一个临界区顺带置成了终态，状态时间刷成终结时刻
    FolderTransferSessionInfo s;
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.state == folder_state_completed);
    CHECK(s.stateTime == 3000);

    //后到的那一帧 cmd_folder_close：拿到 false，安静返回
    CHECK_FALSE(mgr.finishAttempt(sid, 1, folder_state_completed, 4000));
    //第三次、第四次同理 —— 掉线收尾与定时器清理都可能重复走到终结这一步
    CHECK_FALSE(mgr.finishAttempt(sid, 1, folder_state_failed, 5000));
    CHECK_FALSE(mgr.finishAttempt(sid, 1, folder_state_peer_offline, 6000));

    //被去重的调用不得改动会话状态：否则聊天记录里那条记录写的是"已完成"，
    //而会话表里却成了"已失败"，两处对不上
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.state == folder_state_completed);
    CHECK(s.stateTime == 3000);

    //序号<u>小于</u>已终结序号的调用同样被挡下：一笔传输重试到第 2 次之后，
    //第 1 次那条迟到的终结判定不该再落一条记录
    CHECK_FALSE(mgr.finishAttempt(sid, 0, folder_state_completed, 7000));

    mgr.removeSession(sid);
}

TEST_CASE("FTM21 重试之后能再终结一次：序号加一，新一次尝试不被上一次的标记挡住")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    const int64_t sender   = 810021;
    const int64_t receiver = 820021;

    std::string sid = mgr.createSession(sender, receiver, makeScale(5, 500), 1000);
    REQUIRE(mgr.setState(sid, folder_state_transferring, 2000));

    //第一次尝试失败：终结成功，会话转入失败态并开始计保留期
    CHECK(mgr.finishAttempt(sid, 1, folder_state_failed, 3000));

    FolderTransferSessionInfo s;
    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.state == folder_state_failed);
    CHECK(s.stateTime == 3000);
    CHECK(s.attemptSeq == 1);

    //同一次尝试的重复终结仍被挡住（对端那帧 cmd_folder_close 随后才到）
    CHECK_FALSE(mgr.finishAttempt(sid, 1, folder_state_failed, 3100));

    //保留期内点「重试」：序号加到 2，会话回到传输中
    int attempt = 0;
    REQUIRE(mgr.beginRetryAttempt(sid, 4000, attempt));
    CHECK(attempt == 2);

    //第二次尝试走到终态：<u>必须</u>能再终结一次并各留一条记录，需求要的就是
    //"每次尝试各留一条"，先失败后重试成功的一笔在聊天记录里能看到失败与完成两条
    CHECK(mgr.finishAttempt(sid, attempt, folder_state_completed, 5000));

    REQUIRE(mgr.getSession(sid, s));
    CHECK(s.state == folder_state_completed);
    CHECK(s.stateTime == 5000);

    //第二次尝试的重复终结照样被挡住
    CHECK_FALSE(mgr.finishAttempt(sid, attempt, folder_state_completed, 5100));
    //第一次尝试那条迟到的终结判定也不会被放行
    CHECK_FALSE(mgr.finishAttempt(sid, 1, folder_state_failed, 5200));

    //连续重试多次：每一次尝试都各自终结一次
    REQUIRE(mgr.beginRetryAttempt(sid, 6000, attempt));
    CHECK(attempt == 3);
    CHECK(mgr.finishAttempt(sid, attempt, folder_state_failed, 7000));
    CHECK_FALSE(mgr.finishAttempt(sid, attempt, folder_state_failed, 7100));

    mgr.removeSession(sid);
}

TEST_CASE("FTM22 会话不存在 / 已销毁 / 非终态时的终结判定")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    configureLoose(mgr);

    //类内初始化的 static const 先复制到局部变量再参与断言，理由见 FTM18 的同类注释
    const int64_t retentionMs = FolderTransferManager::kFinishedAttemptRetentionMs;

    const int64_t sender   = 810022;
    const int64_t receiver = 820022;

    //（1）会话根本不在会话表里：照样受理。邀请超时那条路径正是如此 —— tick 已经把会话
    //移出会话表，main.cpp 手里只有一份快照，此时仍要留下那条"已超时"的记录
    const std::string ghost = "810022-820022-1000-ghost";
    CHECK(mgr.finishAttempt(ghost, 1, folder_state_timeout, 1000));
    //但重复的那一次仍被挡住：定时器判定超时的同时，发送端那帧 cmd_folder_close 可能
    //正好也在路上
    CHECK_FALSE(mgr.finishAttempt(ghost, 1, folder_state_timeout, 1100));

    //（2）会话被销毁之后，登记项仍然留着 —— 销毁与"重复的终结判定到达"之间还有窗口
    std::string sid = mgr.createSession(sender, receiver, makeScale(5, 500), 1000);
    REQUIRE(mgr.setState(sid, folder_state_transferring, 2000));
    CHECK(mgr.finishAttempt(sid, 1, folder_state_completed, 3000));
    REQUIRE(mgr.removeSession(sid));
    CHECK_FALSE(mgr.finishAttempt(sid, 1, folder_state_completed, 3100));

    //（3）非终态一律不受理：既不登记也不改状态，避免这一次尝试此后真正走到终态时
    //被当成重复而丢掉整条记录
    std::string sid2 = mgr.createSession(sender, receiver, makeScale(5, 500), 1000);
    CHECK_FALSE(mgr.finishAttempt(sid2, 1, folder_state_ringing, 2000));
    CHECK_FALSE(mgr.finishAttempt(sid2, 1, folder_state_transferring, 2000));

    FolderTransferSessionInfo s;
    REQUIRE(mgr.getSession(sid2, s));
    CHECK(s.state == folder_state_ringing);
    CHECK(s.stateTime == 1000);

    //刚才那两次不受理没有留下任何登记项，故真正的终态照常受理
    CHECK(mgr.finishAttempt(sid2, 1, folder_state_rejected, 3000));
    mgr.removeSession(sid2);

    //（4）登记项的回收：所属会话已经销毁、且终结至今已达保留期，tick 才丢弃它。
    //丢弃之后同一个 (会话 id, 序号) 又会被当成第一次 —— 这不影响正确性，那时早已
    //没有任何一帧还在路上了
    std::vector<FolderTransferSessionInfo> timedOut;
    std::vector<FolderTransferSessionInfo> expired;

    //差 1 毫秒未到保留期：登记项还在，重复的终结判定仍被挡住
    mgr.tick(3000 + retentionMs - 1, timedOut, expired);
    CHECK_FALSE(mgr.finishAttempt(sid, 1, folder_state_completed, 3000 + retentionMs - 1));

    //正好到点即回收，与本类其余各处的时间边界口径一致（一律取"大于等于"）
    mgr.tick(3000 + retentionMs, timedOut, expired);
    CHECK(mgr.finishAttempt(sid, 1, folder_state_completed, 3000 + retentionMs));

    //会话仍在表里的登记项<u>不受</u>保留期约束：失败会话在整个 folderretrywindow 内
    //都可能再收到一帧重复的 cmd_folder_close，此时挡不住就会多留一条记录
    std::string sid3 = mgr.createSession(sender, receiver, makeScale(5, 500), 1000);
    REQUIRE(mgr.setState(sid3, folder_state_transferring, 2000));
    CHECK(mgr.finishAttempt(sid3, 1, folder_state_failed, 3000));
    mgr.tick(3000 + retentionMs, timedOut, expired);
    CHECK_FALSE(mgr.finishAttempt(sid3, 1, folder_state_failed, 3000 + retentionMs));

    mgr.removeSession(sid3);
}

// ======================= 2026-10-04 单聊单文件 P2P 直传会话 =======================
// 方案见 docs/单聊文件P2P直传与P2P徽章方案.md：直传会话与文件夹传输共用会话表，但不占文件夹传输的
// 每用户与整台服务器名额（它不用 fileserver 中转），另按 directfilemaxperuser 限额；邀请超时取 directfileinvitetimeout。

namespace {

//直传会话的规模：一个文件、没有目录
FolderTransferScale makeDirectScale()
{
    FolderTransferScale scale;
    scale.folderName = "quote.xlsx";
    scale.totalDirs  = 0;
    scale.totalFiles = 1;
    scale.totalSize  = 4096;
    scale.kind       = folder_kind_file;
    return scale;
}

//测试用的直传邀请超时（毫秒）= 5 分钟，与配置项默认值一致
const int64_t kTestDirectTimeoutMs = 5 * 60 * 1000;
//用例里的起始时间（Unix 毫秒），取值无关紧要
const int64_t kDirectT0 = 1790000000000LL;
//三个测试用户
const int64_t kDirectUserA = 7001;
const int64_t kDirectUserB = 7002;
const int64_t kDirectUserC = 7003;

//把单例里本组用例建的会话全部删掉，避免影响其它用例
void removeDirectUsersSessions(FolderTransferManager& mgr)
{
    const int64_t users[] = { kDirectUserA, kDirectUserB, kDirectUserC };
    for (size_t i = 0; i < sizeof(users) / sizeof(users[0]); ++i)
    {
        std::vector<FolderTransferSessionInfo> found;
        mgr.findSessionsByUser(users[i], found);
        for (size_t j = 0; j < found.size(); ++j)
            mgr.removeSession(found[j].sessionid);
    }
}

} // namespace

TEST_CASE("FTM23 直传会话不占文件夹传输的每用户与整台服务器名额")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    removeDirectUsersSessions(mgr);
    //文件夹传输：每人 1 笔、整台服务器 1 笔
    mgr.configure(kTestRelayKey, kTestRelayHost, kTestRelayPort, kTestLanes,
                  kLooseMaxFiles, kLooseMaxSize, 1, 1, kTestInviteTimeoutMs, kTestRetryWindowMs);
    mgr.configureDirectFile(kTestDirectTimeoutMs, 10);

    //A 先发起一笔直传：文件夹名额不受影响
    const std::string direct = mgr.createSession(kDirectUserA, kDirectUserB, makeDirectScale(), kDirectT0);
    CHECK(mgr.getUserSessionCount(kDirectUserA) == 0);
    CHECK_FALSE(mgr.isServerBusy());
    CHECK(mgr.checkInvite(kDirectUserA, kDirectUserB, 1, 1) == error_code_ok);

    //再建一笔文件夹传输：A 的文件夹名额与整台服务器名额都满了，但直传仍可发起
    mgr.createSession(kDirectUserA, kDirectUserC, makeScale(1, 1), kDirectT0);
    CHECK(mgr.getUserSessionCount(kDirectUserA) == 1);
    CHECK(mgr.checkInvite(kDirectUserA, kDirectUserC, 1, 1) == error_code_folder_busy);
    CHECK(mgr.isServerBusy());
    CHECK(mgr.checkDirectFileInvite(kDirectUserA, kDirectUserC) == error_code_ok);

    //按用户反查仍返回两种会话（掉线收尾要用）
    std::vector<FolderTransferSessionInfo> found;
    CHECK(mgr.findSessionsByUser(kDirectUserA, found) == 2);
    CHECK(mgr.getActiveSessionCount() >= 2);

    removeDirectUsersSessions(mgr);
    (void)direct;
    configureLoose(mgr);
}

TEST_CASE("FTM24 直传的每人上限：发送方或接收方的直传会话数到顶即拒绝，只数直传会话")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    removeDirectUsersSessions(mgr);
    configureLoose(mgr);
    mgr.configureDirectFile(kTestDirectTimeoutMs, 2);
    CHECK(mgr.getDirectFileMaxPerUser() == 2);

    mgr.createSession(kDirectUserA, kDirectUserB, makeDirectScale(), kDirectT0);
    CHECK(mgr.checkDirectFileInvite(kDirectUserA, kDirectUserC) == error_code_ok);
    mgr.createSession(kDirectUserA, kDirectUserB, makeDirectScale(), kDirectT0);
    //A 已有 2 笔：作发送方被拒
    CHECK(mgr.checkDirectFileInvite(kDirectUserA, kDirectUserC) == error_code_folder_busy);
    //B 已有 2 笔：作接收方同样被拒
    CHECK(mgr.checkDirectFileInvite(kDirectUserC, kDirectUserB) == error_code_folder_busy);
    //文件夹传输会话不计入直传名额
    mgr.createSession(kDirectUserC, kDirectUserA, makeScale(1, 1), kDirectT0);
    mgr.createSession(kDirectUserC, kDirectUserA, makeScale(1, 1), kDirectT0);
    CHECK(mgr.checkDirectFileInvite(kDirectUserC, kDirectUserA + 100) == error_code_ok);

    removeDirectUsersSessions(mgr);
    configureLoose(mgr);
}

TEST_CASE("FTM25 邀请超时按种类：直传取 directfileinvitetimeout，文件夹传输仍取 folderinvitetimeout")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    removeDirectUsersSessions(mgr);
    configureLoose(mgr);
    mgr.configureDirectFile(kTestDirectTimeoutMs, 10);
    const std::string direct = mgr.createSession(kDirectUserA, kDirectUserB, makeDirectScale(), kDirectT0);
    const std::string folder = mgr.createSession(kDirectUserA, kDirectUserB, makeScale(1, 1), kDirectT0);

    std::vector<FolderTransferSessionInfo> timedOut;
    std::vector<FolderTransferSessionInfo> expired;
    //5 分钟差 1 毫秒：都没超时
    mgr.tick(kDirectT0 + kTestDirectTimeoutMs - 1, timedOut, expired);
    CHECK(timedOut.empty());
    //满 5 分钟：只有直传超时
    mgr.tick(kDirectT0 + kTestDirectTimeoutMs, timedOut, expired);
    REQUIRE(timedOut.size() == 1);
    CHECK(timedOut[0].sessionid == direct);
    CHECK(timedOut[0].kind == folder_kind_file);
    FolderTransferSessionInfo info;
    CHECK(mgr.getSession(folder, info));
    //满 30 分钟：文件夹传输也超时
    timedOut.clear();
    mgr.tick(kDirectT0 + kTestInviteTimeoutMs, timedOut, expired);
    REQUIRE(timedOut.size() == 1);
    CHECK(timedOut[0].sessionid == folder);

    removeDirectUsersSessions(mgr);
    configureLoose(mgr);
}

TEST_CASE("FTM26 会话记下种类：createSession 按规模里的 kind 建会话，getSession 取回")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    removeDirectUsersSessions(mgr);
    configureLoose(mgr);
    const std::string direct = mgr.createSession(kDirectUserA, kDirectUserB, makeDirectScale(), kDirectT0);
    const std::string folder = mgr.createSession(kDirectUserA, kDirectUserB, makeScale(1, 1), kDirectT0);
    FolderTransferSessionInfo info;
    REQUIRE(mgr.getSession(direct, info));
    CHECK(info.kind == folder_kind_file);
    CHECK(info.folderName == "quote.xlsx");
    CHECK(info.totalFiles == 1);
    REQUIRE(mgr.getSession(folder, info));
    CHECK(info.kind == folder_kind_folder);
    removeDirectUsersSessions(mgr);
}

TEST_CASE("FTM27 configureDirectFile：小于 1 的配置取默认值（5 分钟、10 笔）")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    mgr.configureDirectFile(0, 0);
    CHECK(mgr.getDirectFileInviteTimeoutMs() == FolderTransferManager::kDefaultDirectFileInviteTimeoutMs);
    CHECK(mgr.getDirectFileMaxPerUser() == FolderTransferManager::kDefaultDirectFileMaxPerUser);
    CHECK(FolderTransferManager::kDefaultDirectFileInviteTimeoutMs == 5 * 60 * 1000);
    mgr.configureDirectFile(1234, 3);
    CHECK(mgr.getDirectFileInviteTimeoutMs() == 1234);
    CHECK(mgr.getDirectFileMaxPerUser() == 3);
    mgr.configureDirectFile(kTestDirectTimeoutMs, FolderTransferManager::kDefaultDirectFileMaxPerUser);
}

TEST_CASE("FTM28 终结闸门接受「已改为离线发送」(9)：直传响铃中改为离线发送时照常推送并销毁")
{
    FolderTransferManager& mgr = Singleton<FolderTransferManager>::Instance();
    removeDirectUsersSessions(mgr);
    configureLoose(mgr);
    const std::string sid = mgr.createSession(kDirectUserA, kDirectUserB, makeDirectScale(), kDirectT0);
    CHECK(mgr.finishAttempt(sid, 1, folder_state_to_offline, kDirectT0 + 1));
    FolderTransferSessionInfo info;
    REQUIRE(mgr.getSession(sid, info));
    CHECK(info.state == folder_state_to_offline);
    //重复终结照样被挡下
    CHECK_FALSE(mgr.finishAttempt(sid, 1, folder_state_to_offline, kDirectT0 + 2));
    //越界取值仍然拒绝
    CHECK_FALSE(mgr.finishAttempt(sid, 1, folder_state_to_offline + 1, kDirectT0 + 3));
    removeDirectUsersSessions(mgr);
}
