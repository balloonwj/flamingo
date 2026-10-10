/**
 *  企业问答管理接口（/admin/qa/*.do）的纯逻辑：请求字段的解析、应用范围的校验与应答拼装，QaAdminProtocol.h
 *  （docs/群问答方案.md §2.1、docs/Admin控制台接口文档.md「企业问答」一章）。
 *
 *  与 SensitiveAdminProtocol 一样只包含 AdminApiCodes.h，不包含 Protocol.h（两边的 error_code 枚举同名会冲突）。
 *  不碰网络与数据库，见 tests/test_groupqa.cpp。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../jsoncpp1.9.0/json.h"
#include "GroupQaProtocol.h"   // EnterpriseQaRecord / enterprise_qa_scope

namespace qaadmin {

/**
 *  从请求里取出一条企业问答的内容：question、answer（去掉首尾空白）、scope（缺省 0）、groupids（字符串或整数数组）。
 *  @param root 请求 JSON
 *  @param out  [出参] 解析结果；id、enterpriseid、管理员与时间字段不在这里填
 *  @return question 与 answer 是字符串、scope 为 0 或 1、groupids 若出现则是数组且元素都为正时返回 true
 */
bool ParseQaFields(const Json::Value& root, EnterpriseQaRecord& out);

/**
 *  检查一条企业问答能否保存，返回管理接口的错误码：内容为空或超长回 error_code_qa_invalid；应用到所选的群时群列表为空、
 *  或含有不属于本企业的群回 error_code_qa_group_invalid；通过回 error_code_ok。
 *  @param rec             待保存的内容
 *  @param enterpriseGroups 本企业的全部群 ID
 */
int32_t CheckQa(const EnterpriseQaRecord& rec, const std::set<int64_t>& enterpriseGroups);

/**
 *  一条企业问答转成管理接口应答里的 JSON：{ id, question, answer, scope, groupids:[...], creatorname, updatetime }，
 *  ID 一律写成字符串。
 */
Json::Value QaToJson(const EnterpriseQaRecord& rec);

/**
 *  一个群转成 groups.do 应答里的 JSON：{ groupid, name }。
 */
Json::Value GroupToJson(int64_t groupid, const std::string& name);

} // namespace qaadmin
