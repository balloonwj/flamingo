/**
 *  多人共享的画面清单与多画面布局，见 ShareScreens.h。
 *  balloonwj@qq.com   2026-10-05
 */
#include "ShareScreens.h"

#include <math.h>
#include <algorithm>

namespace screenshare {

namespace {

//按画面座位升序（主持人另行排在最前）
bool screenSeatLess(const ShareScreenInfo& a, const ShareScreenInfo& b)
{
    return a.m_screenSeat < b.m_screenSeat;
}

//矩形
RECT makeRect(int left, int top, int right, int bottom)
{
    RECT r = { left, top, right, bottom };
    return r;
}

//在 area 里按 cols × rows 的网格平分，取前 count 格（按行从左到右）
std::vector<RECT> gridTiles(const RECT& area, int count, int cols, int rows, int gap)
{
    std::vector<RECT> out;
    const int w = area.right - area.left;
    const int h = area.bottom - area.top;
    const int cellW = (w - gap * (cols - 1)) / cols;
    const int cellH = (h - gap * (rows - 1)) / rows;
    for (int i = 0; i < count; ++i)
    {
        const int col = i % cols;
        const int row = i / cols;
        const int left = area.left + col * (cellW + gap);
        const int top = area.top + row * (cellH + gap);
        //最右一列、最下一行贴到区域边上，免得整除余下的几个像素空着
        const int right = (col == cols - 1) ? area.right : left + cellW;
        const int bottom = (row == rows - 1) ? area.bottom : top + cellH;
        out.push_back(makeRect(left, top, right, bottom));
    }
    return out;
}

} // namespace

ShareScreenInfo::ShareScreenInfo()
    : m_ownerId(0)
    , m_ownerSeat(-1)
    , m_screenSeat(-1)
    , m_isHost(false)
    , m_isWhiteboard(false)
{
}

std::vector<ShareScreenInfo> CollectShareScreens(const ShareStateNotify& state)
{
    std::vector<ShareScreenInfo> host;
    std::vector<ShareScreenInfo> others;
    for (size_t i = 0; i < state.m_members.size(); ++i)
    {
        const ShareMember& m = state.m_members[i];
        if (!m.m_sharing || m.m_screenSeat < 0)
            continue;
        ShareScreenInfo s;
        s.m_ownerId = m.m_userId;
        s.m_ownerSeat = m.m_seat;
        s.m_screenSeat = m.m_screenSeat;
        s.m_isHost = (m.m_userId == state.m_hostId);
        if (s.m_isHost)
            host.push_back(s);
        else
            others.push_back(s);
    }
    std::sort(others.begin(), others.end(), screenSeatLess);
    host.insert(host.end(), others.begin(), others.end());
    //白板（第 23 期）排在所有屏幕之后，按画面座位升序
    std::vector<ShareScreenInfo> boards;
    for (size_t i = 0; i < state.m_whiteboards.size(); ++i)
    {
        const ShareWhiteboardInfo& b = state.m_whiteboards[i];
        ShareScreenInfo s;
        s.m_ownerId = b.m_ownerId;
        const ShareMember* owner = state.findMember(b.m_ownerId);
        s.m_ownerSeat = (owner != NULL) ? owner->m_seat : -1;
        s.m_screenSeat = b.m_seat;
        s.m_isHost = (b.m_ownerId == state.m_hostId);
        s.m_isWhiteboard = true;
        boards.push_back(s);
    }
    std::sort(boards.begin(), boards.end(), screenSeatLess);
    host.insert(host.end(), boards.begin(), boards.end());
    return host;
}

int ShareSlotsUsed(const ShareStateNotify& state)
{
    int n = 0;
    for (size_t i = 0; i < state.m_members.size(); ++i)
    {
        if (state.m_members[i].m_sharing)
            ++n;
    }
    //白板（第 23 期）也占共享名额
    return n + static_cast<int>(state.m_whiteboards.size());
}

int FindScreenBySeat(const std::vector<ShareScreenInfo>& screens, int screenSeat)
{
    for (size_t i = 0; i < screens.size(); ++i)
    {
        if (screens[i].m_screenSeat == screenSeat)
            return static_cast<int>(i);
    }
    return -1;
}

std::vector<RECT> ComputeScreenTiles(const RECT& area, int count, int enlarged, int gapPx)
{
    std::vector<RECT> out;
    if (count <= 0)
        return out;
    const int gap = gapPx > 0 ? gapPx : 0;
    if (count == 1)
    {
        out.push_back(area);
        return out;
    }
    const int w = area.right - area.left;
    const int h = area.bottom - area.top;
    //放大一块：它占上方，其余缩成底部居中的一排小图
    if (enlarged >= 0 && enlarged < count)
    {
        const int stripH = h * kScreenStripPercent / 100;
        const int others = count - 1;
        const int fairW = (w - gap * (others - 1)) / others;
        const int aspectW = stripH * kScreenThumbAspectW / kScreenThumbAspectH;
        const int thumbW = (std::min)(fairW, aspectW);
        const int rowW = thumbW * others + gap * (others - 1);
        int x = area.left + (w - rowW) / 2;
        const int stripTop = area.bottom - stripH;
        out.assign(count, RECT());
        out[enlarged] = makeRect(area.left, area.top, area.right, stripTop - gap);
        for (int i = 0; i < count; ++i)
        {
            if (i == enlarged)
                continue;
            out[i] = makeRect(x, stripTop, x + thumbW, area.bottom);
            x += thumbW + gap;
        }
        return out;
    }
    //平分：2 块左右；3～4 块田字格；更多时按近似正方形的网格
    if (count == 2)
        return gridTiles(area, count, 2, 1, gap);
    const int cols = static_cast<int>(ceil(sqrt(static_cast<double>(count))));
    const int rows = (count + cols - 1) / cols;
    return gridTiles(area, count, cols, rows, gap);
}

std::vector<int64_t> HostTransferCandidates(const ShareStateNotify& state, int64_t selfId)
{
    //与画面清单同一顺序（按画面座位），去掉本人；只有共享屏幕的人能接任主持人（第 23 期：只建了白板的人不算）
    std::vector<int64_t> out;
    const std::vector<ShareScreenInfo> screens = CollectShareScreens(state);
    for (size_t i = 0; i < screens.size(); ++i)
    {
        if (screens[i].m_ownerId != selfId && !screens[i].m_isWhiteboard)
            out.push_back(screens[i].m_ownerId);
    }
    return out;
}

int ShareMineStateForPhase(int phase)
{
    //取值与 share_viewer_mine_state 一致：0 没有申请也没在共享、1 已申请、2 正在共享
    const int kMineNone = 0;
    const int kMineRequested = 1;
    const int kMineSharing = 2;
    if (phase == share_publish_requested)
        return kMineRequested;
    if (phase == share_publish_picking || phase == share_publish_streaming)
        return kMineSharing;
    return kMineNone;
}

int SyncPublishPhase(int phase, const ShareStateNotify& state, int64_t selfId)
{
    //只校正选择内容中与推流中：服务端此时一定已把本人记为共享者，快照里不是就说明被停掉了
    if (phase != share_publish_picking && phase != share_publish_streaming)
        return phase;
    const ShareMember* self = state.findMember(selfId);
    if (self == NULL || !self->m_sharing)
        return share_publish_idle;
    return phase;
}

std::vector<ShareScreenInfo> CollectOtherScreens(const ShareStateNotify& state, int64_t selfId)
{
    std::vector<ShareScreenInfo> out;
    const std::vector<ShareScreenInfo> all = CollectShareScreens(state);
    for (size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].m_ownerId != selfId)
            out.push_back(all[i]);
    }
    return out;
}

