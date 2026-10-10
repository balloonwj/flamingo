/**
 *  未收到应答的请求登记簿：按帧 seq 记下请求的命令号与发起方（窗口），应答到达时据此交回发起方。
 *
 *  服务端的应答与请求用同一个命令号、同一个 seq（docs/协议设计-v2.md 4.4），所以按 seq 找到登记后还要核对命令号，
 *  对不上的不认领（防止断线重连后 seq 从头计数时把别的应答交错了窗口）。窗口关闭时撤销它名下的全部登记，
 *  之后到达的应答直接丢弃；断线重连时清空全部登记。
 *
 *  纯逻辑，单元测试直接调用（Tests/BotCoreTests.cpp）。只在界面线程上使用，不加锁。
 *
 *  典型用法：
 *      book.Add(seq, bot_cmd_get_detail, clientId);
 *      uint32_t client = 0;
 *      if (book.Take(seq, cmd, client))
 *          DeliverTo(client, reply);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include <stdint.h>
#include <map>

namespace flamingobot {

/**
 *  CBotRequestBook —— seq → （命令号，发起方）的登记簿。
 */
class CBotRequestBook
{
public:
    CBotRequestBook();

    /**
     *  登记一条已发出的请求。同一个 seq 重复登记时以后一次为准。
     *  @param seq      帧 seq（大于 0）
     *  @param cmd      命令号
     *  @param clientId 发起方编号（大于 0）
     */
    void Add(uint32_t seq, uint16_t cmd, uint32_t clientId);

    /**
     *  认领一条应答：seq 已登记且命令号相同时取出发起方并删除登记。
     *  @param seq      应答的 seq
     *  @param cmd      应答的命令号
     *  @param clientId [出参] 发起方编号
     *  @return 认领成功返回 true；没有登记或命令号对不上返回 false（登记保持不变）
     */
    bool Take(uint32_t seq, uint16_t cmd, uint32_t& clientId);

    /**
     *  撤销某个发起方名下的全部登记（窗口关闭时）。
     *  @return 撤销的条数
     */
    size_t RemoveClient(uint32_t clientId);

    //清空全部登记（断线重连、退出登录时）
    void Clear();

    //登记着的条数
    size_t Size() const;

    //某个发起方名下登记着的条数
    size_t CountOf(uint32_t clientId) const;

private:
    //一条登记
    struct Entry
    {
        uint16_t m_cmd;       //命令号
        uint32_t m_clientId;  //发起方编号
    };

    std::map<uint32_t, Entry> m_entries;   //seq → 登记
};

} // namespace flamingobot
