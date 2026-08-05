/**
 *  v2 文件 / 图片信令与分片报文编解码（客户端侧）。
 *
 *  文件传输分两段，本模块覆盖客户端要构造 / 解析的全部报文：
 *    ① 信令（在 chatserver 连接上收发）——
 *       cmd_request_upload(1801)：申请上传，含秒传判断、返回上传令牌与目标
 *       服务器；cmd_request_download(1802)：申请下载，返回下载令牌与目标服务器。
 *    ② 字节流（在 fileserver / imgserver 连接上收发）——
 *       cmd_file_upload(1851)：上传分片，包体为「描述符 + 二进制附件」；
 *       cmd_file_download(1852)：下载分片，应答包体为「描述符 + 二进制附件」。
 *
 *  本模块只产出 / 解析 JSON 文本（信令包体、分片描述符），不负责封帧与附件
 *  打包——那是 net/FrameCodec 的 encodeFrame / encodeAttachmentBody 的职责。
 *  本模块是纯逻辑，只依赖 jsoncpp，不触及网络与 UI，便于单元测试，见
 *  flamingoclient/Source/Tests/FileProtocolClientTests.cpp。
 *
 *  本模块与服务端 chatserversrc/FileProtocol（信令）、fileserversrc/FileSession
 *  （字节流）互为镜像：服务端解析请求 / 构造应答，客户端构造请求 / 解析应答。
 *
 *  balloonwj@qq.com   2026-06-03
 */
#pragma once

#include <stdint.h>
#include <string>

namespace net {

//cmd_request_upload(1801) 应答的解析结果。
//成功（m_code==0）时：若 m_exists 为 true 即秒传命中、无需上传，其余上传相关
//字段无意义；若 m_exists 为 false，则 m_uploadtoken / m_host / m_port / m_tls /
//m_chunksize 指明上传所需的令牌与目标 fileserver / imgserver。
struct RequestUploadResult
{
    int         m_code;         //结果码，0 成功，非 0 见 enum error_code
    std::string m_msg;          //结果描述（成功为 "ok"，失败为诊断信息）
    std::string m_fileid;       //服务器分配的文件ID（字符串编码，避免大整数精度丢失）
    bool        m_exists;       //秒传命中标志：true 表示文件已存在、客户端无需上传
    std::string m_uploadtoken;  //上传令牌；仅 m_exists==false 时有效
    std::string m_host;         //目标 fileserver / imgserver 主机；仅 m_exists==false 时有效
    int         m_port;         //目标服务器端口；仅 m_exists==false 时有效
    bool        m_tls;          //目标服务器是否启用 TLS；仅 m_exists==false 时有效
    int         m_chunksize;    //上传分片字节数；仅 m_exists==false 时有效

    RequestUploadResult()
        : m_code(-1)        //-1：尚未解析出有效结果码，避免被误判为成功
        , m_exists(false)
        , m_port(0)
        , m_tls(false)
        , m_chunksize(0)
    {
    }
};

//cmd_request_download(1802) 应答的解析结果。
//成功（m_code==0）时各字段有效：m_downloadtoken / m_host / m_port 指明下载所需
//的令牌与目标服务器，m_md5 / m_size 供客户端下载完成后校验完整性。
struct RequestDownloadResult
{
    int         m_code;          //结果码，0 成功，非 0 见 enum error_code
    std::string m_msg;           //结果描述
    std::string m_fileid;        //文件ID（字符串编码）
    std::string m_downloadtoken; //下载令牌
    std::string m_md5;           //文件内容MD5（小写十六进制），下载完成后据此校验
    int64_t     m_size;          //文件总字节数
    std::string m_host;          //目标 fileserver / imgserver 主机
    int         m_port;          //目标服务器端口
    bool        m_tls;           //目标服务器是否启用 TLS

    RequestDownloadResult()
        : m_code(-1)
        , m_size(0)
        , m_port(0)
        , m_tls(false)
    {
    }
};

//cmd_file_upload(1851) 分片回执的解析结果（普通 JSON 包体，无附件）。
//m_done 为 true 表示服务器已收齐并校验通过；无论 done 与否，m_nextoffset 都
//给出服务器权威的下一片续传位点，客户端据此续传 / 收尾。
struct UploadChunkAck
{
    int         m_code;        //结果码，0 成功，非 0 见 enum error_code
    std::string m_msg;         //结果描述（错误时为诊断信息）
    bool        m_done;        //是否已收齐：true 表示整文件上传完成
    int64_t     m_nextoffset;  //服务器权威续传位点（下一片应从此偏移开始发）

