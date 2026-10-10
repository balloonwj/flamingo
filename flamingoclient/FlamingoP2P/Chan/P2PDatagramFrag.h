/**
 *  数据报通道的分片与重组（docs/P2P通信方案.md 7.3 节，第 4 期 4a）。
 *
 *  数据报通道一次 Send 的数据（如一帧视频）可能超过一个链路报文能装下的大小，发送端把它切成若干片，每片前面加
 *  8 字节的分片头：[消息号 4B][片号 2B][片数 2B]（网络字节序）。接收端按消息号把片收齐后交给业务方；任何一片丢失，
 *  整条消息在超时或被挤出待重组表时丢弃，由业务方自行补救（如请求关键帧）。
 *
 *  - 每片负载的上限由调用方按路径给出：UDP 路径受单个链路报文的明文上限约束，中转路径可以大得多；
 *  - 一条消息最多 kP2PMaxDatagramFragments 片；每条通道同时最多重组 kP2PMaxPendingDatagrams 条消息，表满时丢弃最旧的；
 *    一条消息从收到第一片起 kP2PDatagramReassemblyMs 内没有收齐即丢弃；
 *  - 重复的片忽略；与已记录的片数、长度不一致的片视为非法，整条消息丢弃。
 *
 *  只依赖标准库，不加锁（由会话的互斥量保护）。
 *
 *  典型用法：
 *      std::vector<std::string> frags;
 *      P2PDatagramFragmenter::Split(msgId, data, len, maxFragPayload, frags);    //发送端
 *      P2PDatagramReassembler r;
 *      std::string msg;
 *      if (r.Add(fragBytes, fragLen, nowMs, msg)) ...交给业务方;                  //接收端
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

namespace p2p {

//分片头长度（字节）：消息号 4 + 片号 2 + 片数 2
const size_t kP2PDatagramFragHeaderBytes = 8;

//一条消息最多的片数：按 UDP 路径每片约 1.1 KB 计，一条消息最大约 1.1 MB，足以装下高清视频的关键帧
const size_t kP2PMaxDatagramFragments = 1024;

//每条通道同时重组的消息上限：超过时丢弃最旧的一条
const size_t kP2PMaxPendingDatagrams = 16;

//一条消息从收到第一片起的重组时限（毫秒）：音视频数据过了这么久已经没有用了
const int64_t kP2PDatagramReassemblyMs = 1000;

/**
 *  发送端：把一条消息切成带分片头的若干片。只有静态函数。
 */
class P2PDatagramFragmenter
{
public:
    /**
     *  切片。
     *  @param msgId          消息号（每条通道各自递增，回绕无妨）
     *  @param data           消息内容；len 为 0 时可以为 NULL（得到一片只有分片头的空消息）
     *  @param len            消息长度（字节）
     *  @param maxFragPayload 每片负载的上限（字节，不含分片头），须大于 0
     *  @param out            [出参] 各片（含分片头），先清空
     *  @return 片数不超过 kP2PMaxDatagramFragments 返回 true；否则返回 false 且 out 为空
     */
    static bool Split(uint32_t msgId, const uint8_t* data, size_t len, size_t maxFragPayload,
                      std::vector<std::string>& out);

    /**
     *  按每片负载上限算出一条消息允许的最大长度（字节）。
     *  @param maxFragPayload 每片负载的上限（字节）
     */
    static size_t MaxMessageBytes(size_t maxFragPayload);
};

/**
 *  接收端：按消息号重组。一条通道一个实例。
 */
class P2PDatagramReassembler
{
public:
    P2PDatagramReassembler();

    /**
     *  收到一片。
     *  @param frag    一片（含分片头），只在本次调用期间有效
     *  @param len     长度（字节）
     *  @param nowMs   当前时间（毫秒），用于判定重组超时
     *  @param message [出参] 收齐时的整条消息
     *  @return 这一片使某条消息收齐时返回 true；其它情况（还没收齐、重复、非法、已超时丢弃）返回 false
     */
    bool Add(const uint8_t* frag, size_t len, int64_t nowMs, std::string& message);

    //正在重组的消息个数
    size_t PendingCount() const;

    //因超时、表满或非法而丢弃的消息个数（累计）
    uint64_t DroppedCount() const;

private:
    //一条正在重组的消息
    struct Pending
    {
        int64_t                  m_firstMs;    //收到第一片的时刻（毫秒）
        uint16_t                 m_count;      //片数
        uint16_t                 m_received;   //已收到的片数
        std::vector<std::string> m_frags;      //各片负载（按片号存放，未收到的为空且 m_have 为假）
        std::vector<bool>        m_have;       //各片是否已收到
    };

    //丢弃超时的消息
    void Expire(int64_t nowMs);

    //表满时丢弃收到第一片最早的一条
    void EvictOldest();

private:
    std::map<uint32_t, Pending> m_pending;   //消息号 → 正在重组的消息
    uint64_t                    m_dropped;   //丢弃的消息个数
};

} // namespace p2p
