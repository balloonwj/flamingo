/**
 *  DirectFileManagerTests.cpp
 *
 *  单聊单文件 P2P 直传在客户端文件夹传输管理器一侧的测试，标签 [directfilemgr]，方案见
 *  docs/单聊文件P2P直传与P2P徽章方案.md 第 3 节：
 *    - DFM1：直传记录不占文件夹传输的并发名额（服务端也是分开限额的）；直传另有自己的计数；
 *    - DFM2：待应答的邀请带上传输种类，聊天窗据此画文件气泡还是文件夹气泡；
 *    - DFM3：回投界面的进度与结果新增的字段（种类、链路路径、MD5、落地文件）默认值；
 *    - DFM4：直传失败结束时报给服务端的错误码：用户取消照报 617；从没走上 P2P 链路的报 622（服务端随即让发送端
 *            改为离线文件）；走上过 P2P 链路之后的失败照报原因（保留期内可续传）。
 *
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-04
 */

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

//被测头文件放在最后：它引入 <windows.h>，理由见 FolderTransferManagerTests.cpp 的同一处注释
#include "../Feature/FolderTransfer/DirectFileTransfer.h"
#include "../Feature/FolderTransfer/FolderTransferManager.h"
#include "../net/Protocol.h"

using namespace foldertransfer;

namespace {

//造一条占着名额的记录
FolderSessionRecord MakeActiveRecord(const std::string& sessionId, int kind)
{
    FolderSessionRecord record;
    record.m_role = kFolderRoleSender;
    record.m_peerUserId = 5308;
    record.m_sessionId = sessionId;
    record.m_active = true;
    record.m_kind = kind;
    return record;
}

} // namespace

TEST_CASE("DFM1 直传记录不占文件夹传输的并发名额", "[directfilemgr]")
{
    FolderSessionRegistry registry;
    registry.SetMaxConcurrent(1);
    //一笔直传在跑：文件夹名额仍空着
    registry.Insert(MakeActiveRecord("d-1", net::folder_kind_file));
    CHECK(registry.ActiveCount() == 0);
    CHECK_FALSE(registry.IsFull());
    CHECK(registry.DirectActiveCount() == 1);
    //再来一笔文件夹传输：文件夹名额满了，直传计数不变
    registry.Insert(MakeActiveRecord("f-1", net::folder_kind_folder));
    CHECK(registry.ActiveCount() == 1);
    CHECK(registry.IsFull());
    CHECK(registry.DirectActiveCount() == 1);
    //直传失败之后名额释放
    FolderSessionRecord* d = registry.FindBySessionId("d-1");
    REQUIRE(d != NULL);
    registry.MarkFailed(*d, 1000);
    CHECK(registry.DirectActiveCount() == 0);
    //新建的记录默认是文件夹传输
    CHECK(FolderSessionRecord().m_kind == net::folder_kind_folder);
}

TEST_CASE("DFM2 待应答的邀请带上传输种类", "[directfilemgr]")
{
    FolderSessionRegistry registry;
    FolderSessionRecord record;
    record.m_role = kFolderRoleRecv;
    record.m_peerUserId = 5127;
    record.m_sessionId = "d-2";
    record.m_active = true;
    record.m_kind = net::folder_kind_file;
    record.m_invite.m_sessionId = "d-2";
    record.m_invite.m_fromUserId = 5127;
    record.m_invite.m_folderNameUtf8 = "a.bin";
    record.m_invite.m_fileCount = 1;
    record.m_invite.m_totalBytes = 10;
    record.m_invite.m_kind = net::folder_kind_file;
    registry.Insert(record);

    std::vector<FolderPendingInvite> invites;
    REQUIRE(registry.CollectPendingInvites(5127, invites) == 1);
    CHECK(invites[0].m_kind == net::folder_kind_file);
    CHECK(invites[0].m_folderNameUtf8 == "a.bin");
    //默认是文件夹传输
    CHECK(FolderPendingInvite().m_kind == net::folder_kind_folder);
}

TEST_CASE("DFM3 回投界面的进度与结果新增字段的默认值", "[directfilemgr]")
{
    FolderResultNotice result;
    CHECK(result.m_kind == net::folder_kind_folder);
    CHECK(result.m_linkPath == net::kFolderLinkPathNone);
    CHECK(result.m_md5.empty());
    CHECK(result.m_localFile.empty());
    FolderProgressNotice progress;
    CHECK(progress.m_kind == net::folder_kind_folder);
    CHECK(progress.m_linkPath == net::kFolderLinkPathNone);
}

TEST_CASE("DFM4 直传失败结束时报给服务端的错误码", "[directfilemgr]")
{
    //用户取消：照报取消，不让发送端自动改为离线文件
    CHECK(DirectFileCloseErrCode(error_code_folder_canceled, false) == error_code_folder_canceled);
    CHECK(DirectFileCloseErrCode(error_code_folder_canceled, true) == error_code_folder_canceled);
    //从没走上 P2P 链路：报 622，服务端推给双方，发送端改为离线文件
    CHECK(DirectFileCloseErrCode(error_code_folder_relay_broken, false) == error_code_folder_direct_unavailable);
    CHECK(DirectFileCloseErrCode(error_code_folder_read_fail, false) == error_code_folder_direct_unavailable);
    //走上过 P2P 链路之后的失败：照报原因，保留期内可续传
    CHECK(DirectFileCloseErrCode(error_code_folder_relay_broken, true) == error_code_folder_relay_broken);
    CHECK(DirectFileCloseErrCode(error_code_folder_write_fail, true) == error_code_folder_write_fail);
}
