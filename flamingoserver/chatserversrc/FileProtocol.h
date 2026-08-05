/**
 *  v2 文件信令报文（cmd_request_upload / cmd_request_download）的编解码。
 *
 *  文件传输分两段：本模块处理在 chatserver 连接上收发的「信令」——客户端
 *  申请上传 / 下载，chatserver 据此做秒传判断、签发令牌、指明目标
 *  fileserver / imgserver；真正的字节流分片（cmd_file_upload /
 *  cmd_file_download）在 fileserver / imgserver 连接上收发，不经本模块。
 *
 *  本模块是纯逻辑——只依赖 jsoncpp，不触及网络与数据库，便于单元测试
 *  （见 tests/test_fileprotocol.cpp）。秒传查库、令牌签发、推送在会话层。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include <stdint.h>
#include <string>

//上传 / 下载令牌的有效期，单位毫秒：1 小时
constexpr int64_t FILE_TOKEN_TTL_MS = 3600LL * 1000LL;
//头像文件（scene=3）大小上限，单位字节：5 MiB
constexpr int64_t FILE_MAX_SIZE_AVATAR = 5LL * 1024 * 1024;
//图片消息文件（scene=4）大小上限，单位字节：20 MiB
constexpr int64_t FILE_MAX_SIZE_IMAGE_MSG = 20LL * 1024 * 1024;
//聊天文件（scene=1）/ 群文件（scene=2）不设大小上限，故此处无对应常量：
//二者都走 fileserver、分片流式收发，服务端不会把整文件读入内存。校验逻辑见
//ChatSession::onRequestUpload，那里用 maxSize = -1 表示"不校验大小"。
//文件内容 MD5 十六进制串的固定长度
constexpr size_t FILE_MD5_HEX_LEN = 32;

//cmd_request_upload 请求
struct RequestUploadRequest
{
    int          scene;   //文件用途场景，取值见 enum file_scene
    std::string  md5;     //文件内容MD5（小写十六进制，32位）
    int64_t      size;    //文件总字节数
    std::string  name;    //原始文件名（图片场景可为空）
    std::string  ext;     //扩展名，不含点（可为空）

    RequestUploadRequest()
        : scene(0), size(0)
    {
    }
};

//cmd_request_download 请求
struct RequestDownloadRequest
{
    int64_t fileid;   //待下载文件的 fileid

    RequestDownloadRequest()
        : fileid(0)
    {
    }
};

//文件信令报文编解码工具类；全部为静态方法，不可实例化
class FileProtocol
{
public:
    /**
     *  解析 cmd_request_upload 请求。
     *  scene 不在 1~4、md5 非 32 位、size 非正均返回 false；name/ext 可缺省。
     */
    static bool parseRequestUploadRequest(const std::string& json, RequestUploadRequest& req);

    /**
     *  解析 cmd_request_download 请求；fileid 缺失或非正返回 false。
     */
    static bool parseRequestDownloadRequest(const std::string& json, RequestDownloadRequest& req);

    /**
     *  构造 cmd_request_upload 应答。
     *  @param fileid      服务器分配的文件 fileid
     *  @param exists      是否秒传命中：true 表示文件已存在、客户端无需上传
     *  @param uploadtoken 上传令牌；exists 为 true 时本参数被忽略
     *  @param host        目标 fileserver / imgserver 主机；exists 为 true 时忽略
     *  @param port        目标服务器端口；exists 为 true 时忽略
     *  @param tls         目标服务器是否启用 TLS；exists 为 true 时忽略
     *  @param chunksize   上传分片字节数；exists 为 true 时忽略
     *  @return 应答 JSON 文本
     */
    static std::string buildRequestUploadResponse(int64_t fileid, bool exists,
                                                  const std::string& uploadtoken,
                                                  const std::string& host, int port,
                                                  bool tls, int chunksize);

    /**
     *  构造 cmd_request_download 应答。
     *  @param fileid        文件 fileid
     *  @param downloadtoken 下载令牌
     *  @param md5           文件内容MD5，供客户端下载完成后校验
     *  @param size          文件总字节数
     *  @param host          目标 fileserver / imgserver 主机
     *  @param port          目标服务器端口
     *  @param tls           目标服务器是否启用 TLS
     *  @return 应答 JSON 文本
     */
    static std::string buildRequestDownloadResponse(int64_t fileid,
                                                    const std::string& downloadtoken,
                                                    const std::string& md5, int64_t size,
                                                    const std::string& host, int port,
                                                    bool tls);

    /**
     *  构造通用 { code, msg } 应答，用于各类错误返回。
     */
    static std::string buildSimpleResponse(int code, const std::string& msg);

private:
    FileProtocol() = delete;
    ~FileProtocol() = delete;
    FileProtocol(const FileProtocol&) = delete;
    FileProtocol& operator=(const FileProtocol&) = delete;
};
