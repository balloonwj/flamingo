/**
 *  通话远端 / 本端视频帧的显示去向接口，CallVideoDisplaySink.h
 *
 *  把「通话中转会话（CallMediaSession）→ 通话窗 UI」之间的视频帧交付接口单独成头，避免通话窗
 *  为拿这个接口而 include 重量级的 CallMediaSession.h（其含 winsock2.h，会给 UI 侧带来包含
 *  顺序负担）。CallMediaSession 把视频引擎采集 / 解码出的帧经本接口交给实现方（通话窗）显示。
 *
 *  线程：OnRemoteVideoFrame 在会话的视频解码线程上触发，OnLocalVideoFrame 在采集线程上触发；
 *  实现方都须拷出帧后 PostMessage 转投 UI 线程绘制，勿在回调内阻塞。
 *
 *  balloonwj@qq.com   2026-06-16
 */
#pragma once

#include <stdint.h>

namespace call {

//远端 / 本端视频帧显示去向：由通话窗实现并注入到 CallMediaSession。
class CallVideoDisplaySink
{
public:
    virtual ~CallVideoDisplaySink() {}

    /**
     *  解码出一帧远端视频（NV12）。**在会话的视频解码线程上触发**，实现方须自行拷出后转投
     *  UI 线程绘制，勿在本回调内做耗时阻塞操作。
     *  @param nv12   NV12 帧（Y 平面起点；UV 平面位于 nv12 + stride*height）；仅本次回调内有效
     *  @param width  画面宽（像素）
     *  @param height 画面高（像素）
     *  @param stride Y 平面行跨度（字节）
     */
    virtual void OnRemoteVideoFrame(const uint8_t* nv12, int width, int height, int stride) = 0;

    /**
     *  本端摄像头采集到一帧（NV12，编码之前）。**在会话的采集线程上触发**，实现方通常转 BGRA
     *  显示到本地预览小窗（PiP）；同样须拷出后转投 UI 线程，勿阻塞。
     *  @param nv12   NV12 帧（Y 平面起点；UV 平面位于 nv12 + stride*height）；仅本次回调内有效
     *  @param width/height/stride 尺寸与行跨度
     */
    virtual void OnLocalVideoFrame(const uint8_t* nv12, int width, int height, int stride) = 0;
};

} // namespace call
