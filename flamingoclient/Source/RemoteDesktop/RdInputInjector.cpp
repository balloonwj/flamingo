/**
 *  远程桌面输入注入（被控端）实现，RdInputInjector.cpp
 *  balloonwj@qq.com   2026-06-08
 */
#include "RdInputInjector.h"

namespace remotedesktop {

//小端读取一个 32 位无符号整数
static uint32_t readU32(const BYTE* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool RdInputInjector::parse(const BYTE* data, size_t len, RdInputCommand& out)
{
    if (data == NULL || len < 1)
        return false;

    BYTE type = data[0];
    if (type == rd_input_mouse)
    {
        if (len < (size_t)rd_input_mouse_len)
            return false;
        out.type    = rd_input_mouse;
        out.message = readU32(data + 1);
        out.x       = (int32_t)readU32(data + 5);
        out.y       = (int32_t)readU32(data + 9);
        out.wheel   = (int32_t)readU32(data + 13);
        return true;
    }
    if (type == rd_input_key)
    {
        if (len < (size_t)rd_input_key_len)
            return false;
        out.type    = rd_input_key;
        out.message = readU32(data + 1);
        out.vkey    = readU32(data + 5);
        return true;
    }
    return false;
}

bool RdInputInjector::inject(const BYTE* data, size_t len)
{
    RdInputCommand cmd;
    if (!parse(data, len, cmd))
        return false;

    if (cmd.type == rd_input_mouse)
        injectMouse(cmd.message, cmd.x, cmd.y, cmd.wheel);
    else if (cmd.type == rd_input_key)
        injectKey(cmd.message, cmd.vkey);
    return true;
}

void RdInputInjector::injectMouse(uint32_t message, int32_t x, int32_t y, int32_t wheel)
{
    switch (message)
    {
    case WM_MOUSEMOVE:
        ::SetCursorPos(x, y);
        break;
    case WM_LBUTTONDOWN:
        ::SetCursorPos(x, y);
        ::mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        break;
    case WM_LBUTTONUP:
        ::SetCursorPos(x, y);
        ::mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        break;
    case WM_RBUTTONDOWN:
        ::SetCursorPos(x, y);
        ::mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
        break;
    case WM_RBUTTONUP:
        ::SetCursorPos(x, y);
        ::mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
        break;
    case WM_MBUTTONDOWN:
        ::SetCursorPos(x, y);
        ::mouse_event(MOUSEEVENTF_MIDDLEDOWN, 0, 0, 0, 0);
        break;
    case WM_MBUTTONUP:
        ::SetCursorPos(x, y);
        ::mouse_event(MOUSEEVENTF_MIDDLEUP, 0, 0, 0, 0);
        break;
    case WM_LBUTTONDBLCLK:
        ::SetCursorPos(x, y);
        ::mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        ::mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        break;
    case WM_RBUTTONDBLCLK:
        ::SetCursorPos(x, y);
        ::mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
        ::mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
        break;
    case WM_MOUSEWHEEL:
        ::mouse_event(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)wheel, 0);
        break;
    default:
        break;
    }
}

void RdInputInjector::injectKey(uint32_t message, uint32_t vkey)
{
    switch (message)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        ::keybd_event((BYTE)vkey, (BYTE)::MapVirtualKey(vkey, 0), 0, 0);
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        ::keybd_event((BYTE)vkey, (BYTE)::MapVirtualKey(vkey, 0), KEYEVENTF_KEYUP, 0);
        break;
    default:
        break;
    }
}

} // namespace remotedesktop
