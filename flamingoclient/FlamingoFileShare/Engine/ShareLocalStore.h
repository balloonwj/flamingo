/**
 *  文件共享的本机存储（DLL 自己的 SQLite 库），ShareLocalStore.h
 *
 *  库文件为 <个人目录>\ChatLog\fileshare.db，按账号隔离（docs/文件共享方案.md 7.6），三张表：
 *    - t_share_local：分享者本机的共享根记录，是「以本机记录为准」的那一份。发起共享时先以 clientshareid 写入
 *      （此时 shareid 为 0），拿到服务端应答后补上 shareid；共享被取消时删除；
 *    - t_share_download：访问者的下载续传位点。源文件的大小与修改时间都没变时才从 offset 续传，任何一项变了
 *      都作废并删除记录；
 *    - t_share_activity：分享者在「文件共享」窗口里看到的最近操作记录，只保留最近 N 条（默认 kShareActivityKeep）。
 *
 *  路径在库里存成 UTF-8。全部方法由一把互斥量串行化，可在任意线程调用。
 *  单元测试见 FlamingoFileShare/Tests/ShareLocalStoreTests.cpp（标签 [sharestore]，用 :memory: 库）。
 *
 *  典型用法（分享者发起共享）：
 *      store.AddPendingShare(clientShareId, rootPath, kind, includeHidden, nowMs);
 *      ...（发 2201，收到应答）
 *      store.BindShareId(clientShareId, shareId);
 *
 *  balloonwj@qq.com   2026-10-02
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <mutex>
#include <string>
#include <vector>

//前置声明 SQLite 连接句柄类型，避免在本头里包含 sqlite3.h
struct sqlite3;

namespace fileshare {

//最近操作记录默认保留的条数（方案 7.6 的 kShareActivityKeep）
const size_t kShareActivityKeep = 1000;

//t_share_local 的一行
struct ShareLocalRecord
{
    int64_t      m_shareId;         //共享 id；发起后服务端应答回来之前为 0
    std::string  m_clientShareId;   //发起时生成的 UUID
    std::wstring m_rootPath;        //共享根在本机的路径（磁盘为盘符根目录，单文件为文件本身）
    int          m_kind;            //共享类型，share_kind
    bool         m_includeHidden;   //是否包含隐藏文件与系统文件
    int64_t      m_createTime;      //发起时间（Unix 毫秒）

    ShareLocalRecord()
        : m_shareId(0)
        , m_kind(0)
        , m_includeHidden(false)
        , m_createTime(0)
    {
    }
};

//t_share_download 的一行
struct ShareDownloadRecord
{
    int64_t      m_shareId;      //共享 id
    std::string  m_relPath;      //文件在共享内的相对路径（'/' 分隔）
    int64_t      m_srcSize;      //开始下载时源文件的字节数
    int64_t      m_srcMtime;     //开始下载时源文件的修改时间（Unix 毫秒）
    std::wstring m_localPath;    //本机落地路径（正式文件名，半截文件为它加 .part）
    int64_t      m_offset;       //已经写到本机的字节数
    int64_t      m_updateTime;   //最后一次更新的时间（Unix 毫秒）

    ShareDownloadRecord()
        : m_shareId(0)
        , m_srcSize(0)
        , m_srcMtime(0)
        , m_offset(0)
        , m_updateTime(0)
    {
    }
};

//t_share_activity 的一行
struct ShareActivityRecord
{
    int64_t     m_id;          //自增主键；AddActivity 时忽略
    int64_t     m_shareId;     //共享 id
    int64_t     m_visitorId;   //访问者 userid
    int         m_action;      //动作，share_audit_action
    std::string m_path;        //相对共享根的路径
    int         m_result;      //0 成功，否则为错误码
    int64_t     m_time;        //时间（Unix 毫秒）

    ShareActivityRecord()
        : m_id(0)
        , m_shareId(0)
        , m_visitorId(0)
        , m_action(0)
        , m_result(0)
        , m_time(0)
    {
    }
};

/**
 *  CShareLocalStore —— 文件共享的本机 SQLite 库。不可拷贝（持有连接句柄与互斥量）。
 */
class CShareLocalStore
{
public:
    CShareLocalStore();
    ~CShareLocalStore();

    CShareLocalStore(const CShareLocalStore&) = delete;
    CShareLocalStore& operator=(const CShareLocalStore&) = delete;

    /**
     *  打开（不存在则创建）数据库并建表；已打开时先关闭再打开。
     *  @param dbPath 数据库全路径（UTF-16），父目录须已存在；传 L":memory:" 开内存库（单元测试用）
     *  @return 成功返回 true
     */
    bool Open(const wchar_t* dbPath);

    //关闭数据库（若已打开）。析构时自动调用
    void Close();

    //是否已打开
    bool IsOpen() const;

