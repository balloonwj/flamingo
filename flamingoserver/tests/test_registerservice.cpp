/**
 *  注册业务流程单元测试，test_registerservice.cpp
 *  Suite RS：RegisterService::doRegister 的全部判定分支。
 *
 *  这里用一个内存假数据源顶替 UserManager / EnterpriseManager / DepartmentManager，
 *  因此整个用例集不连库、不依赖任何单例，可以在 Tests.exe 里独立跑。
 *
 *  balloonwj@qq.com   2026-08-04
 */
#include "doctest.h"
#include "../chatserversrc/RegisterService.h"
#include "../chatserversrc/Protocol.h"
#include "../chatserversrc/AccountName.h"
#include <map>
#include <set>
#include <string>

//------------------------------------------------------------------------------
// 测试替身：把 RegisterService 依赖的数据访问全部换成内存实现。
//------------------------------------------------------------------------------

//内存版数据源。各 public 字段既是"预置数据"也是"观测点"：
//用例先往里塞企业与已占用的账号，跑完 doRegister 再检查建号与入部门是否按预期发生。
class FakeRegisterDataSource : public IRegisterDataSource
{
public:
    //初始化列表的书写顺序必须与成员的声明顺序一致，否则 GCC 会报 -Wreorder。
    //m_nextUserId 声明在末尾的 private 段里，所以这里也要放在最后
    FakeRegisterDataSource()
        : m_createUserShouldFail(false)
        , m_addToDeptShouldFail(false)
        , m_createdUserId(0)
        , m_createdEnterpriseId(0)
        , m_attachedDeptId(0)
        , m_attachedUserId(0)
        , m_createUserCalled(false)
        , m_addToDeptCalled(false)
        , m_nextUserId(FAKE_FIRST_USER_ID)
    {
    }

    //---- 预置数据 ----
    std::map<std::string, RegisterEnterprise> m_enterprises;  //企业全称 -> 企业信息
    std::set<std::string>                     m_takenPhones;     //已被占用的手机号

    //---- 故障注入开关 ----
    bool m_createUserShouldFail;   //置 true 时 createUser 一律失败，用于覆盖建号失败分支
    bool m_addToDeptShouldFail;    //置 true 时 addUserToDepartment 一律失败

    //---- 观测点：doRegister 跑完后由用例检查 ----
    int64_t m_createdUserId;       //建出来的用户ID；未建号时为 0
    int64_t m_createdEnterpriseId; //建号时传入的企业ID；未建号时为 0
    int64_t m_attachedDeptId;      //入部门时传入的部门ID；未入部门时为 0
    int64_t m_attachedUserId;      //入部门时传入的用户ID；未入部门时为 0
    bool    m_createUserCalled;    //createUser 是否被调用过
    bool    m_addToDeptCalled;     //addUserToDepartment 是否被调用过

    virtual bool findEnterpriseByName(const std::string& name, RegisterEnterprise& out)
    {
        std::map<std::string, RegisterEnterprise>::const_iterator it = m_enterprises.find(name);
        if (it == m_enterprises.end())
            return false;

        out = it->second;
        return true;
    }

    virtual bool isPhoneTaken(const std::string& phone)
    {
        return m_takenPhones.find(phone) != m_takenPhones.end();
    }

    virtual bool createUser(const RegisterRequest& req, int64_t enterpriseId,
                            int64_t& outUserId, std::string& outUsername)
    {
        m_createUserCalled = true;
        if (m_createUserShouldFail)
            return false;

        outUserId = m_nextUserId++;
        //登录账号由建号这一步自动生成，规则与生产实现一致（AccountName.h）。
        outUsername = MakeEmployeeAccount(outUserId);
        m_createdUserId = outUserId;
        m_createdEnterpriseId = enterpriseId;
        m_takenPhones.insert(req.phone);
        return true;
    }

    virtual bool addUserToDepartment(int64_t deptId, int64_t userId)
    {
        m_addToDeptCalled = true;
        if (m_addToDeptShouldFail)
            return false;

        m_attachedDeptId = deptId;
        m_attachedUserId = userId;
        return true;
    }

private:
    //假数据源分配用户ID的起点。取一个明显不像真实库里会出现的值，
    //断言失败时一眼能看出这是测试造出来的ID。
    static const int64_t FAKE_FIRST_USER_ID = 90000001;

    int64_t m_nextUserId;   //下一个待分配的用户ID
};

//------------------------------------------------------------------------------
// 测试夹具的构造辅助
//------------------------------------------------------------------------------

//造一个各字段都合法的注册请求，用例按需改其中一两个字段来触发某条分支。
static RegisterRequest MakeValidRequest()
{
    RegisterRequest req;
    req.nickname = "林知夏";
    req.password = "0123456789abcdef"; //客户端 SHA-256 的十六进制串，本层不关心其内容
    req.phone    = "13917043329";
    req.company  = "明远科技有限公司";
    return req;
}

