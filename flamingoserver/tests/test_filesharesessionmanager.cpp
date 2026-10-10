/**
 *  文件共享访问会话与传输子会话管理单元测试（Suite SSM），test_filesharesessionmanager.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 5 步，2026-10-02 与用户确认的用例）：
 *    - SSM1 配置的取值修正：通道条数限定在 1~4，各上限至少为 1，缺失取默认值；
 *    - SSM2 打开访问会话：id 带 share- 前缀且不重复，总数到顶返回服务器繁忙，令牌角色 9 / 10、uid 对调；
 *    - SSM3 申请传输的两道闸门：每个共享的人数上限（813）、同一访问会话重复申请（813）、
 *           整台服务器的传输数（814），两者同时到顶先报 813；
 *    - SSM4 传输子会话 id 的形式与唯一性，结束传输释放名额，结束访问会话连带结束传输；
 *    - SSM5 按用户、按共享反查；
 *    - SSM6 批量收尾：取消共享、移除成员、连接断开，各自只结束该结束的会话并返回清单；
 *    - SSM7 多线程同时打开、申请、关闭，结束后计数与名额不错乱。
 *  每个用例各用一个独立的 FileShareSessionManager 实例，不碰进程内的单例。
 *  balloonwj@qq.com   2026-10-02
 */
#include <string>
#include <thread>
#include <vector>
#include <atomic>

#include "doctest.h"
#include "../chatserversrc/FileShareSessionManager.h"
#include "../chatserversrc/FileShareProtocol.h"   //share_transfer_direction
#include "../chatserversrc/Protocol.h"
#include "../utils/RelayToken.h"

namespace {

//测试用的令牌密钥
const char* const kTestRelayKey = "fsm-relay-key";

//测试用的当前时间（Unix 毫秒）
const int64_t kNowMs = 1759370000000LL;

//SSM7 的线程数与每条线程的轮数
const int kStressThreads = 8;
const int kStressRounds = 200;

//建一个配置好的管理器：访问会话上限 maxAccess、传输上限 maxTransfers、每共享人数上限 perShare
void configureManager(FileShareSessionManager& mgr, int maxAccess, int maxTransfers, int perShare)
{
    FileShareSessionConfig cfg;
    cfg.m_relayTokenKey = kTestRelayKey;
    cfg.m_relayHost = "10.0.0.1";
    cfg.m_relayPort = 20001;
    cfg.m_maxAccess = maxAccess;
    cfg.m_maxTransfers = maxTransfers;
    cfg.m_maxTransferPerShare = perShare;
    mgr.configure(cfg);
}

//打开一个访问会话并要求成功，返回会话 id
std::string mustOpen(FileShareSessionManager& mgr, int64_t shareId, int64_t ownerId, ShareConnKey ownerConn,
                     int64_t visitorId, ShareConnKey visitorConn)
{
    std::string accessId;
    REQUIRE(mgr.openAccess(shareId, ownerId, ownerConn, visitorId, visitorConn, kNowMs, accessId) == error_code_ok);
    return accessId;
}

//SSM7 的工作线程：反复打开访问会话、申请传输、结束传输、结束访问会话
void stressWorker(FileShareSessionManager* mgr, int threadIndex, std::atomic<int>* failures)
{
    const int64_t shareId = 100 + threadIndex % 2;
    const int64_t visitorId = 20000 + threadIndex;
    for (int round = 0; round < kStressRounds; ++round)
    {
        std::string accessId;
        if (mgr->openAccess(shareId, 10001, 1, visitorId, 2 + threadIndex, kNowMs, accessId) != error_code_ok)
            continue;

        std::string transferId;
        int code = mgr->requestTransfer(accessId, visitorId, share_transfer_download, transferId);
        if (code == error_code_ok)
        {
            ShareAccessInfo info;
            if (!mgr->closeTransfer(transferId, info))
                failures->fetch_add(1);
        }

        ShareAccessInfo closed;
        if (!mgr->closeAccess(accessId, closed))
            failures->fetch_add(1);
    }
}

} // namespace

