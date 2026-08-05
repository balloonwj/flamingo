/**
 *  远程桌面输入注入（被控端），RdInputInjector.h
 *
 *  把控制端发来的输入命令字节解析成 RdInputCommand，并注入到本机系统
 *  （鼠标用 SetCursorPos + mouse_event，键盘用 keybd_event；逻辑取自旧
 *  CScreenManager::ProcessCommand，按精简命令格式重写）。
 *
 *  解析（parse）是纯逻辑、可单测；注入（inject）是系统调用，靠端到端目视
 *  验证。全部为静态方法。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <windows.h>
#include <stddef.h>
#include "RdInputDef.h"

namespace remotedesktop {

class RdInputInjector
{
public:
    /**
     *  解析一条输入命令字节（不注入）。
     *  @param data 命令字节起始
     *  @param len  命令字节数
     *  @param out  [出参] 解析结果
     *  @return 类型合法且长度足够返回 true，否则 false
     */
    static bool parse(const BYTE* data, size_t len, RdInputCommand& out);

    /**
     *  解析并把一条输入命令注入到本机系统（鼠标移动 / 点击 / 滚轮、键盘按键）。
     *  @param data 命令字节起始
     *  @param len  命令字节数
     *  @return 解析成功并执行注入返回 true；解析失败返回 false
     */
    static bool inject(const BYTE* data, size_t len);

private:
    //把鼠标命令注入系统
    static void injectMouse(uint32_t message, int32_t x, int32_t y, int32_t wheel);
    //把键盘命令注入系统
    static void injectKey(uint32_t message, uint32_t vkey);

    RdInputInjector() = delete;
    ~RdInputInjector() = delete;
    RdInputInjector(const RdInputInjector&) = delete;
    RdInputInjector& operator=(const RdInputInjector&) = delete;
};

} // namespace remotedesktop
