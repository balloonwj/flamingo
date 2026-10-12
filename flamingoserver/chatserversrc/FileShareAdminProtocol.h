/**
 *  文件共享管理后台接口（/admin/setting/ 与 /admin/fileshare/ 下的接口）的报文编解码与几条纯规则，FileShareAdminProtocol.h
 *
 *  docs/文件共享方案.md 6.5（第 3 期第 3 步）。处理流水线是「HttpSession 校验令牌 → FileShareAdminService 调各个
 *  Manager → HttpSession 写管理员操作日志、回包」；其中不碰网络与数据库的部分（企业范围的确定、分页换算、请求解析、
 *  应答 JSON 的拼装）放在这里，便于单元测试。接口说明见 docs/Admin控制台接口文档.md 的「文件共享」一章。
 *
 *  本文件经 FileShareManager.h 间接包含 Protocol.h，因此<u>不能</u>被 HttpSession 包含（AdminApiCodes.h 与
 *  Protocol.h 的 error_code 同名且取值不同）；HttpSession 只包含 FileShareAdminService.h。
 *  单元测试见 flamingoserver/tests/test_fileshareadmin.cpp（Suite FSA）。
 *
 *  典型用法（FileShareAdminService）：
 *      ShareAdminQuery q = fileshareadmin::ParseShareQuery(req, fileshareadmin::ResolveListEnterprise(adminEid, JInt(req)));
 *      mgr.queryAdminShares(q, views, total);
 *      out = fileshareadmin::BuildShareList(views, total);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../jsoncpp1.9.0/json.h"
#include "FeatureSwitchManager.h"   //FeatureSettingView
#include "FileShareManager.h"       //ShareAdminQuery / ShareAdminView / ShareMemberView / ShareAuditQuery / ShareAuditView

namespace fileshareadmin {

//分页：页码从 1 起；每页条数缺省 50、上限 200（与其余后台列表接口一致）
const int64_t kDefaultPageSize = 50;
const int64_t kMaxPageSize = 200;

//关键字最多取多少个字节（超出的部分截掉，避免拼出过长的 LIKE 条件）
const size_t kKeywordMaxBytes = 64;

/**
 *  确定「读写功能设置」作用的企业：企业管理员一律是本企业（请求里写了别的企业也不理会），平台管理员必须在请求里指定。
 *  @param adminEnterpriseId 管理员所属企业 id，0 表示平台管理员
 *  @param requested         请求里的 enterpriseid，缺省时为 0
 *  @param enterpriseId      [出参] 本次操作的企业 id；返回 false 时为 0
 *  @return 确定了企业返回 true；平台管理员没有指定企业时返回 false
 */
bool ResolveTargetEnterprise(int64_t adminEnterpriseId, int64_t requested, int64_t& enterpriseId);

/**
 *  确定「共享列表、审计查询」的企业过滤：企业管理员一律只看本企业；平台管理员按请求，不填（0）为全部企业。
 *  @param adminEnterpriseId 管理员所属企业 id，0 表示平台管理员
 *  @param requested         请求里的 enterpriseid，缺省时为 0
 *  @return 企业过滤值；0 表示不限
 */
int64_t ResolveListEnterprise(int64_t adminEnterpriseId, int64_t requested);

/**
 *  单个共享的操作（看成员、强制取消）是否在管理员的范围内：平台管理员可以操作任意企业的共享，企业管理员只能操作本企业的。
 *  @param adminEnterpriseId 管理员所属企业 id，0 表示平台管理员
 *  @param shareEnterpriseId 共享的分享者所属企业 id
 *  @return 在范围内返回 true
 */
bool ShareInScope(int64_t adminEnterpriseId, int64_t shareEnterpriseId);

/**
 *  页码与每页条数换算成 SQL 的偏移与条数：页码小于 1 按 1；每页条数小于 1 按缺省值，大于上限按上限。
 *  @param page     页码（从 1 起）
 *  @param pageSize 每页条数
 *  @param offset   [出参] 跳过的条数
 *  @param limit    [出参] 最多返回的条数
 */
void PageToRange(int64_t page, int64_t pageSize, int& offset, int& limit);

/**
 *  解析共享列表请求 {page, pagesize, keyword, state}。state 只认 1（有效）与 2（已取消），其余按不限。
 *  @param root             请求 JSON
 *  @param enterpriseFilter 企业过滤值（ResolveListEnterprise 的结果）
 *  @return 查询条件
 */
ShareAdminQuery ParseShareQuery(const Json::Value& root, int64_t enterpriseFilter);

/**
 *  解析审计查询请求 {page, pagesize, actorkeyword, action, starttime, endtime, shareid}（时间为 Unix 毫秒，0 为不限）。
 *  @param root             请求 JSON
 *  @param enterpriseFilter 企业过滤值
 *  @return 查询条件
 */
ShareAuditQuery ParseAuditQuery(const Json::Value& root, int64_t enterpriseFilter);

/**
 *  从请求里取一个 64 位 id（数字或数字字符串都认）。
 *  @param root 请求 JSON
 *  @param key  字段名
 *  @return id；缺失或非法时为 0
 */
int64_t ParseId(const Json::Value& root, const char* key);

/**
 *  功能设置的应答主体：{"settings":[{"key","value","isdefault"}]}。
 */
Json::Value BuildSettings(const std::vector<FeatureSettingView>& settings);

/**
 *  共享列表的应答主体：{"total", "list":[{shareid, enterpriseid, ownerid, ownername, owneraccount, ctype, cid,
 *  conversationname, kind, name, displaypath, membercount, state, cancelreason, createtime, canceltime}]}。
 */
Json::Value BuildShareList(const std::vector<ShareAdminView>& list, int64_t total);

/**
 *  成员列表的应答主体：{"members":[{userid, name, account, perms}]}。
 */
Json::Value BuildMembers(const std::vector<ShareMemberView>& members);

/**
 *  审计列表的应答主体：{"total", "list":[{id, time, shareid, sharename, ownerid, ownername, actorid, actorname,
 *  actorisadmin, action, source, path, topath, size, result, accessid}]}。
 */
Json::Value BuildAuditList(const std::vector<ShareAuditView>& list, int64_t total);

} // namespace fileshareadmin
