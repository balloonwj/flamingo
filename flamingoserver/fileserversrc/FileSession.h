/** 
 * FileSession.h
 * zhangyl, 2017.03.17
 **/

#pragma once
#include "../net/Buffer.h"
#include "TcpSession.h"

class FileSession : public TcpSession
{
public:
    FileSession(const std::shared_ptr<TcpConnection>& conn, const char* filebasedir);
    virtual ~FileSession();

    FileSession(const FileSession& rhs) = delete;
    FileSession& operator =(const FileSession& rhs) = delete;

    //有数据可读, 会被多个工作loop调用
    void onRead(const std::shared_ptr<TcpConnection>& conn, Buffer* pBuffer, Timestamp receivTime);   

private:
    //64位机器上，size_t是8个字节
    bool process(const std::shared_ptr<TcpConnection>& conn, const char* inbuf, size_t length);
    
    bool onUploadFileResponse(const std::string& filemd5, int64_t offset, int64_t filesize, const std::string& filedata, const std::shared_ptr<TcpConnection>& conn);
    bool onDownloadFileResponse(const std::string& filemd5, int32_t clientNetType, const std::shared_ptr<TcpConnection>& conn);

    void resetFile();

private:
    int32_t           m_id;         //session id
    int32_t           m_seq;        //当前Session数据包序列号

    //当前文件信息
    FILE*             m_fp{};
    int64_t           m_currentDownloadFileOffset{};    //当前在正下载的文件的偏移量
    int64_t           m_currentDownloadFileSize{};      //当前在正下载的文件的大小(下载完成以后最好置0)
    std::string       m_strFileBaseDir;                 //文件目录
    bool              m_bFileUploading;                 //是否处于正在上传文件的过程中
};