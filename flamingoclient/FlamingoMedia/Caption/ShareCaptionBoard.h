/**
 *  屏幕共享实时字幕的显示与发送规则（docs/语音字幕方案.md 第 9 节），ShareCaptionBoard.h
 *
 *    - ShareCaptionThrottle：发送端的中间结果限速（每秒最多 kShareCaptionPartialPerSec 次），最终结果不限速；
 *    - ShareCaptionBoard：接收端各座位当前的字幕：同一句的中间结果不断替换，最终结果保留 kShareCaptionFinalHoldMs 毫秒
 *      后消失，长时间没有更新的中间结果也会消失；同时最多显示两个人，按最近更新的先后取，显示时较早的在上；
 *    - ShareCaptionRemoteTimes：录制共享时，别人的一句最终结果在录制时间轴上的起止时刻（收到时刻减去这句的时长）。
 *  全部为纯逻辑，不依赖窗口，单测见 Source/Tests/ShareCaptionTests.cpp（标签 [sharecaption]）。
 *
 *  典型用法：
 *      if (throttle.Allow(final, nowMs)) send(...);
 *      board.OnCaption(seat, speaker, text, final, sentenceId, nowMs);
 *      std::vector<ShareCaptionLine> lines = board.Visible(nowMs);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

namespace screenshare {

//中间结果每秒最多发送的次数
const int kShareCaptionPartialPerSec = 5;
//最终结果在画面上保留的时长（毫秒）
const int64_t kShareCaptionFinalHoldMs = 4000;
//中间结果多久没有更新就不再显示（毫秒）：对方断线或识别中断时不至于一直挂在画面上
const int64_t kShareCaptionPartialStaleMs = 8000;
//同时最多显示几个人的字幕
const size_t kShareCaptionMaxSpeakers = 2;

/**
 *  发送端的中间结果限速。
 */
class ShareCaptionThrottle
{
public:
    ShareCaptionThrottle();

    /**
     *  这一条现在能不能发。最终结果总是可以；中间结果距上一次发出的中间结果不足 1000 / kShareCaptionPartialPerSec 毫秒时不发。
     *  能发时记下时刻。
     *  @param final 是否为最终结果
     *  @param nowMs 当前时刻（毫秒，单调递增）
     */
    bool Allow(bool final, int64_t nowMs);

    //重新开始（打开字幕时）
    void Reset();

private:
    int64_t m_lastPartialMs;   //上一次发出中间结果的时刻；-1 表示还没发过
};

//画面上的一行字幕
struct ShareCaptionLine
{
    int          m_seat;        //发言人的座位号
    std::wstring m_speaker;     //发言人显示名
    std::wstring m_text;        //文字
    bool         m_partial;     //是否为中间结果
    uint32_t     m_sentenceId;  //句子编号
    int64_t      m_updatedMs;   //最近一次更新的时刻

    ShareCaptionLine()
        : m_seat(-1)
        , m_partial(false)
        , m_sentenceId(0)
        , m_updatedMs(0)
    {
    }
};

/**
 *  接收端各座位当前的字幕。只在 UI 线程上使用。
 */
class ShareCaptionBoard
{
public:
    /**
     *  收到一条字幕（含本人自己的识别结果）。同一座位只保留最近的一句：句子编号较旧的中间结果忽略（乱序到达）。
     *  @param seat       发言人座位号
     *  @param speaker    发言人显示名
     *  @param text       文字；为空的中间结果表示这一句被清空
     *  @param final      是否为最终结果
     *  @param sentenceId 句子编号
     *  @param nowMs      当前时刻
     *  @return 画面内容有变化返回 true
     */
    bool OnCaption(int seat, const std::wstring& speaker, const std::wstring& text, bool final, uint32_t sentenceId,
                   int64_t nowMs);

    /**
     *  清掉过期的行（最终结果超过 kShareCaptionFinalHoldMs、中间结果超过 kShareCaptionPartialStaleMs）。
     *  @return 有行被清掉返回 true
     */
    bool Expire(int64_t nowMs);

    /**
     *  要显示的行：最多 kShareCaptionMaxSpeakers 个人，取最近更新的，按更新先后排列（较早的在前，显示在上面）。
     */
    std::vector<ShareCaptionLine> Visible() const;

    //某个成员离开房间
    void RemoveSeat(int seat);

    //清空（关闭字幕显示、离开共享时）
    void Clear();

    //是否没有任何字幕
    bool Empty() const;

private:
    std::map<int, ShareCaptionLine> m_lines;   //座位号 → 这个人当前的字幕
};

/**
 *  录制共享时别人的一句最终结果在录制时间轴上的起止时刻：结束为收到时的录制时刻，开始为结束减去这句的时长（不早于 0）。
 *  时长为 0 时按 kShareCaptionMinRemoteMs 处理，免得字幕一闪而过。
 *  @param receivedAtMs 收到这一帧时的录制时刻（毫秒，不含暂停）
 *  @param durationMs   帧里带的时长
 *  @param startMs      [出参] 开始
 *  @param endMs        [出参] 结束
 */
void ShareCaptionRemoteTimes(int64_t receivedAtMs, uint32_t durationMs, int64_t& startMs, int64_t& endMs);

//别人的一句没有带时长时，在录像里至少显示的时长（毫秒）
const int64_t kShareCaptionMinRemoteMs = 1000;

} // namespace screenshare