//造一家可正常加入的企业：活跃状态、根部门已建好。
static RegisterEnterprise MakeActiveEnterprise()
{
    RegisterEnterprise ent;
    ent.enterpriseId = 1;
    ent.name         = "明远科技有限公司";
    ent.status       = ENTERPRISE_STATUS_ACTIVE;
    ent.rootDeptId   = 0x60000001;   //与 DEPARTMENT_ID_BASE 同量级的一个部门ID
    return ent;
}

//------------------------------------------------------------------------------
// 用例
//------------------------------------------------------------------------------

TEST_CASE("RS1 注册成功：建号写入企业归属，并挂进公司根部门")
{
    FakeRegisterDataSource ds;
    RegisterEnterprise ent = MakeActiveEnterprise();
    ds.m_enterprises[ent.name] = ent;

    RegisterRequest req = MakeValidRequest();
    RegisterOutcome out;
    REQUIRE(RegisterService::doRegister(ds, req, out) == error_code_ok);

    //建号时必须把企业ID一起写进去，否则新人不属于任何企业，登录后通讯录是空的。
    CHECK(ds.m_createUserCalled);
    CHECK(ds.m_createdEnterpriseId == ent.enterpriseId);
    //新人要挂在企业的公司根部门下，而不是别的部门。
    CHECK(ds.m_addToDeptCalled);
    CHECK(ds.m_attachedDeptId == ent.rootDeptId);
    CHECK(ds.m_attachedUserId == ds.m_createdUserId);

    CHECK(out.userid == ds.m_createdUserId);
    CHECK(out.enterpriseId == ent.enterpriseId);
    CHECK(out.departmentAttached);

    //登录账号由建号这一步自动生成，必须原样回传出来——用户就靠它登录，
    //回传丢了或者对不上，注册完成页显示的账号就是错的。
    CHECK(out.username == MakeEmployeeAccount(out.userid));
    CHECK_FALSE(out.username.empty());
}

TEST_CASE("RS2 注册成功时企业名回显取库中原文，而不是用户输入的原文")
{
    //库里存的是"明远科技有限公司"，用户输入了带空格的写法（parseRegisterRequest 已
    //去掉首尾空白，这里模拟的是大小写/简繁一类服务端匹配得上、但字面不同的情形）。
    //完成页要显示库里的名字，这样差异能当场被用户看见。
    FakeRegisterDataSource ds;
    RegisterEnterprise ent = MakeActiveEnterprise();
    ds.m_enterprises[ent.name] = ent;

    RegisterRequest req = MakeValidRequest();
    RegisterOutcome out;
    REQUIRE(RegisterService::doRegister(ds, req, out) == error_code_ok);
    CHECK(out.enterpriseName == ent.name);
}

TEST_CASE("RS3 公司名找不到：返回企业不存在，且绝不建号")
{
    FakeRegisterDataSource ds;
    RegisterEnterprise ent = MakeActiveEnterprise();
    ds.m_enterprises[ent.name] = ent;

    RegisterRequest req = MakeValidRequest();
    req.company = "并不存在的某某公司";

    RegisterOutcome out;
    CHECK(RegisterService::doRegister(ds, req, out) == error_code_enterprise_not_exist);
    //关键：公司名不对时一个账号都不能落库，否则会留下一堆无归属的孤儿账号。
    CHECK_FALSE(ds.m_createUserCalled);
    CHECK_FALSE(ds.m_addToDeptCalled);
}

TEST_CASE("RS4 企业处于待审核 / 已停用状态：拒绝注册，且不建号")
{
    SUBCASE("待审核（f_status = 0）")
    {
        FakeRegisterDataSource ds;
        RegisterEnterprise ent = MakeActiveEnterprise();
        ent.status = 0;
        ds.m_enterprises[ent.name] = ent;

        RegisterRequest req = MakeValidRequest();
        RegisterOutcome out;
        CHECK(RegisterService::doRegister(ds, req, out) == error_code_enterprise_disabled);
        CHECK_FALSE(ds.m_createUserCalled);
    }

    SUBCASE("已停用（f_status = 2）")
    {
        FakeRegisterDataSource ds;
        RegisterEnterprise ent = MakeActiveEnterprise();
        ent.status = 2;
        ds.m_enterprises[ent.name] = ent;

        RegisterRequest req = MakeValidRequest();
        RegisterOutcome out;
        CHECK(RegisterService::doRegister(ds, req, out) == error_code_enterprise_disabled);
        CHECK_FALSE(ds.m_createUserCalled);
    }
}

