/**
 *  单聊「收发文件」取消簿记单元测试（Suite CTCR），ChatTransferCancelRegistryTests.cpp
 *
 *  覆盖 net::ChatTransferCancelRegistry 的纯逻辑边界：
 *    CTCR1 登记后拿到未置位的标志，重复登记沿用同一个标志对象；
 *    CTCR2 取消在册的发送 → 标志被置位、查询命中；
 *    CTCR3 取消未知 id → 返回 false 且不误伤其它在册任务；
 *    CTCR4 重复取消幂等；
 *    CTCR5 收尾后移出簿记，再取消不再命中；
 *    CTCR6 收尾后 worker 手上那份标志仍然有效（不悬垂）；
 *    CTCR7 上传（按 clientMsgId）与下载（按 msgid）两张表互不干扰；
 *    CTCR8 非法键（空串 / 非正 msgid）不登记，返回空指针；
 *    CTCR9 先取消、后重复登记：置位状态保留，worker 取出时仍判定为已取消。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-03
 */
#include "catch_amalgamated.hpp"
#include "../net/ChatTransferCancelRegistry.h"

using namespace net;

TEST_CASE("CTCR1 登记后拿到未置位标志，重复登记沿用同一对象")
{
    ChatTransferCancelRegistry reg;

    std::shared_ptr<std::atomic<bool> > f1 = reg.RegisterSend("cm-1");
    REQUIRE(f1);
    REQUIRE(f1->load() == false);
    REQUIRE(reg.SendCount() == 1);

    //同一 clientMsgId 再登记一次：必须是同一个标志对象，否则取消会置错那一个
    std::shared_ptr<std::atomic<bool> > f2 = reg.RegisterSend("cm-1");
    REQUIRE(f2.get() == f1.get());
    REQUIRE(reg.SendCount() == 1);

    //下载侧同理
    std::shared_ptr<std::atomic<bool> > d1 = reg.RegisterDownload(1001);
    std::shared_ptr<std::atomic<bool> > d2 = reg.RegisterDownload(1001);
    REQUIRE(d1);
    REQUIRE(d2.get() == d1.get());
    REQUIRE(reg.DownloadCount() == 1);
}

TEST_CASE("CTCR2 取消在册任务：标志置位且查询命中")
{
    ChatTransferCancelRegistry reg;
    std::shared_ptr<std::atomic<bool> > flag = reg.RegisterSend("cm-1");

    REQUIRE(reg.IsSendCancelled("cm-1") == false);
    REQUIRE(reg.CancelSend("cm-1"));
    REQUIRE(reg.IsSendCancelled("cm-1"));
    //worker 正是通过这份 shared_ptr 读到取消的
    REQUIRE(flag->load());

    std::shared_ptr<std::atomic<bool> > dflag = reg.RegisterDownload(2002);
    REQUIRE(reg.IsDownloadCancelled(2002) == false);
    REQUIRE(reg.CancelDownload(2002));
    REQUIRE(reg.IsDownloadCancelled(2002));
    REQUIRE(dflag->load());
}

TEST_CASE("CTCR3 取消未知 id 返回 false 且不误伤其它任务")
{
    ChatTransferCancelRegistry reg;
    std::shared_ptr<std::atomic<bool> > flag = reg.RegisterSend("cm-1");

    REQUIRE(reg.CancelSend("cm-never-existed") == false);
    REQUIRE(reg.CancelDownload(999999) == false);

    //在册那笔不受影响
    REQUIRE(flag->load() == false);
    REQUIRE(reg.IsSendCancelled("cm-1") == false);

    //未知 id 的查询一律 false，不抛不崩
    REQUIRE(reg.IsSendCancelled("cm-never-existed") == false);
    REQUIRE(reg.IsDownloadCancelled(999999) == false);
}

TEST_CASE("CTCR4 重复取消幂等")
{
    ChatTransferCancelRegistry reg;
    reg.RegisterSend("cm-1");
    reg.RegisterDownload(3003);

    REQUIRE(reg.CancelSend("cm-1"));
    REQUIRE(reg.CancelSend("cm-1"));   //用户连点两次「取消」
    REQUIRE(reg.IsSendCancelled("cm-1"));

    REQUIRE(reg.CancelDownload(3003));
    REQUIRE(reg.CancelDownload(3003));
    REQUIRE(reg.IsDownloadCancelled(3003));
}

