/**
 *  屏幕共享画面的硬件 H.264 编码器（docs/屏幕共享与录制方案.md 第 9 期 9c），ShareHwH264Encoder.h
 *
 *  用 MFTEnumEx 找系统里的硬件 H.264 编码器（NVIDIA NVENC、Intel Quick Sync、AMD 等），按系统给的优先顺序逐个尝试，
 *  第一个能按要求的尺寸、帧率、码率建起来的即被采用。硬件编码器都是异步 MFT：解锁异步模式后，按它发出的
 *  METransformNeedInput / METransformHaveOutput 事件喂入画面、取出码流。本类把这套事件流程包装成与
 *  call::MfH264Encoder 相同的同步接口：EncodeNv12 喂入一帧，并在返回前把已经编好的码流交给回调。
 *  等待事件有超时（硬件编码器卡住时 EncodeNv12 返回 false），调用方据此改用软件编码器。
 *
 *  线程：所有方法都在同一线程（推流线程）调用；该线程须已初始化 COM（多线程套间）与 Media Foundation。
 *
 *  典型用法：
 *      ShareHwH264Encoder hw;
 *      if (hw.Init(1920, 1080, 15, 2000000, 600))
 *      {
 *          if (!hw.EncodeNv12(nv12, 1920, ptsHns, sink))   // 失败：改用软件编码器
 *              ...
 *      }
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <memory>
#include <string>

#include "../Call/MfH264Codec.h"   // call::MfH264EncoderSink

namespace screenshare {

//屏幕共享的硬件 H.264 编码器（异步 MFT 的同步包装）
class ShareHwH264Encoder
{
public:
    ShareHwH264Encoder();
    ~ShareHwH264Encoder();

    ShareHwH264Encoder(const ShareHwH264Encoder& rhs) = delete;
    ShareHwH264Encoder& operator=(const ShareHwH264Encoder& rhs) = delete;

    //系统里登记的、能把 NV12 编成 H.264 的硬件编码器个数（0 表示没有，只能用软件编码器）
    static int CountHardwareEncoders();

    /**
     *  按系统给的优先顺序逐个尝试硬件编码器，建起第一个可用的。已建好时先关闭再重建。
     *  @param width / height 编码尺寸（像素，正偶数）
     *  @param fps            帧率
     *  @param bitrateBps     目标码率（bit/s）
     *  @param gopFrames      关键帧间隔（帧）；0 表示用编码器的默认值
     *  @return 建起来了返回 true；没有硬件编码器或全部失败返回 false
     */
    bool Init(int width, int height, int fps, int bitrateBps, int gopFrames);

    //关闭编码器（析构时自动调用）；可重复调用
    void Close();

    //编码器是否已建好
    bool IsReady() const;

    //所用硬件编码器的名称（日志用，如「NVIDIA H.264 Encoder MFT」）；没建好时为空
    const std::wstring& Name() const;

    //要求下一帧编成关键帧；编码器接受了请求返回 true
    bool ForceKeyFrame();

    /**
     *  运行中修改目标码率：按新码率重建编码器，重建后的第一帧是关键帧。
     *  不用 CODECAPI_AVEncCommonMeanBitRate 运行中修改，是因为 Intel Quick Sync 接受了设置却不生效（实测改到
     *  200 kbps 后仍按约 2 Mbps 输出，加关键帧也一样；初始化时给 200 kbps 则输出约 180 kbps，第 9 期 9c）。
     *  @param bitrateBps 新的目标码率（bit/s），须大于 0
     *  @return 重建成功返回 true；失败时编码器处于未就绪状态（调用方应改用软件编码器）
     */
    bool SetBitrate(int bitrateBps);

    /**
     *  编码一帧，并把此刻已经编好的码流交给回调（硬件编码器通常在喂入后几毫秒内产出，本函数会等一小段时间）。
     *  @param nv12    NV12 画面：Y 平面后紧跟 UV 平面
     *  @param stride  行跨度（字节），不小于编码宽度
     *  @param ptsHns  时间戳（100 纳秒）
     *  @param sink    码流回调，在本函数返回之前、在本线程上调用；不可为 NULL
     *  @return 喂入成功返回 true；编码器出错或等事件超时返回 false（调用方应改用软件编码器）
     */
    bool EncodeNv12(const uint8_t* nv12, int stride, int64_t ptsHns, call::MfH264EncoderSink* sink);

    //Media Foundation 对象与状态，定义在 .cpp 里；声明为公开只是为了让 .cpp 里的辅助函数能使用，外部不应使用
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;   //Media Foundation 对象与状态
};

} // namespace screenshare
