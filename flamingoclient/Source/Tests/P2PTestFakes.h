/**
 *  P2P 业务方单元测试共用的假对象（P2P 方案第 6 期从 FolderP2PTransportTests.cpp 抽出），P2PTestFakes.h
 *
 *  - FakeLink    假链路：记录打开的通道与发出的字节，引用计数与 Close 次数可查，可让接下来几次 Send 返回 would_block；
 *  - WithLength  按 P2P 帧通道的格式给一帧加 4 字节长度（网络字节序）。
 *
 *  典型用法（FolderP2PTransportTests.cpp、RdTransportTests.cpp）：
 *      p2ptest::FakeLink link;
 *      broker.OnLinkReady("s1", &link);
 *      ...
 *      CHECK(link.m_sent == p2ptest::WithLength(frame));
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "../../FlamingoP2P/FlamingoP2PApi.h"

namespace p2ptest {

//一个字节的位数与掩码（拼长度字段）
const int kFakeBitsPerByte = 8;
const uint32_t kFakeByteMask = 0xFF;

/**
 *  按网络字节序给一帧加 4 字节长度。
 *  @param frame 帧数据
 *  @return 长度字段 + 帧数据
 */
inline std::string WithLength(const std::string& frame)
{
    std::string out;
    const uint32_t len = (uint32_t)frame.size();
    out.push_back((char)((len >> (3 * kFakeBitsPerByte)) & kFakeByteMask));
    out.push_back((char)((len >> (2 * kFakeBitsPerByte)) & kFakeByteMask));
    out.push_back((char)((len >> kFakeBitsPerByte) & kFakeByteMask));
    out.push_back((char)(len & kFakeByteMask));
    return out + frame;
}

/**
 *  假链路：记录打开的通道与发出的字节，引用计数与 Close 次数可查。测试里对象本身不随引用释放而销毁，
 *  须比持有它的链路槽活得久。
 */
class FakeLink : public p2p::IP2PLink
{
public:
    FakeLink()
        : m_refs(0)
        , m_closes(0)
        , m_closeReason(0)
        , m_sink(NULL)
        , m_blockSends(0)
        , m_statsPath(0)
    {
    }

    virtual int OpenChannel(uint16_t channelId, int kind, p2p::IP2PChannelSink* sink) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_channels.push_back(channelId);
        m_kinds.push_back(kind);
        m_sink = sink;
        return p2p::p2p_send_ok;
    }

    virtual int Send(uint16_t channelId, const uint8_t* data, uint32_t len) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_blockSends > 0)
        {
            --m_blockSends;
            return p2p::p2p_send_would_block;
        }
        m_sentChannels.push_back(channelId);
        m_sent.append(reinterpret_cast<const char*>(data), len);
        return p2p::p2p_send_ok;
    }

    virtual void GetStats(p2p::P2PLinkStats* out) override
    {
        //只填链路路径（P2P 徽章的用例用到），其余统计保持调用方清零后的值
        if (out != NULL)
            out->m_path = m_statsPath;
    }

    virtual void Close(int reason) override
    {
        ++m_closes;
        m_closeReason = reason;
    }

    virtual void AddRef() override
    {
        ++m_refs;
    }

    virtual void Release() override
    {
        --m_refs;
    }

    //已发出的全部字节（加锁取一份副本，供别的线程还在发送时读取）
    std::string SentCopy()
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_sent;
    }

    std::mutex               m_mutex;          //保护以下容器
    std::atomic<int>         m_refs;           //当前引用数
    std::atomic<int>         m_closes;         //Close 的次数
    std::atomic<int>         m_closeReason;    //最近一次 Close 的原因
    std::atomic<int>         m_statsPath;      //GetStats 报出的链路路径（取值见 p2p_link_path），用例按需设置
    std::vector<uint16_t>    m_channels;       //打开过的通道号
    std::vector<int>         m_kinds;          //打开时的通道类型
    p2p::IP2PChannelSink*    m_sink;           //最近一次打开时交来的接收端
    std::vector<uint16_t>    m_sentChannels;   //每次 Send 的通道号
    std::string              m_sent;           //发出的全部字节
    int                      m_blockSends;     //接下来多少次 Send 返回 would_block
};

} // namespace p2ptest
