/**
 *  按企业的功能开关 FeatureSwitchManager 的实现，见 FeatureSwitchManager.h。
 *  balloonwj@qq.com   2026-10-03
 */
#include "FeatureSwitchManager.h"

#include <memory>
#include <sstream>
#include "../jsoncpp1.9.0/json.h"
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../dbapi/DbConfig.h"
#include "../dbapi/DbTransaction.h"
#include "../base/AsyncLog.h"
#include "../base/Singleton.h"

const char* const kFeatureFileShare = "fileshare";
const char* const kFeatureP2P = "p2p";
const char* const kFeatureInviteConfirm = "inviteconfirm";
const char* const kFeatureRegister = "register";
const char* const kFeatureGroupCall = "groupcall";
const char* const kFeatureCaption = "caption";
const char* const kFeatureCloudRecord = "cloudrecord";
const char* const kFeatureSecretChat = "secretchat";
const char* const kFeatureThreadGroup = "threadgroup";
const char* const kFeatureBot = "bot";
const char* const kFeatureBotGroup = "botgroup";
const char* const kFeatureBotPersonal = "botpersonal";
const char* const kFeatureBotGroupExternal = "botgroupexternal";
const char* const kSettingFileShareEnabled = "fileshare.enabled";
const char* const kSettingP2PEnabled = "p2p.enabled";
const char* const kSettingInviteConfirm = "group.inviteconfirm";
const char* const kSettingRegisterEnabled = "register.enabled";
const char* const kSettingGroupCallEnabled = "groupcall.enabled";
const char* const kSettingCaptionEnabled = "caption.enabled";
const char* const kSettingCloudRecordEnabled = "cloudrecord.enabled";
const char* const kSettingSecretChatEnabled = "secretchat.enabled";
const char* const kSettingThreadGroupEnabled = "threadgroup.enabled";
const char* const kSettingBotEnabled = "bot.enabled";
const char* const kSettingBotGroup = "bot.groupbot";
const char* const kSettingBotPersonal = "bot.personal";
const char* const kSettingBotGroupExternal = "bot.groupexternal";
const char* const kSettingCloudRecordQuotaGb = "cloudrecord.quotagb";
const char* const kSettingCloudRecordRetainDays = "cloudrecord.retaindays";

