/**
 *  屏幕共享业务规则的实现，ScreenShareRules.cpp
 *  规则说明见 ScreenShareRules.h。
 *
 *  balloonwj@qq.com   2026-10-02
 */
#include "ScreenShareRules.h"

#include <stdio.h>
#include <algorithm>
#include "Protocol.h"
#include "ScreenShareLinkTable.h"   //screenshare_link_status（分享链接，第 25 期）
#include "ScreenShareProtocol.h"    //buildCardContentFor 按卡片格式组装 content
#include "../jsoncpp1.9.0/json.h"   //buildEndTipContent 构造系统提示的 content
#include "CallProtocol.h"

namespace {

//时长换算：每分钟的秒数、每小时的秒数
const int64_t kSecondsPerMinute = 60;
const int64_t kSecondsPerHour = 3600;

//结束提示的固定前缀「屏幕共享已结束，时长 」（UTF-8，末尾含一个半角空格）。用转义写出字节，
//不依赖编译器对源文件与执行字符集的设置
const char* kEndTipPrefix = "\xE5\xB1\x8F\xE5\xB9\x95\xE5\x85\xB1\xE4\xBA\xAB\xE5\xB7\xB2\xE7\xBB\x93\xE6\x9D\x9F"
                            "\xEF\xBC\x8C\xE6\x97\xB6\xE9\x95\xBF ";

//格式化时长所用缓冲区的大小：最长的情况是 19 位小时数加 ":mm:ss"，64 字节足够
const size_t kDurationBufSize = 64;

//全角空格 U+3000 的 UTF-8 编码与字节数（共享标题与描述去掉首尾空白时一并去掉，第 15 期）
const char kFullWidthSpace[] = "\xE3\x80\x80";
const size_t kFullWidthSpaceBytes = 3;

//UTF-8 首字节的判定：掩码与取值，依次为 1～4 字节的字符；后续字节的掩码与取值
const unsigned char kUtf8Lead1Mask = 0x80;
const unsigned char kUtf8Lead1Bits = 0x00;
const unsigned char kUtf8Lead2Mask = 0xE0;
const unsigned char kUtf8Lead2Bits = 0xC0;
const unsigned char kUtf8Lead3Mask = 0xF0;
const unsigned char kUtf8Lead3Bits = 0xE0;
const unsigned char kUtf8Lead4Mask = 0xF8;
const unsigned char kUtf8Lead4Bits = 0xF0;
const unsigned char kUtf8TrailMask = 0xC0;
const unsigned char kUtf8TrailBits = 0x80;
//1～4 字节字符的字节数
const size_t kUtf8Bytes1 = 1;
const size_t kUtf8Bytes2 = 2;
const size_t kUtf8Bytes3 = 3;
const size_t kUtf8Bytes4 = 4;

//该字节是否是半角空白（空格、制表、回车、换行）
bool isAsciiBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

//去掉首尾的半角空白与全角空格
void trimShareText(std::string& s)
{
    for (;;)
    {
        if (!s.empty() && isAsciiBlank(s[0]))
            s.erase(0, 1);
        else if (s.compare(0, kFullWidthSpaceBytes, kFullWidthSpace) == 0)
            s.erase(0, kFullWidthSpaceBytes);
        else
            break;
    }
    for (;;)
    {
        if (!s.empty() && isAsciiBlank(s[s.size() - 1]))
            s.erase(s.size() - 1);
        else if (s.size() >= kFullWidthSpaceBytes &&
                 s.compare(s.size() - kFullWidthSpaceBytes, kFullWidthSpaceBytes, kFullWidthSpace) == 0)
            s.erase(s.size() - kFullWidthSpaceBytes);
        else
            break;
    }
}

/**
 *  校验一段文字是合法 UTF-8 并数出字符个数。
 *  @param s         待检查的文字
 *  @param outChars  [出参] 字符个数（合法时有效）
 *  @return 合法 UTF-8 返回 true
 */
bool countUtf8Chars(const std::string& s, size_t& outChars)
{
    size_t chars = 0;
    size_t i = 0;
    while (i < s.size())
    {
        const unsigned char lead = static_cast<unsigned char>(s[i]);
        size_t bytes = 0;
        if ((lead & kUtf8Lead1Mask) == kUtf8Lead1Bits)
            bytes = kUtf8Bytes1;
        else if ((lead & kUtf8Lead2Mask) == kUtf8Lead2Bits)
            bytes = kUtf8Bytes2;
        else if ((lead & kUtf8Lead3Mask) == kUtf8Lead3Bits)
            bytes = kUtf8Bytes3;
        else if ((lead & kUtf8Lead4Mask) == kUtf8Lead4Bits)
            bytes = kUtf8Bytes4;
        else
            return false;
        if (i + bytes > s.size())
            return false;
        for (size_t k = 1; k < bytes; ++k)
        {
            if ((static_cast<unsigned char>(s[i + k]) & kUtf8TrailMask) != kUtf8TrailBits)
                return false;
        }
        i += bytes;
        ++chars;
    }
    outChars = chars;
    return true;
}

} // namespace

