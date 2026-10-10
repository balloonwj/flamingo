/**
 *  文件共享管理后台接口的业务执行层的实现，见 FileShareAdminService.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "FileShareAdminService.h"

#include <vector>
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"
#include "ChatSession.h"              //cancelShareAndNotify / applyFileShareSwitch / pushFeaturesNotify
#include "FeatureSwitchManager.h"
#include "FileShareAdminProtocol.h"
#include "FileShareManager.h"

namespace fileshareadmin {

int GetSettings(int64_t enterpriseId, Json::Value& out)
{
    if (enterpriseId <= 0)
        return file_share_admin_bad_request;
    out = BuildSettings(Singleton<FeatureSwitchManager>::Instance().getSettings(enterpriseId));
    return file_share_admin_ok;
}

int SetSetting(int64_t enterpriseId, const std::string& key, const std::string& value, int64_t adminId, int64_t nowMs,
               bool& enabledBefore, bool& enabledAfter)
{
    enabledBefore = false;
    enabledAfter = false;
    if (enterpriseId <= 0 || !FeatureSwitchManager::isValidSetting(key, value))
        return file_share_admin_bad_request;
    if (!Singleton<FeatureSwitchManager>::Instance().setSetting(enterpriseId, key, value, adminId, nowMs, enabledBefore,
                                                               enabledAfter))
        return file_share_admin_db_error;
    //写库成功之后再推送，按设置项分流。文件共享：推 1013，开关确有变化时结束访问会话、推新状态。
    //P2P：只推 1013；已经建立的 P2P 会话不强制断开，新会话按新开关决定是否建立（docs/P2P通信方案.md 9.4）。
    //邀请入群需被邀请人同意：同样只推 1013；已经发出的入群邀请卡片不受影响，开关关掉后照常可用（docs/邀请入群方案.md §3.5）
    //允许用户自助注册：只在服务端注册流程里检查，不下发给客户端，不推 1013；已注册的账号不受影响
    //语音字幕：只推 1013；客户端据此加载或卸载字幕模块，正在进行的录制与共享里已开始的字幕识别不强制中断（docs/语音字幕方案.md）
    //云端录制：只推 1013；客户端据此显示或隐藏云端录制的入口，进行中的云端录制不强制停止（屏幕共享与录制方案 12.6.5）
    //云端录像的配额与保留天数（数值项）：不推送；客户端在列表应答里取，到期清理按新值进行
    //单聊密聊：开关从开变关时先结束该企业成员参与的全部密聊（双方转为「只能查看」），再推 1013（docs/密聊方案.md 4.9）
    //帖子群：只推 1013；客户端据此隐藏帖子群，服务端此后拒绝 2700 段请求，已有的帖子保留（docs/帖子群方案.md 4.7）
    if (FeatureSwitchManager::featureOfSetting(key) == kFeatureFileShare)
    {
        ChatSession::applyFileShareSwitch(enterpriseId, enabledBefore, enabledAfter);
    }
    else if (FeatureSwitchManager::featureOfSetting(key) == kFeatureSecretChat)
    {
        if (enabledBefore && !enabledAfter)
            ChatSession::endSecretChatsOfEnterprise(enterpriseId);
        ChatSession::pushFeaturesNotify(enterpriseId);
    }
    else if (FeatureSwitchManager::isSentToClient(key))
        ChatSession::pushFeaturesNotify(enterpriseId);
    else
        LOGI("server-side switch set, no features notify, enterpriseid: %lld, key: %s, enabled: %d -> %d",
             (long long)enterpriseId, key.c_str(), enabledBefore ? 1 : 0, enabledAfter ? 1 : 0);
    return file_share_admin_ok;
}

int ListShares(const Json::Value& req, int64_t adminEnterpriseId, Json::Value& out)
{
    const int64_t filter = ResolveListEnterprise(adminEnterpriseId, ParseId(req, "enterpriseid"));
    const ShareAdminQuery q = ParseShareQuery(req, filter);
    std::vector<ShareAdminView> views;
    int64_t total = 0;
    if (!Singleton<FileShareManager>::Instance().queryAdminShares(q, views, total))
        return file_share_admin_db_error;
    out = BuildShareList(views, total);
    return file_share_admin_ok;
}

int GetMembers(int64_t shareId, int64_t adminEnterpriseId, Json::Value& out)
{
    if (shareId <= 0)
        return file_share_admin_bad_request;
    FileShareManager& mgr = Singleton<FileShareManager>::Instance();
    ShareRecord share;
    if (!mgr.getShare(shareId, share))
        return file_share_admin_not_found;
    if (!ShareInScope(adminEnterpriseId, share.m_enterpriseId))
        return file_share_admin_denied;
    std::vector<ShareMemberView> members;
    if (!mgr.getMemberViews(shareId, members))
        return file_share_admin_db_error;
    out = BuildMembers(members);
    return file_share_admin_ok;
}

int CancelShare(int64_t shareId, int64_t adminEnterpriseId, int64_t adminId, int64_t nowMs, std::string& shareName,
                int64_t& shareEnterpriseId)
{
    shareName.clear();
    shareEnterpriseId = 0;
    if (shareId <= 0)
        return file_share_admin_bad_request;
    ShareRecord share;
    if (!Singleton<FileShareManager>::Instance().getShare(shareId, share))
        return file_share_admin_not_found;
    shareName = share.m_name;
    shareEnterpriseId = share.m_enterpriseId;
    if (!ShareInScope(adminEnterpriseId, share.m_enterpriseId))
        return file_share_admin_denied;

    const int code = ChatSession::cancelShareAndNotify(shareId, share_invalid_admin_canceled, adminId, nowMs);
    switch (code)
    {
    //取消成功：成员与分享者已收到「已失效，管理员在后台强制取消」
    case error_code_ok:
        LOGI("share force canceled by admin, shareid: %lld, admin: %lld", (long long)shareId, (long long)adminId);
        return file_share_admin_ok;

    //已经取消过：不再推送
    case error_code_share_canceled:
        return file_share_admin_canceled;

    //读记录与改记录之间被删掉：按不存在处理
    case error_code_share_not_exist:
        return file_share_admin_not_found;

    //数据库失败
    default:
        return file_share_admin_db_error;
    }
}

int QueryAudits(const Json::Value& req, int64_t adminEnterpriseId, Json::Value& out)
{
    const int64_t filter = ResolveListEnterprise(adminEnterpriseId, ParseId(req, "enterpriseid"));
    const ShareAuditQuery q = ParseAuditQuery(req, filter);
    std::vector<ShareAuditView> views;
    int64_t total = 0;
    if (!Singleton<FileShareManager>::Instance().queryAdminAudits(q, views, total))
        return file_share_admin_db_error;
    out = BuildAuditList(views, total);
    return file_share_admin_ok;
}

} // namespace fileshareadmin
