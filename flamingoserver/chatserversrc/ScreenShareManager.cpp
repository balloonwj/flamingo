/**
 *  屏幕共享房间管理器实现，ScreenShareManager.cpp
 *  balloonwj@qq.com   2026-10-02
 */
#include "ScreenShareManager.h"

#include <algorithm>
#include <sstream>
#include "Protocol.h"                // conversation_type、error_code_screenshare_*
#include "../utils/RelayToken.h"

const int ScreenShareManager::kDefaultMaxMembers;
const int ScreenShareManager::kDefaultMaxCameras;
const int64_t ScreenShareManager::kDefaultHostGraceMs;
const int ScreenShareManager::kMaxSeat;
const int64_t ScreenShareManager::kRelayTokenTtlMs;
const int ScreenShareManager::kDefaultMaxSharers;

namespace {

//房间号前缀：与通话（纯数字与连字符）、文件共享（share- 前缀）的中转 sessionid 区分开
const char* kRoomIdPrefix = "ss-";

//房间人数上限的下限：至少要容得下发起人与一名观看者
const int kMinMembers = 2;

//发起人的座位号：只有建房间的人坐这个座位，他的推流管线就是这条中转连接（第 22 期据此区分主持人推屏幕走哪条连接）
const int kCreatorSeat = 0;

//控制键鼠的五种动作，取值与 ScreenShareProtocol.h 的 enum screenshare_control_action 一致（本文件不包含协议头，
//以免管理器依赖协议编解码；两边一致由 SSCT1～SSCT4 用例守卫）
const int kControlRequest = 1;   //观看者申请
const int kControlCancel  = 2;   //申请人撤回申请
const int kControlGrant   = 3;   //画面主人授予
const int kControlDeny    = 4;   //画面主人拒绝
const int kControlRevoke  = 5;   //画面主人收回或控制者结束
const int kControlActionFirst = kControlRequest;   //动作取值的下限
const int kControlActionLast  = kControlRevoke;    //动作取值的上限
//多人共享的动作（第 22 期，取值与 ScreenShareProtocol.h 的 enum screenshare_sharing_action 一致）
const int kSharingRequest = 1;   //申请共享自己的屏幕
const int kSharingCancel  = 2;   //撤回申请
const int kSharingApprove = 3;   //主持人同意
const int kSharingReject  = 4;   //主持人拒绝
const int kSharingStop    = 5;   //停止共享
const int kSharingActionFirst = kSharingRequest;   //动作取值的下限
const int kSharingActionLast  = kSharingStop;      //动作取值的上限

} // namespace

ScreenShareConfig::ScreenShareConfig()
    : m_relayPort(0)
    , m_maxMembers(ScreenShareManager::kDefaultMaxMembers)
    , m_maxCameras(ScreenShareManager::kDefaultMaxCameras)
    , m_maxSharers(ScreenShareManager::kDefaultMaxSharers)
    , m_hostGraceMs(ScreenShareManager::kDefaultHostGraceMs)
{
}

ScreenShareMember::ScreenShareMember()
    : m_userId(0)
    , m_seat(0)
    , m_mic(false)
    , m_camera(false)
    , m_systemAudio(false)
    , m_recording(false)
    , m_joinTime(0)
    , m_controlledBy(0)
    , m_sharing(false)
    , m_screenSeat(-1)
    , m_shareRequested(false)
    , m_boardRequested(false)
    , m_viaLink(false)
{
}

ScreenShareWhiteboard::ScreenShareWhiteboard()
    : m_seat(-1)
    , m_ownerId(0)
{
}

ScreenShareSharingResult::ScreenShareSharingResult()
    : m_notifyUser(0)
    , m_changed(false)
    , m_publishUser(0)
    , m_subject(0)
    , m_boardSeat(-1)
{
}

ScreenShareControlResult::ScreenShareControlResult()
    : m_notifyUser(0)
    , m_changed(false)
    , m_prevController(0)
{
}

ScreenShareRoom::ScreenShareRoom()
    : m_ctype(0)
    , m_convKeyA(0)
    , m_convKeyB(0)
    , m_hostId(0)
    , m_starterId(0)
    , m_startTime(0)
    , m_annotateAllowed(true)
    , m_recordAllowed(true)
    , m_hostOfflineSince(0)
    , m_forced(false)
    , m_cardMsgId(0)
    , m_maxSharers(ScreenShareManager::kDefaultMaxSharers)
    , m_cloudRecordId(0)
    , m_cloudRecordOwner(0)
    , m_cloudRecordPaused(false)
    , m_cloudRecordSince(0)
    , m_cloudRecordEndCode(0)
{
}

bool ScreenShareRoom::isForcedAway(int64_t userId) const
{
    return std::find(m_forcedAway.begin(), m_forcedAway.end(), userId) != m_forcedAway.end();
}

bool ScreenShareRoom::isBanned(int64_t userId) const
{
    return std::find(m_banned.begin(), m_banned.end(), userId) != m_banned.end();
}

const ScreenShareMember* ScreenShareRoom::findMember(int64_t userId) const
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

ScreenShareManager::ScreenShareManager()
    : m_roomSeq(0)
{
}

ScreenShareManager::~ScreenShareManager()
{
}

