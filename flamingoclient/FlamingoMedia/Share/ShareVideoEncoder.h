/**
 *  屏幕共享的画面编码器，ShareVideoEncoder.h
 *
 *  把采集器产出的 BGRA 画面编成 H.264：按 ComputeShareEncodeSize 定编码尺寸（长边不超过 1920、取偶数），
 *  带缩放转成 NV12，交给系统自带的 H.264 编码器（call::MfH264Encoder）。源画面尺寸变化（共享的窗口被拉大、
 *  切换了共享内容）时自动按新尺寸重建编码器，重建后的第一帧一定是关键帧；观看端的解码器能随码流尺寸重新协商。
 *  设计见 docs/屏幕共享与录制方案.md 5.6。
 *
 *  依赖系统编码器，不进自动化单测；尺寸与颜色转换的规则在 ShareVideoPolicy 里单测，整体由调试入口
 *  --shareencode 采集真实画面「编码→解码」核对。一个实例只在一条线程上使用，该线程须已 CoInitializeEx 与 MFStartup。
 *
 *  典型用法：
 *      ShareVideoEncoder enc;
 *      enc.Encode(frame, wantKeyFrame, timestampUs, &sink);   //sink 同步收到编好的 H.264 访问单元
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include "ShareCapturer.h"
#include "ShareHwH264Encoder.h"
#include "../Call/MfH264Codec.h"

namespace screenshare {

//编码输出回调；在调用 Encode 的线程上同步触发
class ShareVideoEncoderSink
{
public:
    virtual ~ShareVideoEncoderSink()
    {
    }

    /**
     *  编好了一个 H.264 访问单元（字节流格式，含起始码；关键帧带 SPS / PPS）。
     *  @param data        数据，只在本次回调期间有效
     *  @param len         字节数
     *  @param keyframe    是否关键帧
     *  @param timestampUs 时间戳，单位微秒（沿用这一帧在 Encode 时传入的值；硬件编码器的码流可能在下一次 Encode 时才交出）
     */
    virtual void OnShareEncoded(const uint8_t* data, size_t len, bool keyframe, uint64_t timestampUs) = 0;
};

//屏幕共享的画面编码器
class ShareVideoEncoder : private call::MfH264EncoderSink
{
public:
    ShareVideoEncoder();
    ~ShareVideoEncoder();

    ShareVideoEncoder(const ShareVideoEncoder& rhs) = delete;
    ShareVideoEncoder& operator=(const ShareVideoEncoder& rhs) = delete;

    /**
     *  编码一帧。源尺寸与上次不同时先按新尺寸重建编码器（此时这一帧必定编成关键帧）。
     *  @param frame       采集到的画面
     *  @param keyframe    是否要求编成关键帧
     *  @param timestampUs 时间戳，单位微秒
     *  @param sink        输出回调，不可为 NULL
     *  @return 已交给编码器返回 true；编码器建不起来返回 false
     */
    bool Encode(const ShareCaptureFrame& frame, bool keyframe, uint64_t timestampUs, ShareVideoEncoderSink* sink);

    //释放编码器；之后再 Encode 会重建
    void Close();

    //当前编码尺寸，单位像素；还没建编码器时为 0
    int EncodeWidth() const;
    int EncodeHeight() const;

    //最近一次 Encode 是否因为重建编码器而强制编成了关键帧
    bool LastWasRebuild() const;

    /**
     *  修改目标码率（码率自适应用，第 9 期 9b）：编码器已建好时立即生效，之后重建编码器时也沿用。
     *  @param bitrateBps 目标码率（bit/s），须大于 0
     */
    void SetBitrate(int bitrateBps);

    /**
     *  是否允许用硬件编码器（默认允许）。允许时下一次建编码器先试硬件编码器，建不起来或编码出错就改用软件编码器，
     *  且本对象之后不再尝试硬件（第 9 期 9c）。在两次 Encode 之间调用，下一次重建编码器时生效。
     *  @param allowed false 表示只用软件编码器（单元测试与排查问题用）
     */
    void SetHardwareAllowed(bool allowed);

    //当前是否在用硬件编码器（还没建编码器时为 false）
    bool IsHardware() const;

private:
    virtual void OnEncodedH264(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns);

    //按编码尺寸建编码器：允许硬件且之前没失败过时先试硬件编码器，不行再建软件编码器；成功返回 true
    bool openEncoder(int w, int h);

    //当前所用的编码器是否已建好
    bool encoderReady() const;

    call::MfH264Encoder     m_encoder;       //系统 H.264 编码器
    int                     m_srcW;          //当前源画面宽
    int                     m_srcH;          //当前源画面高
    int                     m_encW;          //当前编码宽
    int                     m_encH;          //当前编码高
    std::vector<uint8_t>    m_nv12;          //NV12 转换缓冲
    ShareVideoEncoderSink*  m_sink;          //本次 Encode 的输出回调（只在 Encode 期间有效）
    uint64_t                m_timestampUs;   //本次 Encode 的时间戳
    bool                    m_lastRebuild;   //最近一次 Encode 是否重建了编码器
    int                     m_bitrateBps;    //目标码率（bit/s），初始为 kShareBitrateBps，由码率自适应修改
    ShareHwH264Encoder      m_hw;            //硬件编码器（第 9 期 9c）；建不起来时用 m_encoder
    bool                    m_useHw;         //当前是否在用硬件编码器
    bool                    m_hwAllowed;     //是否允许用硬件编码器（SetHardwareAllowed）
    bool                    m_hwFailed;      //硬件编码器在本次推流中是否失败过；失败后到 Close 之前只用软件编码器
    bool                    m_hwRebuilt;     //硬件编码器因改码率重建过、还没编下一帧（下一次 Encode 报告为重建）
};

} // namespace screenshare
