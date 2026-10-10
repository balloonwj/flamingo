/**
 *  企业租户管理器，EnterpriseManager.h
 *  balloonwj@qq.com   2026.05.21
 *
 *  管理 t_enterprise：企业的增删改查、状态变更、CSV 导出、级联删除。
 *  新建企业时通过 AdminManager 连带创建其初始管理员账号，并通过 DepartmentManager
 *  连带创建"公司根部门"——一个 f_parent_dept_id 为 0、以企业全称命名的真实部门行，
 *  代表组织架构树上"公司本身"这一层，员工可直接挂在其下。企业改名时其名字同步更新。
 */
#ifndef __ENTERPRISE_MANAGER_H__
#define __ENTERPRISE_MANAGER_H__

#include <stdint.h>
#include <string>
#include <vector>
#include <mutex>

//前向声明：数据库连接类，仅在 .cpp 中需要其完整定义。
class IDatabase;

//默认企业全称的内置值：chatserver.conf 没有 defaultenterprise 这一项、或值为空白时使用。
//chatserver 启动时调用 EnterpriseManager::ensureDefaultEnterprise 保证库里有这家企业；PC 客户端
//注册窗「公司名称」的默认项取自它的 flamingo.ini 的 [ui] defaultcompany。两边各自配置，须由
//部署的人保证配成同一个值（2026-10-04 之前两边都写死为本值）。
#define DEFAULT_ENTERPRISE_NAME   "FlamingoIM.org"

//企业状态（t_enterprise.f_status）。管理控制台的「停用 / 启用企业」改的就是它，
//IM 侧登录时据此决定放不放行 —— 只有「活跃」的企业其员工才能登录。
enum enterprise_status
{
    //待审核：企业刚建出来、尚未通过审核。其员工不允许登录 IM；自助注册时也
    //不允许选这类企业加入（见 error_code_enterprise_disabled 的注释）。
    kEnterpriseStatusPending  = 0,
    //活跃：正常营业中，唯一允许员工登录 IM 的状态。
    kEnterpriseStatusActive   = 1,
    //已停用：被平台管理员停服。其员工会被立即踢下线，且不允许再登录。
    kEnterpriseStatusDisabled = 2
};

//企业信息（用于列表/详情的输出，也用作新建/修改的输入载体）。
struct EnterpriseInfo
{
    EnterpriseInfo() : enterpriseId(0), plan(0), status(0), rootDeptId(0), memberCount(0), deptCount(0) {}

    int64_t     enterpriseId;       //企业业务ID
    std::string name;               //企业全称
    std::string shortName;          //企业简称
    std::string industry;           //所属行业
    std::string region;             //所在地区
    int32_t     plan;               //套餐：0=基础版 1=专业版 2=企业版
    int32_t     status;             //状态：0=待审核 1=活跃 2=已停用
    int64_t     rootDeptId;         //公司根部门ID，关联 t_department.f_dept_id；0 表示尚未建立（存量数据迁移前）
    std::string adminName;          //主管理员姓名
    std::string adminEmail;         //主管理员邮箱
    std::string logoColor;          //企业Logo底色
    std::string openDate;           //开通日期
    std::string licenseExpireTime;  //授权到期时间
    std::string createTime;         //创建时间
    int32_t     memberCount;        //员工数（列表/详情时填充）
    int32_t     deptCount;          //部门数（列表/详情时填充）
    std::string accountPrefix;      //新建账号的前缀（t_enterprise.f_account_prefix，大写）；为空时用 EMP。
                                    //只在读企业时填充；改它走 setAccountPrefix，updateEnterprise 不碰它
};

//企业列表与导出的筛选条件（enterprise/list.do、export.do 共用）。各项未设置时不参与筛选。
struct EnterpriseListFilter
{
    EnterpriseListFilter() : status(-1), plan(-1) {}

