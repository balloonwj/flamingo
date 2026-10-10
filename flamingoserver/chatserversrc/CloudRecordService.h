/**
 *  云端录制的服务端处理流程，CloudRecordService.h
 *
 *  第 26 期（docs/屏幕共享与录制方案.md 12.6）。把存储（CloudRecordStore）、规则（CloudRecordRules）、协议（CloudRecordProtocol）、
 *  imgserver 控制连接（ICloudRecordCtlSender，线上为 CloudRecordLink）与外部环境（ICloudRecordEnv，线上为 CloudRecordServerEnv）
 *  串起来，完成：
 *    - 1754 共享中的云端录制：开始前的检查（开关、控制连接、配额）、建录像记录、通知 imgserver 开始 / 暂停 / 继续 / 停止。
 *      房间里的录制状态与 1725 广播由 ScreenShareService 负责，它在合适的时机调用本类；
 *    - imgserver 的回报：开始失败（删记录、经环境通知房间）、录制结束（写结束时间与大小，状态改为等待合成或出错，推 1757）、
 *      最终文件上传完（状态改为可观看，删原始帧文件，推 1757）；
 *    - 控制连接恢复：给已不在任何房间里录制、库里却还是「录制中」的录像补发 stop（chatserver 重启或停止命令丢失时）；
 *    - 1755 列表、1756 操作（重命名、删除、下载与上传令牌、发到会话）；
 *    - 每小时的到期提醒与到期清理。
 *  本类不直接碰网络与其它单例，全部经两个接口，因此可以用假环境做单元测试（tests/test_cloudrecordservice.cpp，CRV 系列）。
 *  线程：公开方法可在任意线程调用，内部用一把锁串行化（库操作与发控制消息都在锁内，量很小）。
 *
 *  典型用法（chatserver 的 main.cpp）：
 *      static CloudRecordStore store;
 *      static CloudRecordServerEnv env;
 *      static CloudRecordService service(store, env, Singleton<CloudRecordLink>::Instance(), filetokenkey);
 *      CloudRecordService::setInstance(&service);
 *
 *  balloonwj@qq.com   2026-10-06
 */
#pragma once

#include <stdint.h>
#include <mutex>
#include <string>
#include <vector>
#include "CloudRecordProtocol.h"
#include "CloudRecordRules.h"
#include "CloudRecordStore.h"

//发往 imgserver 的控制消息出口
class ICloudRecordCtlSender
{
public:
    virtual ~ICloudRecordCtlSender() {}

    //控制连接当前是否可用
    virtual bool isUp() const = 0;

    /**
     *  发一条控制消息（JSON，格式见 utils/CloudRecordCtlMsg.h）。
     *  @return 已发出返回 true；连接不可用返回 false
     */
    virtual bool send(const std::string& body) = 0;
};

//云端录制流程用到的外部环境（线上实现见 CloudRecordServerEnv）
class ICloudRecordEnv
{
public:
    virtual ~ICloudRecordEnv() {}

    //此人所属企业 id；不属于任何企业时为 0
    virtual int64_t enterpriseOf(int64_t userId) = 0;

    //企业是否开通了云端录制（功能开关 cloudrecord）
    virtual bool isEnabled(int64_t enterpriseId) = 0;

    //企业的云端录像配额（GB）与保留天数（0 为永久）
    virtual int64_t quotaGb(int64_t enterpriseId) = 0;
    virtual int64_t retainDays(int64_t enterpriseId) = 0;

    //此人是否是该群的成员
    virtual bool isGroupMember(int64_t groupId, int64_t userId) = 0;

    //推一帧给此人的全部在线连接
    virtual void push(int64_t userId, uint16_t cmd, const std::string& body) = 0;

    /**
     *  房间里的这次云端录制被动结束了（imgserver 开始失败、磁盘不足、写入出错、imgserver 退出）：清掉房间的录制状态并经
     *  1725 事件 15 广播。房间已不存在或已不在录这条录像时什么也不做。
     *  @param code 原因，作为 1725 的 cloudrecordcode 下发（0 表示正常停止）
     */
    virtual void onRoomRecordEnded(const std::string& roomId, int64_t recordId, int code) = 0;

    //房间是否还在录这条录像
    virtual bool isRoomRecording(const std::string& roomId, int64_t recordId) = 0;

    /**
     *  以 fromId 的名义往会话里发一张云端录像卡片（message_type_cloudrecord）：落库并推给会话成员。
     *  @return 消息 id；失败返回 0
     */
    virtual int64_t sendCard(int64_t fromId, int ctype, int64_t cid, const std::string& content, int64_t nowMs) = 0;

    //imgserver 图片端口的地址（客户端凭令牌去那里上传、下载）
    virtual void imgServerAddr(std::string& host, int& port) = 0;
};

//录像标题的敏感词检查（每次请求由 ChatSession 构造，它知道提交者的名称，命中时记拦截记录）
class ICloudRecordTitleChecker
{
public:
    virtual ~ICloudRecordTitleChecker() {}

    /**
     *  标题是否命中敏感词；命中时由实现方记一条拦截记录（来源 sensitive_source_cloudrecord）。
     *  @param title 标题（UTF-8，已去掉首尾空白）
     */
    virtual bool hitTitle(const std::string& title) = 0;
};

//屏幕共享房间里与建录像记录有关的信息
struct CloudRecordRoomInfo
{
    std::string m_roomId;      //房间号
    int         m_ctype;       //会话类型
    int64_t     m_cid;         //会话 id（以开启者为准：单聊为对方 userid，群聊为群 id）
    std::string m_title;       //共享标题（作为录像的默认标题）

    CloudRecordRoomInfo();
};

