/**
 *  P2P 链路的重放窗口（docs/P2P通信方案.md 7.2 节，第 3 期 3a）。
 *
 *  接收方向上记住最近 kP2PReplayWindowSize 个计数器是否已经收到过：重复的计数器、以及比已收到的最大计数器落后
 *  超过窗口大小的计数器一律拒绝；窗口内乱序到达的报文照常接受。UDP 会乱序、会重复，中转路径不会，但两条路径
 *  用同一套检查。
 *
 *  用法分两步，以免认证失败的伪造报文把窗口推着走：先 IsAcceptable 判断（便宜，在解密之前），认证通过后再
 *  Accept 记下。计数器从 1 开始（0 不合法）。
 *
 *  典型用法（P2PLinkCipher::Open）：
 *      if (!m_replay.IsAcceptable(header.m_counter))
 *          return p2p_open_replayed;
 *      ...解密认证通过后
 *      m_replay.Accept(header.m_counter);
 *
 *  只依赖标准库，单线程使用（链路的报文都在 IO 线程上处理）。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stdint.h>
#include <vector>

namespace p2p {

//重放窗口的大小（个报文）：方案 7.2 节暂定 1024，须为 64 的倍数（按 64 位一组存放）
const uint64_t kP2PReplayWindowSize = 1024;

/**
 *  一个接收方向的重放窗口。
 */
class P2PReplayWindow
{
public:
    P2PReplayWindow();

    /**
     *  这个计数器能否接受（不修改窗口）。
     *  @param counter 报文头里的计数器
     *  @return 计数器为 0、已经收到过、或落后已收到的最大计数器 kP2PReplayWindowSize 个及以上时返回 false
     */
    bool IsAcceptable(uint64_t counter) const;

    /**
     *  记下一个已通过认证的计数器；比当前最大值大时窗口随之前移。调用前须先经 IsAcceptable 判断。
     *  @param counter 报文头里的计数器
     */
    void Accept(uint64_t counter);

    //已接受的最大计数器；一个都没接受过时为 0
    uint64_t Highest() const { return m_highest; }

private:
    //计数器 counter 在位图里的位置：第 word 个 64 位组的第 bit 位
    static void BitOf(uint64_t counter, size_t& word, uint64_t& bit);

private:
    uint64_t              m_highest;   //已接受的最大计数器；0 表示还没有接受过任何报文
    std::vector<uint64_t> m_bits;      //位图：计数器 c 在窗口内时，第 (c % 窗口大小) 位表示是否已收到
};

} // namespace p2p
