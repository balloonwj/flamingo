/**
 *  屏幕共享的分享链接码表，ScreenShareLinkTable.h
 *
 *  第 25 期（docs/屏幕共享与录制方案.md 第 12.4 节，第 44～46 条）：主持人可以为进行中的共享生成一条分享链接
 *  「flamingo://screenshare/<链接码>」，发到任意会话；同企业、已登录的人在 Flamingo 聊天里点它即可加入。本类管理：
 *    - 链接码：10 位，取自去掉易混字符（0 O o 1 l I）后的 56 个字母数字，约 58 位随机量，不能从房间号推算；
 *    - 房间与链接码一一对应：每个房间同时至多一条有效链接；重置时作废旧码、生成新码，停用时作废且不再生成；
 *    - 「点链接加入免输密码」开关随链接保存（第 46 条），重置链接时保留；
 *    - 共享结束后链接码再保留 kEndedKeepMs，这段时间里解析结果为「已结束」而不是「无效」；
 *    - 解析频率限制：每个用户每 kResolveWindowMs 最多解析 kResolveLimitPerWindow 次，防止枚举链接码。
 *  链接码与房间一样只存在内存里，chatserver 重启后旧链接一律无效。
 *
 *  本类为纯逻辑（不触网、不查库），全部公开方法线程安全（内部一把互斥量），便于单元测试，见
 *  tests/test_screensharelink.cpp（Suite SSLK）。全局实例经 Singleton<ScreenShareLinkTable> 访问；测试里可直接构造独立实例，
 *  并用 setRandomFn 换成可预测的随机数。
 *
 *  典型用法（ScreenShareService 里）：
 *      ScreenShareLinkTable& links = Singleton<ScreenShareLinkTable>::Instance();
 *      std::string code;
 *      bool noPassword = false;
 *      links.getOrCreate(roomId, code, noPassword);
 *      ...
 *      std::string roomId;
 *      int status = links.resolve(code, roomId, noPassword);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <map>
#include <mutex>
#include <string>

//链接码的解析结果
enum screenshare_link_status
{
    screenshare_link_invalid = 0,   //没有这个链接码：从未生成、已被重置或停用、共享结束已超过保留期、chatserver 重启过
    screenshare_link_active  = 1,   //链接有效，对应的共享正在进行
    screenshare_link_ended   = 2,   //对应的共享已经结束（结束后 kEndedKeepMs 以内）
};

//分享链接码表；全局单例经 Singleton<ScreenShareLinkTable> 访问
class ScreenShareLinkTable final
{
public:
    //链接码的长度（字符）
    static const size_t kCodeLength = 10;
    //共享结束后链接码再保留的时长（毫秒）：24 小时
    static const int64_t kEndedKeepMs = 24LL * 60 * 60 * 1000;
    //解析频率限制的统计窗口（毫秒）与窗口内的次数上限（第 12.4.2 节：每分钟 30 次）
    static const int64_t kResolveWindowMs = 60 * 1000;
    static const int kResolveLimitPerWindow = 30;
    //生成链接码时遇到重复码的最多重试次数（56^10 的空间里实际不会用到，只是避免死循环）
    static const int kMaxGenerateTries = 16;

    /**
     *  随机数函数：返回 [0, upperBound) 内均匀分布的整数。产品里用 libsodium 的 randombytes_uniform，测试里可换成可预测的实现。
     *  @param upperBound 上界（不含），大于 0
     */
    typedef uint32_t (*RandomFn)(uint32_t upperBound);

    ScreenShareLinkTable();
    ~ScreenShareLinkTable();

    ScreenShareLinkTable(const ScreenShareLinkTable& rhs) = delete;
    ScreenShareLinkTable& operator=(const ScreenShareLinkTable& rhs) = delete;

    /**
     *  替换随机数函数（测试用）。
     *  @param fn 新的随机数函数；传 NULL 恢复为 libsodium 的 randombytes_uniform
     */
    void setRandomFn(RandomFn fn);

    //链接码使用的字符表（56 个字符，不含 0 O o 1 l I）
    static const char* alphabet();

    /**
     *  链接码的格式是否合法：正好 kCodeLength 个字符，且每个都在 alphabet() 里。
     *  @param code 待检查的链接码（来自客户端，按不可信输入处理）
     */
    static bool isValidCode(const std::string& code);

    /**
     *  取房间当前的有效链接；没有时生成一条（主持人「复制分享链接」「发送邀请」时调用）。
     *  @param roomId        房间号（调用方已确认房间存在、请求者是主持人）
     *  @param outCode       [出参] 链接码
     *  @param outNoPassword [出参] 是否打开了「点链接加入免输密码」；新生成的链接为 false
     *  @return 返回已有的链接时为 false，新生成时为 true（调用方据此记日志）
     */
    bool getOrCreate(const std::string& roomId, std::string& outCode, bool& outNoPassword);

    /**
     *  取房间当前的有效链接，没有时不生成。
     *  @param roomId        房间号
     *  @param outCode       [出参] 链接码；没有时不改
     *  @param outNoPassword [出参] 是否免输密码；没有时不改
     *  @return 有有效链接返回 true
     */
    bool get(const std::string& roomId, std::string& outCode, bool& outNoPassword) const;

    /**
     *  重置链接：作废旧码（之后解析为 invalid），生成新码；「免输密码」开关沿用旧链接的设置。房间原来没有链接时等同 getOrCreate。
     *  @param roomId        房间号
     *  @param outCode       [出参] 新链接码
     *  @param outNoPassword [出参] 是否免输密码
     */
    void reset(const std::string& roomId, std::string& outCode, bool& outNoPassword);

    /**
     *  停用链接：作废房间当前的链接码（之后解析为 invalid），不再生成，直到主持人再次复制或发送。没有链接时什么也不做。
     *  @param roomId 房间号
     *  @return 确实作废了一条链接返回 true
     */
    bool disable(const std::string& roomId);

    /**
     *  设置「点链接加入免输密码」（第 46 条）。
     *  @param roomId 房间号
     *  @param on     是否免输密码
     *  @return 房间有有效链接时返回 true；没有链接时返回 false（不生成）
     */
    bool setNoPassword(const std::string& roomId, bool on);

    /**
     *  解析链接码。
     *  @param code          链接码（格式不合法时直接返回 invalid）
     *  @param outRoomId     [出参] 对应的房间号（active 与 ended 时回填）
     *  @param outNoPassword [出参] 是否免输密码（active 时回填）
     *  @return 取值见 screenshare_link_status
     */
    int resolve(const std::string& code, std::string& outRoomId, bool& outNoPassword) const;

    /**
     *  共享结束：把房间的链接标记为「已结束」，保留 kEndedKeepMs 后由 reap 清除。房间没有链接时什么也不做。
     *  @param roomId 房间号
     *  @param now    当前时间，Unix 毫秒
     */
    void onRoomEnded(const std::string& roomId, int64_t now);

    /**
     *  清除已结束超过 kEndedKeepMs 的链接码，以及统计窗口已过期的频率计数（主循环每秒扫描时顺带调用）。
     *  @param now 当前时间，Unix 毫秒
     */
    void reap(int64_t now);

    /**
     *  解析频率限制：记一次解析并判断是否超限。窗口从该用户本窗口内第一次解析起算，满 kResolveWindowMs 后重新计数。
     *  @param userId 解析者 userid
     *  @param now    当前时间，Unix 毫秒
     *  @return 未超限返回 true（本次计入）；超限返回 false（调用方回 error_code_screenshare_link_too_frequent）
     */
    bool allowResolve(int64_t userId, int64_t now);

    //当前保存的链接码条数（有效与已结束的合计，测试与日志用）
    size_t size() const;

