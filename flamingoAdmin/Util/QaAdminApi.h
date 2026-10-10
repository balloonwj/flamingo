/**
 *  flamingoAdmin —— 企业问答接口（/admin/qa/*.do，docs/Admin控制台接口文档.md 模块 13，docs/群问答方案.md）。
 *
 *  请求构造、应答解析、本地校验与给界面看的文字都在这里，不依赖窗口与网络线程，单元测试见
 *  Tests/EnterpriseQaTests.cpp。与 AdminApi 分开放：AdminApi.cpp 很大、改动频繁，企业问答自成一个文件更容易维护。
 *
 *  典型用法：
 *      HttpRequest req = qaapi::MakeQaListRequest(server, session.Token(), enterpriseId);
 *      req.m_tag = kApiTagQaList;
 *      //应答到达后
 *      qaapi::QaListResult r = qaapi::ParseQaListResponse(http);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <vector>

#include "Util/AppConfig.h"     // ServerConfig
#include "Util/HttpWorker.h"    // HttpRequest
#include "Util/HttpClient.h"    // HttpResult

namespace flamingoadmin { namespace qaapi {

// 问题最多多少个字（与服务端 kGroupQaQuestionMaxChars 一致）
const int kQaQuestionMaxChars = 128;
// 答案最多多少个字（与服务端 kGroupQaAnswerMaxChars 一致）
const int kQaAnswerMaxChars = 2048;

// 应用范围
enum QaScope
{
    // 应用到本企业全部群
    kQaScopeAll = 0,
    // 只应用到所选的群
    kQaScopeSelected = 1
};

// 一条企业问答
struct QaItem
{
    long long              m_id;           // 问答ID；0 表示新增
    CString                m_question;     // 问题
    CString                m_answer;       // 答案
    int                    m_scope;        // 应用范围，取值见 QaScope
    std::vector<long long> m_groupIds;     // 所选的群（m_scope 为 kQaScopeSelected 时有效）
    CString                m_creatorName;  // 最后保存它的管理员
    long long              m_updateTime;   // 最后修改时间，Unix 毫秒

    QaItem()
        : m_id(0), m_scope(kQaScopeAll), m_updateTime(0)
    {
    }
};

// 本企业的一个群（选择应用范围用）
struct QaGroup
{
    long long m_id;     // 群ID
    CString   m_name;   // 群名

    QaGroup()
        : m_id(0)
    {
    }
};

// 应答的公共部分
struct QaResultBase
{
    bool    m_transportOk;   // HTTP 往返是否成功
    int     m_code;          // 业务错误码；0 成功
    CString m_message;       // 失败时给用户看的中文提示

    QaResultBase()
        : m_transportOk(false), m_code(0)
    {
    }

    // 往返成功且业务成功
    bool Succeeded() const { return m_transportOk && m_code == 0; }
};

// list.do 的应答
struct QaListResult : public QaResultBase
{
    std::vector<QaItem> m_items;   // 按修改时间倒序
};

// groups.do 的应答
struct QaGroupsResult : public QaResultBase
{
    std::vector<QaGroup> m_groups;   // 按群ID升序
};

// add.do / update.do / delete.do 的应答
struct QaWriteResult : public QaResultBase
{
    long long m_id;   // 新增成功时的新问答ID

    QaWriteResult()
        : m_id(0)
    {
    }
};

/**
 *  构造拉取企业问答列表的请求（qa/list.do）。
 *    server：服务器配置（m_host 可为空，由请求出口补上）；token：会话令牌；
 *    enterpriseId：企业ID；企业管理员传 0（服务端按本企业处理），平台超管必须传选中的企业。
 */
HttpRequest MakeQaListRequest(const ServerConfig& server, const CString& token, long long enterpriseId);

/**
 *  构造拉取本企业全部群的请求（qa/groups.do），供弹窗选择应用范围。参数同 MakeQaListRequest。
 */
HttpRequest MakeQaGroupsRequest(const ServerConfig& server, const CString& token, long long enterpriseId);

/**
 *  构造保存请求：item.m_id 为 0 时走 qa/add.do，否则走 qa/update.do。应用范围为全部群时不带 groupids。
 */
HttpRequest MakeQaSaveRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                              const QaItem& item);

/**
 *  构造删除请求（qa/delete.do）。
 */
HttpRequest MakeQaDeleteRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                long long id);

// 解析 list.do 的应答
QaListResult ParseQaListResponse(const HttpResult& http);

// 解析 groups.do 的应答
QaGroupsResult ParseQaGroupsResponse(const HttpResult& http);

// 解析 add.do / update.do / delete.do 的应答
QaWriteResult ParseQaWriteResponse(const HttpResult& http);

/**
 *  业务错误码对应的中文提示：160～163 为本模块专属，其余给通用说法。
 */
CString QaErrorText(int code);

/**
 *  保存前的本地校验：问题、答案不能为空，不超过上限；选了「所选的群」时至少选一个群。
 *    item：弹窗里填的内容（首尾空白已去掉）；error：[出参，可为 NULL] 不通过时的原因。
 *  @return 通过返回 true
 */
bool ValidateQa(const QaItem& item, CString* error);

/**
 *  列表里「应用范围」一栏的文字：全部群时为「全部群」；所选的群时为「N 个群：甲、乙、丙」（最多列出 3 个名字，
 *  名字取不到的群写群号）。
 *    item：问答；groups：本企业的群（可能还没拉到，为空）。
 */
CString QaScopeText(const QaItem& item, const std::vector<QaGroup>& groups);

}} // namespace flamingoadmin::qaapi
