/**
 *  一路画面的「最新一帧」缓存，ShareLatestFrame.h
 *
 *  推流管线的工作线程每编一帧都要把编码前的画面交给两处：本机预览（ShareLocalPreview，多人共享里本人那一块）与录制旁路
 *  （发起人录共享时录下与观看者看到的一致的画面），界面线程或录制线程再按帧序号取走。2026-10-14 之前两处各写一份
 *  「拷贝、记宽高、帧序号加一、加锁取出」，现在合并为本类（docs/远程桌面控制权限与观看工具条方案.md 8.2 第 6 条）；
 *  预览的座位与限帧率仍由 ShareLocalPreview 负责。
 *  所有方法都可在任意线程调用，内部加锁。
 *
 *  典型用法：
 *      latest.Store(pixels, w, h, stride);           //工作线程：留下这一帧
 *      latest.Get(bgra, w, h, serial);                //其他线程：取最新一帧
 *      latest.Clear();                                //关闭：丢掉画面（帧序号不归零）
 *
 *  单元测试经 ShareLocalPreview（Source/Tests/ShareMultiTests.cpp 的 MSC2a）与录制旁路的用例（[sharerecmerge] 等）覆盖。
 *
 *  balloonwj@qq.com   2026-10-14
 */
#pragma once

#include <stdint.h>
#include <mutex>
#include <vector>

namespace screenshare {

//一路画面的最新一帧（BGRA，行跨度 = 宽 × 4）与帧序号
class ShareLatestFrame
{
public:
    ShareLatestFrame();

    /**
     *  留下一帧：按宽 × 4 紧密拷贝，帧序号加一。
     *  @param pixels 自上而下的 BGRA；不拥有，调用期间有效；为 NULL 时不留
     *  @param width  宽（像素），须大于 0
     *  @param height 高（像素），须大于 0
     *  @param stride 行跨度（字节），不小于宽 × 4
     *  @return 留下了返回 true；参数不合法时返回 false
     */
    bool Store(const uint8_t* pixels, int width, int height, int stride);

    /**
     *  取最新一帧（拷贝）。
     *  @param bgra   [出参] 自上而下的 BGRA，行跨度 = 宽 × 4
     *  @param width  [出参] 宽
     *  @param height [出参] 高
     *  @param serial [出参] 帧序号，每留一帧加一
     *  @return 有画面返回 true
     */
    bool Get(std::vector<uint8_t>& bgra, int& width, int& height, uint64_t& serial) const;

    /**
     *  只取帧序号，不拷贝画面（取帧方先比序号，变了才调 Get）。
     *  @param serial [出参] 帧序号
     *  @return 有画面返回 true
     */
    bool GetSerial(uint64_t& serial) const;

    //是否有画面
    bool HasFrame() const;

    /**
     *  已有画面时它的宽高。
     *  @param width  [出参] 宽；没有画面时为 0
     *  @param height [出参] 高；没有画面时为 0
     */
    void Size(int& width, int& height) const;

    //丢掉画面。帧序号不归零，取帧方不会把之后的新画面误认作旧帧
    void Clear();

private:
    mutable std::mutex   m_mutex;    //保护以下各项
    std::vector<uint8_t> m_frame;    //最新一帧（BGRA，行跨度 = 宽 × 4）；没有画面时为空
    int                  m_width;    //该帧的宽
    int                  m_height;   //该帧的高
    uint64_t             m_serial;   //帧序号，每留一帧加一
};

} // namespace screenshare
