/**
 *  平台公告本地库——SQLite 读写封装，AnnounceStore.h
 *
 *  主面板「公告中心」里那些公告的本地落点。公告由企业管理控制台发布、经 chatserver
 *  投递（cmd_get_announcements / cmd_announcement_notify），本库把收到的逐条存下来，
 *  于是：
 *    · 离线也能打开公告中心回看历史公告；
 *    · 重新登录不必每次全量拉，带上本地最大公告ID做增量拉取即可；
 *    · 未读数直接从库里数，不依赖服务端每次都算一遍。
 *
 *  服务端只提供「发给我的公告有哪些」，<u>不</u>承担客户端的历史存储——已撤回的公告
 *  服务端下次就不返回了，本地却仍要留着那一行（标记为已撤回），这也是本库存在的理由
 *  之一。
 *
 *  以 announcementid 作主键：它由服务端全局分配、单调递增，天然去重，也正好用作增量
 *  拉取的游标。
 *
 *  单例：公告中心窗口（读）与主面板收公告（写）都要用它，穿线传递不如取全局一份。
 *  库文件与历史消息库同目录、按账号隔离（<个人目录>\ChatLog\announce.db），登录后由
 *  主面板 Open，账号切换时改开新库。
 *
 *  数据库读写用例见 flamingoclient/Source/Tests/AnnounceStoreTests.cpp（用 :memory: 库）。
 *
 *  balloonwj@qq.com   2026-08-05
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <mutex>

//前置声明 SQLite 连接句柄类型，避免在本头里包含 sqlite3.h。
struct sqlite3;

namespace balloonhistory {

/**
 *  AnnounceRecord —— 本地库里的一条平台公告。
 *
 *  字段与 net::AnnouncementItem 基本一一对应，多出一个 m_recalled：服务端撤回一条公告
 *  之后就不再下发它了，本地仍保留这一行并打上撤回标记，供「用户刚才还在看这条」这种
 *  场景给出正确的提示，而不是让它凭空消失。
 *
 *  文本字段一律是 <u>UTF-8</u>（与网络层同口径），交给界面前须转 UTF-16。
 */
struct AnnounceRecord
{
    int64_t     m_announcementId;  //公告ID，主键；服务端全局分配、单调递增
    std::string m_title;           //标题（UTF-8）
    std::string m_body;            //正文（UTF-8）
    int         m_severity;        //级别，取值见 net/Protocol.h 的 announcement_severity
    std::string m_senderName;      //发布者名称（UTF-8），如「平台运营」
    int         m_pinned;          //是否置顶：0 否 1 是
    int64_t     m_sentTimeMs;      //发布时间，Unix 毫秒
    int         m_read;            //本人是否已读：0 未读 1 已读
    int         m_recalled;        //是否已被管理员撤回：0 否 1 是；撤回的不在公告中心里显示

    AnnounceRecord()
        : m_announcementId(0)
        , m_severity(0)
        , m_pinned(0)
        , m_sentTimeMs(0)
        , m_read(0)
        , m_recalled(0)
    {
    }
};

/**
 *  CAnnounceStore —— 平台公告本地库。
 *
 *  线程：读写都发生在 UI 线程（主面板收到投递后写、公告中心窗口渲染时读），内部仍以
 *  std::mutex 自保护，为将来挪到 worker 线程留好接口。不可拷贝（持有连接句柄与互斥量）。
 */
class CAnnounceStore
{
public:
    /**
     *  取全局单例。首次调用时构造，进程退出时随静态对象析构（析构里会 Close）。
     */
    static CAnnounceStore& GetInstance();

    /**
     *  打开（不存在则创建）数据库文件并建表；幂等。
     *  已打开且路径相同时直接返回 true；路径不同（账号切换）时先关旧库再开新库。
     *
     *  打开时会顺带给存量库补齐后加的列，详见 .cpp 里 kSchemaColumns 的说明。
     *
     *  @param dbPathUtf16 数据库全路径（UTF-16，含文件名）；其父目录须已存在。
     *                     传 L":memory:" 可开一个纯内存库，测试用。
     *  @return 打开并建表成功返回 true；参数为空或失败返回 false（此时保持未打开态）
     */
    bool Open(const wchar_t* dbPathUtf16);

