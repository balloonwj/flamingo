/**
 *  屏幕共享房间里其他成员摄像头的接收端，ShareCameraReceiver.h
 *
 *  按座位分路：每个开着摄像头的成员一个 H.264 解码器，解出的画面转成 BGRA 留最近一帧，供摄像头小窗显示
 *  （docs/屏幕共享与录制方案.md 5.8、第 5 期）。同屏最多解码 kShareMaxCameraTiles（9）路，超出的人只记下座位、
 *  不解码（5b），有人关掉摄像头或离开后，后来的人从他的下一个关键帧开始解码。
 *
 *  线程：OnRemoteFrame 在中转接收线程上调用，只把帧放进队列；解码在本类自己的工作线程上（该线程初始化 COM 与
 *  Media Foundation），不占用中转接收线程。其余方法可在任意线程调用。
 *
 *  典型用法：
 *      m_cameras.Start();
 *      m_cameras.OnRemoteFrame(frame);          //接收线程
 *      m_cameras.GetFrame(seat, bgra, w, h, serial);   //界面定时取
 *      m_cameras.Stop();
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "ShareFraming.h"
#include "../Call/MfH264Codec.h"

namespace screenshare {

//其他成员摄像头的接收端
class ShareCameraReceiver
{
public:
    ShareCameraReceiver();
    ~ShareCameraReceiver();

    ShareCameraReceiver(const ShareCameraReceiver&) = delete;
    ShareCameraReceiver& operator=(const ShareCameraReceiver&) = delete;

    //启动解码线程；可重复调用
    void Start();

    //停止解码线程并清空全部座位；可重复调用
    void Stop();

    //收到一帧（中转接收线程）：只处理摄像头轨，放进解码队列
    void OnRemoteFrame(const ShareMediaFrame& frame);

    //删除某个座位的解码器与画面（成员离开或关掉摄像头时）
    void RemoveSeat(int seat);

    /**
     *  取某个座位最近一帧（BGRA，自上而下，行跨度 = 宽 × 4）。
     *  @param serial [出参] 帧序号，调用方据此判断有没有新画面
     *  @return 该座位已有至少一帧返回 true
     */
    bool GetFrame(int seat, std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) const;

    //正在解码的座位（升序，最多 kShareMaxCameraTiles 个）
    std::vector<int> Seats() const;

    //因超出路数上限而没有解码的座位（升序）
    std::vector<int> OverflowSeats() const;

    //队列里还没解码的帧数（测试用）
    size_t PendingFrames() const;

private:
    //一个座位的解码状态
    struct SeatDecoder : public call::MfH264DecoderSink
    {
        ShareCameraReceiver* m_owner;     //所属接收端
        int                  m_seat;      //座位号
        call::MfH264Decoder  m_decoder;   //解码器（只在解码线程使用）
        bool                 m_initTried; //是否已尝试创建解码器
        int                  m_cropW;     //当前帧的真实宽（载荷里带的）
        int                  m_cropH;     //当前帧的真实高

        SeatDecoder();
        virtual void OnDecodedNv12(const uint8_t* nv12, int width, int height, int stride, int64_t ptsHns) override;
    };

    //一个座位最近一帧
    struct SeatFrame
    {
        std::vector<uint8_t> m_bgra;      //画面
        int                  m_width;     //宽
        int                  m_height;    //高
        uint64_t             m_serial;    //帧序号

        SeatFrame();
    };

    void threadProc();
    void decode(const ShareMediaFrame& frame);
    void storeFrame(int seat, const uint8_t* nv12, int width, int height, int stride, int cropW, int cropH);

    std::unique_ptr<std::thread>                 m_thread;       //解码线程
    std::atomic<bool>                            m_stop;         //是否要求停止
    mutable std::mutex                           m_queueMutex;   //保护 m_queue
    std::condition_variable                      m_queueCv;      //队列有新帧
    std::deque<ShareMediaFrame>                  m_queue;        //待解码的帧
    std::map<int, std::unique_ptr<SeatDecoder> > m_decoders;     //座位 → 解码器（只在解码线程使用）
    std::vector<int>                             m_removed;      //待删除的座位（任意线程写，解码线程处理），受 m_queueMutex 保护
    mutable std::mutex                           m_frameMutex;   //保护以下两项
    std::map<int, SeatFrame>                     m_frames;       //座位 → 最近一帧
    std::map<int, bool>                          m_overflow;     //超出上限、没有解码的座位
    std::vector<uint8_t>                         m_convert;      //NV12 → BGRA 转换缓冲（只在解码线程使用）
};

} // namespace screenshare
