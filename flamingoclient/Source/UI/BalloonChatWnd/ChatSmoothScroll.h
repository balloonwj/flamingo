/**
 *  ChatSmoothScroll.h
 *
 *  消息列表的平滑滚动：把滚轮输入换算成目标滚动位置，并按时间给出缓出动画的当前位置。
 *  纯逻辑、不依赖窗口，时间由调用方传入，便于单元测试逐帧验证。
 *
 *  解决的两个问题：
 *    · 滚轮一格原先瞬间跳 64 像素，快速拨动时 Windows 把十几格合成一条消息、一次跳几百
 *      像素，观感是一顿一顿的。现在每次滚轮只改目标位置，由动画在约 150 毫秒内过渡过去。
 *    · 触控板与高精度滚轮每次只送一小段 delta（小于一格的 120），原先按整数除法换算成
 *      0 格、输入被整个丢弃。现在按比例换算成像素，一格对应 64 像素，delta=30 就是 16 像素。
 *
 *  典型用法（ChatMessageList 内部）：
 *      //收到滚轮
 *      if (m_smooth.OnWheel(m_scrollY, maxScroll, zDelta, kWheelStep, PerfNowMs()))
 *          启动逐帧驱动;
 *      //每一帧
 *      m_scrollY = m_smooth.Advance(PerfNowMs());
 *      if (!m_smooth.IsActive())
 *          停止逐帧驱动;
 *
 *  balloonwj@qq.com   2026-09-30
 */

#pragma once

namespace balloonchatwnd {

// 平滑滚动动画的时长（毫秒）。每次收到滚轮都从当前位置重新计时，所以连续滚动时画面
// 不会停顿，停手之后约这么久停稳。取 150：短于 100 看不出过渡，长于 200 会觉得跟手慢。
const int kChatSmoothScrollDurationMs = 150;

/**
 *  ChatSmoothScroll —— 一个滚动方向上的平滑滚动状态。
 *
 *  位置单位是像素，与 ChatMessageList::ScrollY 同一口径（0 为内容顶部，越大越靠下）。
 *  内部用浮点记录起点与目标，不足一像素的滚动量（触控板的细小输入）不会丢失。
 */
class ChatSmoothScroll
{
public:
    ChatSmoothScroll();

    /**
     *  把滚轮 delta 换算成像素位移。
     *    zDelta：WM_MOUSEWHEEL 的 delta，一格为 WHEEL_DELTA（120）；正值表示向上滚。
     *    stepPx：一格对应的像素数（>0）。
     *  @return 位移像素数（带小数）。符号与 zDelta 相同：正值表示内容向下移、看更早的消息。
     *          例：stepPx=64 时，120 → 64，30 → 16，-240 → -128。
     */
    static double WheelDeltaToPixels(int zDelta, int stepPx);

    /**
     *  收到一次滚轮：更新目标位置并从当前位置重新开始计时。
     *
     *  目标的累加基准：动画进行中时在<u>当前目标</u>上累加，否则在当前位置上累加。前者
     *  保证连续快速滚动时每一格都算数，不会因为动画还没走完而少滚。
     *  新目标夹紧在 [0, maxScroll]。夹紧发生在目标上，所以滚到顶或到底之后反向滚，
     *  反向的第一格就立即生效，不会先抵消一段越界量。
     *
     *    currentPos：列表当前的实际滚动位置（像素）。
     *    maxScroll： 可滚动的最大位置（像素，>=0）。
     *    zDelta：    滚轮 delta，含义同 WheelDeltaToPixels。
     *    stepPx：    一格对应的像素数（>0）。
     *    nowMs：     当前时刻（毫秒，单调递增的时钟即可，与 Advance 用同一个时钟）。
     *  @return 滚动后需要动画（目标与当前位置不同）时返回 true；已经在顶部 / 底部、
     *          本次滚动没有任何效果时返回 false，此时状态不变。
     */
    bool OnWheel(int currentPos, int maxScroll, int zDelta, int stepPx, double nowMs);

    /**
     *  推进到 nowMs 时刻，返回此刻应处的滚动位置（取整后的像素）。
     *  动画按缓出曲线从起点走到目标：刚开始走得快、快到目标时变慢。时长到了即结束，
     *  返回精确的目标位置，之后 IsActive 返回 false。
     *  没有进行中的动画时返回目标位置（即最近一次动画的终点）。
     *    nowMs：当前时刻（毫秒），与 OnWheel 用同一个时钟；早于起始时刻时按起点处理。
     */
    int Advance(double nowMs);

    // 是否有进行中的动画。
    bool IsActive() const;

    // 当前目标位置（像素，带小数）。
    double Target() const;

    /**
     *  取消进行中的动画，停在调用方当前的位置上。用于滚动位置被别的操作直接改掉的场合：
     *  拖动滚动条、滚到底（新消息到达）、定位到某条消息、清空列表。不取消的话，动画的下
     *  一帧会把位置拉回去。
     */
    void Cancel();

    /**
     *  把起点与目标一起平移 delta 像素。用于列表头部插入内容（向上翻页加载了更早的
     *  消息）时：列表会把滚动位置加上新增内容的高度，使视口里的内容不动；进行中的动画
     *  也要同步平移，否则下一帧会把视口拉回插入前的位置。没有动画时只平移目标。
     *    delta：平移量（像素），正值表示向下。
     */
    void Shift(int delta);

private:
    bool   m_active;    // 是否有进行中的动画
    double m_from;      // 本段动画的起点位置（像素）
    double m_target;    // 目标位置（像素）
    double m_startMs;   // 本段动画的起始时刻（毫秒）
};

} // namespace balloonchatwnd
