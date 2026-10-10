/**
 *  远程桌面输入命令定义，RdInputDef.h
 *
 *  控制端把本地鼠标 / 键盘操作编码成"输入命令"字节，经 v2 中转通道发给被控端，
 *  被控端解析后用 SendInput 系列 API 注入到系统。命令格式刻意精简（不沿用笨重
 *  的 Win32 MSG 结构），全部为定长小端字段：
 *
 *    鼠标命令(17 字节)：[type=1][message u32][x i32][y i32][wheel i32]
 *    键盘命令( 9 字节)：[type=2][message u32][vkey u32]
 *
 *  message 取 Win32 WM_*（WM_MOUSEMOVE / WM_LBUTTONDOWN / WM_KEYDOWN 等）；
 *  鼠标 x/y 为<u>被控端屏幕绝对坐标</u>（控制端在观看窗里点的客户区坐标，须先
 *  经 RdInputEncoder::mapClientToRemote 映射成被控端屏幕坐标）；wheel 为滚轮
 *  增量（仅 WM_MOUSEWHEEL 用）；vkey 为虚拟键码。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <stdint.h>

namespace remotedesktop {

//输入命令类型，占命令首字节
enum rd_input_type
{
    rd_input_mouse = 1,   //鼠标命令
    rd_input_key   = 2,   //键盘命令
};

//鼠标命令总长：type(1) + message(4) + x(4) + y(4) + wheel(4)
const int rd_input_mouse_len = 17;
//键盘命令总长：type(1) + message(4) + vkey(4)
const int rd_input_key_len = 9;

//从命令字节解析出的一条输入；按 type 取用对应字段
struct RdInputCommand
{
    int      type;      //命令类型，见 rd_input_type
    uint32_t message;   //Win32 WM_* 消息
    int32_t  x;         //鼠标 x（被控端屏幕坐标），仅鼠标命令有效
    int32_t  y;         //鼠标 y（被控端屏幕坐标），仅鼠标命令有效
    int32_t  wheel;     //滚轮增量，仅 WM_MOUSEWHEEL 有效
    uint32_t vkey;      //虚拟键码，仅键盘命令有效

    RdInputCommand()
        : type(0), message(0), x(0), y(0), wheel(0), vkey(0)
    {
    }
};

} // namespace remotedesktop
