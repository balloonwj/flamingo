/**
 *  远程桌面控制端的 H.264 画面，RdVideoPicture.h
 *
 *  2026-10-15 远程桌面改用 H.264（docs/远程桌面H264与共享画质提升方案.md 4.7）。接收线程把 4 号视频帧放进有上限的队列
 *  （RdVideoFrameQueue），本类自己的解码线程（初始化 COM 多线程套间与 Media Foundation）取出来用系统解码器（call::MfH264Decoder）
 *  解码、按编码尺寸裁掉补齐的部分转成 BGRA，存为「最新一帧」，再向观看窗投 msgFrame 请它重画。不在 UI 线程上解码
 *  （2K、4K 的关键帧解码可能要几十毫秒），也不在 P2P 帧通道的转交线程上解码（那条线程不归本模块初始化 COM）。
 *  解码跟不上（队列溢出）、解码器建不起来时向观看窗投 msgKeyRequest，由观看窗经限频后请求关键帧。
 *  指针由 5 号帧更新（UI 线程调 SetCursor）。实现 IRdPicture：源尺寸用于等比显示与鼠标坐标换算，与编码尺寸无关。
 *
 *  线程：PushFrame 在接收线程；SetCursor、drawTo、ready、width 等在 UI 线程；解码在本类的线程。共享的数据以互斥量保护。
 *  典型用法（观看窗）：
 *      m_video.Start(m_hWnd, WM_XXX_PICTURE, WM_XXX_KEYREQ);
 *      ...接收线程收到 4 号帧：m_video.PushFrame(data, len);
 *      ...WM_XXX_PICTURE：画布改用 m_video 并重画；关窗：m_video.Stop();
 *
 *  balloonwj@qq.com   2026-10-15
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "RdPicture.h"
#include "RdVideoFrameQueue.h"
#include "../Call/MfH264Codec.h"
#include "../Share/ShareFrameDrawer.h"   // ShareFrameDrawer：缩小显示时用 SSE2 缩放代替 HALFTONE

namespace remotedesktop {

//控制端的 H.264 画面
class RdVideoPicture : public IRdPicture, private call::MfH264DecoderSink
{
public:
    RdVideoPicture();
    ~RdVideoPicture();

    RdVideoPicture(const RdVideoPicture&) = delete;
    RdVideoPicture& operator=(const RdVideoPicture&) = delete;

    /**
     *  起解码线程（已在运行时先停掉）。画面与指针清空。
     *  @param notifyWnd     解出新画面、需要请求关键帧时投消息的窗口；可为 NULL（单元测试）
     *  @param msgFrame      解出新画面时投的消息
     *  @param msgKeyRequest 需要请求关键帧时投的消息
     */
    void Start(HWND notifyWnd, UINT msgFrame, UINT msgKeyRequest);

    //停解码线程并清空队列；画面保留到下一次 Start。幂等
    void Stop();

    /**
     *  放入一个 4 号视频帧（接收线程）。
     *  @param data / len 整个引擎帧
     *  @return 是有效的视频帧并已交给解码线程（或因等关键帧被丢弃）返回 true；解析失败返回 false
     */
    bool PushFrame(const unsigned char* data, size_t len);

    /**
     *  更新指针（UI 线程，5 号帧）。
     *  @param x / y 被控端屏幕坐标
     *  @param type  指针类型索引
     */
    void SetCursor(int32_t x, int32_t y, BYTE type);

    /**
     *  回执里回送的时刻（UI 线程，每 500 毫秒）：最近收到的视频帧的发送时刻加上它在本端停留的时长（RdAckEchoMs）。
     *  @param nowMs 当前时刻（GetTickCount64，毫秒；与收帧时记下的时刻同一时钟）
     *  @param echo  [出参] 回送的时刻
     *  @return 收到过视频帧返回 true
     */
    bool AckEcho(int64_t nowMs, uint32_t& echo) const;

    //收帧队列是否在等关键帧（溢出之后）：观看窗据此在关键帧请求被限频挡下后补发
    bool WaitingForKeyframe() const;

    //累计解出的帧数（日志与测试用）
    uint64_t DecodedCount() const;

    /**
     *  试建 H.264 解码器，得出本机能否解 H.264 与能解的最大长边（控制端连上之前调用一次，方案 4.3）。
     *  先按 1920×1080 建，建不起来即不能解；再按 maxLongSide × 对应 16:9 的高建，被拒就报 1920（据推测 Win7 最大 1920×1088）。
     *  自己初始化与释放 COM、Media Foundation，可以在任意线程上调用；耗时几十毫秒，不要在 UI 线程上调用。
     *  @param wantLongSide 想要的最大长边（像素，kRdVideoMaxLongSide）
     *  @param maxLongSide  [出参] 能解的最大长边；不能解时为 0
     *  @return 能解 H.264 返回 true
     */
    static bool ProbeDecoder(int wantLongSide, int& maxLongSide);

    //IRdPicture
    virtual bool ready() const override;
    virtual int width() const override;
    virtual int height() const override;
    virtual void drawTo(HDC hdc, const RECT& dest) const override;
    virtual POINT cursorPos() const override;
    virtual BYTE cursorType() const override;

    /**
     *  单元测试用：直接设一帧画面（不经解码）。
     *  @param srcW / srcH 源尺寸
     *  @param encW / encH 编码尺寸（bgra 的宽高）
     *  @param bgra        自上而下的 BGRA，行跨度 = encW × 4
     */
    void SetFrameForTest(int srcW, int srcH, int encW, int encH, const std::vector<uint8_t>& bgra);

    /**
     *  单元测试用：复制出最新一帧。
     *  @return 有画面返回 true
     */
    bool GetFrameForTest(std::vector<uint8_t>& bgra, int& encW, int& encH) const;