void ScreenShareManager::configure(const ScreenShareConfig& config)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_config = config;
    //人数上限：座位号只有 0~kMaxSeat，越界按默认值
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
    if (m_config.m_hostGraceMs <= 0)
    {
        m_config.m_hostGraceMs = kDefaultHostGraceMs;
    }
    //共享人数上限（第 22 期）：1 ~ 人数上限，越界按默认值（默认值本身也不超过人数上限）
    if (m_config.m_maxSharers < 1 || m_config.m_maxSharers > m_config.m_maxMembers)
    {
        m_config.m_maxSharers = (std::min)(kDefaultMaxSharers, m_config.m_maxMembers);
    }
}

ScreenShareConfig ScreenShareManager::getConfig() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_config;
}

int ScreenShareManager::startShare(int64_t hostId, int ctype, int64_t cid, bool mic, bool camera, bool sysAudio,
                                   int64_t now, std::string& outRoomId, const std::string& password, bool forced,
                                   bool recordAllowed)
{
    //参数校验：会话类型只支持单聊与群聊，单聊不能对自己发起
    if (hostId <= 0 || cid <= 0 || (ctype != conversation_single && ctype != conversation_group))
    {
        return error_code_bad_request;
    }
    if (ctype == conversation_single && cid == hostId)
    {
        return error_code_bad_request;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    int64_t keyA = 0;
    int64_t keyB = 0;
    makeConvKey(ctype, hostId, cid, keyA, keyB);
    //先查会话：同一会话同时只能一个共享（发起人在同一会话里再发起也报这一条，提示更贴切）
    for (std::map<std::string, ScreenShareRoom>::const_iterator it = m_rooms.begin(); it != m_rooms.end(); ++it)
    {
        if (it->second.m_ctype == ctype && it->second.m_convKeyA == keyA && it->second.m_convKeyB == keyB)
        {
            return error_code_screenshare_exists;
        }
    }
    //再查人：一个人同时只能在一个房间里
    if (m_userRoom.find(hostId) != m_userRoom.end())
    {
        return error_code_screenshare_in_other_room;
    }

    std::ostringstream oss;
    oss << kRoomIdPrefix << hostId << "-" << now << "-" << (++m_roomSeq);
    ScreenShareRoom room;
    room.m_roomId = oss.str();
    room.m_ctype = ctype;
    room.m_convKeyA = keyA;
    room.m_convKeyB = keyB;
    room.m_hostId = hostId;
    //最初的发起人：之后移交主持人也不变，会话里的共享卡片按它写发起人
    room.m_starterId = hostId;
    room.m_startTime = now;
    room.m_password = password;
    room.m_forced = forced;
    room.m_recordAllowed = recordAllowed;
    room.m_maxSharers = m_config.m_maxSharers;

    ScreenShareMember host;
    host.m_userId = hostId;
    host.m_seat = kCreatorSeat;
    host.m_mic = mic;
    host.m_camera = camera;
    host.m_systemAudio = sysAudio;
    host.m_joinTime = now;
    //主持人发起即在共享，用自己的座位推流（第 22 期）
    host.m_sharing = true;
    host.m_screenSeat = host.m_seat;
    room.m_members.push_back(host);

    m_rooms[room.m_roomId] = room;
    m_userRoom[hostId] = room.m_roomId;
    outRoomId = room.m_roomId;
    return error_code_ok;
}

int ScreenShareManager::joinShare(const std::string& roomId, int64_t userId, int64_t now, int& outSeat, bool viaLink)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_screenshare_not_exist;
    }
    ScreenShareRoom& room = it->second;
    //重复加入是幂等的：座位号不变
    const ScreenShareMember* existing = room.findMember(userId);
    if (existing != NULL)
    {
        outSeat = existing->m_seat;
        return error_code_ok;
    }
    //被发起人移出且不允许再次加入（第 14 期）
    if (room.isBanned(userId))
    {
        return error_code_screenshare_removed;
    }
    if (m_userRoom.find(userId) != m_userRoom.end())
    {
        return error_code_screenshare_in_other_room;
    }
    if (static_cast<int>(room.m_members.size()) >= m_config.m_maxMembers)
    {
        return error_code_screenshare_room_full;
    }
    int seat = lowestFreeSeat(room);
    if (seat < 0)
    {
        return error_code_screenshare_room_full;
    }

    ScreenShareMember member;
    member.m_userId = userId;
    member.m_seat = seat;
    member.m_joinTime = now;
    member.m_viaLink = viaLink;
    //按座位号升序插入，推送给客户端的成员列表顺序稳定
    std::vector<ScreenShareMember>::iterator pos = room.m_members.begin();
    while (pos != room.m_members.end() && pos->m_seat < seat)
    {
        ++pos;
    }
    room.m_members.insert(pos, member);
    m_userRoom[userId] = roomId;
    //强制模式下掉线的观看者回来了：不再需要让他回到观看
    room.m_forcedAway.erase(std::remove(room.m_forcedAway.begin(), room.m_forcedAway.end(), userId),
                            room.m_forcedAway.end());
    outSeat = seat;
    return error_code_ok;
}

