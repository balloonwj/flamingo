/**
 * TcpSession.cpp
 * zhangyl 2017.03.09
 **/
#include "TcpSession.h"
#include "../base/AsyncLog.h"
#include "../net/ProtocolStream.h"
#include "FileMsg.h"

TcpSession::TcpSession(const std::weak_ptr<TcpConnection>& tmpconn) : tmpConn_(tmpconn)
{
    
}

TcpSession::~TcpSession()
{
    
}

void TcpSession::Send(int32_t cmd, int32_t seq, int32_t errorcode, const std::string& filemd5, int64_t offset, int64_t filesize, const std::string& filedata)
{
    std::string outbuf;
    net::BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(cmd);
    writeStream.WriteInt32(seq);
    writeStream.WriteInt32(errorcode);
    writeStream.WriteString(filemd5);
    writeStream.WriteInt64(offset);
    writeStream.WriteInt64(filesize);
    writeStream.WriteString(filedata);   
    writeStream.Flush();

    SendPackage(outbuf.c_str(), outbuf.length());
}

void TcpSession::SendPackage(const char* body, int64_t bodylength)
{
    string strPackageData;
    file_msg header = { (int64_t)bodylength };
    strPackageData.append((const char*)&header, sizeof(header));
    strPackageData.append(body, bodylength);

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
        LOGI("Send data, package length: %d, body length: %d", strPackageData.length(), bodylength);
        //LOG_DEBUG_BIN((unsigned char*)body, bodylength);
        conn->send(strPackageData.c_str(), strPackageData.length());
    }
}