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
    void onAdminClientVersionRepublish(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

    //群组管理：在企业详情页把企业成员拉进一个群，指定群主、可选管理员（/admin/group/create.do）。
    void onAdminGroupCreate(const std::string& body, const std::shared_ptr<TcpConnection>& conn);

private:
    std::weak_ptr<TcpConnection>       m_tmpConn;
};


#endif //!__HTTP_SESSION_H__