int ScreenShareManager::leaveShare(const std::string& roomId, int64_t userId, bool& outDissolved,
                                   ScreenShareRoom& outRoom)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_screenshare_not_exist;
    }
    if (it->second.findMember(userId) == NULL)
    {
        return error_code_screenshare_not_member;
    }
    outRoom = it->second;
    //发起人离开等同结束共享
    if (userId == it->second.m_hostId)
    {
        ScreenShareRoom dissolved;
        dissolveLocked(roomId, dissolved);
        outDissolved = true;
        return error_code_ok;
    }
    //强制模式：观看者要等发起人结束才能离开
    if (it->second.m_forced)
    {
        return error_code_screenshare_forced_leave;
    }
    removeMemberLocked(it->second, userId);
    outDissolved = false;
    return error_code_ok;
}

int ScreenShareManager::stopShare(const std::string& roomId, int64_t requesterId, ScreenShareRoom& outRoom)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_screenshare_not_exist;
    }
    if (it->second.m_hostId != requesterId)
    {
        return error_code_screenshare_not_host;
    }
    dissolveLocked(roomId, outRoom);
    return error_code_ok;
}

bool ScreenShareManager::getRoom(const std::string& roomId, ScreenShareRoom& out) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::const_iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return false;
    }
    out = it->second;
    return true;
}

bool ScreenShareManager::findRoomByConversation(int ctype, int64_t selfId, int64_t cid, ScreenShareRoom& out) const
{
    int64_t keyA = 0;
    int64_t keyB = 0;
    makeConvKey(ctype, selfId, cid, keyA, keyB);
    std::lock_guard<std::mutex> guard(m_mutex);
    for (std::map<std::string, ScreenShareRoom>::const_iterator it = m_rooms.begin(); it != m_rooms.end(); ++it)
    {
        if (it->second.m_ctype == ctype && it->second.m_convKeyA == keyA && it->second.m_convKeyB == keyB)
        {
            out = it->second;
            return true;
        }
    }
    return false;
}

int ScreenShareManager::setMemberMedia(const std::string& roomId, int64_t userId, bool mic, bool camera, bool sysAudio)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_screenshare_not_exist;
    }
    ScreenShareRoom& room = it->second;
    ScreenShareMember* member = NULL;
    int camerasOn = 0;
    for (size_t i = 0; i < room.m_members.size(); ++i)
    {
        if (room.m_members[i].m_userId == userId)
        {
            member = &room.m_members[i];
        }
        if (room.m_members[i].m_camera)
        {
            ++camerasOn;
        }
    }
    if (member == NULL)
    {
        return error_code_screenshare_not_member;
    }
    //新开摄像头才受上限约束；本来就开着的保持开着不算新增
    if (camera && !member->m_camera && camerasOn >= m_config.m_maxCameras)
    {
        return error_code_screenshare_camera_limit;
    }
    member->m_mic = mic;
    member->m_camera = camera;
    member->m_systemAudio = sysAudio;
    return error_code_ok;
}

int ScreenShareManager::setPermission(const std::string& roomId, int64_t requesterId, int annotate, int record,
                                      std::vector<int64_t>& outStoppedRecords)
{
    outStoppedRecords.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_screenshare_not_exist;
    }
    ScreenShareRoom& room = it->second;
    if (room.m_hostId != requesterId)
    {
        return error_code_screenshare_not_host;
    }
    if (annotate >= 0)
    {
        room.m_annotateAllowed = (annotate != 0);
    }
    if (record >= 0)
    {
        room.m_recordAllowed = (record != 0);
        //收回录制权限：正在录制的观看者一律停止（2026-10-02 与用户确认），发起人自己不受影响
        if (!room.m_recordAllowed)
        {
            for (size_t i = 0; i < room.m_members.size(); ++i)
            {
                ScreenShareMember& m = room.m_members[i];
                if (m.m_userId != room.m_hostId && m.m_recording)
                {
                    m.m_recording = false;
                    outStoppedRecords.push_back(m.m_userId);
                }
            }
        }
    }
    return error_code_ok;
}

int ScreenShareManager::muteMember(const std::string& roomId, int64_t requesterId, int64_t targetId, bool& outChanged)
{
    outChanged = false;
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_screenshare_not_exist;
    }
    ScreenShareRoom& room = it->second;
    if (room.m_hostId != requesterId)
    {
        return error_code_screenshare_not_host;
    }
    if (targetId == room.m_hostId)
    {
        return error_code_bad_request;
    }
    for (size_t i = 0; i < room.m_members.size(); ++i)
    {
        ScreenShareMember& m = room.m_members[i];
        if (m.m_userId != targetId)
        {
            continue;
        }
        if (m.m_mic)
        {
            m.m_mic = false;
            outChanged = true;
        }
        return error_code_ok;
    }
    return error_code_screenshare_not_member;
}

int ScreenShareManager::removeMember(const std::string& roomId, int64_t requesterId, int64_t targetId, bool allowRejoin)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_screenshare_not_exist;
    }
    ScreenShareRoom& room = it->second;
    if (room.m_hostId != requesterId)
    {
        return error_code_screenshare_not_host;
    }
    if (targetId == room.m_hostId)
    {
        return error_code_bad_request;
    }
    //发起人点「移出」时对方可能恰好掉线：强制模式下此人已移出房间、记在待回到观看的名单里，同样按移出处理
    const bool isMember = (room.findMember(targetId) != NULL);
    if (!isMember && !room.isForcedAway(targetId))
    {
        return error_code_screenshare_not_member;
    }
    //强制模式不限制发起人移出（718 只限制观看者自己离开）
    if (isMember)
    {
        removeMemberLocked(room, targetId);
    }
    //被移出的人重新登录后不再被拉回观看
    room.m_forcedAway.erase(std::remove(room.m_forcedAway.begin(), room.m_forcedAway.end(), targetId),
                            room.m_forcedAway.end());
    if (!allowRejoin && !room.isBanned(targetId))
    {
        room.m_banned.push_back(targetId);
    }
    return error_code_ok;
}

