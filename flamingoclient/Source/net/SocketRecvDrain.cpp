/**
 *  主连接收包循环的纯函数实现，说明见 SocketRecvDrain.h。
 *  balloonwj@qq.com   2026-10-01
 */

#include <winsock2.h>
#include "SocketRecvDrain.h"

namespace net {

SocketDrainResult DrainSocketRecv(SOCKET hSocket, std::string& strOut, char* pChunk,
                                  int nChunkSize, SocketDrainStats* pStats)
{
    if (pStats != NULL)
    {
        pStats->m_nChunks = 0;
        pStats->m_nBytes = 0;
        pStats->m_nLastError = 0;
    }

    while (true)
    {
        int nRet = ::recv(hSocket, pChunk, nChunkSize, 0);
        if (nRet == SOCKET_ERROR)
        {
            const int nError = ::WSAGetLastError();
            //接收缓冲已读空，本次可读事件处理完毕
            if (nError == WSAEWOULDBLOCK)
                return kSocketDrainOk;

            if (pStats != NULL)
                pStats->m_nLastError = nError;
            return kSocketDrainError;
        }

        //recv 返回 0 表示对端已关闭连接
        if (nRet < 1)
            return kSocketDrainClosed;

        strOut.append(pChunk, nRet);
        if (pStats != NULL)
        {
            ++pStats->m_nChunks;
            pStats->m_nBytes += (size_t)nRet;
        }

        //两次 recv 之间不等待。旧实现在此处 Sleep(1)，而 Windows 默认计时精度下 Sleep(1)
        //实际要睡一个系统时钟周期（约 16 毫秒），并且读完最后一块之后也要先睡一次才发现
        //已经读空，于是每个服务端应答都至少晚到约 16 毫秒（SocketRecvDrainTests 的 NET1
        //实测每轮读取的中位数为 15.1 毫秒）。非阻塞 socket 读空时 recv 立即报告
        //WSAEWOULDBLOCK，循环随即结束，去掉等待并不会空转。
    }
}

} // namespace net
