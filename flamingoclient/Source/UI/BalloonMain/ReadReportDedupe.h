/**
 *  会话已读位点上报的去重：记下每个会话最近一次上报给服务端的已读位点，同一个（或更小的）
 *  位点不再重复上报。
 *
 *  背景：从会话列表打开会话时，点击处理与新窗口被激活各调一次
 *  CBalloonMainPanel::MarkConversationRead；双击会话列表时这一套还会执行两遍（树控件每次
 *  鼠标抬起都发一次"单击"通知）。于是打开一次群聊窗，服务端会收到 2 到 4 条相同的已读上报，
 *  每条都要写一次库，单聊还会给对方多推一次"已读"通知。服务端的已读位点只往前推进
 *  （GREATEST(f_readmsgid, 新值)），同样或更小的值报了也不起作用。
 *
 *  只做判定、不发请求，也不影响本地清未读与托盘停闪（那些每次照常执行）。
 *  重新登录、断线重连成功、登出时须调 Reset：断线期间发出的上报可能被丢弃，
 *  清空记录后下次打开该会话会重新上报。
 *
 *  典型用法（MarkConversationRead 内）：
 *      if (m_readReportDedupe.ShouldReport(ctype, cid, readmsgid))
 *          CIUSocket::GetInstance().ReportConversationRead(ctype, cid, readmsgid);
 *
 *  <u>本文件被 FlamingoUnit 测试工程直接编译</u>，故 .cpp 不走主工程的预编译头。
 *  测试见 Tests/ReadReportDedupeTests.cpp（标签 [readreport]）。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#pragma once

#include <stdint.h>
#include <map>
#include <utility>

namespace balloonmain {

//会话已读位点上报的去重记录。只在 UI 线程上使用，不加锁
class ReadReportDedupe
{
public:
    /**
     *  这次是否需要把已读位点上报给服务端。需要时同时记下这个位点。
     *  @param ctype     会话类型（1单聊 2群聊）
     *  @param cid       会话ID（单聊=对端 userid，群聊=群 id）
     *  @param readmsgid 本次要上报的已读位点
     *  @return 位点大于 0 且大于该会话上次上报的位点时返回 true；否则返回 false，且不改动记录
     */
    bool ShouldReport(int ctype, int64_t cid, int64_t readmsgid);

    //清空全部记录。重新登录、断线重连成功、登出时调用
    void Reset();

private:
    //(会话类型, 会话ID) → 最近一次上报的已读位点
    std::map<std::pair<int, int64_t>, int64_t> m_lastReported;
};

} // namespace balloonmain