int ScreenShareManager::muteAll(const std::string& roomId, int64_t requesterId, std::vector<int64_t>& outMuted)
{
    outMuted.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_screenshare_not_exist;
    }
    ScreenShareRoom& room = it->second;
    if (room.m_hostId != requesterId)
    {
        return error_code_screenshare_not_host;
    }
    for (size_t i = 0; i < room.m_members.size(); ++i)
    {
        ScreenShareMember& m = room.m_members[i];
        if (m.m_userId != room.m_hostId && m.m_mic)
        {
            m.m_mic = false;
            outMuted.push_back(m.m_userId);
        }
    }
    return error_code_ok;
}

int ScreenShareManager::setRecording(const std::string& roomId, int64_t userId, bool recording)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_screenshare_not_exist;
    }
    ScreenShareRoom& room = it->second;
    ScreenShareMember* member = NULL;
    for (size_t i = 0; i < room.m_members.size(); ++i)
    {
        if (room.m_members[i].m_userId == userId)
        {
            member = &room.m_members[i];
        }
    }
    if (member == NULL)
    {
        return error_code_screenshare_not_member;
    }
    //观看者开始录制须有录制权限；发起人随时可录，停止录制总是允许
    if (recording && userId != room.m_hostId && !room.m_recordAllowed)
    {
        return error_code_screenshare_no_record_perm;
    }
    member->m_recording = recording;
    return error_code_ok;
}

int ScreenShareManager::applyControl(const std::string& roomId, int64_t actorId, int action, int64_t ownerId,
                                     int64_t targetId, ScreenShareControlResult& out)
{
    out = ScreenShareControlResult();
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_screenshare_not_exist;
    }
    ScreenShareRoom& room = it->second;
    //动作取值：enum screenshare_control_action 的 1～5（ScreenShareProtocol.h；本文件不依赖它，按取值区间判断）
    if (action < kControlActionFirst || action > kControlActionLast)
    {
        return error_code_bad_request;
    }
    if (room.findMember(actorId) == NULL)
    {
        return error_code_screenshare_not_member;
    }
    ScreenShareMember* owner = findScreenOwnerLocked(room, ownerId);
    if (owner == NULL)
    {
        return error_code_screenshare_not_sharing;
    }
    out.m_prevController = owner->m_controlledBy;

    switch (action)
    {
    //申请、撤回：观看者发起，转给画面主人；已在控制时再申请不必打扰画面主人
    case kControlRequest:
    case kControlCancel:
        if (actorId == ownerId)
        {
            return error_code_bad_request;
        }
        if (!(action == kControlRequest && owner->m_controlledBy == actorId))
        {
            out.m_notifyUser = ownerId;
        }
        break;

    //授予、拒绝：只有画面主人能做，对象须是房间里的其他人
    case kControlGrant:
    case kControlDeny:
        if (actorId != ownerId)
        {
            return error_code_screenshare_not_owner;
        }
        if (room.findMember(targetId) == NULL)
        {
            return error_code_screenshare_not_member;
        }
        if (targetId == ownerId)
        {
            return error_code_bad_request;
        }
        if (action == kControlDeny)
        {
            out.m_notifyUser = targetId;
            break;
        }
        if (owner->m_controlledBy != targetId)
        {
            //一个人同一时间只控制一块画面：先从别的画面上撤下（多人共享时才会出现）
            for (size_t i = 0; i < room.m_members.size(); ++i)
            {
                if (room.m_members[i].m_controlledBy == targetId)
                {
                    room.m_members[i].m_controlledBy = 0;
                }
            }
            owner->m_controlledBy = targetId;
            out.m_changed = true;
        }
        break;

    //收回：画面主人或正在控制的人；没人控制时什么也不改
    case kControlRevoke:
        if (actorId != ownerId && (owner->m_controlledBy == 0 || actorId != owner->m_controlledBy))
        {
            return error_code_screenshare_not_owner;
        }
        if (owner->m_controlledBy != 0)
        {
            owner->m_controlledBy = 0;
            out.m_changed = true;
        }
        break;

    //取值区间已在上面检查过，走不到这里
    default:
        return error_code_bad_request;
    }
    out.m_room = room;
    return error_code_ok;
}

bool ScreenShareManager::signTokenFor(const std::string& roomId, int64_t userId, int64_t now, std::string& outToken,
                                      int& outSeat) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::const_iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return false;
    }
    const ScreenShareMember* member = it->second.findMember(userId);
    if (member == NULL)
    {
        return false;
    }
    RelayTokenPayload payload;
    payload.sessionid = roomId;
    payload.role = (userId == it->second.m_hostId) ? RelayToken::role_screenshare_host
                                                   : RelayToken::role_screenshare_member;
    payload.uid = userId;
    //房间中转没有「对端」，peeruid 字段承载座位号（见 RelayToken.h 的角色说明）
    payload.peeruid = member->m_seat;
    payload.expire = now + kRelayTokenTtlMs;
    outToken = RelayToken::sign(payload, m_config.m_relayTokenKey);
    outSeat = member->m_seat;
    return true;
}

