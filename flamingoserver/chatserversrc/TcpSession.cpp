/**
 * TcpSession.cpp
 * zhangyl 2017.03.09
 **/
#include "TcpSession.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "../net/ProtocolStream.h"
#include "../zlib1.2.11/ZlibUtil.h"
#include "ChatServer.h"
#include "Msg.h"

TcpSession::TcpSession(const std::weak_ptr<TcpConnection>& tmpconn) : tmpConn_(tmpconn)
{
    
}

TcpSession::~TcpSession()
{
    
}

void TcpSession::send(int32_t cmd, int32_t seq, const std::string& data)
{
    send(cmd, seq, data.c_str(), data.length());
}

void TcpSession::send(int32_t cmd, int32_t seq, const char* data, int32_t dataLength)
{
    std::string outbuf;
    net::BinaryStreamWriter writeStream(&outbuf);
    writeStream.WriteInt32(cmd);
    writeStream.WriteInt32(seq);
    writeStream.WriteCString(data, dataLength);
    writeStream.Flush();

    sendPackage(outbuf.c_str(), outbuf.length());
}

void TcpSession::send(const std::string& p)
{
    sendPackage(p.c_str(), p.length());
}

void TcpSession::send(const char* p, int32_t length)
{
    sendPackage(p, length);
}

void TcpSession::sendPackage(const char* p, int32_t length)
{   
    string srcbuf(p, length);
    string destbuf;
    if (!ZlibUtil::compressBuf(srcbuf, destbuf))
    {
        LOGE("compress buf error");
        return;
    }
 
    string strPackageData;
    chat_msg_header header;
    header.compressflag = 1;
    header.compresssize = destbuf.length();
    header.originsize = length;
    if (Singleton<ChatServer>::Instance().isLogPackageBinaryEnabled())
    {
        LOGI("Send data, header length: %d, body length: %d", sizeof(header), destbuf.length());
    }
    
    //插入一个包头
    strPackageData.append((const char*)&header, sizeof(header));
    strPackageData.append(destbuf);

    //TODO: 这些Session和connection对象的生命周期要好好梳理一下
    if (tmpConn_.expired())
    {
        //FIXME: 出现这种问题需要排查
        LOGE("Tcp connection is destroyed , but why TcpSession is still alive ?");
        return;
    }

    std::shared_ptr<TcpConnection> conn = tmpConn_.lock();
    if (conn)
    {
        if (Singleton<ChatServer>::Instance().isLogPackageBinaryEnabled())
        {
            size_t length = strPackageData.length();
            LOGI("Send data, package length: %d", length);
            //LOG_DEBUG_BIN((unsigned char*)strPackageData.c_str(), length);
        }
        
        conn->send(strPackageData);
    }
}