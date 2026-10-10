/**
 *  群问答的纯逻辑与报文编解码实现，GroupQaProtocol.cpp。说明见 GroupQaProtocol.h。
 *  balloonwj@qq.com   2026-10-07
 */
#include "GroupQaProtocol.h"

#include <set>
#include <stdlib.h>
#include "../jsoncpp1.9.0/json.h"

namespace {

//被视作首尾空白的字符
const char* const kQaTrimChars = " \t\r\n";

//把 Json::Value 序列化为紧凑的 JSON 文本
std::string WriteCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

//把 JSON 文本解析成对象；解析失败或不是对象返回 false
bool ParseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//取 64 位 ID：字符串按十进制解析，整数直接取；缺失或类型不对返回 0
int64_t GetId(const Json::Value& root, const char* key)
{
    if (root[key].isString())
        return static_cast<int64_t>(atoll(root[key].asString().c_str()));
    if (root[key].isIntegral())
        return root[key].asInt64();
    return 0;
}

//UTF-8 延续字节（10xxxxxx）的判定：取高两位的掩码，与高两位为 10 时的值
const unsigned char kUtf8HighTwoBitsMask = 0xC0;
const unsigned char kUtf8ContinuationBits = 0x80;

//UTF-8 字符数：不是延续字节的字节各算一个字符
int Utf8CharCount(const std::string& s)
{
    int count = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if ((static_cast<unsigned char>(s[i]) & kUtf8HighTwoBitsMask) != kUtf8ContinuationBits)
            ++count;
    }
    return count;
}

} // anonymous namespace

std::string GroupQaProtocol::trim(const std::string& s)
{
    const std::string::size_type first = s.find_first_not_of(kQaTrimChars);
    if (first == std::string::npos)
        return std::string();
    const std::string::size_type last = s.find_last_not_of(kQaTrimChars);
    return s.substr(first, last - first + 1);
}

std::string GroupQaProtocol::normalizeQuestion(const std::string& question)
{
    std::string r = trim(question);
    for (size_t i = 0; i < r.size(); ++i)
    {
        if (r[i] >= 'A' && r[i] <= 'Z')
            r[i] = static_cast<char>(r[i] - 'A' + 'a');
    }
    return r;
}

int GroupQaProtocol::checkContent(const std::string& question, const std::string& answer)
{
    if (trim(question).empty() || trim(answer).empty())
        return group_qa_check_empty;
    if (Utf8CharCount(question) > kGroupQaQuestionMaxChars)
        return group_qa_check_question_long;
    if (Utf8CharCount(answer) > kGroupQaAnswerMaxChars)
        return group_qa_check_answer_long;
    return group_qa_check_ok;
}

std::vector<GroupQaRecord> GroupQaProtocol::mergeForGroup(const std::vector<GroupQaRecord>& groupItems,
                                                          const std::vector<GroupQaRecord>& enterpriseItems)
{
    std::vector<GroupQaRecord> merged;
    std::set<std::string> seen;
    for (size_t i = 0; i < groupItems.size(); ++i)
    {
        GroupQaRecord r = groupItems[i];
        r.source = group_qa_source_group;
        merged.push_back(r);
        seen.insert(normalizeQuestion(r.question));
    }
    //本群有同一个问题时以本群的为准；企业问答之间重复时留第一条
    for (size_t i = 0; i < enterpriseItems.size(); ++i)
    {
        const std::string key = normalizeQuestion(enterpriseItems[i].question);
        if (!seen.insert(key).second)
            continue;
        GroupQaRecord r = enterpriseItems[i];
        r.source = group_qa_source_enterprise;
        merged.push_back(r);
    }
    return merged;
}

bool GroupQaProtocol::parseSaveRequest(const std::string& json, SaveGroupQaRequest& req)
{
    req = SaveGroupQaRequest();
    Json::Value root;
    if (!ParseObject(json, root))
        return false;
    req.groupid = GetId(root, "groupid");
    req.id = GetId(root, "id");
    if (req.groupid <= 0 || req.id < 0)
        return false;
    if (!root["question"].isString() || !root["answer"].isString())
        return false;
    req.question = trim(root["question"].asString());
    req.answer = trim(root["answer"].asString());
    return true;
}

bool GroupQaProtocol::parseDeleteRequest(const std::string& json, int64_t& groupid, int64_t& id)
{
    groupid = 0;
    id = 0;
    Json::Value root;
    if (!ParseObject(json, root))
        return false;
    groupid = GetId(root, "groupid");
    id = GetId(root, "id");
    return groupid > 0 && id > 0;
}

std::string GroupQaProtocol::buildGetResponse(int code, const std::string& msg, int64_t groupid,
                                              const std::vector<GroupQaRecord>& items)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    root["groupid"] = std::to_string(groupid);
    Json::Value arr(Json::arrayValue);
    for (size_t i = 0; i < items.size(); ++i)
    {
        const GroupQaRecord& r = items[i];
        Json::Value jo;
        jo["id"] = std::to_string(r.id);
        jo["question"] = r.question;
        jo["answer"] = r.answer;
        jo["source"] = (r.source == group_qa_source_enterprise) ? "enterprise" : "group";
        jo["editorname"] = r.editorname;
        jo["updatetime"] = static_cast<Json::Int64>(r.updatetime);
        arr.append(jo);
    }
    root["items"] = arr;
    return WriteCompact(root);
}

std::string GroupQaProtocol::buildSaveResponse(int code, const std::string& msg, int64_t groupid, int64_t id)
{
    Json::Value root;
    root["code"] = code;
    root["msg"] = msg;
    root["groupid"] = std::to_string(groupid);
    root["id"] = std::to_string(id);
    return WriteCompact(root);
}

std::string GroupQaProtocol::buildChangedNotify(int64_t groupid)
{
    Json::Value root;
    root["groupid"] = std::to_string(groupid);
    return WriteCompact(root);
}