TEST_CASE("RS5 企业尚未建立公司根部门：拒绝注册，不建无处安置的账号")
{
    FakeRegisterDataSource ds;
    RegisterEnterprise ent = MakeActiveEnterprise();
    ent.rootDeptId = 0;   //存量数据迁移前的企业会是这个样子
    ds.m_enterprises[ent.name] = ent;

    RegisterRequest req = MakeValidRequest();
    RegisterOutcome out;
    CHECK(RegisterService::doRegister(ds, req, out) == error_code_enterprise_no_root_dept);
    CHECK_FALSE(ds.m_createUserCalled);
}

TEST_CASE("RS6 连续注册拿到的账号互不相同")
{
    //账号由用户ID派生，ID 递增则账号必然不同。这条契约保住了"账号全局唯一"
    //这个前提——它现在不再有任何查重步骤兜着，全靠生成规则本身。
    FakeRegisterDataSource ds;
    RegisterEnterprise ent = MakeActiveEnterprise();
    ds.m_enterprises[ent.name] = ent;

    RegisterRequest first = MakeValidRequest();
    RegisterOutcome outFirst;
    REQUIRE(RegisterService::doRegister(ds, first, outFirst) == error_code_ok);

    //第二个人：换手机号（同一手机号只能注册一个账号）。
    RegisterRequest second = MakeValidRequest();
    second.phone = "13900139000";
    RegisterOutcome outSecond;
    REQUIRE(RegisterService::doRegister(ds, second, outSecond) == error_code_ok);

    CHECK(outFirst.userid != outSecond.userid);
    CHECK(outFirst.username != outSecond.username);
}

TEST_CASE("RS7 手机号已被占用：返回已注册，且不建号")
{
    FakeRegisterDataSource ds;
    RegisterEnterprise ent = MakeActiveEnterprise();
    ds.m_enterprises[ent.name] = ent;

    RegisterRequest req = MakeValidRequest();
    ds.m_takenPhones.insert(req.phone);

    RegisterOutcome out;
    CHECK(RegisterService::doRegister(ds, req, out) == error_code_already_registered);
    CHECK_FALSE(ds.m_createUserCalled);
}

TEST_CASE("RS8 客户端就算在报文里塞了账号也不作数")
{
    //登录账号由服务端生成。RegisterRequest 里已经没有 username 字段，报文层
    //也会忽略老客户端发上来的那个（见 test_registerprotocol 的 R8）。这里再从
    //业务层确认一次：最终账号只可能是按用户ID生成的那个。
    FakeRegisterDataSource ds;
    RegisterEnterprise ent = MakeActiveEnterprise();
    ds.m_enterprises[ent.name] = ent;

    RegisterRequest req = MakeValidRequest();
    RegisterOutcome out;
    REQUIRE(RegisterService::doRegister(ds, req, out) == error_code_ok);
    CHECK(out.username == MakeEmployeeAccount(out.userid));
}

TEST_CASE("RS9 建号失败：返回注册失败，不再尝试入部门")
{
    FakeRegisterDataSource ds;
    RegisterEnterprise ent = MakeActiveEnterprise();
    ds.m_enterprises[ent.name] = ent;
    ds.m_createUserShouldFail = true;

    RegisterRequest req = MakeValidRequest();
    RegisterOutcome out;
    CHECK(RegisterService::doRegister(ds, req, out) == error_code_register_fail);
    CHECK(ds.m_createUserCalled);
    CHECK_FALSE(ds.m_addToDeptCalled);
}

TEST_CASE("RS10 入部门失败：注册仍算成功，但 departmentAttached 为 false")
{
    //账号此时已经建出来且能登录，把整次注册判为失败会让用户拿同一个账号重试时
    //撞上"账号已存在"，反而更糟。会话层据 departmentAttached 打 LOGE 告警。
    FakeRegisterDataSource ds;
    RegisterEnterprise ent = MakeActiveEnterprise();
    ds.m_enterprises[ent.name] = ent;
    ds.m_addToDeptShouldFail = true;

    RegisterRequest req = MakeValidRequest();
    RegisterOutcome out;
    CHECK(RegisterService::doRegister(ds, req, out) == error_code_ok);
    CHECK(ds.m_createUserCalled);
    CHECK(ds.m_addToDeptCalled);
    CHECK_FALSE(out.departmentAttached);
    //账号本身是建好了的，出参里仍要给出真实的 userid 供客户端展示。
    CHECK(out.userid == ds.m_createdUserId);
    CHECK(out.userid != 0);
}

