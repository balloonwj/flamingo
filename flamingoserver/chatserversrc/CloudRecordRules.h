/**
 *  云端录像的数据结构与纯规则，CloudRecordRules.h
 *
 *  第 26 期（docs/屏幕共享与录制方案.md 12.6）：t_cloud_record / t_cloud_record_share 的行结构，以及不依赖网络与数据库的
 *  判定规则 —— 到期时间、配额、标题、文件名、谁能看与谁能下载、imgserver 回报录制结束后的状态。会话层（ChatSession）、
 *  存储层（CloudRecordStore）与协议层（CloudRecordProtocol）都用这里的定义。单元测试见 tests/test_cloudrecordserver.cpp（CRR 系列）。
 *
 *  典型用法：
 *      if (!CloudRecordRules::quotaAllows(usedBytes, quotaGb, 0))
 *          回 error_code_storage_quota;
 *      row.m_expireTime = CloudRecordRules::expireTimeFor(endMs, retainDays);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <set>
#include <string>
#include <vector>

//一条云端录像（t_cloud_record 的一行）
struct CloudRecordRow
{
    int64_t     m_id;            //录像 id
    int64_t     m_enterpriseId;  //开启者所属企业 id（配额按企业计）
    int64_t     m_owner;         //开启者 userid
    int         m_source;        //来源，取值见 cloudrecord_source
    std::string m_roomId;        //屏幕共享房间号（来源为共享时）
    int         m_ctype;         //共享所在会话类型（1 单聊 2 群聊）；来源为上传时为 0
    int64_t     m_cid;           //共享所在会话 id：单聊为对方 userid（以开启者为准），群聊为群 id
    std::string m_title;         //标题（UTF-8）
    int64_t     m_startTime;     //开始录制时间，Unix 毫秒
    int64_t     m_endTime;       //结束录制时间，Unix 毫秒；录制中为 0
    int64_t     m_durationMs;    //时长，毫秒（不含暂停）
    int64_t     m_frecBytes;     //原始帧文件字节数；已删除或来源为上传时为 0
    int64_t     m_bytes;         //最终文件字节数；尚未上传时为 0
    bool        m_audioOnly;     //是否只有声音（m4a）
    int         m_status;        //状态，取值见 cloudrecord_status
    std::string m_frecFile;      //原始帧文件名；已删除或没有时为空
    std::string m_file;          //最终文件名；尚未上传时为空
    int64_t     m_expireTime;    //到期时间，Unix 毫秒；0 为永久保留
    bool        m_warned;        //是否已推送到期提醒
    int64_t     m_createTime;    //创建时间，Unix 毫秒

    CloudRecordRow();
};

//一条分享记录（t_cloud_record_share 的一行）
struct CloudRecordShareRow
{
    int64_t m_recordId;   //录像 id
    int     m_ctype;      //会话类型（1 单聊 2 群聊）
    int64_t m_cid;        //会话 id：单聊为接收方 userid，群聊为群 id
    int64_t m_sender;     //发送人 userid
    int64_t m_sendTime;   //最近一次发送时间，Unix 毫秒

    CloudRecordShareRow();
};

//下载令牌可以取的文件种类（1756 取下载令牌时的 kind 字段）
enum cloudrecord_file_kind
{
    cloudrecord_file_frec  = 1,   //原始帧文件（只有开启者能取，合成用）
    cloudrecord_file_final = 2,   //最终文件（MP4 或 m4a；开启者与录像发到过的会话的成员能取）
};

//云端录像的纯规则；全部为静态方法
class CloudRecordRules
{
public:
    //标题最多几个字（按 Unicode 字符计）
    static const size_t kTitleMaxChars = 50;
    //到期前多久推送提醒（毫秒）：3 天（第 52 条）
    static const int64_t kExpireWarnBeforeMs = 3LL * 24 * 60 * 60 * 1000;
    //上传、下载令牌的有效期（毫秒）：24 小时，长录像的下载与续传都够用
    static const int64_t kTokenTtlMs = 24LL * 60 * 60 * 1000;
    //1755 列表每页条数的默认值与上限
    static const int kListPageDefault = 20;
    static const int kListPageMax = 50;

    /**
     *  到期时间。
     *  @param baseMs     起算时刻（录制结束或上传完成），Unix 毫秒
     *  @param retainDays 保留天数（cloudrecord.retaindays）；0 表示永久保留
     *  @return Unix 毫秒；永久保留或参数不合法（起算时刻不大于 0、天数小于 0）时为 0
     */
    static int64_t expireTimeFor(int64_t baseMs, int64_t retainDays);

    /**
     *  配额是否还够。
     *  @param usedBytes  企业已用字节数（原始帧文件与最终文件之和）
     *  @param quotaGb    企业配额（cloudrecord.quotagb），GB；0 表示不允许再新增
     *  @param extraBytes 这次要新增的字节数（开始录制时为 0，上传时为文件大小）
     *  @return 已用 + 新增不超过配额返回 true
     */
    static bool quotaAllows(int64_t usedBytes, int64_t quotaGb, int64_t extraBytes);

    //配额 GB 换算成字节；小于 0 时为 0
    static int64_t quotaBytes(int64_t quotaGb);

    /**
     *  是否该推送到期提醒：有到期时间、还没提醒过、离到期不足 kExpireWarnBeforeMs、还没到期。
     *  @param expireTime 到期时间，Unix 毫秒；0 为永久保留
     *  @param warned     是否已提醒过
     *  @param nowMs      当前时间，Unix 毫秒
     */
    static bool shouldWarnExpire(int64_t expireTime, bool warned, int64_t nowMs);

    //是否已到期（有到期时间且不晚于当前时间）
    static bool isExpired(int64_t expireTime, int64_t nowMs);

    /**
     *  标题是否合法：去掉首尾空白后 1～kTitleMaxChars 个字符，且是合法的 UTF-8。
     *  @param title  标题（UTF-8）
     *  @param trimmed [出参] 去掉首尾空白后的标题
     */
    static bool normalizeTitle(const std::string& title, std::string& trimmed);

    /**
     *  默认标题：共享标题不为空时用它，否则用「云端录像」；超长时截断到 kTitleMaxChars 个字符。
     *  @param shareTitle 屏幕共享的标题（UTF-8）
     */
    static std::string defaultTitle(const std::string& shareTitle);

    /**
     *  某人能不能看这条录像（取最终文件、在列表里看到它）：开启者本人；或录像发到过的单聊的接收方；
     *  或录像发到过的群的成员。
     *  @param userId       要看的人
     *  @param ownerId      开启者
     *  @param shares       这条录像的分享记录
     *  @param memberGroups 分享记录里出现的群中，userId 是成员的那些群 id（调用方查好）
     */
    static bool canView(int64_t userId, int64_t ownerId, const std::vector<CloudRecordShareRow>& shares,
                        const std::set<int64_t>& memberGroups);

    /**
     *  能不能取某种文件的下载令牌。
     *  @param kind    文件种类，取值见 cloudrecord_file_kind
     *  @param isOwner 是否开启者
     *  @param canView 是否能看（见 canView；开启者恒为 true）
     *  @param row     录像
     *  @return 原始帧文件：开启者、来源为共享、文件还在；最终文件：能看、状态为可观看、文件已上传
     */
    static bool canDownload(int kind, bool isOwner, bool canView, const CloudRecordRow& row);

    /**
     *  imgserver 回报一次录制结束后的状态：录到了帧为等待合成，一帧都没有为出错。
     *  @param frames 写进文件的帧数
     */
    static int statusAfterFinished(uint64_t frames);

    //发到会话的前提：状态为可观看
    static bool canSend(const CloudRecordRow& row);
};