    std::string keyword;   //名称/简称/行业模糊关键字；空串表示不过滤
    int32_t     status;    //企业状态 0 待审核 / 1 活跃 / 2 停用；-1 表示不过滤
    std::string industry;  //行业，与 t_enterprise.f_industry 精确匹配；空串表示不过滤
    int32_t     plan;      //套餐编号（t_enterprise.f_plan）；-1 表示不过滤
    std::string openFrom;  //开通日期下限「YYYY-MM-DD」，当天包含在内；空串或格式不对时不过滤
    std::string openTo;    //开通日期上限「YYYY-MM-DD」，当天包含在内；空串或格式不对时不过滤
};

//企业租户管理器（服务器级单例，经 Singleton<EnterpriseManager> 使用）。
class EnterpriseManager final
{
public:
    EnterpriseManager();
    ~EnterpriseManager();

    EnterpriseManager(const EnterpriseManager& rhs) = delete;
    EnterpriseManager& operator=(const EnterpriseManager& rhs) = delete;

    //初始化。连接参数统一由 dbapi/DbConfig 保管，本函数不再收参。
    bool init();

    /**
     *  按筛选条件分页查询企业列表（enterprise/list.do 使用）。
     *  @param filter   筛选条件，未设置的项不参与筛选，见 EnterpriseListFilter。
     *  @param page     页码，从 1 起。
     *  @param pageSize 每页条数。
     *  @param outTotal 输出满足条件的总条数。
     *  @param outList  输出当前页的企业列表（含员工数、部门数）。
     */
    void listEnterprises(const EnterpriseListFilter& filter, int32_t page, int32_t pageSize,
                         int32_t& outTotal, std::vector<EnterpriseInfo>& outList);

    /**
     *  分页查询企业列表（只按关键字与状态筛选的旧版本，转调上面的按条件版本）。
     *  @param keyword  名称/简称/行业模糊关键字，空串表示不过滤。
     *  @param status   状态过滤；传 -1 表示不过滤。
     *  @param page     页码，从 1 起。
     *  @param pageSize 每页条数。
     *  @param outTotal 输出满足条件的总条数。
     *  @param outList  输出当前页的企业列表（含员工数、部门数）。
     */
    void listEnterprises(const std::string& keyword, int32_t status, int32_t page, int32_t pageSize,
                         int32_t& outTotal, std::vector<EnterpriseInfo>& outList);

    //取单个企业详情；企业不存在返回 false。
    bool getEnterprise(int64_t enterpriseId, EnterpriseInfo& outInfo);

    /**
     *  按企业全称精确查找企业。供 PC 客户端注册流程使用：用户手填公司全称，
     *  服务端据此定位到唯一一家企业，再把新账号挂进它的公司根部门。
     *
     *  @param name    企业全称，须与 t_enterprise.f_name 完全一致。调用方应先
     *                 去掉首尾空白；本函数不做任何模糊匹配、不匹配企业简称。
     *  @param outInfo [出参] 命中时填入企业信息。为避免注册路径上多打两条统计
     *                 查询，本函数**不**填充 memberCount / deptCount，两者保持
     *                 构造时的 0；需要统计数请改用 getEnterprise。
     *  @return 命中返回 true；name 为空串或库中无此名字返回 false。
     *
     *  依赖 t_enterprise.f_name 上的唯一约束 uniq_enterprise_name（见 table_mysql.sql）。万一存量库尚未建立
     *  该约束而出现重名，本函数取 f_enterprise_id 最小的那一家并记警告日志。
     *
     *  与 listEnterprises / getEnterprise 一致，读操作不加锁。
     */
    bool getEnterpriseByName(const std::string& name, EnterpriseInfo& outInfo);

