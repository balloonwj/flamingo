/**
 * TcpSession.cpp
 * zhangyl 2017.03.09
 **/
#include "../base/logging.h"
#include "../net/protocolstream.h"
#include "FileMsg.h"
#include "TcpSession.h"

TcpSession::TcpSession(const std::shared_ptr<TcpConnection>& conn) : conn_(conn)
{
    
}

TcpSession::~TcpSession()
{
    
}

void TcpSession::Send(int32_t cmd, int32_t seq, int32_t errorcode, const std::string& filemd5, int64_t offset, int64_t filesize, const std::string& filedata)
{
    std::string outbuf;
    balloon::BinaryWriteStream writeStream(&outbuf);
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
    if (conn_)
    {
        //LOG_INFO << "Send data, length:" << length;
        //LOG_DEBUG_BIN((unsigned char*)p, length);
        conn_->send(strPackageData.c_str(), strPackageData.length());
    }
}