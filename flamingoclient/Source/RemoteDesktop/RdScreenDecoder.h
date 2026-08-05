/**
 *  远程桌面解码绘制（控制端），RdScreenDecoder.h
 *
 *  接收对端发来的引擎帧字节（rd_frame_bitmapinfo / first / next），在本地维护
 *  一份"当前整屏 DIB"缓冲，并能把它绘制到任意 HDC（缩放到目标矩形）。
 *
 *  设计要点：解码（apply）只做纯内存 buffer 操作，<u>不碰 GDI</u>，便于单元
 *  测试（见 Tests/RdScreenDecoderTests.cpp）；只有绘制（drawTo）走 GDI 的
 *  StretchDIBits。增量帧的差异块按 [偏移 + 长度 + 数据] 直接贴回整屏缓冲，
 *  是被控端 CScreenSpy 差异编码的逆操作（旧 RemoteDesktopDlg 内联汇编版的
 *  C++ 重写）。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <windows.h>
#include <vector>
#include <stdint.h>

namespace remotedesktop {

class RdScreenDecoder
{
public:
    RdScreenDecoder();

    /**
     *  喂入一个引擎帧（首字节为 rd_frame_type）。
     *  @param data 引擎帧字节起始地址
     *  @param len  引擎帧字节数
     *  @return 画面内容可能已变化、需要重绘时返回 true（first / next 帧）；
     *          BITMAPINFO 帧或畸形输入返回 false
     */
    bool apply(const BYTE* data, size_t len);

    /**
     *  把当前整屏画面绘制到 hdc 的 dest 矩形（按需缩放）。未就绪时不绘制。
     *  @param hdc  目标设备上下文
     *  @param dest 目标矩形（窗口客户区坐标）
     */
    void drawTo(HDC hdc, const RECT& dest) const;

    //是否已收到 BITMAPINFO 与首屏、可以绘制
    bool ready() const;
    //当前画面宽 / 高（像素）
    int width() const;
    int height() const;
    //对端最近一次上报的光标位置（被控端屏幕坐标）与光标类型索引
    POINT cursorPos() const;
    BYTE cursorType() const;
    //当前整屏 DIB 缓冲（供单测 / 调试只读访问）
    const std::vector<BYTE>& screen() const;

private:
    void applyBitmapInfo(const BYTE* p, size_t n);
    void applyFirst(const BYTE* p, size_t n);
    void applyNext(const BYTE* p, size_t n);

    std::vector<BYTE> m_bmi;          //BITMAPINFO 字节（含调色板）
    std::vector<BYTE> m_screen;       //当前整屏 DIB 缓冲
    int               m_width;        //画面宽（像素）
    int               m_height;       //画面高（像素）
    int               m_bitCount;     //位深
    long              m_biSizeImage;  //整屏 DIB 字节数
    POINT             m_cursorPos;    //对端光标位置（被控端屏幕坐标）
    BYTE              m_cursorType;   //对端光标类型索引
    bool              m_hasBmi;       //是否已收到 BITMAPINFO
    bool              m_hasFirst;     //是否已收到首屏
};

} // namespace remotedesktop
