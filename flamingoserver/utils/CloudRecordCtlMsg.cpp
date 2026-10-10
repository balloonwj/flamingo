/**
 *  云端录制控制连接上的消息格式，格式说明见 CloudRecordCtlMsg.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecordCtlMsg.h"

#include <limits>
#include "../jsoncpp1.9.0/json.h"

namespace {

//op 字段的文字与枚举值的对应
struct OpName
{
    int         m_op;     //取值见 cloudrecctl_op
    const char* m_name;   //JSON 里的写法
};

const OpName kOpNames[] =
{
    { cloudrecctl_op_start,    "start" },
    { cloudrecctl_op_pause,    "pause" },
    { cloudrecctl_op_resume,   "resume" },
    { cloudrecctl_op_stop,     "stop" },
    { cloudrecctl_op_delete,   "delete" },
    { cloudrecctl_op_ping,     "ping" },
    { cloudrecctl_op_pong,     "pong" },
    { cloudrecctl_op_started,  "started" },
    { cloudrecctl_op_finished, "finished" },
    { cloudrecctl_op_uploaded, "uploaded" },
};

const size_t kOpNameCount = sizeof(kOpNames) / sizeof(kOpNames[0]);

int opFromName(const std::string& name)
{
    for (size_t i = 0; i < kOpNameCount; ++i)
    {
        if (name == kOpNames[i].m_name)
            return kOpNames[i].m_op;
    }
    return cloudrecctl_op_unknown;
}

std::string writeCompact(const Json::Value& v)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, v);
}

bool parseObject(const std::string& json, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(json.c_str(), json.c_str() + json.size(), &root, &errs);
    delete reader;
    return ok && errs.empty() && root.isObject();
}

//读一个非负整数字段；字段缺失或不是非负整数时返回 false
bool readUInt64(const Json::Value& root, const char* key, uint64_t& out)
{
    const Json::Value& v = root[key];
    if (!v.isUInt64())
        return false;
    out = v.asUInt64();
    return true;
}

bool readInt64(const Json::Value& root, const char* key, int64_t& out)
{
    const Json::Value& v = root[key];
    if (!v.isInt64())
        return false;
    out = v.asInt64();
    return true;
}

bool readInt(const Json::Value& root, const char* key, int& out)
{
    const Json::Value& v = root[key];
    if (!v.isInt())
        return false;
    out = v.asInt();
    return true;
}

bool readString(const Json::Value& root, const char* key, std::string& out)
{
    const Json::Value& v = root[key];
    if (!v.isString())
        return false;
    out = v.asString();
    return true;
}

} // namespace

CloudRecordCtlMsg::CloudRecordCtlMsg()
    : m_op(cloudrecctl_op_unknown)
    , m_recordId(0)
    , m_code(0)
    , m_bytes(0)
    , m_durationMs(0)
    , m_frames(0)
    , m_dropped(0)
    , m_reason(0)
{
}

//文件名的前缀与各种后缀
const char* const kFileNamePrefix = "rec_";
const char* const kFileExtFrec = ".frec";
const char* const kFileExtMp4 = ".mp4";
const char* const kFileExtM4a = ".m4a";
//录像 id 最多几位十进制数字：uint64 的上限是 20 位
const size_t kMaxIdDigits = 20;

std::string CloudRecordFileName(uint64_t recordId, int type)
{
    const char* ext = NULL;
    switch (type)
    {
    //原始帧文件
    case cloudrec_file_type_frec:
        ext = kFileExtFrec;
        break;

    //视频
    case cloudrec_file_type_mp4:
        ext = kFileExtMp4;
        break;

    //录音
    case cloudrec_file_type_m4a:
        ext = kFileExtM4a;
        break;

    //不认识的种类
    default:
        return std::string();
    }
    if (recordId == 0)
        return std::string();
    return std::string(kFileNamePrefix) + std::to_string(static_cast<unsigned long long>(recordId)) + ext;
}

bool ParseCloudRecordFileName(const std::string& name, uint64_t& recordId, int& type)
{
    recordId = 0;
    type = 0;
    const std::string prefix(kFileNamePrefix);
    if (name.compare(0, prefix.size(), prefix) != 0)
        return false;
    const size_t dot = name.find('.', prefix.size());
    if (dot == std::string::npos)
        return false;
    const std::string digits = name.substr(prefix.size(), dot - prefix.size());
    const std::string ext = name.substr(dot);
    if (digits.empty() || digits.size() > kMaxIdDigits || digits[0] == '0')
        return false;
    const uint64_t kBase = 10;
    uint64_t id = 0;
    for (size_t i = 0; i < digits.size(); ++i)
    {
        if (digits[i] < '0' || digits[i] > '9')
            return false;
        const uint64_t digit = static_cast<uint64_t>(digits[i] - '0');
        //再乘 10 加一位会超出 uint64 时判为不合法
        if (id > (std::numeric_limits<uint64_t>::max() - digit) / kBase)
            return false;
        id = id * kBase + digit;
    }
    if (ext == kFileExtFrec)
        type = cloudrec_file_type_frec;
    else if (ext == kFileExtMp4)
        type = cloudrec_file_type_mp4;
    else if (ext == kFileExtM4a)
        type = cloudrec_file_type_m4a;
    else
        return false;
    recordId = id;
    return true;
}

std::string CloudRecordTokenKey(const std::string& fileName)
{
    uint64_t id = 0;
    int type = 0;
    if (!ParseCloudRecordFileName(fileName, id, type))
        return std::string();
    std::string key = fileName;
    key[key.find('.')] = '_';
    return key;
}

bool ParseCloudRecordTokenKey(const std::string& key, std::string& fileName)
{
    fileName.clear();
    if (key.find('.') != std::string::npos)
        return false;
    const size_t pos = key.rfind('_');
    if (pos == std::string::npos)
        return false;
    std::string name = key;
    name[pos] = '.';
    uint64_t id = 0;
    int type = 0;
    if (!ParseCloudRecordFileName(name, id, type))
        return false;
    fileName = name;
    return true;
}

const char* CloudRecordCtlOpName(int op)
{
    for (size_t i = 0; i < kOpNameCount; ++i)
    {
        if (kOpNames[i].m_op == op)
            return kOpNames[i].m_name;
    }
    return "unknown";
}

std::string BuildCloudRecordCtlMsg(const CloudRecordCtlMsg& msg)
{
    if (msg.m_op == cloudrecctl_op_unknown)
        return std::string();
    Json::Value root(Json::objectValue);
    root["op"] = CloudRecordCtlOpName(msg.m_op);
    switch (msg.m_op)
    {
    //开始录制：录像 id 与房间号
    case cloudrecctl_op_start:
        root["recordid"] = Json::UInt64(msg.m_recordId);
        root["roomid"] = msg.m_roomId;
        break;

    //暂停、继续、停止：只带录像 id
    case cloudrecctl_op_pause:
    case cloudrecctl_op_resume:
    case cloudrecctl_op_stop:
        root["recordid"] = Json::UInt64(msg.m_recordId);
        break;

    //删除文件：文件名数组
    case cloudrecctl_op_delete:
    {
        Json::Value files(Json::arrayValue);
        for (size_t i = 0; i < msg.m_files.size(); ++i)
            files.append(msg.m_files[i]);
        root["files"] = files;
        break;
    }

    //开始录制的结果
    case cloudrecctl_op_started:
        root["recordid"] = Json::UInt64(msg.m_recordId);
        root["code"] = msg.m_code;
        break;

    //一次录制结束
    case cloudrecctl_op_finished:
        root["recordid"] = Json::UInt64(msg.m_recordId);
        root["file"] = msg.m_file;
        root["bytes"] = Json::UInt64(msg.m_bytes);
        root["durationms"] = Json::Int64(msg.m_durationMs);
        root["frames"] = Json::UInt64(msg.m_frames);
        root["dropped"] = Json::UInt64(msg.m_dropped);
        root["reason"] = msg.m_reason;
        break;

    //合成后的 MP4 已上传完
    case cloudrecctl_op_uploaded:
        root["recordid"] = Json::UInt64(msg.m_recordId);
        root["file"] = msg.m_file;
        root["bytes"] = Json::UInt64(msg.m_bytes);
        break;

    //探活与回应：只有 op
    default:
        break;
    }
    return writeCompact(root);
}

bool ParseCloudRecordCtlMsg(const std::string& body, CloudRecordCtlMsg& out)
{
    out = CloudRecordCtlMsg();
    Json::Value root;
    if (!parseObject(body, root))
        return false;
    std::string opName;
    if (!readString(root, "op", opName))
        return false;
    out.m_op = opFromName(opName);
    switch (out.m_op)
    {
    //开始录制：录像 id 须大于 0，房间号不能为空
    case cloudrecctl_op_start:
        return readUInt64(root, "recordid", out.m_recordId) && out.m_recordId != 0 &&
               readString(root, "roomid", out.m_roomId) && !out.m_roomId.empty();

    //暂停、继续、停止
    case cloudrecctl_op_pause:
    case cloudrecctl_op_resume:
    case cloudrecctl_op_stop:
        return readUInt64(root, "recordid", out.m_recordId) && out.m_recordId != 0;

    //删除文件：数组里不是字符串的元素视为格式错误
    case cloudrecctl_op_delete:
    {
        const Json::Value& files = root["files"];
        if (!files.isArray())
            return false;
        for (Json::ArrayIndex i = 0; i < files.size(); ++i)
        {
            if (!files[i].isString())
                return false;
            out.m_files.push_back(files[i].asString());
        }
        return true;
    }

    //开始录制的结果
    case cloudrecctl_op_started:
        return readUInt64(root, "recordid", out.m_recordId) && readInt(root, "code", out.m_code);

    //一次录制结束
    case cloudrecctl_op_finished:
        return readUInt64(root, "recordid", out.m_recordId) && readString(root, "file", out.m_file) &&
               readUInt64(root, "bytes", out.m_bytes) && readInt64(root, "durationms", out.m_durationMs) &&
               readUInt64(root, "frames", out.m_frames) && readUInt64(root, "dropped", out.m_dropped) &&
               readInt(root, "reason", out.m_reason);

    //合成后的 MP4 已上传完
    case cloudrecctl_op_uploaded:
        return readUInt64(root, "recordid", out.m_recordId) && readString(root, "file", out.m_file) &&
               readUInt64(root, "bytes", out.m_bytes);

    //探活、回应与不认识的 op：没有其他字段要读
    default:
        return true;
    }
}
