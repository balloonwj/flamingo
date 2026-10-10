/**
 *  远程桌面抓屏公共类型，RdCaptureTypes.h
 *
 *  抓屏抽象层（IScreenCapturer，后续增量引入）与帧编码器（CScreenFrameEncoder）
 *  共用的轻量类型。一帧像素视图 FrameView 指向抓取器内部缓冲（零拷贝），抓取器
 *  （DXGI / GDI）统一产出 BGRA32 top-down，编码器据此转成现有协议引擎帧。
 *
 *  balloonwj@qq.com   2026-06-11
 */
#pragma once

#include <windows.h>

namespace remotedesktop {

//一帧像素视图：指向抓取器内部缓冲，仅在下次抓帧前有效（零拷贝，不拥有内存）。
//像素格式固定 BGRA32（每像素 4 字节，顺序 B/G/R/A），top-down（第 0 行是屏幕顶行）。
struct FrameView
{
    const BYTE* m_data;    //像素起始地址（BGRA32，top-down）；NULL 表示无效帧
    int         m_width;   //像素宽
    int         m_height;  //像素高
    int         m_stride;  //行字节数（可能 > m_width*4，含对齐 padding，逐行须按它走）

    FrameView()
        : m_data(NULL)
        , m_width(0)
        , m_height(0)
        , m_stride(0)
    {
    }
};

} // namespace remotedesktop
