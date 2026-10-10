/**
 *  员工性别的写入与校验测试，test_employeegender.cpp
 *
 *  admin 控制台的 user/create.do 原先不接受性别，新建员工一律写 0；user/update.do 也没有
 *  性别字段，控制台弹窗里选的性别被丢弃。2026-10-04 起两个接口都接受 gender（0 保密 / 1 男 /
 *  2 女，即 EmployeeManager.h 的 EMPLOYEE_GENDER_*，数值与 Protocol.h 的 enum profile_gender
 *  一致），update.do 不带时保持原值。
 *  本套用例对着临时库真的跑 EmployeeManager 的 SQL：
 *    - EMG1：新建时带性别 2，库里与详情读回都是 2；不带（默认）时为 0；
 *    - EMG2：编辑时改为 1，读回为 1；
 *    - EMG3：编辑时不带性别（EMPLOYEE_GENDER_KEEP），性别保持原值，其它字段照改；
 *    - EMG4：性别 3、-2 等非法值在新建与编辑时都被拒绝，库里不留痕迹。
 *
 *  另有 RSW4（2026-10-04，「允许用户自助注册」开关）：企业关闭该开关后，管理台新建员工（createEmployee）不受影响。
 *
 *  2026-10-06（docs/登录账号与手机号登录方案.md）起新建与编辑员工时手机号必填、全局唯一，上面几条用例
 *  建员工时都带上互不重复的手机号，编辑时带上员工自己的号码。另有：
 *    - LGA7：同一企业两名没填工号的员工先后编辑都成功、库里工号为 NULL；改成本企业别人已用的
 *            工号返回 error_code_registeralready；保留自己原来的工号不算重复；
 *    - LGA9：手机号为空或格式不对返回 error_code_phone_invalid，与其他账号重复返回 error_code_phone_taken，
 *            编辑时保留自己原来的号码不算重复；被拒的请求不建号、不改库。
 *
 *  每个用例先用 testdb::resetDb 把 CDbConfig 指到本套专用的库文件上。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include <stdio.h>
#include <memory>
#include <string>

#include "doctest.h"
#include "test_dbbackend.h"
#include "../chatserversrc/EmployeeManager.h"
#include "../chatserversrc/FeatureSwitchManager.h"   //RSW4：关闭允许用户自助注册
#include "../chatserversrc/AdminApiCodes.h"          //error_code_ok
#include "../dbapi/IDatabase.h"
#include "../dbapi/DbFactory.h"
#include "../base/Singleton.h"

//本套用例专用的临时数据库文件（SQLite 模式），以及 MySQL 模式下会被整个重建的库名
static const char* const EMG_DB_PATH = "flamingo_sqlite_employeegender_test.db";
static const char* const EMG_MYSQL_DB = "flamingo_test_employeegender";

//新建测试员工所属的企业 ID（只用于落库，不参与断言）
static const int64_t EMG_ENTERPRISE_ID = 1;

//按 userid 从库里读 f_gender（绕过所有缓存，确认真的写进了库）；取不到时返回 -100
static const int EMG_GENDER_MISSING = -100;
static int emgReadGender(int64_t userid)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        return EMG_GENDER_MISSING;
    const std::string sql = "SELECT f_gender FROM t_user WHERE f_user_id = " + std::to_string(userid);
    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
        return EMG_GENDER_MISSING;
    int value = EMG_GENDER_MISSING;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        value = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;
    return value;
}

//t_user 当前的行数，用于确认被拒绝的新建请求没有留下半条记录
static int emgCountUsers()
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        return -1;
    QueryResult* pResult = pConn->query("SELECT COUNT(*) FROM t_user");
    if (pResult == NULL)
        return -1;
    int count = -1;
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        count = pRow[0].getInt32();
    pResult->endQuery();
    delete pResult;
    return count;
}

//生成一个本进程里不重复的合法手机号（139 + 8 位序号）。用户缓存是进程级单例、换库时不清空，
//所以序号在各用例之间持续递增，前面用例建过的号码不会被后面的当成重复
static std::string emgNextPhone()
{
    static int s_seq = 0;
    char buf[16] = { 0 };
    snprintf(buf, sizeof(buf), "139%08d", ++s_seq);
    return std::string(buf);
}

//按 userid 读员工详情里的手机号；取不到时返回空串
static std::string emgPhoneOf(int64_t userId)
{
    EmployeeDetail detail;
    if (!Singleton<EmployeeManager>::Instance().getEmployeeDetail(userId, detail))
        return std::string();
    return detail.phone;
}

//新建一名不归属任何部门的员工，返回其 userid；失败时让用例停下
//  name：姓名；gender：性别取值
static int64_t emgCreate(const char* name, int32_t gender)
{
    CreateEmployeeReq req;
    req.enterpriseId = EMG_ENTERPRISE_ID;
    req.deptId = 0;
    req.name = name;
    req.email = "emg@test.com";
    req.phone = emgNextPhone();
    req.gender = gender;
    int64_t userId = 0;
    std::string initPassword;
    std::string username;
    REQUIRE(Singleton<EmployeeManager>::Instance().createEmployee(req, userId, initPassword, username)
            == error_code_ok);
    REQUIRE(userId > 0);
    return userId;
}

//构造一份编辑请求：姓名与签名必填，性别按参数给，手机号保留员工自己原来的号码
//  userId：目标员工；gender：性别取值或 EMPLOYEE_GENDER_KEEP；signature：个性签名
static UpdateEmployeeReq emgUpdateReq(int64_t userId, int32_t gender, const char* signature)
{
    UpdateEmployeeReq req;
    req.userId = userId;
    req.name = "张三";
    req.signature = signature;
    req.gender = gender;
    req.phone = emgPhoneOf(userId);
    return req;
}

TEST_CASE("EMG1 新建员工时写入性别：带 2 则库里与详情都是 2，默认为 0")
{
    testdb::resetDb(EMG_DB_PATH, EMG_MYSQL_DB);

    const int64_t female = emgCreate("李四", EMPLOYEE_GENDER_FEMALE);
    CHECK(emgReadGender(female) == EMPLOYEE_GENDER_FEMALE);

    EmployeeDetail detail;
    REQUIRE(Singleton<EmployeeManager>::Instance().getEmployeeDetail(female, detail));
    CHECK(detail.gender == EMPLOYEE_GENDER_FEMALE);

    //CreateEmployeeReq 默认的性别是 0（保密）：老调用方不带 gender 时与改动前行为一致
    CreateEmployeeReq defaults;
    CHECK(defaults.gender == EMPLOYEE_GENDER_UNSET);
    const int64_t unset = emgCreate("王五", defaults.gender);
    CHECK(emgReadGender(unset) == EMPLOYEE_GENDER_UNSET);

    testdb::cleanupDb(EMG_DB_PATH);
}

TEST_CASE("RSW4 企业关闭了允许用户自助注册后，管理台新建员工照常成功")
{
    testdb::resetDb(EMG_DB_PATH, EMG_MYSQL_DB);
    FeatureSwitchManager& switches = Singleton<FeatureSwitchManager>::Instance();
    switches.setMemoryValue(EMG_ENTERPRISE_ID, kSettingRegisterEnabled, "0");
    REQUIRE_FALSE(switches.isEnabled(EMG_ENTERPRISE_ID, kFeatureRegister));

    //开关只拦客户端的自助注册（RegisterService），管理台建员工不经过它
    const int before = emgCountUsers();
    const int64_t userId = emgCreate("RSW4", 0);
    CHECK(userId > 0);
    CHECK(emgCountUsers() == before + 1);

    //恢复默认，不影响后面的用例
    switches.setMemoryValue(EMG_ENTERPRISE_ID, kSettingRegisterEnabled, "");
}

TEST_CASE("EMG2 编辑员工时修改性别")
{
    testdb::resetDb(EMG_DB_PATH, EMG_MYSQL_DB);

    const int64_t userId = emgCreate("张三", EMPLOYEE_GENDER_UNSET);
    CHECK(Singleton<EmployeeManager>::Instance().updateEmployee(
              emgUpdateReq(userId, EMPLOYEE_GENDER_MALE, "签名一")) == error_code_ok);
    CHECK(emgReadGender(userId) == EMPLOYEE_GENDER_MALE);

    testdb::cleanupDb(EMG_DB_PATH);
}

TEST_CASE("EMG3 编辑时不带性别则保持原值，其它字段照改")
{
    testdb::resetDb(EMG_DB_PATH, EMG_MYSQL_DB);

    const int64_t userId = emgCreate("张三", EMPLOYEE_GENDER_FEMALE);
    //UpdateEmployeeReq 默认就是「不修改性别」：老调用方不带 gender 时不会把它清掉
    UpdateEmployeeReq defaults;
    CHECK(defaults.gender == EMPLOYEE_GENDER_KEEP);

    CHECK(Singleton<EmployeeManager>::Instance().updateEmployee(
              emgUpdateReq(userId, EMPLOYEE_GENDER_KEEP, "改过的签名")) == error_code_ok);
    CHECK(emgReadGender(userId) == EMPLOYEE_GENDER_FEMALE);

    EmployeeDetail detail;
    REQUIRE(Singleton<EmployeeManager>::Instance().getEmployeeDetail(userId, detail));
    CHECK(detail.signature == "改过的签名");
    CHECK(detail.gender == EMPLOYEE_GENDER_FEMALE);

    testdb::cleanupDb(EMG_DB_PATH);
}

TEST_CASE("EMG4 非法性别在新建与编辑时都被拒绝，库里不留痕迹")
{
    testdb::resetDb(EMG_DB_PATH, EMG_MYSQL_DB);
    EmployeeManager& mgr = Singleton<EmployeeManager>::Instance();

    const int usersBefore = emgCountUsers();
    CreateEmployeeReq bad;
    bad.enterpriseId = EMG_ENTERPRISE_ID;
    bad.name = "赵六";
    bad.email = "emg@test.com";
    bad.phone = emgNextPhone();
    bad.gender = 3;
    int64_t userId = 0;
    std::string initPassword;
    std::string username;
    CHECK(mgr.createEmployee(bad, userId, initPassword, username) != error_code_ok);
    CHECK(emgCountUsers() == usersBefore);

    const int64_t existing = emgCreate("张三", EMPLOYEE_GENDER_MALE);
    CHECK(mgr.updateEmployee(emgUpdateReq(existing, 3, "x")) != error_code_ok);
    CHECK(mgr.updateEmployee(emgUpdateReq(existing, -2, "x")) != error_code_ok);
    CHECK(emgReadGender(existing) == EMPLOYEE_GENDER_MALE);

    testdb::cleanupDb(EMG_DB_PATH);
}

//按 userid 读库里的工号：NULL 返回 "<null>"，取不到返回 "<none>"（绕过所有缓存）
static std::string emgReadEmployeeNo(int64_t userid)
{
    std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
    if (!pConn)
        return "<none>";
    const std::string sql = "SELECT f_employee_no IS NULL, f_employee_no FROM t_user WHERE f_user_id = "
                            + std::to_string(userid);
    QueryResult* pResult = pConn->query(sql.c_str());
    if (pResult == NULL)
        return "<none>";
    std::string value = "<none>";
    Field* pRow = pResult->fetch();
    if (pRow != NULL)
        value = (pRow[0].getInt32() != 0) ? std::string("<null>") : pRow[1].getString();
    pResult->endQuery();
    delete pResult;
    return value;
}

TEST_CASE("LGA7 没填工号的员工编辑后工号为 NULL，同企业多人都能保存；工号重复给明确错误码")
{
    testdb::resetDb(EMG_DB_PATH, EMG_MYSQL_DB);
    EmployeeManager& mgr = Singleton<EmployeeManager>::Instance();

    //同一企业两名没填工号的员工先后编辑：此前第二名会撞「企业 + 工号」唯一索引
    const int64_t first = emgCreate("甲", EMPLOYEE_GENDER_UNSET);
    const int64_t second = emgCreate("乙", EMPLOYEE_GENDER_UNSET);
    CHECK(mgr.updateEmployee(emgUpdateReq(first, EMPLOYEE_GENDER_KEEP, "s1")) == error_code_ok);
    CHECK(mgr.updateEmployee(emgUpdateReq(second, EMPLOYEE_GENDER_KEEP, "s2")) == error_code_ok);
    CHECK(emgReadEmployeeNo(first) == "<null>");
    CHECK(emgReadEmployeeNo(second) == "<null>");

    //给甲填上工号；乙改成同一个工号被拒，库里乙的工号不变
    UpdateEmployeeReq withNo = emgUpdateReq(first, EMPLOYEE_GENDER_KEEP, "s1");
    withNo.employeeNo = "E-001";
    CHECK(mgr.updateEmployee(withNo) == error_code_ok);
    CHECK(emgReadEmployeeNo(first) == "E-001");
    UpdateEmployeeReq clash = emgUpdateReq(second, EMPLOYEE_GENDER_KEEP, "s2");
    clash.employeeNo = "E-001";
    CHECK(mgr.updateEmployee(clash) == error_code_registeralready);
    CHECK(emgReadEmployeeNo(second) == "<null>");

    //甲保留自己原来的工号再保存一次：不算重复
    CHECK(mgr.updateEmployee(withNo) == error_code_ok);
    CHECK(emgReadEmployeeNo(first) == "E-001");

    testdb::cleanupDb(EMG_DB_PATH);
}

TEST_CASE("LGA9 员工手机号必填、格式合法、全局唯一：新建与编辑时不合格都被拒")
{
    testdb::resetDb(EMG_DB_PATH, EMG_MYSQL_DB);
    EmployeeManager& mgr = Singleton<EmployeeManager>::Instance();
    const int usersBefore = emgCountUsers();

    //新建：为空、位数不对、不以 1 开头都返回 error_code_phone_invalid，不建号
    const char* const kBadPhones[] = { "", "1380000000", "138000000000", "23800000000", "138 0000 000" };
    for (size_t i = 0; i < sizeof(kBadPhones) / sizeof(kBadPhones[0]); ++i)
    {
        CAPTURE(kBadPhones[i]);
        CreateEmployeeReq req;
        req.enterpriseId = EMG_ENTERPRISE_ID;
        req.name = "坏号码";
        req.email = "emg@test.com";
        req.phone = kBadPhones[i];
        int64_t userId = 0;
        std::string initPassword;
        std::string username;
        CHECK(mgr.createEmployee(req, userId, initPassword, username) == error_code_phone_invalid);
    }
    CHECK(emgCountUsers() == usersBefore);

    //新建：号码与已有员工重复返回 error_code_phone_taken
    const int64_t owner = emgCreate("号码主人", EMPLOYEE_GENDER_UNSET);
    const std::string ownerPhone = emgPhoneOf(owner);
    REQUIRE_FALSE(ownerPhone.empty());
    CreateEmployeeReq dup;
    dup.enterpriseId = EMG_ENTERPRISE_ID;
    dup.name = "撞号";
    dup.email = "emg@test.com";
    dup.phone = ownerPhone;
    int64_t dupId = 0;
    std::string dupPassword;
    std::string dupUsername;
    CHECK(mgr.createEmployee(dup, dupId, dupPassword, dupUsername) == error_code_phone_taken);

    //编辑：保留自己原来的号码可以保存；改成别人的号码被拒；改成空串或坏号码被拒；库里号码不变
    const int64_t other = emgCreate("另一人", EMPLOYEE_GENDER_UNSET);
    const std::string otherPhone = emgPhoneOf(other);
    CHECK(mgr.updateEmployee(emgUpdateReq(other, EMPLOYEE_GENDER_KEEP, "keep")) == error_code_ok);
    UpdateEmployeeReq steal = emgUpdateReq(other, EMPLOYEE_GENDER_KEEP, "steal");
    steal.phone = ownerPhone;
    CHECK(mgr.updateEmployee(steal) == error_code_phone_taken);
    UpdateEmployeeReq empty = emgUpdateReq(other, EMPLOYEE_GENDER_KEEP, "empty");
    empty.phone = "";
    CHECK(mgr.updateEmployee(empty) == error_code_phone_invalid);
    CHECK(emgPhoneOf(other) == otherPhone);

    //编辑：换成一个没人用的新号码可以保存
    UpdateEmployeeReq change = emgUpdateReq(other, EMPLOYEE_GENDER_KEEP, "change");
    change.phone = emgNextPhone();
    CHECK(mgr.updateEmployee(change) == error_code_ok);
    CHECK(emgPhoneOf(other) == change.phone);

    testdb::cleanupDb(EMG_DB_PATH);
}