TEST_CASE("SSM1 配置的取值修正")
{
    FileShareSessionManager mgr;

    FileShareSessionConfig bad;
    bad.m_lanes = FileShareSessionManager::kShareMaxLanes + 5;
    bad.m_maxAccess = 0;
    bad.m_maxTransfers = -3;
    bad.m_maxTransferPerShare = 0;
    bad.m_openTimeoutMs = 0;
    bad.m_idleTimeoutMs = -1;
    bad.m_searchMaxResults = 0;
    bad.m_searchTimeoutMs = 0;
    mgr.configure(bad);
    const FileShareSessionConfig& got = mgr.getConfig();
    CHECK(got.m_lanes == FileShareSessionManager::kShareMaxLanes);
    CHECK(got.m_maxAccess == FileShareSessionManager::kDefaultMaxAccess);
    CHECK(got.m_maxTransfers == FileShareSessionManager::kDefaultMaxTransfers);
    CHECK(got.m_maxTransferPerShare == FileShareSessionManager::kDefaultMaxTransferPerShare);
    CHECK(got.m_openTimeoutMs == FileShareSessionManager::kDefaultOpenTimeoutMs);
    CHECK(got.m_idleTimeoutMs == FileShareSessionManager::kDefaultIdleTimeoutMs);
    CHECK(got.m_searchMaxResults == FileShareSessionManager::kDefaultSearchMaxResults);
    CHECK(got.m_searchTimeoutMs == FileShareSessionManager::kDefaultSearchTimeoutMs);

    FileShareSessionConfig zeroLanes;
    zeroLanes.m_lanes = 0;
    mgr.configure(zeroLanes);
    CHECK(mgr.getConfig().m_lanes == FileShareSessionManager::kDefaultLanes);

    //默认上限按平台取值（方案 6.4）：Windows 受 select 每线程 64 条连接所限，Linux 用 epoll
#ifdef WIN32
    CHECK(FileShareSessionManager::kDefaultMaxAccess == 30);
    CHECK(FileShareSessionManager::kDefaultMaxTransfers == 12);
#else
    CHECK(FileShareSessionManager::kDefaultMaxAccess == 1000);
    CHECK(FileShareSessionManager::kDefaultMaxTransfers == 256);
#endif

    //结构体自身的默认值就是各项默认值
    FileShareSessionConfig defaults;
    mgr.configure(defaults);
    CHECK(mgr.getConfig().m_lanes == FileShareSessionManager::kDefaultLanes);
    CHECK(mgr.getConfig().m_maxTransferPerShare == FileShareSessionManager::kDefaultMaxTransferPerShare);
}

