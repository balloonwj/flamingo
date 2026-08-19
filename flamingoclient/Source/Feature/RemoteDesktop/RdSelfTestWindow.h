/**
 *  远程桌面引擎自环测试窗口，RdSelfTestWindow.h
 *
 *  调试入口（命令行 --rdpselftest）：开一个纯 Win32 窗口，定时把
 *  本机屏幕走一遍完整引擎链路——RdScreenCapturer 抓屏 + 编码 → RdScreenDecoder
 *  解码 → drawTo 绘制到窗口客户区。用于目视确认"抓屏 + 编码 + 解码 + 绘制"
 *  闭环出画面（窗口里应实时显示本机桌面的缩小镜像）。不接网络、不接聊天窗口，
 *  仅用于开发期验证引擎本身。
 *
 *  刻意不依赖 balloonui / MFC / WTL，自带 RegisterClass + WndProc，保持轻量、
 *  自包含、易移除。正式发布构建（非 _DEBUG）不会创建本窗口。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <windows.h>
#include <vector>

namespace remotedesktop
{
    class RdScreenCapturer;
    class RdScreenDecoder;
}

class CRdSelfTestWindow
{
public:
    CRdSelfTestWindow();
    ~CRdSelfTestWindow();

    CRdSelfTestWindow(const CRdSelfTestWindow& rhs) = delete;
    CRdSelfTestWindow& operator=(const CRdSelfTestWindow& rhs) = delete;

    /**
     *  创建并显示自环测试窗口，启动定时抓屏-解码-绘制循环。
     *  @param hParent 父窗口句柄（仅作所有权提示，可为 NULL）
     */
    void CreateAndShow(HWND hParent);

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    //定时回调：抓下一帧并喂给解码器
    void onTimer();
    //绘制回调：把解码器当前整屏画到客户区（缩放）
    void onPaint(HDC hdc);
    //鼠标左键点击：把点击的客户区坐标映射到被控端（自环=本机）屏幕坐标并注入
    //鼠标移动，目视验证"坐标映射 + 输入注入"闭环（光标应跳到桌面对应位置）
    void onLButtonDown(int clientX, int clientY);

    HWND                          m_hWnd;       //窗口句柄
    remotedesktop::RdScreenCapturer* m_capturer;//抓屏编码器（本类拥有）
    remotedesktop::RdScreenDecoder*  m_decoder; //解码器（本类拥有）
    std::vector<BYTE>             m_buf;        //引擎帧复用缓冲
};
