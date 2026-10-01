/**
 *  自动群名生成规则的实现，GroupAutoName.cpp
 *
 *  规则说明见 GroupAutoName.h。
 *
 *  balloonwj@qq.com   2026-09-30
 */
#include "GroupAutoName.h"

#include <algorithm>

namespace chatserver {

namespace {

// 群名里最多出现的成员昵称个数；与此前客户端建群时自动拼名的上限一致。
const size_t kMaxNamesInGroupName = 4;

// 群名最多的字符数，与 t_group.f_name 的 varchar(64) 一致（MySQL utf8 按字符计）。
const int kMaxGroupNameChars = 64;

// 昵称之间的分隔符、人数更多时的后缀，以及没有可用昵称时的兜底群名。
const char* const kNameSeparator   = u8"、";
const char* const kMoreSuffix      = u8"等";
const char* const kFallbackName    = u8"群聊";

// UTF-8 后续字节的标志位：形如 10xxxxxx 的字节不是一个字符的开头。
const unsigned char kUtf8ContinuationMask  = 0xC0;
const unsigned char kUtf8ContinuationValue = 0x80;

// 排序比较：入群时间早的在前，时间相同按 userid 升序，保证结果与传入顺序无关。
bool JoinedEarlier(const GroupNameMember& a, const GroupNameMember& b)
{
    if (a.jointime != b.jointime)
    {
        return a.jointime < b.jointime;
    }
    return a.userid < b.userid;
}

// 把 UTF-8 文本截断到最多 maxChars 个字符，只在字符边界上截。
std::string TruncateUtf8(const std::string& text, int maxChars)
{
    int chars = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const bool isCharStart = ((c & kUtf8ContinuationMask) != kUtf8ContinuationValue);
        if (isCharStart)
        {
            if (chars == maxChars)
            {
                return text.substr(0, i);
            }
            ++chars;
        }
    }
    return text;
}

} // anonymous namespace

std::string BuildAutoGroupName(int64_t ownerid, const std::vector<GroupNameMember>& members)
{
    //挑出除群主外、有昵称的成员，按入群先后排好
    std::vector<GroupNameMember> others;
    for (size_t i = 0; i < members.size(); ++i)
    {
        if (members[i].userid == ownerid || members[i].nickname.empty())
            continue;
        others.push_back(members[i]);
    }
    if (others.empty())
        return kFallbackName;

    std::sort(others.begin(), others.end(), JoinedEarlier);

    std::string name;
    const size_t shown = (std::min)(others.size(), kMaxNamesInGroupName);
    for (size_t i = 0; i < shown; ++i)
    {
        if (i > 0)
            name += kNameSeparator;
        name += others[i].nickname;
    }
    if (others.size() > kMaxNamesInGroupName)
        name += kMoreSuffix;

    return TruncateUtf8(name, kMaxGroupNameChars);
}

} // namespace chatserver