int ScreenShareManager::applySharing(const std::string& roomId, int64_t actorId, int action, int64_t targetId,
                                     ScreenShareSharingResult& out)
{
    out = ScreenShareSharingResult();
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_screenshare_not_exist;
    }
    ScreenShareRoom& room = it->second;
    if (action < kSharingActionFirst || action > kSharingActionLast)
    {
        return error_code_bad_request;
    }
    ScreenShareMember* actor = findMemberLocked(room, actorId);
    if (actor == NULL)
    {
        return error_code_screenshare_not_member;
    }
    const bool actorIsHost = (actorId == room.m_hostId);

    switch (action)
    {
    //申请：正在共享的人是空操作；主持人停掉自己的画面后再申请直接恢复；其他人记下申请、转给主持人
    case kSharingRequest:
        out.m_subject = actorId;
        if (actor->m_sharing)
        {
            break;
        }
        if (sharingCount(room) >= m_config.m_maxSharers)
        {
            return error_code_screenshare_sharers_full;
        }
        //建房间的主持人：推流管线就是他自己的中转连接，直接恢复、用自己的座位
        if (actorIsHost && actor->m_seat == kCreatorSeat)
        {
            actor->m_sharing = true;
            actor->m_screenSeat = actor->m_seat;
            out.m_changed = true;
            break;
        }
        //经移交成为主持人的人：他自己的连接只是观看连接，与其他共享者一样另分配画面座位、凭「只推流」令牌另连一条
        //中转连接推屏幕；不用等谁同意
        if (actorIsHost)
        {
            const int screenSeat = lowestFreeSeat(room);
            if (screenSeat < 0)
            {
                return error_code_screenshare_room_full;
            }
            actor->m_shareRequested = false;
            actor->m_sharing = true;
            actor->m_screenSeat = screenSeat;
            out.m_changed = true;
            out.m_publishUser = actorId;
            break;
        }
        actor->m_shareRequested = true;
        out.m_notifyUser = room.m_hostId;
        break;

    //撤回：有申请才转给主持人
    case kSharingCancel:
        out.m_subject = actorId;
        if (actor->m_shareRequested)
        {
            actor->m_shareRequested = false;
            out.m_notifyUser = room.m_hostId;
        }
        break;

    //同意、拒绝：只有主持人能做，对方须有待处理的申请
    case kSharingApprove:
    case kSharingReject:
    {
        if (!actorIsHost)
        {
            return error_code_screenshare_not_host;
        }
        ScreenShareMember* target = findMemberLocked(room, targetId);
        if (target == NULL)
        {
            return error_code_screenshare_not_member;
        }
        if (!target->m_shareRequested)
        {
            return error_code_screenshare_no_request;
        }
        out.m_subject = targetId;
        out.m_notifyUser = targetId;
        if (action == kSharingReject)
        {
            target->m_shareRequested = false;
            break;
        }
        //同意：人数已满时申请保留，等有人停下后再批
        if (sharingCount(room) >= m_config.m_maxSharers)
        {
            return error_code_screenshare_sharers_full;
        }
        const int screenSeat = lowestFreeSeat(room);
        if (screenSeat < 0)
        {
            return error_code_screenshare_room_full;
        }
        target->m_shareRequested = false;
        target->m_sharing = true;
        target->m_screenSeat = screenSeat;
        out.m_changed = true;
        out.m_publishUser = targetId;
        break;
    }

    //停止：共享者停自己的，主持人可停任何人的；停止后这块画面的控制随之结束
    case kSharingStop:
    {
        const int64_t subjectId = (targetId > 0) ? targetId : actorId;
        if (subjectId != actorId && !actorIsHost)
        {
            return error_code_screenshare_not_host;
        }
        ScreenShareMember* subject = findMemberLocked(room, subjectId);
        if (subject == NULL)
        {
            return error_code_screenshare_not_member;
        }
        if (!subject->m_sharing)
        {
            return error_code_screenshare_not_sharing;
        }
        subject->m_sharing = false;
        subject->m_screenSeat = -1;
        subject->m_controlledBy = 0;
        out.m_subject = subjectId;
        out.m_changed = true;
        if (subjectId != actorId)
        {
            out.m_notifyUser = subjectId;
        }
        break;
    }

    //取值区间已在上面检查过，走不到这里
    default:
        return error_code_bad_request;
    }
    out.m_room = room;
    return error_code_ok;
}

