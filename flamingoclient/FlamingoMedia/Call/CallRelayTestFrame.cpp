/**
 *  音视频通话中转「测试帧」编解码实现，CallRelayTestFrame.cpp
 *  balloonwj@qq.com   2026-06-11
 */
#include "CallRelayTestFrame.h"

#include <string.h>

namespace call {

const char kTestFrameMagic[] = { 'C', 'T', 'S', 'T' };

//载荷字节模式系数：让载荷随序号与位置都变化，使任何字节错位 / 损坏都改变比对结果。
//取一组互质的小奇数，纯为打散字节分布，无特殊数学含义。
static const uint32_t kPayloadSeqFactor = 131u;   //序号对载荷的影响系数
static const uint32_t kPayloadPosFactor = 31u;    //位置对载荷的影响系数
static const uint32_t kPayloadBias      = 17u;    //偏置常量

//按序号与位置算出载荷第 i 字节的确定性取值（构造与校验共用同一算法）。
static unsigned char testFramePayloadByte(uint32_t seq, size_t i)
{
    uint32_t v = seq * kPayloadSeqFactor + static_cast<uint32_t>(i) * kPayloadPosFactor + kPayloadBias;
    return static_cast<unsigned char>(v & 0xFFu);
}

//把 uint32 以大端写入 4 字节（与序号字段定义一致）。
static void writeBigEndianU32(unsigned char* dst, uint32_t v)
{
    dst[0] = static_cast<unsigned char>((v >> 24) & 0xFFu);
    dst[1] = static_cast<unsigned char>((v >> 16) & 0xFFu);
    dst[2] = static_cast<unsigned char>((v >> 8) & 0xFFu);
    dst[3] = static_cast<unsigned char>(v & 0xFFu);
}

//从 4 字节大端读出 uint32。
static uint32_t readBigEndianU32(const unsigned char* src)
{
    return (static_cast<uint32_t>(src[0]) << 24)
         | (static_cast<uint32_t>(src[1]) << 16)
         | (static_cast<uint32_t>(src[2]) << 8)
         | (static_cast<uint32_t>(src[3]));
}

std::string BuildTestFrame(uint32_t seq)
{
    std::string frame;
    frame.resize(kTestFrameTotalLen);
    unsigned char* p = reinterpret_cast<unsigned char*>(&frame[0]);

    //魔数
    memcpy(p, kTestFrameMagic, kTestFrameMagicLen);
    //序号（大端）
    writeBigEndianU32(p + kTestFrameMagicLen, seq);
    //载荷：由序号确定的字节模式
    unsigned char* payload = p + kTestFrameMagicLen + kTestFrameSeqLen;
    for (size_t i = 0; i < kTestFramePayloadLen; ++i)
        payload[i] = testFramePayloadByte(seq, i);

    return frame;
}

bool ParseTestFrame(const unsigned char* data, size_t len, uint32_t& seqOut)
{
    //定长帧：长度不符（含缓冲过短 / 截断）直接判失败，后续读取均在此长度内、不会越界。
    if (data == NULL || len != kTestFrameTotalLen)
        return false;

    //魔数校验
    if (memcmp(data, kTestFrameMagic, kTestFrameMagicLen) != 0)
        return false;

    //取序号
    uint32_t seq = readBigEndianU32(data + kTestFrameMagicLen);

    //载荷逐字节比对：发现任何透传中的字节损坏
    const unsigned char* payload = data + kTestFrameMagicLen + kTestFrameSeqLen;
    for (size_t i = 0; i < kTestFramePayloadLen; ++i)
    {
        if (payload[i] != testFramePayloadByte(seq, i))
            return false;
    }

    seqOut = seq;
    return true;
}

} // namespace call