TEST_CASE("SSM2 打开访问会话：id、总数上限与令牌")
{
    FileShareSessionManager mgr;
    configureManager(mgr, 2, 10, 5);

    std::string a1 = mustOpen(mgr, 7, 10001, 1, 10002, 2);
    std::string a2 = mustOpen(mgr, 7, 10001, 1, 10003, 3);
    CHECK(a1.compare(0, 6, "share-") == 0);
    CHECK(a2.compare(0, 6, "share-") == 0);
    CHECK(a1 != a2);
    //中转 sessionid 不得含 "." 与 "|"（RelayToken 的分隔符）
    CHECK(a1.find('.') == std::string::npos);
    CHECK(a1.find('|') == std::string::npos);
    CHECK(mgr.getAccessCount() == 2);

    //总数到顶
    std::string a3;
    CHECK(mgr.openAccess(7, 10001, 1, 10004, 4, kNowMs, a3) == error_code_share_server_busy);
    CHECK(a3.empty());
    CHECK(mgr.getAccessCount() == 2);

    ShareAccessInfo info;
    REQUIRE(mgr.getAccess(a1, info));
    CHECK(info.m_shareId == 7);
    CHECK(info.m_ownerId == 10001);
    CHECK(info.m_visitorId == 10002);
    CHECK(info.m_ownerConn == 1);
    CHECK(info.m_visitorConn == 2);
    CHECK(info.m_transferId.empty());

    //令牌：两枚都能用同一把密钥校验通过，角色 9 / 10，uid 与 peeruid 对调，sessionid 就是会话 id
    std::string ownerToken;
    std::string visitorToken;
    mgr.signTokens(a1, 10001, 10002, kNowMs, ownerToken, visitorToken);
    RelayTokenPayload owner;
    RelayTokenPayload visitor;
    REQUIRE(RelayToken::verify(ownerToken, kTestRelayKey, kNowMs, owner));
    REQUIRE(RelayToken::verify(visitorToken, kTestRelayKey, kNowMs, visitor));
    CHECK(owner.sessionid == a1);
    CHECK(visitor.sessionid == a1);
    CHECK(owner.role == RelayToken::role_share_owner);
    CHECK(visitor.role == RelayToken::role_share_visitor);
    CHECK(owner.uid == 10001);
    CHECK(owner.peeruid == 10002);
    CHECK(visitor.uid == 10002);
    CHECK(visitor.peeruid == 10001);
    //有效期为 kRelayTokenTtlMs：到期前还能用，到期即失效
    RelayTokenPayload tmp;
    CHECK(RelayToken::verify(ownerToken, kTestRelayKey, kNowMs + FileShareSessionManager::kRelayTokenTtlMs - 1, tmp));
    CHECK_FALSE(RelayToken::verify(ownerToken, kTestRelayKey, kNowMs + FileShareSessionManager::kRelayTokenTtlMs, tmp));
}

TEST_CASE("SSM3 申请传输的两道闸门")
{
    SUBCASE("每个共享同时传输的人数上限，以及同一访问会话的重复申请")
    {
        FileShareSessionManager mgr;
        configureManager(mgr, 50, 50, 2);

        std::string a1 = mustOpen(mgr, 7, 10001, 1, 10002, 2);
        std::string a2 = mustOpen(mgr, 7, 10001, 1, 10003, 3);
        std::string a3 = mustOpen(mgr, 7, 10001, 1, 10004, 4);
        std::string other = mustOpen(mgr, 8, 10001, 1, 10005, 5);

        std::string t1;
        std::string t2;
        std::string t3;
        REQUIRE(mgr.requestTransfer(a1, 10002, share_transfer_download, t1) == error_code_ok);
        REQUIRE(mgr.requestTransfer(a2, 10003, share_transfer_upload, t2) == error_code_ok);
        //第 3 个人：该共享已有 2 人在传
        CHECK(mgr.requestTransfer(a3, 10004, share_transfer_download, t3) == error_code_share_transfer_busy);
        //同一访问会话重复申请
        std::string again;
        CHECK(mgr.requestTransfer(a1, 10002, share_transfer_download, again) == error_code_share_transfer_busy);
        //别的共享不受影响
        std::string t4;
        CHECK(mgr.requestTransfer(other, 10005, share_transfer_download, t4) == error_code_ok);
        CHECK(mgr.getTransferCount() == 3);
    }

    SUBCASE("申请者不是访问者、会话不存在、方向非法")
    {
        FileShareSessionManager mgr;
        configureManager(mgr, 50, 50, 5);
        std::string a1 = mustOpen(mgr, 7, 10001, 1, 10002, 2);
        std::string t;
        CHECK(mgr.requestTransfer(a1, 10009, share_transfer_download, t) == error_code_permission_denied);
        CHECK(mgr.requestTransfer("share-nope", 10002, share_transfer_download, t) == error_code_share_not_exist);
        CHECK(mgr.requestTransfer(a1, 10002, 0, t) == error_code_bad_request);
    }

    SUBCASE("整台服务器的传输数上限；两道闸门同时到顶时先报 813")
    {
        FileShareSessionManager mgr;
        configureManager(mgr, 50, 2, 1);
        std::string a1 = mustOpen(mgr, 7, 10001, 1, 10002, 2);
        std::string a2 = mustOpen(mgr, 8, 10001, 1, 10003, 3);
        std::string a3 = mustOpen(mgr, 9, 10001, 1, 10004, 4);
        std::string a4 = mustOpen(mgr, 7, 10001, 1, 10006, 6);
        std::string t;
        REQUIRE(mgr.requestTransfer(a1, 10002, share_transfer_download, t) == error_code_ok);
        REQUIRE(mgr.requestTransfer(a2, 10003, share_transfer_download, t) == error_code_ok);
        //共享 9 上没人在传，但服务器已到顶
        CHECK(mgr.requestTransfer(a3, 10004, share_transfer_download, t) == error_code_share_server_busy);
        //共享 7 上已有 1 人在传（每共享上限 1），服务器也到顶：先报 813
        CHECK(mgr.requestTransfer(a4, 10006, share_transfer_download, t) == error_code_share_transfer_busy);
    }
}