bool PickScreenRect(const std::vector<int>& tileSeats, const std::vector<RECT>& videoRects, int seat, RECT& out)
{
    out = makeRect(0, 0, 0, 0);
    if (videoRects.empty())
        return false;
    if (tileSeats.empty())
    {
        out = videoRects[0];
        return !::IsRectEmpty(&out);
    }
    for (size_t i = 0; i < tileSeats.size() && i < videoRects.size(); ++i)
    {
        if (tileSeats[i] == seat)
        {
            out = videoRects[i];
            return !::IsRectEmpty(&out);
        }
    }
    return false;
}

ShareRequestQueue::ShareRequestQueue(int kind)
    : m_kind(kind)
{
}

bool ShareRequestQueue::Add(int64_t userId, const std::wstring& name)
{
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        if (m_items[i].first == userId)
            return false;
    }
    m_items.push_back(std::make_pair(userId, name));
    return true;
}

bool ShareRequestQueue::Remove(int64_t userId)
{
    for (std::deque<std::pair<int64_t, std::wstring> >::iterator it = m_items.begin(); it != m_items.end(); ++it)
    {
        if (it->first == userId)
        {
            m_items.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<int64_t> ShareRequestQueue::Prune(const ShareStateNotify& state)
{
    std::vector<int64_t> removed;
    std::deque<std::pair<int64_t, std::wstring> > kept;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        const ShareMember* m = state.findMember(m_items[i].first);
        const bool requesting = (m_kind == share_kind_whiteboard) ? (m != NULL && m->m_boardRequesting)
                                                                  : (m != NULL && m->m_requesting);
        if (!requesting)
            removed.push_back(m_items[i].first);
        else
            kept.push_back(m_items[i]);
    }
    m_items.swap(kept);
    return removed;
}

int64_t ShareRequestQueue::FrontUser() const
{
    return m_items.empty() ? 0 : m_items.front().first;
}

std::wstring ShareRequestQueue::FrontName() const
{
    return m_items.empty() ? std::wstring() : m_items.front().second;
}

size_t ShareRequestQueue::Size() const
{
    return m_items.size();
}

void ShareRequestQueue::Clear()
{
    m_items.clear();
}

int HitScreenTile(const std::vector<RECT>& tiles, POINT pt)
{
    for (size_t i = 0; i < tiles.size(); ++i)
    {
        if (::PtInRect(&tiles[i], pt))
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace screenshare
