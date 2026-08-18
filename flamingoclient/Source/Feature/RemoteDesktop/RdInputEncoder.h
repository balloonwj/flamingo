/**
 *  远程桌面输入编码（控制端），RdInputEncoder.h
 *
 *  把控制端本地的鼠标 / 键盘操作编码成输入命令字节（格式见 RdInputDef.h），
 *  并提供"观看窗客户区坐标 → 被控端屏幕坐标"的等比映射。全部为纯逻辑静态
 *  方法，便于单元测试（见 Tests/RdInputTests.cpp）。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <windows.h>
#include <vector>
#include <stdint.h>

namespace remotedesktop {

class RdInputEncoder
{
public:
    /**
     *  编码一条鼠标命令到 out（覆盖写入，定长 17 字节）。
     *  @param out     输出缓冲
     *  @param message Win32 鼠标消息（WM_MOUSEMOVE / WM_LBUTTONDOWN / WM_MOUSEWHEEL 等）
     *  @param x       被控端屏幕坐标 x
     *  @param y       被控端屏幕坐标 y
     *  @param wheel   滚轮增量（仅 WM_MOUSEWHEEL 用，其余传 0）
     */
    static void encodeMouse(std::vector<BYTE>& out, uint32_t message,
                            int32_t x, int32_t y, int32_t wheel);

    /**
     *  编码一条键盘命令到 out（覆盖写入，定长 9 字节）。
     *  @param out     输出缓冲
     *  @param message Win32 键盘消息（WM_KEYDOWN / WM_KEYUP / WM_SYSKEYDOWN / WM_SYSKEYUP）
     *  @param vkey    虚拟键码
     */
    static void encodeKey(std::vector<BYTE>& out, uint32_t message, uint32_t vkey);

    /**
     *  把控制端观看窗的客户区坐标等比映射到被控端屏幕坐标，并夹取到屏幕范围内。
     *  @param clientX  客户区 x
     *  @param clientY  客户区 y
     *  @param clientW  客户区宽（>0）
     *  @param clientH  客户区高（>0）
     *  @param remoteW  被控端屏幕宽（>0）
     *  @param remoteH  被控端屏幕高（>0）
     *  @param remoteX  [出参] 映射后的被控端屏幕 x，夹取到 [0, remoteW-1]
     *  @param remoteY  [出参] 映射后的被控端屏幕 y，夹取到 [0, remoteH-1]
     */
    static void mapClientToRemote(int clientX, int clientY, int clientW, int clientH,
                                  int remoteW, int remoteH, int& remoteX, int& remoteY);

private:
    RdInputEncoder() = delete;
    ~RdInputEncoder() = delete;
    RdInputEncoder(const RdInputEncoder&) = delete;
    RdInputEncoder& operator=(const RdInputEncoder&) = delete;
};

} // namespace remotedesktop
