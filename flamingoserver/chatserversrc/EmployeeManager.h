/**
 *  控制台员工管理器，EmployeeManager.h
 *  balloonwj@qq.com   2026.05.21
 *
 *  面向 admin 控制台的「员工/用户」模块：在 t_user 表之上做员工的增删改查、
 *  启停用、改密，并读取 t_login_record / t_user_activity_log 两类历史。
 *  与 IM 侧的 UserManager（带内存缓存、服务好友关系）相互独立：本类按请求
 *  直查数据库、不做内存缓存，设计参照 EnterpriseManager / AdminManager。
 *
 *  缓存一致性：本类直接改 t_user，并在每次增删改后调用 UserManager::reloadUserCache
 *  把变更同步进 IM 侧的用户缓存，故新建/改名/停用/删除对 IM 端即时生效，无需重启。
 */
#ifndef __EMPLOYEE_MANAGER_H__
#define __EMPLOYEE_MANAGER_H__

#include <stdint.h>
#include <string>
#include <vector>
#include <utility>
#include <mutex>
#include <atomic>

//前向声明：数据库连接类，仅在 .cpp 中需要其完整定义。
class IDatabase;

//员工列表项（user/list.do 的输出元素）。
struct EmployeeListItem
{
    EmployeeListItem() : userid(0), role(0), status(0) {}

    int64_t     userid;     //用户ID
    std::string name;       //姓名（取 t_user.f_nickname）
    std::string title;      //职务（取主部门成员记录的 f_position）
    std::string deptname;   //主部门名称
    int32_t     role;       //企业内角色：0=普通成员 1=部门负责人 2=企业管理员
    int32_t     status;     //账号状态：0=启用 1=停用
};

//员工详情（user/detail.do 的输出）。
struct EmployeeDetail
{
    EmployeeDetail()
        : userid(0), enterpriseid(0), deptid(0), role(0), status(0), gender(0), birthday(0)
    {
    }

    int64_t     userid;        //用户ID
    int64_t     enterpriseid;  //所属企业ID
    std::string username;      //登录用户名
    std::string name;          //姓名（f_nickname）
    std::string title;         //职务（主部门 f_position）
    int64_t     deptid;        //主部门ID，0表示未归属任何部门
    std::string deptname;      //主部门名称
    int32_t     role;          //企业内角色：0=普通成员 1=部门负责人 2=企业管理员
    int32_t     status;        //账号状态：0=启用 1=停用
    std::string employeeNo;    //工号
    std::string joinDate;      //入职日期
    std::string landline;      //座机
    std::string workstation;   //工位地址
    std::string phone;         //手机号（f_phonenumber）
    std::string email;         //邮箱（f_mail）
    std::string signature;     //个性签名
    std::string address;       //地址
    int32_t     gender;        //性别：0=未知 1=男 2=女
    int64_t     birthday;      //生日，形如 19900101
    std::string registerTime;  //注册/创建时间
};

//新建员工请求（user/create.do 的输入载体）。
struct CreateEmployeeReq
{
    CreateEmployeeReq() : enterpriseId(0), deptId(0) {}

    int64_t     enterpriseId;  //所属企业ID
    int64_t     deptId;        //初始归属部门ID；0表示暂不归属部门
    std::string name;          //姓名
    std::string title;         //职务（写入部门成员记录的 f_position）
    std::string employeeNo;    //工号；空则由服务器据 userid 生成
    std::string email;         //邮箱
    std::string phone;         //手机号
    std::string joinDate;      //入职日期，形如 2026-05-21；空则不写
};

//修改员工请求（user/update.do 的输入载体）。
struct UpdateEmployeeReq
{
    UpdateEmployeeReq() : userId(0) {}

    int64_t     userId;        //目标用户ID
    std::string name;          //姓名
    std::string title;         //职务（更新主部门成员记录的 f_position）
    std::string employeeNo;    //工号
    std::string joinDate;      //入职日期
    std::string landline;      //座机
    std::string workstation;   //工位地址
    std::string phone;         //手机号
    std::string email;         //邮箱
    std::string signature;     //个性签名
};

//一条登录记录（user/loginrecords.do 应答 records 数组的元素）。
struct LoginRecordItem
{
    LoginRecordItem() : result(0) {}

