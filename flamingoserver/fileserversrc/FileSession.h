/**
 *  文件 / 图片传输会话，FileSession.h
 *
 *  处理 v2 文件传输的「字节流」段：在 fileserver / imgserver 连接上收发
 *  cmd_file_upload（1851）/ cmd_file_download（1852）。鉴权完全依赖分片
 *  描述符里的上传 / 下载令牌（FileToken），不握手、不登录、不查库。
 *
 *  一条连接同一时刻只处理一个文件（上传或下载）：上传写入 <md5>.part
 *  临时文件、按 offset 支持断点续传、末片校验 MD5 后改名为 <md5>；下载
 *  按客户端请求的 offset 逐片回传。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string>
#include "../net/ByteBuffer.h"
#include "TcpSession.h"

class FileSession : public TcpSession
{
public:
    /**
     *  @param conn        本会话所属连接
     *  @param filebasedir 文件存储根目录
     *  @param tokenkey    上传 / 下载令牌的 HMAC 校验密钥
     *  @param serverkind  服务器种类，取值见 enum file_server_kind
     */
    FileSession(const std::shared_ptr<TcpConnection>& conn, const char* filebasedir,
                const std::string& tokenkey, int serverkind);
    virtual ~FileSession();

    FileSession(const FileSession& rhs) = delete;
    FileSession& operator =(const FileSession& rhs) = delete;

    //有数据可读，会被工作 loop 调用
    void onRead(const std::shared_ptr<TcpConnection>& conn, ByteBuffer* pBuffer, Timestamp receivTime);

private:
    //会话当前所处的传输模式
    enum transfer_mode
    {
        mode_idle        = 0,   //空闲，尚未开始任何传输
        mode_uploading   = 1,   //正在上传
        mode_downloading = 2,   //正在下载
    };

    //分发单个已解出的帧
    bool processFrame(const std::shared_ptr<TcpConnection>& conn,
                      uint16_t cmd, uint32_t seq, const std::string& body);

    //处理一帧上传分片（cmd_file_upload）
    bool onUploadChunk(const std::shared_ptr<TcpConnection>& conn,
                       uint32_t seq, const std::string& body);
    //处理一帧下载请求（cmd_file_download）
    bool onDownloadChunk(const std::shared_ptr<TcpConnection>& conn,
                         uint32_t seq, const std::string& body);

    //发送一个 v2 帧（flags 由调用方指定）
    void sendFrame(uint16_t cmd, uint32_t seq, uint8_t flags, const std::string& body);
    //发送一个普通 JSON 应答帧（无附件，用于上传应答）
    void sendJsonResponse(uint16_t cmd, uint32_t seq, const std::string& json);
    //发送一个下载错误应答（附件帧，描述符带错误码，二进制段为空）
    void sendDownloadError(uint32_t seq, int code, const std::string& msg);

    //中止当前上传：关闭并删除 .part 临时文件、复位状态
    void abortUpload();
    //复位文件状态：关闭句柄、清空当前传输信息
    void resetFile();

private:
    std::string m_strFileBaseDir;   //文件存储根目录
    std::string m_tokenKey;         //令牌校验密钥
    int         m_serverKind;       //服务器种类，见 enum file_server_kind

    int         m_mode;             //当前传输模式，见 enum transfer_mode
    FILE*       m_fp;               //当前打开的文件句柄（上传为 .part，下载为最终文件）
    std::string m_curMd5;           //当前传输文件的 MD5
    int64_t     m_expectSize;       //上传:期望总字节数; 下载:文件实际字节数
    int64_t     m_uploadOffset;     //上传:已写入 .part 的字节数（权威续传位点）
    std::string m_partPath;         //上传:.part 临时文件的完整路径
};