TEST_CASE("SSM4 传输子会话 id、名额释放与连带结束")
{
    FileShareSessionManager mgr;
    configureManager(mgr, 50, 50, 1);
    std::string a1 = mustOpen(mgr, 7, 10001, 1, 10002, 2);
    std::string a2 = mustOpen(mgr, 7, 10001, 1, 10003, 3);

    std::string t1;
    REQUIRE(mgr.requestTransfer(a1, 10002, share_transfer_upload, t1) == error_code_ok);
    CHECK(t1.compare(0, a1.size() + 2, a1 + "-t") == 0);
    CHECK(t1.find('.') == std::string::npos);
    CHECK(t1.find('|') == std::string::npos);

    ShareAccessInfo info;
    REQUIRE(mgr.getAccess(a1, info));
    CHECK(info.m_transferId == t1);
    CHECK(info.m_transferDirection == share_transfer_upload);

    //名额被占：第二个人申请不到；结束第一笔后就能申请到
    std::string t2;
    CHECK(mgr.requestTransfer(a2, 10003, share_transfer_download, t2) == error_code_share_transfer_busy);
    ShareAccessInfo closedOwner;
    REQUIRE(mgr.closeTransfer(t1, closedOwner));
    CHECK(closedOwner.m_accessId == a1);
    CHECK(closedOwner.m_transferId == t1);
    CHECK(mgr.getTransferCount() == 0);
    CHECK_FALSE(mgr.closeTransfer(t1, closedOwner));
    REQUIRE(mgr.requestTransfer(a2, 10003, share_transfer_download, t2) == error_code_ok);
    CHECK(t2 != t1);

    //结束访问会话连带结束其传输
    ShareAccessInfo closed;
    REQUIRE(mgr.closeAccess(a2, closed));
    CHECK(closed.m_transferId == t2);
    CHECK(mgr.getTransferCount() == 0);
    CHECK(mgr.getAccessCount() == 1);
    CHECK_FALSE(mgr.closeAccess(a2, closed));
    ShareAccessInfo none;
    CHECK_FALSE(mgr.closeTransfer(t2, none));
}

TEST_CASE("SSM5 按用户、按共享反查")
{
    FileShareSessionManager mgr;
    configureManager(mgr, 50, 50, 5);
    mustOpen(mgr, 7, 10001, 1, 10002, 2);
    mustOpen(mgr, 7, 10001, 1, 10003, 3);
    mustOpen(mgr, 8, 10002, 2, 10001, 1);

    CHECK(mgr.findByShare(7).size() == 2);
    CHECK(mgr.findByShare(8).size() == 1);
    CHECK(mgr.findByShare(9).empty());

    //10001 是共享 7 的分享者，又是共享 8 的访问者
    CHECK(mgr.findByUser(10001).size() == 3);
    //10002 是共享 7 的访问者，又是共享 8 的分享者
    CHECK(mgr.findByUser(10002).size() == 2);
    CHECK(mgr.findByUser(10003).size() == 1);
    CHECK(mgr.findByUser(10099).empty());
}

