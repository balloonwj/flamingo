/**
 *  v2 群文件库信令报文编解码（客户端侧），GroupFileProtocolClient.h
 *
 *  群文件库是群聊窗右侧「群文件」标签页的后端：群里共享过的文件汇总成一个
 *  可搜索 / 分类筛选 / 分页的列表，并支持上传登记。本模块覆盖客户端要构造 /
 *  解析的全部群文件库信令（均在 chatserver 连接上收发）：
 *    cmd_get_group_files(1803)：拉取群文件列表（分页 + 类型筛选）；
 *    cmd_add_group_file(1804)：把一个已上传到 fileserver 的文件登记进群文件库；
 *    cmd_group_files_changed_notify(1806)：服务端推送群文件库的新增 / 删除变更。
 *  另附 cmd_delete_group_file(1805) 请求构造，供后续删除入口使用（本轮 UI 不接）。
 *
 *  本模块只产出 / 解析 JSON 文本，不负责封帧——封帧是 net/FrameCodec 的职责；
 *  也不触及网络与 UI，是纯逻辑，只依赖 jsoncpp，便于单元测试，见
 *  flamingoclient/Source/Tests/GroupFileProtocolClientTests.cpp。
 *
 *  本模块与服务端 chatserversrc/GroupFileProtocol 互为镜像：服务端解析请求 /
 *  构造应答与推送，客户端构造请求 / 解析应答与推送。所有大整数 ID（群ID / 文件ID /
 *  条目ID / 上传者ID）一律以字符串编码进 JSON，避免 JSON number 的精度问题；
 *  文件大小与登记时间则以 JSON 整型编码。逐字段与服务端 GroupFileProtocol.cpp 对齐。
 *
 *  balloonwj@qq.com   2026-06-08
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace net {

//一条群文件条目：cmd_get_group_files 应答的列表项、cmd_add_group_file 应答的
//file、cmd_group_files_changed_notify 新增推送的 file 三处共用的解析结果。
//文件名称 / 扩展名 / 大小 / MD5 是文件本身的元数据；entryid 是「该文件在本群文件库
//里的登记条目ID」（删除时用），与 fileid（文件本体ID，下载时用）是两个不同的ID。
struct GroupFileEntry
{
    std::string m_entryid;      //群文件条目ID（字符串编码int64，对外即entryid，删除群文件用）
    std::string m_fileid;       //文件本体ID（字符串编码int64，下载用）
    std::string m_name;         //原始文件名（UTF-8，含扩展名）
    std::string m_ext;          //扩展名，不含点（UTF-8，可空；用于客户端按类型分类与取图标）
    int64_t     m_size;         //文件总字节数
    std::string m_md5;          //文件内容MD5（小写十六进制32位），下载完成后据此校验
    int         m_filetype;     //服务端文件类别（见 group_file_type：1文档 2图片 …6其它）
    std::string m_uploaderid;   //登记者(上传者)userid（字符串编码int64；UI 据此判断是否本人上传）
    std::string m_uploadername; //登记者显示名（UTF-8，服务端已解析好，UI 直接显示）
    int64_t     m_uploadtime;   //登记时间（Unix 毫秒），UI 据此显示"今天/昨天/日期"

    GroupFileEntry()
        : m_size(0)
        , m_filetype(0)
        , m_uploadtime(0)
    {
    }
};

//cmd_get_group_files(1803) 应答的解析结果。
//成功（m_code==0）时各字段有效：m_files 为本页文件条目（服务端按登记时间倒序），
//m_total 为该群该筛选条件下的文件总数，m_hasmore 指明是否还有后续页（客户端据此
//循环翻页拉全）。m_groupid / m_filetype / m_offset 为服务端回显的本次请求参数。
struct GetGroupFilesResult
{
    int         m_code;      //结果码，0成功，非0见 enum error_code
    std::string m_msg;       //结果描述（成功为"ok"，失败为诊断信息）
    std::string m_groupid;   //群ID（回显，字符串编码int64）
    int         m_filetype;  //本次筛选类别（回显，见 group_file_type）
    int         m_offset;    //本页起始偏移（回显）
    int         m_total;     //该群该筛选条件下的文件总数
    bool        m_hasmore;   //是否还有后续页：(offset + 本页条数) < total
    std::vector<GroupFileEntry> m_files;  //本页文件条目（按登记时间倒序）

    GetGroupFilesResult()
        : m_code(-1)         //-1：尚未解析出有效结果码，避免被误判为成功
        , m_filetype(0)
        , m_offset(0)
        , m_total(0)
        , m_hasmore(false)
    {
    }
};

//cmd_add_group_file(1804) 应答的解析结果。
//成功（m_code==0）时 m_file 为刚登记进群文件库的条目详情（含服务端分配的 entryid
//与上传时间），客户端据此把新文件即时插入列表、无需重新拉全。
struct AddGroupFileResult
{
    int            m_code;  //结果码，0成功，非0见 enum error_code
    std::string    m_msg;   //结果描述
    GroupFileEntry m_file;  //登记成功的群文件条目（仅 m_code==0 时有效）

    AddGroupFileResult()
        : m_code(-1)
    {
    }
};

//cmd_delete_group_file(1805) 应答的解析结果。成功 m_code==0（服务端随后推 1806 移除该
//文件，客户端据 1806 更新列表，故本应答只用于成功 / 失败日志）；失败 m_msg 为原因。
struct DeleteGroupFileResult
{
    int         m_code;     //结果码，0成功，非0见 enum error_code（如权限不足 / 不存在）
    std::string m_msg;      //失败描述（成功时可空）
    std::string m_entryid;  //成功时回带的群文件条目ID（字符串编码int64；失败时为空）

    DeleteGroupFileResult()
        : m_code(-1)
    {
    }
};

//cmd_group_files_changed_notify(1806) 推送的解析结果。
//m_changetype==group_file_change_added(1) 时 m_file 有效（某文件被登记进库）；
//m_changetype==group_file_change_removed(2) 时 m_entryid 有效（某群文件被移除）。
//客户端据此对本地缓存做增量更新，不必整列表重拉。
struct GroupFilesChangedNotify
{
    std::string    m_groupid;     //发生变更的群ID（字符串编码int64）
    int            m_changetype;  //变更类型（见 group_file_change_type：1新增 2删除）
    GroupFileEntry m_file;        //新增的条目（仅 m_changetype==1 有效）
    std::string    m_entryid;     //被删除条目的ID（仅 m_changetype==2 有效，字符串编码int64）

    GroupFilesChangedNotify()
        : m_changetype(0)
    {
    }
};

//群文件库信令报文编解码工具类；全部为静态方法，不可实例化（同 FileProtocolClient）。
class GroupFileProtocolClient
{
public:
    /**
     *  构造 cmd_get_group_files(1803) 请求 JSON 包体（紧凑 UTF-8）。
     *  @param groupid  目标群ID（>0）；以字符串编码写入 JSON
     *  @param filetype 类型筛选，取值见 enum group_file_type（0=全部，1-6=具体类别）。
     *                  注：本客户端按 Option B 客户端全量缓存，实际只用 0 拉全；保留
     *                  本参数以备服务端驱动筛选或单测覆盖
     *  @param offset   分页偏移（>=0）：本次请求从第 offset 条开始
     *  @param count    本页期望条数；服务端对 <=0 取缺省20、>50 钳到50
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildGetGroupFilesRequest(int64_t groupid, int filetype,
                                                 int offset, int count);

    /**
     *  解析 cmd_get_group_files(1803) 应答 JSON 包体。
     *  只要 JSON 合法且含整型 code 即返回 true，调用方据 out.m_code 判定结果；
     *  m_code==0 时 m_files / m_total / m_hasmore 等有效。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseGetGroupFilesResponse(const std::string& jsonbody, GetGroupFilesResult& out);

    /**
     *  构造 cmd_add_group_file(1804) 请求 JSON 包体（紧凑 UTF-8）。
     *  把一个已上传到 fileserver（scene=file_scene_group_file）的文件登记进群文件库。
     *  @param groupid 目标群ID（>0）；以字符串编码写入
     *  @param fileid  已上传文件的文件ID（来自 cmd_request_upload 应答，字符串编码int64）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildAddGroupFileRequest(int64_t groupid, const std::string& fileid);

    /**
     *  解析 cmd_add_group_file(1804) 应答 JSON 包体。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；m_code==0 时 m_file 有效
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseAddGroupFileResponse(const std::string& jsonbody, AddGroupFileResult& out);

    /**
     *  构造 cmd_delete_group_file(1805) 请求 JSON 包体（紧凑 UTF-8）。
     *  本轮 UI 不接删除入口，提供本方法供后续删除功能或调试使用。
     *  @param groupid 目标群ID（>0）
     *  @param entryid 待删除的群文件条目ID（字符串编码int64，来自列表项 m_entryid）
     *  @return 紧凑格式的请求 JSON 文本
     */
    static std::string buildDeleteGroupFileRequest(int64_t groupid, const std::string& entryid);

    /**
     *  解析 cmd_delete_group_file(1805) 应答 JSON 包体。
     *  @param jsonbody 应答包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；m_code==0 为删除成功（随后服务端推 1806）
     *  @return JSON 合法且含 code 返回 true，否则 false
     */
    static bool parseDeleteGroupFileResponse(const std::string& jsonbody, DeleteGroupFileResult& out);

    /**
     *  解析 cmd_group_files_changed_notify(1806) 推送 JSON 包体。
     *  @param jsonbody 推送包体（UTF-8 JSON 文本）
     *  @param out      [出参] 解析结果；据 m_changetype 取 m_file 或 m_entryid
     *  @return JSON 合法、含字符串 groupid 与整型 changetype 返回 true，否则 false
     */
    static bool parseGroupFilesChangedNotify(const std::string& jsonbody, GroupFilesChangedNotify& out);

private:
    GroupFileProtocolClient() = delete;
    ~GroupFileProtocolClient() = delete;
    GroupFileProtocolClient(const GroupFileProtocolClient&) = delete;
    GroupFileProtocolClient& operator=(const GroupFileProtocolClient&) = delete;
};

} // namespace net
