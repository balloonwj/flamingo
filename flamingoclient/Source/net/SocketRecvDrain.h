/**
 *  主连接收包循环的纯函数实现：socket 报告可读之后，把系统接收缓冲里现有的数据一次读完，
 *  直到对端暂无数据（WSAEWOULDBLOCK）、对端关闭连接或出错为止。
 *
 *  从 CIUSocket::Recv 中抽出，目的是让单元测试可以直接对本机回环 socket 验证它的
 *  读取结果与耗时，而不必启动整个网络模块。
 *
 *  典型用法（收包线程在 select 报告可读之后）：
 *      net::SocketDrainStats stats;
 *      net::SocketDrainResult r = net::DrainSocketRecv(hSocket, strRecvBuf,
 *                                                      pChunk, nChunkSize, &stats);
 *      if (r != net::kSocketDrainOk) { ...关闭连接并上报... }
 *
 *  <u>本文件被 FlamingoUnit 测试工程直接编译</u>，故 .cpp 不走主工程的预编译头。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#pragma once

//主工程的编译单元经预编译头引入 <windows.h>，其中已带有老版本的 winsock.h；
//此时再引入 winsock2.h 会与之重复定义。故只在两者都未引入时才引入 winsock2.h，
//本头文件需要的 SOCKET、recv 等声明两个版本都有。
#if !defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#include <winsock2.h>
#endif

#include <string>
#include <stddef.h>

namespace net {

//主连接单次 recv 使用的缓冲大小，单位字节。
//取 64 KB：一页 50 条历史消息的应答在几十 KB 量级，多数情况下一次 recv 即可读完；
//再大对单次读事件已无收益，只是白占内存。
static const int kRecvChunkSize = 64 * 1024;

//DrainSocketRecv 的结束原因
enum SocketDrainResult
{
    //读到对端暂无数据为止（recv 报告 WSAEWOULDBLOCK），连接仍然正常；本次读到的字节数可能为 0
    kSocketDrainOk,
    //对端已关闭连接（recv 返回 0）；在此之前读到的数据仍已追加到输出缓冲
    kSocketDrainClosed,
    //socket 出错（recv 返回 SOCKET_ERROR 且错误码不是 WSAEWOULDBLOCK），错误码见 SocketDrainStats::m_nLastError
    kSocketDrainError
};

//一次 DrainSocketRecv 调用的统计信息，供日志与性能跟踪使用
struct SocketDrainStats
{
    //本次读到数据的 recv 调用次数
    int m_nChunks;
    //本次读到的总字节数
    size_t m_nBytes;
    //出错时的 WSAGetLastError() 值；正常结束或对端关闭时为 0
    int m_nLastError;
};

/**
 *  把 socket 接收缓冲里现有的数据全部读出，追加到 strOut 末尾。
 *
 *  两次 recv 之间不做任何等待：socket 必须是非阻塞的，接收缓冲读空时 recv 立即报告
 *  WSAEWOULDBLOCK，本函数随即返回，不会空转。读到一半的协议帧留在 strOut 里，
 *  等下一次可读事件补齐。
 *
 *  @param hSocket     已连接的非阻塞 socket；阻塞 socket 会在读空后一直阻塞到对端再发数据
 *  @param strOut      接收缓冲，读到的数据追加在其末尾，原有内容不动
 *  @param pChunk      单次 recv 使用的临时缓冲，由调用方持有，不能为 NULL
 *  @param nChunkSize  pChunk 的字节数，须大于 0
 *  @param pStats      输出统计信息，可以为 NULL；非 NULL 时本函数先将其清零再填写
 *  @return 结束原因，见 SocketDrainResult
 *  副作用：从 socket 读取数据；修改 strOut。
 */
SocketDrainResult DrainSocketRecv(SOCKET hSocket, std::string& strOut, char* pChunk,
                                  int nChunkSize, SocketDrainStats* pStats);

} // namespace net
