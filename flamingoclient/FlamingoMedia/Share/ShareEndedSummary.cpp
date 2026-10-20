/**
 *  屏幕共享结束提示的名单与文字，说明见 ShareEndedSummary.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "ShareEndedSummary.h"

#include <algorithm>
#include "ShareRoomPolicy.h"                 // FormatShareDuration
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找

namespace screenshare {

namespace {

//userid 是否已在列表里
bool contains(const std::vector<int64_t>& ids, int64_t id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

//用顿号连接名字
std::wstring joinNames(const std::vector<std::wstring>& names, size_t count)
{
    std::wstring out;
    for (size_t i = 0; i < names.size() && i < count; ++i)
    {
        if (i > 0)
            out += TR(L"、");
        out += names[i];
    }
    return out;
}

} // namespace

ShareParticipantLog::ShareParticipantLog()
    : m_inCall(false)
    , m_hostId(0)
{
}

void ShareParticipantLog::Reset(const std::string& roomId, bool inCall)
{
    m_roomId = roomId;
    m_inCall = inCall;
    m_hostId = 0;
    m_people.clear();
    m_sharers.clear();
}

const std::string& ShareParticipantLog::RoomId() const
{
    return m_roomId;
}

bool ShareParticipantLog::InCall() const
{
    return m_inCall;
}

void ShareParticipantLog::SetInitiator(int64_t hostId)
{
    if (m_hostId == 0 && hostId != 0)
        m_hostId = hostId;
}

void ShareParticipantLog::Observe(const ShareStateNotify& n)
{
    if (m_roomId.empty() || n.m_roomId != m_roomId)
        return;
    //发起人：第一次看到的主持人，之后不再改（2026-10-13）。主持人移交后推送里的 hostId 是接任的人；原来跟着改的话，
    //发起人既不在最前、也不在其余分享人里（他当主持人时被跳过），结束提示会把他算成参与者
    if (m_hostId == 0 && n.m_hostId != 0)
        m_hostId = n.m_hostId;
    for (size_t i = 0; i < n.m_members.size(); ++i)
    {
        const ShareMember& m = n.m_members[i];
        if (m.m_userId == 0)
            continue;
        if (!contains(m_people, m.m_userId))
            m_people.push_back(m.m_userId);
        //发起人另外放在分享人最前，这里只记其余共享过屏幕的人
        if (m.m_sharing && m.m_userId != m_hostId && !contains(m_sharers, m.m_userId))
            m_sharers.push_back(m.m_userId);
    }
}

std::vector<int64_t> ShareParticipantLog::Sharers() const
{
    std::vector<int64_t> out;
    if (m_hostId != 0)
        out.push_back(m_hostId);
    for (size_t i = 0; i < m_sharers.size(); ++i)
    {
        if (m_sharers[i] != m_hostId)
            out.push_back(m_sharers[i]);
    }
    return out;
}

std::vector<int64_t> ShareParticipantLog::Participants() const
{
    std::vector<int64_t> out;
    for (size_t i = 0; i < m_people.size(); ++i)
    {
        if (m_people[i] != m_hostId && !contains(m_sharers, m_people[i]))
            out.push_back(m_people[i]);
    }
    return out;
}

std::wstring ShareEndedNameOf(int64_t userId, int64_t selfId, const std::wstring& displayName)
{
    if (userId != 0 && userId == selfId)
        return TR(L"你");
    return displayName;
}

ShareEndedTexts FormatShareEndedTexts(int64_t durationSeconds, const std::vector<std::wstring>& sharers,
                                      const std::vector<std::wstring>& participants, int maxNames)
{
    ShareEndedTexts t;
    t.m_duration = TRF(L"时长 {0}", FormatShareDuration(durationSeconds < 0 ? 0 : durationSeconds));
    t.m_sharers = TRF(L"分享人：{0}", joinNames(sharers, sharers.size()));
    const size_t limit = static_cast<size_t>(maxNames > 0 ? maxNames : 1);
    if (participants.empty())
        t.m_participants = TR(L"参与者：无");
    else if (participants.size() <= limit)
        t.m_participants = TRF(L"参与者：{0}", joinNames(participants, participants.size()));
    else
        t.m_participants = TRF(L"参与者：{0} 等 {1} 人", joinNames(participants, limit),
                               static_cast<int>(participants.size()));
    return t;
}

} // namespace screenshare