    UploadChunkAck()
        : m_code(-1)
        , m_done(false)
        , m_nextoffset(0)
    {
    }
};

//cmd_file_download(1852) 分片应答帧「描述符」的解析结果（二进制段由调用方按
//FrameCodec::decodeAttachmentBody 另取，不在本结构里）。
//m_done 为 true 表示本片为末片，客户端拼接完成后按 md5 校验。
struct DownloadChunkInfo
{
    int         m_code;       //结果码，0 成功，非 0 见 enum error_code（错误时无二进制段）
    std::string m_msg;        //结果描述（错误时为诊断信息）
    int64_t     m_offset;     //本片在文件中的起始偏移
    int64_t     m_totalsize;  //文件总字节数
    bool        m_done;       //是否末片：true 表示文件已传完

    DownloadChunkInfo()
        : m_code(-1)
        , m_offset(0)
        , m_totalsize(0)
        , m_done(false)
    {
    }
};

//文件 / 图片信令与分片报文编解码工具类；全部为静态方法，不可实例化。
class FileProtocolClient
{
public:
    /**
     *  构造 cmd_request_upload(1801) 请求 JSON 包体（紧凑 UTF-8）。
     *  @param scene 文件用途场景，取值见 enum file_scene（图片消息传 file_scene_image_msg）
     *  @param md5   文件内容MD5（小写十六进制，32 位）；调用方须保证已算好
     *  @param size  文件总字节数（>0）
     *  @param name  原始文件名（可空，图片场景常为空）
     *  @param ext   扩展名，不含点（可空）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildRequestUploadRequest(int scene, const std::string& md5,
                                                 int64_t size, const std::string& name,
                                                 const std::string& ext);

    /**
     *  解析 cmd_request_upload(1801) 应答 JSON 包体。
     *  只要 JSON 合法且含整型 code 即返回 true，调用方据 out.m_code 判定结果。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；m_code==0 时 m_fileid / m_exists 有效，
     *                  m_exists==false 时上传令牌与目标服务器字段亦有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseRequestUploadResponse(const std::string& jsonbody, RequestUploadResult& out);

    /**
     *  构造 cmd_request_download(1802) 请求 JSON 包体（紧凑 UTF-8）。
     *  @param fileid 待下载文件的文件ID（字符串编码，与服务端约定一致）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildRequestDownloadRequest(const std::string& fileid);

    /**
     *  解析 cmd_request_download(1802) 应答 JSON 包体。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；m_code==0 时各下载字段有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseRequestDownloadResponse(const std::string& jsonbody, RequestDownloadResult& out);

    /**
     *  构造 cmd_file_upload(1851) 上传分片的「描述符」JSON（紧凑 UTF-8）。
     *  外层须用 FrameCodec::encodeAttachmentBody 把本描述符与该片二进制拼成
     *  附件包体，再 encodeFrame 套帧头并置 frame_flag_has_attachment。
     *  服务端仅据令牌取 md5 / 大小，故描述符只需 uploadtoken 与 offset。
     *  @param uploadtoken 上传令牌（来自 cmd_request_upload 应答）
     *  @param offset      本片在文件中的起始偏移（字节）
     *  @return 紧凑格式的描述符 JSON 文本
     */
    static std::string buildUploadChunkDescriptor(const std::string& uploadtoken, int64_t offset);

    /**
     *  解析 cmd_file_upload(1851) 分片回执 JSON 包体（普通 JSON，无附件）。
     *  @param jsonbody 回执包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；含 m_code / m_done / m_nextoffset
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseUploadChunkAck(const std::string& jsonbody, UploadChunkAck& out);

    /**
     *  构造 cmd_file_download(1852) 下载分片请求的「描述符」JSON（紧凑 UTF-8）。
     *  下载请求帧为普通 JSON 包体（无附件），直接以本描述符作包体 encodeFrame。
     *  @param downloadtoken 下载令牌（来自 cmd_request_download 应答）
     *  @param offset        请求从该偏移开始的分片（字节）
     *  @return 紧凑格式的描述符 JSON 文本
     */
    static std::string buildDownloadChunkDescriptor(const std::string& downloadtoken, int64_t offset);

    /**
     *  解析 cmd_file_download(1852) 分片应答帧的「描述符」JSON。
     *  调用方先用 FrameCodec::decodeAttachmentBody 拆出描述符与二进制段，再把
     *  描述符文本传入本方法解析；二进制段（本片字节）由调用方自行拼接落盘。
     *  @param descriptor 应答帧的描述符 JSON 文本
     *  @param out        [出参] 解析结果；含 m_code / m_offset / m_totalsize / m_done
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseDownloadChunkInfo(const std::string& descriptor, DownloadChunkInfo& out);

private:
    FileProtocolClient() = delete;
    ~FileProtocolClient() = delete;
    FileProtocolClient(const FileProtocolClient&) = delete;
    FileProtocolClient& operator=(const FileProtocolClient&) = delete;
};

} // namespace net
