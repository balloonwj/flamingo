/**
 *  云端录像的客户端协议层（1755～1757 的 JSON 组装与解析），CloudRecordProtocolClient.h
 *
 *  第 27 期（docs/屏幕共享与录制方案.md 12.6，字段见 docs/协议设计-v2.md 17.10）。纯逻辑，不碰网络与界面，
 *  单元测试见 Source/Tests/CloudRecordClientTests.cpp（标签 [cloudrec]）。共享中开始 / 停止云端录制的 1754 在
 *  Share/ShareProtocolClient 里（它与房间状态在一起）。
 *
 *  典型用法：
 *      host->SendSignal(cmd_cloudrecord_op, ...BuildCloudRecordDownloadToken(42, cloudrecord_file_frec)...);
 *      CloudRecordOpResult r;
 *      if (ParseCloudRecordOpResult(json, r) && r.m_code == 0) 用 r.m_token 下载;
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace screenrecord {

//取下载令牌的文件种类（1756 的 kind 字段，与服务端 cloudrecord_file_kind 一致）
enum CloudRecordFileKind
{
    kCloudFileFrec  = 1,   //原始帧文件（只有开启者能取，合成用）
    kCloudFileFinal = 2,   //最终文件（MP4 或 m4a）
};

//一条云端录像（服务端 recordToJson 的字段）
struct CloudRecordInfo
{
    int64_t     m_recordId;    //录像 id
    int64_t     m_owner;       //开启者 userid
    int         m_source;      //来源，取值见 Protocol.h 的 cloudrecord_source
    std::string m_roomId;      //屏幕共享房间号（来源为共享时）
    int         m_ctype;       //共享所在会话类型；来源为上传时为 0
    int64_t     m_cid;         //共享所在会话 id
    std::string m_title;       //标题（UTF-8）
    int64_t     m_startTime;   //开始时间，Unix 毫秒
    int64_t     m_endTime;     //结束时间，Unix 毫秒；录制中为 0
    int64_t     m_durationMs;  //时长，毫秒
    int64_t     m_bytes;       //最终文件字节数
    int64_t     m_frecBytes;   //原始帧文件字节数
    bool        m_audioOnly;   //是否只有声音
    int         m_status;      //状态，取值见 Protocol.h 的 cloudrecord_status
    int64_t     m_expireTime;  //到期时间，Unix 毫秒；0 为永久
    int64_t     m_createTime;  //创建时间，Unix 毫秒

    CloudRecordInfo();
};

//1755 的应答
struct CloudRecordListResult
{
    int                          m_code;        //错误码，0 为成功
    std::vector<CloudRecordInfo> m_records;     //本页录像，按 id 从大到小
    bool                         m_hasMore;     //还有更早的录像
    bool                         m_enabled;     //本企业是否开通了云端录制
    int64_t                      m_usedBytes;   //本企业已用字节数
    int64_t                      m_quotaBytes;  //本企业配额字节数
    int64_t                      m_retainDays;  //保留天数，0 为永久

    CloudRecordListResult();
};

//1756 的应答
struct CloudRecordOpResult
{
    int             m_code;       //错误码，0 为成功
    int             m_op;         //操作，取值见 Protocol.h 的 cloudrecord_op_type
    int64_t         m_recordId;   //录像 id（新建上传录像时为新 id）
    bool            m_hasRecord;  //是否带了整条录像
    CloudRecordInfo m_record;     //整条录像（m_hasRecord 为 true 时有效）
    std::string     m_token;      //上传或下载令牌；没有时为空
    std::string     m_host;       //imgserver 地址
    int             m_port;       //imgserver 图片端口
    std::string     m_fileName;   //文件名，如 rec_42.mp4
    int64_t         m_bytes;      //文件字节数

    CloudRecordOpResult();
};

//1757 推送
struct CloudRecordNotify
{
    int             m_type;       //类型，取值见 Protocol.h 的 cloudrecord_notify_type
    int64_t         m_recordId;   //录像 id
    bool            m_hasRecord;  //是否带了整条录像（类型为删除时不带）
    CloudRecordInfo m_record;     //整条录像

    CloudRecordNotify();
};

//1755 请求：beforeId 为 0 时从最新的开始，limit 为每页条数
std::string BuildCloudRecordList(int64_t beforeId, int limit);

//1756 请求：重命名（标题 UTF-8）、删除、取下载令牌、发到会话
std::string BuildCloudRecordRename(int64_t recordId, const std::string& titleUtf8);
std::string BuildCloudRecordDelete(int64_t recordId);
std::string BuildCloudRecordDownloadToken(int64_t recordId, int kind);
std::string BuildCloudRecordSend(int64_t recordId, int ctype, int64_t cid);

/**
 *  1756 请求：取上传令牌。
 *  @param recordId   已有录像（合成后上传、续传）；为 0 时新建一条来源为上传的录像
 *  @param bytes      文件字节数（>0）
 *  @param audioOnly  新建时：是否只有声音
 *  @param titleUtf8  新建时：标题
 *  @param durationMs 录像时长（毫秒）
 *  @param startTime  新建时：本机开始录制的时间（Unix 毫秒）
 */
