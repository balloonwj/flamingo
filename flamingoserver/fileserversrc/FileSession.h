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
 *  同一个监听端口上还按帧 cmd 分流出第三种用途：单聊文件夹传输的多通道中转
 *  （cmd_folder_relay_hello / cmd_folder_relay_data，不新开端口）。这条路上
 *  的连接不读写磁盘，只把包体从本端会话密钥换成配对端会话密钥后转发，具体逻辑
 *  在 FolderRelayEndpoint，本类只负责分流、模式互斥与收发帧。三种用途<u>互斥</u>：
 *  一条连接一旦进入其中一种模式，就不再受理另外两种的命令。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <memory>
#include "../net/ByteBuffer.h"
#include "TcpSession.h"
#include "FolderRelayEndpoint.h"

//v2 帧头，定义在 chatserversrc/Protocol.h；此处仅按 const 引用传递，故只需前向声明
struct frame_header;

//继承 enable_shared_from_this 是为了在中转握手成功时把「自己」登记进文件夹中转配对表：
//配对表保存的是会话的弱引用，而会话对象由 FileServer 的 std::shared_ptr 持有，
//故这里必须能从 this 拿回那个 shared_ptr（做法同 imgserversrc/RelaySession.h）
class FileSession : public TcpSession, public std::enable_shared_from_this<FileSession>
{
public:
    /**
     *  @param conn          本会话所属连接
     *  @param filebasedir   文件存储根目录
     *  @param tokenkey      上传 / 下载令牌的 HMAC 校验密钥
     *  @param serverkind    服务器种类，取值见 enum file_server_kind
     *  @param relaytokenkey 文件夹传输中转令牌的 HMAC 校验密钥（fileserver.conf 的
     *                       relaytokenkey，须与 chatserver.conf 的同名项一致）。空串表示
     *                       本服务不承载文件夹中转，此时中转握手一律被拒（imgserver 即如此）
     *  @param relayhub      文件夹中转配对表指针；由 FileServer 持有，生命周期长于本会话。
     *                       本类不获取其所有权。传 NULL 时中转握手一律失败
     */
    FileSession(const std::shared_ptr<TcpConnection>& conn, const char* filebasedir,
                const std::string& tokenkey, int serverkind,
                const std::string& relaytokenkey, FolderRelayHub* relayhub);
    virtual ~FileSession();

    FileSession(const FileSession& rhs) = delete;
    FileSession& operator =(const FileSession& rhs) = delete;

    //有数据可读，会被工作 loop 调用
    void onRead(const std::shared_ptr<TcpConnection>& conn, ByteBuffer* pBuffer, Timestamp receivTime);

    /**
     *  把一帧文件夹中转数据发到本会话所属连接上。
     *
     *  <u>由配对端会话在它自己的 I/O 线程里调用</u>：包体要用本连接的会话密钥加密，
     *  只有本对象拿得到那把密钥，所以这一步必须由本对象来做（做法同
     *  RelaySession::forwardFrame）。本连接的发送方向因而有两个来源——本会话自己回的
     *  握手应答，以及配对端线程转发来的数据帧——两者最终都由 sendFrame 归到本连接
     *  所属的 EventLoop 线程上执行，理由见 sendFrame 的实现。
     *
     *  @param cmd       原帧的 cmd（cmd_folder_relay_data），原样沿用
     *  @param seq       原帧的 seq，原样沿用，供对端客户端关联收发
     *  @param plainBody 已用发送端会话密钥解密还原的明文包体。fileserver <u>不解析</u>
     *                   它的内容，只当作不透明字节转发
     *  副作用：向本连接写出一个完整 v2 帧；加密或建帧失败时只记日志并丢弃本帧。
     */
    void sendRelayFrame(uint16_t cmd, uint32_t seq, const std::string& plainBody);

private:
    //会话当前所处的传输模式。三种传输模式互斥：连接一旦进入其中一种，另外两种的命令
    //一律拒绝并断开，否则可以拿一条中转连接去调文件接口，绕过上传下载令牌里的场景校验
    //（见 docs/单聊文件夹传输方案.md 第 6.2 节「模式互斥必须显式判」）
    enum transfer_mode
    {
        mode_idle         = 0,   //空闲，尚未开始任何传输，三种命令都还可以受理
        mode_uploading    = 1,   //正在上传：收到首片 cmd_file_upload 并打开 .part 文件后进入
        mode_downloading  = 2,   //正在下载：收到首个 cmd_file_download 并打开目标文件后进入
        mode_folder_relay = 3,   //正在做文件夹传输中转：cmd_folder_relay_hello 校验通过、
                                 //本端已登记进配对表后进入，此后本连接只转发不读写磁盘
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

    //处理一帧文件夹中转握手（cmd_folder_relay_hello）：模式互斥判定 + 令牌校验 + 配对表登记，
    //成功后本连接切入 mode_folder_relay。返回 false 表示应答已发出但须关闭连接
    bool onFolderRelayHello(const std::shared_ptr<TcpConnection>& conn,
                            uint32_t seq, const std::string& body);
    //处理一帧文件夹中转数据（cmd_folder_relay_data）：按 (sessionid, lane) 找到配对端，把明文
    //包体交给它重新加密后发出。返回 false 表示本连接不该收到该帧，调用方须关闭连接
    bool onFolderRelayData(const std::shared_ptr<TcpConnection>& conn,
                           uint32_t seq, const std::string& body);

    /**
     *  协议加密的收包入口：在业务分发之前处理握手帧、校验加密状态、把密文包体
     *  还原成明文。实际逻辑在 net::processInboundFrame，与 chatserver 共用一份。
     *
     *  @param conn    当前连接
     *  @param header  已解出的帧头，其中字段用于构造附加认证数据
     *  @param body    入参为线上包体；本帧为密文且解密成功时被替换为明文
     *  @param handled 出参。为 true 表示本帧是握手帧、已在本函数内应答完毕，
     *                 调用方不应再往下走业务分发
     *  @return false 表示协议出错且<u>本函数已关闭连接</u>，调用方须立即返回
     */
    bool processFrameSecurity(const std::shared_ptr<TcpConnection>& conn,
                              const frame_header& header,
                              std::string& body, bool& handled);

    //发送一个 v2 帧（flags 由调用方指定）。可从任意线程调用，见实现里的说明
    void sendFrame(uint16_t cmd, uint32_t seq, uint8_t flags, const std::string& body);
    //在本连接所属的 EventLoop 线程里完成「组帧 + 加密 + 写 socket」。
    //只允许由 sendFrame 直接调用（已在该线程时）或经 runInLoop 投递后执行
    void sendFrameInLoop(uint16_t cmd, uint32_t seq, uint8_t flags, const std::string& body);
    //发送一个普通 JSON 应答帧（无附件，用于上传应答）
    void sendJsonResponse(uint16_t cmd, uint32_t seq, const std::string& json);
    //发送一个下载错误应答（附件帧，描述符带错误码，二进制段为空）
    void sendDownloadError(uint32_t seq, int code, const std::string& msg);
    //回一帧中转握手应答（包体 {code,msg}，cmd 与请求同为 cmd_folder_relay_hello）
    void sendFolderRelayHelloAck(uint32_t seq, int code, const std::string& msg);

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

    //本连接的文件夹传输中转端点：握手校验、配对与找对端都由它负责。会话建立时即构造
    //（此时尚未握手，不占配对表的位置），随会话一同销毁
    FolderRelayEndpoint m_relayEndpoint;
};
