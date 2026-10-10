/**
 *  第 4 期：浏览窗口关闭后传输在后台继续的单元测试（Suite SBG，标签 [sharebg]），ShareBackgroundTests.cpp
 *  覆盖 docs/文件共享方案.md 第 14.5 节：
 *    - SBG1 访问会话未就绪时发起下载：发 2220 并排队、不发 2224；会话就绪（收到 sf_hello）后发出 2224；
 *    - SBG2 窗口关闭时有传输：不发 sf_bye 与 2221，访问状态机与传输都还在、不再回调已关闭的窗口；服务端结束这笔传输后
 *           结束监听收到结果（不是用户取消）；下一次定时检查才发 2221 并销毁访问状态机；
 *    - SBG3 窗口重新打开：接回同一个访问状态机（已就绪），AdoptVisitorTransfers 交回名称与状态，此后结束回调给新窗口；
 *    - SBG4 用户点「取消」的结果标记为用户取消；退出登录中断的不是，且退出登录后访问状态机全部销毁；
 *    - SBG5 会话打开失败：排队的传输以失败原因结束，结束监听收到同一原因；没有窗口、没有传输的访问状态机在定时检查时销毁；
 *    - SBG6 浏览窗口没开时重试：新建不挂窗口的访问状态机并排队，打开成功后发出 2224；
 *    - SBG7 拖出下载的结束回调：传输结束时调用一次，与观察者是否还在无关；结束监听收到的结果标记为拖出。
 *  访问状态机经 AcquireVisitor 取得后直接喂给它中转连接结果与 sf_hello（测试不起中转服务器）；业务总控没有通知窗口，
 *  真实访问通道连本机一个不监听的端口失败的事件被丢弃，不影响用例。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <memory>
#include <string>
#include <vector>

#include "../Engine/ShareService.h"
#include "../Engine/ShareControlFraming.h"
#include "../../Source/net/Protocol.h"
#include "ShareNoticeTestHost.h"
#include "ShareTestFs.h"

using namespace fileshare;

namespace {

//本套用例在测试程序目录下建的个人目录（每个用例开始前整个删掉）
const wchar_t* const kSbgTestDir = L"sbg_test";

//用例里的共享 id、访问会话 id、传输子会话 id
const int64_t kShareId = 9101;
const char* const kAccessId = "share-a-9101";
const char* const kTransferId = "share-t-9101";

//分享者不在线（服务端结束传输的原因）
const int kOwnerGone = error_code_share_unavailable;

//一份下载请求：一个文件
std::vector<ShareTransferEntry> OneFile()
{
    ShareTransferEntry e;
    e.m_path = "docs/a.txt";
    e.m_size = 100;
    return std::vector<ShareTransferEntry>(1, e);
}

//带名称的传输附加信息
ShareTransferOptions Labeled(const wchar_t* label)
{
    ShareTransferOptions o;
    o.m_label = label;
    return o;
}

//一个已登录的业务总控。观察者桩都声明在业务总控之前：析构时业务总控先 Detach（会结束全部传输并回调它们）
struct BgFixture
{
    noticetest::VisitorRecorder    m_window;     //第一个浏览窗口（访问状态机的观察者）
    noticetest::VisitorRecorder    m_window2;    //重新打开的浏览窗口
    noticetest::TransferRecorder   m_xfer;       //第一个浏览窗口（传输的观察者）
    noticetest::TransferRecorder   m_xfer2;      //重新打开的浏览窗口（传输的观察者）
    noticetest::FinishRecorder     m_finish;     //结束监听
    noticetest::ActivityRecorder   m_activity;   //分享者操作的监听
    noticetest::NoticeTestHost     m_host;       //宿主桩
    CShareService                  m_service;    //被测的业务总控

    BgFixture()
    {
        const std::wstring base = sharetestfs::ExeDir() + L"\\" + kSbgTestDir;
        sharetestfs::DeleteTree(base);
        sharetestfs::MakeDirs(base);
        m_host.m_personalDir = base + L"\\";
        m_service.Attach(&m_host, NULL, 0);
        m_service.OnLoggedIn();
        m_service.SetNoticeListeners(&m_finish, &m_activity);
    }

    ~BgFixture()
    {
        m_service.SetNoticeListeners(NULL, NULL);
        m_service.Detach();
    }

    //把正在打开（已发 2220）的访问状态机推到就绪：2220 成功、2222、中转连上、收到 sf_hello
    void OpenToReady(CShareVisitor* visitor)
    {
        REQUIRE(visitor->GetState() == share_visitor_opening);
        m_service.HandleServerFrame(cmd_share_open, m_host.LastSeq(cmd_share_open), noticetest::OpenOk(kAccessId));
        m_service.HandleServerFrame(cmd_share_relay_info, 0, noticetest::VisitorRelay(kAccessId));
        REQUIRE(visitor->GetState() == share_visitor_connecting);
        const int64_t now = static_cast<int64_t>(::GetTickCount64());
        visitor->OnRelayConnected(true, now);
        ShareControlMessage hello;
        hello.m_type = sf_hello;
        hello.m_kind = share_kind_folder;
        hello.m_perms = share_perm_upload;
        visitor->OnFrame(hello, now);
        REQUIRE(visitor->GetState() == share_visitor_ready);
    }

    //发起一笔下载并让它拿到 transferid（访问状态机须已就绪）
    uint32_t StartAcceptedDownload(IShareTransferObserver* observer)
    {
        const int before = m_host.Count(cmd_share_request_transfer);
        const uint32_t id = m_service.StartDownload(kShareId, OneFile(), L"D:\\dl", observer, Labeled(L"a.txt"));
        REQUIRE(id != 0);
        REQUIRE(m_host.Count(cmd_share_request_transfer) == before + 1);
        m_service.HandleServerFrame(cmd_share_request_transfer, m_host.LastSeq(cmd_share_request_transfer),
                                    noticetest::TransferOk(kTransferId));
        return id;
    }
};

} // namespace

TEST_CASE("SBG1 download requested before the access session is ready waits for it", "[fileshare][sharebg]")
{
    BgFixture f;
    CShareVisitor* visitor = f.m_service.AcquireVisitor(kShareId, &f.m_window);
    REQUIRE(visitor != NULL);
    REQUIRE(visitor->GetState() == share_visitor_idle);

    //访问会话还没打开：业务总控发 2220 并排队，不发 2224
    const uint32_t id = f.m_service.StartDownload(kShareId, OneFile(), L"D:\\dl", &f.m_xfer, Labeled(L"a.txt"));
    REQUIRE(id != 0);
    CHECK(f.m_host.Count(cmd_share_open) == 1);
    CHECK(f.m_host.Count(cmd_share_request_transfer) == 0);
    CHECK(f.m_service.IsTransferQueued(id));
    CHECK(f.m_service.GetTransferCount(false) == 1);

    //就绪之后发出
    f.OpenToReady(visitor);
    CHECK(f.m_host.Count(cmd_share_request_transfer) == 1);
    CHECK_FALSE(f.m_service.IsTransferQueued(id));
    CHECK(f.m_host.LastBody(cmd_share_request_transfer).find(kAccessId) != std::string::npos);
}

TEST_CASE("SBG2 closing the browser keeps the access session while a transfer runs", "[fileshare][sharebg]")
{
    BgFixture f;
    CShareVisitor* visitor = f.m_service.AcquireVisitor(kShareId, &f.m_window);
    REQUIRE(visitor != NULL);
    visitor->List(std::string(), static_cast<int64_t>(::GetTickCount64()));
    f.OpenToReady(visitor);
    const uint32_t id = f.StartAcceptedDownload(&f.m_xfer);

    //窗口关闭：访问会话保留，不发 2221
    const size_t statesBefore = f.m_window.m_states.size();
    f.m_service.ReleaseVisitor(kShareId);
    CHECK(f.m_host.Count(cmd_share_close_access) == 0);
    CHECK(f.m_service.HasVisitor(kShareId));
    CHECK_FALSE(f.m_service.IsVisitorAttached(kShareId));
    CHECK(f.m_service.GetTransferCount(false) == 1);
    CHECK(visitor->GetState() == share_visitor_ready);

    //定时检查：还有传输，不回收
    f.m_service.OnTimer();
    CHECK(f.m_service.HasVisitor(kShareId));
    CHECK(f.m_host.Count(cmd_share_close_access) == 0);

    //服务端结束这笔传输（分享者下线）：结束监听收到结果，已关闭的窗口不再收到回调
    f.m_service.HandleServerFrame(cmd_share_access_state_notify, 0, noticetest::AccessEnded(kAccessId, kTransferId, kOwnerGone));
    REQUIRE(f.m_finish.m_results.size() == 1);
    const ShareVisitorTransferResult& r = f.m_finish.m_results[0];
    CHECK(r.m_localId == id);
    CHECK(r.m_shareId == kShareId);
    CHECK(r.m_direction == share_transfer_download);
    CHECK(r.m_code == kOwnerGone);
    CHECK_FALSE(r.m_userCanceled);
    CHECK_FALSE(r.m_dragOut);
    CHECK(r.m_label == L"a.txt");
    CHECK(r.m_shareName == "team docs");
    CHECK(r.m_targetDir == L"D:\\dl");
    REQUIRE(r.m_downloadItems.size() == 1);
    CHECK(r.m_downloadItems[0].m_path == "docs/a.txt");
    CHECK(f.m_xfer.m_finished.empty());
    CHECK(f.m_window.m_states.size() == statesBefore);

    //下一次定时检查：没有窗口也没有传输了，结束访问会话（发 2221）并销毁
    f.m_service.OnTimer();
    CHECK(f.m_host.Count(cmd_share_close_access) == 1);
    CHECK(f.m_host.LastBody(cmd_share_close_access).find(kAccessId) != std::string::npos);
    CHECK_FALSE(f.m_service.HasVisitor(kShareId));
}

TEST_CASE("SBG3 reopening the browser adopts the background transfer", "[fileshare][sharebg]")
{
    BgFixture f;
    CShareVisitor* visitor = f.m_service.AcquireVisitor(kShareId, &f.m_window);
    REQUIRE(visitor != NULL);
    visitor->List(std::string(), static_cast<int64_t>(::GetTickCount64()));
    f.OpenToReady(visitor);
    const uint32_t id = f.StartAcceptedDownload(&f.m_xfer);
    f.m_service.ReleaseVisitor(kShareId);

    //重新打开：同一个访问状态机，已经就绪
    CShareVisitor* again = f.m_service.AcquireVisitor(kShareId, &f.m_window2);
    CHECK(again == visitor);
    CHECK(again->GetState() == share_visitor_ready);
    CHECK(f.m_service.IsVisitorAttached(kShareId));
    const std::vector<ShareTransferSnapshot> adopted = f.m_service.AdoptVisitorTransfers(kShareId, &f.m_xfer2);
    REQUIRE(adopted.size() == 1);
    CHECK(adopted[0].m_localId == id);
    CHECK(adopted[0].m_direction == share_transfer_download);
    CHECK(adopted[0].m_label == L"a.txt");
    CHECK_FALSE(adopted[0].m_queued);

    //此后的结束回调给新窗口
    f.m_service.HandleServerFrame(cmd_share_access_state_notify, 0, noticetest::AccessEnded(kAccessId, kTransferId, kOwnerGone));
    REQUIRE(f.m_xfer2.m_finished.size() == 1);
    CHECK(f.m_xfer2.m_finished[0].first == id);
    CHECK(f.m_xfer.m_finished.empty());
    //挂着窗口的访问状态机没有传输也不回收
    f.m_service.OnTimer();
    CHECK(f.m_service.HasVisitor(kShareId));
    CHECK(f.m_host.Count(cmd_share_close_access) == 0);
}

TEST_CASE("SBG4 user cancel is marked, logout cancel is not", "[fileshare][sharebg]")
{
    BgFixture f;
    CShareVisitor* visitor = f.m_service.AcquireVisitor(kShareId, &f.m_window);
    REQUIRE(visitor != NULL);
    //访问会话还在打开时点「取消」
    const uint32_t first = f.m_service.StartDownload(kShareId, OneFile(), L"D:\\dl", &f.m_xfer, Labeled(L"a.txt"));
    REQUIRE(first != 0);
    f.m_service.CancelTransfer(first);
    REQUIRE(f.m_finish.m_results.size() == 1);
    CHECK(f.m_finish.m_results[0].m_code == error_code_share_transfer_canceled);
    CHECK(f.m_finish.m_results[0].m_userCanceled);

    //就绪后的一笔被退出登录中断：不是用户取消；退出登录后访问状态机全部销毁
    f.OpenToReady(visitor);
    f.StartAcceptedDownload(&f.m_xfer);
    f.m_service.ReleaseVisitor(kShareId);
    f.m_service.OnLoggingOut();
    REQUIRE(f.m_finish.m_results.size() == 2);
    CHECK(f.m_finish.m_results[1].m_code == error_code_share_transfer_canceled);
    CHECK_FALSE(f.m_finish.m_results[1].m_userCanceled);
    CHECK_FALSE(f.m_service.HasVisitor(kShareId));
}

TEST_CASE("SBG5 failing to open the access session fails the queued transfers", "[fileshare][sharebg]")
{
    BgFixture f;
    CShareVisitor* visitor = f.m_service.AcquireVisitor(kShareId, &f.m_window);
    REQUIRE(visitor != NULL);
    const uint32_t id = f.m_service.StartDownload(kShareId, OneFile(), L"D:\\dl", &f.m_xfer, Labeled(L"a.txt"));
    REQUIRE(id != 0);
    //窗口先关了：排队的传输让访问状态机留在后台
    f.m_service.ReleaseVisitor(kShareId);
    CHECK(f.m_service.HasVisitor(kShareId));

    //打开失败（分享者不在线）：排队的传输以同一原因结束
    f.m_service.HandleServerFrame(cmd_share_open, f.m_host.LastSeq(cmd_share_open), noticetest::OpenFail(kOwnerGone));
    REQUIRE(f.m_finish.m_results.size() == 1);
    CHECK(f.m_finish.m_results[0].m_localId == id);
    CHECK(f.m_finish.m_results[0].m_code == kOwnerGone);
    CHECK_FALSE(f.m_finish.m_results[0].m_userCanceled);
    CHECK(f.m_service.GetTransferCount(false) == 0);

    //定时检查销毁它；会话从没打开过，不发 2221
    f.m_service.OnTimer();
    CHECK_FALSE(f.m_service.HasVisitor(kShareId));
    CHECK(f.m_host.Count(cmd_share_close_access) == 0);
}

TEST_CASE("SBG6 retry without a browser opens a background access session", "[fileshare][sharebg]")
{
    BgFixture f;
    //没有浏览窗口：业务总控新建一个不挂窗口的访问状态机
    const uint32_t id = f.m_service.StartDownload(kShareId, OneFile(), L"D:\\dl", NULL, Labeled(L"a.txt"));
    REQUIRE(id != 0);
    CHECK(f.m_service.HasVisitor(kShareId));
    CHECK_FALSE(f.m_service.IsVisitorAttached(kShareId));
    CHECK(f.m_host.Count(cmd_share_open) == 1);
    CHECK(f.m_service.IsTransferQueued(id));
    //排队的传输让它留着
    f.m_service.OnTimer();
    CHECK(f.m_service.HasVisitor(kShareId));

    //拿到它（窗口打开时也是这样接回的）并推到就绪：发出 2224
    CShareVisitor* visitor = f.m_service.AcquireVisitor(kShareId, &f.m_window);
    REQUIRE(visitor != NULL);
    f.OpenToReady(visitor);
    CHECK(f.m_host.Count(cmd_share_request_transfer) == 1);
    CHECK_FALSE(f.m_service.IsTransferQueued(id));
}

TEST_CASE("SBG7 the completion callback runs once even without an observer", "[fileshare][sharebg]")
{
    BgFixture f;
    CShareVisitor* visitor = f.m_service.AcquireVisitor(kShareId, &f.m_window);
    REQUIRE(visitor != NULL);
    visitor->List(std::string(), static_cast<int64_t>(::GetTickCount64()));
    f.OpenToReady(visitor);

    std::shared_ptr<noticetest::CompletionRecorder> completion(new noticetest::CompletionRecorder());
    ShareTransferOptions options = Labeled(L"a.txt");
    options.m_dragOut = true;
    options.m_completion = completion;
    const uint32_t id = f.m_service.StartDownload(kShareId, OneFile(), L"D:\\drag", &f.m_xfer, options);
    REQUIRE(id != 0);
    f.m_service.HandleServerFrame(cmd_share_request_transfer, f.m_host.LastSeq(cmd_share_request_transfer),
                                  noticetest::TransferOk(kTransferId));
    //窗口关闭后传输结束：结束回调照常调用一次，结果标记为拖出
    f.m_service.ReleaseVisitor(kShareId);
    f.m_service.HandleServerFrame(cmd_share_access_state_notify, 0, noticetest::AccessEnded(kAccessId, kTransferId, kOwnerGone));
    REQUIRE(completion->m_codes.size() == 1);
    CHECK(completion->m_codes[0] == kOwnerGone);
    REQUIRE(f.m_finish.m_results.size() == 1);
    CHECK(f.m_finish.m_results[0].m_dragOut);
    CHECK(f.m_xfer.m_finished.empty());
}
