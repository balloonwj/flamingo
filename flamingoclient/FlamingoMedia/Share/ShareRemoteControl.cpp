/**
 *  屏幕共享中控制键鼠的纯逻辑实现，ShareRemoteControl.cpp
 *  格式与约定见 ShareRemoteControl.h。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#include "ShareRemoteControl.h"

#include <algorithm>
#include <stdlib.h>                              // abs
#include "ShareAnnotation.h"                     // kAnnotCoordMax
#include "../RemoteDesktop/RdInputDef.h"         // rd_input_*、命令字节长度
#include "../RemoteDesktop/RdInputInjector.h"    // RdInputInjector::parse
#include "../../Source/net/Protocol.h"           // screenshare_track_control、screenshare_control_input

namespace screenshare {

namespace {

//载荷里各字段的偏移：控制类型、目标座位、输入命令
const size_t kInputTypeOffset = 0;
const size_t kInputTargetOffset = 1;
const size_t kInputCommandOffset = 2;
//座位号的合法上限（255 是服务端专用）
const int kMaxMemberSeat = 254;
//远程桌面命令里 32 位字段的字节数与每字节的位数（小端，见 RdInputDef.h）
const int kU32Bytes = 4;
const int kBitsPerByte = 8;
const uint32_t kByteMask = 0xFF;

//小端写一个 32 位整数
void writeLe32(std::string& out, uint32_t v)
{
    for (int i = 0; i < kU32Bytes; ++i)
        out.push_back(static_cast<char>((v >> (i * kBitsPerByte)) & kByteMask));
}

//把整数夹到 [lo, hi]
int clampInt(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

//组远程桌面输入命令的字节；x、y 原样写入（输入控制帧里是归一化坐标，交给注入器前是屏幕坐标）
std::string encodeRdCommand(const ShareInputEvent& ev, int x, int y)
{
    std::string out;
    if (ev.m_type == remotedesktop::rd_input_mouse)
    {
        out.push_back(static_cast<char>(remotedesktop::rd_input_mouse));
        writeLe32(out, ev.m_message);
        writeLe32(out, static_cast<uint32_t>(x));
        writeLe32(out, static_cast<uint32_t>(y));
        writeLe32(out, static_cast<uint32_t>(ev.m_wheel));
    }
    else if (ev.m_type == remotedesktop::rd_input_key)
    {
        out.push_back(static_cast<char>(remotedesktop::rd_input_key));
        writeLe32(out, ev.m_message);
        writeLe32(out, ev.m_vkey);
    }
    return out;
}

//是否修饰键（左右两侧的虚拟键码也算）
bool isCtrlKey(uint32_t vkey)
{
    return vkey == VK_CONTROL || vkey == VK_LCONTROL || vkey == VK_RCONTROL;
}

bool isAltKey(uint32_t vkey)
{
    return vkey == VK_MENU || vkey == VK_LMENU || vkey == VK_RMENU;
}

bool isWinKey(uint32_t vkey)
{
    return vkey == VK_LWIN || vkey == VK_RWIN;
}

//是否按下类消息
bool isKeyDown(uint32_t message)
{
    return message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
}

//是否抬起类消息
bool isKeyUp(uint32_t message)
{
    return message == WM_KEYUP || message == WM_SYSKEYUP;
}

} // namespace

ShareInputEvent::ShareInputEvent()
    : m_type(0)
    , m_message(0)
    , m_x(0)
    , m_y(0)
    , m_wheel(0)
    , m_vkey(0)
{
}

ShareInputEvent ShareInputEvent::Mouse(uint32_t message, int x, int y, int wheel)
{
    ShareInputEvent ev;
    ev.m_type = remotedesktop::rd_input_mouse;
    ev.m_message = message;
    ev.m_x = x;
    ev.m_y = y;
    ev.m_wheel = wheel;
    return ev;
}

ShareInputEvent ShareInputEvent::Key(uint32_t message, uint32_t vkey)
{
    ShareInputEvent ev;
    ev.m_type = remotedesktop::rd_input_key;
    ev.m_message = message;
    ev.m_vkey = vkey;
    return ev;
}

std::string ShareRemoteInput::BuildInputBody(int senderSeat, int targetSeat, const ShareInputEvent& ev)
{
    if (targetSeat < 0 || targetSeat > kMaxMemberSeat)
        return std::string();
    const std::string command = encodeRdCommand(ev, clampInt(ev.m_x, 0, kAnnotCoordMax),
                                                clampInt(ev.m_y, 0, kAnnotCoordMax));
    if (command.empty())
        return std::string();
    std::string payload;
    payload.push_back(static_cast<char>(screenshare_control_input));
    payload.push_back(static_cast<char>(targetSeat));
    payload += command;

    ShareFrameHeader header;
    header.m_track = screenshare_track_control;
    header.m_senderSeat = senderSeat;
    header.m_flags = screenshare_flag_first_frag | screenshare_flag_last_frag;
    //时间戳为 0：控制帧不参与音画同步
    header.m_timestamp = 0;
    return ShareFraming::encodeBody(header, payload.data(), payload.size());
}

bool ShareRemoteInput::ParseInputFrame(const ShareMediaFrame& frame, int& targetSeat, ShareInputEvent& out)
{
    if (frame.m_track != screenshare_track_control || frame.m_data.size() <= kInputCommandOffset)
        return false;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(frame.m_data.data());
    if (p[kInputTypeOffset] != screenshare_control_input)
        return false;
    remotedesktop::RdInputCommand cmd;
    if (!remotedesktop::RdInputInjector::parse(p + kInputCommandOffset, frame.m_data.size() - kInputCommandOffset, cmd))
        return false;
    //命令长度须恰好相符：多出的字节说明格式不对
    const size_t expected = (cmd.type == remotedesktop::rd_input_mouse)
        ? static_cast<size_t>(remotedesktop::rd_input_mouse_len)
        : static_cast<size_t>(remotedesktop::rd_input_key_len);
    if (frame.m_data.size() - kInputCommandOffset != expected)
        return false;

    targetSeat = p[kInputTargetOffset];
    out = ShareInputEvent();
    out.m_type = cmd.type;
    out.m_message = cmd.message;
    if (cmd.type == remotedesktop::rd_input_mouse)
    {
        out.m_x = clampInt(cmd.x, 0, kAnnotCoordMax);
        out.m_y = clampInt(cmd.y, 0, kAnnotCoordMax);
        out.m_wheel = cmd.wheel;
    }
    else
    {
        out.m_vkey = cmd.vkey;
    }
    return true;
}

bool ShareRemoteInput::ViewToNormalized(const RECT& video, POINT pt, int& nx, int& ny)
{
    const int w = video.right - video.left;
    const int h = video.bottom - video.top;
    if (w <= 0 || h <= 0)
        return false;
    if (pt.x < video.left || pt.x >= video.right || pt.y < video.top || pt.y >= video.bottom)
        return false;
    //用 64 位乘法，避免大分辨率乘上 65535 溢出
    nx = static_cast<int>(static_cast<int64_t>(pt.x - video.left) * kAnnotCoordMax / w);
    ny = static_cast<int>(static_cast<int64_t>(pt.y - video.top) * kAnnotCoordMax / h);
    return true;
}

POINT ShareRemoteInput::NormalizedToSource(const RECT& source, int nx, int ny)
{
    const int x = clampInt(nx, 0, kAnnotCoordMax);
    const int y = clampInt(ny, 0, kAnnotCoordMax);
    const int w = source.right - source.left;
    const int h = source.bottom - source.top;
    POINT pt;
    pt.x = source.left;
    pt.y = source.top;
    if (w > 0)
        pt.x += clampInt(static_cast<int>((static_cast<int64_t>(x) * w + kAnnotCoordMax / 2) / kAnnotCoordMax), 0, w - 1);
    if (h > 0)
        pt.y += clampInt(static_cast<int>((static_cast<int64_t>(y) * h + kAnnotCoordMax / 2) / kAnnotCoordMax), 0, h - 1);
    return pt;
}

std::string ShareRemoteInput::ToRdCommand(const ShareInputEvent& ev, POINT screenPt)
{
    return encodeRdCommand(ev, screenPt.x, screenPt.y);
}

const uint32_t ShareViewerInputTranslator::kShareMoveIntervalMs;

ShareViewerInputTranslator::ShareViewerInputTranslator()
    : m_hasMove(false)
    , m_lastMoveMs(0)
    , m_hasPendingMove(false)
{
    m_lastMovePt.x = 0;
    m_lastMovePt.y = 0;
    m_pendingMovePt.x = 0;
    m_pendingMovePt.y = 0;
}

bool ShareViewerInputTranslator::Translate(UINT msg, WPARAM wParam, POINT clientPt, const RECT& video,
                                           uint32_t nowMs, ShareInputEvent& out)
{
    switch (msg)
    {
    //键盘的按下与抬起（含系统键）：原样发送
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        out = ShareInputEvent::Key(msg, static_cast<uint32_t>(wParam));
        return true;

    //鼠标的移动、滚轮、按下（含双击）与松开：要不要发由与远程桌面共用的按钮记录决定（2026-10-14 起）
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        return translateMouse(msg, wParam, clientPt, video, nowMs, out);

    //其余消息（字符、绘制等）：不发送
    default:
        return false;
    }
}

bool ShareViewerInputTranslator::translateMouse(UINT msg, WPARAM wParam, POINT clientPt, const RECT& video,
                                                uint32_t nowMs, ShareInputEvent& out)
{
    if (video.right <= video.left || video.bottom <= video.top)
        return false;
    //画面外按下不发、按住时拖出画面照发、记过的按钮在哪里松开都发、滚轮只在画面内发（规则见 ViewerMouseButtons.h）
    const bool inside = (::PtInRect(&video, clientPt) != FALSE);
    if (!m_buttons.Decide(msg, inside))
        return false;
    //画面外的消息坐标夹到画面边缘，再换成归一化坐标
    POINT pt;
    pt.x = clampInt(clientPt.x, video.left, video.right - 1);
    pt.y = clampInt(clientPt.y, video.top, video.bottom - 1);
    int nx = 0;
    int ny = 0;
    ShareRemoteInput::ViewToNormalized(video, pt, nx, ny);

    //鼠标移动：位置变了、距上次发送超过间隔才发
    if (msg == WM_MOUSEMOVE)
    {
        //回到上次发出的位置：不发，被挡下的移动也作废（对方的指针本来就停在这里）
        if (m_hasMove && clientPt.x == m_lastMovePt.x && clientPt.y == m_lastMovePt.y)
        {
            m_hasPendingMove = false;
            return false;
        }
        const ShareInputEvent move = ShareInputEvent::Mouse(WM_MOUSEMOVE, nx, ny, 0);
        //不足间隔：记为待补发，到间隔后由 TakePendingMove 取出（2026-10-17 之前直接丢弃，鼠标停下时对方的指针
        //停在离最终位置差一点的地方，要等下一次移动或点击才对上）
        if (m_hasMove && nowMs - m_lastMoveMs < kShareMoveIntervalMs)
        {
            m_pendingMove = move;
            m_pendingMovePt = clientPt;
            m_hasPendingMove = true;
            return false;
        }
        m_hasMove = true;
        m_lastMovePt = clientPt;
        m_lastMoveMs = nowMs;
        m_hasPendingMove = false;
        out = move;
        return true;
    }
    //滚轮、按下与松开都带着自己的坐标：被挡下的移动作废，不能在它们之后把指针拉回更早的位置
    m_hasPendingMove = false;
    //滚轮：带增量，不限频
    if (msg == WM_MOUSEWHEEL)
    {
        out = ShareInputEvent::Mouse(WM_MOUSEWHEEL, nx, ny, GET_WHEEL_DELTA_WPARAM(wParam));
        return true;
    }
    //按下（双击当作按下，由画面主人那边的系统按点击间隔自己合成双击）
    const int down = ViewerButtonOfDown(msg);
    if (down == kViewerButtonRight)
        out = ShareInputEvent::Mouse(WM_RBUTTONDOWN, nx, ny, 0);
    else if (down == kViewerButtonMiddle)
        out = ShareInputEvent::Mouse(WM_MBUTTONDOWN, nx, ny, 0);
    else if (down == kViewerButtonLeft)
        out = ShareInputEvent::Mouse(WM_LBUTTONDOWN, nx, ny, 0);
    //松开：原样发送
    else
        out = ShareInputEvent::Mouse(msg, nx, ny, 0);
    return true;
}

std::vector<uint32_t> ShareViewerInputTranslator::PendingButtonUps() const
{
    return m_buttons.PendingButtonUps();
}

bool ShareViewerInputTranslator::TakePendingMove(uint32_t nowMs, ShareInputEvent& out)
{
    if (!m_hasPendingMove || nowMs - m_lastMoveMs < kShareMoveIntervalMs)
        return false;
    out = m_pendingMove;
    //视同这一刻发出了移动：限频从这一刻重新计时，补发的位置成为「上次发出的位置」
    m_hasPendingMove = false;
    m_lastMovePt = m_pendingMovePt;
    m_lastMoveMs = nowMs;
    return true;
}

bool ShareViewerInputTranslator::HasPendingMove() const
{
    return m_hasPendingMove;
}

void ShareViewerInputTranslator::Reset()
{
    m_hasMove = false;
    m_lastMoveMs = 0;
    m_hasPendingMove = false;
    m_buttons.Reset();
}

bool ShareViewerInputTranslator::IsControlMessage(UINT msg)
{
    //鼠标消息（含滚轮、双击）与键盘消息（按键、字符、系统键、系统字符）
    return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST);
}

ShareInputKeyFilter::ShareInputKeyFilter()
{
}

bool ShareInputKeyFilter::Allow(uint32_t message, uint32_t vkey)
{
    if (isKeyUp(message))
    {
        //按下时被拦下的键：抬起同样不注入
        std::vector<uint32_t>::iterator blocked = std::find(m_blocked.begin(), m_blocked.end(), vkey);
        if (blocked != m_blocked.end())
        {
            m_blocked.erase(blocked);
            return false;
        }
        //Win 键的抬起一律不注入（哪怕按下时没经过本过滤器）
        if (isWinKey(vkey))
            return false;
        m_held.erase(std::remove(m_held.begin(), m_held.end(), vkey), m_held.end());
        return true;
    }
    if (!isKeyDown(message))
        return false;

    //按下前的修饰键状态：只看经本过滤器注入、尚未抬起的键
    bool ctrl = false;
    bool alt = false;
    for (size_t i = 0; i < m_held.size(); ++i)
    {
        if (isCtrlKey(m_held[i]))
            ctrl = true;
        if (isAltKey(m_held[i]))
            alt = true;
    }
    const bool system = isWinKey(vkey)
        || (alt && (vkey == VK_TAB || vkey == VK_ESCAPE))       // Alt+Tab、Alt+Esc：切换窗口
        || (ctrl && vkey == VK_ESCAPE)                           // Ctrl+Esc、Ctrl+Shift+Esc：开始菜单、任务管理器
        || (ctrl && alt && vkey == VK_DELETE);                   // Ctrl+Alt+Del：安全选项
    if (system)
    {
        if (std::find(m_blocked.begin(), m_blocked.end(), vkey) == m_blocked.end())
            m_blocked.push_back(vkey);
        return false;
    }
    //按住不放时系统会重复发按下：只记一次
    if (!IsHeld(vkey))
        m_held.push_back(vkey);
    return true;
}

std::vector<uint32_t> ShareInputKeyFilter::HeldKeys() const
{
    return m_held;
}

void ShareInputKeyFilter::Reset()
{
    m_held.clear();
    m_blocked.clear();
}

bool ShareInputKeyFilter::IsHeld(uint32_t vkey) const
{
    return std::find(m_held.begin(), m_held.end(), vkey) != m_held.end();
}

int DecideShareInput(const ShareInputEvent& ev, int senderSeat, int controllerSeat, bool windowShare,
                     bool pointOnWindow, bool windowForeground, ShareInputKeyFilter& filter)
{
    if (controllerSeat < 0 || senderSeat != controllerSeat)
        return share_input_not_controller;
    if (ev.m_type == remotedesktop::rd_input_mouse)
    {
        if (windowShare && !pointOnWindow)
            return share_input_outside_window;
        return share_input_inject;
    }
    if (ev.m_type != remotedesktop::rd_input_key)
        return share_input_not_controller;
    if (windowShare && !windowForeground)
        return share_input_not_foreground;
    return filter.Allow(ev.m_message, ev.m_vkey) ? share_input_inject : share_input_system_key;
}

} // namespace screenshare
