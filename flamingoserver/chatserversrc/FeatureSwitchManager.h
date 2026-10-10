/**
 *  按企业的功能开关，FeatureSwitchManager.h
 *
 *  docs/文件共享方案.md 5.8、6.1（第 3 期第 1 步）：功能开关按企业取值，按通用机制设计。目前有六项：「文件共享」、
 *  「客户端点对点直连」（docs/P2P通信方案.md 9.4，第 2 期 2d 加入）、「邀请入群需被邀请人同意」（docs/邀请入群方案.md §3.5）、
 *  「允许用户自助注册」（2026-10-04 加入，关闭后客户端无法自助注册加入该企业，只能由管理员新建账号）、「群语音 / 群视频通话」
 *  （docs/群音视频通话方案.md）、「语音字幕」（2026-10-06 加入，docs/语音字幕方案.md 第 2 节第 8 条，只在客户端使用）与
 *  「云端录制」（2026-10-06 加入，docs/屏幕共享与录制方案.md 12.6.5，默认关闭）。
 *  另有数值型设置项（2026-10-06 加入，同样存在 t_system_setting）：云端录像的企业总配额 cloudrecord.quotagb（GB，默认 50）
 *  与保留天数 cloudrecord.retaindays（默认 30，0 表示永久保留）。它们不是开关，不进 features 对象，取值见 getNumber。
 *    - 取值规则（纯函数 resolveEnabled）：不属于任何企业的账号（企业 id 为 0）恒为关闭；有该企业的设置记录就用记录的值
 *      （"1" 为开启，其余为关闭）；没有记录就用这项功能的内置默认值（文件共享为开启）。因此企业开通时不必预先插入记录；
 *    - 设置记录存在 t_system_setting（主键 f_enterprise_id + f_key，目前有 fileshare.enabled、p2p.enabled、group.inviteconfirm、
 *      register.enabled、groupcall.enabled 与 caption.enabled），启动时整表载入内存，
 *      之后读内存、写库成功后同步更新内存；
 *    - 对客户端的形态是一个 features 对象，例如 {"fileshare":1,"p2p":1}：登录应答里带一份，开关变化时经 1013（cmd_features_notify）
 *      推给该企业的在线用户，两者同构（buildFeaturesJson）。只在服务端使用的开关（目前是「允许用户自助注册」，注册发生在
 *      登录之前，客户端用不上）不放进 features 对象，改动时也不推 1013（isSentToClient）。
 *
 *  本类不碰网络：推送由会话层（ChatServer::applyFeatureChange）在 setSetting 返回之后进行。线程安全：内存表由互斥量保护。
 *  单元测试见 flamingoserver/tests/test_featureswitch.cpp（Suite FSW）。
 *
 *  典型用法：
 *      Singleton<FeatureSwitchManager>::Instance().loadFromDb();                     //main.cpp 启动时
 *      bool on = Singleton<FeatureSwitchManager>::Instance().isEnabled(eid, kFeatureFileShare);
 *      std::string body = FeatureSwitchManager::buildFeaturesJson(mgr.featuresFor(eid));
 *
 *  balloonwj@qq.com   2026-10-03
 */
#ifndef __FEATURE_SWITCH_MANAGER_H__
#define __FEATURE_SWITCH_MANAGER_H__

#include <stdint.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

//功能名：登录应答与 1013 的 features 对象里的键
extern const char* const kFeatureFileShare;   //文件共享
extern const char* const kFeatureP2P;         //客户端点对点直连（只在服务端建 P2P 会话时检查）
extern const char* const kFeatureInviteConfirm; //邀请入群需被邀请人同意（按被邀请人所属企业取值，docs/邀请入群方案.md §3.5）
extern const char* const kFeatureRegister;    //允许用户自助注册（只在服务端注册流程里检查，不下发给客户端）
extern const char* const kFeatureGroupCall;   //群语音 / 群视频通话（docs/群音视频通话方案.md）
extern const char* const kFeatureCaption;     //语音字幕（录制与屏幕共享的字幕，docs/语音字幕方案.md；只在客户端使用）
extern const char* const kFeatureCloudRecord; //云端录制（屏幕共享的服务器录制与独立录制保存到云端，屏幕共享与录制方案 12.6）
extern const char* const kFeatureSecretChat;  //单聊密聊（docs/密聊方案.md 4.9；发起与应答时检查双方所属企业）
extern const char* const kFeatureThreadGroup; //帖子群（docs/帖子群方案.md 4.7；建帖子群与 2700 段请求时检查）
extern const char* const kFeatureBot;         //机器人总开关（docs/机器人方案.md §4.1；关闭后本企业全部机器人停止响应）
extern const char* const kFeatureBotGroup;    //允许群主添加群机器人
extern const char* const kFeatureBotPersonal; //允许员工配置个人机器人（含「离开 / 忙碌自动回复」）
extern const char* const kFeatureBotGroupExternal; //允许群主给群机器人配置外部服务（Webhook 与呼叫外部服务）

