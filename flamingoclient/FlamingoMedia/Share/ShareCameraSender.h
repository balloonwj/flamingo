/**
 *  屏幕共享房间里本人摄像头的发送端，ShareCameraSender.h
 *
 *  打开摄像头 → 画面缩进 640×360 → H.264 编码（15 帧每秒、约 400 kbps、4 秒一个兜底关键帧）→ 载荷带上宽高
 *  （与屏幕画面同一格式，见 ShareFraming::buildVideoPayload）→ 按摄像头轨（screenshare_track_camera）发进房间中转
 *  （docs/屏幕共享与录制方案.md 5.8、第 5 期 5a）。同时保留最近一帧编码尺寸的 BGRA 画面，供本人的摄像头小窗预览。
 *  服务端为新成员发来摄像头轨的关键帧请求时调用 RequestKeyFrame。
 *
 *  联调：Debug 版设置了环境变量 FLAMINGO_SHARE_FAKE_CAMERA=1 时，Start 不打开真实摄像头，改由一个模拟线程每秒
 *  15 帧送出移动的彩条画面，其余处理完全相同，用于在没有摄像头、或不便打开摄像头的机器上做端到端验证。
 *
 *  线程：Start / Stop / RequestKeyFrame / GetPreview 在 UI 线程（或任意线程）调用；采集、缩放、编码、发送都在摄像头的
 *  采集线程上。无摄像头模式（单元测试）不打开摄像头，由 FeedFrame 在调用线程上走同一条处理（调用线程须已初始化 COM
 *  与 Media Foundation）。
 *
 *  典型用法：
 *      if (!m_camera.Start(&m_streamer, seat)) 提示摄像头不可用;
 *      m_camera.Stop();
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "ShareAudioTransport.h"
#include "../Call/MfCameraCapture.h"
#include "../Call/MfH264Codec.h"

namespace screenshare {

//本人摄像头的发送端
class ShareCameraSender : private call::MfVideoFrameHandler, private call::MfH264EncoderSink
{
public:
    ShareCameraSender();
    ~ShareCameraSender();

    ShareCameraSender(const ShareCameraSender&) = delete;
    ShareCameraSender& operator=(const ShareCameraSender&) = delete;

    /**
     *  打开摄像头并开始发送。
     *  @param transport 发帧通道（房间中转连接），不拥有，须在 Stop 之前一直有效
     *  @param seat      本人座位号（写进帧头）
     *  @return 摄像头打开返回 true；没有摄像头或被占用返回 false（原因见 FailReason）
     */
    bool Start(ShareAudioTransport* transport, int seat);

    //无摄像头模式启动（单元测试）：之后由 FeedFrame 喂画面
    bool StartWithoutCamera(ShareAudioTransport* transport, int seat);

    /**
     *  只打开摄像头取预览，不编码、不发送（独立录屏的摄像头画中画用）。之后 GetPreview 取画面。
     *  @return 摄像头打开返回 true；失败原因见 FailReason
     */
    bool StartLocal();

    //停止发送并关闭摄像头；可重复调用
    void Stop();

    //是否在发送
    bool IsRunning() const;

    //要求下一帧编成关键帧（服务端发来摄像头轨的关键帧请求时）；任意线程可调
    void RequestKeyFrame();

    /**
     *  无摄像头模式：喂一帧 NV12 画面，走与摄像头回调相同的缩放、编码、发送。
     *  @param nv12   Y 平面后紧接 UV 平面，行跨度 stride
     */
    void FeedFrame(const uint8_t* nv12, int width, int height, int stride);

    /**
     *  取本人摄像头最近一帧（编码尺寸的 BGRA，自上而下，行跨度 = 宽 × 4）。
     *  @param serial [出参] 帧序号，调用方据此判断有没有新画面
     *  @return 已有至少一帧返回 true
     */
    bool GetPreview(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) const;

    //摄像头最近一次失败的原因（call::CameraFailReason）；没有失败过为 0
    int FailReason() const;

    /**
     *  设置编码的目标码率（群通话按开摄像头的人数调整，docs/群音视频通话方案.md 第 5 期）。任意线程可调；
     *  编码器还没创建时在创建时生效，已创建时在下一帧编码之前交给编码器。屏幕共享不调用，保持默认码率。
     *  @param bitrateBps 目标码率，单位 bit/s；不大于 0 时回到默认码率 kShareCameraBitrateBps
     */
    void SetTargetBitrate(int bitrateBps);

    //编码器当前使用的码率（bit/s）；编码器还没创建时为 0
    int AppliedBitrate() const;

private:
    //MfVideoFrameHandler：采集线程上收到一帧
    virtual void OnVideoFrame(const uint8_t* data, int width, int height, int stride) override;
    //MfH264EncoderSink：编码出一帧
    virtual void OnEncodedH264(const uint8_t* data, size_t len, bool keyframe, int64_t ptsHns) override;
    //模拟摄像头的出帧线程（只在 Debug 版、设置了 FLAMINGO_SHARE_FAKE_CAMERA=1 时使用）
    void fakeCameraProc();

    call::MfCameraCapture    m_capture;        //摄像头采集
    call::MfH264Encoder      m_encoder;        //编码器（采集线程上懒创建）
    ShareAudioTransport*     m_transport;      //发帧通道（不拥有）
    int                      m_seat;           //本人座位号
    std::atomic<bool>        m_running;        //是否在发送
    std::atomic<bool>        m_keyRequested;   //是否收到了关键帧请求
    std::atomic<int>         m_targetBitrate;  //目标码率（bit/s），SetTargetBitrate 写、采集线程读
    std::atomic<int>         m_appliedBitrate; //编码器当前使用的码率（bit/s），采集线程写；编码器没创建时为 0
    bool                     m_encoderTried;   //是否已尝试创建编码器（只在采集线程使用）
    int                      m_encW;           //编码宽（采集线程上按第一帧确定）
    int                      m_encH;           //编码高
    int64_t                  m_frameIndex;     //已编码的帧数（算时间戳；只在采集线程使用）
    std::vector<uint8_t>     m_scaled;         //缩放后的 NV12（只在采集线程使用）
    mutable std::mutex       m_previewMutex;   //保护以下四项
    std::vector<uint8_t>     m_preview;        //最近一帧预览（BGRA）
    int                      m_previewW;       //预览宽
    int                      m_previewH;       //预览高
    uint64_t                 m_previewSerial;  //预览帧序号
    std::unique_ptr<std::thread> m_fakeThread; //模拟摄像头的出帧线程；没有使用模拟摄像头时为空
    std::atomic<bool>        m_fakeStop;       //通知模拟线程退出
};

} // namespace screenshare
