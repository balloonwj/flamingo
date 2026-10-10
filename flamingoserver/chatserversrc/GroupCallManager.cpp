/**
 *  群通话房间管理器实现，GroupCallManager.cpp
 *  规则与线程约束见 GroupCallManager.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "GroupCallManager.h"

#include <algorithm>
#include <set>
#include <sstream>
#include "Protocol.h"                // error_code_groupcall_*
#include "GroupCallProtocol.h"       // groupcall_media
#include "../utils/RelayToken.h"

const int GroupCallManager::kDefaultMaxMembers;
const int GroupCallManager::kDefaultMaxCameras;
const int64_t GroupCallManager::kDefaultOfflineGraceMs;
const int64_t GroupCallManager::kRingTimeoutMs;
const int GroupCallManager::kMaxSeat;
const int GroupCallManager::kFirstSeat;
const int64_t GroupCallManager::kRelayTokenTtlMs;

namespace {

//房间号前缀：与屏幕共享（ss-）、文件共享（share-）、P2P（p2p-）的中转 sessionid 区分开
const char* kRoomIdPrefix = "gc-";

//人数上限的下限：至少要容得下两个人
const int kMinMembers = 2;

//成员排序的比较：已加入的在前、按座位号升序；振铃中的在后、保持原有先后（配合 stable_sort）
bool memberOrderLess(const GroupCallMember& a, const GroupCallMember& b)
{
    const bool aJoined = (a.m_state == groupcall_member_joined);
    const bool bJoined = (b.m_state == groupcall_member_joined);
    if (aJoined != bJoined)
    {
        return aJoined;
    }
    if (aJoined)
    {
        return a.m_seat < b.m_seat;
    }
    return false;
}

} // namespace

GroupCallConfig::GroupCallConfig()
    : m_relayPort(0)
    , m_maxMembers(GroupCallManager::kDefaultMaxMembers)
    , m_maxCameras(GroupCallManager::kDefaultMaxCameras)
    , m_offlineGraceMs(GroupCallManager::kDefaultOfflineGraceMs)
{
}

GroupCallMember::GroupCallMember()
    : m_userId(0)
    , m_state(groupcall_member_ringing)
    , m_seat(0)
    , m_mic(false)
    , m_camera(false)
    , m_inviterId(0)
    , m_inviteTime(0)
    , m_joinTime(0)
    , m_offlineSince(0)
{
}

GroupCallRoom::GroupCallRoom()
    : m_groupId(0)
    , m_initiatorId(0)
    , m_media(groupcall_media_voice)
    , m_startTime(0)
    , m_cardMsgId(0)
    , m_allMuted(false)
{
}

const GroupCallMember* GroupCallRoom::findMember(int64_t userId) const
{
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        if (m_members[i].m_userId == userId)
        {
            return &m_members[i];
        }
    }
    return NULL;
}

int GroupCallRoom::joinedCount() const
{
    int count = 0;
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        if (m_members[i].m_state == groupcall_member_joined)
        {
            ++count;
        }
    }
    return count;
}

int GroupCallRoom::cameraCount() const
{
    int count = 0;
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        if (m_members[i].m_state == groupcall_member_joined && m_members[i].m_camera)
        {
            ++count;
        }
    }
    return count;
}

std::vector<int64_t> GroupCallRoom::participantIds() const
{
    std::vector<int64_t> ids;
    ids.reserve(m_members.size());
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        ids.push_back(m_members[i].m_userId);
    }
    return ids;
}

std::vector<int64_t> GroupCallRoom::joinedIds() const
{
    std::vector<int64_t> ids;
    for (size_t i = 0; i < m_members.size(); ++i)
    {
        if (m_members[i].m_state == groupcall_member_joined)
        {
            ids.push_back(m_members[i].m_userId);
        }
    }
    return ids;
}

GroupCallReaped::GroupCallReaped()
    : m_userId(0)
    , m_ended(false)
{
}

GroupCallManager::GroupCallManager()
    : m_roomSeq(0)
{
}

GroupCallManager::~GroupCallManager()
{
}

void GroupCallManager::configure(const GroupCallConfig& config)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_config = config;
    //人数上限：座位号只有 kFirstSeat~kMaxSeat，越界按默认值
    if (m_config.m_maxMembers < kMinMembers || m_config.m_maxMembers > kMaxSeat)
    {
        m_config.m_maxMembers = kDefaultMaxMembers;
    }
    //摄像头上限：不大于 0 按默认值，且不能超过人数上限
    if (m_config.m_maxCameras < 1)
    {
        m_config.m_maxCameras = kDefaultMaxCameras;
    }
    if (m_config.m_maxCameras > m_config.m_maxMembers)
    {
        m_config.m_maxCameras = m_config.m_maxMembers;
    }
    if (m_config.m_offlineGraceMs <= 0)
    {
        m_config.m_offlineGraceMs = kDefaultOfflineGraceMs;
    }
}

GroupCallConfig GroupCallManager::getConfig() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_config;
}

int GroupCallManager::startCall(int64_t initiatorId, int64_t groupId, int media, const std::vector<int64_t>& invitees,
                                bool mic, bool camera, int64_t now, std::string& outRoomId,
                                std::string& outExistingId, std::vector<int64_t>& outBusy)
{
    outBusy.clear();
    if (initiatorId <= 0 || groupId <= 0 || (media != groupcall_media_voice && media != groupcall_media_video))
    {
        return error_code_bad_request;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    //一个群同时只有一场：把现有房间号交给调用方，客户端改为加入
    std::map<int64_t, std::string>::const_iterator existing = m_groupRoom.find(groupId);
    if (existing != m_groupRoom.end())
    {
        outExistingId = existing->second;
        return error_code_groupcall_exists;
    }
    //发起人已在某个房间里（含振铃中）
    if (m_userRoom.find(initiatorId) != m_userRoom.end())
    {
        return error_code_groupcall_busy;
    }

    //筛出真正要振铃的人：去重、去掉发起人自己、已在别的房间里的记为忙线
    std::vector<int64_t> ringing;
    std::set<int64_t> seen;
    for (size_t i = 0; i < invitees.size(); ++i)
    {
        const int64_t uid = invitees[i];
        if (uid <= 0 || uid == initiatorId || !seen.insert(uid).second)
        {
            continue;
        }
        if (m_userRoom.find(uid) != m_userRoom.end())
        {
            outBusy.push_back(uid);
            continue;
        }
        ringing.push_back(uid);
    }
    //发起人加上振铃的人不能超过上限；超过时整个请求失败，不建房间
    if (static_cast<int>(ringing.size()) + 1 > m_config.m_maxMembers)
    {
        outBusy.clear();
        return error_code_groupcall_room_full;
    }

    GroupCallRoom room;
    std::ostringstream oss;
    oss << kRoomIdPrefix << groupId << "-" << now << "-" << (++m_roomSeq);
    room.m_roomId = oss.str();
    room.m_groupId = groupId;
    room.m_initiatorId = initiatorId;
    room.m_media = media;
    room.m_startTime = now;

    GroupCallMember self;
    self.m_userId = initiatorId;
    self.m_state = groupcall_member_joined;
    self.m_seat = kFirstSeat;
    self.m_mic = mic;
    self.m_camera = camera;
    self.m_joinTime = now;
    room.m_members.push_back(self);

    for (size_t i = 0; i < ringing.size(); ++i)
    {
        GroupCallMember m;
        m.m_userId = ringing[i];
        m.m_state = groupcall_member_ringing;
        m.m_inviterId = initiatorId;
        m.m_inviteTime = now;
        room.m_members.push_back(m);
        m_userRoom[ringing[i]] = room.m_roomId;
    }

    m_rooms[room.m_roomId] = room;
    m_groupRoom[groupId] = room.m_roomId;
    m_userRoom[initiatorId] = room.m_roomId;
    outRoomId = room.m_roomId;
    return error_code_ok;
}

int GroupCallManager::joinCall(const std::string& roomId, int64_t userId, bool mic, bool camera, int64_t now,
                               int& outSeat, bool& outAlready, bool canSpeakWhenAllMuted)
{
    outAlready = false;
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, GroupCallRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_groupcall_not_exist;
    }
    GroupCallRoom& room = it->second;

    //此人在别的群通话房间里（含振铃中）
    std::map<int64_t, std::string>::const_iterator where = m_userRoom.find(userId);
    if (where != m_userRoom.end() && where->second != roomId)
    {
        return error_code_groupcall_busy;
    }

    GroupCallMember* member = NULL;
    for (size_t i = 0; i < room.m_members.size(); ++i)
    {
        if (room.m_members[i].m_userId == userId)
        {
            member = &room.m_members[i];
            break;
        }
    }
    //已加入：幂等，座位不变
    if (member != NULL && member->m_state == groupcall_member_joined)
    {
        outSeat = member->m_seat;
        outAlready = true;
        return error_code_ok;
    }
    //不在房间里的人直接加入：要占一个新名额
    if (member == NULL && static_cast<int>(room.m_members.size()) >= m_config.m_maxMembers)
    {
        return error_code_groupcall_room_full;
    }
    const int seat = lowestFreeSeat(room);
    if (seat < 0)
    {
        return error_code_groupcall_room_full;
    }
    //摄像头人数已达上限时按关闭处理，不让加入失败
    const bool cameraOn = camera && room.cameraCount() < m_config.m_maxCameras;

    if (member == NULL)
    {
        GroupCallMember m;
        m.m_userId = userId;
        room.m_members.push_back(m);
        member = &room.m_members.back();
        m_userRoom[userId] = roomId;
    }
    member->m_state = groupcall_member_joined;
    member->m_seat = seat;
    //全员静音期间，发起人、群主、管理员之外的人一律按关麦克风加入
    member->m_mic = mic && (!room.m_allMuted || canSpeakWhenAllMuted);
    member->m_camera = cameraOn;
    member->m_joinTime = now;
    member->m_offlineSince = 0;
    outSeat = seat;
    sortMembers(room);
    return error_code_ok;
}

int GroupCallManager::declineCall(const std::string& roomId, int64_t userId, GroupCallRoom& outRoom)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, GroupCallRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_groupcall_not_exist;
    }
    const GroupCallMember* member = it->second.findMember(userId);
    if (member == NULL || member->m_state != groupcall_member_ringing)
    {
        return error_code_groupcall_not_member;
    }
    removeMemberLocked(it->second, userId);
    outRoom = it->second;
    return error_code_ok;
}

int GroupCallManager::leaveCall(const std::string& roomId, int64_t userId, bool& outWasRinging, bool& outEnded,
                                GroupCallRoom& outRoom)
{
    outWasRinging = false;
    outEnded = false;
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, GroupCallRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_groupcall_not_exist;
    }
    const GroupCallMember* member = it->second.findMember(userId);
    if (member == NULL)
    {
        return error_code_groupcall_not_member;
    }
    outWasRinging = (member->m_state == groupcall_member_ringing);
    removeAndMaybeEndLocked(it->second, userId, outEnded, outRoom);
    return error_code_ok;
}

int GroupCallManager::inviteMore(const std::string& roomId, int64_t requesterId, const std::vector<int64_t>& invitees,
                                 int64_t now, std::vector<int64_t>& outAdded, std::vector<int64_t>& outBusy)
{
    outAdded.clear();
    outBusy.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, GroupCallRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_groupcall_not_exist;
    }
    GroupCallRoom& room = it->second;
    const GroupCallMember* requester = room.findMember(requesterId);
    if (requester == NULL || requester->m_state != groupcall_member_joined)
    {
        return error_code_groupcall_not_member;
    }

    std::vector<int64_t> ringing;
    std::set<int64_t> seen;
    for (size_t i = 0; i < invitees.size(); ++i)
    {
        const int64_t uid = invitees[i];
        if (uid <= 0 || !seen.insert(uid).second)
        {
            continue;
        }
        //已在本房间里（已加入或振铃中）的人忽略
        if (room.findMember(uid) != NULL)
        {
            continue;
        }
        if (m_userRoom.find(uid) != m_userRoom.end())
        {
            outBusy.push_back(uid);
            continue;
        }
        ringing.push_back(uid);
    }
    if (static_cast<int>(room.m_members.size() + ringing.size()) > m_config.m_maxMembers)
    {
        outBusy.clear();
        return error_code_groupcall_room_full;
    }

    for (size_t i = 0; i < ringing.size(); ++i)
    {
        GroupCallMember m;
        m.m_userId = ringing[i];
        m.m_state = groupcall_member_ringing;
        m.m_inviterId = requesterId;
        m.m_inviteTime = now;
        room.m_members.push_back(m);
        m_userRoom[ringing[i]] = roomId;
    }
    outAdded = ringing;
    return error_code_ok;
}

int GroupCallManager::setMemberMedia(const std::string& roomId, int64_t userId, bool mic, bool camera,
                                     bool canSpeakWhenAllMuted)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, GroupCallRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_groupcall_not_exist;
    }
    GroupCallRoom& room = it->second;
    for (size_t i = 0; i < room.m_members.size(); ++i)
    {
        GroupCallMember& m = room.m_members[i];
        if (m.m_userId != userId)
        {
            continue;
        }
        if (m.m_state != groupcall_member_joined)
        {
            return error_code_groupcall_not_member;
        }
        //要打开摄像头、本人原本没开、且人数已达上限
        if (camera && !m.m_camera && room.cameraCount() >= m_config.m_maxCameras)
        {
            return error_code_groupcall_camera_limit;
        }
        //全员静音期间，没有豁免的人要打开麦克风（原本关着）：拒绝，两项都不改
        if (mic && !m.m_mic && room.m_allMuted && !canSpeakWhenAllMuted)
        {
            return error_code_groupcall_no_permission;
        }
        m.m_mic = mic;
        m.m_camera = camera;
        return error_code_ok;
    }
    return error_code_groupcall_not_member;
}

int GroupCallManager::muteMember(const std::string& roomId, int64_t targetId, bool& outChanged)
{
    outChanged = false;
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, GroupCallRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_groupcall_not_exist;
    }
    for (size_t i = 0; i < it->second.m_members.size(); ++i)
    {
        GroupCallMember& m = it->second.m_members[i];
        if (m.m_userId != targetId)
        {
            continue;
        }
        if (m.m_state != groupcall_member_joined)
        {
            return error_code_groupcall_not_member;
        }
        outChanged = m.m_mic;
        m.m_mic = false;
        return error_code_ok;
    }
    return error_code_groupcall_not_member;
}

int GroupCallManager::muteAll(const std::string& roomId, const std::vector<int64_t>& exemptIds,
                              std::vector<int64_t>& outMuted)
{
    outMuted.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, GroupCallRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_groupcall_not_exist;
    }
    it->second.m_allMuted = true;
    for (size_t i = 0; i < it->second.m_members.size(); ++i)
    {
        GroupCallMember& m = it->second.m_members[i];
        if (m.m_state != groupcall_member_joined || !m.m_mic)
        {
            continue;
        }
        if (std::find(exemptIds.begin(), exemptIds.end(), m.m_userId) != exemptIds.end())
        {
            continue;
        }
        m.m_mic = false;
        outMuted.push_back(m.m_userId);
    }
    return error_code_ok;
}

int GroupCallManager::unmuteAll(const std::string& roomId, bool& outChanged)
{
    outChanged = false;
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, GroupCallRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_groupcall_not_exist;
    }
    outChanged = it->second.m_allMuted;
    it->second.m_allMuted = false;
    return error_code_ok;
}

int GroupCallManager::removeMember(const std::string& roomId, int64_t targetId, bool& outEnded, GroupCallRoom& outRoom)
{
    outEnded = false;
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, GroupCallRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_groupcall_not_exist;
    }
    if (it->second.findMember(targetId) == NULL)
    {
        return error_code_groupcall_not_member;
    }
    removeAndMaybeEndLocked(it->second, targetId, outEnded, outRoom);
    return error_code_ok;
}

bool GroupCallManager::getRoom(const std::string& roomId, GroupCallRoom& out) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, GroupCallRoom>::const_iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return false;
    }
    out = it->second;
    return true;
}

bool GroupCallManager::findRoomByGroup(int64_t groupId, GroupCallRoom& out) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int64_t, std::string>::const_iterator g = m_groupRoom.find(groupId);
    if (g == m_groupRoom.end())
    {
        return false;
    }
    std::map<std::string, GroupCallRoom>::const_iterator it = m_rooms.find(g->second);
    if (it == m_rooms.end())
    {
        return false;
    }
    out = it->second;
    return true;
}

bool GroupCallManager::isUserInCall(int64_t userId, std::string* outRoomId) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int64_t, std::string>::const_iterator it = m_userRoom.find(userId);
    if (it == m_userRoom.end())
    {
        return false;
    }
    if (outRoomId != NULL)
    {
        *outRoomId = it->second;
    }
    return true;
}

bool GroupCallManager::signTokenFor(const std::string& roomId, int64_t userId, int64_t now, std::string& outToken,
                                    int& outSeat) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, GroupCallRoom>::const_iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return false;
    }
    const GroupCallMember* member = it->second.findMember(userId);
    if (member == NULL || member->m_state != groupcall_member_joined)
    {
        return false;
    }
    RelayTokenPayload payload;
    payload.sessionid = roomId;
    payload.role = RelayToken::role_groupcall_member;
    payload.uid = userId;
    //房间中转没有「对端」，peeruid 字段承载座位号（见 RelayToken.h 的角色说明）
    payload.peeruid = member->m_seat;
    payload.expire = now + kRelayTokenTtlMs;
    outToken = RelayToken::sign(payload, m_config.m_relayTokenKey);
    outSeat = member->m_seat;
    return true;
}

int64_t GroupCallManager::ringRemainingMs(const std::string& roomId, int64_t userId, int64_t now) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, GroupCallRoom>::const_iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return -1;
    }
    const GroupCallMember* member = it->second.findMember(userId);
    if (member == NULL || member->m_state != groupcall_member_ringing)
    {
        return -1;
    }
    const int64_t remaining = member->m_inviteTime + kRingTimeoutMs - now;
    return remaining > 0 ? remaining : 0;
}

bool GroupCallManager::setInitiatorName(const std::string& roomId, const std::string& name)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, GroupCallRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return false;
    }
    it->second.m_initiatorName = name;
    return true;
}

bool GroupCallManager::setCardMsgId(const std::string& roomId, int64_t cardMsgId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, GroupCallRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return false;
    }
    it->second.m_cardMsgId = cardMsgId;
    return true;
}

int GroupCallManager::onUserOffline(int64_t userId, int64_t now, GroupCallRoom& outRoom)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int64_t, std::string>::const_iterator where = m_userRoom.find(userId);
    if (where == m_userRoom.end())
    {
        return groupcall_offline_none;
    }
    std::map<std::string, GroupCallRoom>::iterator it = m_rooms.find(where->second);
    if (it == m_rooms.end())
    {
        return groupcall_offline_none;
    }
    GroupCallRoom& room = it->second;
    for (size_t i = 0; i < room.m_members.size(); ++i)
    {
        GroupCallMember& m = room.m_members[i];
        if (m.m_userId != userId)
        {
            continue;
        }
        //振铃中的人掉线：没有可以保留的东西，直接移除（振铃中的人不影响房间存续）
        if (m.m_state == groupcall_member_ringing)
        {
            removeMemberLocked(room, userId);
            outRoom = room;
            return groupcall_offline_ringing_removed;
        }
        //已加入的人掉线：保留座位，记下时刻；已记过的以第一次为准
        if (m.m_offlineSince == 0)
        {
            m.m_offlineSince = now;
        }
        outRoom = room;
        return groupcall_offline_joined_away;
    }
    return groupcall_offline_none;
}

bool GroupCallManager::onUserOnline(int64_t userId, GroupCallRoom& outRoom)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int64_t, std::string>::const_iterator where = m_userRoom.find(userId);
    if (where == m_userRoom.end())
    {
        return false;
    }
    std::map<std::string, GroupCallRoom>::iterator it = m_rooms.find(where->second);
    if (it == m_rooms.end())
    {
        return false;
    }
    for (size_t i = 0; i < it->second.m_members.size(); ++i)
    {
        GroupCallMember& m = it->second.m_members[i];
        if (m.m_userId == userId && m.m_state == groupcall_member_joined && m.m_offlineSince != 0)
        {
            m.m_offlineSince = 0;
            outRoom = it->second;
            return true;
        }
    }
    return false;
}

void GroupCallManager::reapRingTimeouts(int64_t now, std::vector<GroupCallReaped>& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    for (std::map<std::string, GroupCallRoom>::iterator it = m_rooms.begin(); it != m_rooms.end(); ++it)
    {
        //先收集再移除，避免一边遍历成员一边删
        std::vector<int64_t> expired;
        for (size_t i = 0; i < it->second.m_members.size(); ++i)
        {
            const GroupCallMember& m = it->second.m_members[i];
            if (m.m_state == groupcall_member_ringing && now - m.m_inviteTime >= kRingTimeoutMs)
            {
                expired.push_back(m.m_userId);
            }
        }
        for (size_t i = 0; i < expired.size(); ++i)
        {
            removeMemberLocked(it->second, expired[i]);
            GroupCallReaped r;
            r.m_room = it->second;
            r.m_userId = expired[i];
            r.m_ended = false;
            out.push_back(r);
        }
    }
}

void GroupCallManager::reapOfflineMembers(int64_t now, std::vector<GroupCallReaped>& out)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    //先收集（房间号、userid），再逐个处理：处理过程中房间可能结束、从表里删除
    std::vector<std::pair<std::string, int64_t> > expired;
    for (std::map<std::string, GroupCallRoom>::const_iterator it = m_rooms.begin(); it != m_rooms.end(); ++it)
    {
        for (size_t i = 0; i < it->second.m_members.size(); ++i)
        {
            const GroupCallMember& m = it->second.m_members[i];
            if (m.m_state == groupcall_member_joined && m.m_offlineSince != 0 &&
                now - m.m_offlineSince >= m_config.m_offlineGraceMs)
            {
                expired.push_back(std::make_pair(it->first, m.m_userId));
            }
        }
    }
    for (size_t i = 0; i < expired.size(); ++i)
    {
        std::map<std::string, GroupCallRoom>::iterator it = m_rooms.find(expired[i].first);
        if (it == m_rooms.end())
        {
            continue;
        }
        GroupCallReaped r;
        r.m_userId = expired[i].second;
        removeAndMaybeEndLocked(it->second, expired[i].second, r.m_ended, r.m_room);
        out.push_back(r);
    }
}

size_t GroupCallManager::roomCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_rooms.size();
}

int GroupCallManager::lowestFreeSeat(const GroupCallRoom& room)
{
    std::set<int> used;
    for (size_t i = 0; i < room.m_members.size(); ++i)
    {
        if (room.m_members[i].m_state == groupcall_member_joined)
        {
            used.insert(room.m_members[i].m_seat);
        }
    }
    for (int seat = kFirstSeat; seat <= kMaxSeat; ++seat)
    {
        if (used.find(seat) == used.end())
        {
            return seat;
        }
    }
    return -1;
}

void GroupCallManager::sortMembers(GroupCallRoom& room)
{
    std::stable_sort(room.m_members.begin(), room.m_members.end(), memberOrderLess);
}

void GroupCallManager::removeMemberLocked(GroupCallRoom& room, int64_t userId)
{
    for (std::vector<GroupCallMember>::iterator it = room.m_members.begin(); it != room.m_members.end(); ++it)
    {
        if (it->m_userId == userId)
        {
            room.m_members.erase(it);
            break;
        }
    }
    std::map<int64_t, std::string>::iterator where = m_userRoom.find(userId);
    if (where != m_userRoom.end() && where->second == room.m_roomId)
    {
        m_userRoom.erase(where);
    }
}

void GroupCallManager::dissolveLocked(const std::string& roomId)
{
    std::map<std::string, GroupCallRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return;
    }
    for (size_t i = 0; i < it->second.m_members.size(); ++i)
    {
        std::map<int64_t, std::string>::iterator where = m_userRoom.find(it->second.m_members[i].m_userId);
        if (where != m_userRoom.end() && where->second == roomId)
        {
            m_userRoom.erase(where);
        }
    }
    std::map<int64_t, std::string>::iterator g = m_groupRoom.find(it->second.m_groupId);
    if (g != m_groupRoom.end() && g->second == roomId)
    {
        m_groupRoom.erase(g);
    }
    m_rooms.erase(it);
}

void GroupCallManager::removeAndMaybeEndLocked(GroupCallRoom& room, int64_t userId, bool& outEnded,
                                               GroupCallRoom& outRoom)
{
    removeMemberLocked(room, userId);
    outRoom = room;
    outEnded = (room.joinedCount() == 0);
    //最后一个已加入的人走了：房间结束（outRoom 已是结束之前的快照，仍含振铃中的人）。
    //房间号先拷贝出来：dissolveLocked 会删除 room 本身，不能把它内部字段的引用传进去
    if (outEnded)
    {
        const std::string roomId = room.m_roomId;
        dissolveLocked(roomId);
    }
}