int ScreenShareManager::applyWhiteboard(const std::string& roomId, int64_t actorId, int action, int64_t targetId,
                                        ScreenShareSharingResult& out)
{
    out = ScreenShareSharingResult();
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_screenshare_not_exist;
    }
    ScreenShareRoom& room = it->second;
    if (action < kSharingActionFirst || action > kSharingActionLast)
    {
        return error_code_bad_request;
    }
    ScreenShareMember* actor = findMemberLocked(room, actorId);
    if (actor == NULL)
    {
        return error_code_screenshare_not_member;
    }
    const bool actorIsHost = (actorId == room.m_hostId);

    switch (action)
    {
    //申请新建白板：已有白板的人是空操作；名额已满返回 731；主持人直接新建，其他人记下申请、转给主持人
    case kSharingRequest:
        out.m_subject = actorId;
        if (findBoardIndex(room, actorId) >= 0)
        {
            break;
        }
        if (sharingCount(room) >= m_config.m_maxSharers)
        {
            return error_code_screenshare_sharers_full;
        }
        if (actorIsHost)
        {
            const int seat = lowestFreeSeat(room);
            if (seat < 0)
            {
                return error_code_screenshare_room_full;
            }
            ScreenShareWhiteboard board;
            board.m_seat = seat;
            board.m_ownerId = actorId;
            room.m_whiteboards.push_back(board);
            out.m_changed = true;
            out.m_boardSeat = seat;
            out.m_notifyUser = actorId;
            break;
        }
        actor->m_boardRequested = true;
        out.m_notifyUser = room.m_hostId;
        break;

    //撤回：有申请才转给主持人
    case kSharingCancel:
        out.m_subject = actorId;
        if (actor->m_boardRequested)
        {
            actor->m_boardRequested = false;
            out.m_notifyUser = room.m_hostId;
        }
        break;

    //同意、拒绝：只有主持人能做，对方须有待处理的申请
    case kSharingApprove:
    case kSharingReject:
    {
        if (!actorIsHost)
        {
            return error_code_screenshare_not_host;
        }
        ScreenShareMember* target = findMemberLocked(room, targetId);
        if (target == NULL)
        {
            return error_code_screenshare_not_member;
        }
        if (!target->m_boardRequested)
        {
            return error_code_screenshare_no_request;
        }
        out.m_subject = targetId;
        out.m_notifyUser = targetId;
        if (action == kSharingReject)
        {
            target->m_boardRequested = false;
            break;
        }
        //同意：名额已满时申请保留，等有人停下后再批
        if (sharingCount(room) >= m_config.m_maxSharers)
        {
            return error_code_screenshare_sharers_full;
        }
        const int seat = lowestFreeSeat(room);
        if (seat < 0)
        {
            return error_code_screenshare_room_full;
        }
        target->m_boardRequested = false;
        ScreenShareWhiteboard board;
        board.m_seat = seat;
        board.m_ownerId = targetId;
        room.m_whiteboards.push_back(board);
        out.m_changed = true;
        out.m_boardSeat = seat;
        break;
    }

    //关闭：建立者关自己的，主持人可关任何人的
    case kSharingStop:
    {
        const int64_t ownerId = (targetId > 0) ? targetId : actorId;
        if (ownerId != actorId && !actorIsHost)
        {
            return error_code_screenshare_not_host;
        }
        const int index = findBoardIndex(room, ownerId);
        if (index < 0)
        {
            return error_code_screenshare_not_sharing;
        }
        room.m_whiteboards.erase(room.m_whiteboards.begin() + index);
        out.m_subject = ownerId;
        out.m_changed = true;
        if (ownerId != actorId)
        {
            out.m_notifyUser = ownerId;
        }
        break;
    }

    //取值区间已在上面检查过，走不到这里
    default:
        return error_code_bad_request;
    }
    out.m_room = room;
    return error_code_ok;
}

int ScreenShareManager::findBoardIndex(const ScreenShareRoom& room, int64_t ownerId)
{
    for (size_t i = 0; i < room.m_whiteboards.size(); ++i)
    {
        if (room.m_whiteboards[i].m_ownerId == ownerId)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int ScreenShareManager::transferHost(const std::string& roomId, int64_t actorId, int64_t targetId,
                                     const std::string& targetName, ScreenShareRoom& outRoom)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_screenshare_not_exist;
    }
    ScreenShareRoom& room = it->second;
    if (actorId != room.m_hostId)
    {
        return error_code_screenshare_not_host;
    }
    if (targetId == actorId)
    {
        return error_code_bad_request;
    }
    const ScreenShareMember* target = findMemberLocked(room, targetId);
    if (target == NULL)
    {
        return error_code_screenshare_not_member;
    }
    if (!target->m_sharing)
    {
        return error_code_screenshare_not_sharing;
    }
    //换主持人，原主持人离开房间（他的画面、他正在控制的画面随之结束）；主持人昵称一并换掉，
    //否则凭链接查到的概要、强制模式的 rejoin 推送会是新主持人的 id 配原主持人的名字。最初的发起人不变
    room.m_hostId = targetId;
    room.m_hostName = targetName;
    room.m_hostOfflineSince = 0;
    removeMemberLocked(room, actorId);
    outRoom = room;
    return error_code_ok;
}

bool ScreenShareManager::signPublishToken(const std::string& roomId, int64_t userId, int64_t now, std::string& outToken,
                                          int& outScreenSeat) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::const_iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return false;
    }
    const ScreenShareMember* member = it->second.findMember(userId);
    //只给用另一条连接推流的共享者签：画面座位与成员座位不同（主持人用自己的座位、自己的连接）
    if (member == NULL || !member->m_sharing || member->m_screenSeat < 0 || member->m_screenSeat == member->m_seat)
    {
        return false;
    }
    RelayTokenPayload payload;
    payload.sessionid = roomId;
    payload.role = RelayToken::role_screenshare_publisher;
    payload.uid = userId;
    payload.peeruid = member->m_screenSeat;
    payload.expire = now + kRelayTokenTtlMs;
    outToken = RelayToken::sign(payload, m_config.m_relayTokenKey);
    outScreenSeat = member->m_screenSeat;
    return true;
}

int ScreenShareManager::maxSharers() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_config.m_maxSharers;
}

