/**
 *  录制画面的合成，RecordFrameComposer.h
 *
 *  录制的每一帧由三部分合成（docs/屏幕共享与录制方案.md 6.1）：画面来源（采集到的或解码出的画面）等比缩放进输出尺寸、
 *  四周留黑边；共享录制时在画面上叠标注（与观看端用同一套绘制）；需要时在右下角叠摄像头画中画（带一圈白边）。
 *  合成在一块 32 位 DIB 上完成，结果是自上而下的 BGRA，直接交给 RecordMp4Writer。画面来源原尺寸时逐行复制、缩小时用
 *  BgraDownscaler 直接写进画布，只有放大才用 GDI 的 HALFTONE 缩放（2026-10-15，docs/远程桌面H264与共享画质提升方案.md A7）；
 *  标注与画中画仍用 GDI 绘制。
 *
 *  线程：只在录制的写入线程上使用。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <vector>
#include <windows.h>

#include "../Share/ShareAnnotation.h"
#include "../Share/ShareFrameDrawer.h"   // BgraDownscaler：缩小进画布时代替 HALFTONE（2026-10-15）
#include "RecordPolicy.h"   // RecordPipPlacement

namespace screenrecord {

//录制画面的合成器
class RecordFrameComposer
{
public:
    RecordFrameComposer();
    ~RecordFrameComposer();

    RecordFrameComposer(const RecordFrameComposer&) = delete;
    RecordFrameComposer& operator=(const RecordFrameComposer&) = delete;

    //建输出画布（宽高须为正）；重复调用会重建
    bool Init(int width, int height);

    /**
     *  合成一帧。
     *  @param src       画面来源：自上而下的 BGRA；为 NULL 时整帧黑色
     *  @param srcW、srcH、srcStride 来源尺寸与行跨度
     *  @param strokes   要叠的标注；为 NULL 或空时不画
     *  @param pip       画中画：自上而下的 BGRA（行跨度 = pipW × 4）；为 NULL 时不画
     *  @param pipW、pipH 画中画尺寸
     */
    void Compose(const uint8_t* src, int srcW, int srcH, int srcStride,
                 const std::vector<screenshare::AnnotationStroke>* strokes, const uint8_t* pip, int pipW, int pipH);

    //合成结果：自上而下的 BGRA
    const uint8_t* Pixels() const;
    int Stride() const;
    int Width() const;
    int Height() const;

    //最近一次合成时画面来源实际所占的矩形（标注按它换算坐标）
    const RECT& ContentRect() const;

    /**
     *  画中画的摆放（2026-10-05）：之后的 Compose 按它摆画中画；默认（m_custom 为 false）摆在右下角。
     *  @param placement 摆放，见 RecordPolicy.h 的 RecordPipPlacement
     */
    void SetPipPlacement(const RecordPipPlacement& placement);

private:
    void release();

    HDC      m_dc;        //画布的内存设备上下文
    HBITMAP  m_bitmap;    //画布 DIB
    HGDIOBJ  m_oldBmp;    //选入画布前的位图
    uint8_t* m_bits;      //画布像素（DIB 内存，随 m_bitmap 释放）
    int      m_width;     //画布宽
    int      m_height;    //画布高
    RECT     m_content;   //画面来源所占的矩形
    RecordPipPlacement m_pipPlacement;   //画中画的摆放（2026-10-05）；默认右下角
    screenshare::BgraDownscaler m_scaler;   //画面缩小进画布时用的快速缩放（2026-10-15），列映射表与中间缓冲按尺寸复用
};

} // namespace screenrecord