private:
    //解码线程主体
    void DecodeProc();

    //解码一个视频帧（解码线程）
    void decodeOne(const std::vector<unsigned char>& frame);

    //MfH264DecoderSink：解出一帧 NV12（解码线程），转成 BGRA 存为最新一帧并通知窗口
    virtual void OnDecodedNv12(const uint8_t* nv12, int width, int height, int stride, int64_t ptsHns) override;

    //向窗口投一条消息（窗口为 NULL 或已销毁时什么都不做）
    void notify(UINT msg) const;

    RdVideoFrameQueue        m_queue;        //收帧队列（接收线程放、解码线程取）
    std::unique_ptr<std::thread> m_thread;   //解码线程
    std::atomic<bool>        m_stop;         //要求解码线程退出
    std::mutex               m_wakeMutex;    //配合 m_wake 使用
    std::condition_variable  m_wake;         //有新帧或要求退出时唤醒解码线程
    HWND                     m_notifyWnd;    //通知窗口（不拥有）
    UINT                     m_msgFrame;     //解出新画面时投的消息
    UINT                     m_msgKeyRequest;//需要请求关键帧时投的消息
    mutable std::mutex       m_ackMutex;     //保护以下三项（接收线程写，UI 线程读；不与 m_mutex 共用，绘制时不阻塞收帧）
    bool                     m_hasSendMs;    //是否收到过视频帧
    uint32_t                 m_lastSendMs;   //最近收到的视频帧的发送时刻（被控端时钟）
    int64_t                  m_lastRecvMs;   //收到它的时刻（GetTickCount64，毫秒）
    std::atomic<uint64_t>    m_decoded;      //累计解出的帧数
    //以下只在解码线程上使用
    call::MfH264Decoder      m_decoder;      //系统 H.264 解码器
    int                      m_curSrcW;      //正在解码的帧的源宽
    int                      m_curSrcH;      //正在解码的帧的源高
    int                      m_curEncW;      //正在解码的帧的编码宽（裁掉补齐的部分用）
    int                      m_curEncH;      //正在解码的帧的编码高
    std::vector<uint8_t>     m_staging;      //备用缓冲：转换出的下一帧，转好后与 m_latest 交换
    //以下受 m_mutex 保护（解码线程写，UI 线程读）
    mutable std::mutex       m_mutex;
    std::vector<uint8_t>     m_latest;       //最新一帧（BGRA，自上而下，行跨度 = m_encW × 4）
    int                      m_srcW;         //最新一帧的源宽
    int                      m_srcH;         //最新一帧的源高
    int                      m_encW;         //最新一帧的宽
    int                      m_encH;         //最新一帧的高
    POINT                    m_cursor;       //指针位置（被控端屏幕坐标）
    BYTE                     m_cursorType;   //指针类型索引
    //画面的绘制器（只在 UI 线程的 drawTo 里使用，在 m_mutex 内调用；drawTo 是 const，故为 mutable）
    mutable screenshare::ShareFrameDrawer m_drawer;
};

} // namespace remotedesktop
