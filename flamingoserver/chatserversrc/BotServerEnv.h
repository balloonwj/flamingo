/**
 *  机器人运行环境的生产实现，BotServerEnv.h（docs/机器人方案.md §5.3）。
 *
 *  把 IBotEnv 的各项查询接到现有的管理器上：群与成员（GroupManager）、企业归属与部门（DepartmentManager）、
 *  昵称（UserManager）、问答（GroupQaManager）、功能开关（FeatureSwitchManager）、发消息（ChatServer::postMessageAs）、
 *  敏感词（ChatSession::checkSensitiveForServer）与出站 HTTP（utils/HttpsClient）。单元测试不用它，而是注入假实现。
 *
 *  典型用法（main.cpp）：
 *      static BotServerEnv botEnv;
 *      Singleton<BotEngine>::Instance().start(&botEnv, 2);
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include "BotEngine.h"

//机器人运行环境的生产实现；无状态，可在任意线程调用
class BotServerEnv final : public IBotEnv
{
public:
    BotServerEnv();
    ~BotServerEnv() override;

    //当前时间（Unix 毫秒）
    int64_t nowMs() override;

    //群的企业归属（群主所属企业）、群名、是否为帖子群
    bool groupInfo(int64_t groupid, int64_t& enterpriseId, std::string& name, bool& isThread) override;

    //用户所属企业
    int64_t userEnterprise(int64_t userId) override;

    //账号昵称
    std::string displayName(int64_t userId) override;

    //群名片优先，其次账号昵称
    std::string groupDisplayName(int64_t groupid, int64_t userId) override;

    //合并各设备后的在线状态
    int presence(int64_t userId) override;

    //所在部门及全部上级部门
    void userDeptChain(int64_t userId, std::vector<int64_t>& out) override;

    //本群问答与企业问答（合并去重之后）；groupid 为 0 时只取应用到全部群的企业问答
    void groupQa(int64_t groupid, int64_t enterpriseId, std::vector<bot::QaItem>& out) override;

    //企业的全部群
    void enterpriseGroups(int64_t enterpriseId, std::vector<int64_t>& out) override;

    //功能开关
    bool featureOn(int64_t enterpriseId, const std::string& feature) override;

    //以指定身份发一条文字消息
    int64_t postMessage(int ctype, int64_t from, int64_t target, const std::string& content,
                        const std::string& clientMsgId) override;

    //外部内容的敏感词检查（来源 sensitive_source_bot_external）
    bool hitSensitive(int64_t enterpriseId, int64_t botUserId, const std::string& botName, int64_t groupid,
                      const std::string& text) override;

    //出站 POST
    bool httpPost(const std::string& url, const std::vector<std::pair<std::string, std::string> >& headers,
                  const std::string& body, int timeoutMs, int& status, std::string& respBody) override;
};