private:
    //一条链接
    struct Link
    {
        std::string m_roomId;       //房间号
        bool        m_noPassword;   //是否免输密码
        bool        m_ended;        //对应的共享是否已结束
        int64_t     m_endedAt;      //结束时刻，Unix 毫秒；m_ended 为 false 时为 0

        Link();
    };

    //某个用户的解析频率计数
    struct ResolveCounter
    {
        int64_t m_windowStart;   //本统计窗口的起点，Unix 毫秒
        int     m_count;         //本窗口内已解析的次数

        ResolveCounter();
    };

    //生成一个当前没有被占用的链接码（调用方已持锁）
    std::string generateLocked();
    /**
     *  作废房间当前的有效链接（调用方已持锁）：从两张表里都移除，之后解析为 invalid。
     *  @param roomId        房间号
     *  @param outNoPassword [出参] 作废前是否免输密码；没有链接时不改
     *  @return 确实有一条有效链接被作废时返回 true
     */
    bool dropActiveLocked(const std::string& roomId, bool& outNoPassword);

    mutable std::mutex                    m_mutex;      //保护以下全部成员
    RandomFn                              m_random;     //随机数函数
    std::map<std::string, Link>           m_links;      //链接码 → 链接（含已结束、尚在保留期内的）
    std::map<std::string, std::string>    m_byRoom;     //房间号 → 当前有效的链接码（共享结束、重置、停用时移除）
    std::map<int64_t, ResolveCounter>     m_counters;   //userid → 解析频率计数
};
