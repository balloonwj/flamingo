/**
 *  群通话窗口的宫格排布与格子排序，GroupCallLayout.cpp
 *  规则见 GroupCallLayout.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "GroupCallLayout.h"

#include <stdio.h>
#include <algorithm>
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找

namespace groupcall {

namespace {

//摄像头码率自适应（第 5 期）：开摄像头的人数达到这两个值时分别降到中、低档；三档码率单位 bit/s
const int kCamerasForMidBitrate = 3;
const int kCamerasForLowBitrate = 5;
const int kCameraBitrateHigh = 400000;
const int kCameraBitrateMid = 300000;
const int kCameraBitrateLow = 200000;

//毫秒、秒、分钟、小时之间的换算
const int64_t kMsPerSecond = 1000;
const int64_t kSecondsPerMinute = 60;
const int64_t kSecondsPerHour = 3600;

//宫格的几种形状：格子数上限 → 行、列
struct GridShape
{
    int m_maxCount;   //这个形状最多放几格
    int m_rows;       //行数
    int m_cols;       //列数
};

const GridShape kGridShapes[] =
{
    { 1, 1, 1 },   //1 人整幅
    { 2, 1, 2 },   //2 人左右两格
    { 4, 2, 2 },   //3~4 人 2×2
    { 6, 2, 3 },   //5~6 人 2×3
    { 9, 3, 3 },   //7~9 人 3×3
};

//按座位号升序比较两格
bool seatLess(const GroupCallTile& a, const GroupCallTile& b)
{
    return a.m_seat < b.m_seat;
}

GroupCallTile tileOf(const GroupCallMember& m, bool self)
{
    GroupCallTile t;
    t.m_userId = m.m_userId;
    t.m_seat = m.m_seat;
    t.m_self = self;
    t.m_ringing = (m.m_state == gc_member_ringing);
    t.m_camera = m.m_camera;
    t.m_mic = m.m_mic;
    t.m_online = m.m_online;
    return t;
}

} // namespace

GroupCallTile::GroupCallTile()
    : m_userId(0)
    , m_seat(0)
    , m_self(false)
    , m_ringing(false)
    , m_camera(false)
    , m_mic(false)
    , m_online(true)
{
}

void GridShapeFor(int count, int& rows, int& cols)
{
    if (count < 1)
        count = 1;
    const size_t shapes = sizeof(kGridShapes) / sizeof(kGridShapes[0]);
    for (size_t i = 0; i < shapes; ++i)
    {
        if (count <= kGridShapes[i].m_maxCount)
        {
            rows = kGridShapes[i].m_rows;
            cols = kGridShapes[i].m_cols;
            return;
        }
    }
    rows = kGridShapes[shapes - 1].m_rows;
    cols = kGridShapes[shapes - 1].m_cols;
}

void BuildGroupCallTiles(const GroupCallState& state, int64_t selfId, bool selfCamera, bool selfMic, int maxGridTiles,
                         std::vector<GroupCallTile>& grid, std::vector<GroupCallTile>& strip)
{
    grid.clear();
    strip.clear();
    std::vector<GroupCallTile> ordered;
    std::vector<GroupCallTile> withCamera;
    std::vector<GroupCallTile> withoutCamera;
    std::vector<GroupCallTile> ringing;
    for (size_t i = 0; i < state.m_members.size(); ++i)
    {
        const GroupCallMember& m = state.m_members[i];
        if (m.m_userId == selfId)
        {
            //本人只在已加入时出现，摄像头与麦克风按本地设备状态
            if (m.m_state == gc_member_joined)
            {
                GroupCallTile self = tileOf(m, true);
                self.m_camera = selfCamera;
                self.m_mic = selfMic;
                ordered.push_back(self);
            }
            continue;
        }
        if (m.m_state == gc_member_ringing)
            ringing.push_back(tileOf(m, false));
        else if (m.m_camera)
            withCamera.push_back(tileOf(m, false));
        else
            withoutCamera.push_back(tileOf(m, false));
    }
    std::stable_sort(withCamera.begin(), withCamera.end(), seatLess);
    std::stable_sort(withoutCamera.begin(), withoutCamera.end(), seatLess);
    ordered.insert(ordered.end(), withCamera.begin(), withCamera.end());
    ordered.insert(ordered.end(), withoutCamera.begin(), withoutCamera.end());
    ordered.insert(ordered.end(), ringing.begin(), ringing.end());

    const size_t limit = maxGridTiles > 0 ? static_cast<size_t>(maxGridTiles) : 0;
    for (size_t i = 0; i < ordered.size(); ++i)
    {
        if (i < limit)
            grid.push_back(ordered[i]);
        else
            strip.push_back(ordered[i]);
    }
}

std::vector<int> SubscribedCameraSeats(const std::vector<GroupCallTile>& grid)
{
    std::vector<int> seats;
    for (size_t i = 0; i < grid.size(); ++i)
    {
        if (!grid[i].m_self && !grid[i].m_ringing && grid[i].m_camera && grid[i].m_seat > 0)
            seats.push_back(grid[i].m_seat);
    }
    std::sort(seats.begin(), seats.end());
    return seats;
}

bool SameGridStructure(const std::vector<GroupCallTile>& a, const std::vector<GroupCallTile>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].m_userId != b[i].m_userId || a[i].m_ringing != b[i].m_ringing || a[i].m_camera != b[i].m_camera)
            return false;
    }
    return true;
}

int CountGroupCallCameras(const std::vector<GroupCallTile>& grid, const std::vector<GroupCallTile>& strip)
{
    int count = 0;
    for (size_t i = 0; i < grid.size(); ++i)
    {
        if (grid[i].m_camera && !grid[i].m_ringing)
            ++count;
    }
    for (size_t i = 0; i < strip.size(); ++i)
    {
        if (strip[i].m_camera && !strip[i].m_ringing)
            ++count;
    }
    return count;
}

int GroupCallCameraBitrateFor(int camerasOn)
{
    if (camerasOn >= kCamerasForLowBitrate)
        return kCameraBitrateLow;
    if (camerasOn >= kCamerasForMidBitrate)
        return kCameraBitrateMid;
    return kCameraBitrateHigh;
}

std::wstring FormatGroupCallDuration(int64_t elapsedMs)
{
    if (elapsedMs < 0)
        elapsedMs = 0;
    const int64_t total = elapsedMs / kMsPerSecond;
    const int hours = static_cast<int>(total / kSecondsPerHour);
    const int minutes = static_cast<int>((total % kSecondsPerHour) / kSecondsPerMinute);
    const int seconds = static_cast<int>(total % kSecondsPerMinute);
    wchar_t buf[32] = { 0 };
    if (hours > 0)
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d:%02d:%02d", hours, minutes, seconds);
    else
        _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%02d:%02d", minutes, seconds);
    return std::wstring(buf);
}

std::wstring MakeGroupCallTitle(const std::wstring& groupName, int media)
{
    const std::wstring kind = (media == gc_media_video) ? TR(L"群视频通话") : TR(L"群语音通话");
    if (groupName.empty())
        return kind;
    return groupName + L" · " + kind;
}

std::wstring MakeGroupCallInvitePrompt(const std::wstring& inviterName, int media)
{
    if (media == gc_media_video)
        return TRF(L"{0} 邀请你加入群视频通话", inviterName);
    return TRF(L"{0} 邀请你加入群语音通话", inviterName);
}

std::wstring MakeGroupCallInviteDetail(const std::wstring& groupName, int joinedCount)
{
    if (joinedCount <= 0)
        return groupName;
    const std::wstring count = TRF(L"{0} 人正在通话", joinedCount);
    if (groupName.empty())
        return count;
    return groupName + L" · " + count;
}

int DecideSelfMicOnState(const GroupCallState& state, int64_t selfId, bool canManage, bool localMicOn)
{
    const GroupCallMember* self = state.findMember(selfId);
    //本地本来就关着、本人不是已加入的状态、或快照里本人的麦克风仍开着：没有要关的
    if (!localMicOn || self == NULL || self->m_state != gc_member_joined || self->m_mic)
        return gc_self_mic_keep;
    //被单独静音
    if (state.m_event == gc_event_member_muted && state.m_target == selfId)
        return gc_self_mic_muted_by_peer;
    //别人开启了全员静音（老服务端不带 allmuted，同样按这一帧处理）
    if (state.m_event == gc_event_muted_all && state.m_actor != selfId)
        return gc_self_mic_muted_by_all;
    //房间处于全员静音、本人没有管理权：本人在全员静音期间加入，服务端已按关麦克风记下
    if (IsGroupCallMicLocked(state.m_allMuted, canManage))
        return gc_self_mic_muted_by_all;
    return gc_self_mic_keep;
}

bool IsGroupCallMicLocked(bool allMuted, bool canManage)
{
    return allMuted && !canManage;
}

std::wstring MakeGroupCallStatusLine(const std::wstring& notice, bool micLocked, const std::wstring& talkers)
{
    if (!notice.empty())
        return notice;
    const std::wstring talking = talkers.empty() ? std::wstring() : TRF(L"{0} 正在说话", talkers);
    if (!micLocked)
        return talking;
    if (talking.empty())
        return TR(L"已开启全员静音");
    return TRF(L"已开启全员静音 · {0}", talking);
}

} // namespace groupcall