int ScreenShareManager::sharingCount(const ScreenShareRoom& room)
{
    int n = 0;
    for (size_t i = 0; i < room.m_members.size(); ++i)
    {
        if (room.m_members[i].m_sharing)
        {
            ++n;
        }
    }
    //白板（第 23 期）也占共享名额
    n += static_cast<int>(room.m_whiteboards.size());
    return n;
}

ScreenShareMember* ScreenShareManager::findMemberLocked(ScreenShareRoom& room, int64_t userId)
{
    for (size_t i = 0; i < room.m_members.size(); ++i)
    {
        if (room.m_members[i].m_userId == userId)
        {
            return &room.m_members[i];
        }
    }
    return NULL;
}

bool ScreenShareManager::isUserInShare(int64_t userId, std::string* outRoomId) const
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

int ScreenShareManager::onUserOffline(int64_t userId, int64_t now, ScreenShareRoom& outRoom)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int64_t, std::string>::const_iterator where = m_userRoom.find(userId);
    if (where == m_userRoom.end())
    {
        return screenshare_offline_none;
    }
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(where->second);
    if (it == m_rooms.end())
    {
        return screenshare_offline_none;
    }
    ScreenShareRoom& room = it->second;
    if (room.m_hostId == userId)
    {
        //发起人掉线：房间保留，记下掉线时刻（重复上报时保留最早的时刻，宽限期不被顺延）
        if (room.m_hostOfflineSince == 0)
        {
            room.m_hostOfflineSince = now;
        }
        //画面主人掉线：控制者的输入已无处注入，解除控制；回来后要重新授予（第 18 期）
        for (size_t i = 0; i < room.m_members.size(); ++i)
        {
            if (room.m_members[i].m_userId == userId)
            {
                room.m_members[i].m_controlledBy = 0;
            }
        }
        outRoom = room;
        return screenshare_offline_host_away;
    }
    removeMemberLocked(room, userId);
    //强制模式：记下这个观看者，重新登录时让他回到观看
    if (room.m_forced && !room.isForcedAway(userId))
    {
        room.m_forcedAway.push_back(userId);
    }
    outRoom = room;
    return screenshare_offline_member_left;
}

bool ScreenShareManager::onUserOnline(int64_t userId, ScreenShareRoom& outRoom)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<int64_t, std::string>::const_iterator where = m_userRoom.find(userId);
    if (where == m_userRoom.end())
    {
        return false;
    }
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(where->second);
    if (it == m_rooms.end() || it->second.m_hostId != userId || it->second.m_hostOfflineSince == 0)
    {
        return false;
    }
    it->second.m_hostOfflineSince = 0;
    outRoom = it->second;
    return true;
}

bool ScreenShareManager::setHostName(const std::string& roomId, const std::string& hostName)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return false;
    }
    it->second.m_hostName = hostName;
    //最初的发起人昵称只在第一次写入：发起成功后服务层调用的正是这一次
    if (it->second.m_starterName.empty())
        it->second.m_starterName = hostName;
    return true;
}

int ScreenShareManager::setInfo(const std::string& roomId, int64_t requesterId, const std::string& title,
                                const std::string& desc)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return error_code_screenshare_not_exist;
    }
    if (it->second.m_hostId != requesterId)
    {
        return error_code_screenshare_not_host;
    }
    it->second.m_title = title;
    it->second.m_desc = desc;
    return error_code_ok;
}

int ScreenShareManager::beginCloudRecord(const std::string& roomId, int64_t requesterId, int64_t recordId, int64_t nowMs,
                                         ScreenShareRoom& outRoom)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
        return error_code_screenshare_not_exist;
    ScreenShareRoom& room = it->second;
    if (room.m_hostId != requesterId)
        return error_code_screenshare_not_host;
    if (room.m_cloudRecordId != 0)
        return error_code_cloudrecord_busy;
    room.m_cloudRecordId = recordId;
    room.m_cloudRecordOwner = requesterId;
    room.m_cloudRecordPaused = false;
    room.m_cloudRecordSince = nowMs;
    room.m_cloudRecordEndCode = 0;
    outRoom = room;
    return error_code_ok;
}

int ScreenShareManager::setCloudRecordPaused(const std::string& roomId, int64_t requesterId, bool paused,
                                             int64_t& recordId, ScreenShareRoom& outRoom)
{
    recordId = 0;
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
        return error_code_screenshare_not_exist;
    ScreenShareRoom& room = it->second;
    if (room.m_hostId != requesterId)
        return error_code_screenshare_not_host;
    if (room.m_cloudRecordId == 0)
        return error_code_cloudrecord_busy;
    room.m_cloudRecordPaused = paused;
    recordId = room.m_cloudRecordId;
    outRoom = room;
    return error_code_ok;
}

int ScreenShareManager::endCloudRecord(const std::string& roomId, int64_t requesterId, int64_t expectRecordId,
                                       int endCode, int64_t& recordId, ScreenShareRoom& outRoom)
{
    recordId = 0;
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
        return error_code_screenshare_not_exist;
    ScreenShareRoom& room = it->second;
    if (requesterId != 0 && room.m_hostId != requesterId)
        return error_code_screenshare_not_host;
    if (room.m_cloudRecordId == 0 || (expectRecordId != 0 && room.m_cloudRecordId != expectRecordId))
        return error_code_cloudrecord_busy;
    recordId = room.m_cloudRecordId;
    room.m_cloudRecordId = 0;
    room.m_cloudRecordOwner = 0;
    room.m_cloudRecordPaused = false;
    room.m_cloudRecordSince = 0;
    room.m_cloudRecordEndCode = endCode;
    outRoom = room;
    return error_code_ok;
}