std::string BuildCloudRecordUploadToken(int64_t recordId, int64_t bytes, bool audioOnly, const std::string& titleUtf8,
                                        int64_t durationMs, int64_t startTime);

//本机记下的一次没传完的「保存到云端」（第 27 期 27d-4）：上传途中退出程序、退出登录或断网时留在本机清单里，下次登录读到列表后续传
struct PendingCloudUpload
{
    int64_t     m_recordId;    //服务端分配的录像 id（来源为上传、状态为上传中）
    std::string m_pathUtf8;    //本机文件的完整路径（UTF-8）
    int64_t     m_bytes;       //申请上传时的文件字节数；续传时文件大小须与它相同
    bool        m_audioOnly;   //是否只有声音
    int64_t     m_durationMs;  //时长（毫秒）
    int64_t     m_startTime;   //开始录制的时间（Unix 毫秒）
    std::string m_title;       //标题（UTF-8）

    PendingCloudUpload();
};

/**
 *  把没传完的上传清单写成 JSON（本机文件 pending_<userid>.json 的内容）。
 *  @param list 清单；录像 id 不大于 0 的条目跳过
 */
std::string BuildPendingUploadsJson(const std::vector<PendingCloudUpload>& list);

/**
 *  解析没传完的上传清单。缺录像 id 或路径的条目跳过。
 *  @param json 文件内容
 *  @param out  [出参] 清单
 *  @return 是 JSON 对象且带 uploads 数组时返回 true（数组可以为空）
 */
bool ParsePendingUploadsJson(const std::string& json, std::vector<PendingCloudUpload>& out);

//DecidePendingResume 的结果
enum PendingResumeDecision
{
    pending_resume = 1,   //续传：服务端仍是上传中，本机文件还在且大小不变
    pending_drop   = 2,   //从清单里去掉：已经传完、出错、被删除，或本机文件没了 / 变了
    pending_keep   = 3,   //先留着：这一页列表里没有它、但还有更早的录像没读到
};

/**
 *  登录后读到列表时，对清单里的一条该怎么办。
 *  @param found        列表里是否有这条录像
 *  @param status       有的话它的服务端状态，取值见 Protocol.h 的 cloudrecord_status
 *  @param listComplete 列表是否已读全（没有更早的录像）
 *  @param fileOk       本机文件还在且大小与清单里的相同
 */
int DecidePendingResume(bool found, int status, bool listComplete, bool fileOk);

//解析 1755 / 1756 / 1757；不是 JSON 对象或缺必需字段时返回 false
bool ParseCloudRecordList(const std::string& json, CloudRecordListResult& out);
bool ParseCloudRecordOpResult(const std::string& json, CloudRecordOpResult& out);
bool ParseCloudRecordNotify(const std::string& json, CloudRecordNotify& out);

} // namespace screenrecord
