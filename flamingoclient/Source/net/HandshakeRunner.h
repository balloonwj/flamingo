/**
 *  客户端侧协议加密握手的统一执行器。
 *
 *  客户端有多条到服务端的连接（主连接、文件传输、图片传输、远程桌面与音视频中转），
 *  每条的收发方式都不一样：有的走 CIUSocket 上的 SendData / RecvData，有的自带 socket
 *  裸收发，有的连上之后还会切回阻塞模式。但<u>三帧握手的流程对每条链路完全相同</u>，
 *  所以这里把流程收在一处，各链路只需提供一个薄薄的收发适配器。
 *
 *  这么做不只是省代码。开发中出过一次教训：产品代码与测试代码各写了一份握手流程，
 *  两份都踩了同一个坑（net::encodeFrame 是<u>追加</u>语义，复用同一个缓冲会把第 3 帧
 *  拼在第 1 帧后面一起发出），于是测试全绿、真机却怎么都握不上手。流程只留一份之后，
 *  测试验的就是产品代码本身，这类"测试和产品各错各的"不会再发生。
 *
 *  用法（见 CIUSocket::PerformHandshake）：实现 IHandshakeChannel，然后
 *
 *      net::ClientHandshake hs;
 *      std::string err;
 *      if (!net::performClientHandshake(channel, serverPubKey, hs, err))
 *          （放弃这条连接，不可退回明文）
 *
 *  balloonwj@qq.com   2026-08-13
 */
#pragma once

#include <stddef.h>
#include <string>

#include "FrameCrypto.h"   //ClientHandshake

namespace net {

/**
 *  握手期间的字节收发接口，由各条链路提供自己的实现。
 *
 *  只在握手那三帧期间使用，握手完成后各链路照旧走自己原有的收发路径，
 *  所以实现可以很薄，也不必考虑并发——握手一定是在连接刚建好、还没有别的
 *  线程碰这条 socket 的时候同步做完的。
 */
class IHandshakeChannel
{
public:
    virtual ~IHandshakeChannel() {}

    /**
     *  把 len 个字节全部发出去，中途要自行处理"只发出去一部分"的情况。
     *  @param data 待发送字节起始地址，非空
     *  @param len  待发送字节数，大于 0
     *  @return 全部发出返回 true；出错或超时返回 false
     */
    virtual bool SendAll(const char* data, size_t len) = 0;

    /**
     *  收满 len 个字节才返回，中途要自行处理"只收到一部分"的情况。
     *  @param buf 接收缓冲，容量不小于 len
     *  @param len 要收取的字节数，大于 0
     *  @return 收满返回 true；出错、超时或对端关闭返回 false
     */
    virtual bool RecvAll(char* buf, size_t len) = 0;
};

/**
 *  在一条刚建好的连接上同步走完三帧协议加密握手。
 *
 *  握手帧全部是明文——此刻双方都还没有密钥。三帧依次是：本端一次性公钥与随机数、
 *  服务端的公钥与签名、本端发送流的流头。第 3 帧没有应答，函数发出它即返回。
 *
 *  @param channel      本条连接的收发适配器，其存活期覆盖本次调用即可
 *  @param serverPubKey 预置的服务端身份公钥，IDENTITY_PUBLICKEY_BYTES 字节。
 *                      传 NULL 视为未配置公钥，本函数判失败——是否要加密由调用方
 *                      在调用之前决定，本函数不替它做"没配就跳过"的判断
 *  @param hs           [出参] 本连接的握手器。函数内部会先 reset 它，成功返回后
 *                      其 cipher() 即可用于收发加密帧
 *  @param errOut       [出参] 失败原因（英文，可直接进日志），成功时清空
 *  @return 成功返回 true。失败意味着对端不是我们认识的那台服务器、或握手被中途
 *          破坏，调用方<u>必须放弃这条连接</u>，不得退回明文继续用
 *  副作用：在 channel 上收发三帧；改写 hs 的状态。
 */
bool performClientHandshake(IHandshakeChannel& channel, const unsigned char* serverPubKey,
                            ClientHandshake& hs, std::string& errOut);

} // namespace net
