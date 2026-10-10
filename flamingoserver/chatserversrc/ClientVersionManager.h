/**
 *  客户端版本发布记录管理器，ClientVersionManager.h
 *
 *  负责 t_client_version 表的查询。一行记录 = 某平台的一次版本发布；"当前最新版本"
 *  的取数规则是<u>按平台过滤，在已发布（f_enabled=1）的记录里取 f_publish_time 最大
 *  的一条</u>，而不是取版本号最大的一条 —— 这样出问题要回滚时，把旧版本那行重新发布
 *  一次即可，不必改动或删除已有数据。
 *
 *  两处用到它：
 *    - ChatSession::onCheckUpdate 收到 cmd_check_update(1905) 后取最新版本，与客户端
 *      报上来的版本比较，决定是否下发升级信息；
 *    - ChatSession::onLogin 的版本门槛，读最新记录的 f_min_version 作为最低可登录版本
 *      （读不到则回落到编译期兜底常量，见 ChatSession.cpp）。
 *
 *  每次操作单独建立一条数据库连接（与 ContactRemarkManager / FavoriteManager 同一风格），
 *  不在内存做缓存 —— 发版是极低频操作，而缓存要处理失效，不划算。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "UpdateProtocol.h"   //ClientVersionInfo：与应答共用同一份版本信息结构，避免再定义一份并写映射代码

//updateVersion 的 forced 参数取此值时不修改 f_forced 列：clientversion/update.do 的请求里
//没带 forced 字段（老版本控制台只改说明与最低版本）时用它，保持原值不变。
constexpr int32_t CLIENT_VERSION_FORCED_KEEP = -1;

//版本表里的一条完整记录，含主键、发布状态与两个时间。
//与 ClientVersionInfo 的分工：后者只装"下发给客户端要用的那几个字段"，
//本结构是控制台列表与增改用的完整视图。
struct ClientVersionRecord
{
    int64_t     id;              //记录ID（f_id）；新建时无意义，由服务端回填
    int32_t     platform;        //客户端平台，取值见 enum platform_type
    std::string version;         //本条记录发布的版本号
    std::string minVersion;      //允许登录的最低版本号
    std::string releaseNote;     //更新说明
    std::string packageMd5;      //升级包内容MD5（小写十六进制32位）
    int64_t     packageSize;     //升级包字节数
    std::string packageName;     //升级包原始文件名
    int32_t     forced;          //是否强制升级：0 否、1 是（与 minVersion 相互独立，任一命中即强制）
    int32_t     enabled;         //是否已发布：0 草稿、1 已发布
    int64_t     publishTime;     //发布时间，Unix 毫秒；取数按它最大的一条
    int64_t     creatorAdminId;  //发布人的管理员ID；0 表示非控制台创建
    int64_t     createTime;      //记录创建时间，Unix 毫秒；回滚不会改动它

    ClientVersionRecord()
        : id(0), platform(0), packageSize(0), forced(0), enabled(0)
        , publishTime(0), creatorAdminId(0), createTime(0)
    {
    }
};

//客户端版本发布记录管理器；全局单例，经 Singleton<ClientVersionManager> 访问。
//本类无需 init()：不持有任何启动期加载的状态，每次调用现开连接现查。
class ClientVersionManager final
{
public:
    ClientVersionManager();
    ~ClientVersionManager();

    ClientVersionManager(const ClientVersionManager& rhs) = delete;
    ClientVersionManager& operator=(const ClientVersionManager& rhs) = delete;

    /**
     *  取某平台当前已发布的最新版本。
     *
     *    platform： 客户端平台，取值见 Protocol.h 的 enum platform_type；须为正数
     *    out：      [出参] 查到的版本信息；<u>进函数先清空</u>，仅当返回 true 时各字段有效
     *
     *  @return true 查到一条已发布记录；false 表示该平台还没有任何已发布版本，
     *          <u>或</u>数据库不可用。两种情况调用方都按"没有可下发的新版本"处理：
     *          检查更新回 hasupdate=false，登录门槛回落到编译期兜底常量。
     *          之所以不把这两种情况分成两个返回值：调用方对它们的处理完全一致，
     *          区分只会让调用点多一个用不上的分支；两者在日志里已经分开记录，
     *          排查时凭日志即可分辨。
     *
     *  多条记录 f_publish_time 相同时取 f_id 更大的那条（即后插入的那条），
     *  保证结果确定、可复现。
     */
    bool getLatestVersion(int32_t platform, ClientVersionInfo& out);

    /**
     *  列出版本记录，供控制台的「客户端版本」页展示。
     *
     *    platform： 按平台过滤；<u>取 0 或负数表示不过滤</u>，返回全部平台
     *    out：      [出参] 记录列表；<u>进函数先清空</u>，按发布时间倒序、同时间按 ID 倒序
     *
     *  @return true 查询成功（一条记录都没有也算成功，此时 out 为空）；false 表示库不可用。
     *
     *  <u>草稿也会列出来</u>：控制台要能看到没放出去的记录并继续编辑它，
     *  这与"下发给客户端"的取数规则（只取已发布）不是一回事。
     */
    bool listVersions(int32_t platform, std::vector<ClientVersionRecord>& out);

    /**
     *  新建一条版本记录。
     *
     *    rec：          待写入的记录。其中 id / publishTime / createTime 由本方法自行填写，
     *                   调用方给的值会被忽略；enabled 决定是存草稿还是直接发布
     *    nowMs：        当前时刻，Unix 毫秒。写入 f_create_time；enabled 为 1 时同时写入
     *                   f_publish_time。由调用方传入而非本类自取，便于测试构造确定的时间序
     *    outId：        [出参] 成功时回填新记录的 ID
     *    outErrorCode： [出参] 失败原因（取值见 Protocol.h 的 enum error_code）；成功时置 error_code_ok
     *
     *  @return true 写入成功；false 时看 outErrorCode。
     *
     *  入参校验：版本号非空、MD5 为 32 位十六进制、包字节数为正、平台号为正。
     *  任一不满足返回 error_code_bad_request —— 这几个值直接决定客户端能不能下到包，
     *  与其写进库等着客户端下载失败，不如在入口拦掉。
     *
     *  本方法<u>不校验</u>"包是否真的躺在升级包目录里"，那属于会话层 / 接口层的业务判定
     *  （由 ScanUpdatePackages 的结果比对）。
     */
    bool createVersion(const ClientVersionRecord& rec, int64_t nowMs,
                       int64_t& outId, int& outErrorCode);

    /**
     *  修改一条版本记录的最低版本号、更新说明与「强制升级」标志。
     *
     *    id：           记录ID
     *    minVersion：   新的最低可登录版本号，按传入值写入；<u>空串表示不另设门槛</u>，
     *                   与新建时留空的含义一致（登录时回落到 ChatSession.cpp 的编译期
     *                   兜底值）。控制台编辑弹窗会预填当前值，传来空串说明是有意清空
     *    releaseNote：  新的更新说明；<u>空串是合法值</u>，表示把说明清空
     *    forced：       1 强制升级、0 不强制；取 CLIENT_VERSION_FORCED_KEEP 表示不修改
     *                   该列（请求里没带 forced 字段的老调用方）
     *    outErrorCode： [出参] 失败原因；成功时置 error_code_ok
     *
     *  @return true 修改成功；记录不存在返回 false 且 outErrorCode 为 error_code_not_found。
     *
     *  <u>只允许改这三项</u>：版本号与升级包三件套（MD5 / 大小 / 文件名）一旦发布出去
     *  就可能已被客户端下载，原地改掉会让"同一个版本号对应两份内容"，排查时无从分辨。
     *  要换包请新建一条记录。
     */
    bool updateVersion(int64_t id, const std::string& minVersion,
                       const std::string& releaseNote, int32_t forced, int& outErrorCode);

    /**
     *  发布或撤回一条版本记录。
     *
     *    id：           记录ID
     *    enabled：      true 发布、false 撤回
     *    nowMs：        当前时刻，Unix 毫秒。<u>发布时会把 f_publish_time 刷成该值</u>
     *    outErrorCode： [出参] 失败原因；成功时置 error_code_ok
     *
     *  @return true 操作成功；记录不存在返回 false 且 outErrorCode 为 error_code_not_found。
     *
     *  发布会把 f_publish_time 刷到当前，于是该条立刻成为取数规则下的"最新发布"。
     *  把某个旧版本重新顶成当前生效，就靠这一点：先撤回、再发布即可，不需要另外的
     *  "回滚"动作。控制台早先有过一个「回滚」按钮，与发布在数据层是同一件事，
     *  只是审计动作名不同；2026-08-13 按用户要求去掉了，理由是那个叫法容易让人以为
     *  能把<u>已经升级完的客户端</u>降回去，而实际上它只影响尚未升级的客户端。
     *
     *  撤回<u>不改动</u> f_publish_time：该列在 f_enabled 为 0 时本就不参与取数，
     *  留着它反而能看出这条记录上次是什么时候放出去的。
     */
    bool setEnabled(int64_t id, bool enabled, int64_t nowMs, int& outErrorCode);
};
