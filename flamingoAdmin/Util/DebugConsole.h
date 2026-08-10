/**
 *  flamingoAdmin —— 调试控制台数据层（请求/响应往返记录）。
 *
 *  admin 控制台所有接口都经 HttpWorker 这一个咽喉点收发。本文件提供进程级
 *  单例 DebugConsole：把每一次「请求—响应」往返记录成一条 DebugEntry，存入
 *  一个有上限的环形缓冲，供调试控制台窗口实时展示。
 *
 *  分层与线程模型：
 *    · HttpWorker（工作线程）—— 取出请求后调 BeginEntry()，HTTP 往返结束后
 *      调 EndEntry()。两者均在【HTTP 工作线程】执行。
 *    · DebugConsoleWindow（UI 线程）—— 调 Snapshot() 读快照刷新列表。
 *  因此 DebugConsole 的全部公有方法都【加锁】，可在任意线程安全调用。
 *
 *  本文件【不依赖】balloonui、不依赖 HttpWorker —— BeginEntry 只收基本类型
 *  参数，故 DebugConsole.cpp 可被独立的单元测试工程直接编入。
 *
 *  敏感信息：BeginEntry / EndEntry 内部会对请求体 / 响应体里的 password、
 *  oldpassword、newpassword、token 等字段做掩码后再存储 —— 缓冲区里【不会】
 *  留存任何明文密码或完整令牌。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

#include <atlstr.h>
#include <string>
#include <deque>
#include <vector>
#include <mutex>
#include <climits>

namespace flamingoadmin {

// 环形缓冲的记录条数上限。超出后每新增一条即丢弃最旧一条。
// 取 1000：admin 控制台一次会话的接口调用量远低于此，足以回看全过程，
// 同时单条记录含两段 JSON 文本，1000 条的内存占用仍在数 MB 量级，可接受。
const size_t kDebugMaxEntries = 1000;

// 业务 code 的哨兵值。响应体不是合法 JSON、或 JSON 里没有顶层整数字段
// "code" 时，DebugEntry::m_businessCode 取此值（INT_MIN 不会与任何真实的
// 服务端 error_code 相等）。
const int kDebugNoBusinessCode = INT_MIN;

// 一条往返记录的进度状态。
enum class DebugEntryState
{
    Pending = 0,   // 请求已发出，响应尚未回来（列表里显示为「进行中」）
    Done    = 1,   // 响应已回来（无论传输成败、业务成败均为此值）
};

// 一条往返记录的结果类别。决定调试控制台列表里状态圆点的颜色。
enum class DebugEntryKind
{
    Pending       = 0,   // 进行中：m_state == Pending 时恒为此（灰点）
    Success       = 1,   // 业务成功：传输成功且业务 code == 0（绿点）
    BusinessFail  = 2,   // 业务失败：传输成功但业务 code != 0 或缺失（橙点）
    TransportFail = 3,   // 传输失败：连接 / 收发出错，未拿到应答（红点）
};

// 一次完整的「请求—响应」往返记录。
// 由 BeginEntry 创建（仅请求侧字段有效、m_state=Pending），EndEntry 回填
// 响应侧字段并置 m_state=Done。
struct DebugEntry
{
    unsigned long long m_seq;          // 自增序号，从 1 起；BeginEntry 分配
    DebugEntryState    m_state;        // 进度状态：进行中 / 已完成
    CString            m_reqTime;      // 请求发起的本地时刻，格式 "HH:MM:SS.mmm"
    CString            m_url;          // 完整请求 URL：scheme://host:port/path
    CString            m_apiName;      // 请求路径末段，列表「接口」列用，如 "enterprise/list.do"
    std::string        m_reqBodyUtf8;  // 请求体 JSON（UTF-8 字节，已掩码、已美化缩进）
    unsigned           m_elapsedMs;    // 往返耗时（毫秒）；m_state==Pending 时为 0
    bool               m_transportOk;  // 传输是否成功完成一次往返（仅 Done 时有效）
    int                m_httpStatus;   // HTTP 状态码（仅 m_transportOk==true 时有效）
    CString            m_errorText;    // 传输失败的中文原因（仅 m_transportOk==false 时有效）
    std::string        m_respBodyUtf8; // 响应体 JSON（UTF-8 字节，已掩码、已美化缩进）
    int                m_businessCode; // 响应体顶层 "code" 值；缺失取 kDebugNoBusinessCode

    DebugEntry();

    // 据 m_state / m_transportOk / m_businessCode 推导本条记录的结果类别。
    // 返回：见 DebugEntryKind。供列表绘制状态圆点、「仅看错误」筛选使用。
    DebugEntryKind Kind() const;
};

// 调试控制台数据层：进程级单例，持有所有往返记录的环形缓冲。
class DebugConsole
{
public:
    // 取全局唯一实例。
    static DebugConsole& Instance();

    // 记录一条请求，标志一次往返开始。线程安全，可在任意线程调用。
    //   host：服务器主机名 / IP。
    //   port：服务器端口（取值 1 - 65535）。
    //   useTls：true 表示 https，false 表示明文 http；仅用于拼 URL 的 scheme。
    //   path：请求路径，以 '/' 开头，如 L"/admin/enterprise/list.do"。
    //   bodyUtf8：原始请求体 JSON（UTF-8 字节）。函数内部完成敏感字段掩码
    //             与美化缩进后存入记录，不留存原始明文。
    //   返回：本条记录分配到的自增序号（从 1 起），供随后 EndEntry 配对；
    //         若当前处于暂停态，则不入缓冲并返回 0。
    unsigned long long BeginEntry(const CString& host, int port, bool useTls,
                                  const CString& path,
                                  const std::string& bodyUtf8);

    // 回填一条请求的响应，标志一次往返结束。线程安全。
    //   seq：BeginEntry 返回的序号；为 0 或找不到对应记录时本调用为空操作。
    //   transportOk：是否成功完成一次 HTTP 往返（见 HttpResult::m_transportOk）。
    //   httpStatus：HTTP 状态码（transportOk 为 true 时有效）。
    //   respBodyUtf8：响应体 JSON（UTF-8 字节）。函数内部完成掩码与美化后存储。
    //   errorText：传输失败的中文原因（transportOk 为 false 时有效）。
    //   elapsedMs：本次往返耗时（毫秒），由调用方测量后传入。
    void EndEntry(unsigned long long seq, bool transportOk, int httpStatus,
                  const std::string& respBodyUtf8, const CString& errorText,
                  unsigned elapsedMs);

    // 暂停 / 恢复录制。暂停期间 BeginEntry 直接返回 0、不入缓冲，EndEntry
    // 对暂停期间产生的 seq（0）也不做任何事；已在缓冲中的记录不受影响。
    //   paused：true 暂停，false 恢复。
    void SetPaused(bool paused);

    // 当前是否处于暂停态。
    bool IsPaused() const;

    // 清空全部记录。序号计数【不】重置（继续自增），便于区分清空前后。
    void Clear();

    // 取当前缓冲内的记录条数。
    size_t Count() const;

    // 取全部记录的一份快照（加锁拷贝）。供 UI 线程读取以刷新列表，
    // 拷贝出来后即与内部缓冲解耦，遍历期间不会被工作线程改动。
    std::vector<DebugEntry> Snapshot() const;

    // 设置 UI 通知接收窗口。此后每次 BeginEntry / EndEntry / Clear 改动缓冲
    // 后，都会向该窗口 PostMessage(msg, 0, 0)，由窗口据此刷新界面。
    //   hwnd：接收通知的窗口句柄；nullptr 表示取消通知（窗口销毁前须调用）。
    //   msg：通知消息码（窗口自定义，建议用 WM_APP 之上的值）。
    void SetSink(HWND hwnd, UINT msg);

    // 把当前全部记录格式化成多行可读文本（每条含请求与响应），供「导出」用。
    //   返回：UTF-16 文本，可直接写入文件或放入剪贴板。
    CString FormatAllForExport() const;

private:
    DebugConsole();
    ~DebugConsole();
    DebugConsole(const DebugConsole&) = delete;
    DebugConsole& operator=(const DebugConsole&) = delete;

    // 向已登记的 sink 窗口发一条刷新通知。调用前须已持有 m_mutex；
    // 实际的 PostMessage 在锁外不强制，但通知本身是廉价的异步投递。
    void NotifySink();

    mutable std::mutex     m_mutex;     // 保护以下所有成员，使全部公有方法线程安全
    std::deque<DebugEntry> m_entries;   // 往返记录环形缓冲（FIFO，上限 kDebugMaxEntries）
    unsigned long long     m_nextSeq;   // 下一条记录的序号，从 1 起单调自增
    bool                   m_paused;    // 是否暂停录制
    HWND                   m_sinkHwnd;  // UI 通知接收窗口，nullptr 表示不通知
    UINT                   m_sinkMsg;   // UI 通知消息码
};

} // namespace flamingoadmin
