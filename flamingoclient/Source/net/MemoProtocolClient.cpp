/**
 *  「备忘与提醒」报文编解码（客户端侧）实现，MemoProtocolClient.cpp
 *  balloonwj@qq.com   2026-10-03
 */
#include "MemoProtocolClient.h"
#include <stdio.h>
#include <stdlib.h>                     // strtoll
#include "../third-party/jsoncpp-1.9.0/json.h"

namespace net {

//"yyyy-mm-dd hh:mm" 的长度与各分隔符所在的下标
const size_t kMemoStartTextLen = 16;
const size_t kMemoStartDash1 = 4;
const size_t kMemoStartDash2 = 7;
const size_t kMemoStartSpace = 10;
const size_t kMemoStartColon = 13;

//十进制 yyyymmddhhmm 里各段的位权与每段的进位基数
const int64_t kMemoStartYearUnit = 100000000LL;
const int64_t kMemoStartMonthUnit = 1000000LL;
const int64_t kMemoStartDayUnit = 10000LL;
const int64_t kMemoStartHourUnit = 100LL;
const int64_t kMemoStartPartBase = 100LL;

//格式化开始时间用的缓冲区大小，"yyyy-mm-dd hh:mm" 加结尾的 '\0' 并留余量
const size_t kMemoStartBufSize = 32;

//memo_change_type 的两个取值，与 Protocol.h 一致（本文件不引 Protocol.h，保持与测试工程共用时的依赖最少）
const int kMemoChangeUpsert = 1;
const int kMemoChangeDelete = 2;

//把 JSON 值转成 int64：字符串按十进制解析，整型直接取，其它返回 0
static int64_t jsonToInt64(const Json::Value& v)
{
    if (v.isString())
        return static_cast<int64_t>(strtoll(v.asString().c_str(), NULL, 10));
    if (v.isIntegral())
        return v.asInt64();
    return 0;
}

static std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

static bool parseObject(const std::string& jsonbody, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    bool ok = reader->parse(jsonbody.c_str(), jsonbody.c_str() + jsonbody.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

static int readInt(const Json::Value& jo, const char* key, int defaultValue)
{
    return jo[key].isInt() ? jo[key].asInt() : defaultValue;
}

//从一个 JSON 对象里读出一条备忘；缺字段按默认值填
static void readMemoItem(const Json::Value& jo, MemoItem& out)
{
    out = MemoItem();
    out.m_memoid = jsonToInt64(jo["memoid"]);
    out.m_ctype = readInt(jo, "ctype", kMemoCtypePersonal);
    out.m_groupid = jsonToInt64(jo["groupid"]);
    out.m_creator = jsonToInt64(jo["creator"]);
    out.m_editor = jsonToInt64(jo["editor"]);
    out.m_title = jo["title"].isString() ? jo["title"].asString() : std::string();
    out.m_note = jo["note"].isString() ? jo["note"].asString() : std::string();
    out.m_start = jo["start"].isString() ? MemoProtocolClient::parseStart(jo["start"].asString()) : 0;
    out.m_advance = readInt(jo, "advance", 0);
    out.m_repeat = readInt(jo, "repeat", 0);
    out.m_weekdays = readInt(jo, "weekdays", 0);
    out.m_count = readInt(jo, "count", 1);
    out.m_style = readInt(jo, "style", 1);
    out.m_targetAll = jo["targetall"].isBool() ? jo["targetall"].asBool() : false;
    const Json::Value& targets = jo["targets"];
    if (targets.isArray())
    {
        for (Json::ArrayIndex i = 0; i < targets.size(); ++i)
        {
            const int64_t uid = jsonToInt64(targets[i]);
            if (uid > 0)
                out.m_targets.push_back(uid);
        }
    }
    out.m_createTime = jsonToInt64(jo["createtime"]);
    out.m_updateTime = jsonToInt64(jo["updatetime"]);
}

std::string MemoProtocolClient::formatStart(int64_t start)
{
    const int year = static_cast<int>(start / kMemoStartYearUnit);
    const int month = static_cast<int>((start / kMemoStartMonthUnit) % kMemoStartPartBase);
    const int day = static_cast<int>((start / kMemoStartDayUnit) % kMemoStartPartBase);
    const int hour = static_cast<int>((start / kMemoStartHourUnit) % kMemoStartPartBase);
    const int minute = static_cast<int>(start % kMemoStartPartBase);
    char buf[kMemoStartBufSize] = { 0 };
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
    return std::string(buf);
}

int64_t MemoProtocolClient::parseStart(const std::string& text)
{
    if (text.size() != kMemoStartTextLen)
        return 0;
    int64_t value = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (i == kMemoStartDash1 || i == kMemoStartDash2)
        {
            if (c != '-')
                return 0;
            continue;
        }
        if (i == kMemoStartSpace)
        {
            if (c != ' ')
                return 0;
            continue;
        }
        if (i == kMemoStartColon)
        {
            if (c != ':')
                return 0;
            continue;
        }
        if (c < '0' || c > '9')
            return 0;
        const int64_t kDecimal = 10;
        value = value * kDecimal + (c - '0');
    }
    return value;
}

std::string MemoProtocolClient::buildListRequest(int64_t groupid)
{
    Json::Value root(Json::objectValue);
    if (groupid > 0)
        root["groupid"] = std::to_string(groupid);
    return writeCompact(root);
}

std::string MemoProtocolClient::buildSaveRequest(const MemoItem& memo)
{
    Json::Value root(Json::objectValue);
    root["memoid"] = std::to_string(memo.m_memoid);
    root["ctype"] = memo.m_ctype;
    root["groupid"] = std::to_string(memo.m_ctype == kMemoCtypeGroup ? memo.m_groupid : 0);
    root["title"] = memo.m_title;
    root["note"] = memo.m_note;
    root["start"] = formatStart(memo.m_start);
    root["advance"] = memo.m_advance;
    root["repeat"] = memo.m_repeat;
    root["weekdays"] = memo.m_weekdays;
    root["count"] = memo.m_count;
    root["style"] = memo.m_style;
    root["targetall"] = memo.m_targetAll;
    Json::Value targets(Json::arrayValue);
    for (size_t i = 0; i < memo.m_targets.size(); ++i)
        targets.append(std::to_string(memo.m_targets[i]));
    root["targets"] = targets;
    return writeCompact(root);
}

std::string MemoProtocolClient::buildDeleteRequest(int64_t memoid)
{
    Json::Value root(Json::objectValue);
    root["memoid"] = std::to_string(memoid);
    return writeCompact(root);
}

bool MemoProtocolClient::parseListResponse(const std::string& jsonbody, MemoListResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root) || !root["code"].isInt())
        return false;
    out.m_code = root["code"].asInt();
    out.m_msg = root["msg"].isString() ? root["msg"].asString() : std::string();
    out.m_memos.clear();
    const Json::Value& arr = root["memos"];
    if (arr.isArray())
    {
        for (Json::ArrayIndex i = 0; i < arr.size(); ++i)
        {
            if (!arr[i].isObject())
                continue;
            MemoItem item;
            readMemoItem(arr[i], item);
            if (item.m_memoid > 0)
                out.m_memos.push_back(item);
        }
    }
    return true;
}