TEST_CASE("SSM6 批量收尾：取消共享、移除成员、连接断开")
{
    FileShareSessionManager mgr;
    configureManager(mgr, 50, 50, 5);
    std::string a1 = mustOpen(mgr, 7, 10001, 1, 10002, 2);
    std::string a2 = mustOpen(mgr, 7, 10001, 1, 10003, 3);
    std::string a3 = mustOpen(mgr, 8, 10001, 1, 10002, 2);
    std::string a4 = mustOpen(mgr, 9, 10004, 4, 10005, 5);
    std::string t1;
    REQUIRE(mgr.requestTransfer(a1, 10002, share_transfer_download, t1) == error_code_ok);

    SUBCASE("取消共享 7：只结束共享 7 的两个会话，连带结束其传输")
    {
        std::vector<ShareAccessInfo> closed = mgr.closeAllForShare(7);
        CHECK(closed.size() == 2);
        CHECK(mgr.getAccessCount() == 2);
        CHECK(mgr.getTransferCount() == 0);
        CHECK(mgr.findByShare(7).empty());
    }

    SUBCASE("把 10002 移出共享 7：只结束他在共享 7 上的会话")
    {
        std::vector<ShareAccessInfo> closed = mgr.closeForMember(7, 10002);
        REQUIRE(closed.size() == 1);
        CHECK(closed[0].m_accessId == a1);
        CHECK(closed[0].m_transferId == t1);
        ShareAccessInfo info;
        CHECK(mgr.getAccess(a3, info));   //他在共享 8 上的会话不受影响
        CHECK(mgr.getAccess(a2, info));
    }

    SUBCASE("连接 1（分享者 10001 的连接）断开：结束它作为分享者一端的全部会话")
    {
        std::vector<ShareAccessInfo> closed = mgr.closeForConnection(1);
        CHECK(closed.size() == 3);
        CHECK(mgr.getAccessCount() == 1);
        ShareAccessInfo info;
        CHECK(mgr.getAccess(a4, info));
    }

    SUBCASE("连接 2（访问者 10002 的连接）断开：结束它作为访问者一端的会话")
    {
        std::vector<ShareAccessInfo> closed = mgr.closeForConnection(2);
        CHECK(closed.size() == 2);
        CHECK(mgr.getAccessCount() == 2);
        CHECK(mgr.getTransferCount() == 0);
    }

    SUBCASE("没有会话的共享、成员、连接：返回空清单")
    {
        CHECK(mgr.closeAllForShare(99).empty());
        CHECK(mgr.closeForMember(7, 10099).empty());
        CHECK(mgr.closeForConnection(99).empty());
        CHECK(mgr.getAccessCount() == 4);
    }
}

TEST_CASE("SSM7 多线程同时打开、申请、关闭后计数与名额不错乱")
{
    FileShareSessionManager mgr;
    configureManager(mgr, kStressThreads, 3, 2);

    std::atomic<int> failures(0);
    std::vector<std::thread> threads;
    for (int i = 0; i < kStressThreads; ++i)
        threads.push_back(std::thread(stressWorker, &mgr, i, &failures));
    for (size_t i = 0; i < threads.size(); ++i)
        threads[i].join();

    CHECK(failures.load() == 0);
    CHECK(mgr.getAccessCount() == 0);
    CHECK(mgr.getTransferCount() == 0);

    //名额全部释放：此时还能把上限用满
    std::string a1 = mustOpen(mgr, 100, 10001, 1, 30001, 9);
    std::string a2 = mustOpen(mgr, 100, 10001, 1, 30002, 10);
    std::string t;
    CHECK(mgr.requestTransfer(a1, 30001, share_transfer_download, t) == error_code_ok);
    CHECK(mgr.requestTransfer(a2, 30002, share_transfer_download, t) == error_code_ok);
}