//static const 整型成员被按引用使用（如测试里的 CHECK）时需要类外定义，否则 GCC 下链接失败
const size_t ScreenShareRules::kShareTitleMaxChars;
const size_t ScreenShareRules::kShareDescMaxChars;

int ScreenShareRules::checkShareInfo(std::string& title, std::string& desc, int ctype, int64_t cid,
                                     IScreenShareSensitiveChecker* checker)
{
    trimShareText(title);
    trimShareText(desc);
    size_t titleChars = 0;
    size_t descChars = 0;
    if (title.empty() || !countUtf8Chars(title, titleChars) || titleChars > kShareTitleMaxChars)
        return error_code_bad_request;
    if (!countUtf8Chars(desc, descChars) || descChars > kShareDescMaxChars)
        return error_code_bad_request;
    //格式合格后才查敏感词，不为格式不合法的请求记拦截记录
    if (checker != NULL)
    {
        std::vector<std::string> texts;
        texts.push_back(title);
        texts.push_back(desc);
        if (checker->hitShareInfo(ctype, cid, texts))
            return error_code_screenshare_sensitive;
    }
    return error_code_ok;
}

int ScreenShareRules::checkJoinEligibility(const ScreenShareRoom& room, int64_t userId, bool isGroupMember)
{
    if (room.m_ctype == conversation_single)
    {
        //单聊：只有会话双方能加入；isGroupMember 对单聊没有意义，忽略
        if (userId != room.m_convKeyA && userId != room.m_convKeyB)
            return error_code_screenshare_not_member;
    }
    else if (room.m_ctype != conversation_group || !isGroupMember)
    {
        return error_code_screenshare_not_member;
    }

    //被发起人移出且不允许再次加入（第 14 期）。调用方先判资格、再校验密码，所以这类人不会先被要求输入密码
    if (room.isBanned(userId))
        return error_code_screenshare_removed;
    return error_code_ok;
}

int ScreenShareRules::checkLinkJoin(const ScreenShareRoom& room, int64_t userId, int memberCode, int linkStatus,
                                    const std::string& linkRoomId, bool linkNoPassword, bool sameEnterprise,
                                    bool& outViaLink, bool& outSkipPassword)
{
    outViaLink = false;
    outSkipPassword = false;
    //链接有效、且正是这个房间的链接
    const bool linkOk = (linkStatus == screenshare_link_active && linkRoomId == room.m_roomId);
    //会话成员：资格不变；带了有效的免密链接时不再要密码
    if (memberCode == error_code_ok)
    {
        outSkipPassword = linkOk && linkNoPassword;
        return error_code_ok;
    }
    //不是会话成员以外的拒绝原因（如已被移出）原样返回；没带链接码时也原样返回
    if (memberCode != error_code_screenshare_not_member || linkStatus == screenshare_link_invalid)
        return memberCode;
    if (!linkOk)
        return error_code_screenshare_link_invalid;
    if (!sameEnterprise)
        return error_code_screenshare_link_enterprise;
    //凭链接加入的人同样受「移出且不允许再加入」约束（第 14 期）
    if (room.isBanned(userId))
        return error_code_screenshare_removed;
    outViaLink = true;
    outSkipPassword = linkNoPassword;
    return error_code_ok;
}

