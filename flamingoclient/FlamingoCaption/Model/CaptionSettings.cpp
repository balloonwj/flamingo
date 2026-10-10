/**
 *  按账号保存的字幕设置，实现见 CaptionSettings.h。
 *
 *  balloonwj@qq.com   2026-10-06
 */
#include "CaptionSettings.h"

#include "CaptionJson.h"

namespace caption {

namespace {

bool ReadFlag(const Json::Value& obj, const char* key, bool fallback)
{
    if (!obj.isObject() || !obj.isMember(key))
        return fallback;
    return JsonInt64(obj[key], fallback ? 1 : 0) != 0;
}

} // namespace

CaptionSettings::CaptionSettings()
    : m_recordMic(true)
    , m_recordSystem(false)
    , m_exportHard(true)
    , m_exportTrack(false)
    , m_exportSrt(false)
    , m_exportText(false)
    , m_liveSelf(false)
    , m_liveShow(true)
{
}

bool CaptionSettings::FromJson(const std::string& json)
{
    *this = CaptionSettings();
    if (json.empty())
        return true;
    Json::Value root;
    if (!ParseJson(json, root) || !root.isObject())
        return false;
    m_raw = json;
    if (root.isMember("replace"))
        m_replace.FromJson(WriteJson(root["replace"], false));
    const Json::Value& record = root["record"];
    m_recordMic = ReadFlag(record, "mic", true);
    m_recordSystem = ReadFlag(record, "system", false);
    const Json::Value& exp = root["export"];
    m_exportHard = ReadFlag(exp, "hard", true);
    m_exportTrack = ReadFlag(exp, "track", false);
    m_exportSrt = ReadFlag(exp, "srt", false);
    m_exportText = ReadFlag(exp, "text", false);
    const Json::Value& live = root["live"];
    m_liveSelf = ReadFlag(live, "self", false);
    m_liveShow = ReadFlag(live, "show", true);
    return true;
}

std::string CaptionSettings::ToJson() const
{
    Json::Value root(Json::objectValue);
    if (!m_raw.empty())
    {
        Json::Value old;
        if (ParseJson(m_raw, old) && old.isObject())
            root = old;
    }
    Json::Value replace;
    if (ParseJson(m_replace.ToJson(), replace))
        root["replace"] = replace;
    Json::Value record = root["record"].isObject() ? root["record"] : Json::Value(Json::objectValue);
    record["mic"] = m_recordMic ? 1 : 0;
    record["system"] = m_recordSystem ? 1 : 0;
    root["record"] = record;
    Json::Value exp = root["export"].isObject() ? root["export"] : Json::Value(Json::objectValue);
    exp["hard"] = m_exportHard ? 1 : 0;
    exp["track"] = m_exportTrack ? 1 : 0;
    exp["srt"] = m_exportSrt ? 1 : 0;
    exp["text"] = m_exportText ? 1 : 0;
    root["export"] = exp;
    Json::Value live = root["live"].isObject() ? root["live"] : Json::Value(Json::objectValue);
    live["self"] = m_liveSelf ? 1 : 0;
    live["show"] = m_liveShow ? 1 : 0;
    root["live"] = live;
    return WriteJson(root, false);
}

} // namespace caption
