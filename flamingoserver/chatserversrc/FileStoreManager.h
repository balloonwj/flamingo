/**
 *  文件元数据存储管理器，FileStoreManager.h
 *
 *  负责 v2 文件元数据表 t_file 的落库与查询，并持有文件域的运行配置：
 *  令牌密钥、目标 fileserver / imgserver 的地址。每次数据库操作单独建立
 *  一条连接（与 MessageManager / GroupManager / CardManager 同一风格），
 *  不在内存缓存。
 *
 *  命名说明：fileserver / imgserver 进程内另有一个同义但不同职责的
 *  fileserversrc/FileManager（管理磁盘文件），二者分属不同可执行程序、
 *  从不在同一进程内链接，故不冲突；本类只服务 chatserver。
 *
 *  典型用法：main.cpp 启动时 init(...) + configure(...)；会话层在
 *  cmd_request_upload / cmd_request_download 处理中调用。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include <stdint.h>
#include <string>

//t_file.f_status 取值：文件记录已建、字节尚未传完
const int file_status_pending   = 0;
//t_file.f_status 取值：文件已上传完成，可被秒传命中、可下载
const int file_status_completed = 1;

//一条文件元数据，对应数据库 t_file 的一行
struct FileRecord
{
    int64_t      fileid;      //文件ID，即 t_file 自增主键，对外的 fileid
    std::string  md5;         //文件内容MD5（小写十六进制，32位），磁盘文件名依据
    std::string  name;        //上传时的原始文件名
    std::string  ext;         //扩展名，不含点
    int64_t      size;        //文件总字节数
    int          scene;       //用途场景，取值见 enum file_scene
    int64_t      uploaderid;  //上传者用户userid
    int          status;      //状态，取值见 file_status_*
    int64_t      createtime;  //记录创建时间，Unix毫秒

    FileRecord()
        : fileid(0), size(0), scene(0), uploaderid(0),
          status(file_status_pending), createtime(0)
    {
    }
};

//文件元数据存储管理器；全局单例，经 Singleton<FileStoreManager> 访问
class FileStoreManager final
{
public:
    FileStoreManager();
    ~FileStoreManager();

    FileStoreManager(const FileStoreManager& rhs) = delete;
    FileStoreManager& operator=(const FileStoreManager& rhs) = delete;

    /**
     *  初始化（文件元数据不预加载到内存）。
     *  @return 恒返回 true
     */
    bool init();

    /**
     *  配置文件域运行参数；main.cpp 读取配置文件后调用。
     *  @param tokenKey  上传 / 下载令牌的 HMAC 密钥，与 fileserver/imgserver 共享
     *  @param imgHost   imgserver 对客户端公布的主机地址
     *  @param imgPort   imgserver 端口
     *  @param imgTls    imgserver 是否启用 TLS
     *  @param fileHost  fileserver 对客户端公布的主机地址
     *  @param filePort  fileserver 端口
     *  @param fileTls   fileserver 是否启用 TLS
     */
    void configure(const std::string& tokenKey,
                   const std::string& imgHost, int imgPort, bool imgTls,
                   const std::string& fileHost, int filePort, bool fileTls);

    /**
     *  取令牌 HMAC 密钥。
     */
    const std::string& getTokenKey() const;

    /**
     *  按文件场景取应连接的目标服务器地址。
     *  @param scene 文件场景，取值见 enum file_scene
     *  @param host  [出参] 目标服务器主机
     *  @param port  [出参] 目标服务器端口
     *  @param tls   [出参] 目标服务器是否启用 TLS
     */
    void getServerForScene(int scene, std::string& host, int& port, bool& tls) const;

    /**
     *  按 MD5 与场景查找一条已完成的文件记录，用于秒传判断。
     *
     *  只命中**同一场景**的记录，不跨场景复用，原因有两点：
     *    - 下载时令牌场景与目标服务器都取自记录本身。图片消息若复用了群文件的
     *      记录，接收方会被指向 fileserver，而客户端收图固定连 imgserver，
     *      imgserver 按场景拒绝，图片永远下载失败。
     *    - 同一台服务器上的两种场景也不能互相复用：下载鉴权只要 fileid 登记在
     *      群文件表里就要求调用者是群成员，聊天文件若复用了群文件的记录，
     *      不在该群的接收方同样会被拒绝。
     *  按场景精确匹配不会让同一内容在磁盘上存两份：fileserver / imgserver 接收
     *  上传前会按 MD5 判断磁盘上是否已有该文件，有则直接回复完成。
     *
     *  @param md5   文件内容MD5
     *  @param scene 本次上传的用途场景，取值见 enum file_scene
     *  @param out   [出参] 命中时回填文件记录
     *  @return 找到同场景的已完成记录返回 true；无命中或数据库出错返回 false
     */
    bool findCompletedByMd5(const std::string& md5, int scene, FileRecord& out);

    /**
     *  新建一条待上传（status=pending）的文件记录。
     *  @param md5        文件内容MD5
     *  @param name       原始文件名
     *  @param ext        扩展名，不含点
     *  @param size       文件总字节数
     *  @param scene      用途场景，取值见 enum file_scene
     *  @param uploaderid 上传者用户userid
     *  @param now        创建时间，Unix毫秒
     *  @return 新记录的 fileid；数据库出错返回 0
     */
    int64_t createPendingFile(const std::string& md5, const std::string& name,
                              const std::string& ext, int64_t size, int scene,
                              int64_t uploaderid, int64_t now);

    /**
     *  按 fileid 查文件记录。
     *  @param fileid 文件ID
     *  @param out    [出参] 找到时回填
     *  @return 找到返回 true
     */
    bool getFileById(int64_t fileid, FileRecord& out);

    /**
     *  把文件记录标记为已完成（status=completed），不校验调用者是否为上传者。
     *  目前只有登记群文件（cmd_add_group_file）调用；发消息、更新头像时引用 fileid
     *  走的是 markCompletedByUploader。
     *  @param fileid 文件ID
     *  @return 数据库访问成功返回 true
     */
    bool markCompleted(int64_t fileid);

    /**
     *  由上传者本人把文件记录标记为已完成，使后续同内容、同场景的上传可命中秒传。
     *
     *  发送方用某个 fileid 发出图片 / 文件消息、或把它设为头像时调用。客户端总是在
     *  上传成功之后才引用 fileid，所以此时文件已完整落在对应的文件服务器上。
     *
     *  只有记录的上传者本人引用时才会标记：只有上传者知道自己的上传确实完成了；
     *  别人（例如转发者）引用同一 fileid 时不改动记录，也就不可能把一份半途失败的
     *  上传标记为已完成。
     *
     *  @param fileid     文件ID
     *  @param uploaderid 引用该文件的用户 userid；与记录的上传者不同时不做任何修改
     *  @return 数据库访问成功返回 true（包括"不是上传者、未作修改"与"本已完成"两种
     *          情形）；数据库出错返回 false
     */
    bool markCompletedByUploader(int64_t fileid, int64_t uploaderid);

private:

    std::string m_tokenKey;       //上传/下载令牌 HMAC 密钥
    std::string m_imgHost;        //imgserver 对外主机地址
    int         m_imgPort;        //imgserver 端口
    bool        m_imgTls;         //imgserver 是否启用 TLS
    std::string m_fileHost;       //fileserver 对外主机地址
    int         m_filePort;       //fileserver 端口
    bool        m_fileTls;        //fileserver 是否启用 TLS
};
