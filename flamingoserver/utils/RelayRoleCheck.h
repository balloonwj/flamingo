/**
 *  fileserver 中转会话的「会话 id 前缀与角色是否相符」判定，RelayRoleCheck.h
 *
 *  fileserver 的中转配对表同时承载两种业务：单聊文件夹传输（角色 5 / 6）与文件共享（角色 9 / 10）。
 *  两者共用 cmd_folder_relay_hello 报到、共用一张配对表，配对表本身只要求同一通道上两端角色不同。
 *  为防止一枚文件夹传输的令牌与一枚文件共享的令牌被配成一对，文件共享的中转会话 id 一律以
 *  kShareRelaySessionIdPrefix 开头（由 chatserver 的 FileShareSessionManager 生成），fileserver 报到时
 *  再按前缀核对角色（docs/文件共享方案.md 6.2）。chatserver 签发令牌时已经保证了这一点，这里是第二道检查。
 *
 *  只有头文件、只依赖 RelayToken.h 的角色常量，chatserver 与 fileserver 共同包含。
 *  单元测试见 tests/test_relayrolecheck.cpp（Suite FRR）。
 *
 *  典型用法（fileserver 处理报到帧）：
 *      if (!isAcceptedFileRelayRole(payload.sessionid, payload.role))
 *          return error_code_permission_denied;
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <string>
#include "RelayToken.h"

//文件共享中转会话 id 的前缀：访问会话形如 "share-<序号>-<毫秒>"，传输子会话在其后追加 "-t<序号>"
const char* const kShareRelaySessionIdPrefix = "share-";

/**
 *  判断一枚令牌的角色能否用于它所声明的中转会话。
 *  @param sessionid 令牌里的中转会话 id
 *  @param role      令牌里的角色，取值见 RelayToken::role_*
 *  @return sessionid 以 kShareRelaySessionIdPrefix 开头时，角色为 role_share_owner / role_share_visitor 返回 true；
 *          否则角色为 role_folder_sender / role_folder_receiver 返回 true；其余一律返回 false
 */
inline bool isAcceptedFileRelayRole(const std::string& sessionid, int role)
{
    const std::string prefix(kShareRelaySessionIdPrefix);
    const bool isShareSession = (sessionid.compare(0, prefix.size(), prefix) == 0);
    if (isShareSession)
        return role == RelayToken::role_share_owner || role == RelayToken::role_share_visitor;
    return role == RelayToken::role_folder_sender || role == RelayToken::role_folder_receiver;
}