bool MemoProtocolClient::parseSaveResponse(const std::string& jsonbody, MemoSaveResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root) || !root["code"].isInt())
        return false;
    out.m_code = root["code"].asInt();
    out.m_msg = root["msg"].isString() ? root["msg"].asString() : std::string();
    out.m_hasMemo = root["memo"].isObject();
    if (out.m_hasMemo)
        readMemoItem(root["memo"], out.m_memo);
    return true;
}

bool MemoProtocolClient::parseDeleteResponse(const std::string& jsonbody, MemoDeleteResult& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root) || !root["code"].isInt())
        return false;
    out.m_code = root["code"].asInt();
    out.m_msg = root["msg"].isString() ? root["msg"].asString() : std::string();
    out.m_memoid = jsonToInt64(root["memoid"]);
    return true;
}

bool MemoProtocolClient::parseNotify(const std::string& jsonbody, MemoNotify& out)
{
    Json::Value root;
    if (!parseObject(jsonbody, root) || !root["changetype"].isInt())
        return false;
    out.m_changeType = root["changetype"].asInt();
    if (out.m_changeType == kMemoChangeUpsert)
    {
        if (!root["memo"].isObject())
            return false;
        readMemoItem(root["memo"], out.m_memo);
        out.m_memoid = out.m_memo.m_memoid;
    }
    else if (out.m_changeType == kMemoChangeDelete)
    {
        out.m_memo = MemoItem();
        out.m_memoid = jsonToInt64(root["memoid"]);
        out.m_memo.m_memoid = out.m_memoid;
    }
    else
    {
        return false;
    }
    return out.m_memoid > 0;
}

} // namespace net