//设置项名：t_system_setting 的 f_key
extern const char* const kSettingFileShareEnabled;   //文件共享开关
extern const char* const kSettingP2PEnabled;         //客户端点对点直连开关
extern const char* const kSettingInviteConfirm;      //邀请入群需被邀请人同意开关
extern const char* const kSettingRegisterEnabled;    //允许用户自助注册开关
extern const char* const kSettingGroupCallEnabled;   //群通话开关
extern const char* const kSettingCaptionEnabled;     //语音字幕开关
extern const char* const kSettingCloudRecordEnabled; //云端录制开关
extern const char* const kSettingSecretChatEnabled;  //单聊密聊开关
extern const char* const kSettingThreadGroupEnabled; //帖子群开关
extern const char* const kSettingBotEnabled;         //机器人总开关
extern const char* const kSettingBotGroup;           //允许群主添加群机器人
extern const char* const kSettingBotPersonal;        //允许员工配置个人机器人
extern const char* const kSettingBotGroupExternal;   //允许群主给群机器人配置外部服务
extern const char* const kSettingCloudRecordQuotaGb; //云端录像的企业总配额（数值，单位 GB）
extern const char* const kSettingCloudRecordRetainDays; //云端录像的保留天数（数值，0 表示永久保留）

//一项设置在后台「读设置」接口里的样子
struct FeatureSettingView
{
    std::string m_key;        //设置项名，如 fileshare.enabled
    std::string m_value;      //当前生效的值（开关项为 "0" 或 "1"）
    bool        m_isDefault;  //是否为内置默认值（该企业没有这一项的设置记录）

    FeatureSettingView()
        : m_isDefault(true)
    {
    }
};

/**
 *  FeatureSwitchManager —— 按企业的功能开关。单例（Singleton<FeatureSwitchManager>），线程安全。
 */
class FeatureSwitchManager
{
public:
    FeatureSwitchManager();
    ~FeatureSwitchManager();

    FeatureSwitchManager(const FeatureSwitchManager&) = delete;
    FeatureSwitchManager& operator=(const FeatureSwitchManager&) = delete;

    /**
     *  取值规则（纯函数）。
     *  @param enterpriseId   企业 id；0 表示不属于任何企业
     *  @param hasRecord      该企业有没有这一项的设置记录
     *  @param value          设置记录的值（hasRecord 为假时忽略）
     *  @param builtinDefault 这项功能的内置默认值
     *  @return 是否开启：企业 id 为 0 恒为 false；有记录时值为 "1" 才为 true；没有记录时取内置默认值
     */
    static bool resolveEnabled(int64_t enterpriseId, bool hasRecord, const std::string& value, bool builtinDefault);

    /**
     *  设置项名是否认识、值是否合法：开关项（见 FeatureSwitchManager.cpp 的 kKnownFeatures，如 fileshare.enabled）只接受
     *  "0" 或 "1"；数值项（kKnownNumbers，如 cloudrecord.quotagb）只接受不带符号的十进制整数，且在该项的取值范围内。
     *  @param key   设置项名
     *  @param value 值
     *  @return 两者都合法返回 true
     */
    static bool isValidSetting(const std::string& key, const std::string& value);

    //设置项是否是数值项（如 cloudrecord.quotagb）；开关项与不认识的设置项返回 false
    static bool isNumberSetting(const std::string& key);

    /**
     *  管理员操作日志里一次设置变更的说明文字：开关项同 describeSettingChange，数值项如「云端录像配额设为 100 GB」。
     *  @param key     设置项名
     *  @param value   写入的值
     *  @param enabled 开关项写之后是否开启（数值项忽略）
     *  @return UTF-8 文字；不认识的设置项返回空串
     */
    static std::string describeSettingWrite(const std::string& key, const std::string& value, bool enabled);

    /**
     *  设置项名对应的功能名。后台改设置后据此决定后续动作：文件共享要结束访问会话并推新状态，P2P 只推 1013。
     *  @param key 设置项名，如 kSettingP2PEnabled
     *  @return 功能名，如 kFeatureP2P；不认识的设置项返回空串
     */
    static std::string featureOfSetting(const std::string& key);

