/**
 *  注册业务流程实现，RegisterService.cpp
 *  balloonwj@qq.com   2026-08-04
 */
#include "RegisterService.h"
#include "Protocol.h"
#include "AccountName.h"   //IsValidPhoneNumber：手机号格式

int32_t RegisterService::doRegister(IRegisterDataSource& ds, const RegisterRequest& req, RegisterOutcome& out)
{
    //登录账号由服务端自动生成，用户填不了也改不了，因此这里不再有
    //"账号长度""账号是否被占用"之类的校验——账号的唯一性由生成时的查重保证
    //（见 AccountName.h），库上还有 uk_f_username 兜底。

    //手机号格式：此前只在客户端检查，服务端补上同一条规则。手机号可用于登录，
    //格式不对的号码进了库就再也登录不上（docs/登录账号与手机号登录方案.md 第 3.4 节）
    if (!IsValidPhoneNumber(req.phone))
    {
        return error_code_bad_request;
    }

    //一个手机号只能注册一个账号。这一步是扫内存缓存，比下面查企业便宜，排在前面。
    if (ds.isPhoneTaken(req.phone))
    {
        return error_code_already_registered;
    }

    //按用户填的公司全称定位企业。找不到就是最常见的那类输入错误——名字打错、
    //或者这家公司压根还没在管理台开通。
    RegisterEnterprise enterprise;
    if (!ds.findEnterpriseByName(req.company, enterprise))
    {
        return error_code_enterprise_not_exist;
    }

    //只有活跃企业允许新人加入：待审核的企业尚未开通，已停用的企业进去也是一片空白。
    if (enterprise.status != ENTERPRISE_STATUS_ACTIVE)
    {
        return error_code_enterprise_disabled;
    }

    //企业关闭了「允许用户自助注册」：新员工只能由管理员在管理台新建，这里拒绝。排在企业状态之后：
    //待审核或已停用的企业照旧返回「企业不可用」，提示更准确
    if (!ds.isRegisterAllowed(enterprise.enterpriseId))
    {
        return error_code_register_closed;
    }

    //公司根部门是新员工的落脚点。存量数据迁移前的企业可能还没有根部门（f_root_dept_id
    //为 0），此时宁可拒绝注册，也不要建出一个不属于任何部门、在通讯录里看不见的账号。
    if (enterprise.rootDeptId == 0)
    {
        return error_code_enterprise_no_root_dept;
    }

    //昵称按新账号将要加入的企业的敏感词库检查（docs/敏感词方案.md 第 6.3 节）。放在企业的
    //各项判定之后：企业不存在或不可用时还无从确定该用哪份词库；放在建号之前：被拦下时
    //不留下账号，用户改了昵称可以用同一个手机号重新注册
    if (ds.hitSensitiveNickname(enterprise.enterpriseId, req))
    {
        return error_code_profile_sensitive_word;
    }

    //建号。登录账号的生成与口令加盐哈希都在数据源实现里完成，本层不接触
    //命名规则、明文与算法，只负责把生成结果传出去。
    int64_t     newUserId = 0;
    std::string newUsername;
    if (!ds.createUser(req, enterprise.enterpriseId, newUserId, newUsername))
    {
        return error_code_register_fail;
    }

    out.userid         = newUserId;
    out.username       = newUsername;
    out.enterpriseId   = enterprise.enterpriseId;
    //回显用库里的企业全称而不是用户输入的原文，这样大小写、空格上的差异能当场看出来。
    out.enterpriseName = enterprise.name;
    //根部门ID带给会话层：注册成功后要向同企业的在线客户端推一条组织架构变更通知，
    //那条通知的"受影响部门"就是新人落脚的这个根部门。上面几条失败分支都在建号之前
    //返回，rootDeptId 保持构造时的 0，会话层不会拿到一个似是而非的部门ID。
    out.rootDeptId     = enterprise.rootDeptId;

    //把新人挂进公司根部门，作为其主部门。失败不改变注册结果（理由见头文件中
    //本函数的注释），只把结果记在 departmentAttached 上交给调用方去打日志。
    out.departmentAttached = ds.addUserToDepartment(enterprise.rootDeptId, newUserId);

    return error_code_ok;
}
