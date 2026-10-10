/**
 * Http会话类, HttpSession.h
 * zhangyl 2018.05.16
 */
#ifndef __HTTP_SESSION_H__
#define __HTTP_SESSION_H__

#include "../net/ByteBuffer.h"
#include "../base/Timestamp.h"
#include "../net/TcpConnection.h"
#include "TcpSession.h"
#include <memory>

//文件共享管理接口的私有辅助函数要用到，只需前向声明
namespace Json { class Value; }
struct AdminInfo;

using namespace net;

class HttpSession
{
public:
    HttpSession(std::shared_ptr<TcpConnection>& conn);
    ~HttpSession() = default;
    HttpSession(const HttpSession& rhs) = delete;
    HttpSession& operator =(const HttpSession& rhs) = delete;

public:
    //有数据可读, 会被多个工作loop调用
    void onRead(const std::shared_ptr<TcpConnection>& conn, ByteBuffer* pBuffer, Timestamp receivTime);

    std::shared_ptr<TcpConnection> getConnectionPtr()
    {
        if (m_tmpConn.expired())
            return NULL;

        return m_tmpConn.lock();
    }

    void send(const char* data, size_t length);

private:
    //按 url 分发请求；param 为 GET 的查询参数或 POST 的请求体。
    bool process(const std::shared_ptr<TcpConnection>& conn, const std::string& url, const std::string& param);
    //分发 /admin/ 开头的控制台接口；body 为 POST 请求体（JSON）。
    bool processAdminRequest(const std::shared_ptr<TcpConnection>& conn, const std::string& url, const std::string& body);

    void makeupResponse(const std::string& input, std::string& output, const std::string& contentType = "application/json;charset=utf-8");
    //把 JSON 文本包成 HTTP 应答并发送。
    void sendJsonResponse(const std::shared_ptr<TcpConnection>& conn, const std::string& json);
    //发送只含 code/msg 两个字段的简单 JSON 应答。
    void sendAdminResult(const std::shared_ptr<TcpConnection>& conn, int code, const std::string& msg);