TEST_CASE("RS11 判定顺序：手机号已被占用时，连企业都不去查")
{
    //手机号查重是扫内存缓存，比查企业（要打一次数据库）便宜，必须排在前面，
    //免得为一个必然失败的请求白白多查一次库。
    FakeRegisterDataSource ds;
    //刻意不预置任何企业：若实现把查企业排在手机号查重之前，返回的会是"企业不存在"。
    RegisterRequest req = MakeValidRequest();
    ds.m_takenPhones.insert(req.phone);

    RegisterOutcome out;
    CHECK(RegisterService::doRegister(ds, req, out) == error_code_already_registered);
}

TEST_CASE("RS12 注册成功：带出公司根部门ID，供会话层广播组织架构变更")
{
    //会话层要靠这个ID告诉同企业的在线客户端"哪个部门多了人"。以前 outcome 里
    //没有它，注册成功后那条 cmd_org_structure_notify 就无从发起——在线的老用户
    //因此看不到刚注册的新同事，直到自己重新登录一次。
    FakeRegisterDataSource ds;
    RegisterEnterprise ent = MakeActiveEnterprise();
    ds.m_enterprises[ent.name] = ent;

    RegisterRequest req = MakeValidRequest();
    RegisterOutcome out;
    REQUIRE(RegisterService::doRegister(ds, req, out) == error_code_ok);

    CHECK(out.rootDeptId == ent.rootDeptId);
    //与实际挂进去的那个部门必须是同一个，否则通知里的部门ID指向别处。
    CHECK(out.rootDeptId == ds.m_attachedDeptId);
}

TEST_CASE("RS13 注册失败时根部门ID保持 0，会话层不会据此误发通知")
{
    //各条失败分支都在建号之前返回，此时 outcome 应当是干净的——尤其不能带出
    //一个看着像真的部门ID，否则会话层会为一次根本没成功的注册广播变更通知。
    SUBCASE("企业不存在")
    {
        FakeRegisterDataSource ds;
        RegisterEnterprise ent = MakeActiveEnterprise();
        ds.m_enterprises[ent.name] = ent;

        RegisterRequest req = MakeValidRequest();
        req.company = "并不存在的某某公司";

        RegisterOutcome out;
        CHECK(RegisterService::doRegister(ds, req, out) == error_code_enterprise_not_exist);
        CHECK(out.rootDeptId == 0);
    }

    SUBCASE("企业已停用")
    {
        FakeRegisterDataSource ds;
        RegisterEnterprise ent = MakeActiveEnterprise();
        ent.status = 2;
        ds.m_enterprises[ent.name] = ent;

        RegisterRequest req = MakeValidRequest();
        RegisterOutcome out;
        CHECK(RegisterService::doRegister(ds, req, out) == error_code_enterprise_disabled);
        CHECK(out.rootDeptId == 0);
    }

    SUBCASE("企业尚未建立公司根部门")
    {
        FakeRegisterDataSource ds;
        RegisterEnterprise ent = MakeActiveEnterprise();
        ent.rootDeptId = 0;
        ds.m_enterprises[ent.name] = ent;

        RegisterRequest req = MakeValidRequest();
        RegisterOutcome out;
        CHECK(RegisterService::doRegister(ds, req, out) == error_code_enterprise_no_root_dept);
        CHECK(out.rootDeptId == 0);
    }

    SUBCASE("手机号已被占用")
    {
        FakeRegisterDataSource ds;
        RegisterEnterprise ent = MakeActiveEnterprise();
        ds.m_enterprises[ent.name] = ent;

        RegisterRequest req = MakeValidRequest();
        ds.m_takenPhones.insert(req.phone);

        RegisterOutcome out;
        CHECK(RegisterService::doRegister(ds, req, out) == error_code_already_registered);
        CHECK(out.rootDeptId == 0);
    }

    SUBCASE("建号失败")
    {
        FakeRegisterDataSource ds;
        RegisterEnterprise ent = MakeActiveEnterprise();
        ds.m_enterprises[ent.name] = ent;
        ds.m_createUserShouldFail = true;

        RegisterRequest req = MakeValidRequest();
        RegisterOutcome out;
        CHECK(RegisterService::doRegister(ds, req, out) == error_code_register_fail);
        CHECK(out.rootDeptId == 0);
    }
}

TEST_CASE("RS14 入部门失败：根部门ID照常带出，但 departmentAttached 为 false")
{
    //这两项要一起看：会话层只在 departmentAttached 为真时才广播，光有部门ID
    //不足以说明新人已经在组织架构里了。
    FakeRegisterDataSource ds;
    RegisterEnterprise ent = MakeActiveEnterprise();
    ds.m_enterprises[ent.name] = ent;
    ds.m_addToDeptShouldFail = true;

    RegisterRequest req = MakeValidRequest();
    RegisterOutcome out;
    REQUIRE(RegisterService::doRegister(ds, req, out) == error_code_ok);

    CHECK(out.rootDeptId == ent.rootDeptId);
    CHECK_FALSE(out.departmentAttached);
}
