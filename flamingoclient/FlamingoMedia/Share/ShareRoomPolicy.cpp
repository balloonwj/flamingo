/**
 *  屏幕共享房间里与界面无关的规则，ShareRoomPolicy.cpp
 *  说明见 ShareRoomPolicy.h。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "ShareRoomPolicy.h"

#include <stdio.h>
#include <windows.h>   // WideCharToMultiByte（ShareTextToUtf8，第 15 期）
#include <algorithm>
#include "../../Source/Common/I18n/I18n.h"   // TR：界面文字的多语言查找
#include "../../Source/net/Protocol.h"   // 云端录制的错误码与动作（第 27b 期）

namespace screenshare {

namespace {

//时间换算
const int64_t kSecondsPerMinute = 60;
const int64_t kSecondsPerHour = 3600;
const int64_t kMsPerSecond = 1000;
//时长文字的缓冲区长度（字符），足够放下「h:mm:ss」
const size_t kDurationChars = 32;

//名单排序：按座位号升序
bool viewerBySeat(const ShareViewerRow& a, const ShareViewerRow& b)
{
    return a.m_seat < b.m_seat;
}

//以下两段文字只做标记（TR_NOOP），使用处再用 TR 取译文
//强制模式的右下角提示在发起人名（与标题）之后接的说明（第 14 期）
const wchar_t* const kForcedToastTail = TR_NOOP(L"。这是强制模式：加入后全屏观看、不能中途退出，直到对方结束共享。");
//加入强制模式的共享前确认框的正文（第 13 期）；第 15 期起前面加上共享标题与描述
const wchar_t* const kForcedJoinBody =
    TR_NOOP(L"这是强制观看的屏幕共享：加入后要等发起人结束共享才能离开，观看窗会一直全屏显示在最前面。\n确定要加入吗？");
//全角空格（第 15 期，共享标题与描述去掉首尾空白时一并去掉）
const wchar_t kFullWidthSpace = L'\x3000';
//UTF-16 高位代理的取值范围：它与随后的低位代理合起来表示一个字符（如表情）
const wchar_t kHighSurrogateFirst = 0xD800;
const wchar_t kHighSurrogateLast = 0xDBFF;

//是否是共享标题与描述首尾要去掉的空白：半角空格、制表、回车、换行与全角空格
bool isShareInfoBlank(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == kFullWidthSpace;
}

//去掉首尾的空白
void trimShareInfo(std::wstring& s)
{
    size_t begin = 0;
    while (begin < s.size() && isShareInfoBlank(s[begin]))
        ++begin;
    size_t end = s.size();
    while (end > begin && isShareInfoBlank(s[end - 1]))
        --end;
    s = s.substr(begin, end - begin);
}

} // namespace

std::wstring FormatShareDuration(int64_t seconds)
{
    if (seconds < 0)
        seconds = 0;
    const int64_t hours = seconds / kSecondsPerHour;
    const int64_t minutes = (seconds % kSecondsPerHour) / kSecondsPerMinute;
    const int64_t secs = seconds % kSecondsPerMinute;
    wchar_t buf[kDurationChars] = { 0 };
    if (hours > 0)
    {
        _snwprintf_s(buf, kDurationChars, _TRUNCATE, L"%lld:%02lld:%02lld", static_cast<long long>(hours),
                     static_cast<long long>(minutes), static_cast<long long>(secs));
    }
    else
    {
        _snwprintf_s(buf, kDurationChars, _TRUNCATE, L"%02lld:%02lld", static_cast<long long>(minutes),
                     static_cast<long long>(secs));
    }
    return std::wstring(buf);
}

ShareDurationClock::ShareDurationClock()
    : m_running(false)
    , m_baseMs(0)
    , m_baseTick(0)
{
}

void ShareDurationClock::Start(int64_t elapsedMs, uint64_t nowTick)
{
    m_running = true;
    m_baseMs = elapsedMs > 0 ? elapsedMs : 0;
    m_baseTick = nowTick;
}

void ShareDurationClock::Stop()
{
    m_running = false;
    m_baseMs = 0;
    m_baseTick = 0;
}

bool ShareDurationClock::IsRunning() const
{
    return m_running;
}

int64_t ShareDurationClock::ElapsedSeconds(uint64_t nowTick) const
{
    if (!m_running)
        return 0;
    //本机时刻早于开始时刻（不应出现）时按开始时刻算
    const int64_t passed = nowTick > m_baseTick ? static_cast<int64_t>(nowTick - m_baseTick) : 0;
    return (m_baseMs + passed) / kMsPerSecond;
}

ShareViewerRow::ShareViewerRow()
    : m_userId(0)
    , m_seat(0)
    , m_micOn(false)
    , m_speaking(false)
    , m_canMute(false)
    , m_canRemove(false)
    , m_canControl(false)
    , m_controlling(false)
{
}

void BuildShareViewerRows(const ShareStateNotify& state, const std::map<int64_t, std::wstring>& names,
                          const std::set<int>& speakingSeats, bool asHost, std::vector<ShareViewerRow>& out)
{
    out.clear();
    //正在控制发起人屏幕的人（第 18 期）：记在发起人成员项的 controlledby 上
    int64_t controller = 0;
    const ShareMember* hostMember = state.findMember(state.m_hostId);
    if (hostMember != NULL)
        controller = hostMember->m_controlledBy;
    for (size_t i = 0; i < state.m_members.size(); ++i)
    {
        const ShareMember& m = state.m_members[i];
        if (m.m_userId == state.m_hostId)
            continue;
        ShareViewerRow row;
        row.m_userId = m.m_userId;
        row.m_seat = m.m_seat;
        std::map<int64_t, std::wstring>::const_iterator it = names.find(m.m_userId);
        if (it != names.end() && !it->second.empty())
        {
            row.m_name = it->second;
        }
        else
        {
            //与会话列表同一口径：查不到显示名时写「用户<id>」
            wchar_t buf[kDurationChars] = { 0 };
            _snwprintf_s(buf, kDurationChars, _TRUNCATE, TR(L"用户%lld"), static_cast<long long>(m.m_userId));
            row.m_name = buf;
        }
        row.m_micOn = m.m_mic;
        row.m_speaking = speakingSeats.find(m.m_seat) != speakingSeats.end();
        row.m_canMute = asHost && m.m_mic;
        //「移出」与对方开没开麦克风无关，发起人对每个观看者都可以（第 14 期）
        row.m_canRemove = asHost;
        //控制键鼠（第 18 期）：发起人对每个观看者都可以授予或收回；谁在控制所有人都看得到
        row.m_canControl = asHost;
        row.m_controlling = (controller != 0 && controller == m.m_userId);
        out.push_back(row);
    }
    std::sort(out.begin(), out.end(), viewerBySeat);
}

std::wstring ShareViewerStatusText(const ShareViewerRow& row)
{
    //正在控制屏幕（第 18 期）比麦克风状态更要紧
    if (row.m_controlling)
        return TR(L"正在控制");
    if (row.m_micOn && row.m_speaking)
        return TR(L"正在说话");
    return row.m_micOn ? TR(L"麦克风开着") : TR(L"麦克风已关");
}

std::wstring ShareViewerCountText(int viewerCount)
{
    if (viewerCount <= 0)
        return TR(L"暂无观看者 ▾");
    wchar_t buf[kDurationChars] = { 0 };
    _snwprintf_s(buf, kDurationChars, _TRUNCATE, TR(L"%d 人观看 ▾"), viewerCount);
    return std::wstring(buf);
}

bool ShouldMuteSelfByHost(int event, int64_t target, int64_t selfId, bool micEnabled)
{
    return event == share_event_member_muted && target != 0 && target == selfId && micEnabled;
}

ShareViewerChrome::ShareViewerChrome()
    : m_showLeave(true)
    , m_showExitFullscreen(true)
    , m_escExitsFullscreen(true)
    , m_closeLeaves(true)
    , m_topmost(false)
    , m_pullBackToFront(false)
{
}

ShareViewerChrome MakeShareViewerChrome(bool forced)
{
    ShareViewerChrome chrome;
    if (forced)
    {
        //强制模式：观看者要等发起人结束才能离开，只能全屏观看，切走后拉回前台
        chrome.m_showLeave = false;
        chrome.m_showExitFullscreen = false;
        chrome.m_escExitsFullscreen = false;
        chrome.m_closeLeaves = false;
        chrome.m_topmost = true;
        chrome.m_pullBackToFront = true;
    }
    return chrome;
}

bool ShouldConfirmForcedJoin(bool forced, bool rejoin)
{
    return forced && !rejoin;
}

std::wstring MakeShareStartedToastText(const std::wstring& hostName, bool locked, bool forced)
{
    //{0} 为发起人名，不知道时为「对方」
    std::wstring text = TRF(L"{0} 正在共享屏幕", hostName.empty() ? std::wstring(TR(L"对方")) : hostName);
    if (locked)
        text += TR(L"（需要密码）");
    //强制模式写明效果，不只写一个词（第 14 期用户要求）
    if (forced)
        text += TR(kForcedToastTail);
    return text;
}

std::wstring MakeShareRemovedNotice(const std::wstring& hostName, bool canRejoin)
{
    //能否再加入各用一句完整的模板；{0} 为发起人名，不知道时为「发起人」
    const std::wstring who = hostName.empty() ? std::wstring(TR(L"发起人")) : hostName;
    if (canRejoin)
        return TRF(L"你已被 {0} 移出屏幕共享。", who);
    return TRF(L"你已被 {0} 移出屏幕共享，不能再加入这场共享。", who);
}

std::wstring MakeShareRemovePrompt(const std::wstring& viewerName)
{
    //{0} 为观看者名，不知道时为「这位观看者」
    return TRF(L"确定将 {0} 移出屏幕共享吗？", viewerName.empty() ? std::wstring(TR(L"这位观看者")) : viewerName);
}

bool IsShareRemovedReason(int reason)
{
    return reason == share_end_removed || reason == share_end_removed_banned;
}

std::wstring MakeDefaultShareTitle(const std::wstring& hostName)
{
    //不知道发起人名时用「我的屏幕共享」整句，不拼「我」+「的屏幕共享」
    if (hostName.empty())
        return TR(L"我的屏幕共享");
    return TRF(L"{0}的屏幕共享", hostName);
}

size_t CountShareInfoChars(const std::wstring& text)
{
    size_t chars = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        //高位代理与随后的低位代理合起来算一个字符
        if (text[i] >= kHighSurrogateFirst && text[i] <= kHighSurrogateLast && i + 1 < text.size())
            ++i;
        ++chars;
    }
    return chars;
}

bool NormalizeShareInfo(std::wstring& title, std::wstring& desc)
{
    trimShareInfo(title);
    trimShareInfo(desc);
    return !title.empty() && CountShareInfoChars(title) <= kShareTitleMaxChars &&
           CountShareInfoChars(desc) <= kShareDescMaxChars;
}

std::wstring MakeShareStartedToastText(const std::wstring& hostName, const std::wstring& title, bool locked,
                                       bool forced)
{
    //老服务端没有标题：与第 14 期的写法相同
    if (title.empty())
        return MakeShareStartedToastText(hostName, locked, forced);
    //{0} 为发起人名（不知道时为「对方」），{1} 为共享标题
    std::wstring text = TRF(L"{0} 正在共享：{1}", hostName.empty() ? std::wstring(TR(L"对方")) : hostName, title);
    if (locked)
        text += TR(L"（需要密码）");
    if (forced)
        text += TR(kForcedToastTail);
    return text;
}

std::wstring MakeShareViewerCaption(const std::wstring& title, const std::wstring& hostName)
{
    if (!title.empty())
        return title;
    return hostName.empty() ? std::wstring(TR(L"屏幕共享")) : TRF(L"{0} 的屏幕共享", hostName);
}

std::wstring MakeShareBarCaption(const std::wstring& title)
{
    if (title.empty())
        return std::wstring(TR(L"屏幕共享"));
    return TRF(L"屏幕共享 · {0}", title);
}

std::wstring MakeShareBarStatusText(bool paused, const std::wstring& duration, const std::wstring& password, bool forced,
                                    const std::wstring& speaking)
{
    std::wstring text = paused ? TR(L"已暂停") : TR(L"正在共享");
    if (!duration.empty())
        text += L" " + duration;
    if (!password.empty())
    {
        text += L" · ";
        text += TRF(L"密码 {0}", password);
    }
    if (forced)
        text += TR(L" · 强制模式");
    if (!speaking.empty())
        text += L" · " + speaking;
    return text;
}

std::wstring MakeShareRecordingText(const std::wstring& duration, bool withDot, bool dotOn)
{
    if (duration.empty())
        return std::wstring();
    //{0} 为已录的时长，如 03:21
    const std::wstring text = TRF(L"录制 {0}", duration);
    if (!withDot)
        return text;
    //实心与空心圆点交替，表示正在录制并闪烁
    return std::wstring(dotOn ? L"● " : L"○ ") + text;
}

ShareHostMenuItem::ShareHostMenuItem()
    : m_id(0)
    , m_checkable(false)
    , m_checked(false)
    , m_separatorBefore(false)
{
}

void BuildShareHostMenu(bool recordAllowed, bool annotateAllowed, bool captureAllowed,
                        std::vector<ShareHostMenuItem>& out)
{
    out.clear();
    ShareHostMenuItem item;
    item.m_id = share_host_menu_edit_info;
    item.m_text = TR(L"修改标题与描述…");
    out.push_back(item);
    item.m_id = share_host_menu_mute_all;
    item.m_text = TR(L"全员静音");
    out.push_back(item);
    //两项权限是勾选项，与上面两项动作之间画分隔线
    item.m_id = share_host_menu_record_perm;
    item.m_text = TR(L"允许他人录制");
    item.m_checkable = true;
    item.m_checked = recordAllowed;
    item.m_separatorBefore = true;
    out.push_back(item);
    item.m_id = share_host_menu_annot_perm;
    item.m_text = TR(L"允许他人标注");
    item.m_checked = annotateAllowed;
    item.m_separatorBefore = false;
    out.push_back(item);
    //允许截图（2026-10-08）：本机的设置，与上面两项房间权限分开
    item.m_id = share_host_menu_allow_capture;
    item.m_text = TR(L"允许截图");
    item.m_checked = captureAllowed;
    item.m_separatorBefore = true;
    out.push_back(item);
}

bool IsTransferredShareHost(bool hostStreaming, bool watching, const std::string& viewerRoomId,
                            const std::string& stateRoomId, int64_t stateHostId, int64_t selfId)
{
    //发起人有自己的控制条；不在观看窗里、快照不是这一场时都不算
    if (hostStreaming || !watching || viewerRoomId.empty() || stateRoomId != viewerRoomId)
        return false;
    return selfId > 0 && stateHostId == selfId;
}

int DecideShareRelayInfoAction(const std::string& roomId, bool hostActive, const std::string& hostRoomId,
                               bool hostWaiting, bool streamerRunning, bool viewerJoining, bool viewerWatching,
                               const std::string& viewerRoomId)
{
    if (roomId.empty())
        return share_relay_info_ignore;
    //本人发起的房间
    if (hostActive && roomId == hostRoomId)
        return (hostWaiting || !streamerRunning) ? share_relay_info_start_streaming : share_relay_info_host_running;
    if (roomId != viewerRoomId)
        return share_relay_info_ignore;
    if (viewerJoining)
        return share_relay_info_open_viewer;
    //观看中又收到同一房间的 1726：服务端只在主持人宽限期内重新登录时补发，按重连处理
    if (viewerWatching)
        return share_relay_info_reconnect_viewer;
    return share_relay_info_ignore;
}

std::wstring MakeForcedJoinConfirmText(const std::wstring& title, const std::wstring& desc)
{
    std::wstring text;
    //标题一行、描述一行，再空一行接原来的说明
    if (!title.empty())
    {
        //标题加的引号随语言翻译
        text = TRF(L"「{0}」", title) + L"\n";
        if (!desc.empty())
            text += desc + L"\n";
        text += L"\n";
    }
    return text + TR(kForcedJoinBody);
}

std::string ShareTextToUtf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    const int wideLen = static_cast<int>(text.size());
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), wideLen, NULL, 0, NULL, NULL);
    if (bytes <= 0)
        return std::string();
    std::string out(static_cast<size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), wideLen, &out[0], bytes, NULL, NULL);
    return out;
}

int ShareToastTextLines(bool forced, bool hasDesc)
{
    //普通一行；强制模式的说明较长，占三行；描述另起一行，最多两行
    const int kPlainLines = 1;
    const int kForcedLines = 3;
    const int kDescLines = 2;
    return (forced ? kForcedLines : kPlainLines) + (hasDesc ? kDescLines : 0);
}

void ShareRoomInfoCache::Remember(const std::string& roomId, const std::wstring& title, const std::wstring& desc)
{
    //房间号为空、或老服务端不带标题：不改已记下的
    if (roomId.empty() || title.empty())
        return;
    m_rooms[roomId] = std::make_pair(title, desc);
}

bool ShareRoomInfoCache::Find(const std::string& roomId, std::wstring& title, std::wstring& desc) const
{
    std::map<std::string, std::pair<std::wstring, std::wstring> >::const_iterator it = m_rooms.find(roomId);
    if (it == m_rooms.end())
        return false;
    title = it->second.first;
    desc = it->second.second;
    return true;
}

void ShareRoomInfoCache::Forget(const std::string& roomId)
{
    m_rooms.erase(roomId);
}

void ShareRoomInfoCache::Clear()
{
    m_rooms.clear();
}

std::wstring MakeCloudRecordErrorText(int code, int action)
{
    switch (code)
    {
    //成功
    case 0:
        return std::wstring();
    //企业没有开通
    case error_code_cloudrecord_disabled:
        return TR(L"本企业没有开通云端录制，请联系管理员。");
    //开始时已在录；暂停、继续、停止时已不在录
    case error_code_cloudrecord_busy:
        return action == cloudrecord_control_start ? TR(L"这场共享已经在云端录制。")
                                                   : TR(L"这场共享目前没有在云端录制。");
    //chatserver 与 imgserver 之间的连接断开，或 imgserver 拒绝
    case error_code_cloudrecord_unavailable:
        return TR(L"云端存储服务暂时不可用，请稍后再试。");
    //配额已满
    case error_code_storage_quota:
        return TR(L"本企业的云端录像空间已满，请删除不需要的云端录像，或联系管理员调整配额。");
    //不是主持人（主持人刚移交给别人）
    case error_code_screenshare_not_host:
        return TR(L"只有主持人可以操作云端录制。");
    //其它错误
    default:
        return TRF(L"云端录制操作失败（错误码 {0}）。", code);
    }
}

std::wstring MakeCloudRecordStoppedText(int code)
{
    if (code == 0)
        return std::wstring();
    return TR(L"云端录制已停止：云端存储服务出现问题（例如磁盘空间不足），已录制的部分会保留。");
}

ShareBoardCloseItem::ShareBoardCloseItem()
    : m_ownerId(0)
{
}

void BuildShareBoardCloseItems(const std::vector<ShareWhiteboardInfo>& boards, int64_t selfId,
                               const std::map<int64_t, std::wstring>& names, std::vector<ShareBoardCloseItem>& out)
{
    out.clear();
    for (size_t i = 0; i < boards.size(); ++i)
    {
        const int64_t owner = boards[i].m_ownerId;
        //本人的白板用「关闭我的白板」；没有建立者的不列
        if (owner == selfId || owner == 0)
            continue;
        std::map<int64_t, std::wstring>::const_iterator it = names.find(owner);
        const std::wstring name = (it != names.end() && !it->second.empty()) ? it->second : std::to_wstring(owner);
        ShareBoardCloseItem item;
        item.m_ownerId = owner;
        item.m_text = TRF(L"{0} 的白板", name);
        out.push_back(item);
    }
}

bool ShareViewerListCanGrantControl(bool roomHost, bool hostStreaming, bool hostScreenStopped, bool publishStreaming)
{
    //交出去的是本人正在推的屏幕：原主持人推流中且没停掉自己的画面，或经共享者管线推流中
    return roomHost && ((hostStreaming && !hostScreenStopped) || publishStreaming);
}

std::wstring MakeShareControlStartNotice(const std::wstring& controllerName)
{
    return TRF(L"{0} 正在控制你的屏幕，你仍可以操作；要收回控制，请点屏幕顶部工具条上的「收回控制」", controllerName);
}

std::wstring MakeShareControlledByText(const std::wstring& controllerName)
{
    return TRF(L"{0} 正在控制你的屏幕", controllerName);
}

std::wstring MakeShareTileName(const std::wstring& ownerName, bool isWhiteboard, bool isSelf)
{
    if (isWhiteboard)
        return isSelf ? TRF(L"{0} 的白板（我）", ownerName) : TRF(L"{0} 的白板", ownerName);
    return isSelf ? TRF(L"{0}（我）", ownerName) : ownerName;
}

bool ShouldExcludeShareView(bool sharingOwnScreen, bool captureAllowed)
{
    return sharingOwnScreen && !captureAllowed;
}

int DecideShareControlTarget(bool activeIsBoard, int64_t ownerId, int64_t selfId)
{
    if (activeIsBoard)
        return share_control_target_board;
    if (ownerId <= 0)
        return share_control_target_none;
    if (ownerId == selfId)
        return share_control_target_self;
    return share_control_target_ok;
}

} // namespace screenshare
