/**
 *  企业部门管理器，DepartmentManager.h
 *  balloonwj@qq.com   2026.05.21
 *
 *  管理企业组织架构（部门树 + 成员归属）。初始数据从数据库 t_department 与
 *  t_department_member 加载并缓存于内存；增删改移操作一律先写库再刷新缓存。
 *  物化路径的计算复用 dept::DepartmentPath。本类设计参照 UserManager。
 */
#ifndef __DEPARTMENT_MANAGER_H__
#define __DEPARTMENT_MANAGER_H__

#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include "DepartmentMsg.h"

//部门业务ID的起始基数。
//userid 取值 (0, 0x0FFFFFFF)，groupid 取值 [0x0FFFFFFF, ...)（见 UserManager.h）。
//部门ID自 0x60000000 起递增：既明显区别于 userid/groupid，又落在 32 位正整数范围内，
//避免与协议中其它整型ID混淆，同时为群组保留了约 13 亿（0x0FFFFFFF~0x5FFFFFFF）的增长空间。
#define DEPARTMENT_ID_BASE   0x60000000

//前向声明：数据库连接类，仅在 .cpp 中需要其完整定义。
class IDatabase;

//部门详情（admin 控制台 dept/detail.do 的输出）。
//在基础部门字段之外，额外给出负责人姓名与三类成员/子部门计数。
struct DepartmentDetail
{
    DepartmentDetail()
        : deptid(0), enterpriseid(0), parentdeptid(0), deptlevel(1), deptorder(0)
        , leaderuserid(0), directMembers(0), subDepts(0), totalMembers(0)
    {
    }

    int64_t     deptid;         //部门业务ID
    int64_t     enterpriseid;   //所属企业ID
    std::string deptname;       //部门名称
    int64_t     parentdeptid;   //父部门ID，0表示顶级部门
    int32_t     deptlevel;      //部门层级，顶级为1
    int32_t     deptorder;      //同级排序权重，值小者靠前
    int64_t     leaderuserid;   //部门负责人userid，0表示未指定
    std::string leadername;     //部门负责人姓名（取 t_user.f_nickname；无负责人时为空）
    std::string deptdesc;       //部门描述
    int32_t     directMembers;  //本部门直属在职成员数
    int32_t     subDepts;       //直接在用子部门数
    int32_t     totalMembers;   //本部门连同其全部子部门的在职成员去重合计
};

//企业部门管理器（服务器级单例，经 Singleton<DepartmentManager> 使用）。
class DepartmentManager final
{
public:
    DepartmentManager();
    ~DepartmentManager();

    DepartmentManager(const DepartmentManager& rhs) = delete;
    DepartmentManager& operator=(const DepartmentManager& rhs) = delete;

    /**
     *  初始化：从库中加载整棵组织架构到缓存，并确定部门ID分配基数。
     *  @return true 表示加载成功。
     */
    bool init();

    /**
     *  取出指定企业当前缓存的组织架构（仅正常部门与在职成员）。
     *  @param enterpriseId 企业ID；只返回该企业的部门，实现租户隔离。
     *  @param outDepts     输出的部门数组（扁平，含各部门内嵌成员；成员的在线状态字段未填充，
     *                      需由调用方在应答时另行补齐）。
     */
    void getOrgStructure(int64_t enterpriseId, std::vector<dept::DepartmentInfo>& outDepts);

    /**
     *  就地更新组织架构缓存里某用户（可能挂在多个部门）的头像字段。某用户改头像后
     *  调用，使后续 getOrgStructure（cmd_get_contacts 数据源）立即返回新头像，避免缓存
     *  陈旧导致同事端重登仍看到旧头像。仅改内存缓存，不写库（库已由调用方更新）。
     *  @param userId     被改头像的用户ID。
     *  @param facetype   新头像类型。
     *  @param customface 新头像标识（UTF-8）。
     */
    void updateCachedMemberAvatar(int64_t userId, int32_t facetype, const std::string& customface);

