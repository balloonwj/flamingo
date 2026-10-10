/**
 *  SubtitleModel.cpp
 *
 *  聊天字幕模式内容模型的实现，说明见 SubtitleModel.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */

#include "stdafx.h"

#include "SubtitleModel.h"

#include "../../Common/I18n/I18n.h"
#include "../../Common/Util/EncodeUtil.h"
#include "../../net/MessageProtocolClient.h"
#include "../../net/Protocol.h"
#include "../BalloonChatWnd/ChatVoiceBubble.h"   // FormatVoiceDuration：语音占位文字里的时长写法
#include "../BalloonChatWnd/ChatMarkdown.h"      // DigestForNotify：Markdown 消息的字幕去掉标记
#include "SubtitleTextFit.h"                      // kEllipsisChar

namespace subtitle {

namespace {

//判断一个字符是否算作空白（要合并成一个空格的字符）
bool IsCollapsibleSpace(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x3000 || c == 0x00A0;
}

//UTF-16 高位代理的范围：截断时不能停在高位代理之后
const wchar_t kHighSurrogateFirst = 0xD800;
const wchar_t kHighSurrogateLast = 0xDBFF;

} // anonymous namespace

CSubtitleModel::CSubtitleModel()
{
    Reset();
}

void CSubtitleModel::Reset()
{
    m_current = SubtitleLine();
    m_current.m_kind = kLineWaiting;
    m_current.m_content = WaitingHintText();
    //来源标注属于会话而不是某一条消息，重新进入字幕模式时保留
    m_current.m_origin = m_origin;
    m_recent.clear();
}

bool CSubtitleModel::OnIncoming(const SubtitleIncoming& in)
{
    //本人消息与系统提示不上字幕（用户确认自己发的消息不显示）
    if (in.m_fromSelf || in.m_isSystem)
        return false;
    //同一条消息只显示一次：重复推送不会把字幕换回旧消息
    if (!rememberMsgid(in.m_msgid))
        return false;
    m_current.m_kind = kLineMessage;
    m_current.m_speaker = in.m_speaker;
    m_current.m_content = in.m_content;
    m_current.m_msgid = in.m_msgid;
    return true;
}

bool CSubtitleModel::OnRecalled(long long msgid, const std::wstring& noticeText)
{
    //只有正在显示的那条消息被撤回才需要换掉；等待提示与已是撤回提示的状态都不动
    if (m_current.m_kind != kLineMessage || msgid <= 0 || m_current.m_msgid != msgid)
        return false;
    m_current.m_kind = kLineNotice;
    m_current.m_speaker.clear();
    m_current.m_content = noticeText;
    return true;
}

void CSubtitleModel::SetOrigin(const std::wstring& origin)
{
    m_origin = origin;
    m_current.m_origin = origin;
}

const SubtitleLine& CSubtitleModel::Current() const
{
    return m_current;
}

bool CSubtitleModel::rememberMsgid(long long msgid)
{
    if (msgid <= 0)
        return true;
    for (std::deque<long long>::const_iterator it = m_recent.begin(); it != m_recent.end(); ++it)
    {
        if (*it == msgid)
            return false;
    }
    m_recent.push_back(msgid);
    if (m_recent.size() > kRecentMsgidCount)
        m_recent.pop_front();
    return true;
}

std::wstring CollapseWhitespace(const std::wstring& s)
{
    std::wstring out;
    out.reserve(s.size());
    bool pendingSpace = false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const wchar_t c = s[i];
        if (IsCollapsibleSpace(c))
        {
            //开头的空白丢掉，中间的连续空白记为一个待写的空格
            if (!out.empty())
                pendingSpace = true;
            continue;
        }
        if (pendingSpace)
        {
            out += L' ';
            pendingSpace = false;
        }
        out += c;
    }
    return out;
}

std::wstring BuildSubtitleContent(const net::MessageNotify& notify)
{
    //与会话列表的预览同一口径：文本取正文（内联表情换成 [表情]，Markdown 去掉标记），其它类型取占位文字
    std::wstring text = EncodeUtil::Utf8ToUnicode(balloonchatwnd::DigestForNotify(notify));
    //文件补上文件名、语音补上时长：字幕上只有这一行，信息越具体越好
    if (notify.m_msgtype == message_type_file && !notify.m_fileName.empty())
    {
        text += L' ';
        text += EncodeUtil::Utf8ToUnicode(notify.m_fileName);
    }
    else if (notify.m_msgtype == message_type_voice && notify.m_voiceDuration > 0)
    {
        text += L' ';
        text += balloonchatwnd::FormatVoiceDuration(notify.m_voiceDuration);
    }
    text = CollapseWhitespace(text);
    //超长的内容先截掉：字幕最多三行，这么多字无论如何显示不下，排版时也只需测量有限长度的文字
    if (text.size() > kMaxContentChars)
    {
        size_t keep = kMaxContentChars - 1;
        //不停在代理对中间
        if (keep > 0 && text[keep - 1] >= kHighSurrogateFirst && text[keep - 1] <= kHighSurrogateLast)
            --keep;
        text = text.substr(0, keep);
        text += kEllipsisChar;
    }
    return text;
}

SubtitleIncoming MakeSubtitleIncoming(const net::MessageNotify& notify, int64_t selfUserId,
                                      const std::wstring& speaker)
{
    SubtitleIncoming in;
    in.m_msgid = notify.m_msgid;
    in.m_fromSelf = (selfUserId != 0 && notify.m_from == selfUserId);
    in.m_isSystem = (notify.m_msgtype == message_type_system);
    in.m_speaker = speaker;
    //系统提示不上字幕，不必生成内容
    if (!in.m_isSystem)
        in.m_content = BuildSubtitleContent(notify);
    return in;
}

const wchar_t* WaitingHintText()
{
    return TR(L"等待新消息…");
}

void CGroupCardCache::SetGroupCards(int64_t groupId, const std::map<int64_t, std::wstring>& cards)
{
    m_cards[groupId] = cards;
}

void CGroupCardCache::ForgetGroup(int64_t groupId)
{
    m_cards.erase(groupId);
}

void CGroupCardCache::Clear()
{
    m_cards.clear();
}

bool CGroupCardCache::HasGroup(int64_t groupId) const
{
    return m_cards.find(groupId) != m_cards.end();
}

std::wstring CGroupCardCache::ResolveSpeaker(int64_t groupId, int64_t userId, const std::wstring& fallback) const
{
    std::map<int64_t, std::map<int64_t, std::wstring> >::const_iterator group = m_cards.find(groupId);
    if (group == m_cards.end())
        return fallback;
    std::map<int64_t, std::wstring>::const_iterator member = group->second.find(userId);
    if (member == group->second.end())
        return fallback;
    //群名片只有空白时与没设一样
    const std::wstring card = CollapseWhitespace(member->second);
    return card.empty() ? fallback : card;
}

} // namespace subtitle
