/**
 *  企业服务号管理器，ServiceAccountManager.h
 *  balloonwj@qq.com   2026.05.21
 *
 *  面向 admin 控制台的「服务号」模块：管理 t_service_account（服务号主表）与
 *  t_service_account_binding（可见范围绑定表）。服务号归属企业，按企业租户隔离。
 *  按请求直查数据库、不做内存缓存，设计参照 EnterpriseManager / EmployeeManager。
 */
#ifndef __SERVICE_ACCOUNT_MANAGER_H__
#define __SERVICE_ACCOUNT_MANAGER_H__

#include <stdint.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

//前向声明：数据库连接类，仅在 .cpp 中需要其完整定义。
class IDatabase;

//服务号类型（t_service_account.f_kind 与 service/save.do 的 kind 字段）。
enum service_account_kind
{
    //通知型：单向推送，员工只读（如人事通知、财务通知）
    service_kind_notification = 0,
    //助手型：对话式，员工可以回复（如 IT 支持、考勤助手）
    service_kind_assistant    = 1,
    //集成型：对接外部系统（如监控告警）。目前与通知型的服务端行为相同，只作分类
    service_kind_integration  = 2,
};

//判断服务号类型取值是否合法（0～2）。service/save.do 据此拒绝未定义的类型，免得库里
//出现控制台和客户端都不认识的值。
inline bool isValidServiceKind(int32_t kind)
{
    return kind == service_kind_notification
        || kind == service_kind_assistant
        || kind == service_kind_integration;
}

//服务号可见范围的一条绑定（t_service_account_binding 的一行）。
struct ServiceBinding
{
    ServiceBinding() : bindType(0), targetId(0) {}

    int32_t bindType;  //绑定类型：0=本企业全员 1=部门 2=员工
    int64_t targetId;  //绑定目标ID（部门ID或用户ID；全员时为0）
};

//服务号信息（list 取摘要、detail 取全量，公用此结构）。
struct ServiceAccountInfo
{
    ServiceAccountInfo()
        : serviceId(0), enterpriseId(0), kind(0), ownerAdminId(0)
        , allowUnsubscribe(1), status(0)
    {
    }

    int64_t     serviceId;        //服务号业务ID
    int64_t     enterpriseId;     //所属企业ID
    std::string name;             //服务号名称
    int32_t     kind;             //类型：取值见 enum service_account_kind（0 通知型 / 1 助手型 / 2 集成型）
    std::string description;      //服务号描述
    std::string avatarColor;      //头像底色
    std::string avatarGlyph;      //头像字形/图标
    int64_t     ownerAdminId;     //所有者管理员ID
    int32_t     allowUnsubscribe; //是否允许员工取消订阅：0=否 1=是
    int32_t     status;           //状态：0=正常 1=停用
    std::string createTime;       //创建时间
    std::vector<ServiceBinding> bindings;  //可见范围绑定（仅 detail 填充）
};

//新建/编辑服务号请求（save.do 的输入载体）。
struct SaveServiceReq
{
    SaveServiceReq() : serviceId(0), enterpriseId(0), kind(0), allowUnsubscribe(1) {}

    int64_t     serviceId;        //服务号ID；0表示新建，非0表示编辑既有服务号
    int64_t     enterpriseId;     //所属企业ID（新建时使用）
    std::string name;             //服务号名称
    int32_t     kind;             //类型：取值见 enum service_account_kind（0 通知型 / 1 助手型 / 2 集成型）
    std::string description;      //服务号描述
    std::string avatarColor;      //头像底色
    int32_t     allowUnsubscribe; //是否允许员工取消订阅：0=否 1=是
};

//企业服务号管理器（服务器级单例，经 Singleton<ServiceAccountManager> 使用）。
class ServiceAccountManager final
{
public:
    ServiceAccountManager();
    ~ServiceAccountManager();

    ServiceAccountManager(const ServiceAccountManager& rhs) = delete;
    ServiceAccountManager& operator=(const ServiceAccountManager& rhs) = delete;

    /**
     *  初始化：据库中最大 f_service_id 确定ID分配基数。
     *  @return true 表示初始化成功。
     */
    bool init();

    /**
     *  列出某企业的全部服务号（按服务号ID升序，不含绑定）。
     *  @param enterpriseId 企业ID。
     *  @param outList      输出服务号列表。
     */
    void listServices(int64_t enterpriseId, std::vector<ServiceAccountInfo>& outList);

    /**
     *  取服务号详情，含可见范围绑定。
     *  @param serviceId 服务号ID。
     *  @param outInfo   输出详情，仅在返回 true 时有效。
     *  @return true 表示服务号存在；false 表示不存在。
     */
    bool getService(int64_t serviceId, ServiceAccountInfo& outInfo);

    /**
     *  新建或编辑服务号。
     *  @param req          服务号内容；serviceId 为 0 表示新建。
     *  @param ownerAdminId 操作管理员ID（新建时记为所有者）。
     *  @param outId        输出服务号ID（新建时为新分配的ID）。
     *  @return 错误码，0 表示成功，取值见 AdminApiCodes.h 的 enum error_code。
     */
    int32_t saveService(const SaveServiceReq& req, int64_t ownerAdminId, int64_t& outId);

    //删除服务号及其全部可见范围绑定。返回错误码。
    int32_t deleteService(int64_t serviceId);

    /**
     *  整体设置服务号的可见范围绑定（先清空旧绑定，再写入新绑定集）。
     *  @param serviceId 服务号ID。
     *  @param bindings  新的绑定集。
     *  @return 错误码，0 表示成功。
     */
    int32_t setBindings(int64_t serviceId, const std::vector<ServiceBinding>& bindings);

    //启用/停用服务号：status 0=正常 1=停用。返回错误码。
    int32_t setStatus(int64_t serviceId, int32_t status);

    /**
     *  查询某服务号所属企业ID，供 admin 控制台做跨租户越权校验。
     *  @param serviceId 服务号ID。
     *  @return 该服务号的 f_enterprise_id；服务号不存在返回 0。
     */
    int64_t getServiceEnterpriseId(int64_t serviceId);

private:
    //创建一个已 initialize 的数据库连接；失败返回 NULL，成功时所有权归调用方。
    IDatabase* createDbConnection();

    //执行一条返回单个计数值的 SQL（形如 SELECT COUNT(*) ...）；查询失败返回 0。
    int32_t queryCount(IDatabase* pConn, const char* sql);

    //把某服务号的可见范围绑定整体重写为 bindings（先删后插）。调用方须保证服务号已存在。
    bool replaceBindings(IDatabase* pConn, int64_t serviceId,
                         const std::vector<ServiceBinding>& bindings);

private:
    std::mutex           m_mutex;        //串行化新建/编辑/状态变更等写操作
    std::atomic<int64_t> m_baseServiceId;//服务号ID分配基数，取库中最大 f_service_id
};

#endif // __SERVICE_ACCOUNT_MANAGER_H__