namespace {

//开关项的两个取值
const char* const kSwitchOn = "1";
const char* const kSwitchOff = "0";

//一项认识的功能：对外的功能名、存库的设置项名、内置默认值、中文显示名、是否下发给客户端
struct KnownFeature
{
    const char* m_feature;         //功能名（features 对象里的键）
    const char* m_key;             //设置项名（t_system_setting 的 f_key）
    bool        m_builtinDefault;  //没有设置记录时的取值
    const char* m_displayName;     //中文显示名（UTF-8），用于管理员操作日志的说明文字
    bool        m_sentToClient;    //是否放进登录应答与 1013 的 features 对象；为 false 时改设置也不推 1013
};

//全部认识的功能。新增功能时在这里加一行，后台读设置会自动带上它；m_sentToClient 为 true 的还会进登录应答与 1013
const KnownFeature kKnownFeatures[] =
{
    //文件共享：未设置时默认开启（docs/文件共享方案.md 2.5、5.8）
    { kFeatureFileShare, kSettingFileShareEnabled, true, u8"文件共享", true },
    //客户端点对点直连：未设置时默认开启（docs/P2P通信方案.md 第 2 节第 6、7 条与 9.4 节）
    { kFeatureP2P, kSettingP2PEnabled, true, u8"客户端点对点直连", true },
    //邀请入群需被邀请人同意：未设置时默认关闭，被邀请人直接入群（docs/邀请入群方案.md §3.5）
    { kFeatureInviteConfirm, kSettingInviteConfirm, false, u8"邀请入群需被邀请人同意", true },
    //允许用户自助注册：未设置时默认开启。关闭后注册流程（RegisterService::doRegister）拒绝加入该企业，只能由管理员
    //新建账号。注册发生在登录之前，客户端用不上这个值，不下发
    { kFeatureRegister, kSettingRegisterEnabled, true, u8"允许用户自助注册", false },
    //群语音 / 群视频通话：未设置时默认开启。关闭后客户端隐藏群聊窗的语音、视频按钮，服务端拒绝发起与加入
    //（docs/群音视频通话方案.md、docs/协议设计-v2.md 21.7）
    { kFeatureGroupCall, kSettingGroupCallEnabled, true, u8"群语音 / 群视频通话", true },
    //语音字幕：未设置时默认开启。关闭后客户端不加载字幕模块，录制与屏幕共享都不再出现字幕相关入口；服务端不做检查，
    //字幕识别与导出全在客户端本机完成（docs/语音字幕方案.md 第 2 节第 8 条）
    { kFeatureCaption, kSettingCaptionEnabled, true, u8"语音字幕", true },
    //云端录制：未设置时默认关闭（第 52 条）。关闭后客户端隐藏「云端录制」与「保存到云端」入口，服务端拒绝开始云端录制与上传；
    //已有的云端录像照常可看、可删（docs/屏幕共享与录制方案.md 12.6.5）
    { kFeatureCloudRecord, kSettingCloudRecordEnabled, false, u8"云端录制", true },
    //单聊密聊：未设置时默认开启。关闭后客户端不显示「密聊」按钮，服务端拒绝发起与应答，并结束本企业成员参与的
    //全部密聊（docs/密聊方案.md 4.9）
    { kFeatureSecretChat, kSettingSecretChatEnabled, true, u8"单聊密聊", true },
    //帖子群：未设置时默认开启。关闭后客户端隐藏全部帖子群与建群窗口里的类型选择，服务端拒绝建帖子群与 2700 段请求；
    //已有的帖子与留言保留（docs/帖子群方案.md 4.7）
    { kFeatureThreadGroup, kSettingThreadGroupEnabled, true, u8"帖子群", true },
    //机器人总开关：未设置时默认开启。关闭后本企业的企业机器人、群机器人、个人机器人（含自动回复）一律停止响应，
    //客户端隐藏全部机器人入口（docs/机器人方案.md §4.1）
    { kFeatureBot, kSettingBotEnabled, true, u8"机器人", true },
    //允许群主添加群机器人：未设置时默认开启。关闭后群主不能新建群机器人，已有的群机器人停止响应
    { kFeatureBotGroup, kSettingBotGroup, true, u8"允许群主添加群机器人", true },
    //允许员工配置个人机器人：未设置时默认开启。关闭后个人机器人与「离开 / 忙碌自动回复」都停止代答
    { kFeatureBotPersonal, kSettingBotPersonal, true, u8"允许员工配置个人机器人", true },
    //允许群主给群机器人配置外部服务：未设置时默认关闭。外部服务会把群消息发出服务器，须企业管理员明确打开（§5.6）
    { kFeatureBotGroupExternal, kSettingBotGroupExternal, false, u8"允许群主给群机器人配置外部服务", true },
};

//一项认识的数值设置：设置项名、内置默认值、取值范围、中文显示名与单位
struct KnownNumber
{
    const char* m_key;            //设置项名（t_system_setting 的 f_key）
    int64_t     m_builtinDefault; //没有设置记录时的取值
    int64_t     m_min;            //最小值（含）
    int64_t     m_max;            //最大值（含）
    const char* m_displayName;    //中文显示名（UTF-8），用于管理员操作日志的说明文字
    const char* m_unit;           //单位（UTF-8），接在数值之后
};

//云端录像配额的默认值（GB，第 52 条）与上限（GB）
const int64_t kCloudRecordQuotaGbDefault = 50;
const int64_t kCloudRecordQuotaGbMax = 1000000;
//云端录像保留天数的默认值（第 52 条）与上限（天，约十年）；0 表示永久保留
const int64_t kCloudRecordRetainDaysDefault = 30;
const int64_t kCloudRecordRetainDaysMax = 3650;
//数值项的值最多几位十进制数字：超过的直接判为不合法，免得换算时溢出
const size_t kMaxNumberDigits = 12;

//全部认识的数值设置。新增时在这里加一行，后台读设置会自动带上它
const KnownNumber kKnownNumbers[] =
{
    //云端录像的企业总配额：原始帧文件与合成后的 MP4 都计入；0 表示不允许再新增云端录像
    { kSettingCloudRecordQuotaGb, kCloudRecordQuotaGbDefault, 0, kCloudRecordQuotaGbMax, u8"云端录像配额", u8" GB" },
    //云端录像的保留天数：到期后由 chatserver 删除；0 表示永久保留
    { kSettingCloudRecordRetainDays, kCloudRecordRetainDaysDefault, 0, kCloudRecordRetainDaysMax, u8"云端录像保留天数",
      u8" 天" },
};

const size_t kKnownNumberCount = sizeof(kKnownNumbers) / sizeof(kKnownNumbers[0]);

//操作日志说明文字：数值项「xx 设为 N 单位」
const char* const kTextSetTo = u8"设为 ";

//按设置项名找数值项；不认识时返回 NULL
const KnownNumber* findNumber(const std::string& key)
{
    for (size_t i = 0; i < kKnownNumberCount; ++i)
    {
        if (key == kKnownNumbers[i].m_key)
            return &kKnownNumbers[i];
    }
    return NULL;
}

//解析不带符号的十进制整数；空串、含非数字字符、位数过多时返回 false
bool parseDecimal(const std::string& text, int64_t& out)
{
    if (text.empty() || text.size() > kMaxNumberDigits)
        return false;
    const int64_t kBase = 10;
    int64_t v = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] < '0' || text[i] > '9')
            return false;
        v = v * kBase + (text[i] - '0');
    }
    out = v;
    return true;
}

