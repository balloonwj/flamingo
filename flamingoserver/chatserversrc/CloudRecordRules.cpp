/**
 *  云端录像的数据结构与纯规则，见 CloudRecordRules.h。
 *  balloonwj@qq.com   2026-10-06
 */
#include "CloudRecordRules.h"

#include "Protocol.h"

namespace {

//一天的毫秒数
const int64_t kMsPerDay = 24LL * 60 * 60 * 1000;
//1 GB 的字节数
const int64_t kBytesPerGb = 1024LL * 1024 * 1024;
//默认标题（共享标题为空时）
const char* const kDefaultTitle = u8"云端录像";

//UTF-8 首字节对应的字符长度；不是合法首字节时返回 0
size_t utf8CharLen(unsigned char c)
{
    if (c < 0x80)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 0;
}

//数 UTF-8 字符数；不是合法 UTF-8 时返回 false
bool countUtf8(const std::string& s, size_t& count)
{
    count = 0;
    size_t i = 0;
    while (i < s.size())
    {
        const size_t len = utf8CharLen(static_cast<unsigned char>(s[i]));
        if (len == 0 || i + len > s.size())
            return false;
        for (size_t k = 1; k < len; ++k)
        {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80)
                return false;
        }
        i += len;
        ++count;
    }
    return true;
}

//截到前 maxChars 个 UTF-8 字符（输入须是合法 UTF-8）
std::string truncateUtf8(const std::string& s, size_t maxChars)
{
    size_t i = 0;
    size_t chars = 0;
    while (i < s.size() && chars < maxChars)
    {
        const size_t len = utf8CharLen(static_cast<unsigned char>(s[i]));
        if (len == 0 || i + len > s.size())
            break;
        i += len;
        ++chars;
    }
    return s.substr(0, i);
}

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string trim(const std::string& s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && isSpace(s[b]))
        ++b;
    while (e > b && isSpace(s[e - 1]))
        --e;
    return s.substr(b, e - b);
}

} // namespace

//类内初始化的静态常量的类外定义：被 ODR-use（如 doctest 的 CHECK 按引用取操作数）时 g++ 要求有类外定义，
//MSVC 不要求
const size_t CloudRecordRules::kTitleMaxChars;
const int64_t CloudRecordRules::kExpireWarnBeforeMs;
const int64_t CloudRecordRules::kTokenTtlMs;
const int CloudRecordRules::kListPageDefault;
const int CloudRecordRules::kListPageMax;

CloudRecordRow::CloudRecordRow()
    : m_id(0)
    , m_enterpriseId(0)
    , m_owner(0)
    , m_source(cloudrecord_source_share)
    , m_ctype(0)
    , m_cid(0)
    , m_startTime(0)
    , m_endTime(0)
    , m_durationMs(0)
    , m_frecBytes(0)
    , m_bytes(0)
    , m_audioOnly(false)
    , m_status(cloudrecord_status_recording)
    , m_expireTime(0)
    , m_warned(false)
    , m_createTime(0)
{
}

CloudRecordShareRow::CloudRecordShareRow()
    : m_recordId(0)
    , m_ctype(0)
    , m_cid(0)
    , m_sender(0)
    , m_sendTime(0)
{
}

int64_t CloudRecordRules::expireTimeFor(int64_t baseMs, int64_t retainDays)
{
    if (baseMs <= 0 || retainDays <= 0)
        return 0;
    return baseMs + retainDays * kMsPerDay;
}

int64_t CloudRecordRules::quotaBytes(int64_t quotaGb)
{
    return quotaGb > 0 ? quotaGb * kBytesPerGb : 0;
}

bool CloudRecordRules::quotaAllows(int64_t usedBytes, int64_t quotaGb, int64_t extraBytes)
{
    const int64_t limit = quotaBytes(quotaGb);
    if (limit <= 0)
        return false;
    const int64_t used = usedBytes > 0 ? usedBytes : 0;
    const int64_t extra = extraBytes > 0 ? extraBytes : 0;
    //开始录制时 extra 为 0：已用量等于配额时也不允许，避免录到一半就超出
    if (extra == 0)
        return used < limit;
    return used + extra <= limit;
}

bool CloudRecordRules::shouldWarnExpire(int64_t expireTime, bool warned, int64_t nowMs)
{
    if (expireTime <= 0 || warned)
        return false;
    return nowMs < expireTime && expireTime - nowMs <= kExpireWarnBeforeMs;
}

bool CloudRecordRules::isExpired(int64_t expireTime, int64_t nowMs)
{
    return expireTime > 0 && expireTime <= nowMs;
}

bool CloudRecordRules::normalizeTitle(const std::string& title, std::string& trimmed)
{
    trimmed = trim(title);
    size_t chars = 0;
    if (!countUtf8(trimmed, chars))
        return false;
    return chars >= 1 && chars <= kTitleMaxChars;
}

std::string CloudRecordRules::defaultTitle(const std::string& shareTitle)
{
    const std::string t = trim(shareTitle);
    size_t chars = 0;
    if (t.empty() || !countUtf8(t, chars))
        return kDefaultTitle;
    return truncateUtf8(t, kTitleMaxChars);
}

bool CloudRecordRules::canView(int64_t userId, int64_t ownerId, const std::vector<CloudRecordShareRow>& shares,
                               const std::set<int64_t>& memberGroups)
{
    if (userId <= 0)
        return false;
    if (userId == ownerId)
        return true;
    for (size_t i = 0; i < shares.size(); ++i)
    {
        const CloudRecordShareRow& s = shares[i];
        //单聊：接收方或发送人
        if (s.m_ctype == conversation_single && (s.m_cid == userId || s.m_sender == userId))
            return true;
        //群聊：该群的成员
        if (s.m_ctype == conversation_group && memberGroups.find(s.m_cid) != memberGroups.end())
            return true;
    }
    return false;
}

bool CloudRecordRules::canDownload(int kind, bool isOwner, bool canView, const CloudRecordRow& row)
{
    switch (kind)
    {
    //原始帧文件：只给开启者合成用，文件须还在
    case cloudrecord_file_frec:
        return isOwner && row.m_source == cloudrecord_source_share && !row.m_frecFile.empty();

    //最终文件：能看、已可观看、文件已上传
    case cloudrecord_file_final:
        return (isOwner || canView) && row.m_status == cloudrecord_status_ready && !row.m_file.empty();

    //不认识的种类
    default:
        return false;
    }
}

int CloudRecordRules::statusAfterFinished(uint64_t frames)
{
    return frames > 0 ? cloudrecord_status_pending : cloudrecord_status_failed;
}

bool CloudRecordRules::canSend(const CloudRecordRow& row)
{
    return row.m_status == cloudrecord_status_ready && !row.m_file.empty();
}
