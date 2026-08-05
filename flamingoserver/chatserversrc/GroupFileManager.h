/**
 *  群文件存储管理器，GroupFileManager.h
 *
 *  负责 v2 群文件登记表 t_group_file 的落库与查询。一行 t_group_file 表示
 *  「某文件被登记进某群的文件库」——文件元数据（名称/扩展名/大小/MD5）仍在
 *  t_file，本表只存「群↔文件」登记关系，故删除群文件只删本表行、不动 t_file
 *  与磁盘文件（内容去重、秒传、他群可能引用同一文件）。
 *
 *  每次数据库操作单独建立一条连接（与 FileStoreManager / GroupManager 同一
 *  风格），群文件不在内存缓存。
 *
 *  典型用法：main.cpp 启动时 Singleton<GroupFileManager>::Instance().init(...)；
 *  会话层在 cmd_add_group_file / cmd_get_group_files / cmd_delete_group_file
 *  与 cmd_request_download 的群文件鉴权处调用。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "GroupFileProtocol.h"

//群文件存储管理器；全局单例，经 Singleton<GroupFileManager> 访问
class GroupFileManager final
{
public:
    GroupFileManager();
    ~GroupFileManager();

    GroupFileManager(const GroupFileManager& rhs) = delete;
    GroupFileManager& operator=(const GroupFileManager& rhs) = delete;

    /**
     *  初始化（群文件不预加载到内存）。
     *  @return 恒返回 true
     */
    bool init();

    /**
     *  把一个文件登记进某群的文件库，新增一行 t_group_file。
     *  @param groupid    目标群ID
     *  @param fileid     文件ID，引用 t_file.f_id
     *  @param filetype   文件类别，取值见 enum group_file_type
     *  @param uploaderid 登记者用户userid
     *  @param now        登记时间，Unix毫秒
     *  @return 新条目的 entryid（t_group_file 自增主键）；数据库出错返回 0
     */
    int64_t addGroupFile(int64_t groupid, int64_t fileid, int filetype,
                         int64_t uploaderid, int64_t now);

    /**
     *  分页查询某群的群文件列表，并回填总数。文件元数据由 t_group_file
     *  JOIN t_file 取得；按登记时间倒序（最新在前）。
     *  @param groupid  目标群ID
     *  @param filetype 筛选类别：group_file_type_all(0) 表示不筛选；其余按该类别筛选
     *  @param offset   分页偏移，从 0 起
     *  @param count    本页期望条数（调用方须已钳到合理范围）
     *  @param out      [出参] 本页群文件条目（uploadername 字段留空，由会话层补全）
     *  @param total    [出参] 该群该筛选条件下的文件总数
     *  @return 数据库访问成功返回 true
     */
    bool getGroupFiles(int64_t groupid, int filetype, int offset, int count,
                       std::vector<GroupFileEntry>& out, int& total);

    /**
     *  按条目ID取一条群文件登记记录的关键字段，供删除前的归属与权限校验。
     *  @param entryid    群文件条目ID
     *  @param groupid    [出参] 该条目实际所属的群ID
     *  @param fileid     [出参] 该条目引用的文件ID
     *  @param uploaderid [出参] 该条目的登记者userid
     *  @return 条目存在返回 true（出参才有效）
     */
    bool getEntryById(int64_t entryid, int64_t& groupid,
                      int64_t& fileid, int64_t& uploaderid);

    /**
     *  删除一条群文件登记记录（只删 t_group_file 行，不动 t_file 与磁盘文件）。
     *  @param entryid 群文件条目ID
     *  @return 数据库访问成功返回 true
     */
    bool deleteGroupFile(int64_t entryid);

    /**
     *  下载鉴权查询：一次查询同时判定「某文件是否为群文件」与「调用者是否
     *  有权下载它」。会话层据此决定群文件走成员校验、非群文件走持令牌即可。
     *  @param fileid      待下载文件ID
     *  @param userid      发起下载的用户userid
     *  @param isGroupFile [出参] 该 fileid 是否已登记为任一群的群文件
     *  @param isMember    [出参] 调用者是否为「持有该文件的某个群」的成员
     *  @return 数据库访问成功返回 true（出参才有效）；出错返回 false
     */
    bool checkDownloadAccess(int64_t fileid, int64_t userid,
                             bool& isGroupFile, bool& isMember);

private:
};