int ScreenShareRules::checkCallConflict(bool inCall, int callState, int64_t callPeerId, int ctype,
                                        int64_t sharePeerId)
{
    if (!inCall)
        return error_code_ok;
    //通话中只允许与通话对端在单聊里共享；响铃中的通话还没有建立，不算
    if (callState == call_state_connected && ctype == conversation_single && callPeerId == sharePeerId)
        return error_code_ok;
    return error_code_screenshare_in_call;
}

bool ScreenShareRules::isValidPassword(const std::string& password)
{
    //共享密码的位数
    const size_t kPasswordLength = 6;
    if (password.size() != kPasswordLength)
        return false;
    for (size_t i = 0; i < password.size(); ++i)
    {
        if (password[i] < '0' || password[i] > '9')
            return false;
    }
    return true;
}

int ScreenShareRules::checkJoinPassword(const ScreenShareRoom& room, int64_t userId, const std::string& provided,
                                        bool inCallWithHost)
{
    //没有密码；发起人自己（宽限期内重新登录）；已在房间里的人重复加入；通话对方；强制模式下掉线后回来的观看者：都不校验
    if (room.m_password.empty() || userId == room.m_hostId || room.findMember(userId) != NULL || inCallWithHost ||
        room.isForcedAway(userId))
        return error_code_ok;
    return provided == room.m_password ? error_code_ok : error_code_screenshare_bad_password;
}

std::vector<int64_t> ScreenShareRules::startNotifyTargets(const ScreenShareRoom& room,
                                                          const std::vector<int64_t>& groupMembers)
{
    std::vector<int64_t> targets;
    if (room.m_ctype == conversation_single)
    {
        targets.push_back(conversationIdFor(room, room.m_hostId));
        return targets;
    }

    for (size_t i = 0; i < groupMembers.size(); ++i)
    {
        int64_t uid = groupMembers[i];
        if (uid == room.m_hostId)
            continue;
        //群成员名单理应不重复，这里仍按 userid 去重，避免同一人收到两条推送
        if (std::find(targets.begin(), targets.end(), uid) != targets.end())
            continue;
        targets.push_back(uid);
    }
    return targets;
}

int64_t ScreenShareRules::conversationIdFor(const ScreenShareRoom& room, int64_t receiverId)
{
    if (room.m_ctype == conversation_single)
    {
        if (receiverId == room.m_convKeyA)
            return room.m_convKeyB;
        if (receiverId == room.m_convKeyB)
            return room.m_convKeyA;
        //会话双方以外的人只可能是凭分享链接加入的链接访客（第 25 期），他们没有对应的单聊会话
        return 0;
    }
    return room.m_convKeyA;
}

std::string ScreenShareRules::formatEndTip(int64_t durationSeconds)
{
    if (durationSeconds < 0)
        durationSeconds = 0;

    const long long hours = static_cast<long long>(durationSeconds / kSecondsPerHour);
    const long long minutes = static_cast<long long>((durationSeconds % kSecondsPerHour) / kSecondsPerMinute);
    const long long seconds = static_cast<long long>(durationSeconds % kSecondsPerMinute);

    char buf[kDurationBufSize] = { 0 };
    if (hours > 0)
        snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", hours, minutes, seconds);
    else
        snprintf(buf, sizeof(buf), "%02lld:%02lld", minutes, seconds);
    return std::string(kEndTipPrefix) + buf;
}

std::string ScreenShareRules::buildEndTipContent(int64_t durationSeconds)
{
    if (durationSeconds < 0)
        durationSeconds = 0;

    Json::Value args(Json::objectValue);
    args["duration"] = static_cast<Json::Int64>(durationSeconds);

    Json::Value root;
    root["text"]    = formatEndTip(durationSeconds);
    root["tipkind"] = kSystemTipScreenShareEnd;
    root["tipargs"] = args;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    return Json::writeString(wb, root);
}

std::string ScreenShareRules::buildCardContentFor(const ScreenShareRoom& room, const std::string& title,
                                                  const std::string& desc)
{
    //发起人写最初的发起人，不写当前主持人：卡片记录的是「谁发起了这场共享」，与这条卡片消息的发送者一致
    return ScreenShareProtocol::buildCardContent(room.m_roomId, room.m_starterId, room.m_starterName, room.m_startTime,
                                                 !room.m_password.empty(), room.m_forced, title, desc);
}