    std::string ts;          //登录时间
    std::string device;      //设备描述
    std::string ip;          //登录IP
    std::string location;    //登录地理位置
    int32_t     result;      //结果：0=成功 1=失败
    std::string failReason;  //失败原因（result=1时）
};

//一条操作历史（user/activity.do 应答 list 数组的元素）。
struct ActivityItem
{
    ActivityItem() : severity(0) {}

    std::string ts;        //操作时间
    std::string action;    //操作类型
    std::string detail;    //操作详情
    int32_t     severity;  //级别：0=info 1=warn 2=danger
};

//一条权限项及其在某员工身上的取值（user/getpermissions.do 应答元素）。
//13 个权限项与各角色默认值是固定的代码常量；t_user_permission 只存逐人覆盖。
struct PermissionItem
{
    PermissionItem() : roleDefault(false), overridden(false), effective(false) {}

    std::string group;        //权限分组名（通讯/文件与会议/群组/数据）
    std::string key;          //权限项键名，如 msg.external
    std::string label;        //权限项中文名
    bool        roleDefault;  //当前角色下该权限的默认值
    bool        overridden;   //是否存在逐人覆盖（t_user_permission 有记录）
    bool        effective;    //最终生效值：被覆盖时取覆盖值，否则取 roleDefault
};

//控制台员工管理器（服务器级单例，经 Singleton<EmployeeManager> 使用）。
class EmployeeManager final
{
public:
    EmployeeManager();
    ~EmployeeManager();

    EmployeeManager(const EmployeeManager& rhs) = delete;
    EmployeeManager& operator=(const EmployeeManager& rhs) = delete;

    /**
     *  初始化：据库中最大 f_user_id 确定新用户ID分配基数。
     *  @return true 表示初始化成功。
     */
    bool init();

    /**
     *  分页查询员工列表。
     *  @param enterpriseId 所属企业ID过滤；传 0 表示不按企业过滤（平台全量）。
     *  @param deptId       部门过滤：非0时只列该部门的在职成员；0表示不按部门过滤。
     *  @param keyword      姓名/用户名/工号模糊关键字，空串表示不过滤。
     *  @param page         页码，从 1 起。
     *  @param pageSize     每页条数。
     *  @param outTotal     输出满足条件的总条数。
     *  @param outList      输出当前页的员工列表。
     */
    void listEmployees(int64_t enterpriseId, int64_t deptId, const std::string& keyword,
                       int32_t page, int32_t pageSize,
                       int32_t& outTotal, std::vector<EmployeeListItem>& outList);

    //取员工详情；用户不存在返回 false。
    bool getEmployeeDetail(int64_t userId, EmployeeDetail& outDetail);

    /**
     *  新建员工，自动分配用户ID并生成随机初始密码。
     *  仅写 t_user；归属部门由调用方另行通过 DepartmentManager::addMember 处理。
     *  登录账号(f_username)由服务器自动生成、全局唯一；工号(f_employee_no)取
     *  请求里的 employeeNo，可空（空则入库为 NULL）。两者是相互独立的字段。
     *  @param req             新建请求（其中 employeeNo 为工号，非登录账号）。
     *  @param outUserId       输出新用户ID。
     *  @param outInitPassword 输出生成的明文初始密码（供控制台展示给用户）。
     *  @param outUsername     输出自动生成的登录账号（供控制台展示给用户）。
     *  @return 错误码，0 表示成功，取值见 AdminApiCodes.h 的 enum error_code。
     */
    int32_t createEmployee(const CreateEmployeeReq& req, int64_t& outUserId,
                           std::string& outInitPassword, std::string& outUsername);

    /**
     *  修改员工基本信息（t_user 各字段），并同步更新其主部门成员记录的职务。
     *  @param req 修改请求。
     *  @return 错误码，0 表示成功。
     */
    int32_t updateEmployee(const UpdateEmployeeReq& req);

    //启用/停用员工：status 0=启用 1=停用。返回错误码。
    int32_t setEmployeeStatus(int64_t userId, int32_t status);

    /**
     *  删除员工：删除 t_user 行，并清理其部门成员记录与权限覆盖记录。
     *  @param userId 目标用户ID。
     *  @return 错误码，0 表示成功。
     */
    int32_t deleteEmployee(int64_t userId);