    /**
     *  设置项对应的功能是否下发给客户端（放进登录应答与 1013 的 features 对象）。后台改设置后，只有下发给客户端的
     *  功能才需要推 1013。
     *  @param key 设置项名，如 kSettingRegisterEnabled
     *  @return 下发给客户端返回 true；只在服务端使用的开关（如 register.enabled）与不认识的设置项返回 false
     */
    static bool isSentToClient(const std::string& key);

    /**
     *  管理员操作日志里一次开关变更的说明文字，例如「打开文件共享」「关闭客户端点对点直连」。
     *  @param key     设置项名
     *  @param enabled 写之后是否开启
     *  @return UTF-8 文字；不认识的设置项返回空串
     */
    static std::string describeSettingChange(const std::string& key, bool enabled);

    /**
     *  构造 features 对象的 JSON 文本，用作 1013 的包体（登录应答里的 features 与之同构）。
     *  @param features 功能名 → 0 / 1
     *  @return 紧凑格式的 JSON 文本，例如 {"fileshare":1}
     */
    static std::string buildFeaturesJson(const std::map<std::string, int>& features);

    /**
     *  启动时载入 t_system_setting 的全部记录到内存（覆盖内存表）。
     *  @return 成功返回 true；数据库不可用时返回 false（内存表清空，一律按内置默认值处理）
     */
    bool loadFromDb();

    /**
     *  某企业的某项功能是否开启（读内存表）。
     *  @param enterpriseId 企业 id
     *  @param feature      功能名，如 kFeatureFileShare；不认识的功能返回 false
     *  @return 是否开启
     */
    bool isEnabled(int64_t enterpriseId, const std::string& feature) const;

    /**
     *  某企业某个数值项的当前值（读内存表）。
     *  @param enterpriseId 企业 id
     *  @param key          数值项名，如 kSettingCloudRecordQuotaGb
     *  @return 有设置记录且合法时为记录的值，否则为该项的内置默认值；不认识的数值项返回 0
     */
    int64_t getNumber(int64_t enterpriseId, const std::string& key) const;

    /**
     *  某企业下发给客户端的全部功能的取值（登录应答与 1013 用）。
     *  @param enterpriseId 企业 id
     *  @return 功能名 → 0 / 1，包含全部下发给客户端的功能；只在服务端使用的开关（如允许用户自助注册）不在其中
     */
    std::map<std::string, int> featuresFor(int64_t enterpriseId) const;

    /**
     *  某企业全部设置项的当前值（后台「读设置」接口用）。
     *  @param enterpriseId 企业 id
     *  @return 每个认识的设置项一条，含是否为默认值
     */
    std::vector<FeatureSettingView> getSettings(int64_t enterpriseId) const;

    /**
     *  写一项设置（后台「改设置」接口用）：写库成功后更新内存表。
     *  @param enterpriseId  企业 id，须大于 0
     *  @param key           设置项名（须是认识的开关项或数值项）
     *  @param value         值（开关项为 "0" 或 "1"，数值项为范围内的十进制整数）；数值项的 enabledBefore / enabledAfter 恒为 false
     *  @param adminId       操作的管理员 id（记进 f_updated_by）
     *  @param nowMs         当前时间（Unix 毫秒）
     *  @param enabledBefore [出参] 写之前这项功能是否开启
     *  @param enabledAfter  [出参] 写之后这项功能是否开启
     *  @return 成功返回 true；参数不合法或写库失败返回 false（内存表不变）
     */
    bool setSetting(int64_t enterpriseId, const std::string& key, const std::string& value, int64_t adminId, int64_t nowMs,
                    bool& enabledBefore, bool& enabledAfter);

    /**
     *  直接改内存表（单元测试与「库已写好、只同步内存」的场合用），不写库。
     *  @param enterpriseId 企业 id
     *  @param key          设置项名
     *  @param value        值；为空串时删掉这一项（恢复为默认值）
     */
    void setMemoryValue(int64_t enterpriseId, const std::string& key, const std::string& value);

private:
    //在内存表里查一项（调用方已持锁）；没有时返回 false
    bool findLocked(int64_t enterpriseId, const std::string& key, std::string& value) const;

    //按功能名查它的设置项名与内置默认值；不认识的功能返回 false
    static bool describeFeature(const std::string& feature, std::string& key, bool& builtinDefault);

private:
    mutable std::mutex                                       m_mutex;   //保护 m_values
    std::map<int64_t, std::map<std::string, std::string> >   m_values;  //企业 id → 设置项名 → 值（只有设置过的项）
};

#endif //!__FEATURE_SWITCH_MANAGER_H__
