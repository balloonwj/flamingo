/**
 *  多人共享里本人那一块画面的本机预览，ShareLocalPreview.h
 *
 *  2026-10-12 起每个人都看到全部画面，本人正在共享的那一块也在观看窗（非主持人共享者）与「共享画面」窗口（主持人）里
 *  （docs/远程桌面控制权限与观看工具条方案.md 第 2.2 节 C）。这一块取本机采集、编码前的那一帧：中转不会把一个人推的
 *  画面转给他本人的其他连接（服务端 RoomRelayFrame::shouldForwardTo，2026-10-10 起），无论主持人还是其他共享者，
 *  本人那一块都收不到中转转回来的画面。
 *  推流管线的工作线程每编一帧就交给本类一次，本类按 kLocalPreviewIntervalMs 限帧率留下最新一帧（自己那一块只是用来
 *  确认在共享什么，不需要满帧率，整屏 BGRA 一帧有十几 MB，每帧都拷贝开销太大）；画面尺寸变了时不受限帧率约束、立即留下。
 *  界面线程按座位取帧。所有方法都可在任意线程调用，内部加锁。
 *
 *  典型用法：
 *      preview.SetSeat(mySeat);                                     //界面线程：快照里本人的画面座位
 *      if (preview.Offer(pixels, w, h, stride, nowMs)) notify(...);   //工作线程：编码之前
 *      preview.Get(seat, bgra, w, h, serial);                        //界面线程：观看窗取帧
 *      preview.SetSeat(-1);                                          //停止共享：关闭并丢掉画面
 *
 *  单元测试见 Source/Tests/ShareMultiTests.cpp（MSC2a）。
 *
 *  balloonwj@qq.com   2026-10-12
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <mutex>
#include <vector>
#include "ShareLatestFrame.h"   // ShareLatestFrame：最新一帧的拷贝、帧序号与取帧

namespace screenshare {

//本机预览留帧的最小间隔（毫秒）：每秒最多 5 帧
const int64_t kLocalPreviewIntervalMs = 200;

//本人那一块画面的本机预览：限帧率留下推流管线编码前的最新一帧
class ShareLocalPreview
{
public:
    ShareLocalPreview();

    /**
     *  设置本人画面的座位。座位变了但仍不小于 0 时保留已留的画面（画面内容都是本机的共享内容，只是标号变了）。
     *  @param seat 画面座位；小于 0 表示关闭预览，同时丢掉已留的画面
     */
    void SetSeat(int seat);

    //本人画面的座位；关闭时为 -1
    int Seat() const;

    //预览是否打开（座位不小于 0）
    bool IsEnabled() const;

    //是否已留下画面（推流管线据此在刚打开预览时把最近一帧补进来：画面不变时采集器不出新帧）
    bool HasFrame() const;

    /**
     *  交一帧画面（推流管线的工作线程）。预览关闭时不留；距上次留帧不足 kLocalPreviewIntervalMs 且尺寸没变时不留。
     *  @param pixels 自上而下的 BGRA；不拥有，调用期间有效
     *  @param width  宽（像素）
     *  @param height 高（像素）
     *  @param stride 行跨度（字节），不小于宽 × 4；留下的画面按宽 × 4 紧密排列
     *  @param nowMs  当前时间（毫秒，单调时钟）
     *  @return 留下了这一帧返回 true（调用方据此通知界面取帧）
     */
    bool Offer(const uint8_t* pixels, int width, int height, int stride, int64_t nowMs);

    /**
     *  取最新一帧（界面线程）。
     *  @param seat   要取的画面座位；不是本人画面的座位时取不到
     *  @param bgra   [出参] 自上而下的 BGRA，行跨度 = 宽 × 4
     *  @param serial [出参] 帧序号，每留一帧加一
     *  @return 座位相符且已留下画面返回 true
     */
    bool Get(int seat, std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) const;

    /**
     *  只取最新一帧的帧序号，不拷贝画面（界面线程先比序号，变了才调 Get）。
     *  @param seat   要取的画面座位；不是本人画面的座位时取不到
     *  @param serial [出参] 帧序号，与 Get 带回的是同一个编号
     *  @return 座位相符且已留下画面返回 true
     */
    bool GetSerial(int seat, uint64_t& serial) const;

private:
    std::atomic<int>     m_seat;      //本人画面的座位；-1 表示预览关闭
    std::atomic<bool>    m_hasFrame;  //是否已留下画面（工作线程每一轮都要看，不加锁）
    ShareLatestFrame     m_latest;    //留下的最新一帧与帧序号（2026-10-14 起与录制旁路共用这个类）；关闭预览后帧序号不归零，
                                      //界面不会把新画面误认作旧帧
    std::mutex           m_offerMutex;   //保护 m_lastMs，并让「判断限帧率 + 留帧」成为一步
    int64_t              m_lastMs;    //上次留帧的时间（毫秒，单调时钟），用于限帧率
};

} // namespace screenshare
