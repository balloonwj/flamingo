/**
 *  文件夹本地落点登记库——SQLite 读写封装，FolderLocalStore.h
 *
 *  记录"某一次文件夹传输在本机的根目录是哪个"，使传输结束之后（以及日后翻聊天记录、
 *  翻消息记录窗时）「打开文件夹」还能真的打开那个目录：
 *    · 发送方在服务端分配会话 id 之后登记当初选中的<u>源目录</u>；
 *    · 接收方在传输成功收尾时登记实际的<u>落地目录</u>（含重名自动改名之后的名字）。
 *
 *  为什么单独一张表、而不是并进文件那张 t_file_local：文件按 (内容 MD5, 文件名) 记账，
 *  而文件夹传输在服务端<u>不留任何内容</u>（不记文件清单、不记 MD5），能唯一标识一次
 *  传输的只有会话 id。两者的键根本不是一回事，硬并成一张表只会让两边的查询都别扭。
 *
 *  为什么以会话 id 而不是 msgid 作键：会话 id 在传输一开始就有了，而服务端的传输记录条
 *  （msgtype=18）要等传输走到终态才生成；用 msgid 作键的话，登记时根本还没有键可用。
 *  另外<u>同一次传输可能留下多条记录</u>（每次尝试各一条，失败一条、重试成功再一条），
 *  它们的本地根目录是同一个，按会话 id 记一次即可处处命中。
 *
 *  单例：聊天窗（写：绑定会话、传输收尾；读：渲染记录气泡）与消息记录窗（读）都要用它，
 *  穿线传递不如取全局一份。库文件与历史消息库同目录、按账号隔离
 *  （<个人目录>\ChatLog\folderlocal.db），登录后由主面板 Open，账号切换时改开新库。
 *
 *  <u>换一台机器只会显示记录、没有「打开文件夹」</u>：本表是纯本地的，新机器上查不到
 *  落点，界面据此不给按钮。这是预期行为，不是缺陷。
 *
 *  数据库读写用例见 flamingoclient/Source/Tests/FolderLocalStoreTests.cpp（Suite FLR，
 *  用 :memory: 库）。
 *
 *  balloonwj@qq.com   2026-08-14
 */
#pragma once

#include <string>
#include <mutex>

//前置声明 SQLite 连接句柄类型，避免在本头里包含 sqlite3.h。
struct sqlite3;

namespace balloonhistory {

/**
 *  CFolderLocalStore —— 文件夹本地落点登记库。
 *
 *  线程：读写目前都发生在 UI 线程（聊天窗收到传输事件、消息记录窗渲染行），内部仍以
 *  std::mutex 自保护，为将来挪到 worker 线程留好接口。不可拷贝（持有连接句柄与互斥量）。
 */
class CFolderLocalStore
{
public:
    /**
     *  取全局单例。首次调用时构造，进程退出时随静态对象析构（析构里会 Close）。
     */
    static CFolderLocalStore& GetInstance();

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
     *  登记 / 更新一次文件夹传输的本地根目录。同一会话 id 重复登记时覆盖为新路径 ——
     *  接收端在应答时只知道父目录、收尾时才知道自动改名后的最终目录，后者应当胜出。
     *  @param sessionId 本次传输的会话 id（UTF-8；空串直接返回 false）
     *  @param localRoot 本地根目录完整路径（UTF-16；空串直接返回 false）。发送端是源
     *                   目录、接收端是落地目录，两端各记各的
     *  @return 写入成功返回 true；库未打开或参数非法返回 false
     */
    bool Upsert(const std::string& sessionId, const std::wstring& localRoot);

    /**
     *  查某次传输登记的本地根目录。<u>只查登记、不判断目录是否还在</u>——存在性由调用方
     *  自行查磁盘（用户完全可能事后把目录改名或删掉）。
     *  @param sessionId 本次传输的会话 id（UTF-8）
     *  @param outPath   [出参] 登记的本地根目录；未命中时置空
     *  @return 命中返回 true；未命中 / 库未打开 / 参数非法返回 false
     */
    bool Query(const std::string& sessionId, std::wstring& outPath);

    /**
     *  删除一条登记（目录确认已不在、且不希望再指向它时用）。当前实现里不主动调用 ——
     *  登记留着无害，下次查到目录不存在自会不给按钮；本方法供将来清理逻辑与测试使用。
     *  @param sessionId 本次传输的会话 id（UTF-8）
     *  @return 执行成功返回 true（无匹配行也算成功）
     */
    bool Remove(const std::string& sessionId);

private:
    CFolderLocalStore();
    ~CFolderLocalStore();
    CFolderLocalStore(const CFolderLocalStore&) = delete;
    CFolderLocalStore& operator=(const CFolderLocalStore&) = delete;

    //在已持锁的前提下执行一条无结果集的 SQL。
    bool ExecLocked(const char* sql);

    sqlite3*           m_db;      //SQLite 连接句柄；NULL 表示未打开
    std::wstring       m_dbPath;  //当前已打开的库路径（账号切换时据此判断是否需重开）
    mutable std::mutex m_mutex;   //保护 m_db / m_dbPath 的全部访问
};

} // namespace balloonhistory
