/**
 *  fileserver 中转报到时「会话 id 前缀与角色是否相符」判定的单元测试（Suite FRR），test_relayrolecheck.cpp
 *
 *  覆盖 docs/文件共享方案.md 第 1 期第 8 步（2026-10-02 与用户确认的用例）：
 *    - FRR1 sessionid 以 share- 开头的只接受文件共享的角色 9 / 10，其余只接受文件夹传输的 5 / 6，
 *           远程桌面与音视频的角色 1~4 一律拒绝。
 *  被测函数是 utils/RelayRoleCheck.h 里的纯函数，chatserver 生成会话 id 与 fileserver 校验报到共用它。
 *
 *  典型用法：Tests.exe --test-case="FRR*"
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include <string>

#include "doctest.h"
#include "../utils/RelayRoleCheck.h"
#include "../utils/RelayToken.h"

TEST_CASE("FRR1 中转报到：会话 id 前缀与角色必须相符")
{
    const std::string shareAccess = std::string(kShareRelaySessionIdPrefix) + "3-1759370000000";
    const std::string shareTransfer = shareAccess + "-t7";
    const std::string folderSession = "10001-10002-1759370000000-1";

    //文件共享的访问会话与传输子会话：只接受 9 / 10
    CHECK(isAcceptedFileRelayRole(shareAccess, RelayToken::role_share_owner));
    CHECK(isAcceptedFileRelayRole(shareAccess, RelayToken::role_share_visitor));
    CHECK(isAcceptedFileRelayRole(shareTransfer, RelayToken::role_share_owner));
    CHECK(isAcceptedFileRelayRole(shareTransfer, RelayToken::role_share_visitor));
    CHECK_FALSE(isAcceptedFileRelayRole(shareAccess, RelayToken::role_folder_sender));
    CHECK_FALSE(isAcceptedFileRelayRole(shareAccess, RelayToken::role_folder_receiver));

    //文件夹传输：只接受 5 / 6
    CHECK(isAcceptedFileRelayRole(folderSession, RelayToken::role_folder_sender));
    CHECK(isAcceptedFileRelayRole(folderSession, RelayToken::role_folder_receiver));
    CHECK_FALSE(isAcceptedFileRelayRole(folderSession, RelayToken::role_share_owner));
    CHECK_FALSE(isAcceptedFileRelayRole(folderSession, RelayToken::role_share_visitor));

    //远程桌面与音视频的角色走 imgserver，拿到 fileserver 来一律拒绝
    const int otherRoles[] = { RelayToken::role_controller, RelayToken::role_controlled,
                               RelayToken::role_caller, RelayToken::role_callee, 0, 7, 8, 11 };
    for (size_t i = 0; i < sizeof(otherRoles) / sizeof(otherRoles[0]); ++i)
    {
        INFO("role " << otherRoles[i]);
        CHECK_FALSE(isAcceptedFileRelayRole(shareAccess, otherRoles[i]));
        CHECK_FALSE(isAcceptedFileRelayRole(folderSession, otherRoles[i]));
    }

    //前缀必须在开头：中间含 share- 的不算
    CHECK_FALSE(isAcceptedFileRelayRole("x-share-1-2", RelayToken::role_share_owner));
    CHECK(isAcceptedFileRelayRole("x-share-1-2", RelayToken::role_folder_sender));
}
