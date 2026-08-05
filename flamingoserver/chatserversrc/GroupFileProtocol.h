/**
 *  v2 群文件报文（cmd_get_group_files / cmd_add_group_file /
 *  cmd_delete_group_file / cmd_group_files_changed_notify）的编解码。
 *
 *  群文件功能把「已上传到 fileserver 的文件」登记进某个群的文件库：客户端
 *  先走 cmd_request_upload（scene=2）拿到 fileid 并上传字节，再用本模块的
 *  cmd_add_group_file 把该 fileid 登记进群；其余成员经 cmd_get_group_files
 *  分页查看、经 cmd_request_download 下载。本模块只做报文编解码与文件类别
 *  判定，是纯逻辑——只依赖 jsoncpp，不触及网络与数据库，便于单元测试
 *  （见 tests/test_groupfileprotocol.cpp）。落库在 GroupFileManager，
 *  鉴权与推送在会话层 ChatSession。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "Protocol.h"   //group_file_type / group_file_change_type 枚举

//cmd_get_group_files 单页条数上限：请求 count 超过此值会被钳到此值
constexpr int GROUP_FILE_PAGE_MAX = 50;
//cmd_get_group_files 单页条数缺省值：请求未给 count 或 count<=0 时取用
constexpr int GROUP_FILE_PAGE_DEFAULT = 20;

//一条群文件条目；cmd_add_group_file 应答、cmd_get_group_files 列表项、
//cmd_group_files_changed_notify 推送三处共用。文件名/扩展名/大小/MD5
//源自 t_file，登记关系字段源自 t_group_file。
struct GroupFileEntry
{
    int64_t      entryid;       //群文件条目ID，即 t_group_file 自增主键，删除时定位用
    int64_t      fileid;        //文件ID，引用 t_file.f_id
    std::string  name;          //上传时的原始文件名
    std::string  ext;           //扩展名，不含点
    int64_t      size;          //文件总字节数
    std::string  md5;           //文件内容MD5（小写十六进制，32位）
    int          filetype;      //文件类别，取值见 enum group_file_type
    int64_t      uploaderid;    //登记者（上传者）用户userid
    std::string  uploadername;  //登记者昵称；由会话层从 UserManager 补全
    int64_t      uploadtime;    //登记进群文件库的时间，Unix毫秒

    GroupFileEntry()
        : entryid(0), fileid(0), size(0), filetype(group_file_type_other),
          uploaderid(0), uploadtime(0)
    {
    }
};

//cmd_add_group_file 请求
struct AddGroupFileRequest
{
    int64_t groupid;   //目标群ID
    int64_t fileid;    //待登记的文件ID（须为已上传完成的 t_file 记录）

    AddGroupFileRequest()
        : groupid(0), fileid(0)
    {
    }
};

//cmd_get_group_files 请求
struct GetGroupFilesRequest
{
    int64_t groupid;    //目标群ID
    int     filetype;   //筛选类别：0 全部，其余取值见 enum group_file_type
    int     offset;     //分页偏移，从 0 起
    int     count;      //本页期望条数，已由解析阶段钳到 [1, GROUP_FILE_PAGE_MAX]

    GetGroupFilesRequest()
        : groupid(0), filetype(group_file_type_all),
          offset(0), count(GROUP_FILE_PAGE_DEFAULT)
    {
    }
};

//cmd_delete_group_file 请求
struct DeleteGroupFileRequest
{
    int64_t groupid;   //目标群ID（须与该条目实际所属群一致）
    int64_t entryid;   //待删除的群文件条目ID

    DeleteGroupFileRequest()
        : groupid(0), entryid(0)
    {
    }
};

//群文件报文编解码工具类；全部为静态方法，不可实例化
class GroupFileProtocol
{
public:
    /**
     *  解析 cmd_add_group_file 请求。
     *  @param json 请求 JSON 文本
     *  @param req  [出参] 解析结果
     *  @return groupid / fileid 缺失或非正返回 false，否则 true
     */
    static bool parseAddGroupFileRequest(const std::string& json, AddGroupFileRequest& req);

    /**
     *  解析 cmd_get_group_files 请求。
     *  groupid 缺失或非正返回 false；filetype 越界（<0 或 >6）归 0（全部）；
     *  count<=0 取缺省 GROUP_FILE_PAGE_DEFAULT、>上限则钳到 GROUP_FILE_PAGE_MAX；
     *  offset<0 归 0。即除 groupid 外的字段一律纠正而非拒绝。
     *  @param json 请求 JSON 文本
     *  @param req  [出参] 解析并纠正后的结果
     *  @return groupid 合法返回 true
     */
    static bool parseGetGroupFilesRequest(const std::string& json, GetGroupFilesRequest& req);

    /**
     *  解析 cmd_delete_group_file 请求。
     *  @param json 请求 JSON 文本
     *  @param req  [出参] 解析结果
     *  @return groupid / entryid 缺失或非正返回 false，否则 true
     */
    static bool parseDeleteGroupFileRequest(const std::string& json, DeleteGroupFileRequest& req);

    /**
     *  构造 cmd_add_group_file 成功应答（code 恒为 0）；错误应答用 buildSimpleResponse。
     *  @param entry 已登记的群文件条目
     *  @return 应答 JSON 文本
     */
    static std::string buildAddGroupFileResponse(const GroupFileEntry& entry);

    /**
     *  构造 cmd_get_group_files 成功应答（code 恒为 0）。
     *  @param groupid 群ID
     *  @param filetype 本次筛选用的类别（回显请求值）
     *  @param offset   本页起始偏移（回显请求值）
     *  @param total    该群该类别下的文件总数（用于客户端分页）
     *  @param files    本页文件条目；应答内的 hasmore 由 offset+files.size()<total 算定
     *  @return 应答 JSON 文本
     */
    static std::string buildGetGroupFilesResponse(int64_t groupid, int filetype, int offset,
                                                  int total,
                                                  const std::vector<GroupFileEntry>& files);

    /**
     *  构造 cmd_delete_group_file 成功应答（code 恒为 0）。
     *  @param groupid 群ID
     *  @param entryid 已删除的条目ID
     *  @return 应答 JSON 文本
     */
    static std::string buildDeleteGroupFileResponse(int64_t groupid, int64_t entryid);

    /**
     *  构造 cmd_group_files_changed_notify 推送（新增）：changetype=1，携 file 详情。
     *  @param groupid 群ID
     *  @param entry   新增的群文件条目
     *  @return 推送 JSON 文本
     */
    static std::string buildFileAddedNotify(int64_t groupid, const GroupFileEntry& entry);

    /**
     *  构造 cmd_group_files_changed_notify 推送（删除）：changetype=2，携 entryid。
     *  @param groupid 群ID
     *  @param entryid 被删除的条目ID
     *  @return 推送 JSON 文本
     */
    static std::string buildFileRemovedNotify(int64_t groupid, int64_t entryid);

    /**
     *  构造通用 { code, msg } 应答，用于各类错误返回。
     */
    static std::string buildSimpleResponse(int code, const std::string& msg);

    /**
     *  按文件扩展名算定群文件类别。
     *  @param ext 扩展名，不含点；大小写不敏感（内部转小写比较）；空串归「其它」
     *  @return 文件类别，取值见 enum group_file_type（document/image/video/audio/archive/other）
     */
    static int classifyFileType(const std::string& ext);

private:
    GroupFileProtocol() = delete;
    ~GroupFileProtocol() = delete;
    GroupFileProtocol(const GroupFileProtocol&) = delete;
    GroupFileProtocol& operator=(const GroupFileProtocol&) = delete;
};