    /**
     *  取某企业的公司根部门ID。
     *  @param enterpriseId 企业业务ID。
     *  @return 该企业的公司根部门ID；企业不存在、或其根部门尚未建立（存量数据
     *          迁移前）时返回 0，调用方须自行处理这一情形。
     *
     *  重要：本方法是只读查询，实现上"不得"持有 m_mutex，理由是它会被
     *  DepartmentManager 在其自身锁内调用；而 createEnterprise 又会在 m_mutex
     *  内调用 DepartmentManager。两者若都加锁将构成加锁顺序反转，导致死锁。
     *  与 listEnterprises / getEnterprise 一样，读操作一律不加锁。
     */
    int64_t getRootDeptId(int64_t enterpriseId);

    /**
     *  新建企业，并连带创建其公司根部门与初始管理员账号。
     *  @param req             企业信息，使用其 name/shortName/industry/region/plan/adminName/adminEmail。
     *  @param outEnterpriseId 输出新企业ID。
     *  @param outAdminId      输出初始管理员ID（管理员创建失败时为 0）。
     *  @param outInitPassword 输出初始管理员的明文密码。
     *  @return 错误码，0 表示企业创建成功。
     *
     *  副作用：会在 t_department 内插入一行以企业全称命名的公司根部门，并把其ID
     *  回写到本企业的 f_root_dept_id。根部门或初始管理员创建失败时不回滚企业本身，
     *  仅记录错误日志，由运维通过校验脚本发现并补救。
     */
    int32_t createEnterprise(const EnterpriseInfo& req, int64_t& outEnterpriseId,
                             int64_t& outAdminId, std::string& outInitPassword);

    /**
     *  保证库里存在指定全称的默认企业，供 chatserver 启动时调用。
     *  @param name       默认企业全称，不得为空串；正式调用传 resolveDefaultEnterpriseName 的结果。
     *  @param outCreated [出参] 本次调用新建了这家企业时置 true；企业原本就存在、或调用
     *                    失败时置 false。
     *  @return 错误码：0 表示调用结束时这家企业存在（无论是本次新建的还是原本就有）；
     *          name 为空、数据库不可用、插入企业记录失败时返回 error_code_unknown。
     *
     *  企业不存在时新建：状态直接为「活跃」、套餐为基础版、开通日期取当前时间，并连带
     *  创建以企业全称命名的公司根部门；不创建企业管理员，这家企业由平台超级管理员在
     *  管理台管理。
     *
     *  企业已存在时不修改它的状态：管理员在管理台把它停用，就关闭了「新用户注册进默认
     *  企业」这条途径，服务重启后也不会被改回活跃。唯一的修补是它缺少公司根部门
     *  （f_root_dept_id 为 0）时补建根部门，否则用户选默认企业注册会收到
     *  error_code_enterprise_no_root_dept。
     *
     *  根部门创建失败只记录错误日志、不影响返回值，与 createEnterprise 的处理一致。
     */
    int32_t ensureDefaultEnterprise(const std::string& name, bool& outCreated);

    /**
     *  由 chatserver.conf 的 defaultenterprise 配置值得出默认企业的全称。
     *  @param configValue 配置值（UTF-8），没有这一项时为 NULL。
     *  @return 去掉首尾空格与制表符后的值；为 NULL 或去掉空白后为空时返回内置的
     *          DEFAULT_ENTERPRISE_NAME。
     */
    static std::string resolveDefaultEnterpriseName(const char* configValue);

    /**
     *  修改企业信息（name/shortName/industry/region/plan）。
     *  @param req 使用其 enterpriseId 定位企业，其余字段为新值。
     *  @return 错误码，0 表示成功。
     *
     *  副作用：企业全称变化时，会把公司根部门的名字同步改成新的全称，保证管理端
     *  组织架构树与 IM 客户端通讯录上显示的公司名始终与企业名一致。根部门改名失败
     *  不影响企业本身的修改结果，仅记录错误日志。
     */
    int32_t updateEnterprise(const EnterpriseInfo& req);

