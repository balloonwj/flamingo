/**
 *  RegisterTests.cpp
 *
 *  注册对话框的契约测试：
 *    R1-R8    BuildRegisterBody 产出的控件树符合"控件点名契约" —— 两步
 *             各自的输入框 / 按钮都能按既定数字 id 找到、id 互不重复，
 *             越界步骤号回退到步骤 1。
 *    R9-R20   RegisterModel 数据模型：步骤推进 / 回退、字段读写、表单校验
 *             （合法 / 各类非法输入）与提示文案。
 *    R21-R24  cmd_register 报文编解码（RegisterProtocolClient）。
 *    RG1-RG4  公司名称组合框与默认公司。
 *    RG5      企业关闭了「允许用户自助注册」（服务端错误码 410）时给出专门的提示。
 *    RG6      服务端建号失败（错误码 100）时提示稍后重试，不再提「自定义账号」。
 *    DC3      默认公司取 flamingo.ini 的 [ui] defaultcompany：注册模型的初始公司名与组合框的
 *             默认项都取配置值，未读取配置时为 FlamingoIM.org（DC1～DC2 在 BrandTextTests.cpp）。
 *
 *  用 Catch2 v3（amalgamated 形态）作测试框架。本工程不创建任何窗口，
 *  纯粹在内存里 build 控件树 / 操作模型 + 断言。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonRegister/RegisterLayout.h"
#include "LayoutFitCheck.h"                 // layoutfit::MeasureAt：按设计尺寸排得下的检查
#include "../UI/BalloonRegister/RegisterModel.h"
#include "../App/BrandText.h"               // g_strDefaultCompany / kDefaultCompany：注册的默认公司
#include "../net/RegisterProtocolClient.h"
#include "../net/Protocol.h"                // enum error_code：RG5 的服务端错误码
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"   // 公司名称组合框
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"

using namespace balloonwjui;
using namespace balloonregister;

namespace {

// 递归收集子树里所有非 0 的 ctrlId。
void CollectRegIds(const DuiControl* node, std::vector<UINT>& out)
{
    if (node == nullptr)
    {
        return;
    }
    UINT id = node->GetCtrlId();
    if (id != 0)
    {
        out.push_back(id);
    }
    for (const auto& child : node->Children())
    {
        CollectRegIds(child.get(), out);
    }
}

// 判断一组 id 里是否存在重复值。
bool HasDuplicateRegId(std::vector<UINT> ids)
{
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

// 造一份各字段都合法的模型，用例按需改其中一两项来触发某条校验分支。
RegisterModel MakeValidModel()
{
    RegisterModel model;
    model.SetNickname(_T("火烈鸟"));
    model.SetPassword(_T("pass1234"));         // 8 位，不短于下限
    model.SetConfirmPassword(_T("pass1234"));  // 两次一致
    model.SetPhone(_T("13800138000"));         // 11 位、首位为 1
    model.SetCompany(_T("明远科技有限公司"));
    return model;
}

} // anonymous namespace

// =====================================================================
// R1-R8 注册窗布局契约
// =====================================================================

TEST_CASE("R1 BuildRegisterBody 两步均返回非空控件树", "[register]")
{
    REQUIRE(BuildRegisterBody(kRegStepFillInfo) != nullptr);
    REQUIRE(BuildRegisterBody(kRegStepDone)     != nullptr);
}

TEST_CASE("R2 填写页含全部五个输入框", "[register]")
{
    auto root = BuildRegisterBody(kRegStepFillInfo);
    REQUIRE(root != nullptr);

    CHECK(dynamic_cast<DuiEdit*>(root->FindCtrlById(kRegIdEditNick))    != nullptr);
    CHECK(dynamic_cast<DuiEdit*>(root->FindCtrlById(kRegIdEditPwd))     != nullptr);
    CHECK(dynamic_cast<DuiEdit*>(root->FindCtrlById(kRegIdEditPwd2))    != nullptr);
    // 手机号原先在已被去掉的"手机验证"那一步，现在并入本页。
    CHECK(dynamic_cast<DuiEdit*>(root->FindCtrlById(kRegIdEditPhone))   != nullptr);
    // 公司名称是注册的核心输入，用可编辑组合框：既可选默认公司，也可自行输入。
    CHECK(dynamic_cast<DuiComboBox*>(root->FindCtrlById(kRegIdComboCompany)) != nullptr);
}

TEST_CASE("R3 填写页含返回按钮与提交注册按钮", "[register]")
{
    auto root = BuildRegisterBody(kRegStepFillInfo);
    REQUIRE(root != nullptr);

    CHECK(root->FindCtrlById(kRegIdBackBtn)   != nullptr);
    CHECK(root->FindCtrlById(kRegIdBtnSubmit) != nullptr);
}

TEST_CASE("R4 完成页含立即登录按钮与两行结果文字", "[register]")
{
    auto root = BuildRegisterBody(kRegStepDone);
    REQUIRE(root != nullptr);

    CHECK(root->FindCtrlById(kRegIdBtnLogin)    != nullptr);
    // 账号与公司名两行文字须存在且可按 id 改写 —— 它们的内容只能来自服务端应答。
    // 账号那一行尤其关键：账号是服务端自动生成的，这里显示不出来用户就没法登录。
    CHECK(root->FindCtrlById(kRegIdTextAccount) != nullptr);
    CHECK(root->FindCtrlById(kRegIdTextCompany) != nullptr);
}

TEST_CASE("R5 SetRegisterText 能按 id 改写完成页的结果文字", "[register]")
{
    auto root = BuildRegisterBody(kRegStepDone);
    REQUIRE(root != nullptr);

    CHECK(SetRegisterText(root.get(), kRegIdTextAccount, _T("你的账号是 EMP88234589")));
    CHECK(SetRegisterText(root.get(), kRegIdTextCompany, _T("已加入 明远科技有限公司")));

    // 不存在的 id、以及并非文字控件的 id（按钮），都应返回 false 而不是崩掉。
    CHECK_FALSE(SetRegisterText(root.get(), 9999, _T("x")));
    CHECK_FALSE(SetRegisterText(root.get(), kRegIdBtnLogin, _T("x")));
    CHECK_FALSE(SetRegisterText(nullptr, kRegIdTextAccount, _T("x")));
}

TEST_CASE("R5b 填写页的服务协议 / 隐私政策是链接模式的 DuiLabel", "[register]")
{
    auto root = BuildRegisterBody(kRegStepFillInfo);
    REQUIRE(root != nullptr);

    DuiLabel* agreement = dynamic_cast<DuiLabel*>(root->FindCtrlById(kRegIdLinkAgreement));
    DuiLabel* privacy   = dynamic_cast<DuiLabel*>(root->FindCtrlById(kRegIdLinkPrivacy));
    REQUIRE(agreement != nullptr);
    REQUIRE(privacy   != nullptr);

    // ModeLink 才有下划线、hover 变色、手型光标与 DUIN_CLICK 这一整套超链接行为。
    CHECK(agreement->GetMode() == DuiLabel::ModeLink);
    CHECK(privacy->GetMode()   == DuiLabel::ModeLink);

    // 点击语义由注册窗决定（协议页面尚未接入），控件不得自行 ShellExecute。
    CHECK_FALSE(agreement->GetAutoNavigate());
    CHECK_FALSE(privacy->GetAutoNavigate());

    CHECK_FALSE(agreement->GetText().IsEmpty());
    CHECK_FALSE(privacy->GetText().IsEmpty());
}

TEST_CASE("R6 两步的控件 id 各自互不重复", "[register]")
{
    for (int step = kRegStepFillInfo; step <= kRegStepDone; ++step)
    {
        auto root = BuildRegisterBody(step);
        REQUIRE(root != nullptr);

        std::vector<UINT> ids;
        CollectRegIds(root.get(), ids);
        CHECK_FALSE(ids.empty());
        CHECK_FALSE(HasDuplicateRegId(ids));
    }
}

TEST_CASE("R7 越界步骤号回退到步骤 1 的控件树", "[register]")
{
    // 越界（0 / 99）应回退到步骤 1 —— 故能找到填写页的"提交注册"按钮。
    auto rootLow  = BuildRegisterBody(0);
    auto rootHigh = BuildRegisterBody(99);
    REQUIRE(rootLow  != nullptr);
    REQUIRE(rootHigh != nullptr);
    CHECK(rootLow->FindCtrlById(kRegIdBtnSubmit)  != nullptr);
    CHECK(rootHigh->FindCtrlById(kRegIdBtnSubmit) != nullptr);
}

TEST_CASE("R8 两步互不含对方专属控件", "[register]")
{
    auto fill = BuildRegisterBody(kRegStepFillInfo);
    auto done = BuildRegisterBody(kRegStepDone);
    REQUIRE(fill != nullptr);
    REQUIRE(done != nullptr);

    // 填写页不应出现完成页的按钮与结果文字。
    CHECK(fill->FindCtrlById(kRegIdBtnLogin)    == nullptr);
    CHECK(fill->FindCtrlById(kRegIdTextAccount) == nullptr);

    // 完成页不应再出现任何输入框与提交按钮。
    CHECK(done->FindCtrlById(kRegIdEditNick)    == nullptr);
    CHECK(done->FindCtrlById(kRegIdComboCompany) == nullptr);
    CHECK(done->FindCtrlById(kRegIdBtnSubmit)   == nullptr);
}

// =====================================================================
// R9-R20 RegisterModel 数据模型契约
// =====================================================================

TEST_CASE("R9 RegisterModel 默认处于步骤 1、公司名为默认公司、其余字段为空", "[register][model]")
{
    RegisterModel model;
    CHECK(model.CurrentStep() == kRegStepFillInfo);
    CHECK(model.Nickname().IsEmpty());
    CHECK(model.Password().IsEmpty());
    CHECK(model.Phone().IsEmpty());
    // 公司名默认填好默认公司，用户不改就注册进默认公司
    CHECK(model.Company() == RegDefaultCompany());
}

TEST_CASE("R10 RegisterModel 步骤推进 / 回退到边界后不越界", "[register][model]")
{
    RegisterModel model;

    CHECK(model.GoNextStep() == kRegStepDone);
    // 已在最后一步 —— 再推进保持不变。
    CHECK(model.GoNextStep() == kRegStepDone);

    CHECK(model.GoPrevStep() == kRegStepFillInfo);
    // 已在第一步 —— 再回退保持不变。
    CHECK(model.GoPrevStep() == kRegStepFillInfo);
}

TEST_CASE("R11 RegisterModel.SetCurrentStep 越界被忽略", "[register][model]")
{
    RegisterModel model;
    model.SetCurrentStep(kRegStepDone);
    CHECK(model.CurrentStep() == kRegStepDone);

    model.SetCurrentStep(0);    // 越界 —— 应被忽略
    CHECK(model.CurrentStep() == kRegStepDone);

    model.SetCurrentStep(99);   // 越界 —— 应被忽略
    CHECK(model.CurrentStep() == kRegStepDone);
}

TEST_CASE("R12 RegisterModel 字段读写互不串扰", "[register][model]")
{
    RegisterModel model = MakeValidModel();

    CHECK(model.Nickname()        == CString(_T("火烈鸟")));
    CHECK(model.Password()        == CString(_T("pass1234")));
    CHECK(model.ConfirmPassword() == CString(_T("pass1234")));
    CHECK(model.Phone()           == CString(_T("13800138000")));
    CHECK(model.Company()         == CString(_T("明远科技有限公司")));
}

TEST_CASE("R13 ValidateForm 合法输入通过", "[register][model][validate]")
{
    RegisterModel model = MakeValidModel();
    CHECK(model.ValidateForm() == kRegValidOk);
}

TEST_CASE("R14 ValidateForm 昵称为空 / 仅空白 被拒", "[register][model][validate]")
{
    RegisterModel model = MakeValidModel();

    // 昵称完全为空。
    model.SetNickname(_T(""));
    CHECK(model.ValidateForm() == kRegValidNickEmpty);

    // 昵称仅空白字符 —— Trim 后为空，同样被拒。
    model.SetNickname(_T("   "));
    CHECK(model.ValidateForm() == kRegValidNickEmpty);
}

TEST_CASE("R15 填写页没有账号输入框，模型也没有账号字段",
          "[register][model][validate]")
{
    // 登录账号由服务端按用户ID自动生成，界面上不该给用户任何输入的机会 ——
    // 一旦这里回归（有人又加回一个账号框），下面两条断言会立刻失败。
    auto root = BuildRegisterBody(kRegStepFillInfo);
    REQUIRE(root != nullptr);

    // 811 是历史上"自定义账号"输入框的 id，现在必须找不到任何控件。
    CHECK(root->FindCtrlById(811) == nullptr);

    // 填写页的输入恰好是五项：昵称 / 密码 / 确认密码 / 手机号四个输入框，加上公司名称组合框。
    const int kExpectedEditCount = 4;
    int editCount = 0;
    const UINT kEditIds[] = { kRegIdEditNick, kRegIdEditPwd, kRegIdEditPwd2, kRegIdEditPhone };
    for (size_t i = 0; i < sizeof(kEditIds) / sizeof(kEditIds[0]); ++i)
    {
        if (dynamic_cast<DuiEdit*>(root->FindCtrlById(kEditIds[i])) != nullptr)
        {
            ++editCount;
        }
    }
    CHECK(editCount == kExpectedEditCount);
    CHECK(dynamic_cast<DuiComboBox*>(root->FindCtrlById(kRegIdComboCompany)) != nullptr);
}

TEST_CASE("R16 ValidateForm 密码为空 / 过短 / 两次不一致均被拒",
          "[register][model][validate]")
{
    RegisterModel model = MakeValidModel();

    // 密码为空。
    model.SetPassword(_T(""));
    model.SetConfirmPassword(_T(""));
    CHECK(model.ValidateForm() == kRegValidPwdEmpty);

    // 密码过短（5 位 < 6）。
    model.SetPassword(_T("p1234"));
    model.SetConfirmPassword(_T("p1234"));
    CHECK(model.ValidateForm() == kRegValidPwdTooShort);

    // 两次密码不一致。
    model.SetPassword(_T("pass1234"));
    model.SetConfirmPassword(_T("pass9999"));
    CHECK(model.ValidateForm() == kRegValidPwdMismatch);
}

TEST_CASE("R17 ValidateForm 手机号格式非法被拒", "[register][model][validate]")
{
    RegisterModel model = MakeValidModel();

    // 位数不足。
    model.SetPhone(_T("138001380"));
    CHECK(model.ValidateForm() == kRegValidPhoneFormat);

    // 首位不是 1。
    model.SetPhone(_T("23800138000"));
    CHECK(model.ValidateForm() == kRegValidPhoneFormat);

    // 含非数字字符。
    model.SetPhone(_T("1380013800a"));
    CHECK(model.ValidateForm() == kRegValidPhoneFormat);

    // 为空。
    model.SetPhone(_T(""));
    CHECK(model.ValidateForm() == kRegValidPhoneFormat);
}

TEST_CASE("R18 ValidateForm 公司名为空 / 仅空白 被拒", "[register][model][validate]")
{
    RegisterModel model = MakeValidModel();

    model.SetCompany(_T(""));
    CHECK(model.ValidateForm() == kRegValidCompanyEmpty);

    // 仅空白字符 —— 与服务端"去掉首尾空白后再匹配"的口径一致，判为空。
    model.SetCompany(_T("   "));
    CHECK(model.ValidateForm() == kRegValidCompanyEmpty);

    // 名字两侧带空白但本体非空 —— 合法，交给服务端去做精确匹配。
    model.SetCompany(_T("  明远科技有限公司  "));
    CHECK(model.ValidateForm() == kRegValidOk);
}

TEST_CASE("R19 ValidateForm 公司名超长被拒", "[register][model][validate]")
{
    RegisterModel model = MakeValidModel();

    // 恰好等于上限 —— 合法。
    model.SetCompany(CString(_T('a'), kRegCompanyMaxLen));
    CHECK(model.ValidateForm() == kRegValidOk);

    // 超上限一位 —— 服务端 t_enterprise.f_name 存不下，必然匹配不到企业，
    // 与其发上去等一个"公司不存在"，不如本地就拦住。
    model.SetCompany(CString(_T('a'), kRegCompanyMaxLen + 1));
    CHECK(model.ValidateForm() == kRegValidCompanyTooLong);
}

TEST_CASE("R20 ValidateMessage 对每个校验结果都给出非空中文提示",
          "[register][model][validate]")
{
    // 逐个结果码都要有能直接展示给用户的文案，不能落到"输入有误"兜底上。
    const int kAllResults[] = {
        kRegValidNickEmpty,
        kRegValidPwdEmpty, kRegValidPwdTooShort, kRegValidPwdMismatch,
        kRegValidPhoneFormat, kRegValidCompanyEmpty, kRegValidCompanyTooLong,
    };
    const CString fallback = RegisterModel::ValidateMessage(-1);

    for (size_t i = 0; i < sizeof(kAllResults) / sizeof(kAllResults[0]); ++i)
    {
        const CString msg = RegisterModel::ValidateMessage(kAllResults[i]);
        CHECK_FALSE(msg.IsEmpty());
        CHECK(msg != fallback);
    }
}

// =====================================================================
// R21-R24 cmd_register 报文编解码
// =====================================================================

TEST_CASE("R21 buildRegisterRequest 带上公司名、且不再发 smscode", "[register][protocol]")
{
    net::RegisterRequestParams params;
    params.nickname = "\xE6\x9E\x97\xE7\x9F\xA5\xE5\xA4\x8F";   // UTF-8 的"林知夏"
    params.password = "0123456789abcdef";
    params.phone    = "13917043329";
    params.company  = "ACME";

    const std::string body = net::RegisterProtocolClient::buildRegisterRequest(params);

    CHECK(body.find("\"company\":\"ACME\"")     != std::string::npos);
    // 登录账号由服务端生成，请求里不该出现这个字段。
    CHECK(body.find("username") == std::string::npos);
    CHECK(body.find("\"phone\":\"13917043329\"")  != std::string::npos);
    // 注册已不收短信验证码，报文里不该再出现这个字段。
    CHECK(body.find("smscode") == std::string::npos);
}

TEST_CASE("R22 parseRegisterResponse 解析成功应答含企业归属", "[register][protocol]")
{
    const std::string json =
        "{\"code\":0,\"msg\":\"ok\",\"userid\":\"88234589\",\"username\":\"EMP88234589\","
        "\"enterpriseid\":\"1\",\"enterprisename\":\"ACME\"}";

    net::RegisterResponse resp;
    REQUIRE(net::RegisterProtocolClient::parseRegisterResponse(json, resp));
    CHECK(resp.code == 0);
    // userid / enterpriseid 在报文里是字符串，须还原成整型
    CHECK(resp.userid == 88234589);
    CHECK(resp.enterpriseid == 1);
    CHECK(resp.username == "EMP88234589");
    CHECK(resp.enterprisename == "ACME");
}

TEST_CASE("R23 parseRegisterResponse 解析失败应答只取 code 与 msg", "[register][protocol]")
{
    // 407 = error_code_enterprise_not_exist，公司全称匹配不到企业。
    const std::string json = "{\"code\":407,\"msg\":\"company not found\"}";

    net::RegisterResponse resp;
    REQUIRE(net::RegisterProtocolClient::parseRegisterResponse(json, resp));
    CHECK(resp.code == 407);
    CHECK(resp.msg == "company not found");
    // 失败应答里没有这些字段，须保持构造后的默认值
    CHECK(resp.userid == 0);
    CHECK(resp.enterpriseid == 0);
    CHECK(resp.enterprisename.empty());
}

TEST_CASE("R24 parseRegisterResponse 拒绝非法应答", "[register][protocol]")
{
    net::RegisterResponse resp;
    CHECK_FALSE(net::RegisterProtocolClient::parseRegisterResponse("not a json", resp));
    CHECK_FALSE(net::RegisterProtocolClient::parseRegisterResponse("{}", resp));
    // code 不是整型
    CHECK_FALSE(net::RegisterProtocolClient::parseRegisterResponse("{\"code\":\"0\"}", resp));
    // 解析失败时 code 应保持 -1，不能被误判为成功
    CHECK(resp.code == -1);
}

// R25：窗口按设计尺寸设置后（2026-10-02 起 ResizeClient 即整窗尺寸），扣掉标题栏剩下的内容区
// 要完整容下两步各自最底下的主按钮：按钮没有被压扁，下方留白也与空间充裕时相同（没有占用
// 容器的下内边距）。
TEST_CASE("R25 两步在窗口设计尺寸扣掉标题栏后都排得下底部主按钮", "[register][layoutfit]")
{
    const int contentH = kRegWindowHeight - kRegWindowTitleBarH;
    const int steps[]  = { kRegStepFillInfo, kRegStepDone };
    const UINT btns[]  = { (UINT)kRegIdBtnSubmit, (UINT)kRegIdBtnLogin };
    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i)
    {
        INFO("step = " << steps[i]);
        std::unique_ptr<DuiControl> root = BuildRegisterBody(steps[i]);
        REQUIRE(root != nullptr);
        const layoutfit::FitResult r = layoutfit::MeasureAt(root.get(), btns[i], kRegWindowWidth, contentH);
        REQUIRE(r.m_found);
        INFO("button bottom = " << r.m_bottom << ", content height = " << contentH
             << ", height = " << r.m_height << ", roomy height = " << r.m_roomyHeight
             << ", bottom gap = " << r.m_bottomGap << ", roomy bottom gap = " << r.m_roomyBottomGap);
        CHECK(r.m_bottom <= contentH);
        CHECK(r.m_height == r.m_roomyHeight);
        CHECK(r.m_bottomGap == r.m_roomyBottomGap);
    }
}


// =====================================================================
// RG1-RG4 公司名称组合框与默认公司
// =====================================================================

TEST_CASE("RG1 公司名称是可编辑组合框，下拉只有默认公司一项，且默认显示它", "[register][company]")
{
    //与服务端 DEFAULT_ENTERPRISE_NAME 的约定：两边的缺省值相同，chatserver 启动时保证库里有这家企业
    CHECK(std::wstring(brandtext::kDefaultCompany) == L"FlamingoIM.org");

    auto root = BuildRegisterBody(kRegStepFillInfo);
    REQUIRE(root != nullptr);
    DuiComboBox* combo = dynamic_cast<DuiComboBox*>(root->FindCtrlById(kRegIdComboCompany));
    REQUIRE(combo != nullptr);

    CHECK(combo->IsEditable());
    REQUIRE(combo->GetCount() == 1);
    CHECK(combo->GetItemText(0) == RegDefaultCompany());
    CHECK(combo->GetText() == RegDefaultCompany());
    //只有一个默认选项，不提供删除叉
    CHECK_FALSE(combo->IsShowItemDelete());
}

TEST_CASE("RG2 在组合框里输入别的公司名，读回的是输入的内容，校验通过", "[register][company]")
{
    auto root = BuildRegisterBody(kRegStepFillInfo);
    REQUIRE(root != nullptr);
    DuiComboBox* combo = dynamic_cast<DuiComboBox*>(root->FindCtrlById(kRegIdComboCompany));
    REQUIRE(combo != nullptr);

    combo->SetText(_T("明远科技有限公司"));
    CHECK(combo->GetText() == CString(_T("明远科技有限公司")));

    RegisterModel model = MakeValidModel();
    model.SetCompany(combo->GetText());
    CHECK(model.Company() == CString(_T("明远科技有限公司")));
    CHECK(model.ValidateForm() == kRegValidOk);
}

TEST_CASE("RG3 清空组合框后校验仍提示公司名称为空", "[register][company]")
{
    auto root = BuildRegisterBody(kRegStepFillInfo);
    REQUIRE(root != nullptr);
    DuiComboBox* combo = dynamic_cast<DuiComboBox*>(root->FindCtrlById(kRegIdComboCompany));
    REQUIRE(combo != nullptr);

    combo->SetText(_T(""));
    RegisterModel model = MakeValidModel();
    model.SetCompany(combo->GetText());
    CHECK(model.ValidateForm() == kRegValidCompanyEmpty);
}

TEST_CASE("RG4 组合框与其它输入框同高，窗口按设计尺寸仍排得下", "[register][company][layoutfit]")
{
    auto root = BuildRegisterBody(kRegStepFillInfo);
    REQUIRE(root != nullptr);

    //MeasureAt 最后一次按设计尺寸排版，之后读到的控件矩形就是设计尺寸下的结果
    const int contentH = kRegWindowHeight - kRegWindowTitleBarH;
    const layoutfit::FitResult r = layoutfit::MeasureAt(root.get(), kRegIdBtnSubmit, kRegWindowWidth, contentH);
    REQUIRE(r.m_found);
    CHECK(r.m_bottom <= contentH);
    CHECK(r.m_height == r.m_roomyHeight);
    CHECK(r.m_bottomGap == r.m_roomyBottomGap);

    DuiControl* combo = root->FindCtrlById(kRegIdComboCompany);
    DuiControl* phone = root->FindCtrlById(kRegIdEditPhone);
    REQUIRE(combo != nullptr);
    REQUIRE(phone != nullptr);
    const RECT rcCombo = combo->GetRect();
    const RECT rcPhone = phone->GetRect();
    INFO("combo = [" << rcCombo.top << ", " << rcCombo.bottom << "], phone = ["
         << rcPhone.top << ", " << rcPhone.bottom << "]");
    CHECK(rcCombo.bottom - rcCombo.top == rcPhone.bottom - rcPhone.top);
    CHECK(rcCombo.top > rcPhone.bottom);
}

TEST_CASE("RG5 企业关闭了自助注册（410）时给出专门的提示，不落到通用的「注册失败」", "[register][company]")
{
    CHECK(static_cast<int>(error_code_register_closed) == 410);

    const CString closed = RegisterModel::ServerErrorMessage(error_code_register_closed);
    CHECK(closed == CString(L"这家公司已关闭自助注册，请联系公司管理员为你开通账号。"));
    //与通用提示、企业不可用的提示都不同，用户能看出该找管理员开账号
    CHECK(closed != CString(RegisterModel::ServerErrorMessage(-1)));
    CHECK(closed != CString(RegisterModel::ServerErrorMessage(error_code_enterprise_disabled)));
}

TEST_CASE("RG6 服务端建号失败（100）时提示稍后重试，不再提早已不存在的「自定义账号」", "[register][company]")
{
    const CString failed = RegisterModel::ServerErrorMessage(error_code_register_fail);
    CHECK(failed == CString(L"注册失败，请稍后重试。"));
    CHECK(failed.Find(L"自定义账号") < 0);
}

// =====================================================================
// DC3 默认公司取 flamingo.ini 的 [ui] defaultcompany
// =====================================================================

namespace {

// 在作用域内临时改写 g_strDefaultCompany（模拟启动时读到的配置值），离开作用域时写回原值；
// 用例中途断言失败抛出异常时同样会写回，不影响后续用例。
class ScopedDefaultCompany
{
public:
    // 保存原值并改写。value：本作用域内使用的配置值，空串表示尚未读取配置。
    explicit ScopedDefaultCompany(const std::wstring& value)
        : m_saved(g_strDefaultCompany)
    {
        g_strDefaultCompany = value;
    }

    // 写回原值。
    ~ScopedDefaultCompany()
    {
        g_strDefaultCompany = m_saved;
    }

private:
    std::wstring m_saved;   // 改写前的原值，析构时写回
};

} // namespace

TEST_CASE("DC3 注册模型的初始公司名与组合框的默认项取 [ui] defaultcompany 的配置值", "[register][company][brand]")
{
    //尚未读取配置（单元测试不走启动流程）时取内置的 FlamingoIM.org
    {
        ScopedDefaultCompany unread(L"");
        CHECK(RegDefaultCompany() == CString(L"FlamingoIM.org"));
        RegisterModel model;
        CHECK(model.Company() == CString(L"FlamingoIM.org"));
    }

    //配置成别的名称（含中文）时，模型与组合框都取配置值，下拉里仍只有这一项
    const CString configured(L"火烈鸟科技有限公司");
    ScopedDefaultCompany scoped(std::wstring(configured.GetString()));
    CHECK(RegDefaultCompany() == configured);

    RegisterModel model;
    CHECK(model.Company() == configured);

    auto root = BuildRegisterBody(kRegStepFillInfo);
    REQUIRE(root != nullptr);
    DuiComboBox* combo = dynamic_cast<DuiComboBox*>(root->FindCtrlById(kRegIdComboCompany));
    REQUIRE(combo != nullptr);
    REQUIRE(combo->GetCount() == 1);
    CHECK(combo->GetItemText(0) == configured);
    CHECK(combo->GetText() == configured);
}
