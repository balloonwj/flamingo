/**
 *  首屏历史渲染的分段耗时统计：按消息类型累计渲染耗时，另记渲染前的准备（清空列表）
 *  与渲染后的收尾（滚到底、定位等）两段。单聊窗与群聊窗的 OnHistoryMessages 用它
 *  写一行 *-HISTORY-BREAKDOWN 跟踪（DuiTrace 打开时才输出），用来判断时间花在哪一类消息上。
 *
 *  典型用法：
 *      balloonchatwnd::HistoryRenderBreakdown breakdown;
 *      PerfStopWatch itemWatch;
 *      ...渲染一条文字消息...
 *      breakdown.Add(balloonchatwnd::kHistoryRenderText, itemWatch.ElapsedMs());
 *
 *  balloonwj@qq.com   2026-10-01
 */

#pragma once

namespace balloonchatwnd {

//首屏历史里一条消息按哪一类统计渲染耗时
enum HistoryRenderKind
{
    //文字消息，以及用占位文字渲染的其它非文件类消息
    kHistoryRenderText = 0,
    //图片消息
    kHistoryRenderImage,
    //文件消息
    kHistoryRenderFile,
    //文件夹传输记录
    kHistoryRenderFolder,
    //类别总数，只用作数组长度
    kHistoryRenderKindCount
};

//一次首屏历史渲染的分段耗时
struct HistoryRenderBreakdown
{
    //各类消息的条数
    unsigned int m_count[kHistoryRenderKindCount];
    //各类消息的渲染耗时合计，单位毫秒
    double       m_ms[kHistoryRenderKindCount];
    //逐条渲染之前的准备耗时（清空列表或开始头部插入批次），单位毫秒
    double       m_prepareMs;
    //逐条渲染之后的收尾耗时（滚到底、定位、补文件夹邀请等），单位毫秒
    double       m_finishMs;

    HistoryRenderBreakdown()
        : m_prepareMs(0.0)
        , m_finishMs(0.0)
    {
        for (int i = 0; i < kHistoryRenderKindCount; ++i)
        {
            m_count[i] = 0;
            m_ms[i] = 0.0;
        }
    }

    /**
     *  记一条消息的渲染耗时。
     *  @param kind 消息类别；超出范围时忽略
     *  @param ms   这一条的渲染耗时，单位毫秒
     */
    void Add(HistoryRenderKind kind, double ms)
    {
        if (kind < 0 || kind >= kHistoryRenderKindCount)
            return;
        ++m_count[kind];
        m_ms[kind] += ms;
    }
};

} // namespace balloonchatwnd
