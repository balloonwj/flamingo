/**
 *  同一文件只上传一份的门闩单元测试（标签 [uploadgate]），UploadInflightGateTests.cpp
 *  背景：同一个文件一次发给多个目标（含群聊）时，单聊串行 worker 与群文件并发池
 *  会同时申请上传同一 MD5，同一份字节被并行传好几遍。门闩让后来者等前一笔传完再申请，命中秒传。
 *  覆盖：
 *    - FTD1 同一 MD5：后来者等前一笔离开后才进入（结果为 enter_after_wait），大小写不同视为同一 MD5；
 *    - FTD2 不同 MD5 互不等待（结果为 enter_first）；
 *    - FTD3 前一笔中途失败提前返回时，UploadInflightGuard 析构即离开门闩，等待者立即进入；
 *    - FTD4 等待超时后照常进入（enter_timeout），超时进入的多笔都离开后才算空闲；
 *           等待期间取消标志置位则放弃（enter_cancelled），不占门闩。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-06
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include "../net/UploadInflightGate.h"

namespace {

//测试用的文件 MD5
const char* const kFtdMd5 = "0123456789abcdef0123456789abcdef";
//同一 MD5 的大写形式
const char* const kFtdMd5Upper = "0123456789ABCDEF0123456789ABCDEF";
//另一个文件的 MD5
const char* const kFtdOtherMd5 = "ffffffffffffffffffffffffffffffff";
//等待者的最长等待时间（毫秒）：远大于用例里的人为延迟，正常情况下不会超时
const int kFtdLongWaitMs = 10000;
//超时用例的最长等待时间（毫秒）
const int kFtdShortWaitMs = 300;
//人为延迟（毫秒）：确认等待者此时确实还在等
const int kFtdHoldMs = 200;
//取消用例里置位取消标志前的延迟（毫秒）
const int kFtdCancelAfterMs = 150;
//检查后台线程是否已返回的轮询上限（毫秒）
const int kFtdJoinPollMs = 5000;
//轮询间隔（毫秒）
const int kFtdPollStepMs = 10;

//后台等待者：在门闩上等待同一 MD5，返回后记下结果与耗时
struct FtdWaiter
{
    net::UploadInflightGate*         m_gate;      //被测门闩，不持有所有权
    std::string                      m_md5;       //要进入的 MD5
    int                              m_timeoutMs; //最长等待时间（毫秒）
    const std::atomic<bool>*         m_cancel;    //取消标志，可为 NULL
    std::atomic<bool>                m_done;      //Enter 是否已返回
    std::atomic<int>                 m_result;    //Enter 的结果
    std::atomic<long long>           m_elapsedMs; //Enter 的耗时（毫秒）

    FtdWaiter(net::UploadInflightGate* gate, const std::string& md5, int timeoutMs,
              const std::atomic<bool>* cancel)
        : m_gate(gate), m_md5(md5), m_timeoutMs(timeoutMs), m_cancel(cancel),
          m_done(false), m_result(-1), m_elapsedMs(0)
    {
    }
};

//等待者线程函数：调用 Enter 并记录结果。成功进入的不在这里离开，由用例在断言后离开
void FtdWaiterProc(FtdWaiter* w)
{
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    net::UploadInflightGate::enter_result r = w->m_gate->Enter(w->m_md5, w->m_timeoutMs, w->m_cancel);
    w->m_elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    w->m_result = static_cast<int>(r);
    w->m_done = true;
}

//轮询等待某个等待者返回，最多等 kFtdJoinPollMs 毫秒；返回它是否已返回
bool FtdWaitDone(const FtdWaiter& w)
{
    for (int waited = 0; waited < kFtdJoinPollMs; waited += kFtdPollStepMs)
    {
        if (w.m_done.load())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(kFtdPollStepMs));
    }
    return w.m_done.load();
}

//模拟一笔中途失败的上传：持有门闩后提前返回，依靠 UploadInflightGuard 析构离开
bool FtdFailingUpload(net::UploadInflightGate& gate, std::atomic<bool>* entered, std::atomic<bool>* release)
{
    net::UploadInflightGuard guard(gate, kFtdMd5, kFtdLongWaitMs, NULL);
    *entered = true;
    while (!release->load())
        std::this_thread::sleep_for(std::chrono::milliseconds(kFtdPollStepMs));
    //模拟申请上传被拒：直接返回失败，不显式调用 Leave
    return false;
}

//FTD3 里执行失败上传的线程函数
void FtdFailingUploadProc(net::UploadInflightGate* gate, std::atomic<bool>* entered, std::atomic<bool>* release)
{
    FtdFailingUpload(*gate, entered, release);
}

} // namespace

TEST_CASE("FTD1 同一 MD5：后来者等前一笔离开后才进入", "[uploadgate]")
{
    net::UploadInflightGate gate;
    REQUIRE(gate.Enter(kFtdMd5, kFtdLongWaitMs, NULL) == net::UploadInflightGate::enter_first);
    CHECK(gate.IsInflight(kFtdMd5));
    CHECK(gate.IsInflight(kFtdMd5Upper));

    //大写形式视为同一 MD5，同样要等
    FtdWaiter w(&gate, kFtdMd5Upper, kFtdLongWaitMs, NULL);
    std::thread t(FtdWaiterProc, &w);
    std::this_thread::sleep_for(std::chrono::milliseconds(kFtdHoldMs));
    CHECK_FALSE(w.m_done.load());

    gate.Leave(kFtdMd5);
    REQUIRE(FtdWaitDone(w));
    t.join();
    CHECK(w.m_result.load() == net::UploadInflightGate::enter_after_wait);
    CHECK(w.m_elapsedMs.load() >= kFtdHoldMs / 2);
    CHECK(gate.IsInflight(kFtdMd5));

    gate.Leave(kFtdMd5Upper);
    CHECK_FALSE(gate.IsInflight(kFtdMd5));
}

TEST_CASE("FTD2 不同 MD5 互不等待", "[uploadgate]")
{
    net::UploadInflightGate gate;
    REQUIRE(gate.Enter(kFtdMd5, kFtdLongWaitMs, NULL) == net::UploadInflightGate::enter_first);

    FtdWaiter w(&gate, kFtdOtherMd5, kFtdLongWaitMs, NULL);
    std::thread t(FtdWaiterProc, &w);
    REQUIRE(FtdWaitDone(w));
    t.join();
    CHECK(w.m_result.load() == net::UploadInflightGate::enter_first);
    CHECK(w.m_elapsedMs.load() < kFtdHoldMs);
    CHECK(gate.IsInflight(kFtdMd5));
    CHECK(gate.IsInflight(kFtdOtherMd5));

    gate.Leave(kFtdOtherMd5);
    gate.Leave(kFtdMd5);
    CHECK_FALSE(gate.IsInflight(kFtdMd5));
    CHECK_FALSE(gate.IsInflight(kFtdOtherMd5));
}

TEST_CASE("FTD3 前一笔中途失败，守卫析构即放行等待者", "[uploadgate]")
{
    net::UploadInflightGate gate;
    std::atomic<bool> entered(false);
    std::atomic<bool> release(false);
    std::thread failing(FtdFailingUploadProc, &gate, &entered, &release);
    for (int waited = 0; waited < kFtdJoinPollMs && !entered.load(); waited += kFtdPollStepMs)
        std::this_thread::sleep_for(std::chrono::milliseconds(kFtdPollStepMs));
    REQUIRE(entered.load());
    CHECK(gate.IsInflight(kFtdMd5));

    FtdWaiter w(&gate, kFtdMd5, kFtdLongWaitMs, NULL);
    std::thread t(FtdWaiterProc, &w);
    std::this_thread::sleep_for(std::chrono::milliseconds(kFtdHoldMs));
    CHECK_FALSE(w.m_done.load());

    //失败的那一笔提前返回，守卫析构后等待者应立即进入，而不是等到超时
    release = true;
    failing.join();
    REQUIRE(FtdWaitDone(w));
    t.join();
    CHECK(w.m_result.load() == net::UploadInflightGate::enter_after_wait);
    CHECK(w.m_elapsedMs.load() < kFtdLongWaitMs);

    gate.Leave(kFtdMd5);
    CHECK_FALSE(gate.IsInflight(kFtdMd5));
}

TEST_CASE("FTD4 等待超时照常进入，等待中取消则放弃", "[uploadgate]")
{
    net::UploadInflightGate gate;
    REQUIRE(gate.Enter(kFtdMd5, kFtdLongWaitMs, NULL) == net::UploadInflightGate::enter_first);

    SECTION("超时后照常进入，两笔都离开后才空闲")
    {
        FtdWaiter w(&gate, kFtdMd5, kFtdShortWaitMs, NULL);
        std::thread t(FtdWaiterProc, &w);
        REQUIRE(FtdWaitDone(w));
        t.join();
        CHECK(w.m_result.load() == net::UploadInflightGate::enter_timeout);
        CHECK(w.m_elapsedMs.load() >= kFtdShortWaitMs - kFtdPollStepMs);

        gate.Leave(kFtdMd5);
        CHECK(gate.IsInflight(kFtdMd5));
        gate.Leave(kFtdMd5);
        CHECK_FALSE(gate.IsInflight(kFtdMd5));
    }

    SECTION("等待期间置位取消标志：放弃进入，不占门闩")
    {
        std::atomic<bool> cancel(false);
        FtdWaiter w(&gate, kFtdMd5, kFtdLongWaitMs, &cancel);
        std::thread t(FtdWaiterProc, &w);
        std::this_thread::sleep_for(std::chrono::milliseconds(kFtdCancelAfterMs));
        CHECK_FALSE(w.m_done.load());
        cancel = true;
        REQUIRE(FtdWaitDone(w));
        t.join();
        CHECK(w.m_result.load() == net::UploadInflightGate::enter_cancelled);
        CHECK(w.m_elapsedMs.load() < kFtdLongWaitMs);

        //只剩最初那一笔持有者，它离开后即空闲
        gate.Leave(kFtdMd5);
        CHECK_FALSE(gate.IsInflight(kFtdMd5));
    }

    SECTION("不等待：有持有者时立即按超时进入")
    {
        CHECK(gate.Enter(kFtdMd5, 0, NULL) == net::UploadInflightGate::enter_timeout);
        gate.Leave(kFtdMd5);
        gate.Leave(kFtdMd5);
        CHECK_FALSE(gate.IsInflight(kFtdMd5));
    }
}
