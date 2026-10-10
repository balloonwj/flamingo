/**
 *  文件共享管理后台接口的业务执行层，FileShareAdminService.h
 *
 *  docs/文件共享方案.md 6.5（第 3 期第 3 步）：HttpSession 校验令牌之后调这里的函数，它们去调 FeatureSwitchManager、
 *  FileShareManager 与 ChatSession 的推送（1013、2203、2223），把结果写成应答主体（不含 code 与 msg）交回去。
 *
 *  本头文件刻意只依赖标准库与 jsoncpp，<u>不</u>包含 Protocol.h，也不包含 AdminApiCodes.h：HttpSession 只能用后者的
 *  error_code，而实现文件要用前者的共享枚举，两者同名且取值不同（见 AdminApiCodes.h 文件头）。因此这里的返回值是
 *  自己的结果码 file_share_admin_result，由 HttpSession 换成管理端错误码。
 *
 *  线程：在 HTTP 服务的线程上调用；推送经 ChatServer 投递到各连接自己的线程，调用时不得持有 ChatServer 的会话锁。
 *
 *  典型用法（HttpSession::onAdminFileShareCancel）：
 *      std::string name;
 *      int rc = fileshareadmin::CancelShare(shareId, admin.enterpriseid, admin.adminid, nowMs, name);
 *      if (rc == fileshareadmin::file_share_admin_ok) ...写管理员操作日志
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>

#include "../jsoncpp1.9.0/json.h"

namespace fileshareadmin {

//业务执行的结果，HttpSession 换成管理端错误码
enum file_share_admin_result
{
    file_share_admin_ok          = 0,   //成功
    file_share_admin_bad_request = 1,   //请求参数不对（缺 shareid、设置项或取值不认识等）
    file_share_admin_not_found   = 2,   //共享不存在
    file_share_admin_canceled    = 3,   //共享已经取消过
    file_share_admin_denied      = 4,   //共享不属于企业管理员的本企业
    file_share_admin_db_error    = 5,   //数据库访问失败
};

/**
 *  读一个企业的功能设置（/admin/setting/get.do）。
 *  @param enterpriseId 企业 id（已按管理员身份确定，大于 0）
 *  @param out          [出参] 应答主体 {"settings":[...]}
 *  @return file_share_admin_result
 */
int GetSettings(int64_t enterpriseId, Json::Value& out);

/**
 *  改一个企业的一项功能设置（/admin/setting/set.do）：写库、同步内存，再向该企业的在线用户推 1013。
 *  改的是文件共享开关且确有变化时，还要结束（关闭时）该企业全部的访问会话与传输子会话，并向各共享的成员推新状态；
 *  改的是 P2P 开关时只推 1013，已经建立的 P2P 会话不强制断开（docs/P2P通信方案.md 9.4）。
 *  @param enterpriseId  企业 id（大于 0）
 *  @param key           设置项名（fileshare.enabled、p2p.enabled 或 group.inviteconfirm）
 *  @param value         值（"0" 或 "1"）
 *  @param adminId       操作的管理员 id
 *  @param nowMs         当前时间（Unix 毫秒）
 *  @param enabledBefore [出参] 写之前是否开启
 *  @param enabledAfter  [出参] 写之后是否开启
 *  @return file_share_admin_result；设置项或取值不认识为 bad_request，写库失败为 db_error
 */
int SetSetting(int64_t enterpriseId, const std::string& key, const std::string& value, int64_t adminId, int64_t nowMs,
               bool& enabledBefore, bool& enabledAfter);

/**
 *  分页查共享（/admin/fileshare/list.do）。
 *  @param req               请求 JSON
 *  @param adminEnterpriseId 管理员所属企业 id（0 为平台管理员：按请求里的 enterpriseid 过滤，不填为全部）
 *  @param out               [出参] 应答主体 {"total","list":[...]}
 *  @return file_share_admin_result
 */
int ListShares(const Json::Value& req, int64_t adminEnterpriseId, Json::Value& out);

/**
 *  读一个共享的成员与权限（/admin/fileshare/members.do）。
 *  @param shareId           共享 id
 *  @param adminEnterpriseId 管理员所属企业 id
 *  @param out               [出参] 应答主体 {"members":[...]}
 *  @return file_share_admin_result；共享不存在为 not_found，不在本企业为 denied
 */
int GetMembers(int64_t shareId, int64_t adminEnterpriseId, Json::Value& out);

/**
 *  强制取消一个共享（/admin/fileshare/cancel.do），原因「管理员在后台强制取消」：经 ChatSession::cancelShareAndNotify
 *  改记录、结束访问会话、向成员与分享者推「已失效」、记审计（操作人为管理员 id）。
 *  @param shareId           共享 id
 *  @param adminEnterpriseId 管理员所属企业 id
 *  @param adminId           操作的管理员 id
 *  @param nowMs             当前时间（Unix 毫秒）
 *  @param shareName         [出参] 共享的显示名（写管理员操作日志用）
 *  @param shareEnterpriseId [出参] 共享所属企业 id（写管理员操作日志用）
 *  @return file_share_admin_result；不存在为 not_found，不在本企业为 denied，已取消为 canceled
 */
int CancelShare(int64_t shareId, int64_t adminEnterpriseId, int64_t adminId, int64_t nowMs, std::string& shareName,
                int64_t& shareEnterpriseId);

/**
 *  分页查审计（/admin/fileshare/audit.do）。
 *  @param req               请求 JSON
 *  @param adminEnterpriseId 管理员所属企业 id（规则同 ListShares）
 *  @param out               [出参] 应答主体 {"total","list":[...]}
 *  @return file_share_admin_result
 */
int QueryAudits(const Json::Value& req, int64_t adminEnterpriseId, Json::Value& out);

} // namespace fileshareadmin
