/**
 *  屏幕画面来源（第 22 期多人共享），ShareScreenFeed.h
 *
 *  观看窗按画面清单从「画面来源」逐块取最新一帧：观看者的来源是自己的收流管线（ShareViewerReceiver），主持人看别人的屏幕时
 *  来源是自己的推流管线（ShareHostStreamer，它的中转连接上也会收到其他共享者的画面）。两者都实现本接口，观看窗不关心是哪一种。
 *  所有方法都可在 UI 线程上调用，实现方内部自行加锁。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <vector>

namespace screenshare {

//屏幕画面来源
class IShareScreenFeed
{
public:
    virtual ~IShareScreenFeed()
    {
    }

    /**
     *  取某个画面座位的最新一帧。
     *  @param seat   画面座位
     *  @param bgra   [出参] 自上而下的 BGRA，行跨度 = 宽 × 4
     *  @param serial [出参] 帧序号（调用方据此判断是否有新帧）
     *  @return 这一路解出过画面返回 true
     */
    virtual bool GetScreenFrame(int seat, std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) const = 0;

    /**
     *  只取某个画面座位最新一帧的帧序号，不拷贝画面。整屏一帧有十几 MB，观看窗每收到一条帧通知都要逐座位检查，
     *  先用本函数比较序号，序号变了才调 GetScreenFrame。
     *  @param seat   画面座位
     *  @param serial [出参] 帧序号，与 GetScreenFrame 带回的是同一个编号
     *  @return 这一路解出过画面返回 true
     */
    virtual bool GetScreenSerial(int seat, uint64_t& serial) const = 0;

    //解出过画面的画面座位，升序
    virtual std::vector<int> ScreenSeats() const = 0;

    //某个画面座位的共享内容状态（取值见 screenshare_source_state）；没收到过时为正在共享
    virtual int ScreenSourceState(int seat) const = 0;
};

} // namespace screenshare
