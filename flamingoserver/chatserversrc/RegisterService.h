/**
 *  注册业务流程，RegisterService.h
 *
 *  把 cmd_register 的业务判断从会话层里独立出来：账号长度、账号与手机号查重、
 *  按公司全称定位企业、企业可用性、公司根部门是否就绪、建号、把新人挂进公司
 *  根部门，全部集中在 RegisterService::doRegister 一处。
 *
 *  之所以要单独拆一层，是为了让这条流程能被单元测试覆盖：所有对数据库与各
 *  Manager 单例的访问都收敛到 IRegisterDataSource 这一个纯虚接口之后，测试注入
 *  一个内存假实现就能跑遍全部分支，不必连库（见 tests/test_registerservice.cpp）。
 *  生产代码由 ChatSession 里的适配器把该接口接到 UserManager / EnterpriseManager /
 *  DepartmentManager 上。
 *
 *  典型用法：
 *      ChatSessionRegisterDataSource ds;          //会话层的适配器
 *      RegisterOutcome outcome;
 *      int32_t code = RegisterService::doRegister(ds, req, outcome);
 *      if (code == 0) { ...用 outcome 组装应答... }
 *
 *  balloonwj@qq.com   2026-08-04
 */
#ifndef __REGISTER_SERVICE_H__
#define __REGISTER_SERVICE_H__

#include <stdint.h>
#include <string>
#include "RegisterProtocol.h"

//企业处于"活跃"状态时 t_enterprise.f_status 的取值（0=待审核 1=活跃 2=已停用，
//取值定义见 table.sql 的 t_enterprise.f_status 注释）。
//只有活跃企业允许新人注册加入：待审核的企业还没开通，已停用的企业进去也看不到东西。
const int32_t ENTERPRISE_STATUS_ACTIVE = 1;

//注册流程用到的企业信息。只保留判定与落库要用的几项，不带员工数/部门数这类统计值，
//免得在注册路径上多打两条统计查询。
struct RegisterEnterprise
{
    RegisterEnterprise()
        : enterpriseId(0), status(0), rootDeptId(0)
    {
    }

    int64_t     enterpriseId;  //企业业务ID，对应 t_enterprise.f_enterprise_id
    std::string name;          //企业全称，取自库中原文（不是用户输入的原文）
    int32_t     status;        //企业状态：0=待审核 1=活跃 2=已停用
    int64_t     rootDeptId;    //公司根部门ID；0 表示尚未建立，此时无处安置新员工
};

//注册成功后的产出，供会话层组装应答与打日志。
struct RegisterOutcome
{
    RegisterOutcome()
        : userid(0), enterpriseId(0), departmentAttached(false)
    {
    }

    int64_t     userid;              //服务器分配的新用户ID
    std::string username;            //服务端自动生成的登录账号（形如 "EMP1024"），需回传给用户
    int64_t     enterpriseId;        //新账号所属企业的业务ID
    std::string enterpriseName;      //企业全称，取自库中原文，供客户端完成页回显
    bool        departmentAttached;  //是否成功挂进公司根部门；false 表示账号建出来了
                                     //但没进组织架构，登录后通讯录为空，需运维补挂
};

/**
 *  注册流程所需的数据访问抽象。
 *
 *  实现方负责一切与库、与各 Manager 单例打交道的动作；RegisterService 只负责
 *  按什么顺序调用它们、以及每一步失败时该返回哪个错误码。所有方法都可能访问
 *  数据库，调用方应假定它们是阻塞的。
 */
class IRegisterDataSource
{
public:
    virtual ~IRegisterDataSource() {}

    /**
     *  按企业全称精确查找企业。
     *  @param name 企业全称，调用方保证已去掉首尾空白、且非空。
     *  @param out  [出参] 命中时填入企业信息。
     *  @return 命中返回 true；库中无此名字返回 false。
     */
    virtual bool findEnterpriseByName(const std::string& name, RegisterEnterprise& out) = 0;

    //判断手机号是否已被占用（一个手机号只能注册一个账号）；已占用返回 true。
    virtual bool isPhoneTaken(const std::string& phone) = 0;

    /**
     *  创建新账号并落库。登录账号由实现方按用户ID自动生成（见 AccountName.h），
     *  口令加盐哈希也由实现方负责，本层不碰明文与算法。
     *  @param req          注册请求，使用其 nickname/password/phone。
     *  @param enterpriseId 新账号所属企业的业务ID，写入 t_user.f_enterprise_id。
     *  @param outUserId    [出参] 服务器分配的新用户ID。
     *  @param outUsername  [出参] 自动生成的登录账号；必须回传，用户要靠它登录。
     *  @return 落库成功返回 true。
     */
    virtual bool createUser(const RegisterRequest& req, int64_t enterpriseId,
                            int64_t& outUserId, std::string& outUsername) = 0;

    /**
     *  把新账号挂进指定部门，作为其主部门。
     *  @param deptId 目标部门ID，注册流程固定传企业的公司根部门ID。
     *                部门所属的企业由实现方自行根据 deptId 反查，不必再传一遍。
     *  @param userId 被挂的用户ID。
     *  @return 成功返回 true。
     */
    virtual bool addUserToDepartment(int64_t deptId, int64_t userId) = 0;
};

//注册业务流程；只有静态方法，不可实例化。
class RegisterService
{
public:
    /**
     *  执行一次注册。
     *  @param ds  数据访问实现，引用语义，所有权归调用方，须在本调用期间保持有效。
     *  @param req 已解析的注册请求；其 company 须已去掉首尾空白（parseRegisterRequest 会做）。
     *  @param out [出参] 仅在返回 0 时有效，填入自动生成的登录账号与企业归属。
     *  @return 0 表示注册成功；否则为 Protocol.h 中 enum error_code 的取值，
     *          可能是 error_code_register_fail / error_code_already_registered /
     *          error_code_enterprise_not_exist / error_code_enterprise_disabled /
     *          error_code_enterprise_no_root_dept。
     *
     *  注意"挂进公司根部门"这一步失败时本函数**仍返回 0**：账号此时已经建出来并
     *  能登录，把整次注册判为失败会让用户拿同一个账号重试时撞上"账号已存在"，反而
     *  更糟。这种情况通过 out.departmentAttached 为 false 告知调用方，由调用方记录
     *  错误日志，事后用 migrations 下的脚本把这类孤儿账号补挂回公司根部门。
     */
    static int32_t doRegister(IRegisterDataSource& ds, const RegisterRequest& req, RegisterOutcome& out);

private:
    RegisterService() = delete;
    ~RegisterService() = delete;
    RegisterService(const RegisterService&) = delete;
    RegisterService& operator=(const RegisterService&) = delete;
};

#endif // __REGISTER_SERVICE_H__
