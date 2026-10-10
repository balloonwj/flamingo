/**
 *  录像记录本地库——SQLite 读写封装，RecordLocalStore.h
 *
 *  记录本账号录过的每一段录像（路径、时长、大小、录制结束时间），供 FlamingoMedia.dll 的「录制管理」窗口列出、
 *  播放、重命名与删除（docs/屏幕共享与录制方案.md 第 11.6 节 F9）。录像文件本身不归本库管理：文件被外部删除或
 *  改名后记录仍然保留，由录制管理窗口显示为「文件已不存在」。
 *
 *  写法与 CFileLocalStore 相同：单例，库文件与历史消息库同目录、按账号隔离（<个人目录>\ChatLog\recordlocal.db），
 *  登录后由主面板 EnsureMsgStoreOpen 打开，账号切换时改开新库。DLL 不直接访问本库，而是经 IMediaHost 的
 *  录像记录方法由 exe（CMediaHostAdapter）转调。
 *
 *  典型用法：
 *      RecordLocalEntry e;
 *      e.m_path = L"D:\\Videos\\20261005_131807.mp4";
 *      e.m_durationMs = 83000;
 *      CRecordLocalStore::GetInstance().Add(e, &id);
 *      std::vector<RecordLocalEntry> all;
 *      CRecordLocalStore::GetInstance().List(all);   // 最新的在前
 *
 *  读写用例见 flamingoclient/Source/Tests/RecordLocalStoreTests.cpp（用 :memory: 库，标签 [recordstore]）。
 *
 *  balloonwj@qq.com   2026-10-05
 */
#pragma once

#include <stdint.h>
#include <mutex>
#include <string>
#include <vector>

//前置声明 SQLite 连接句柄类型，避免在本头里包含 sqlite3.h。
struct sqlite3;

namespace balloonhistory {

//一条录像记录
struct RecordLocalEntry
{
    int64_t      m_id;           //记录 id（自增主键）；添加前不需要填写
    std::wstring m_path;         //录像文件完整路径（UTF-16）
    int64_t      m_durationMs;   //录像时长（毫秒）
    int64_t      m_sizeBytes;    //文件字节数（录制完成时的大小）
    int64_t      m_endTimeMs;    //录制结束时间（Unix 毫秒，UTC）

    RecordLocalEntry();
};

/**
 *  CRecordLocalStore —— 录像记录本地库。
 *
 *  线程：读写都发生在 UI 线程（录制完成的消息与录制管理窗口都在 UI 线程上处理），内部仍以 std::mutex 自保护。
 *  不可拷贝（持有连接句柄与互斥量）。
 */
class CRecordLocalStore
{
public:
    /**
     *  取全局单例。首次调用时构造，进程退出时随静态对象析构（析构里会 Close）。
     */
    static CRecordLocalStore& GetInstance();

    /**
     *  本账号录像记录库的完整路径：<个人目录>ChatLog\recordlocal.db。主面板与宿主适配器都按它打开，库文件名只写在这一处。
     *  @param personalFolder 当前账号的个人目录（CUserMgr::GetPersonalFolder，以反斜杠结尾）；为空时返回空串（尚未登录）
     */
    static std::wstring DbPathIn(const std::wstring& personalFolder);

    /**
     *  打开（不存在则创建）数据库文件并建表；幂等。
     *  已打开且路径相同时直接返回 true；路径不同（账号切换）时先关旧库再开新库。
     *  @param dbPathUtf16 数据库全路径（UTF-16，含文件名）；其父目录须已存在。传 L":memory:" 可开内存库（单元测试用）
     *  @return 打开并建表成功返回 true
     */
    bool Open(const wchar_t* dbPathUtf16);

    //关闭数据库（若已打开）。析构时自动调用。
    void Close();

    //是否已成功打开（粗判，用于决定是否需要 Open；不加锁）。
    bool IsOpen() const;

    /**
     *  添加一条录像记录。同一路径（不区分大小写）已有记录时覆盖它的时长、大小与结束时间，记录 id 不变。
     *  @param entry 要添加的记录；m_path 不能为空，m_id 被忽略
     *  @param outId [出参，可为 NULL] 写入成功时为这条记录的 id（覆盖时为原有记录的 id）
     *  @return 写入成功返回 true；库未打开或路径为空返回 false
     */
    bool Add(const RecordLocalEntry& entry, int64_t* outId);

    /**
     *  列出全部录像记录，按录制结束时间从新到旧排序，结束时间相同时 id 大的在前。
     *  @param out [出参] 先清空再填入
     *  @return 查询成功返回 true（没有记录也算成功）；库未打开返回 false
     */
    bool List(std::vector<RecordLocalEntry>& out);

    /**
     *  修改一条记录的文件路径（录制管理窗口里重命名文件之后调用）。
     *  @param id      记录 id
     *  @param newPath 新的完整路径，不能为空
     *  @return 修改成功返回 true；库未打开、路径为空、id 不存在或新路径已被另一条记录占用时返回 false
     */
    bool UpdatePath(int64_t id, const std::wstring& newPath);

    /**
     *  删除一条记录（只删记录，不碰文件）。
     *  @param id 记录 id
     *  @return 删掉了一条返回 true；库未打开或 id 不存在返回 false
     */
    bool Remove(int64_t id);

private:
    CRecordLocalStore();
    ~CRecordLocalStore();
    CRecordLocalStore(const CRecordLocalStore&) = delete;
    CRecordLocalStore& operator=(const CRecordLocalStore&) = delete;

    //在已持锁的前提下执行一条无结果集的 SQL。
    bool ExecLocked(const char* sql);

    sqlite3*           m_db;      //SQLite 连接句柄；NULL 表示未打开
    std::wstring       m_dbPath;  //当前已打开的库路径（账号切换时据此判断是否需重开）
    mutable std::mutex m_mutex;   //保护 m_db / m_dbPath 的全部访问
};

} // namespace balloonhistory