//云端录制的服务端处理流程
class CloudRecordService
{
public:
    //每次到期清理、到期提醒最多处理的条数（每小时一次，处理不完下一次接着来）
    static const int kMaintenanceBatch = 200;

    /**
     *  @param store        存储（不拥有，生命周期须长于本对象）
     *  @param env          外部环境（不拥有）
     *  @param sender       imgserver 控制消息出口（不拥有）
     *  @param fileTokenKey 文件令牌的 HMAC 密钥（filetokenkey），与 imgserver 一致
     */
    CloudRecordService(CloudRecordStore& store, ICloudRecordEnv& env, ICloudRecordCtlSender& sender,
                       const std::string& fileTokenKey);

    CloudRecordService(const CloudRecordService&) = delete;
    CloudRecordService& operator=(const CloudRecordService&) = delete;

    //线上实例（main.cpp 设置；没有设置时 ScreenShareService 对 1754 回「云端存储服务不可用」）
    static void setInstance(CloudRecordService* service);
    static CloudRecordService* instance();

    /**
     *  1754 开始前的检查（不改任何东西）。
     *  @return 0 可以开始；736 企业没开通；738 控制连接不可用；604 配额已满
     */
    int checkStart(int64_t userId);

    /**
     *  建一条来源为共享、状态为录制中的录像记录。
     *  @param recordId [出参] 新录像 id
     *  @return 0 成功；写库失败返回 error_code_server_internal
     */
    int createShareRecord(int64_t userId, const CloudRecordRoomInfo& room, int64_t nowMs, int64_t& recordId);

    //撤销刚建的录像记录（房间状态冲突或通知 imgserver 失败时）
    void discardRecord(int64_t recordId);

    //通知 imgserver 开始 / 暂停 / 继续 / 停止；返回控制消息是否已发出（stop 没发出时，控制连接恢复后由 onLinkUp 补发）
    bool sendStart(int64_t recordId, const std::string& roomId);
    bool sendPause(int64_t recordId);
    bool sendResume(int64_t recordId);
    bool sendStop(int64_t recordId);

    /**
     *  处理 imgserver 发来的一条控制消息（started / finished / uploaded / pong）。
     *  @param body  JSON
     *  @param nowMs 当前时间，Unix 毫秒
     */
    void onCtlMessage(const std::string& body, int64_t nowMs);

    //控制连接恢复：库里「录制中」、房间却已不在录的录像补发 stop
    void onLinkUp();

    /**
     *  1755：列出本人的云端录像。
     *  @return 应答包体
     */
    std::string handleList(int64_t userId, const std::string& body);

    /**
     *  1756：对一条云端录像操作。
     *  @param checker 标题的敏感词检查（重命名与新建上传时用）；为 NULL 时不检查（不拥有）
     *  @return 应答包体
     */
    std::string handleOp(int64_t userId, const std::string& body, int64_t nowMs, ICloudRecordTitleChecker* checker);

    /**
     *  每小时调用一次：推送即将到期的提醒，删除已到期的录像（库记录、分享记录与 imgserver 上的文件）。
     *  @param nowMs 当前时间，Unix 毫秒
     */
    void runMaintenance(int64_t nowMs);

    /**
     *  企业的云端录像已用空间（管理后台 setting/get.do 显示用，第 27 期 27g）：全部录像的原始帧文件与最终文件之和，
     *  口径与开始录制、申请上传时的配额检查相同。
     *  @param enterpriseId 企业 id
     *  @param out          [出参] 已用字节数；返回 false 时不变
     *  @return 查库成功返回 true
     */
    bool enterpriseUsedBytes(int64_t enterpriseId, int64_t& out);

private:
    //以下私有方法都要求调用方已持有 m_mutex
    void onStartedLocked(int64_t recordId, int code);
    void onFinishedLocked(int64_t recordId, int64_t bytes, int64_t durationMs, uint64_t frames, int reason, int64_t nowMs);
    void onUploadedLocked(int64_t recordId, const std::string& fileName, int64_t bytes, int64_t nowMs);
    //通知 imgserver 删除这条录像的文件（原始帧与最终文件中不为空的）
    void deleteFilesLocked(const CloudRecordRow& row);
    void sendDeleteLocked(const std::vector<std::string>& files);
    //推 1757 给开启者
    void pushRecordLocked(int type, const CloudRecordRow& row);
    //签文件令牌并填好地址
    void fillTokenLocked(int64_t userId, int tokenType, const std::string& fileName, int64_t bytes, int64_t nowMs,
                         CloudRecordTokenInfo& out);
    //各个 1756 操作
    std::string opRenameLocked(int64_t userId, const CloudRecordOpRequest& req, ICloudRecordTitleChecker* checker);
    std::string opDeleteLocked(int64_t userId, const CloudRecordOpRequest& req);
    std::string opDownloadLocked(int64_t userId, const CloudRecordOpRequest& req, int64_t nowMs);
    std::string opUploadLocked(int64_t userId, const CloudRecordOpRequest& req, int64_t nowMs,
                               ICloudRecordTitleChecker* checker);
    std::string opSendLocked(int64_t userId, const CloudRecordOpRequest& req, int64_t nowMs);
    //此人能不能看这条录像（查分享记录与群成员）
    bool canViewLocked(int64_t userId, const CloudRecordRow& row);

    std::mutex             m_mutex;          //串行化全部处理
    CloudRecordStore&      m_store;          //存储（不拥有）
    ICloudRecordEnv&       m_env;            //外部环境（不拥有）
    ICloudRecordCtlSender& m_sender;         //imgserver 控制消息出口（不拥有）
    std::string            m_fileTokenKey;   //文件令牌的 HMAC 密钥
};