TEST_CASE("CTCR5 收尾后移出簿记，再取消不再命中")
{
    ChatTransferCancelRegistry reg;
    reg.RegisterSend("cm-1");
    reg.RegisterDownload(4004);
    REQUIRE(reg.SendCount() == 1);
    REQUIRE(reg.DownloadCount() == 1);

    reg.CompleteSend("cm-1");
    reg.CompleteDownload(4004);
    REQUIRE(reg.SendCount() == 0);
    REQUIRE(reg.DownloadCount() == 0);

    //已完成的任务再点取消：查不到，不该被当成"取消成功"
    REQUIRE(reg.CancelSend("cm-1") == false);
    REQUIRE(reg.CancelDownload(4004) == false);
    REQUIRE(reg.IsSendCancelled("cm-1") == false);
    REQUIRE(reg.IsDownloadCancelled(4004) == false);
}

TEST_CASE("CTCR6 收尾后 worker 手上的标志仍有效")
{
    ChatTransferCancelRegistry reg;
    //模拟 worker：入队时拿到标志并一直持有
    std::shared_ptr<std::atomic<bool> > workerFlag = reg.RegisterSend("cm-1");

    //UI 线程取消后，worker 尚未读到时任务就走到了收尾（真实时序里可能发生）
    REQUIRE(reg.CancelSend("cm-1"));
    reg.CompleteSend("cm-1");
    REQUIRE(reg.SendCount() == 0);

    //表已清空，但 worker 手上这份不能悬垂，且置位状态保留
    REQUIRE(workerFlag);
    REQUIRE(workerFlag->load());
}

TEST_CASE("CTCR7 上传与下载两张表互不干扰")
{
    ChatTransferCancelRegistry reg;
    reg.RegisterSend("777");     //故意与下面的 msgid 数值一致，验证不会串表
    reg.RegisterDownload(777);

    REQUIRE(reg.CancelSend("777"));
    REQUIRE(reg.IsSendCancelled("777"));
    //取消上传不该波及同数值的下载
    REQUIRE(reg.IsDownloadCancelled(777) == false);

    REQUIRE(reg.CancelDownload(777));
    REQUIRE(reg.IsDownloadCancelled(777));

    //收尾也各归各的
    reg.CompleteSend("777");
    REQUIRE(reg.SendCount() == 0);
    REQUIRE(reg.DownloadCount() == 1);
}

TEST_CASE("CTCR8 非法键不登记")
{
    ChatTransferCancelRegistry reg;

    //空 clientMsgId 无法与任何气泡对回
    REQUIRE(!reg.RegisterSend(""));
    REQUIRE(reg.SendCount() == 0);

    //msgid 是服务器分配的正整数
    REQUIRE(!reg.RegisterDownload(0));
    REQUIRE(!reg.RegisterDownload(-1));
    REQUIRE(reg.DownloadCount() == 0);

    //对非法键的取消 / 查询同样安全
    REQUIRE(reg.CancelSend("") == false);
    REQUIRE(reg.CancelDownload(0) == false);
    REQUIRE(reg.IsSendCancelled("") == false);
    REQUIRE(reg.IsDownloadCancelled(0) == false);
}

TEST_CASE("CTCR9 先取消后重复登记：置位状态保留")
{
    ChatTransferCancelRegistry reg;
    reg.RegisterSend("cm-1");
    REQUIRE(reg.CancelSend("cm-1"));

    //同一 clientMsgId 又被入队一次（未收尾）：沿用原标志，取消状态不该被悄悄清掉，
    //否则 worker 取出时会以为没被取消、继续传下去
    std::shared_ptr<std::atomic<bool> > again = reg.RegisterSend("cm-1");
    REQUIRE(again);
    REQUIRE(again->load());
    REQUIRE(reg.IsSendCancelled("cm-1"));
}
