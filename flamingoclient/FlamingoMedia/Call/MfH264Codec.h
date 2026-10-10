/**
 *  H.264 视频编解码（Media Foundation MFT），MfH264Codec.h
 *
 *  第三期视频管线的编解码层：用 Media Foundation 自带的 H.264 编码器 / 解码器
 *  Transform（MFT）做 NV12 ↔ H.264 转换，不引入任何第三方视频库。
 *    - MfH264Encoder：NV12 帧 → 编码 → H.264 包（经 sink 回调送出，通常发往中转 / 落盘）。
 *    - MfH264Decoder：H.264 包 → 解码 → NV12 帧（经 sink 回调送出，供显示）。
 *
 *  线程模型：编码器 / 解码器各自非线程安全，约定一个实例只在单线程使用（编码在采集侧线程、
 *  解码在接收侧线程）。MF/COM 初始化由调用方保证（同 WASAPI/摄像头：在使用线程内 MFStartup
 *  + CoInitializeEx）。
 *
 *  本模块碰 MF MFT（依赖系统编解码器），不在自动化单测覆盖范围；用 --h264selftest 调试入口
 *  以合成 NV12 帧做「编码→解码→落 BMP」离线核对其 round-trip 正确性。
 *
 *  balloonwj@qq.com   2026-06-15
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace call {

//H.264 编码输出回调。OnEncodedH264 在调用 EncodeNv12 的线程上同步触发。
class MfH264EncoderSink
{
public:
    virtual ~MfH264EncoderSink() {}

    /**
     *  编码产出一个 H.264 包（字节流格式，含起始码）。
     *  @param data     H.264 包字节起始；仅本次回调内有效，需留存须自行拷贝
     *  @param len      字节数
     *  @param keyframe 是否关键帧（IDR）
     *  @param ptsHns   帧时间戳（100ns 单位，沿用编码输入的时间戳）
     */
    virtual void OnEncodedH264(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns) = 0;
};

//H.264 编码器（MF H.264 Encoder MFT）：NV12 → H.264。
class MfH264Encoder
{
public:
    MfH264Encoder();
    ~MfH264Encoder();

    MfH264Encoder(const MfH264Encoder&) = delete;
    MfH264Encoder& operator=(const MfH264Encoder&) = delete;

    /**
     *  初始化编码器（低延迟模式）。重复调用先释放上一个（幂等）。
     *  @param width      画面宽（像素，偶数）
     *  @param height     画面高（像素，偶数）
     *  @param fps        帧率（如 30）
     *  @param bitrateBps 目标码率（bit/s，如 1500000）
     *  @return 成功返回 true；MFT 创建 / 协商失败返回 false
     */
    bool Init(int width, int height, int fps, int bitrateBps);

    /**
     *  同上，另指定关键帧间隔（屏幕共享用，2026-10-02 加入；一对一通话仍用上面的四参数版本，行为不变）。
     *  @param gopFrames 关键帧间隔，单位帧；不大于 0 时用编码器默认值
     */
    bool Init(int width, int height, int fps, int bitrateBps, int gopFrames);

    /**
     *  要求下一帧编成关键帧（CODECAPI_AVEncVideoForceKeyFrame；第 0 期 0a 实测当帧生效）。
     *  @return 编码器接受了请求返回 true
     */
    bool ForceKeyFrame();

    /**
     *  运行中修改目标码率（CODECAPI_AVEncCommonMeanBitRate；屏幕共享的码率自适应用，第 9 期 9b）。
     *  @param bitrateBps 新的目标码率（bit/s），须大于 0
     *  @return 编码器已就绪且接受了新码率返回 true
     */
    bool SetBitrate(int bitrateBps);

    //释放编码器（析构自动调用）。
    void Close();

    //是否就绪。
    bool IsReady() const;

    /**
     *  编码一帧 NV12。尺寸须与 Init 一致。输出 H.264 包经 sink 同步回调（可能 0 或多个）。
     *  @param nv12   NV12 帧（Y 平面起点；UV 平面位于 nv12 + stride*height）
     *  @param stride 行跨度（字节）；当前实现要求 stride == Init 的 width（连续 NV12）
     *  @param ptsHns 帧时间戳（100ns 单位）
     *  @param sink   编码输出回调（非空）
     */
    void EncodeNv12(const uint8_t* nv12, int stride, int64_t ptsHns, MfH264EncoderSink* sink);

private:
    struct Impl;
    Impl* m_impl;
};

//H.264 解码输出回调（NV12）。OnDecodedNv12 在调用 DecodePacket 的线程上同步触发。
class MfH264DecoderSink
{
public:
    virtual ~MfH264DecoderSink() {}

    /**
     *  解码产出一帧 NV12。
     *  @param nv12   NV12 帧（Y 平面起点；UV 平面位于 nv12 + stride*height）；仅本次回调内有效
     *  @param width  画面宽（由码流 SPS 决定）
     *  @param height 画面高
     *  @param stride 行跨度（字节）
     *  @param ptsHns 帧时间戳（100ns 单位）
     */
    virtual void OnDecodedNv12(const uint8_t* nv12, int width, int height, int stride, int64_t ptsHns) = 0;
};

//H.264 解码器（MF H.264 Decoder MFT）：H.264 → NV12。输出尺寸由码流决定。
class MfH264Decoder
{
public:
    MfH264Decoder();
    ~MfH264Decoder();

    MfH264Decoder(const MfH264Decoder&) = delete;
    MfH264Decoder& operator=(const MfH264Decoder&) = delete;

    /**
     *  初始化解码器（低延迟模式）。幂等。
     *  @param width  预期画面宽（与对端编码一致；用于给解码器输入类型设帧尺寸、提前协商输出）
     *  @param height 预期画面高
     *  @return 成功返回 true
     *  说明：真实分辨率以码流 SPS 为准——若与传入不符，解码时经 MF_E_TRANSFORM_STREAM_CHANGE
     *  自动重协商输出尺寸。
     */
    bool Init(int width, int height);

    //释放解码器（析构自动调用）。
    void Close();

    //是否就绪。
    bool IsReady() const;

    /**
     *  解码一个 H.264 包。输出 NV12 帧经 sink 同步回调（可能 0 或多帧）。
     *  @param data   H.264 包字节起始
     *  @param len    字节数
     *  @param ptsHns 帧时间戳（100ns 单位）
     *  @param sink   解码输出回调（非空）
     */
    void DecodePacket(const uint8_t* data, size_t len, int64_t ptsHns, MfH264DecoderSink* sink);

private:
    struct Impl;
    Impl* m_impl;
};

} // namespace call