//操作日志说明文字的前缀：开关写之后为开启 / 关闭
const char* const kTextTurnOn = u8"打开";
const char* const kTextTurnOff = u8"关闭";

const size_t kKnownFeatureCount = sizeof(kKnownFeatures) / sizeof(kKnownFeatures[0]);

//打开一条数据库连接；失败时记日志并返回 NULL
IDatabase* openConnection(const char* caller)
{
    IDatabase* pConn = DbFactory::createConnection();
    if (pConn == NULL)
        LOGE("FeatureSwitchManager::%s db init failed, db: %s", caller, Singleton<CDbConfig>::Instance().describe().c_str());
    return pConn;
}

//转义一段要拼进 SQL 的文本
std::string escapeText(IDatabase* pConn, const std::string& text)
{
    std::vector<char> buf(text.size() * 2 + 1, '\0');
    pConn->escapeString(&buf[0], text.c_str(), static_cast<uint32_t>(text.size()));
    return std::string(&buf[0]);
}

} // namespace

FeatureSwitchManager::FeatureSwitchManager()
{
}

FeatureSwitchManager::~FeatureSwitchManager()
{
}

bool FeatureSwitchManager::resolveEnabled(int64_t enterpriseId, bool hasRecord, const std::string& value, bool builtinDefault)
{
    //不属于任何企业的账号：开关按企业取值，没有企业就无从判定，各项功能一律关闭
    if (enterpriseId <= 0)
        return false;
    if (hasRecord)
        return value == kSwitchOn;
    return builtinDefault;
}

bool FeatureSwitchManager::isValidSetting(const std::string& key, const std::string& value)
{
    for (size_t i = 0; i < kKnownFeatureCount; ++i)
    {
        if (key == kKnownFeatures[i].m_key)
            return value == kSwitchOn || value == kSwitchOff;
    }
    const KnownNumber* number = findNumber(key);
    if (number == NULL)
        return false;
    int64_t v = 0;
    return parseDecimal(value, v) && v >= number->m_min && v <= number->m_max;
}

bool FeatureSwitchManager::isNumberSetting(const std::string& key)
{
    return findNumber(key) != NULL;
}

std::string FeatureSwitchManager::describeSettingWrite(const std::string& key, const std::string& value, bool enabled)
{
    const KnownNumber* number = findNumber(key);
    if (number == NULL)
        return describeSettingChange(key, enabled);
    return std::string(number->m_displayName) + kTextSetTo + value + number->m_unit;
}

int64_t FeatureSwitchManager::getNumber(int64_t enterpriseId, const std::string& key) const
{
    const KnownNumber* number = findNumber(key);
    if (number == NULL)
        return 0;
    std::lock_guard<std::mutex> guard(m_mutex);
    std::string value;
    int64_t v = 0;
    //记录被手工改坏（不是数字或超出范围）时按默认值处理
    if (findLocked(enterpriseId, key, value) && parseDecimal(value, v) && v >= number->m_min && v <= number->m_max)
        return v;
    return number->m_builtinDefault;
}

std::string FeatureSwitchManager::featureOfSetting(const std::string& key)
{
    for (size_t i = 0; i < kKnownFeatureCount; ++i)
    {
        if (key == kKnownFeatures[i].m_key)
            return kKnownFeatures[i].m_feature;
    }
    return std::string();
}