    void onRegisterResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);
    void onLoginResponse(const std::string& data, const std::shared_ptr<TcpConnection>& conn);

    //==== admin 控制台接口处理器（认证模块 + 仪表盘）====
    void onAdminLogin(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminLogout(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminProfile(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminChangePassword(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminUpdateProfile(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminDashboardStats(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminDashboardMessageTrend(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminUserMessageStats(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //==== admin 控制台接口处理器（企业管理模块）====
    void onAdminEnterpriseList(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminEnterpriseDetail(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminEnterpriseCreate(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminEnterpriseUpdate(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminEnterpriseSetStatus(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminEnterpriseExport(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminEnterpriseDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminEnterpriseAdmins(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminEnterpriseAddAdmin(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminEnterpriseRemoveAdmin(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //==== admin 控制台接口处理器（组织架构模块）====
    void onAdminDeptTree(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminDeptDetail(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminDeptCreate(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminDeptUpdate(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminDeptMove(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminDeptDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminDeptReorder(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //==== admin 控制台接口处理器（员工/用户模块）====
    void onAdminUserList(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminUserDetail(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminUserCreate(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminUserUpdate(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminUserSetStatus(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminUserDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminUserMove(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminUserLoginRecords(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminUserActivity(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminUserKickDevice(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminUserResetPassword(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //==== admin 控制台接口处理器（角色与权限模块）====
    void onAdminUserSetRole(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminUserGetPermissions(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminUserSetPermissions(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //==== admin 控制台接口处理器（平台公告模块）====
    void onAdminAnnouncementList(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminAnnouncementDetail(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminAnnouncementSave(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminAnnouncementSend(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminAnnouncementDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminAnnouncementRecall(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //announcement/repush.do：把一条已发布的公告重新推送给在线且未读的受众，应答带推送人数 pushed；仅平台超管可调用
    void onAdminAnnouncementRepush(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminAnnouncementSetPinned(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //==== admin 控制台接口处理器（审计日志模块）====
    void onAdminAuditList(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminAuditExport(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //审计统计聚合：供「审计日志」界面顶部 4 个统计卡使用，恒为全局/本企业概览。
    void onAdminAuditStats(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //==== admin 控制台接口处理器（服务号模块）====
    void onAdminServiceList(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminServiceDetail(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminServiceSave(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminServiceDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminServiceSetBindings(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminServiceSetStatus(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //==== admin 控制台接口处理器（客户端版本模块）====
    //这组接口<u>仅平台超管可用</u>（admin.enterpriseid == 0）：
    //客户端版本是平台级资产，不属于任何一家企业。
    //升级包本身由运维手工放进升级包目录，控制台只管元数据，
    //故这里没有上传接口，scanpackages 负责把目录里的候选包列给人选。
    void onAdminClientVersionScanPackages(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminClientVersionList(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminClientVersionCreate(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminClientVersionUpdate(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    void onAdminClientVersionSetEnabled(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //回滚：把旧版本重新发布一次。数据层与发布是同一个动作，
    //分两个接口是为了在审计日志里区分意图。

    //群组管理：在企业详情页把企业成员拉进一个群，指定群主、可选管理员（/admin/group/create.do）。
    void onAdminGroupCreate(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //==== admin 控制台接口处理器（敏感词模块，/admin/sensitive/*.do）====
    //企业管理员只能操作本企业的词库与拦截记录；平台超级管理员须在请求里用 enterpriseid
    //指定企业。接口说明见 docs/Admin控制台接口文档.md 的「敏感词」一节。
    //词库分页列表，应答附带总开关状态与该企业词条总数
    void onAdminSensitiveList(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //添加一条词条
    void onAdminSensitiveAdd(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //修改一条词条的原文、分类与备注
    void onAdminSensitiveUpdate(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //删除一条或多条词条
    void onAdminSensitiveDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //批量导入，应答列出被跳过的行与原因
    void onAdminSensitiveImport(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //导出全部词条，应答正文是纯文本（每行一条）
    void onAdminSensitiveExport(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //设置企业的敏感词过滤总开关
    void onAdminSensitiveSetEnabled(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //拦截记录分页列表
    void onAdminSensitiveHits(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //==== admin 控制台接口处理器（企业问答，/admin/qa/*.do，docs/群问答方案.md）====
    //企业管理员只能操作本企业的问答；平台超级管理员须在请求里用 enterpriseid 指定企业。
    //本企业全部企业问答（每条带应用到的群）
    void onAdminQaList(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //新增一条企业问答
    void onAdminQaAdd(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //修改一条企业问答（内容与应用范围整份覆盖）
    void onAdminQaUpdate(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //删除一条企业问答
    void onAdminQaDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //列出本企业的群（群主属于本企业的群），供「应用到所选的群」勾选
    void onAdminQaGroups(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //以下为机器人的管理后台接口（/admin/bot/*.do，docs/机器人方案.md 6.3）与外部系统推送入口（/bot/webhook.do）
    //本企业的企业机器人列表（不含规则）
    void onAdminBotList(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //一个企业机器人的详情（含规则、Webhook 令牌与签名密钥）
    void onAdminBotDetail(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //新建或修改企业机器人（含规则，整份覆盖）
    void onAdminBotSave(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //删除企业机器人（软删除）
    void onAdminBotDelete(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //停用 / 启用企业机器人
    void onAdminBotSetStatus(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //列出本企业的群，供「应用到所选的群」与定时消息的目标勾选
    void onAdminBotGroups(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //「试一试」：按给定规则判定一段模拟输入，不发消息
    void onAdminBotTry(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //重新生成 Webhook 令牌与签名密钥
    void onAdminBotResetToken(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //读外部服务的域名白名单
    void onAdminBotDomains(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //整份替换外部服务的域名白名单
    void onAdminBotSetDomains(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //外部系统经 Webhook 推送内容，由机器人发到群里（不需要管理员 token，凭机器人的 Webhook 令牌）
    void onBotWebhook(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //以下为文件共享的管理后台接口（docs/文件共享方案.md 6.5、docs/Admin控制台接口文档.md「文件共享」一章）
    //读一个企业的功能设置（fileshare.enabled、p2p.enabled 与 group.inviteconfirm 的取值及是否为默认值）
    void onAdminSettingGet(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //改一个企业的功能设置：写库后向该企业在线用户推 1013，开关关闭时结束该企业的全部访问
    void onAdminSettingSet(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //分页查共享
    void onAdminFileShareList(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //读一个共享的成员与权限
    void onAdminFileShareMembers(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //强制取消一个共享
    void onAdminFileShareCancel(const std::string& body, const std::shared_ptr<TcpConnection>& conn);
    //分页查文件共享的审计记录
    void onAdminFileShareAudit(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

private:
    //文件共享管理接口的公共步骤：解析请求体并校验令牌，失败时已回包、返回 false
    bool prepareFileShareRequest(const std::string& body, const std::shared_ptr<TcpConnection>& conn, Json::Value& root,
                                 AdminInfo& admin);
    //文件共享管理接口回包：成功时把应答主体并上 code 与 msg，失败时只回 code 与 msg（rc 为 file_share_admin_result）
    void sendFileShareResult(const std::shared_ptr<TcpConnection>& conn, int rc, Json::Value& out);
    //确定读写功能设置作用的企业：企业管理员为本企业，平台管理员须指定且企业存在；失败时已回包、返回 false
    bool resolveSettingEnterprise(const std::shared_ptr<TcpConnection>& conn, const Json::Value& root, const AdminInfo& admin,
                                  int64_t& enterpriseId);

    std::weak_ptr<TcpConnection>       m_tmpConn;
};


#endif //!__HTTP_SESSION_H__
