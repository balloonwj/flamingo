/**
 *  P2P 链路重放窗口的实现，见 P2PReplayWindow.h。
 *  balloonwj@qq.com   2026-10-04
 */
#include "P2PReplayWindow.h"

namespace p2p {

namespace {

//位图每组的位数
const uint64_t kBitsPerWord = 64;

//位图的组数
const size_t kWordCount = (size_t)(kP2PReplayWindowSize / kBitsPerWord);

} // namespace

P2PReplayWindow::P2PReplayWindow()
    : m_highest(0)
    , m_bits(kWordCount, 0)
{
}

bool P2PReplayWindow::IsAcceptable(uint64_t counter) const
{
    //计数器从 1 开始，0 一定是伪造或出错的报文
    if (counter == 0)
        return false;
    if (counter > m_highest)
        return true;
    //落后窗口大小及以上：太旧，无法判断是否收到过，一律拒绝
    if (m_highest - counter >= kP2PReplayWindowSize)
        return false;
    size_t word = 0;
    uint64_t bit = 0;
    BitOf(counter, word, bit);
    return (m_bits[word] & bit) == 0;
}

void P2PReplayWindow::Accept(uint64_t counter)
{
    if (counter == 0)
        return;
    if (counter > m_highest)
    {
        //窗口前移：新进入窗口的那些位置原先记的是更早的计数器，要清空
        if (counter - m_highest >= kP2PReplayWindowSize)
        {
            for (size_t i = 0; i < m_bits.size(); ++i)
                m_bits[i] = 0;
        }
        else
        {
            for (uint64_t c = m_highest + 1; c < counter; ++c)
            {
                size_t word = 0;
                uint64_t bit = 0;
                BitOf(c, word, bit);
                m_bits[word] &= ~bit;
            }
        }
        m_highest = counter;
    }
    size_t word = 0;
    uint64_t bit = 0;
    BitOf(counter, word, bit);
    m_bits[word] |= bit;
}

void P2PReplayWindow::BitOf(uint64_t counter, size_t& word, uint64_t& bit)
{
    const uint64_t index = counter % kP2PReplayWindowSize;
    word = (size_t)(index / kBitsPerWord);
    bit = (uint64_t)1 << (index % kBitsPerWord);
}

} // namespace p2p