    /**
     *  重置员工登录密码为新生成的随机密码。
     *  @param userId         目标用户ID。
     *  @param outNewPassword 输出新的明文密码。
     *  @return 错误码，0 表示成功。
     */
    int32_t resetPassword(int64_t userId, std::string& outNewPassword);

    /**
     *  分页查询某员工的登录历史（t_login_record，按时间倒序）。
     *  @param userId   用户ID。
     *  @param page     页码，从 1 起。
     *  @param pageSize 每页条数。
     *  @param outTotal 输出该用户登录记录总条数。
     *  @param outList  输出当前页的登录记录。
     */
    void listLoginRecords(int64_t userId, int32_t page, int32_t pageSize,
                          int32_t& outTotal, std::vector<LoginRecordItem>& outList);

    /**
     *  分页查询某员工的操作历史（t_user_activity_log，按时间倒序）。
     *  @param userId    用户ID。
     *  @param rangeDays 时间窗口（天）：只取最近 rangeDays 天内的记录；<=0 表示不限。
     *  @param page      页码，从 1 起。
     *  @param pageSize  每页条数。
     *  @param outTotal  输出满足条件的总条数。
     *  @param outList   输出当前页的操作历史。
     */
    void listActivities(int64_t userId, int32_t rangeDays, int32_t page, int32_t pageSize,
                        int32_t& outTotal, std::vector<ActivityItem>& outList);

    /**
     *  查询某员工所属企业ID，供 admin 控制台做跨租户越权校验。
     *  @param userId 用户ID。
     *  @return 该用户的 f_enterprise_id；用户不存在返回 0。
     */
    int64_t getEmployeeEnterpriseId(int64_t userId);

    /**
     *  设置员工的企业内角色。改角色会清空该员工在 t_user_permission 的全部逐人
     *  覆盖，使权限回到新角色的默认值。
     *  @param userId 目标用户ID。
     *  @param role   角色：0=普通成员 1=部门负责人 2=企业管理员。
     *  @return 错误码，0 表示成功。
     */
    int32_t setRole(int64_t userId, int32_t role);

    /**
     *  取员工的角色与完整权限矩阵（13 个固定权限项的默认值/覆盖/生效值）。
     *  @param userId  目标用户ID。
     *  @param outRole 输出该员工当前角色。
     *  @param outList 输出 13 个权限项的取值。
     *  @return true 表示员工存在；false 表示员工不存在。
     */
    bool getPermissions(int64_t userId, int32_t& outRole, std::vector<PermissionItem>& outList);

    /**
     *  整体设置员工的逐人权限覆盖：先清空其全部覆盖，再写入与角色默认值不同的项。
     *  与角色默认相同的提交项会被忽略（它不构成覆盖）。
     *  @param userId 目标用户ID。
     *  @param perms  提交的权限项列表，每项为 (权限键名, 是否授予)。
     *  @return 错误码，0 表示成功。
     */
    int32_t setPermissions(int64_t userId, const std::vector<std::pair<std::string, bool> >& perms);

private:
    //创建一个已 initialize 的数据库连接；失败返回 NULL，成功时所有权归调用方。
    IDatabase* createDbConnection();

    //执行一条返回单个计数值的 SQL（形如 SELECT COUNT(*) ...）；查询失败返回 0。
    int32_t queryCount(IDatabase* pConn, const char* sql);

    //构造 listEmployees 的 WHERE 子句（企业/部门/关键字过滤），t_user 表别名固定为 u。
    std::string buildListWhere(int64_t enterpriseId, int64_t deptId, const std::string& keyword);

    //生成一个长度为 EMPLOYEE_PASSWORD_LENGTH 的随机明文密码（每次调用唯一）。
    std::string generatePassword(int64_t userId);

private:
    //本类<u>不</u>持有用户ID分配基数：全进程唯一的分配入口是
    //UserManager::allocUserId()。历史上这里自持过一份，与 UserManager 的那份
    //都从库中最大ID起算，管理员建员工与用户自助注册并发时会撞ID，已移除。
    std::mutex           m_mutex;       //串行化新建等写操作
};

#endif // __EMPLOYEE_MANAGER_H__