bool ScreenShareManager::setMemberP2PSession(const std::string& roomId, int64_t userId, const std::string& p2pSessionId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return false;
    }
    for (size_t i = 0; i < it->second.m_members.size(); ++i)
    {
        if (it->second.m_members[i].m_userId == userId)
        {
            it->second.m_members[i].m_p2pSessionId = p2pSessionId;
            return true;
        }
    }
    return false;
}

bool ScreenShareManager::setCardMsgId(const std::string& roomId, int64_t cardMsgId)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return false;
    }
    it->second.m_cardMsgId = cardMsgId;
    return true;
}

bool ScreenShareManager::findForcedRejoin(int64_t userId, ScreenShareRoom& outRoom) const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    //已在某个房间里（如重新登录前已从别处加入）：不再拉回
    if (m_userRoom.find(userId) != m_userRoom.end())
    {
        return false;
    }
    for (std::map<std::string, ScreenShareRoom>::const_iterator it = m_rooms.begin(); it != m_rooms.end(); ++it)
    {
        if (it->second.isForcedAway(userId))
        {
            outRoom = it->second;
            return true;
        }
    }
    return false;
}

void ScreenShareManager::reapExpiredHosts(int64_t now, std::vector<ScreenShareRoom>& outExpired)
{
    outExpired.clear();
    std::lock_guard<std::mutex> guard(m_mutex);
    std::vector<std::string> expiredIds;
    for (std::map<std::string, ScreenShareRoom>::const_iterator it = m_rooms.begin(); it != m_rooms.end(); ++it)
    {
        const ScreenShareRoom& room = it->second;
        if (room.m_hostOfflineSince != 0 && now - room.m_hostOfflineSince >= m_config.m_hostGraceMs)
        {
            expiredIds.push_back(it->first);
        }
    }
    for (size_t i = 0; i < expiredIds.size(); ++i)
    {
        ScreenShareRoom snapshot;
        dissolveLocked(expiredIds[i], snapshot);
        outExpired.push_back(snapshot);
    }
}

void ScreenShareManager::makeConvKey(int ctype, int64_t selfId, int64_t cid, int64_t& keyA, int64_t& keyB)
{
    if (ctype == conversation_single)
    {
        //单聊按两人 userid 排序，双方从各自角度查到的是同一个键
        keyA = (selfId < cid) ? selfId : cid;
        keyB = (selfId < cid) ? cid : selfId;
        return;
    }
    keyA = cid;
    keyB = 0;
}

int ScreenShareManager::lowestFreeSeat(const ScreenShareRoom& room)
{
    for (int seat = 1; seat <= kMaxSeat; ++seat)
    {
        bool taken = false;
        for (size_t i = 0; i < room.m_members.size(); ++i)
        {
            //成员座位与画面座位（第 22 期）都算占用
            if (room.m_members[i].m_seat == seat || room.m_members[i].m_screenSeat == seat)
            {
                taken = true;
                break;
            }
        }
        //白板（第 23 期）的画面座位同样算占用
        for (size_t i = 0; i < room.m_whiteboards.size() && !taken; ++i)
        {
            if (room.m_whiteboards[i].m_seat == seat)
            {
                taken = true;
            }
        }
        if (!taken)
        {
            return seat;
        }
    }
    return -1;
}

void ScreenShareManager::dissolveLocked(const std::string& roomId, ScreenShareRoom& outRoom)
{
    std::map<std::string, ScreenShareRoom>::iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
    {
        return;
    }
    outRoom = it->second;
    for (size_t i = 0; i < it->second.m_members.size(); ++i)
    {
        m_userRoom.erase(it->second.m_members[i].m_userId);
    }
    m_rooms.erase(it);
}

void ScreenShareManager::removeMemberLocked(ScreenShareRoom& room, int64_t userId)
{
    for (std::vector<ScreenShareMember>::iterator it = room.m_members.begin(); it != room.m_members.end(); ++it)
    {
        if (it->m_userId == userId)
        {
            room.m_members.erase(it);
            break;
        }
    }
    //离开的人若正在控制某块画面，控制随之结束（离开、被移出、掉线都走这里）
    for (size_t i = 0; i < room.m_members.size(); ++i)
    {
        if (room.m_members[i].m_controlledBy == userId)
        {
            room.m_members[i].m_controlledBy = 0;
        }
    }
    //离开的人建的白板随之关闭（第 23 期）
    const int board = findBoardIndex(room, userId);
    if (board >= 0)
    {
        room.m_whiteboards.erase(room.m_whiteboards.begin() + board);
    }
    m_userRoom.erase(userId);
}

ScreenShareMember* ScreenShareManager::findScreenOwnerLocked(ScreenShareRoom& room, int64_t ownerId)
{
    //第 22 期起任何正在共享的人都是画面主人；主持人掉线的宽限期里他的画面暂时不存在
    if (ownerId == room.m_hostId && room.m_hostOfflineSince != 0)
    {
        return NULL;
    }
    ScreenShareMember* owner = findMemberLocked(room, ownerId);
    return (owner != NULL && owner->m_sharing) ? owner : NULL;
}