    /**
     *  就地更新组织架构缓存里某用户在其<u>主部门</u>的职务字段。管理控制台改员工资料
     *  （user/update.do）时，职务是直写 t_department_member 的，不经本类的写方法，
     *  缓存不会自行重载；不同步这一份，客户端收到组织架构变更通知后重拉通讯录
     *  （数据源正是 getOrgStructure）拿到的仍是旧职务。仅改内存缓存，不写库
     *  （库已由调用方 EmployeeManager::updateEmployee 更新）。
     *
     *  只改主部门（isprimary=1）那条记录，与 updateEmployee 的 SQL 条件保持一致：
     *  职务是"这个人在主部门里担任什么"，兼属部门的职务由部门成员管理另行维护，
     *  不能被这里一并覆盖。
     *  @param userId   被改职务的用户ID。
     *  @param position 新职务（UTF-8）；允许为空串，表示清空职务。
     */
    void updateCachedMemberPosition(int64_t userId, const std::string& position);

    /**
     *  把某用户从组织架构缓存的<u>所有</u>部门成员列表里就地移除。管理控制台删除员工
     *  （user/delete.do）时，其部门成员记录是被 EmployeeManager 直接 DELETE 掉的、
     *  不经本类的写方法，缓存不会自行重载；不同步这一份，被删的人会继续留在
     *  tree.do 与客户端通讯录里，看上去就像"删不掉"。更糟的是此时 UserManager
     *  那边已经把他的资料删干净了，客户端在部门里看得见这个 userid、却查不到对应
     *  资料，只能回退显示成"用户<userid>"。仅改内存缓存，不写库（库已由调用方删除）。
     *
     *  与 updateCachedMemberPosition 不同，这里<u>不</u>限定主部门：人都删了，兼属的
     *  部门也不该再留着他。
     *  @param userId 被删除的用户ID。
     */
    void removeCachedMember(int64_t userId);

    /**
     *  把某企业的<u>整棵组织架构</u>从缓存里就地移除，并清掉该企业名下的用户→企业
     *  映射。管理控制台删除企业（enterprise/delete.do）时，其部门与部门成员记录
     *  是被 EnterpriseManager 直接批量 DELETE 的、不经本类的写方法，两份缓存都不会
     *  自行刷新；不同步的话，被删企业的部门树会继续留在 tree.do 与在线客户端的
     *  通讯录里，症状与删单个员工时"删不掉"一样，只是范围扩大到整个企业。
     *
     *  注意删企业只解除员工的企业归属（f_enterprise_id 清零）、并不删员工账号，
     *  所以这里要把 m_userEnterpriseCache 里指向该企业的条目一并清掉，否则
     *  getUserEnterpriseId 会继续返回这个已经不存在的企业ID。
     *
     *  仅改内存缓存，不写库（库已由调用方 EnterpriseManager::deleteEnterprise 处理）。
     *  @param enterpriseId 被删除的企业ID。
     */
    void removeCachedEnterprise(int64_t enterpriseId);

    /**
     *  查询某用户所属的企业ID。供处理器把会话用户解析为企业维度（典型：在线状态变更时
     *  按企业广播，须对每个在线会话查其企业，频次较高）。
     *
     *  带<u>内存缓存</u>：命中直接返回，未命中才查库并缓存正值（f_enterprise_id 对一个用户
     *  基本不变——建号时定、删号时消，不支持改企业）。缓存只存正值、不存 0/未命中，避免
     *  "用户尚未建好时查到 0 被永久缓存"。组织架构缓存重载（loadFromDb，发生在部门/成员写
     *  操作后）时整张企业缓存会被清空，从而自愈任何组织变更。
     *  @param userId 用户ID。
     *  @return 该用户的 f_enterprise_id；用户不存在时返回 0。
     */
    int64_t getUserEnterpriseId(int64_t userId);

    /**
     *  新建部门。
     *  @param enterpriseId 新部门所属企业ID（取操作者所属企业）。
     *  @param req          部门操作请求，使用其 parentdeptid/deptname/deptdesc/leaderuserid/deptorder。
     *  @param outDeptId    成功时输出服务器分配的新部门ID。
     *  @return 错误码，0 表示成功，取值见 AdminApiCodes.h 的 enum error_code。
     *
     *  关于 parentdeptid 为 0：整棵组织架构树的唯一根是"公司根部门"，业务上不再有
     *  第二个顶级部门。因此 parentdeptid 传 0 时，本方法会把新部门自动挂到该企业的
     *  公司根部门之下，而不是再造一个顶级部门。仅当该企业尚未建立公司根部门（存量
     *  数据迁移之前）才退回旧行为、建成顶级部门，并记一条告警日志。
     */
    int32_t createDepartment(int64_t enterpriseId, const dept::OperateDepartmentReq& req, int64_t& outDeptId);

