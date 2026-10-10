/**
 *  分享者一端的端到端联调用例（Suite SPL，标签 [.live][fileshare]），ShareProviderLiveTests.cpp
 *
 *  对应 docs/文件共享方案.md 第 1 期第 12 步的验证「访问者能列出目录」：分享者是真实运行的客户端（tmp/fileshare_env/clientA，
 *  MY0127，DLL 里的 CShareService / CShareProvider），访问者由本用例扮演（MY0308）：
 *    - SPL1 经 chatserver 发 2220 打开共享 9001，收到 2222 后以角色 10 连上 fileserver 中转；在 opentimeout 内收到分享者的
 *           sf_hello；列根目录、列子目录、查条目都得到正确结果；越界路径回 808；发 sf_bye 与 2221 结束访问会话。
 *           本用例第一次运行时发现：分享者比访问者先报到时，它立即发出的 sf_hello 被 fileserver 丢弃（对端通道尚未报到），
 *           访问者一直等不到；分享者因此改为在收到访问者的第一帧之前定时重发 sf_hello，等应答时要跳过重复的握手帧。
 *
 *  默认不参与常规测试。跑之前：
 *    1. python tmp/fileshare_env/seed_step12.py 造数据（共享 9001 的根是 tmp/fileshare_env/share_root，内有 docs 目录与 readme.txt）；
 *    2. 起 tmp/fileshare_env 的服务端与 clientA（start_server.ps1、start_client.ps1 clientA），等 clientA 登录并报告 2210；
 *    3. FlamingoUnit.exe "[fileshare]"
 *  第 13 步的访问者端做好之后，同一条链路改由两个真实客户端互相验证，本用例仍保留用来单独检查分享者一端。
 *
 *  第 2 期整体验收（第 6 步）在本文件加了 SP2A2（标签 [.live][fileshare][p2accept]）：同样由本用例扮演访问者、直接发
 *  控制帧（绕开访问者一端自己的路径检查，证明的是分享者一端的防护），对共享 9008（全部写权限，seed_p2_write.py）用
 *  构造的相对路径（..、绝对路径、目录联接、能建出来时还有符号链接）列目录、查条目、新建、重命名、移动、删除、搜索、
 *  取缩略图，全部回 808，共享根之外的文件原样保留、也没有建出新文件。
 *
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "../../Source/Tests/LiveChatClient.h"
#include "../../Source/Feature/FolderTransfer/FolderRelayClient.h"
#include "../../Source/net/Protocol.h"
#include "../Engine/ShareControlFraming.h"
#include "../Engine/ShareProtocolCodec.h"
#include "../Engine/ShareWinPath.h"
#include "ShareTestFs.h"

using namespace livetest;
using namespace fileshare;

namespace {

//tmp/fileshare_env 测试服务端的 chatserver 端口
const unsigned short kSplChatPort = 25000;

//访问者账号与密码；共享 9001 的分享者是 MY0127（userid 5127），成员是 MY0308
const char* const kSplVisitorAccount = "MY0308";
const char* const kSplPassword = "perf1234";
const char* const kSplOpenRequest = "{\"shareid\":\"9001\"}";

//等信令应答与推送的最长时间（毫秒）
const int kSplSignalWaitMs = 5000;

//等分享者 sf_hello 的最长时间（毫秒）：分享者在后台线程连接中转，取服务端 opentimeout 的默认值
const int kSplHelloWaitMs = 15000;

//等应用帧应答的最长时间（毫秒）
const int kSplFrameWaitMs = 5000;

//共享根里 readme.txt 的字节数（见 seed_step12.py 写入的内容）
const int64_t kSplReadmeBytes = 30;

//几个请求号
const int64_t kReqListRoot = 1;
const int64_t kReqListDocs = 2;
const int64_t kReqStat = 3;
const int64_t kReqBadPath = 4;

//收集中转接收线程交来的应用帧，供测试线程按超时等待
class FrameCollector : public foldertransfer::FolderRelayClientHandler
{
public:
    FrameCollector()
        : m_closed(false)
    {
    }

    virtual void OnFolderRelayAppFrame(const unsigned char* data, size_t len) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_frames.push_back(std::string(reinterpret_cast<const char*>(data), len));
        m_cv.notify_all();
    }

    virtual void OnFolderRelayClosed() override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_closed = true;
        m_cv.notify_all();
    }

    //等下一个不是 sf_hello 的帧：分享者在收到访问者的第一帧之前会重发 sf_hello，等应答时跳过这些重复的握手帧
    bool WaitReply(ShareControlMessage& out, int timeoutMs)
    {
        while (WaitMessage(out, timeoutMs))
        {
            if (out.m_type != sf_hello)
                return true;
        }
        return false;
    }

    //等下一帧并解码；超时、连接断开或解码失败返回 false
    bool WaitMessage(ShareControlMessage& out, int timeoutMs)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (m_frames.empty() && !m_closed)
        {
            if (m_cv.wait_until(lock, deadline) == std::cv_status::timeout)
                break;
        }
        if (m_frames.empty())
            return false;
        const std::string frame = m_frames.front();
        m_frames.pop_front();
        return ShareControlFraming::DecodeFrame(frame.data(), frame.size(), out);
    }

private:
    std::mutex              m_mutex;    //保护以下成员
    std::condition_variable m_cv;       //有新帧或连接断开时通知
    std::deque<std::string> m_frames;   //尚未取走的应用帧
    bool                    m_closed;   //连接是否已异常断开
};

//编码并经中转发出一个请求
void SendRequest(foldertransfer::FolderRelayClient& relay, int type, int64_t reqId, const std::string& path)
{
    ShareControlMessage req;
    req.m_type = type;
    req.m_reqId = reqId;
    req.m_path = path;
    std::string frame;
    REQUIRE(ShareControlFraming::BuildFrame(req, frame));
    REQUIRE(relay.SendAppFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size()));
}

} // namespace

TEST_CASE("SPL1 访问者经中转列出分享者的目录", "[.live][fileshare]")
{
    LiveChatClient visitor;
    std::string reason;
    const bool visitorLoggedIn = visitor.ConnectAndLogin(kSplChatPort, kTestServerPubKey, kSplVisitorAccount, kSplPassword, reason);
    INFO("login failed: " << reason);
    REQUIRE(visitorLoggedIn);

    //打开共享：应答带 accessid，随后服务端推 2222（访问者那一帧）
    REQUIRE(visitor.Send(cmd_share_open, kSplOpenRequest));
    std::string body;
    REQUIRE(visitor.WaitFor(cmd_share_open, body, kSplSignalWaitMs));
    int code = -1;
    std::string msg;
    std::string accessId;
    REQUIRE(ShareProtocolCodec::ParseOpenResponse(body, code, msg, accessId));
    INFO("open response: " << body);
    REQUIRE(code == 0);

    REQUIRE(visitor.WaitFor(cmd_share_relay_info, body, kSplSignalWaitMs));
    ShareRelayParams relayInfo;
    REQUIRE(ShareProtocolCodec::ParseRelayInfo(body, relayInfo));
    CHECK_FALSE(relayInfo.m_forOwner);
    CHECK(relayInfo.m_accessId == accessId);
    CHECK(relayInfo.m_transferId.empty());

    //以角色 10 连上中转，等分享者的 sf_hello
    unsigned char serverPubKey[net::IDENTITY_PUBLICKEY_BYTES] = { 0 };
    REQUIRE(net::parseServerPublicKey(kTestServerPubKey, serverPubKey));
    FrameCollector collector;
    foldertransfer::FolderRelayClient relay;
    REQUIRE(relay.Connect(relayInfo.m_relayIp, relayInfo.m_relayPort, relayInfo.m_token,
                          foldertransfer::kFolderControlLane, serverPubKey, &collector));

    ShareControlMessage hello;
    REQUIRE(collector.WaitMessage(hello, kSplHelloWaitMs));
    CHECK(hello.m_type == sf_hello);
    CHECK(hello.m_version == kShareControlVersion);
    CHECK(hello.m_kind == share_kind_folder);
    //文件夹共享不给根名（取自真实路径，2026-10-04 起），访问者用 2220 应答里的共享名称
    CHECK(hello.m_rootName.empty());
    CHECK(hello.m_perms == 0);

    //列根目录：先目录（docs）后文件（readme.txt）
    SendRequest(relay, sf_list, kReqListRoot, "");
    ShareControlMessage r;
    REQUIRE(collector.WaitReply(r, kSplFrameWaitMs));
    CHECK(r.m_type == sf_list_result);
    CHECK(r.m_reqId == kReqListRoot);
    CHECK(r.m_code == error_code_ok);
    REQUIRE(r.m_entries.size() == 2);
    CHECK(r.m_entries[0].m_name == "docs");
    CHECK(r.m_entries[0].m_isDir);
    CHECK(r.m_entries[1].m_name == "readme.txt");
    CHECK(r.m_entries[1].m_size == kSplReadmeBytes);
    CHECK_FALSE(r.m_more);

    //列子目录：空目录
    SendRequest(relay, sf_list, kReqListDocs, "docs");
    REQUIRE(collector.WaitReply(r, kSplFrameWaitMs));
    CHECK(r.m_reqId == kReqListDocs);
    CHECK(r.m_code == error_code_ok);
    CHECK(r.m_entries.empty());

    //查条目
    SendRequest(relay, sf_stat, kReqStat, "readme.txt");
    REQUIRE(collector.WaitReply(r, kSplFrameWaitMs));
    CHECK(r.m_type == sf_stat_result);
    CHECK(r.m_reqId == kReqStat);
    REQUIRE(r.m_entries.size() == 1);
    CHECK(r.m_entries[0].m_size == kSplReadmeBytes);

    //越界路径：808
    SendRequest(relay, sf_list, kReqBadPath, "../server");
    REQUIRE(collector.WaitReply(r, kSplFrameWaitMs));
    CHECK(r.m_reqId == kReqBadPath);
    CHECK(r.m_code == error_code_share_path_invalid);

    //结束：先 sf_bye，再 2221
    ShareControlMessage bye;
    bye.m_type = sf_bye;
    std::string frame;
    REQUIRE(ShareControlFraming::BuildFrame(bye, frame));
    REQUIRE(relay.SendAppFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size()));
    REQUIRE(visitor.Send(cmd_share_close_access, ShareProtocolCodec::BuildCloseAccessRequest(accessId, 0)));
    REQUIRE(visitor.WaitFor(cmd_share_close_access, body, kSplSignalWaitMs));
    REQUIRE(ShareProtocolCodec::ParseSimpleResponse(body, code, msg));
    CHECK(code == 0);
    relay.Close();
}

namespace {

//SP2A2：共享 9008 的打开请求与共享根
const char* const kSp2aOpenRequest = "{\"shareid\":\"9008\"}";
const wchar_t* const kSp2aRoot = L"E:\\flamingo\\tmp\\fileshare_env\\share_write";

//SP2A2：共享根之外的几个位置（共享根的上一级就是 tmp/fileshare_env）
const wchar_t* const kSp2aOutsideDir = L"E:\\flamingo\\tmp\\fileshare_env\\share_root";
const wchar_t* const kSp2aDecoy = L"E:\\flamingo\\tmp\\fileshare_env\\outside_decoy.txt";
const wchar_t* const kSp2aEvilFile = L"E:\\flamingo\\tmp\\fileshare_env\\evil.txt";
const wchar_t* const kSp2aEvilDir = L"E:\\flamingo\\tmp\\fileshare_env\\evil_dir";
const wchar_t* const kSp2aJunctionTemp = L"E:\\flamingo\\tmp\\fileshare_env\\sp2a_jtmp";

//SP2A2：本用例在共享根下用的目录名与其中的文件
const wchar_t* const kSp2aDirName = L"越界";
const size_t kSp2aFileBytes = 16;

//SP2A2：第一个请求号（之后逐个加一）
const int64_t kSp2aFirstReqId = 100;

//SP2A2：缩略图请求的长边
const int kSp2aThumbEdge = 96;

//CreateSymbolicLinkW 的两个标志（本工程的 _WIN32_WINNT 较低，头文件里没有它们）：目标是目录、不要求管理员权限（开发者模式）
const DWORD kSymlinkFlagDirectory = 0x1;
const DWORD kSymlinkAllowUnprivileged = 0x2;

typedef BOOLEAN (WINAPI* CreateSymbolicLinkFn)(LPCWSTR, LPCWSTR, DWORD);

//建符号链接（动态取函数）；当前账号没有权限、也没有打开开发者模式时返回 false
bool Sp2aMakeSymlink(const std::wstring& link, const std::wstring& target, bool isDir)
{
    CreateSymbolicLinkFn fn = reinterpret_cast<CreateSymbolicLinkFn>(
        ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "CreateSymbolicLinkW"));
    if (fn == NULL)
        return false;
    const DWORD flags = (isDir ? kSymlinkFlagDirectory : 0) | kSymlinkAllowUnprivileged;
    return fn(link.c_str(), target.c_str(), flags) != FALSE;
}

bool Sp2aExists(const std::wstring& path)
{
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

//建一个只有一个文件的小文件
bool Sp2aWriteFile(const std::wstring& path)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    const std::string data(kSp2aFileBytes, 'd');
    DWORD written = 0;
    const bool ok = ::WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, NULL) != FALSE;
    ::CloseHandle(h);
    return ok && written == data.size();
}

//写操作应答的结果码：多条目操作取第一条的结果，单条目取整体结果
int Sp2aOpCode(const ShareControlMessage& r)
{
    return r.m_items.empty() ? r.m_code : r.m_items[0].m_code;
}

//发一个请求并等它的应答；应答的请求号必须对得上
bool Sp2aRoundTrip(foldertransfer::FolderRelayClient& relay, FrameCollector& collector, const ShareControlMessage& req,
                   ShareControlMessage& reply)
{
    std::string frame;
    if (!ShareControlFraming::BuildFrame(req, frame))
        return false;
    if (!relay.SendAppFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size()))
        return false;
    while (collector.WaitReply(reply, kSplFrameWaitMs))
    {
        if (reply.m_reqId == req.m_reqId)
            return true;
    }
    return false;
}

//一次越界请求：类型、路径与附加参数
ShareControlMessage Sp2aRequest(int type, int64_t reqId, const std::string& path)
{
    ShareControlMessage req;
    req.m_type = type;
    req.m_reqId = reqId;
    req.m_path = path;
    return req;
}

} // namespace

TEST_CASE("SP2A2 构造的相对路径、目录联接与符号链接都不能越出共享根", "[.live][fileshare][p2accept]")
{
    //准备：共享根下的「越界」目录，里面一个普通文件与一个指向共享根之外的目录联接；共享根之外放一个诱饵文件
    const std::wstring dir = std::wstring(kSp2aRoot) + L"\\" + kSp2aDirName;
    sharetestfs::DeleteTree(dir);
    sharetestfs::DeleteTree(kSp2aJunctionTemp);
    REQUIRE(sharetestfs::MakeDirs(dir));
    REQUIRE(Sp2aWriteFile(dir + L"\\f.txt"));
    REQUIRE(Sp2aWriteFile(kSp2aDecoy));
    REQUIRE(sharetestfs::MakeJunction(dir + L"\\junction", kSp2aOutsideDir, kSp2aJunctionTemp));
    //符号链接要求开发者模式或管理员权限：建不出来时如实跳过这一部分
    const bool hasDirLink = Sp2aMakeSymlink(dir + L"\\slink", kSp2aOutsideDir, true);
    const bool hasFileLink = Sp2aMakeSymlink(dir + L"\\slinkfile", kSp2aDecoy, false);
    if (!hasDirLink || !hasFileLink)
        WARN("symbolic links could not be created (no privilege / developer mode off); the symlink part is skipped");

    LiveChatClient visitor;
    std::string reason;
    const bool visitorLoggedIn = visitor.ConnectAndLogin(kSplChatPort, kTestServerPubKey, kSplVisitorAccount, kSplPassword, reason);
    INFO("login failed: " << reason);
    REQUIRE(visitorLoggedIn);
    REQUIRE(visitor.Send(cmd_share_open, kSp2aOpenRequest));
    std::string body;
    REQUIRE(visitor.WaitFor(cmd_share_open, body, kSplSignalWaitMs));
    int code = -1;
    std::string msg;
    std::string accessId;
    REQUIRE(ShareProtocolCodec::ParseOpenResponse(body, code, msg, accessId));
    REQUIRE(code == 0);
    REQUIRE(visitor.WaitFor(cmd_share_relay_info, body, kSplSignalWaitMs));
    ShareRelayParams relayInfo;
    REQUIRE(ShareProtocolCodec::ParseRelayInfo(body, relayInfo));
    unsigned char serverPubKey[net::IDENTITY_PUBLICKEY_BYTES] = { 0 };
    REQUIRE(net::parseServerPublicKey(kTestServerPubKey, serverPubKey));
    FrameCollector collector;
    foldertransfer::FolderRelayClient relay;
    REQUIRE(relay.Connect(relayInfo.m_relayIp, relayInfo.m_relayPort, relayInfo.m_token, foldertransfer::kFolderControlLane,
                          serverPubKey, &collector));
    ShareControlMessage hello;
    REQUIRE(collector.WaitMessage(hello, kSplHelloWaitMs));
    REQUIRE(hello.m_type == sf_hello);

    int64_t reqId = kSp2aFirstReqId;
    ShareControlMessage r;
    const std::string sub = ShareWideToUtf8(kSp2aDirName);

    //列目录与查条目：上一级、经上一级绕回、绝对路径、诱饵文件、目录联接与其中的文件
    std::vector<std::string> listPaths;
    listPaths.push_back("..");
    listPaths.push_back(sub + "/../..");
    listPaths.push_back("C:/Windows");
    listPaths.push_back("/Windows");
    for (size_t i = 0; i < listPaths.size(); ++i)
    {
        const std::string& path = listPaths[i];
        INFO("sf_list " << path);
        REQUIRE(Sp2aRoundTrip(relay, collector, Sp2aRequest(sf_list, ++reqId, path), r));
        CHECK(r.m_code == error_code_share_path_invalid);
    }
    REQUIRE(Sp2aRoundTrip(relay, collector, Sp2aRequest(sf_stat, ++reqId, "../outside_decoy.txt"), r));
    CHECK(r.m_code == error_code_share_path_invalid);
    REQUIRE(Sp2aRoundTrip(relay, collector, Sp2aRequest(sf_list, ++reqId, sub + "/junction"), r));
    CHECK(r.m_code == error_code_share_path_invalid);
    REQUIRE(Sp2aRoundTrip(relay, collector, Sp2aRequest(sf_stat, ++reqId, sub + "/junction/readme.txt"), r));
    CHECK(r.m_code == error_code_share_path_invalid);

    //写操作：在共享根之外新建、把文件改名到上一级、移到上一级或目录联接里、删除共享根之外与联接里的文件
    REQUIRE(Sp2aRoundTrip(relay, collector, Sp2aRequest(sf_mkdir, ++reqId, "../evil_dir"), r));
    CHECK(Sp2aOpCode(r) == error_code_share_path_invalid);
    ShareControlMessage rename = Sp2aRequest(sf_rename, ++reqId, sub + "/f.txt");
    rename.m_newName = "../evil.txt";
    REQUIRE(Sp2aRoundTrip(relay, collector, rename, r));
    CHECK(Sp2aOpCode(r) == error_code_share_path_invalid);
    ShareControlMessage moveUp = Sp2aRequest(sf_move, ++reqId, std::string());
    moveUp.m_paths.push_back(sub + "/f.txt");
    moveUp.m_toDir = "..";
    REQUIRE(Sp2aRoundTrip(relay, collector, moveUp, r));
    CHECK(Sp2aOpCode(r) == error_code_share_path_invalid);
    ShareControlMessage moveIntoLink = Sp2aRequest(sf_move, ++reqId, std::string());
    moveIntoLink.m_paths.push_back(sub + "/f.txt");
    moveIntoLink.m_toDir = sub + "/junction";
    REQUIRE(Sp2aRoundTrip(relay, collector, moveIntoLink, r));
    CHECK(Sp2aOpCode(r) == error_code_share_path_invalid);
    ShareControlMessage del = Sp2aRequest(sf_delete, ++reqId, std::string());
    del.m_paths.push_back("../outside_decoy.txt");
    REQUIRE(Sp2aRoundTrip(relay, collector, del, r));
    CHECK(Sp2aOpCode(r) == error_code_share_path_invalid);
    ShareControlMessage delInLink = Sp2aRequest(sf_delete, ++reqId, std::string());
    delInLink.m_paths.push_back(sub + "/junction/readme.txt");
    REQUIRE(Sp2aRoundTrip(relay, collector, delInLink, r));
    CHECK(Sp2aOpCode(r) == error_code_share_path_invalid);

    //搜索与缩略图
    ShareControlMessage search = Sp2aRequest(sf_search, ++reqId, "..");
    search.m_keyword = "readme";
    REQUIRE(Sp2aRoundTrip(relay, collector, search, r));
    CHECK(r.m_code == error_code_share_path_invalid);
    ShareControlMessage searchLink = Sp2aRequest(sf_search, ++reqId, sub + "/junction");
    searchLink.m_keyword = "readme";
    REQUIRE(Sp2aRoundTrip(relay, collector, searchLink, r));
    CHECK(r.m_code == error_code_share_path_invalid);
    ShareControlMessage thumb = Sp2aRequest(sf_thumb, ++reqId, "../outside.png");
    thumb.m_maxEdge = kSp2aThumbEdge;
    REQUIRE(Sp2aRoundTrip(relay, collector, thumb, r));
    CHECK(r.m_code == error_code_share_path_invalid);

    //符号链接（建得出来时）：目录链接与文件链接都按越界处理
    if (hasDirLink && hasFileLink)
    {
        REQUIRE(Sp2aRoundTrip(relay, collector, Sp2aRequest(sf_list, ++reqId, sub + "/slink"), r));
        CHECK(r.m_code == error_code_share_path_invalid);
        REQUIRE(Sp2aRoundTrip(relay, collector, Sp2aRequest(sf_stat, ++reqId, sub + "/slinkfile"), r));
        CHECK(r.m_code == error_code_share_path_invalid);
    }

    //共享根之外的文件原样保留，也没有建出新文件
    CHECK(Sp2aExists(kSp2aDecoy));
    CHECK(Sp2aExists(std::wstring(kSp2aOutsideDir) + L"\\readme.txt"));
    CHECK(Sp2aExists(dir + L"\\f.txt"));
    CHECK_FALSE(Sp2aExists(kSp2aEvilFile));
    CHECK_FALSE(Sp2aExists(kSp2aEvilDir));
    CHECK_FALSE(Sp2aExists(std::wstring(kSp2aOutsideDir) + L"\\f.txt"));

    ShareControlMessage bye;
    bye.m_type = sf_bye;
    std::string frame;
    REQUIRE(ShareControlFraming::BuildFrame(bye, frame));
    REQUIRE(relay.SendAppFrame(reinterpret_cast<const unsigned char*>(frame.data()), frame.size()));
    REQUIRE(visitor.Send(cmd_share_close_access, ShareProtocolCodec::BuildCloseAccessRequest(accessId, 0)));
    REQUIRE(visitor.WaitFor(cmd_share_close_access, body, kSplSignalWaitMs));
    relay.Close();
    sharetestfs::DeleteTree(dir);
    ::DeleteFileW(kSp2aDecoy);
}