bool FeatureSwitchManager::isSentToClient(const std::string& key)
{
    for (size_t i = 0; i < kKnownFeatureCount; ++i)
    {
        if (key == kKnownFeatures[i].m_key)
            return kKnownFeatures[i].m_sentToClient;
    }
    return false;
}

std::string FeatureSwitchManager::describeSettingChange(const std::string& key, bool enabled)
{
    for (size_t i = 0; i < kKnownFeatureCount; ++i)
    {
        if (key == kKnownFeatures[i].m_key)
            return std::string(enabled ? kTextTurnOn : kTextTurnOff) + kKnownFeatures[i].m_displayName;
    }
    return std::string();
}

std::string FeatureSwitchManager::buildFeaturesJson(const std::map<std::string, int>& features)
{
    Json::Value root(Json::objectValue);
    for (std::map<std::string, int>::const_iterator it = features.begin(); it != features.end(); ++it)
        root[it->first] = it->second;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, root);
}

bool FeatureSwitchManager::describeFeature(const std::string& feature, std::string& key, bool& builtinDefault)
{
    for (size_t i = 0; i < kKnownFeatureCount; ++i)
    {
        if (feature == kKnownFeatures[i].m_feature)
        {
            key = kKnownFeatures[i].m_key;
            builtinDefault = kKnownFeatures[i].m_builtinDefault;
            return true;
        }
    }
    return false;
}

