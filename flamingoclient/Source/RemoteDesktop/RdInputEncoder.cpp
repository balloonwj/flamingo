/**
 *  远程桌面输入编码（控制端）实现，RdInputEncoder.cpp
 *  balloonwj@qq.com   2026-06-08
 */
#include "RdInputEncoder.h"
#include "RdInputDef.h"

namespace remotedesktop {

//小端追加一个 32 位无符号整数
static void appendU32(std::vector<BYTE>& v, uint32_t x)
{
    v.push_back((BYTE)(x & 0xFF));
    v.push_back((BYTE)((x >> 8) & 0xFF));
    v.push_back((BYTE)((x >> 16) & 0xFF));
    v.push_back((BYTE)((x >> 24) & 0xFF));
}

void RdInputEncoder::encodeMouse(std::vector<BYTE>& out, uint32_t message,
                                 int32_t x, int32_t y, int32_t wheel)
{
    out.clear();
    out.push_back((BYTE)rd_input_mouse);
    appendU32(out, message);
    appendU32(out, (uint32_t)x);
    appendU32(out, (uint32_t)y);
    appendU32(out, (uint32_t)wheel);
}

void RdInputEncoder::encodeKey(std::vector<BYTE>& out, uint32_t message, uint32_t vkey)
{
    out.clear();
    out.push_back((BYTE)rd_input_key);
    appendU32(out, message);
    appendU32(out, vkey);
}

void RdInputEncoder::mapClientToRemote(int clientX, int clientY, int clientW, int clientH,
                                       int remoteW, int remoteH, int& remoteX, int& remoteY)
{
    //等比映射；除零保护
    if (clientW > 0)
        remoteX = (int)((int64_t)clientX * remoteW / clientW);
    else
        remoteX = 0;
    if (clientH > 0)
        remoteY = (int)((int64_t)clientY * remoteH / clientH);
    else
        remoteY = 0;

    //夹取到被控端屏幕范围内，避免点到窗口边缘外时坐标越界
    if (remoteX < 0)
        remoteX = 0;
    if (remoteX > remoteW - 1)
        remoteX = remoteW - 1;
    if (remoteY < 0)
        remoteY = 0;
    if (remoteY > remoteH - 1)
        remoteY = remoteH - 1;
}

} // namespace remotedesktop
