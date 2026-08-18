/**
 *  文件本地落点登记库——SQLite 读写封装，FileLocalStore.h
 *
 *  记录"某个文件在本机的哪个位置"，使聊天窗翻历史时不必每次都重新下载：
 *    · 发送方上传成功后登记当初选中的源文件路径；
 *    · 接收方下载完成后登记落地路径。
 *  之后渲染文件气泡时按 (MD5, 文件名) 查回路径，文件还在就直接给「打开 / 打开所在
 *  文件夹 / 另存为」，不在才退回「下载」。
 *
 *  为什么以内容 MD5 而非 msgid 作键：同一个文件可能出现在多条消息里（自己发出去的
 *  那条、对方转发回来的那条、群里再发一次的那条），按内容登记一次即可处处命中；
 *  服务端的秒传本就是按 MD5 判定的，这里与之同口径。文件名一并入键，是因为同一内容
 *  可能被存成不同名字，各自的落点应分别记账。
 *
 *  单例：聊天窗渲染（读）与主面板收尾传输（写）都要用它，穿线传递不如取全局一份。
 *  库文件与历史消息库同目录、按账号隔离（<个人目录>\ChatLog\filelocal.db），登录后由
 *  主面板 Open，账号切换时改开新库。
 *
 *  数据库读写用例见 flamingoclient/Source/Tests/FileLocalStoreTests.cpp（用 :memory: 库）。
 *
 *  balloonwj@qq.com   2026-08-03
 */
#pragma once

#include <string>
#include <mutex>

//前置声明 SQLite 连接句柄类型，避免在本头里包含 sqlite3.h。
struct sqlite3;

namespace balloonhistory {

/**
 *  CFileLocalStore —— 文件本地落点登记库。
 *
 *  线程：读发生在 UI 线程（渲染气泡），写发生在 UI 线程（传输结果回投后），内部仍以
 *  std::mutex 自保护，为将来挪到 worker 线程留好接口。不可拷贝（持有连接句柄与互斥量）。
 */
class CFileLocalStore
{
public:
    /**
     *  取全局单例。首次调用时构造，进程退出时随静态对象析构（析构里会 Close）。
     */
    static CFileLocalStore& GetInstance();

    /**
     *  打开（不存在则创建）数据库文件并建表；幂等。
     *  已打开且路径相同时直接返回 true；路径不同（账号切换）时先关旧库再开新库。
     *  @param dbPathUtf16 数据库全路径（UTF-16，含文件名）；其父目录须已存在。
     *                     传 L":memory:" 可开内存库（单元测试用）
     *  @return 打开并建表成功返回 true
     */
    bool Open(const wchar_t* dbPathUtf16);

    //关闭数据库（若已打开）。析构时自动调用。
    void Close();

    //是否已成功打开（粗判，用于决定是否需要 Open；不加锁）。
    bool IsOpen() const;

    /**
     *  登记 / 更新一个文件的本地落点。同一 (md5, filename) 重复登记时覆盖为新路径
     *  ——文件被另存到别处、或重下到缓存目录后，登记应指向最新的那份。
     *  @param md5       文件内容MD5（小写十六进制32位；空串直接返回 false）
     *  @param filename  原始文件名（UTF-8，含扩展名；空串直接返回 false）
     *  @param localPath 本地完整路径（UTF-16；空串直接返回 false）
     *  @return 写入成功返回 true；库未打开或参数非法返回 false
     */
    bool Upsert(const std::string& md5, const std::string& filename,
                const std::wstring& localPath);

    /**
     *  查某文件登记的本地落点。<u>只查登记、不判断文件是否还在</u>——存在性由调用方
     *  自行查磁盘（见 balloonchatwnd::ResolveLocalFilePath 的用法）。
     *  @param md5      文件内容MD5（小写十六进制32位）
     *  @param filename 原始文件名（UTF-8）
     *  @param outPath  [出参] 登记的本地路径；未命中时置空
     *  @return 命中返回 true；未命中 / 库未打开 / 参数非法返回 false
     */
    bool Query(const std::string& md5, const std::string& filename,
               std::wstring& outPath);

    /**
     *  删除一条登记（文件确认已不在、且不希望再回退到它时用）。当前实现里不主动调用
     *  ——登记留着无害，下次查到文件不存在自会回退；本方法供将来清理逻辑与测试使用。
     *  @param md5      文件内容MD5
     *  @param filename 原始文件名（UTF-8）
     *  @return 执行成功返回 true（无匹配行也算成功）
     */
    bool Remove(const std::string& md5, const std::string& filename);

private:
    CFileLocalStore();
    ~CFileLocalStore();
    CFileLocalStore(const CFileLocalStore&) = delete;
    CFileLocalStore& operator=(const CFileLocalStore&) = delete;

    //在已持锁的前提下执行一条无结果集的 SQL。
    bool ExecLocked(const char* sql);

    sqlite3*           m_db;      //SQLite 连接句柄；NULL 表示未打开
    std::wstring       m_dbPath;  //当前已打开的库路径（账号切换时据此判断是否需重开）
    mutable std::mutex m_mutex;   //保护 m_db / m_dbPath 的全部访问
};

} // namespace balloonhistory