    /**
     *  设置最近操作记录保留的条数（单元测试用小值；默认 kShareActivityKeep）。
     *  @param keep 条数，至少为 1
     */
    void SetActivityKeep(size_t keep);

    //==== 共享根记录（分享者一端）====

    /**
     *  发起共享时写入一条还没有 shareid 的记录；同一个 clientShareId 已存在时覆盖。
     *  @return 成功返回 true
     */
    bool AddPendingShare(const std::string& clientShareId, const std::wstring& rootPath, int kind, bool includeHidden,
                         int64_t createTime);

    /**
     *  服务端应答回来后补上 shareid。
     *  @return 找到该 clientShareId 并更新成功返回 true
     */
    bool BindShareId(const std::string& clientShareId, int64_t shareId);

    //按 shareid 取记录；没有返回 false
    bool GetByShareId(int64_t shareId, ShareLocalRecord& out);

    //按 clientShareId 取记录；没有返回 false
    bool GetByClientShareId(const std::string& clientShareId, ShareLocalRecord& out);

    //列出全部记录，按发起时间升序
    bool ListShares(std::vector<ShareLocalRecord>& out);

    //共享被取消时删除记录（按 shareid）
    bool RemoveShare(int64_t shareId);

    //发起失败时删除还没有 shareid 的记录（按 clientShareId）
    bool RemovePendingShare(const std::string& clientShareId);

    //==== 下载续传位点（访问者一端）====

    /**
     *  保存或更新一个文件的下载进度（按 shareid + 相对路径唯一）。
     *  @return 成功返回 true
     */
    bool SaveDownloadProgress(const ShareDownloadRecord& rec);

    /**
     *  批量保存或更新若干文件的下载进度，在一个事务里完成（上万个文件逐条写时每条都要单独落盘，要几分钟）。
     *  @param recs 记录；为空时直接返回 true
     *  @return 全部写入成功返回 true；任一条失败时整批回滚并返回 false
     */
    bool SaveDownloadProgressBatch(const std::vector<ShareDownloadRecord>& recs);

    /**
     *  查续传位点。源文件的大小与修改时间都与记录一致才返回 true；任何一项不一致时删除该记录并返回 false
     *  （调用方从头下载）。
     *  @param shareId   共享 id
     *  @param relPath   相对路径
     *  @param srcSize   源文件此刻的字节数（来自本次清单）
     *  @param srcMtime  源文件此刻的修改时间（来自本次清单）
     *  @param localPath [出参] 本机落地路径
     *  @param offset    [出参] 已写到本机的字节数
     *  @return 可以续传返回 true
     */
    bool GetResumePoint(int64_t shareId, const std::string& relPath, int64_t srcSize, int64_t srcMtime,
                        std::wstring& localPath, int64_t& offset);

    /**
     *  列出一个共享上的全部续传记录（按相对路径排序）。下载开始前据此判断顶层条目是不是上次没下完的那个，
     *  是则沿用原来的落地名字接着下载，而不是改名重下。
     *  @param shareId 共享 id
     *  @param out     [出参] 记录
     *  @return 成功返回 true
     */
    bool ListDownloads(int64_t shareId, std::vector<ShareDownloadRecord>& out);

    //下载完成或放弃后删除续传记录
    bool RemoveDownload(int64_t shareId, const std::string& relPath);

    /**
     *  批量删除一个共享上若干文件的续传记录，在一个事务里完成（下载完成时用）。
     *  @param shareId  共享 id
     *  @param relPaths 文件在共享内的相对路径
     *  @return 成功返回 true；失败时整批回滚
     */
    bool RemoveDownloads(int64_t shareId, const std::vector<std::string>& relPaths);

    //==== 最近操作记录（分享者一端）====

    /**
     *  追加一条操作记录，并删掉超出保留条数的旧记录。
     *  @return 成功返回 true
     */
    bool AddActivity(const ShareActivityRecord& rec);

    /**
     *  取最近的若干条操作记录，最新的在前。
     *  @param limit 最多取的条数
     *  @param out   [出参] 记录
     *  @return 成功返回 true
     */
    bool ListActivities(size_t limit, std::vector<ShareActivityRecord>& out);

private:
    //建三张表（已存在则跳过）
    bool CreateTables();

    //执行一条不带参数、不取结果的 SQL
    bool ExecLocked(const char* sql);

    //按 WHERE 子句取一条共享根记录（whereSql 带一个文本或整数参数，由调用方绑定）
    bool QueryShareLocked(const char* whereSql, const std::string* textKey, int64_t intKey, ShareLocalRecord& out);

private:
    mutable std::mutex m_mutex;          //串行化全部数据库访问
    sqlite3*           m_db;             //连接句柄；未打开时为 NULL
    size_t             m_activityKeep;   //最近操作记录保留的条数
};

} // namespace fileshare