bool FeatureSwitchManager::loadFromDb()
{
    std::map<int64_t, std::map<std::string, std::string> > values;
    std::unique_ptr<IDatabase> pConn(openConnection("loadFromDb"));
    bool ok = false;
    if (pConn)
    {
        QueryResult* pResult = pConn->query("SELECT f_enterprise_id, f_key, f_value FROM t_system_setting");
        if (pResult != NULL)
        {
            while (true)
            {
                Field* pRow = pResult->fetch();
                if (pRow == NULL)
                    break;
                values[static_cast<int64_t>(pRow[0].getUInt64())][pRow[1].getString()] = pRow[2].getString();
                if (!pResult->nextRow())
                    break;
            }
            pResult->endQuery();
            delete pResult;
            ok = true;
        }
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    m_values.swap(values);
    if (ok)
        LOGI("feature switches loaded, enterprises with settings: %u", static_cast<unsigned>(m_values.size()));
    else
        LOGE("feature switches load failed, every enterprise uses the built-in defaults");
    return ok;
}

bool FeatureSwitchManager::findLocked(int64_t enterpriseId, const std::string& key, std::string& value) const
{
    std::map<int64_t, std::map<std::string, std::string> >::const_iterator e = m_values.find(enterpriseId);
    if (e == m_values.end())
        return false;
    std::map<std::string, std::string>::const_iterator k = e->second.find(key);
    if (k == e->second.end())
        return false;
    value = k->second;
    return true;
}

bool FeatureSwitchManager::isEnabled(int64_t enterpriseId, const std::string& feature) const
{
    std::string key;
    bool builtinDefault = false;
    if (!describeFeature(feature, key, builtinDefault))
        return false;
    std::lock_guard<std::mutex> guard(m_mutex);
    std::string value;
    const bool hasRecord = findLocked(enterpriseId, key, value);
    return resolveEnabled(enterpriseId, hasRecord, value, builtinDefault);
}

std::map<std::string, int> FeatureSwitchManager::featuresFor(int64_t enterpriseId) const
{
    std::map<std::string, int> out;
    std::lock_guard<std::mutex> guard(m_mutex);
    for (size_t i = 0; i < kKnownFeatureCount; ++i)
    {
        //只在服务端使用的开关不下发
        if (!kKnownFeatures[i].m_sentToClient)
            continue;
        std::string value;
        const bool hasRecord = findLocked(enterpriseId, kKnownFeatures[i].m_key, value);
        out[kKnownFeatures[i].m_feature] =
            resolveEnabled(enterpriseId, hasRecord, value, kKnownFeatures[i].m_builtinDefault) ? 1 : 0;
    }
    return out;
}

std::vector<FeatureSettingView> FeatureSwitchManager::getSettings(int64_t enterpriseId) const
{
    std::vector<FeatureSettingView> out;
    std::lock_guard<std::mutex> guard(m_mutex);
    for (size_t i = 0; i < kKnownFeatureCount; ++i)
    {
        FeatureSettingView view;
        view.m_key = kKnownFeatures[i].m_key;
        std::string value;
        const bool hasRecord = findLocked(enterpriseId, view.m_key, value);
        view.m_isDefault = !hasRecord;
        view.m_value = resolveEnabled(enterpriseId, hasRecord, value, kKnownFeatures[i].m_builtinDefault) ? kSwitchOn : kSwitchOff;
        out.push_back(view);
    }
    //数值项：有合法记录时用记录的值，否则用内置默认值（与 getNumber 一致；这里已持锁，不能调它）
    for (size_t i = 0; i < kKnownNumberCount; ++i)
    {
        FeatureSettingView view;
        view.m_key = kKnownNumbers[i].m_key;
        std::string value;
        int64_t v = 0;
        const bool valid = findLocked(enterpriseId, view.m_key, value) && parseDecimal(value, v) &&
                           v >= kKnownNumbers[i].m_min && v <= kKnownNumbers[i].m_max;
        view.m_isDefault = !valid;
        view.m_value = std::to_string(static_cast<long long>(valid ? v : kKnownNumbers[i].m_builtinDefault));
        out.push_back(view);
    }
    return out;
}

bool FeatureSwitchManager::setSetting(int64_t enterpriseId, const std::string& key, const std::string& value, int64_t adminId,
                                      int64_t nowMs, bool& enabledBefore, bool& enabledAfter)
{
    enabledBefore = false;
    enabledAfter = false;
    if (enterpriseId <= 0 || !isValidSetting(key, value))
        return false;

    //这一项对应的功能与内置默认值（isValidSetting 已保证认识）
    bool builtinDefault = false;
    for (size_t i = 0; i < kKnownFeatureCount; ++i)
    {
        if (key == kKnownFeatures[i].m_key)
            builtinDefault = kKnownFeatures[i].m_builtinDefault;
    }
    if (!isNumberSetting(key))
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::string old;
        const bool hasRecord = findLocked(enterpriseId, key, old);
        enabledBefore = resolveEnabled(enterpriseId, hasRecord, old, builtinDefault);
    }

    std::unique_ptr<IDatabase> pConn(openConnection("setSetting"));
    if (!pConn)
        return false;
    CDbTransaction transaction(pConn.get());
    if (!transaction.IsActive())
    {
        LOGE("FeatureSwitchManager::setSetting begin transaction error, enterpriseid: %lld", (long long)enterpriseId);
        return false;
    }
    //两种数据库都支持的写法：先删后插（主键为 企业 id + 设置项名）
    const std::string escKey = escapeText(pConn.get(), key);
    const std::string escValue = escapeText(pConn.get(), value);
    std::ostringstream del;
    del << "DELETE FROM t_system_setting WHERE f_enterprise_id = " << enterpriseId << " AND f_key = '" << escKey << "'";
    std::ostringstream ins;
    ins << "INSERT INTO t_system_setting (f_enterprise_id, f_key, f_value, f_updated_by, f_update_time) VALUES ("
        << enterpriseId << ", '" << escKey << "', '" << escValue << "', " << adminId << ", " << nowMs << ")";
    if (!pConn->execute(del.str().c_str()) || !pConn->execute(ins.str().c_str()) || !transaction.Commit())
    {
        LOGE("FeatureSwitchManager::setSetting write error, enterpriseid: %lld, key: %s", (long long)enterpriseId, key.c_str());
        return false;
    }

    std::lock_guard<std::mutex> guard(m_mutex);
    m_values[enterpriseId][key] = value;
    //数值项没有开关状态，enabledBefore 与 enabledAfter 都保持 false
    if (!isNumberSetting(key))
        enabledAfter = resolveEnabled(enterpriseId, true, value, builtinDefault);
    LOGI("feature switch set, enterpriseid: %lld, key: %s, value: %s, by: %lld, enabled: %d -> %d", (long long)enterpriseId,
         key.c_str(), value.c_str(), (long long)adminId, enabledBefore ? 1 : 0, enabledAfter ? 1 : 0);
    return true;
}

void FeatureSwitchManager::setMemoryValue(int64_t enterpriseId, const std::string& key, const std::string& value)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    if (value.empty())
    {
        std::map<int64_t, std::map<std::string, std::string> >::iterator e = m_values.find(enterpriseId);
        if (e != m_values.end())
        {
            e->second.erase(key);
            if (e->second.empty())
                m_values.erase(e);
        }
        return;
    }
    m_values[enterpriseId][key] = value;
}