    /**
     *  设置企业的账号前缀（docs/登录账号与手机号登录方案.md 第 3.2 节）。只影响之后新建的账号，已有账号不变。
     *  @param enterpriseId 企业业务ID
     *  @param rawPrefix    管理员填写的原文；先经 NormalizeAccountPrefix 规整（去首尾空白、转大写），
     *                      空串表示清除、改回默认的 EMP
     *  @return 错误码：0 成功；前缀不合法返回 error_code_account_prefix_invalid（不写库）；企业不存在或写库失败
     *          返回 error_code_unknown。取值见 AdminApiCodes.h
     */
    int32_t setAccountPrefix(int64_t enterpriseId, const std::string& rawPrefix);

    /**
     *  取企业的账号前缀，供建号时生成账号。不加锁，与本类其它读操作一致。
     *  @param enterpriseId 企业业务ID；0（无企业归属）或企业不存在时返回空串
     *  @return 前缀（大写）；没设时为空串，调用方按默认的 EMP 生成
     */
    std::string getAccountPrefix(int64_t enterpriseId);

    //变更企业状态（0=待审核 1=活跃 2=停用）。返回错误码。
    int32_t setEnterpriseStatus(int64_t enterpriseId, int32_t status);

    //硬删除企业及其部门/成员关系/管理员/服务号数据；其员工账号置为无企业归属。返回错误码。
    int32_t deleteEnterprise(int64_t enterpriseId);

    //按筛选条件导出企业列表为 CSV 文本（只按关键字与状态筛选的旧版本，转调下面的按条件版本）。
    void exportEnterprisesCsv(const std::string& keyword, int32_t status, std::string& outCsv);

    //按筛选条件导出企业列表为 CSV 文本（enterprise/export.do 使用），条件含义与 listEnterprises 相同。
    void exportEnterprisesCsv(const EnterpriseListFilter& filter, std::string& outCsv);

    /**
     *  判断文字是否为合法的「YYYY-MM-DD」日期：长度 10、分隔符为「-」、其余为数字、月份 1~12、
     *  日 1~31（不按月份核对天数）。开通日期筛选只接受这种格式，其余一律视为未设置。
     *  @param text 待判断的文字。
     *  @return 合法返回 true。
     */
    static bool isValidDateText(const std::string& text);

private:
    //创建一个已 initialize 的数据库连接；失败返回 NULL，成功时所有权归调用方。
    IDatabase* createDbConnection();

    //执行一条返回单个计数值的 SQL（形如 SELECT COUNT(*) ...）；查询失败返回 0。
    int32_t queryCount(IDatabase* pConn, const char* sql);

    //构造列表/导出的 WHERE 子句（关键字、状态、行业、套餐、开通日期区间），表别名固定为 e。
    std::string buildWhere(const EnterpriseListFilter& filter);

    /**
     *  判断某个企业全称是否已被别的企业占用。新建 / 改名前的查重。
     *  @param pConn         已建立的数据库连接，所有权仍归调用方。
     *  @param name          待检查的企业全称。
     *  @param excludeEntId  排除掉的企业业务ID；改名时传自身ID，避免把"名字没变"
     *                       误判成重名。新建时传 0（不排除任何一家）。
     *  @return 已被占用返回 true。
     */
    bool isNameTaken(IDatabase* pConn, const std::string& name, int64_t excludeEntId);

    /**
     *  为企业创建公司根部门，并把根部门ID回写到该企业的 f_root_dept_id。
     *  createEnterprise 与 ensureDefaultEnterprise 共用。调用方须已持有 m_mutex。
     *  @param pConn        已建立的数据库连接，所有权仍归调用方。
     *  @param enterpriseId 企业业务ID，非 0。
     *  @param deptName     根部门名称，取企业全称。
     *  @return 根部门创建且回写成功返回 true；任一步失败返回 false，失败原因已记录错误日志。
     */
    bool attachRootDepartment(IDatabase* pConn, int64_t enterpriseId, const std::string& deptName);

private:
    std::mutex  m_mutex;          //串行化写操作
};

#endif // __ENTERPRISE_MANAGER_H__
