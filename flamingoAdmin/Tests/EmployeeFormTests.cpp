/**
 *  flamingoAdmin —— 员工性别与部门负责人选择的单元测试（GEN、LDR 系列）。
 *
 *  修复前的两个问题：
 *    - 新增员工弹窗选的性别被丢弃（user/create.do 不接受性别），编辑员工没有性别；员工详情
 *      又按 0 男 / 1 女 解读服务端的 0 保密 / 1 男 / 2 女，男员工显示成「女」。
 *    - 新增部门弹窗的「负责人」是自由输入的姓名，服务端要的是员工 ID，填了也被丢弃。
 *  本文件覆盖：
 *    - GEN1：性别编码 0 / 1 / 2 / 其它值的显示文字；员工详情按新编码解析；
 *    - GEN2：新增、编辑员工请求里带上 gender；
 *    - GEN3：性别下拉框序号与编码互相转换一致；
 *    - LDR1：从组织树收集负责人候选人：按员工 ID 去重，副文字为「部门 · 职务」；
 *    - LDR2：选中某人时取到其 ID，选「不设置」或留空时为 0；
 *    - LDR3：手输文字与任何一项都对不上（或对上了多个同名的人）时判为无效；
 *    - LDR4：建部门请求里的 leaderuserid 等于所选员工的 ID；
 *    - LDR5：解析组织树时，负责人不是本部门成员（选择器允许选本企业任何员工）也能显示姓名。
 *  2026-10-06（docs/登录账号与手机号登录方案.md）：
 *    - ACP1：账号前缀的规整规则；ACP2：手机号格式（两者与服务端用同一组输入输出）；
 *    - ACP3：企业列表与详情应答里的 accountprefix；ACP4：修改企业信息的请求带 accountprefix；
 *    - ACP5：错误码 101、102、119、150、151、152 的中文提示。
 *  2026-10-07（重置员工密码时显示被强制下线的设备数）：
 *    - RPN1：重置密码应答里的 newpassword 与 kicked 都解析出来，老服务端不带 kicked 时为 0；
 *    - RPN2：「密码已重置」弹窗正文在有设备被下线时多一句「已强制该员工的 N 台设备下线。」，没有时不提。
 *  2026-10-10（组织树里的员工状态）：
 *    - ACS1：组织树里员工是否停用取成员的 accountstatus，在线状态 status 不影响；老服务端没有 accountstatus 时按在职。
 *
 *  全部为纯函数测试，不发起真实网络请求。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"
#include "Tests/TestFramework.h"
#include "Util/AdminApi.h"
#include "Util/OrgTreeBuilder.h"

#include <string>
#include <vector>

namespace flamingoadmin { namespace test {

namespace {

using namespace flamingoadmin::adminapi;
using namespace flamingoadmin::orgtree;

// 构造一份测试用的服务器配置。主机与端口不影响被测的纯函数，取固定值即可。
ServerConfig MakeServer()
{
    ServerConfig s;
    s.m_host = _T("127.0.0.1");
    s.m_port = 12345;
    s.m_useTls = false;
    return s;
}

// 构造一个「传输成功、HTTP 200、body 为给定 JSON」的应答。
//   json：应答体（UTF-8 JSON 文本）。
HttpResult MakeHttpOk(const std::string& json)
{
    HttpResult r;
    r.m_transportOk = true;
    r.m_statusCode = 200;
    r.m_body = json;
    return r;
}

// 判断请求体里是否含某个片段。请求体是 UTF-8 紧凑 JSON，直接做子串匹配。
//   req：待检查的请求；fragment：期望出现的片段。
bool BodyHas(const HttpRequest& req, const char* fragment)
{
    return req.m_bodyUtf8.find(fragment) != std::string::npos;
}

// 构造一个员工节点。
//   id：员工 ID；name：姓名；title：职务。
OrgNode MakeUser(LPCTSTR id, LPCTSTR name, LPCTSTR title)
{
    OrgNode n;
    n.m_kind  = OrgNodeKind::User;
    n.m_id    = id;
    n.m_name  = name;
    n.m_title = title;
    return n;
}

// 构造一个部门节点。
//   id：部门 ID；name：部门名。
OrgNode MakeDept(LPCTSTR id, LPCTSTR name)
{
    OrgNode n;
    n.m_kind = OrgNodeKind::Dept;
    n.m_id   = id;
    n.m_name = name;
    return n;
}

// 样例组织树：
//   明远科技（1，公司根）
//     研发部（11）：张三 501 架构师、王芳 503（无职务）
//     市场部（12）：张三 501 兼职顾问（同一人挂在两个部门）、王芳 504 专员（与 503 同名）
//     李四 502 总经理（直接挂在公司根下）
OrgNode MakeSampleOrg()
{
    OrgNode root = MakeDept(_T("1"), _T("明远科技"));
    root.m_isCompanyRoot = true;

    OrgNode rd = MakeDept(_T("11"), _T("研发部"));
    rd.m_children.push_back(MakeUser(_T("501"), _T("张三"), _T("架构师")));
    rd.m_children.push_back(MakeUser(_T("503"), _T("王芳"), _T("")));

    OrgNode mk = MakeDept(_T("12"), _T("市场部"));
    mk.m_children.push_back(MakeUser(_T("501"), _T("张三"), _T("兼职顾问")));
    mk.m_children.push_back(MakeUser(_T("504"), _T("王芳"), _T("专员")));

    root.m_children.push_back(rd);
    root.m_children.push_back(mk);
    root.m_children.push_back(MakeUser(_T("502"), _T("李四"), _T("总经理")));
    return root;
}

// GEN1：显示文字与员工详情解析。
void TestGenderDisplayText()
{
    BeginCase("GEN1 性别编码 0/1/2/其它 显示为 保密/男/女/保密，员工详情按此解析");

    FA_EXPECT_STREQ(GenderDisplayText(0), _T("保密"));
    FA_EXPECT_STREQ(GenderDisplayText(1), _T("男"));
    FA_EXPECT_STREQ(GenderDisplayText(2), _T("女"));
    FA_EXPECT_STREQ(GenderDisplayText(7), _T("保密"));
    FA_EXPECT_STREQ(GenderDisplayText(-1), _T("保密"));

    const UserDetailResult male = ParseUserDetailResponse(MakeHttpOk(
        "{\"code\":0,\"user\":{\"userid\":501,\"name\":\"张三\",\"gender\":1}}"));
    FA_EXPECT_TRUE(male.Succeeded());
    FA_EXPECT_STREQ(male.m_user.m_gender, _T("男"));
    FA_EXPECT_EQ(male.m_user.m_genderCode, 1);

    const UserDetailResult female = ParseUserDetailResponse(MakeHttpOk(
        "{\"code\":0,\"user\":{\"userid\":502,\"name\":\"李四\",\"gender\":2}}"));
    FA_EXPECT_STREQ(female.m_user.m_gender, _T("女"));
    FA_EXPECT_EQ(female.m_user.m_genderCode, 2);
}

// GEN2：新增、编辑员工请求里带上 gender。
void TestGenderInRequests()
{
    BeginCase("GEN2 新增、编辑员工请求带上 gender");

    const HttpRequest create = MakeUserCreateRequest(
        MakeServer(), _T("tk"), 7, 11, _T("张三"), _T("架构师"), CString(),
        _T("z@x.com"), CString(), CString(), 2);
    FA_EXPECT_TRUE(BodyHas(create, "\"gender\":2"));

    const HttpRequest update = MakeUserUpdateRequest(
        MakeServer(), _T("tk"), 501, _T("张三"), _T("架构师"), CString(), CString(),
        CString(), CString(), CString(), _T("z@x.com"), CString(), 1);
    FA_EXPECT_TRUE(BodyHas(update, "\"gender\":1"));
}

// GEN3：下拉框序号与编码互逆。
void TestGenderComboMapping()
{
    BeginCase("GEN3 性别下拉框序号与编码互相转换一致");

    for (int code = 0; code <= 2; ++code)
    {
        FA_EXPECT_EQ(GenderComboIndexToCode(GenderCodeToComboIndex(code)), code);
    }
    // 下拉框依次是「保密 / 男 / 女」。
    FA_EXPECT_EQ(GenderComboIndexToCode(0), 0);
    FA_EXPECT_EQ(GenderComboIndexToCode(1), 1);
    FA_EXPECT_EQ(GenderComboIndexToCode(2), 2);
    // 越界的序号（未选中为 -1）按「保密」；未知编码选中「保密」。
    FA_EXPECT_EQ(GenderComboIndexToCode(-1), 0);
    FA_EXPECT_EQ(GenderComboIndexToCode(9), 0);
    FA_EXPECT_EQ(GenderCodeToComboIndex(7), 0);
}

// LDR1：候选人收集。
void TestCollectLeaderCandidates()
{
    BeginCase("LDR1 负责人候选人：按员工 ID 去重，副文字为「部门 · 职务」");

    const OrgNode org = MakeSampleOrg();
    const std::vector<LeaderCandidate> c = CollectLeaderCandidates(&org);
    FA_EXPECT_EQ(c.size(), 4);
    if (c.size() == 4)
    {
        // 按组织树的深度优先顺序：研发部的两人、市场部的王芳 504、公司根下的李四。
        FA_EXPECT_STREQ(c[0].m_userId, _T("501"));
        FA_EXPECT_STREQ(c[0].m_name, _T("张三"));
        FA_EXPECT_STREQ(c[0].m_subText, _T("研发部 · 架构师"));   // 第一次出现处的部门与职务
        FA_EXPECT_STREQ(c[1].m_userId, _T("503"));
        FA_EXPECT_STREQ(c[1].m_subText, _T("研发部"));            // 没有职务时只显示部门
        FA_EXPECT_STREQ(c[2].m_userId, _T("504"));
        FA_EXPECT_STREQ(c[2].m_subText, _T("市场部 · 专员"));
        FA_EXPECT_STREQ(c[3].m_userId, _T("502"));
        FA_EXPECT_STREQ(c[3].m_subText, _T("明远科技 · 总经理"));
    }

    // 组织树尚未加载：没有候选人。
    FA_EXPECT_EQ(CollectLeaderCandidates(nullptr).size(), 0);
}

// LDR2：有效的选择。
void TestResolveLeaderValid()
{
    BeginCase("LDR2 选中某人取到其 ID，选「不设置」或留空为 0");

    const OrgNode org = MakeSampleOrg();
    const std::vector<LeaderCandidate> c = CollectLeaderCandidates(&org);

    // 下拉框第 0 项是「不设置」，第 i + 1 项对应 c[i]。
    FA_EXPECT_EQ(ResolveLeaderChoice(c, 0, kLeaderNoneText), 0);
    FA_EXPECT_EQ(ResolveLeaderChoice(c, -1, _T("")), 0);
    FA_EXPECT_EQ(ResolveLeaderChoice(c, -1, _T("   ")), 0);
    FA_EXPECT_EQ(ResolveLeaderChoice(c, 1, _T("张三")), 501);
    FA_EXPECT_EQ(ResolveLeaderChoice(c, 4, _T("李四")), 502);
    // 同名的王芳靠选中项区分。
    FA_EXPECT_EQ(ResolveLeaderChoice(c, 2, _T("王芳")), 503);
    FA_EXPECT_EQ(ResolveLeaderChoice(c, 3, _T("王芳")), 504);
    // 没有选中项但手输的姓名唯一对得上：按那个人处理。
    FA_EXPECT_EQ(ResolveLeaderChoice(c, -1, _T("李四")), 502);
    FA_EXPECT_EQ(ResolveLeaderChoice(c, -1, _T(" 李四 ")), 502);
}

// LDR3：无效的选择。
void TestResolveLeaderInvalid()
{
    BeginCase("LDR3 手输文字对不上任何一项或对上多个同名的人时判为无效");

    const OrgNode org = MakeSampleOrg();
    const std::vector<LeaderCandidate> c = CollectLeaderCandidates(&org);

    FA_EXPECT_EQ(ResolveLeaderChoice(c, -1, _T("赵六")), kLeaderInvalid);
    FA_EXPECT_EQ(ResolveLeaderChoice(c, -1, _T("张")), kLeaderInvalid);       // 只输了一半
    FA_EXPECT_EQ(ResolveLeaderChoice(c, -1, _T("王芳")), kLeaderInvalid);     // 两个同名，无法确定
    // 选中项与文字不一致（选了张三后又把文字改成别的）：以文字为准重新匹配。
    FA_EXPECT_EQ(ResolveLeaderChoice(c, 1, _T("赵六")), kLeaderInvalid);
    FA_EXPECT_EQ(ResolveLeaderChoice(c, 1, _T("李四")), 502);
    // 选中项越界。
    FA_EXPECT_EQ(ResolveLeaderChoice(c, 99, _T("赵六")), kLeaderInvalid);
}

// LDR4：建部门请求带上负责人 ID。
void TestDeptCreateCarriesLeader()
{
    BeginCase("LDR4 建部门请求里的 leaderuserid 等于所选员工的 ID");

    const OrgNode org = MakeSampleOrg();
    const std::vector<LeaderCandidate> c = CollectLeaderCandidates(&org);
    const long long leader = ResolveLeaderChoice(c, 4, _T("李四"));
    const HttpRequest req = MakeDeptCreateRequest(
        MakeServer(), _T("tk"), 7, 1, _T("增长团队"), CString(), leader, 0);
    FA_EXPECT_TRUE(BodyHas(req, "\"leaderuserid\":502"));

    const HttpRequest none = MakeDeptCreateRequest(
        MakeServer(), _T("tk"), 7, 1, _T("增长团队"), CString(),
        ResolveLeaderChoice(c, 0, kLeaderNoneText), 0);
    FA_EXPECT_TRUE(BodyHas(none, "\"leaderuserid\":0"));
}

// LDR5：负责人不在本部门成员里时，按全企业成员补查姓名。
void TestDeptTreeResolvesLeaderAcrossDepartments()
{
    BeginCase("LDR5 组织树解析：负责人不是本部门成员时也能显示姓名");

    // 公司根 1 下有研发部 11（成员张三 501）与增长团队 13（没有成员，负责人是研发部的张三）；
    // 市场部 12 的负责人 502 是本部门成员；部门 14 的负责人 999 不在任何部门里。
    const DeptTreeResult r = ParseDeptTreeResponse(MakeHttpOk(
        "{\"code\":0,\"departments\":["
        "{\"deptid\":1,\"parentdeptid\":0,\"deptname\":\"联调科技\",\"leaderuserid\":0,\"members\":[]},"
        "{\"deptid\":11,\"parentdeptid\":1,\"deptname\":\"研发部\",\"leaderuserid\":0,"
        "\"members\":[{\"userid\":501,\"nickname\":\"张三\"}]},"
        "{\"deptid\":12,\"parentdeptid\":1,\"deptname\":\"市场部\",\"leaderuserid\":502,"
        "\"members\":[{\"userid\":502,\"nickname\":\"李四\"}]},"
        "{\"deptid\":13,\"parentdeptid\":1,\"deptname\":\"增长团队\",\"leaderuserid\":501,\"members\":[]},"
        "{\"deptid\":14,\"parentdeptid\":1,\"deptname\":\"空部门\",\"leaderuserid\":999,\"members\":[]}"
        "]}"), 1, _T("联调科技"));
    FA_EXPECT_TRUE(r.Succeeded());

    CString leaderOf12;
    CString leaderOf13;
    CString leaderOf14 = _T("<not found>");
    for (size_t i = 0; i < r.m_root.m_children.size(); ++i)
    {
        const OrgNode& d = r.m_root.m_children[i];
        if (d.m_id == _T("12"))
        {
            leaderOf12 = d.m_leader;
        }
        else if (d.m_id == _T("13"))
        {
            leaderOf13 = d.m_leader;
        }
        else if (d.m_id == _T("14"))
        {
            leaderOf14 = d.m_leader;
        }
    }
    FA_EXPECT_STREQ(leaderOf12, _T("李四"));   // 负责人在本部门：与原来一样
    FA_EXPECT_STREQ(leaderOf13, _T("张三"));   // 负责人在别的部门：按全企业成员补查
    FA_EXPECT_TRUE(leaderOf14.IsEmpty());      // 负责人不在任何部门：留空，界面显示「—」
}

// ACP1：账号前缀的规整规则，与服务端 NormalizeAccountPrefix（服务端单测 LGA2）用同一组输入输出。
void TestNormalizeAccountPrefix()
{
    BeginCase("ACP1 账号前缀：字母与点号、1～10 个字符、至少一个字母、转大写，空串表示清除");

    CString out = _T("unchanged");
    FA_EXPECT_TRUE(NormalizeAccountPrefix(_T("hw"), out));
    FA_EXPECT_STREQ(out, _T("HW"));
    FA_EXPECT_TRUE(NormalizeAccountPrefix(_T(" fl.im "), out));
    FA_EXPECT_STREQ(out, _T("FL.IM"));
    FA_EXPECT_TRUE(NormalizeAccountPrefix(_T("jingcheng."), out));
    FA_EXPECT_STREQ(out, _T("JINGCHENG."));
    FA_EXPECT_TRUE(NormalizeAccountPrefix(_T("ABCDEFGHIJ"), out));
    FA_EXPECT_STREQ(out, _T("ABCDEFGHIJ"));
    out = _T("x");
    FA_EXPECT_TRUE(NormalizeAccountPrefix(_T("   "), out));
    FA_EXPECT_TRUE(out.IsEmpty());

    FA_EXPECT_FALSE(NormalizeAccountPrefix(_T("ABCDEFGHIJK"), out));
    FA_EXPECT_FALSE(NormalizeAccountPrefix(_T("HW1"), out));
    FA_EXPECT_FALSE(NormalizeAccountPrefix(_T("H-W"), out));
    FA_EXPECT_FALSE(NormalizeAccountPrefix(_T("华为"), out));
    FA_EXPECT_FALSE(NormalizeAccountPrefix(_T(".."), out));
    FA_EXPECT_FALSE(NormalizeAccountPrefix(_T("H W"), out));
}

// ACP2：手机号格式，与服务端 IsValidPhoneNumber（服务端单测 LGA8）用同一组输入输出。
void TestPhoneNumberFormat()
{
    BeginCase("ACP2 手机号：11 位以 1 开头的数字合格，其余不合格");

    FA_EXPECT_TRUE(IsValidPhoneNumber(_T("13800005001")));
    FA_EXPECT_TRUE(IsValidPhoneNumber(_T("19912345678")));
    FA_EXPECT_FALSE(IsValidPhoneNumber(_T("")));
    FA_EXPECT_FALSE(IsValidPhoneNumber(_T("1380000500")));
    FA_EXPECT_FALSE(IsValidPhoneNumber(_T("138000050011")));
    FA_EXPECT_FALSE(IsValidPhoneNumber(_T("23800005001")));
    FA_EXPECT_FALSE(IsValidPhoneNumber(_T("138 0000 500")));
    FA_EXPECT_FALSE(IsValidPhoneNumber(_T("138-0000-500")));
    FA_EXPECT_FALSE(IsValidPhoneNumber(_T("+8613800005")));
}

// ACP3：企业列表与详情应答里的 accountprefix 解析进 Enterprise::m_accountPrefix。
void TestParseEnterpriseAccountPrefix()
{
    BeginCase("ACP3 企业列表与详情应答里的账号前缀解析进企业记录，缺字段时为空");

    const EnterpriseListResult list = ParseEnterpriseListResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"total\":2,\"list\":["
        "{\"enterpriseid\":1,\"name\":\"a\",\"accountprefix\":\"HW.\"},"
        "{\"enterpriseid\":2,\"name\":\"b\"}]}"));
    FA_EXPECT_TRUE(list.Succeeded());
    FA_EXPECT_EQ(static_cast<int>(list.m_list.size()), 2);
    if (list.m_list.size() == 2)
    {
        FA_EXPECT_STREQ(list.m_list[0].m_accountPrefix, _T("HW."));
        FA_EXPECT_TRUE(list.m_list[1].m_accountPrefix.IsEmpty());   //老服务端不下发：为空
    }

    const EnterpriseDetailResult detail = ParseEnterpriseDetailResponse(MakeHttpOk(
        "{\"code\":0,\"msg\":\"ok\",\"enterprise\":{\"enterpriseid\":3,\"name\":\"c\",\"accountprefix\":\"FL.IM\"}}"));
    FA_EXPECT_TRUE(detail.Succeeded());
    FA_EXPECT_STREQ(detail.m_enterprise.m_accountPrefix, _T("FL.IM"));
}

// ACP4：修改企业信息的请求总是带上 accountprefix（服务端「带了才改」，空串表示清除）。
void TestEnterpriseUpdateCarriesPrefix()
{
    BeginCase("ACP4 修改企业信息的请求带上账号前缀，空串同样带上");

    const HttpRequest withPrefix = MakeEnterpriseUpdateRequest(
        MakeServer(), _T("tk"), 7, _T("企业"), _T("简称"), _T("金融服务"), _T("上海"), 1, _T("HW."));
    FA_EXPECT_TRUE(BodyHas(withPrefix, "\"accountprefix\":\"HW.\""));
    FA_EXPECT_TRUE(BodyHas(withPrefix, "\"plan\":1"));

    const HttpRequest cleared = MakeEnterpriseUpdateRequest(
        MakeServer(), _T("tk"), 7, _T("企业"), _T("简称"), _T("金融服务"), _T("上海"), 1, CString());
    FA_EXPECT_TRUE(BodyHas(cleared, "\"accountprefix\":\"\""));
}

// ACP5：新增的几个错误码都有中文提示，不再显示「错误码 N」。
void TestEmployeeAndPrefixErrorMessages()
{
    BeginCase("ACP5 错误码 101、102、119、150、151、152 都有对应的中文提示");

    struct Expect
    {
        const char*  m_body;      // 服务端应答体
        const TCHAR* m_message;   // 期望的提示文字
    };
    const Expect kCases[] =
    {
        { "{\"code\":101,\"msg\":\"x\"}", _T("工号已被本企业其他员工使用") },
        { "{\"code\":102,\"msg\":\"x\"}", _T("员工不存在，可能已被删除") },
        { "{\"code\":119,\"msg\":\"x\"}", _T("企业全称已被其他企业使用") },
        { "{\"code\":150,\"msg\":\"x\"}", _T("请填写正确的手机号（11 位，以 1 开头）") },
        { "{\"code\":151,\"msg\":\"x\"}", _T("该手机号已被其他账号使用") },
        { "{\"code\":152,\"msg\":\"x\"}", _T("账号前缀只能由字母和点号组成，1～10 个字符") },
    };
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
    {
        const EnterpriseActionResult r = ParseEnterpriseActionResponse(MakeHttpOk(kCases[i].m_body));
        FA_EXPECT_FALSE(r.Succeeded());
        FA_EXPECT_STREQ(r.m_message, kCases[i].m_message);
    }
}

// RPN1：重置密码应答里的新密码与被强制下线的会话数都解析出来；老服务端不带 kicked 时为 0。
void TestResetPasswordParsesKicked()
{
    BeginCase("RPN1 重置密码应答解析出新密码与被强制下线的会话数，不带 kicked 时为 0");

    const UserResetPwdResult withKicked =
        ParseUserResetPasswordResponse(MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"newpassword\":\"a1b2c3d4e5\",\"kicked\":2}"));
    FA_EXPECT_TRUE(withKicked.Succeeded());
    FA_EXPECT_STREQ(withKicked.m_newPassword, _T("a1b2c3d4e5"));
    FA_EXPECT_EQ(withKicked.m_kicked, 2);

    const UserResetPwdResult oldServer =
        ParseUserResetPasswordResponse(MakeHttpOk("{\"code\":0,\"msg\":\"ok\",\"newpassword\":\"a1b2c3d4e5\"}"));
    FA_EXPECT_TRUE(oldServer.Succeeded());
    FA_EXPECT_EQ(oldServer.m_kicked, 0);
}

// RPN2：「密码已重置」弹窗正文：有设备被强制下线时多一句说明，没有时不提。
void TestResetPasswordNotice()
{
    BeginCase("RPN2 密码已重置弹窗：有设备被下线时说明台数，没有时不提");

    const CString none = FormatResetPasswordNotice(_T("a1b2c3d4e5"), 0);
    FA_EXPECT_STREQ(none, _T("员工登录密码已重置。\n新密码：a1b2c3d4e5\n请妥善保存并转交，此密码仅显示一次。"));

    const CString two = FormatResetPasswordNotice(_T("a1b2c3d4e5"), 2);
    FA_EXPECT_STREQ(two, _T("员工登录密码已重置。\n新密码：a1b2c3d4e5\n已强制该员工的 2 台设备下线。\n")
                         _T("请妥善保存并转交，此密码仅显示一次。"));
}

// 在组织树里按 m_id 递归查找节点（子部门与员工都在 m_children 里）；找不到时返回 nullptr
const OrgNode* FindNodeById(const OrgNode& node, const CString& id)
{
    if (node.m_id == id)
    {
        return &node;
    }
    for (size_t i = 0; i < node.m_children.size(); ++i)
    {
        const OrgNode* hit = FindNodeById(node.m_children[i], id);
        if (hit != nullptr)
        {
            return hit;
        }
    }
    return nullptr;
}

// ACS1：组织树里员工的启用 / 停用取 accountstatus，不取在线状态 status。
void TestDeptTreeAccountStatus()
{
    BeginCase("ACS1 组织树解析：员工是否停用取 accountstatus，在线状态 status 不影响");

    // 501 在线、账号启用；502 离线、账号停用；503 是老服务端的应答（没有 accountstatus），在线
    const DeptTreeResult r = ParseDeptTreeResponse(MakeHttpOk(
        "{\"code\":0,\"departments\":["
        "{\"deptid\":1,\"parentdeptid\":0,\"deptname\":\"联调科技\",\"leaderuserid\":0,\"members\":["
        "{\"userid\":501,\"nickname\":\"张三\",\"status\":1,\"accountstatus\":0},"
        "{\"userid\":502,\"nickname\":\"李四\",\"status\":0,\"accountstatus\":1},"
        "{\"userid\":503,\"nickname\":\"王五\",\"status\":1}]}"
        "]}"), 1, _T("联调科技"));
    FA_EXPECT_TRUE(r.Succeeded());

    const OrgNode* online = FindNodeById(r.m_root, _T("501"));
    const OrgNode* disabled = FindNodeById(r.m_root, _T("502"));
    const OrgNode* legacy = FindNodeById(r.m_root, _T("503"));
    FA_EXPECT_TRUE(online != nullptr);
    FA_EXPECT_TRUE(disabled != nullptr);
    FA_EXPECT_TRUE(legacy != nullptr);
    if (online == nullptr || disabled == nullptr || legacy == nullptr)
    {
        return;
    }
    FA_EXPECT_EQ((int)online->m_userStatus, (int)UserStatus::Active);
    FA_EXPECT_EQ((int)disabled->m_userStatus, (int)UserStatus::Disabled);
    FA_EXPECT_EQ((int)legacy->m_userStatus, (int)UserStatus::Active);
}

} // anonymous namespace

// 运行本文件全部用例。
void RunAllEmployeeFormTests()
{
    TestGenderDisplayText();
    TestGenderInRequests();
    TestGenderComboMapping();
    TestCollectLeaderCandidates();
    TestResolveLeaderValid();
    TestResolveLeaderInvalid();
    TestDeptCreateCarriesLeader();
    TestDeptTreeResolvesLeaderAcrossDepartments();
    TestNormalizeAccountPrefix();
    TestPhoneNumberFormat();
    TestParseEnterpriseAccountPrefix();
    TestEnterpriseUpdateCarriesPrefix();
    TestEmployeeAndPrefixErrorMessages();
    TestResetPasswordParsesKicked();
    TestResetPasswordNotice();
    TestDeptTreeAccountStatus();
}

}} // namespace flamingoadmin::test