    //关闭数据库连接；未打开时调用无副作用。
    void Close();

    //是否已成功打开（粗判，用于决定是否需要 Open；不加锁）。
    bool IsOpen() const;

    /**
     *  写入 / 更新一条公告。以 announcementid 为主键，已存在即覆盖内容。
     *
     *  <u>不会</u>把已经落好的已读与撤回标记冲掉：同一条公告重复落库是常态（登录时全量
     *  拉一次、之后推送又来一次），若用整行替换，用户读过的公告会在下次拉取后重新变成
     *  未读。故冲突时只更新内容类字段（标题 / 正文 / 级别 / 发布者 / 置顶 / 发布时间），
     *  read 与 recalled 保持库里原值。
     *
     *  唯一的例外是 read：服务端下发的 m_read 为 1（在别的设备上读过）时会把本地置为已读
     *  ——已读只能从「未读」走向「已读」，不会倒退。
     *
     *  @param rec 待写入的公告；m_announcementId 非正时直接返回 false
     *  @return 写入成功返回 true
     */
    bool Upsert(const AnnounceRecord& rec);

    /**
     *  把某条公告标记为已读。公告不存在时什么也不做（返回 true，属幂等操作）。
     *  @param announcementId 公告ID
     *  @return 执行成功返回 true
     */
    bool MarkRead(int64_t announcementId);

    /**
     *  把某条公告标记为已被撤回。公告不存在时什么也不做（返回 true）。
     *  撤回的公告仍留在库里，只是不再出现在公告中心列表中。
     *  @param announcementId 公告ID
     *  @return 执行成功返回 true
     */
    bool MarkRecalled(int64_t announcementId);

    /**
     *  取出全部<u>未撤回</u>的公告，按公告中心的展示顺序排好：置顶的在前，其余按发布
     *  时间由新到旧；发布时间相同时按公告ID倒序，保证顺序稳定、不会每次刷新都跳动。
     *  @param out [出参] 公告列表，先清空再填充
     *  @return 查询成功返回 true（一条都没有也算成功）
     */
    bool QueryAll(std::vector<AnnounceRecord>& out);

    /**
     *  取本地已有的最大公告ID，作增量拉取（cmd_get_announcements 的 sinceid）的游标。
     *
     *  这里<u>要</u>把已撤回的也算进去：它们的ID一样占位，漏算会导致每次登录都把那几条
     *  已撤回的公告重新拉一遍。
     *
     *  @return 最大公告ID；库为空或未打开时返回 0（0 即表示全量拉取）
     */
    int64_t GetMaxId();

    /**
     *  数未读公告条数（未撤回且未读），供主面板 Banner 上的铃铛角标显示。
     *  @return 未读条数；未打开时返回 0
     */
    int GetUnreadCount();

private:
    CAnnounceStore();
    ~CAnnounceStore();
    CAnnounceStore(const CAnnounceStore&) = delete;
    CAnnounceStore& operator=(const CAnnounceStore&) = delete;

    //在已持锁的前提下执行一条无结果集的 SQL；未打开或语句为空时返回 false。
    bool ExecLocked(const char* sql);

    /**
     *  在已持锁的前提下，给存量库补齐 kSchemaColumns 里声明、但表里还没有的列。
     *  建表语句是 CREATE TABLE IF NOT EXISTS，库文件已存在时它整条跳过，后加的列
     *  永远不会自己出现，故须在此显式 ALTER 补上。
     */
    void EnsureColumnsLocked();

private:
    sqlite3*           m_db;      //SQLite 连接句柄；NULL 表示未打开
    std::wstring       m_dbPath;  //当前已打开的库路径（账号切换时据此判断是否需重开）
    mutable std::mutex m_mutex;   //保护 m_db / m_dbPath 的全部访问
};

} // namespace balloonhistory
