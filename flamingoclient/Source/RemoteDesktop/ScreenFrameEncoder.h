/**
 *  远程桌面帧编码器（被控端），ScreenFrameEncoder.h
 *
 *  "路径 A" 的核心：把抓取器产出的 BGRA32 帧转成与现有协议完全一致的引擎帧
 *  （rd_frame_bitmapinfo / first / next），让 DXGI / GDI 两种抓取源都复用现有的
 *  16 位 DIB + DIFF 增量格式与控制端解码器（RdScreenDecoder），协议与解码端零改。
 *
 *  内部把 BGRA32（top-down）转成 16 位 RGB555、bottom-up 的 DIB——方向与像素格式
 *  与旧引擎 CScreenSpy 的 BITMAPINFOHEADER 一致（biHeight 正、BI_RGB）；再对相邻两
 *  帧做纯内存差异（DIFF），产出 [偏移 + 长度 + 数据] 差异块，即 RdScreenDecoder 的
 *  逆操作。
 *
 *  不碰 GDI、不依赖真实屏幕，便于单元测试（见 Tests/RdScreenEncoderTests.cpp）。
 *
 *  典型用法：每帧 Feed(bgra) 喂入；会话首帧发 GetBitmapInfo + GetFirstFrame，之后
 *  每帧发 GetNextFrame（自动以"上次发出的帧"为基准做差异）。
 *
 *  balloonwj@qq.com   2026-06-11
 */
#pragma once

#include <windows.h>
#include <vector>
#include "RdCaptureTypes.h"

namespace remotedesktop {

//帧编码器：BGRA32 → 16 位 RGB555 DIB → 现有协议引擎帧（含 DIFF 增量）。
class CScreenFrameEncoder
{
public:
    CScreenFrameEncoder();

    /**
     *  喂入一帧 BGRA32（来自抓取器）。内部转成 16 位 RGB555 bottom-up DIB 作为
     *  "当前帧"。尺寸变化（含首次）时重建缓冲、并令下一次输出视为首屏（基准失效）。
     *  @param bgra BGRA32 像素视图（top-down，逐行按 m_stride 走）；非法（空/0 尺寸）时忽略
     */
    void Feed(const FrameView& bgra);

    //是否已喂入至少一帧（可产出 BITMAPINFO / 首屏）。
    bool HasFrame() const;

    //产出 BITMAPINFO 引擎帧（rd_frame_bitmapinfo + BITMAPINFOHEADER）。无帧时产出空头。
    void GetBitmapInfo(std::vector<BYTE>& out) const;

    //产出首屏引擎帧（rd_frame_first + 整屏 DIB）。同时把"对端基准帧"设为当前帧。
    void GetFirstFrame(std::vector<BYTE>& out);

    /**
     *  产出增量引擎帧（rd_frame_next + 算法标志 + 光标 + DIFF 差异块）。以"对端基准
     *  帧"为基准对当前帧做差异；产出后把基准帧推进为当前帧。无基准（未发首屏）时只产
     *  出帧头、不含差异块。
     *  @param cursorPos  光标位置（被控端屏幕坐标），原样写入帧供对端显示
     *  @param cursorType 光标类型索引，原样写入帧
     */
    void GetNextFrame(std::vector<BYTE>& out, POINT cursorPos, BYTE cursorType);

    //当前画面宽 / 高（像素）。
    int Width() const;
    int Height() const;

private:
    //把 BGRA32（top-down）转成 16 位 RGB555 bottom-up DIB，写入 dst（内部 resize）。
    void convertToDib(const FrameView& bgra, std::vector<BYTE>& dst) const;
    //构造当前尺寸的 BITMAPINFOHEADER（16 位 / BI_RGB / bottom-up，与 CScreenSpy 一致）。
    void buildBitmapInfoHeader(BITMAPINFOHEADER& hdr) const;
    //当前尺寸下 DIB 的行字节数（4 字节对齐）。
    int  dibStride() const;
    //当前尺寸下 DIB 的总字节数。
    int  dibSizeImage() const;

    int               m_width;       //画面宽（像素）
    int               m_height;      //画面高（像素）
    std::vector<BYTE> m_current;     //当前帧 DIB（16 位 RGB555，bottom-up）
    std::vector<BYTE> m_baseline;    //对端基准帧 DIB（GetFirst/GetNext 后推进，DIFF 基准）
    bool              m_hasBaseline; //是否已有基准帧（已发过首屏）
};

} // namespace remotedesktop
