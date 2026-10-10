/**
 *  UploadInflightGate.cpp —— 同一文件只上传一份的门闩实现，接口说明见 UploadInflightGate.h。
 *  balloonwj@qq.com   2026-10-06
 */

#include "stdafx.h"
#include "UploadInflightGate.h"

#include <chrono>

namespace net {

//等待期间检查取消标志的间隔（毫秒）。条件变量只在持有者离开时被通知，取消标志的置位
//不会唤醒等待者，所以按这个间隔醒来检查一次；取 100 毫秒，用户点「取消」后感觉不到延迟
static const int kCancelPollMs = 100;

//MD5 统一转成小写作键，使大小写不同的同一 MD5 落到同一项
static std::string NormalizeMd5(const std::string& md5)
{
    std::string key(md5);
    for (size_t i = 0; i < key.size(); ++i)
    {
        if (key[i] >= 'A' && key[i] <= 'Z')
            key[i] = static_cast<char>(key[i] - 'A' + 'a');
    }
    return key;
}

UploadInflightGate::UploadInflightGate()
{
}

UploadInflightGate::enter_result UploadInflightGate::Enter(const std::string& md5, int timeoutMs,
                                                           const std::atomic<bool>* cancel)
{
    const std::string key = NormalizeMd5(md5);
    std::unique_lock<std::mutex> lock(m_mtx);

    std::map<std::string, int>::iterator it = m_inflight.find(key);
    if (it == m_inflight.end())
    {
        m_inflight[key] = 1;
        return enter_first;
    }
    //不等待：有持有者时立即按超时进入，与它并行上传
    if (timeoutMs <= 0)
    {
        ++it->second;
        return enter_timeout;
    }

    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true)
    {
        //用户已取消：放弃进入，不占门闩，由调用方按取消回投
        if (cancel != NULL && cancel->load())
            return enter_cancelled;

        it = m_inflight.find(key);
        if (it == m_inflight.end())
        {
            m_inflight[key] = 1;
            return enter_after_wait;
        }

        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (now >= deadline)
        {
            ++it->second;
            return enter_timeout;
        }

        //最多睡到截止时刻，且不超过取消检查间隔
        std::chrono::milliseconds slice = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        if (slice > std::chrono::milliseconds(kCancelPollMs))
            slice = std::chrono::milliseconds(kCancelPollMs);
        if (slice < std::chrono::milliseconds(1))
            slice = std::chrono::milliseconds(1);
        m_cv.wait_for(lock, slice);
    }
}

void UploadInflightGate::Leave(const std::string& md5)
{
    std::lock_guard<std::mutex> guard(m_mtx);
    std::map<std::string, int>::iterator it = m_inflight.find(NormalizeMd5(md5));
    if (it == m_inflight.end())
        return;
    --it->second;
    if (it->second <= 0)
    {
        m_inflight.erase(it);
        //可能有多个 MD5 的等待者共用这一个条件变量，全部唤醒后各自重新检查自己的 MD5
        m_cv.notify_all();
    }
}

bool UploadInflightGate::IsInflight(const std::string& md5) const
{
    std::lock_guard<std::mutex> guard(m_mtx);
    return m_inflight.find(NormalizeMd5(md5)) != m_inflight.end();
}

UploadInflightGuard::UploadInflightGuard(UploadInflightGate& gate, const std::string& md5, int timeoutMs,
                                         const std::atomic<bool>* cancel)
    : m_gate(gate),
      m_md5(md5),
      m_result(gate.Enter(md5, timeoutMs, cancel))
{
}

UploadInflightGuard::~UploadInflightGuard()
{
    //等待中被取消的那一笔没有进入门闩，不能离开，否则会把别人的持有计数减掉
    if (m_result != UploadInflightGate::enter_cancelled)
        m_gate.Leave(m_md5);
}

UploadInflightGate::enter_result UploadInflightGuard::Result() const
{
    return m_result;
}

} // namespace net