    /**
     *  为某企业创建"公司根部门"——组织架构树上代表公司本身的那一层。
     *  与 createDepartment 的区别在于本方法显式建根：父部门固定为 0、层级固定为 1，
     *  且不做"parentdeptid 为 0 时重定向到根部门"的处理，否则新建企业时会因根部门
     *  尚不存在而无法自举。仅供 EnterpriseManager::createEnterprise 与数据修复流程调用。
     *  @param enterpriseId 企业业务ID。
     *  @param deptName     根部门名称，取企业全称。
     *  @param outDeptId    成功时输出新建的根部门ID。
     *  @return 错误码，0 表示成功。
     *
     *  注意：本方法不会把新建的根部门ID回写到 t_enterprise.f_root_dept_id，回写由
     *  调用方负责（EnterpriseManager 才是 t_enterprise 的属主）。
     */
    int32_t createEnterpriseRootDepartment(int64_t enterpriseId, const std::string& deptName,
                                           int64_t& outDeptId);

    /**
     *  只改部门名称，不触碰描述/负责人/排序等其它字段。
     *  供 EnterpriseManager 在企业改名时同步公司根部门的名字使用——若改用
     *  updateDepartment，会把未随请求传入的描述与负责人一并清空。
     *  @param deptId  目标部门ID。
     *  @param newName 新的部门名称，空串视为非法。
     *  @return 错误码，0 表示成功；部门不存在或已停用返回 error_code_dept_notexist。
     */
    int32_t renameDepartment(int64_t deptId, const std::string& newName);

    /**
     *  修改部门的名称/描述/负责人/排序。
     *  @param req 部门操作请求，使用其 deptid/deptname/deptdesc/leaderuserid/deptorder。
     *  @return 错误码，0 表示成功。
     */
    int32_t updateDepartment(const dept::OperateDepartmentReq& req);

    /**
     *  删除（停用）部门；要求其下无在用子部门与在职成员。
     *  @param deptId 被删除部门ID。
     *  @return 错误码，0 表示成功；112 表示其下仍有子部门或成员；
     *          error_code_dept_isroot 表示该部门是公司根部门，不允许删除。
     *
     *  公司根部门代表企业本身，删掉它整棵组织架构树就没有根了，因此一律拒绝；
     *  企业本身要删除时走 EnterpriseManager::deleteEnterprise 的级联删除。
     */
    int32_t deleteDepartment(int64_t deptId);

    /**
     *  移动部门到新父部门之下，并同步改写整棵子树的物化路径与层级。
     *  @param deptId          被移动部门ID。
     *  @param newParentDeptId 新父部门ID；为 0 表示移动到该企业的公司根部门之下
     *                         （企业尚无公司根部门时才退回旧含义"移动到顶层"）。
     *  @return 错误码，0 表示成功；113 表示新父部门非法或会形成环；
     *          error_code_dept_isroot 表示被移动的是公司根部门，不允许移动。
     */
    int32_t moveDepartment(int64_t deptId, int64_t newParentDeptId);

    /**
     *  查询部门详情：基础字段 + 负责人姓名 + 直属成员数/子部门数/子树成员数。
     *  不读写内存缓存，按请求直查数据库。
     *  @param deptId 目标部门ID。
     *  @param out    输出的详情，仅在返回 true 时有效。
     *  @return true 表示部门存在且在用；false 表示部门不存在或已停用。
     */
    bool getDepartmentDetail(int64_t deptId, DepartmentDetail& out);

    /**
     *  按给定顺序批量重排同级部门的排序权重 f_dept_order。
     *  数组下标即新的排序值（0、1、2 ……），值小者靠前。
     *  @param enterpriseId 租户过滤：非0时只重排该企业的部门；0表示不按企业过滤。
     *  @param parentDeptId 这批同级部门的父部门ID；0表示顶级部门。
     *  @param orderedIds   按目标先后顺序排列的部门ID数组。
     *  @return 错误码，0 表示成功，取值见 AdminApiCodes.h 的 enum error_code。
     */
    int32_t reorderDepartments(int64_t enterpriseId, int64_t parentDeptId,
                               const std::vector<int64_t>& orderedIds);

