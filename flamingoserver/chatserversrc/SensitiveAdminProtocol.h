/**
 *  敏感词管理后台接口（/admin/sensitive/ 下的 8 个 .do 接口）的报文编解码与几条纯规则，SensitiveAdminProtocol.h
 *
 *  HttpSession 里的 8 个处理函数只做"校验令牌 → 确定企业 → 调 SensitiveWordManager → 写审计 →
 *  回包"这条流水线；其中不碰网络与数据库的部分（结果码换算、企业范围的确定、请求里的数组解析、
 *  应答 JSON 的拼装）都放在这里，便于单元测试。接口说明见 docs/Admin控制台接口文档.md 的
 *  「敏感词」一节与 docs/敏感词方案.md 第 6.7 节。
 *
 *  本文件只包含 AdminApiCodes.h、不包含 Protocol.h：两者的 error_code 枚举同名且取值不同，
 *  HttpSession 一侧只能用前者（见 AdminApiCodes.h 文件头的说明）。
 *
 *  典型用法：
 *      int64_t enterpriseId = 0;
 *      if (!sensitiveadmin::ResolveEnterprise(admin.enterpriseid, JInt(root, "enterpriseid", 0), enterpriseId))
 *          ...回 "enterpriseid required"
 *      int op = mgr.addWord(enterpriseId, ...);
 *      sendAdminResult(conn, sensitiveadmin::ToAdminCode(op), ...);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "../jsoncpp1.9.0/json.h"
#include "SensitiveWordManager.h"

namespace sensitiveadmin {

/**
 *  把词库写操作的结果（chatserver::sensitive_word_op_result）换成管理端错误码
 *  （AdminApiCodes.h 的 error_code）。
 *  @param opResult 写操作结果
 *  @return 成功为 error_code_ok；为空、超长、重复、已达上限、不存在分别为 120～124；
 *          数据库错误与不认识的取值为 error_code_unknown
 */
int32_t ToAdminCode(int opResult);

/**
 *  确定本次请求操作的企业：企业管理员一律是本企业（忽略请求里的 enterpriseid），平台超级
 *  管理员必须在请求里指定一个企业。
 *  @param adminEnterpriseId 管理员所属企业ID，0 表示平台超级管理员
 *  @param requested         请求里的 enterpriseid，缺省时为 0
 *  @param enterpriseId      [出参] 本次操作的企业ID；返回 false 时为 0
 *  @return 确定了企业返回 true；平台超级管理员没有指定企业时返回 false
 */
bool ResolveEnterprise(int64_t adminEnterpriseId, int64_t requested, int64_t& enterpriseId);

/**
 *  解析请求里的词条ID数组（delete.do 的 ids）。元素须为整数，其它类型的元素跳过。
 *  @param value    请求里的 ids 字段
 *  @param maxCount 最多允许的元素个数
 *  @param ids      [出参] 解析出的ID，调用前会被清空
 *  @return 是数组、非空、且元素个数不超过 maxCount 时返回 true；否则返回 false
 */
bool ParseIds(const Json::Value& value, size_t maxCount, std::vector<int64_t>& ids);

/**
 *  解析请求里的导入行数组（import.do 的 lines）。
 *  @param value    请求里的 lines 字段
 *  @param maxCount 最多允许的行数
 *  @param lines    [出参] 各行文字（UTF-8），调用前会被清空
 *  @return 是数组、非空、全部元素都是字符串、且行数不超过 maxCount 时返回 true；否则返回 false
 */
bool ParseLines(const Json::Value& value, size_t maxCount, std::vector<std::string>& lines);

/**
 *  把一条词条拼成 list.do 应答 list 数组里的一项：
 *  { id, word, category, remark, creatorname, createtime, updatetime }。
 *  @param record 词条
 *  @return JSON 对象
 */
Json::Value WordToJson(const SensitiveWordRecord& record);

/**
 *  把一条拦截记录拼成 hits.do 应答 list 数组里的一项：
 *  { id, time, userid, username, source, ctype, cid, targetname, word, category, snippet }。
 *  @param record 拦截记录
 *  @return JSON 对象
 */
Json::Value HitToJson(const SensitiveHitRecord& record);

/**
 *  把导入结果拼成 import.do 的应答：{ code, msg, added, skipped:[ {line, word, reason} ] }，
 *  其中 reason 已换成管理端错误码（120～124）。
 *  @param added   成功添加的条数
 *  @param skipped 被跳过的行
 *  @return JSON 对象（含 code 与 msg）
 */
Json::Value ImportResultToJson(int added, const std::vector<SensitiveImportSkip>& skipped);

} // namespace sensitiveadmin
