/**
 *  flamingoAdmin —— 企业问答接口实现，说明见 QaAdminApi.h。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "Util/QaAdminApi.h"

#include "json.h"      // 第三方 jsoncpp 1.9.0（third-party/jsoncpp）

#include <memory>

namespace flamingoadmin { namespace qaapi {

namespace {

// 各接口路径
const wchar_t* const kPathQaList   = L"/admin/qa/list.do";
const wchar_t* const kPathQaAdd    = L"/admin/qa/add.do";
const wchar_t* const kPathQaUpdate = L"/admin/qa/update.do";
const wchar_t* const kPathQaDelete = L"/admin/qa/delete.do";
const wchar_t* const kPathQaGroups = L"/admin/qa/groups.do";

// 各阶段超时（毫秒），与 AdminApi 里企业类接口相同
const int kQaTimeoutMs = 8000;
// HTTP 成功状态码
const int kHttpStatusOk = 200;
// 本地判定的「应答格式不对」
const int kErrLocalBadResponse = -1;

// 服务端错误码（chatserversrc/AdminApiCodes.h）
const int kErrUnknown       = 1;     // 服务器内部错误
const int kErrPermission    = 6;     // 无权限
const int kErrQaInvalid     = 160;   // 问题或答案不合规
const int kErrQaNotExist    = 161;   // 问答不存在
const int kErrQaFull        = 162;   // 已达上限
const int kErrQaGroupInvalid = 163;  // 所选的群不合规

// 「应用范围」一栏最多列出几个群名
const size_t kScopeNamesMax = 3;

std::string Utf8FromWide(const CString& wide)
{
    if (wide.IsEmpty())
        return std::string();
    const int len = wide.GetLength();
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide, len, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return std::string();
    std::string out(static_cast<size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, len, &out[0], bytes, nullptr, nullptr);
    return out;
}

CString WideFromUtf8(const std::string& utf8)
{
    if (utf8.empty())
        return CString();
    const int bytes = static_cast<int>(utf8.size());
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), bytes, nullptr, 0);
    if (len <= 0)
        return CString();
    CString out;
    wchar_t* buf = out.GetBuffer(len);
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), bytes, buf, len);
    out.ReleaseBuffer(len);
    return out;
}

// 64 位 ID：字符串按十进制解析，整数直接取
long long ToId(const Json::Value& v)
{
    if (v.isString())
        return _strtoi64(v.asCString(), nullptr, 10);
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

// 按 Unicode 字符计数（代理对算一个），与服务端按 UTF-8 字符计数一致
int CharCount(const CString& text)
{
    const wchar_t kHighFirst = 0xD800;
    const wchar_t kHighLast = 0xDBFF;
    int count = 0;
    for (int i = 0; i < text.GetLength(); ++i)
    {
        if (text[i] >= kHighFirst && text[i] <= kHighLast && i + 1 < text.GetLength())
            ++i;
        ++count;
    }
    return count;
}

Json::Value MakeBody(const CString& token, long long enterpriseId)
{
    Json::Value root;
    root["token"] = Utf8FromWide(token);
    if (enterpriseId > 0)
        root["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    return root;
}

HttpRequest MakeRequest(const ServerConfig& server, const wchar_t* path, const Json::Value& body)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    HttpRequest request;
    request.m_host      = server.m_host.GetString();
    request.m_port      = server.m_port;
    request.m_useTls    = server.m_useTls;
    request.m_path      = path;
    request.m_bodyUtf8  = Json::writeString(wb, body);
    request.m_timeoutMs = kQaTimeoutMs;
    return request;
}

// 解析应答的公共部分；业务成功时返回 true 并给出 root
bool ParsePrelude(const HttpResult& http, QaResultBase& result, Json::Value& root)
{
    if (!http.m_transportOk)
    {
        result.m_message = http.m_error;
        return false;
    }
    result.m_transportOk = true;
    if (http.m_statusCode != kHttpStatusOk)
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message.Format(_T("服务器返回异常状态码 %d"), http.m_statusCode);
        return false;
    }
    Json::CharReaderBuilder b;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    if (!reader->parse(http.m_body.data(), http.m_body.data() + http.m_body.size(), &root, &errs) || !root.isObject())
    {
        result.m_code = kErrLocalBadResponse;
        result.m_message = QaErrorText(kErrLocalBadResponse);
        return false;
    }
    result.m_code = root["code"].isIntegral() ? root["code"].asInt() : kErrLocalBadResponse;
    if (result.m_code != 0)
    {
        result.m_message = QaErrorText(result.m_code);
        return false;
    }
    return true;
}

} // anonymous namespace

HttpRequest MakeQaListRequest(const ServerConfig& server, const CString& token, long long enterpriseId)
{
    return MakeRequest(server, kPathQaList, MakeBody(token, enterpriseId));
}

HttpRequest MakeQaGroupsRequest(const ServerConfig& server, const CString& token, long long enterpriseId)
{
    return MakeRequest(server, kPathQaGroups, MakeBody(token, enterpriseId));
}

HttpRequest MakeQaSaveRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                              const QaItem& item)
{
    Json::Value root = MakeBody(token, enterpriseId);
    if (item.m_id > 0)
        root["id"] = std::to_string(item.m_id);
    root["question"] = Utf8FromWide(item.m_question);
    root["answer"] = Utf8FromWide(item.m_answer);
    root["scope"] = item.m_scope;
    if (item.m_scope == kQaScopeSelected)
    {
        Json::Value groups(Json::arrayValue);
        for (size_t i = 0; i < item.m_groupIds.size(); ++i)
            groups.append(std::to_string(item.m_groupIds[i]));
        root["groupids"] = groups;
    }
    return MakeRequest(server, item.m_id > 0 ? kPathQaUpdate : kPathQaAdd, root);
}

HttpRequest MakeQaDeleteRequest(const ServerConfig& server, const CString& token, long long enterpriseId,
                                long long id)
{
    Json::Value root = MakeBody(token, enterpriseId);
    root["id"] = std::to_string(id);
    return MakeRequest(server, kPathQaDelete, root);
}

QaListResult ParseQaListResponse(const HttpResult& http)
{
    QaListResult result;
    Json::Value root;
    if (!ParsePrelude(http, result, root))
        return result;
    const Json::Value& list = root["list"];
    if (!list.isArray())
        return result;
    for (Json::ArrayIndex i = 0; i < list.size(); ++i)
    {
        const Json::Value& jo = list[i];
        if (!jo.isObject())
            continue;
        QaItem item;
        item.m_id = ToId(jo["id"]);
        if (item.m_id <= 0)
            continue;
        item.m_question = WideFromUtf8(jo["question"].isString() ? jo["question"].asString() : std::string());
        item.m_answer = WideFromUtf8(jo["answer"].isString() ? jo["answer"].asString() : std::string());
        item.m_scope = (jo["scope"].isIntegral() && jo["scope"].asInt() == kQaScopeSelected) ? kQaScopeSelected : kQaScopeAll;
        const Json::Value& groups = jo["groupids"];
        if (groups.isArray())
        {
            for (Json::ArrayIndex k = 0; k < groups.size(); ++k)
            {
                const long long gid = ToId(groups[k]);
                if (gid > 0)
                    item.m_groupIds.push_back(gid);
            }
        }
        item.m_creatorName = WideFromUtf8(jo["creatorname"].isString() ? jo["creatorname"].asString() : std::string());
        item.m_updateTime = jo["updatetime"].isIntegral() ? jo["updatetime"].asInt64() : 0;
        result.m_items.push_back(item);
    }
    return result;
}

QaGroupsResult ParseQaGroupsResponse(const HttpResult& http)
{
    QaGroupsResult result;
    Json::Value root;
    if (!ParsePrelude(http, result, root))
        return result;
    const Json::Value& list = root["list"];
    if (!list.isArray())
        return result;
    for (Json::ArrayIndex i = 0; i < list.size(); ++i)
    {
        const Json::Value& jo = list[i];
        if (!jo.isObject())
            continue;
        QaGroup g;
        g.m_id = ToId(jo["groupid"]);
        if (g.m_id <= 0)
            continue;
        g.m_name = WideFromUtf8(jo["name"].isString() ? jo["name"].asString() : std::string());
        result.m_groups.push_back(g);
    }
    return result;
}

QaWriteResult ParseQaWriteResponse(const HttpResult& http)
{
    QaWriteResult result;
    Json::Value root;
    if (!ParsePrelude(http, result, root))
        return result;
    result.m_id = ToId(root["id"]);
    return result;
}

CString QaErrorText(int code)
{
    switch (code)
    {
    //问题或答案为空、超长，或应用范围取值不对
    case kErrQaInvalid:
        return CString(_T("问题或答案不合规：不能为空，问题最多 128 个字，答案最多 2048 个字"));
    //要修改或删除的问答已不存在
    case kErrQaNotExist:
        return CString(_T("这条问答已不存在，可能已被其他管理员删除"));
    //本企业问答已达 1000 条
    case kErrQaFull:
        return CString(_T("本企业的问答已达 1000 条上限，请先删除一些"));
    //所选的群为空或不属于本企业
    case kErrQaGroupInvalid:
        return CString(_T("请至少选择一个群，且只能选择本企业的群"));
    //无权限
    case kErrPermission:
        return CString(_T("没有权限执行该操作"));
    //服务器内部错误
    case kErrUnknown:
        return CString(_T("服务器内部错误，请稍后重试"));
    //应答格式不对
    case kErrLocalBadResponse:
        return CString(_T("服务器应答异常，请稍后重试"));
    //其余错误：带上错误码
    default:
    {
        CString text;
        text.Format(_T("操作失败（错误码 %d）"), code);
        return text;
    }
    }
}

bool ValidateQa(const QaItem& item, CString* error)
{
    CString reason;
    if (item.m_question.IsEmpty())
        reason = _T("请填写问题");
    else if (item.m_answer.IsEmpty())
        reason = _T("请填写答案");
    else if (CharCount(item.m_question) > kQaQuestionMaxChars)
        reason.Format(_T("问题不能超过 %d 个字"), kQaQuestionMaxChars);
    else if (CharCount(item.m_answer) > kQaAnswerMaxChars)
        reason.Format(_T("答案不能超过 %d 个字"), kQaAnswerMaxChars);
    else if (item.m_scope == kQaScopeSelected && item.m_groupIds.empty())
        reason = _T("请至少选择一个群");
    if (error != nullptr)
        *error = reason;
    return reason.IsEmpty();
}

CString QaScopeText(const QaItem& item, const std::vector<QaGroup>& groups)
{
    if (item.m_scope != kQaScopeSelected)
        return CString(_T("全部群"));
    CString names;
    const size_t shown = (item.m_groupIds.size() < kScopeNamesMax) ? item.m_groupIds.size() : kScopeNamesMax;
    for (size_t i = 0; i < shown; ++i)
    {
        CString name;
        for (size_t k = 0; k < groups.size(); ++k)
        {
            if (groups[k].m_id == item.m_groupIds[i])
            {
                name = groups[k].m_name;
                break;
            }
        }
        if (name.IsEmpty())
            name.Format(_T("%lld"), item.m_groupIds[i]);
        if (!names.IsEmpty())
            names += _T("、");
        names += name;
    }
    if (item.m_groupIds.size() > shown)
        names += _T("…");
    CString text;
    text.Format(_T("%u 个群：%s"), static_cast<unsigned>(item.m_groupIds.size()), names.GetString());
    return text;
}

}} // namespace flamingoadmin::qaapi