    /**
     *  查询某部门所属企业ID，供 admin 控制台做跨租户越权校验。
     *  @param deptId 部门ID。
     *  @return 该部门的 f_enterprise_id；部门不存在返回 0。
     */
    int64_t getDepartmentEnterpriseId(int64_t deptId);

    /**
     *  添加成员到部门。
     *  @param req 成员操作请求，使用其 deptid/userid/position/isprimary/memberorder。
     *  @return 错误码，0 表示成功；114 表示该用户已在此部门。
     */
    int32_t addMember(const dept::OperateDeptMemberReq& req);

    /**
     *  修改成员的职位/是否主部门/排序。
     *  @param req 成员操作请求，使用其 deptid/userid/position/isprimary/memberorder。
     *  @return 错误码，0 表示成功；115 表示该用户不在此部门。
     */
    int32_t updateMember(const dept::OperateDeptMemberReq& req);

    /**
     *  从部门移除（软删除）成员。
     *  @param deptId 部门ID。
     *  @param userId 被移除的用户ID。
     *  @return 错误码，0 表示成功；115 表示该用户不在此部门。
     */
    int32_t removeMember(int64_t deptId, int64_t userId);

    /**
     *  把成员从一个部门移动到另一个部门，迁移时保留其职位等归属属性。
     *  @param deptId    源部门ID。
     *  @param userId    被移动的用户ID。
     *  @param newDeptId 目标部门ID。
     *  @return 错误码，0 表示成功。
     */
    int32_t moveMember(int64_t deptId, int64_t userId, int64_t newDeptId);

private:
    //从数据库重新加载整棵组织架构到 m_cachedDepts。调用方必须已持有 m_mutex。
    bool loadFromDb();

    //创建一个已 initialize 的数据库连接；失败返回 NULL，成功时所有权归调用方。
    IDatabase* createDbConnection();

    //查询某正常部门的物化路径、层级、父部门ID；部门不存在或已停用返回 false。
    bool queryDept(IDatabase* pConn, int64_t deptId,
                   std::string& outPath, int32_t& outLevel, int64_t& outParentId);

    //执行一条返回单个计数值的 SQL（形如 SELECT COUNT(*) ...）；查询失败返回 -1。
    int32_t queryCount(IDatabase* pConn, const char* sql);

    //判断某企业 parentDeptId 之下是否已存在名为 name 的正常部门；excludeDeptId 用于改名时排除自身。
    bool isSiblingNameExist(IDatabase* pConn, int64_t enterpriseId, int64_t parentDeptId,
                            const std::string& name, int64_t excludeDeptId);

    //查询某部门所属企业ID；部门不存在返回 0。
    int64_t queryDeptEnterpriseId(IDatabase* pConn, int64_t deptId);

    //判断 userId 是否为 deptId 的在职成员。
    bool isActiveMember(IDatabase* pConn, int64_t deptId, int64_t userId);

private:
    std::vector<dept::DepartmentInfo> m_cachedDepts;   //缓存的整棵组织架构（仅正常部门、在职成员）
    std::mutex                        m_mutex;         //保护 m_cachedDepts 与各写操作，串行化并发请求
    std::atomic<int64_t>              m_baseDeptId;    //部门ID分配基数，取库中最大 f_dept_id

    //userid → enterpriseid 内存缓存（仅缓存查到的正值），用单独的锁，避免与组织架构读写
    //（m_mutex）相互阻塞。getUserEnterpriseId 命中即返、未命中查库后回填；loadFromDb 重载
    //组织架构时整张清空。锁序固定为 m_mutex→m_userEnterpriseCacheMutex（getUserEnterpriseId
    //只取后者、不取 m_mutex），不会反向加锁，无死锁。
    std::map<int64_t, int64_t>        m_userEnterpriseCache;
    std::mutex                        m_userEnterpriseCacheMutex;
};

#endif // __DEPARTMENT_MANAGER_H__
