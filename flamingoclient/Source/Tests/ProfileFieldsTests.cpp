/**
 *  联系人资料字段扩充（docs/联系人资料字段扩充方案.md）客户端单元测试，前缀 PFX。
 *
 *  第 2a 步（数据层）：
 *    - PFX1  通讯录列表 contacts[] 的性别、生日、手机号、邮箱、座机、岗位读进 ContactItem，缺字段取默认；
 *    - PFX2  1108 资料变更推送解析全部字段，applyUserInfoChanged 把它们合并进通讯录缓存；
 *    - PFX3  登录应答的座机、岗位经登录结果写进本人资料所用的字段；
 *    - PFX3b 登录应答里的长昵称、长签名完整写进登录结果（此前固定长度数组 + strcpy_s，超长即终止程序）；
 *    - PFX4  性别编码与界面文字、下拉框序号的对应，英文界面显示译文；
 *    - PFX5  改资料请求只带改动过的字段，可带座机与岗位，从不带手机号。
 *  第 2b 步（本人资料对话框）：
 *    - PFX6～PFX8 已删除：2026-10-07 本人资料对话框 CBalloonProfileDlg 删除，本人资料改由联系人资料窗承担；
 *    - PFX14 本地校验（纯函数部分，联系人资料窗就地编辑使用）：长度、邮箱、座机、生日。
 *  第 2c 步（联系人资料窗的「个人资料」区块）：
 *    - PFX9  看别人：宽 600；只列出填了的字段、全部只读，后面的格子前移；多于两行时默认收起；全空时区块不显示；
 *    - PFX10 看本人：九格都显示（2026-10-07 加了第一格昵称），没填的显示「未填写」，性别未设置显示「保密」；
 *            八格有编辑按钮，手机号没有；不收起；
 *    - PFX11 两列排法：昵称、签名各占整行，其余两列；各格互不重叠、不越出客户区；
 *    - PFX12 高度随显示行数变化；收起时窗口高度有上限，按该高度排得下底部按钮。
 *  第 2d-1 步（看本人的视图与入口）：
 *    - PFX18 看本人：通话、发消息、标签、备注分组、提醒收藏都不显示，历史消息、组织架构与关闭在；窗口相应变矮、排得下；
 *    - PFX19 本人资料的入口：2026-10-07 起头部昵称区、系统菜单、通讯录双击、右键「查看资料」、搜索选中都打开联系人资料窗。
 *  第 2d-2 步（看本人时就地编辑）：
 *    - PFX13  进入 / 退出编辑只影响当前格，性别用下拉框；提交判定：没改动、有改动、不合格（回车、Esc、失焦的
 *             事件接线在窗口里，第 3 步联调时人工核对）；
 *    - PFX14b 就地编辑的不合格情况：生日格式不对、日期不存在、邮箱缺「@」、座机含不允许的字符；
 *    - PFX15  失败回滚只恢复这一次提交过的项（主面板对本人资料 CBuddyInfo 用的是同一种做法，这里以资料模型验证）；
 *    - PFX16  进入 / 退出编辑后重排，输入框与值都拿到正确的矩形（UI 约定第 5 条）。
 *  第 3 步（多语言）：
 *    - PFX17  英文界面下字段名、「未填写」「保密」「展开全部」为译文，英文字段名在格子里放得下。
 *  本人那一行的同步（2026-10-04，服务端起也把资料变更推送发给本人）：
 *    - PFX20  通讯录树上本人那一行带「（我）」，别人不带；英文界面下为译文。收到本人推送后的刷新在主面板里，
 *             以联调实测（方案第 8 节）。
 *  手机号、座机对同事的公开范围（2026-10-06，方案第 9 节）：
 *    - PFX21  登录应答的两个公开范围经登录结果带给本人资料，缺字段时手机号不公开、座机公开；
 *    - PFX22  改资料请求只在公开范围改变时带 phonevisible / landlinevisible，从不带 phonenumber；失败回滚恢复原设置；
 *    - PFX23  格子数据：看本人时只有手机号、座机两格带开关，文字随取值；看别人时没有开关；控件树里开关按格子显隐；
 *    - PFX24  公开范围菜单：两项、当前项打勾；选当前项或取消不提交，选另一项只提交这一项；
 *    - PFX25  同事一侧：1108 推送里号码为空串时缓存被覆盖，资料窗里这一格消失；再推送带号码时重新出现；
 *    - PFX26  联系人资料窗里的开关与字段名、编辑按钮互不重叠，不越出格子，没有被压窄；中英文都成立；
 *    - PFX27、PFX28 已删除：随本人资料对话框一起删除（2026-10-07），开关在资料窗里的排版由 PFX26 覆盖；
 *    - PFX29  两条新文字在四个 .po 里都有译文；英文界面下开关显示译文。
 *  本人资料统一到联系人资料窗（2026-10-07，方案第 10 节）：
 *    - PFX30  生日文字的解析（由本人资料对话框的 B1～B5 改写，改测资料窗就地编辑用的 profilefields::ParseBirthdayText）。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "../net/ContactsProtocolClient.h"
#include "../net/LoginProtocolClient.h"
#include "../net/ProfileProtocolClient.h"
#include "../net/LoginResultFill.h"
#include "../net/IUProtocolData.h"          // CLoginResult
#include "../net/Protocol.h"                // enum profile_gender
#include "../Session/ProfileFields.h"
#include "../Common/I18n/I18n.h"            // PFX4：模拟英文界面
#include "../third-party/jsoncpp-1.9.0/json.h"
#include "LayoutFitCheck.h"                          // PFX12、PFX18：按设计尺寸排得下
#include "../UI/BalloonContact/ContactLayout.h"       // PFX9～PFX12：联系人资料窗
#include "../UI/BalloonContact/ContactModel.h"
#include "../UI/BalloonMain/ContactRemarkModel.h"     // PFX20：AppendSelfMark
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/DuiResMgr.h"   // PFX26、PFX28：标签没设字体时按默认字体量宽

namespace {

//把一段文字重复 n 次，用来造长度到上限的资料
std::string RepeatText(const std::string& unit, int n)
{
    std::string s;
    for (int i = 0; i < n; ++i)
    {
        s += unit;
    }
    return s;
}

//把 JSON 文本解析成 Json::Value，供 PFX5 检查请求里出现了哪些键
bool ParsePfxJson(const std::string& text, Json::Value& root)
{
    Json::CharReaderBuilder b;
    JSONCPP_STRING errs;
    Json::CharReader* reader = b.newCharReader();
    const bool ok = reader->parse(text.c_str(), text.c_str() + text.size(), &root, &errs);
    delete reader;
    return ok && errs.empty();
}

//PFX4 用的宿主查找函数：只认识三个性别选项，其余返回 NULL（表示没有译文）
const wchar_t* FakePfxGenderHost(const wchar_t* context, const wchar_t* source)
{
    if (context != NULL)
    {
        return NULL;
    }
    if (wcscmp(source, L"男") == 0)
    {
        return L"Male";
    }
    if (wcscmp(source, L"女") == 0)
    {
        return L"Female";
    }
    if (wcscmp(source, L"保密") == 0)
    {
        return L"Prefer not to say";
    }
    return NULL;
}

//用例前后把 i18n 模块恢复为未初始化（简体中文），不影响其它用例
struct ScopedPfxI18nReset
{
    ScopedPfxI18nReset()
    {
        i18n::ResetForTest();
    }
    ~ScopedPfxI18nReset()
    {
        i18n::ResetForTest();
    }
};

} // namespace

TEST_CASE("PFX1 通讯录列表：性别、生日、手机号、邮箱、座机、岗位读进 ContactItem，缺字段取默认", "[profilefields]")
{
    const std::string body = R"({"code":0,"msg":"ok","contacts":[
        {"userid":"100001","nickname":"a","gender":2,"birthday":19960812,"phonenumber":"13800000001",
         "mail":"a@b.c","landline":"010-8888 6666","post":"交互设计师"},
        {"userid":"100002","nickname":"b"}]})";
    net::ContactsResult result;
    REQUIRE(net::ContactsProtocolClient::parseContactsResponse(body, result));
    REQUIRE(result.m_contacts.size() == 2);

    const net::ContactItem& filled = result.m_contacts[0];
    CHECK(filled.m_gender == profile_gender_female);
    CHECK(filled.m_birthday == 19960812);
    CHECK(filled.m_phonenumber == "13800000001");
    CHECK(filled.m_mail == "a@b.c");
    CHECK(filled.m_landline == "010-8888 6666");
    CHECK(filled.m_post == "交互设计师");

    //老服务端没有这些字段：整数为 0、文字为空串
    const net::ContactItem& blank = result.m_contacts[1];
    CHECK(blank.m_gender == profile_gender_unset);
    CHECK(blank.m_birthday == 0);
    CHECK(blank.m_phonenumber.empty());
    CHECK(blank.m_mail.empty());
    CHECK(blank.m_landline.empty());
    CHECK(blank.m_post.empty());
}

TEST_CASE("PFX2 1108 推送解析全部资料字段，合并进通讯录缓存时备注、标签、在线状态不变", "[profilefields]")
{
    const std::string body = R"({"userid":"100001","profile":{"nickname":"新名字","facetype":1,
        "customface":"L|2","signature":"","gender":1,"birthday":20000229,"address":"杭州",
        "phonenumber":"13900000000","mail":"x@y.z","landline":"0571-1234","post":"PM"}})";
    net::UserInfoChangedNotify notify;
    REQUIRE(net::ContactsProtocolClient::parseUserInfoChangedNotify(body, notify));
    CHECK(notify.m_userid == 100001);
    CHECK(notify.m_hasProfile);
    CHECK(notify.m_gender == profile_gender_male);
    CHECK(notify.m_birthday == 20000229);
    CHECK(notify.m_address == "杭州");
    CHECK(notify.m_phonenumber == "13900000000");
    CHECK(notify.m_mail == "x@y.z");
    CHECK(notify.m_landline == "0571-1234");
    CHECK(notify.m_post == "PM");

    net::ContactItem contact;
    contact.m_userid = 100001;
    contact.m_username = "u100001";
    contact.m_nickname = "旧名字";
    contact.m_signature = "旧签名";
    contact.m_address = "上海";
    contact.m_landline = "old";
    contact.m_post = "old";
    contact.m_remark = "备注";
    contact.m_tags.push_back("同事");
    contact.m_presence = 1;
    net::ContactsProtocolClient::applyUserInfoChanged(notify, contact);
    CHECK(contact.m_nickname == "新名字");
    CHECK(contact.m_facetype == 1);
    CHECK(contact.m_customface == "L|2");
    //签名被清空：推送带的是完整资料，空串也要覆盖
    CHECK(contact.m_signature.empty());
    CHECK(contact.m_gender == profile_gender_male);
    CHECK(contact.m_birthday == 20000229);
    CHECK(contact.m_address == "杭州");
    CHECK(contact.m_phonenumber == "13900000000");
    CHECK(contact.m_mail == "x@y.z");
    CHECK(contact.m_landline == "0571-1234");
    CHECK(contact.m_post == "PM");
    //不在推送合并范围内的字段保持原值
    CHECK(contact.m_username == "u100001");
    CHECK(contact.m_remark == "备注");
    REQUIRE(contact.m_tags.size() == 1);
    CHECK(contact.m_tags[0] == "同事");
    CHECK(contact.m_presence == 1);

    //推送里昵称为空：保留原昵称，显示名不变成空白
    net::UserInfoChangedNotify emptyNick = notify;
    emptyNick.m_nickname.clear();
    emptyNick.m_post = "Lead";
    net::ContactsProtocolClient::applyUserInfoChanged(emptyNick, contact);
    CHECK(contact.m_nickname == "新名字");
    CHECK(contact.m_post == "Lead");

    //没有 profile 对象的推送：解析成功但什么都不改
    net::UserInfoChangedNotify noProfile;
    REQUIRE(net::ContactsProtocolClient::parseUserInfoChangedNotify(R"({"userid":"100001"})", noProfile));
    CHECK_FALSE(noProfile.m_hasProfile);
    net::ContactsProtocolClient::applyUserInfoChanged(noProfile, contact);
    CHECK(contact.m_nickname == "新名字");
    CHECK(contact.m_address == "杭州");
    CHECK(contact.m_post == "Lead");
}

TEST_CASE("PFX3b 登录应答里的长昵称、长签名完整写进登录结果，不截断、不终止程序", "[profilefields][loginfill]")
{
    //服务端允许的上限：昵称 64 字、签名 / 所在地 / 邮箱 256 字（按 UTF-8 字符数）。
    //昵称末尾放一个 4 字节字符（U+1F600），确认不会被截成半个
    const std::string kEmoji = "\xF0\x9F\x98\x80";
    net::LoginResponse resp;
    resp.code = 0;
    resp.userid = 8;
    resp.profile.username = "88234561";
    resp.profile.nickname = RepeatText("昵", 63) + kEmoji;
    resp.profile.signature = RepeatText("签", 256);
    resp.profile.address = RepeatText("址", 256);
    resp.profile.mail = RepeatText("m", 250) + "@b.cn";
    resp.profile.customface = "L|3";
    resp.profile.phonenumber = "13800000008";

    CLoginResult out;
    net::FillLoginResultFromResponse(resp, out);
    CHECK(out.m_uAccountID == 8);
    CHECK(out.m_strAccountName == "88234561");
    CHECK(out.m_strNickName == resp.profile.nickname);
    CHECK(out.m_strSignature == resp.profile.signature);
    CHECK(out.m_strAddress == resp.profile.address);
    CHECK(out.m_strMail == resp.profile.mail);
    CHECK(out.m_strCustomFace == "L|3");
    CHECK(out.m_strPhoneNumber == "13800000008");
}

TEST_CASE("PFX3 登录应答的座机、岗位：解析后经登录结果带给本人资料", "[profilefields][loginfill]")
{
    const std::string body = R"({"code":0,"msg":"ok","userid":"8","servertime":1,"profile":{
        "usertype":0,"username":"88234561","nickname":"林知夏","gender":2,"birthday":19960812,
        "phonenumber":"13800000008","mail":"a@b.c","landline":"021-6666 8888","post":"交互设计师",
        "enterpriseid":"5001"},"features":{"fileshare":1}})";
    net::LoginResponse resp;
    REQUIRE(net::LoginProtocolClient::parseLoginResponse(body, resp));
    REQUIRE(resp.code == 0);
    CHECK(resp.profile.landline == "021-6666 8888");
    CHECK(resp.profile.post == "交互设计师");

    CLoginResult out;
    net::FillLoginResultFromResponse(resp, out);
    CHECK(out.m_strLandline == "021-6666 8888");
    CHECK(out.m_strPost == "交互设计师");
    CHECK(out.m_nGender == profile_gender_female);
    CHECK(out.m_nBirthday == 19960812);
    CHECK(out.m_nFileShareFeature == 1);

    //老服务端不下发这两个字段：解析为空串
    net::LoginResponse oldResp;
    REQUIRE(net::LoginProtocolClient::parseLoginResponse(
        R"({"code":0,"msg":"ok","userid":"8","servertime":1,"profile":{"nickname":"x"}})", oldResp));
    CHECK(oldResp.profile.landline.empty());
    CHECK(oldResp.profile.post.empty());
}

TEST_CASE("PFX4 性别：0/1/2 对应保密/男/女，其它值按保密；下拉框序号与编码互逆；英文界面显示译文", "[profilefields]")
{
    ScopedPfxI18nReset reset;

    CHECK(std::wstring(profilefields::GenderText(profile_gender_unset)) == L"保密");
    CHECK(std::wstring(profilefields::GenderText(profile_gender_male)) == L"男");
    CHECK(std::wstring(profilefields::GenderText(profile_gender_female)) == L"女");
    //不认识的值（越界、负数）按「保密」
    CHECK(std::wstring(profilefields::GenderText(3)) == L"保密");
    CHECK(std::wstring(profilefields::GenderText(-1)) == L"保密");

    //下拉框顺序：保密、男、女，序号与编码相同
    REQUIRE(profilefields::kGenderComboCount == 3);
    CHECK(profilefields::ComboIndexToGender(0) == profile_gender_unset);
    CHECK(profilefields::ComboIndexToGender(1) == profile_gender_male);
    CHECK(profilefields::ComboIndexToGender(2) == profile_gender_female);
    for (int i = 0; i < profilefields::kGenderComboCount; ++i)
    {
        CHECK(profilefields::GenderToComboIndex(profilefields::ComboIndexToGender(i)) == i);
    }
    //不认识的编码选中「保密」；越界序号与没有选中（-1）按未设置
    CHECK(profilefields::GenderToComboIndex(7) == 0);
    CHECK(profilefields::ComboIndexToGender(-1) == profile_gender_unset);
    CHECK(profilefields::ComboIndexToGender(profilefields::kGenderComboCount) == profile_gender_unset);

    //英文界面：显示译文，编码与序号的对应不变
    i18n::InitFromHost(&FakePfxGenderHost, L"en-US", L"Segoe UI");
    REQUIRE_FALSE(i18n::IsSourceLanguage());
    CHECK(std::wstring(profilefields::GenderText(profile_gender_male)) == L"Male");
    CHECK(std::wstring(profilefields::GenderText(profile_gender_female)) == L"Female");
    CHECK(std::wstring(profilefields::GenderText(profile_gender_unset)) == L"Prefer not to say");
    CHECK(profilefields::ComboIndexToGender(1) == profile_gender_male);
}

TEST_CASE("PFX5 改资料请求只带改动过的字段，可带座机与岗位，从不带手机号", "[profilefields]")
{
    net::ProfileEditableValues before;
    before.m_nickname = "Tom";
    before.m_signature = "hi";
    before.m_gender = profile_gender_male;
    before.m_birthday = 19900101;
    before.m_address = "Shanghai";
    before.m_mail = "tom@a.com";
    before.m_landline = "010-1";
    before.m_post = "PM";

    //前后相同：一项都不提交，请求体是 {}
    const net::ProfileUpdateFields same = net::ProfileProtocolClient::diffProfileValues(before, before);
    CHECK(same.IsEmpty());
    CHECK(net::ProfileProtocolClient::buildUpdateProfileRequest(same) == "{}");

    //只改岗位、清空座机：请求里恰好这两项
    net::ProfileEditableValues after = before;
    after.m_post = "Lead";
    after.m_landline = "";
    const net::ProfileUpdateFields two = net::ProfileProtocolClient::diffProfileValues(before, after);
    CHECK_FALSE(two.IsEmpty());
    CHECK(two.m_hasPost);
    CHECK(two.m_hasLandline);
    CHECK_FALSE(two.m_hasNickname);
    CHECK_FALSE(two.m_hasGender);
    Json::Value twoJson;
    REQUIRE(ParsePfxJson(net::ProfileProtocolClient::buildUpdateProfileRequest(two), twoJson));
    CHECK(twoJson.size() == 2);
    CHECK(twoJson["post"].asString() == "Lead");
    CHECK(twoJson.isMember("landline"));
    CHECK(twoJson["landline"].asString().empty());
    CHECK_FALSE(twoJson.isMember("nickname"));
    CHECK_FALSE(twoJson.isMember("phonenumber"));

    //性别改成未设置、生日清空：整数 0 也要出现在请求里
    net::ProfileEditableValues cleared = before;
    cleared.m_gender = profile_gender_unset;
    cleared.m_birthday = 0;
    Json::Value clearedJson;
    REQUIRE(ParsePfxJson(net::ProfileProtocolClient::buildUpdateProfileRequest(
        net::ProfileProtocolClient::diffProfileValues(before, cleared)), clearedJson));
    CHECK(clearedJson.size() == 2);
    CHECK(clearedJson["gender"].asInt() == profile_gender_unset);
    CHECK(clearedJson["birthday"].asInt() == 0);

    //八项全改：八个键，邮箱键名是 mail，没有 phonenumber
    net::ProfileEditableValues all;
    all.m_nickname = "Jerry";
    all.m_signature = "hello";
    all.m_gender = profile_gender_female;
    all.m_birthday = 19950402;
    all.m_address = "Hangzhou";
    all.m_mail = "j@a.com";
    all.m_landline = "0571-2";
    all.m_post = "QA";
    Json::Value allJson;
    REQUIRE(ParsePfxJson(net::ProfileProtocolClient::buildUpdateProfileRequest(
        net::ProfileProtocolClient::diffProfileValues(before, all)), allJson));
    CHECK(allJson.size() == 8);
    CHECK(allJson["mail"].asString() == "j@a.com");
    CHECK_FALSE(allJson.isMember("email"));
    CHECK_FALSE(allJson.isMember("phonenumber"));
}

namespace {

//控件矩形是否非空（宽、高都大于 0）
bool PfxRectNonEmpty(const balloonwjui::DuiControl* c)
{
    if (c == nullptr)
    {
        return false;
    }
    const RECT& rc = c->GetRect();
    return (rc.right - rc.left) > 0 && (rc.bottom - rc.top) > 0;
}

} // namespace

TEST_CASE("PFX14 本地校验：长度、邮箱、座机、生日，只查本次提交的字段", "[profilefields]")
{
    const std::string kHan = "岗";
    net::ProfileUpdateFields f;
    CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_ok);

    //长度：恰好到上限通过，多一个字被拒（中文与英文同算一个字）
    f.m_hasPost = true;
    f.m_values.m_post = RepeatText(kHan, profilefields::kProfileShortTextMaxChars);
    CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_ok);
    f.m_values.m_post += "a";
    CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_post_too_long);
    f.m_hasPost = false;

    f.m_hasNickname = true;
    f.m_values.m_nickname = RepeatText("n", profilefields::kProfileShortTextMaxChars + 1);
    CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_nickname_too_long);
    f.m_hasNickname = false;

    f.m_hasSignature = true;
    f.m_values.m_signature = RepeatText(kHan, profilefields::kProfileLongTextMaxChars);
    CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_ok);
    f.m_values.m_signature += kHan;
    CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_signature_too_long);
    f.m_hasSignature = false;

    f.m_hasAddress = true;
    f.m_values.m_address = RepeatText(kHan, profilefields::kProfileLongTextMaxChars + 1);
    CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_address_too_long);
    f.m_hasAddress = false;

    //没有提交的字段不查：库里遗留的超长旧值不连累本次修改
    f.m_values.m_nickname = RepeatText(kHan, profilefields::kProfileShortTextMaxChars + 10);
    CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_ok);

    //邮箱：只要求含「@」，清空允许
    f.m_hasMail = true;
    f.m_values.m_mail = "a@b";
    CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_ok);
    f.m_values.m_mail = "";
    CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_ok);
    f.m_values.m_mail = "tom.a.com";
    CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_mail_invalid);
    f.m_values.m_mail = RepeatText("m", profilefields::kProfileLongTextMaxChars) + "@";
    CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_mail_too_long);
    f.m_hasMail = false;

    //座机：数字、空格与 + - ( )，其它字符（含全角数字）被拒
    f.m_hasLandline = true;
    const char* const kGoodLandlines[] = { "", "010-8888 6666", "+86 (10) 8888-6666", "13800000000" };
    for (size_t i = 0; i < sizeof(kGoodLandlines) / sizeof(kGoodLandlines[0]); ++i)
    {
        CAPTURE(kGoodLandlines[i]);
        f.m_values.m_landline = kGoodLandlines[i];
        CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_ok);
    }
    const char* const kBadLandlines[] = { "010#8888", "ext.12", "０１０", "010/8888" };
    for (size_t i = 0; i < sizeof(kBadLandlines) / sizeof(kBadLandlines[0]); ++i)
    {
        CAPTURE(kBadLandlines[i]);
        f.m_values.m_landline = kBadLandlines[i];
        CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_landline_invalid);
    }
    f.m_values.m_landline = RepeatText("1", profilefields::kProfileShortTextMaxChars + 1);
    CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_landline_too_long);
    f.m_hasLandline = false;

    //生日：0 与存在的日期通过；2 月 30 日、1900 年 2 月 29 日、年份越界被拒（与服务端同口径）
    f.m_hasBirthday = true;
    const int kGoodBirthdays[] = { 0, 19960812, 20000229, 19000101, 21001231 };
    for (size_t i = 0; i < sizeof(kGoodBirthdays) / sizeof(kGoodBirthdays[0]); ++i)
    {
        CAPTURE(kGoodBirthdays[i]);
        f.m_values.m_birthday = kGoodBirthdays[i];
        CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_ok);
    }
    const int kBadBirthdays[] = { 20260230, 19000229, 18991231, 21010101, 20261301 };
    for (size_t i = 0; i < sizeof(kBadBirthdays) / sizeof(kBadBirthdays[0]); ++i)
    {
        CAPTURE(kBadBirthdays[i]);
        f.m_values.m_birthday = kBadBirthdays[i];
        CHECK(profilefields::ValidateProfileUpdate(f) == profilefields::profile_edit_birthday_invalid);
    }

    //提示文字：合格时为空；带长度上限的提示写明字数
    CHECK(profilefields::ProfileEditErrorText(profilefields::profile_edit_ok).empty());
    CHECK(profilefields::ProfileEditErrorText(profilefields::profile_edit_post_too_long).find(L"64") != std::wstring::npos);
    CHECK(profilefields::ProfileEditErrorText(profilefields::profile_edit_signature_too_long).find(L"256") != std::wstring::npos);
    CHECK_FALSE(profilefields::ProfileEditErrorText(profilefields::profile_edit_landline_invalid).empty());
    CHECK_FALSE(profilefields::ProfileEditErrorText(profilefields::profile_edit_mail_invalid).empty());
    CHECK_FALSE(profilefields::ProfileEditErrorText(profilefields::profile_edit_birthday_invalid).empty());
}

namespace {

//造一份看别人用的资料模型：签名、所在地、邮箱、手机号填了，性别未设置、生日与岗位、座机没填
ballooncontact::ContactModel MakePfxPeerModel()
{
    ballooncontact::ContactModel m;
    m.SetBasicInfo(_T("Tom"), _T("10001"), _T("Beijing"), _T(""), _T(""), _T("On the road"),
                   ballooncontact::kContactStatusOnline);
    m.SetProfileFields(profile_gender_unset, 0, _T("tom@a.com"), _T("13800000001"), _T(""), _T(""));
    return m;
}

//造一份看本人用的资料模型：除手机号外都没填
ballooncontact::ContactModel MakePfxSelfModel()
{
    ballooncontact::ContactModel m;
    m.SetBasicInfo(_T("Me"), _T("10002"), _T(""), _T(""), _T(""), _T(""), ballooncontact::kContactStatusOnline);
    m.SetProfileFields(profile_gender_unset, 0, _T(""), _T("13800000002"), _T(""), _T(""));
    m.SetSelf(true);
    return m;
}

//造一份字段都填了的资料模型（看本人时连同昵称九个字段，看别人时不列昵称，八个）
ballooncontact::ContactModel MakePfxFullModel(bool self)
{
    ballooncontact::ContactModel m;
    m.SetBasicInfo(_T("Ann"), _T("10003"), _T("Shanghai"), _T(""), _T(""), _T("Signature text"),
                   ballooncontact::kContactStatusOnline);
    m.SetNickname(_T("Ann"));
    m.SetProfileFields(profile_gender_female, 19960812, _T("ann@a.com"), _T("13800000003"),
                       _T("021-6666 8888"), _T("Designer"));
    m.SetSelf(self);
    return m;
}

//按客户区宽度与给定高度把整棵控件树排一遍（含深层容器）
void LayOutPfxContactBody(balloonwjui::DuiControl* root, int height)
{
    RECT rc = { 0, 0, ballooncontact::kContactBodyWidth, height };
    root->ForceLayout(rc);
    ballooncontact::RelayoutContactBody(root);
}

//某个控件是否真的显示在界面上：它自己与所有上级容器都可见才算（找不到按不可见）。只看控件自己的标志
//不够 —— 槽位空着或所在行被收起时，隐藏的是格子或整行，里面的编辑按钮自身标志仍为可见
bool PfxVisible(balloonwjui::DuiControl* root, UINT id)
{
    balloonwjui::DuiControl* c = root->FindCtrlById(id);
    if (c == nullptr)
    {
        return false;
    }
    for (balloonwjui::DuiControl* node = c; node != nullptr; node = node->GetParent())
    {
        if (!node->IsVisible())
        {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("PFX9 联系人资料窗看别人：只列出填了的字段、全部只读、后面的前移，多于两行默认收起，全空不显示", "[profilefields][contactprofile]")
{
    CHECK(ballooncontact::kContactBodyWidth == 600);

    const ballooncontact::ContactModel model = MakePfxPeerModel();
    const std::vector<ballooncontact::ContactProfileCell> cells = ballooncontact::BuildContactProfileCells(model);
    REQUIRE(cells.size() == 4);
    CHECK(cells[0].m_field == ballooncontact::kProfileFieldSignature);
    CHECK(cells[0].m_value == CString(_T("On the road")));
    CHECK(cells[1].m_field == ballooncontact::kProfileFieldAddress);
    CHECK(cells[2].m_field == ballooncontact::kProfileFieldMail);
    CHECK(cells[3].m_field == ballooncontact::kProfileFieldMobile);
    for (size_t i = 0; i < cells.size(); ++i)
    {
        CHECK_FALSE(cells[i].m_editable);
        CHECK_FALSE(cells[i].m_isEmpty);
    }
    //签名一行，所在地与邮箱一行，手机号一行
    CHECK(ballooncontact::ContactProfileRowCount(cells) == 3);

    //灌进控件树：默认收起成两行，手机号那一行藏起来，出现「展开全部」
    std::unique_ptr<balloonwjui::DuiControl> root = ballooncontact::BuildContactBody();
    REQUIRE(root != nullptr);
    std::vector<int> slots;
    CHECK(ballooncontact::ConfigureContactProfile(root.get(), cells, true, false, slots) == 2);
    REQUIRE(slots.size() == (size_t)ballooncontact::kContactProfileSlotCount);
    //看别人时没有昵称：0 号槽位（昵称那一行）空着、不显示；签名在 1 号槽位，两列格子从 2 号起
    CHECK(slots[0] == -1);
    CHECK_FALSE(PfxVisible(root.get(), ballooncontact::kIdProfileRowBase + 0));
    CHECK(slots[1] == ballooncontact::kProfileFieldSignature);
    CHECK(slots[2] == ballooncontact::kProfileFieldAddress);
    CHECK(slots[3] == ballooncontact::kProfileFieldMail);
    CHECK(slots[4] == -1);
    CHECK(PfxVisible(root.get(), ballooncontact::kIdProfileBlock));
    CHECK(PfxVisible(root.get(), ballooncontact::kIdProfileSeparator));
    CHECK_FALSE(PfxVisible(root.get(), ballooncontact::kIdProfileRowBase + 3));
    balloonwjui::DuiLabel* toggle =
        dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(ballooncontact::kIdProfileToggle));
    REQUIRE(toggle != nullptr);
    CHECK(toggle->IsVisible());
    CHECK(toggle->GetText() == CString(_T("展开全部")));
    //编辑按钮全部不可见
    for (int s = 0; s < ballooncontact::kContactProfileSlotCount; ++s)
    {
        CAPTURE(s);
        CHECK_FALSE(PfxVisible(root.get(), ballooncontact::ContactProfileCtrlId(s, ballooncontact::kProfilePartPencil)));
    }

    //展开：三行都显示，手机号进 4 号槽位，链接变成「收起」
    CHECK(ballooncontact::ConfigureContactProfile(root.get(), cells, true, true, slots) == 3);
    CHECK(slots[4] == ballooncontact::kProfileFieldMobile);
    CHECK(PfxVisible(root.get(), ballooncontact::kIdProfileRowBase + 3));
    CHECK(toggle->GetText() == CString(_T("收起")));

    //一个字段都没填：区块与分隔线都不显示
    ballooncontact::ContactModel empty;
    empty.SetBasicInfo(_T("Nobody"), _T("10004"), _T(""), _T(""), _T(""), _T(""), ballooncontact::kContactStatusOnline);
    const std::vector<ballooncontact::ContactProfileCell> none = ballooncontact::BuildContactProfileCells(empty);
    CHECK(none.empty());
    CHECK(ballooncontact::ContactProfileRowCount(none) == 0);
    CHECK(ballooncontact::ConfigureContactProfile(root.get(), none, true, false, slots) == 0);
    CHECK_FALSE(PfxVisible(root.get(), ballooncontact::kIdProfileBlock));
    CHECK_FALSE(PfxVisible(root.get(), ballooncontact::kIdProfileSeparator));
    CHECK_FALSE(toggle->IsVisible());
}

TEST_CASE("PFX10 联系人资料窗看本人：九格都显示，没填的显示未填写，八格可编辑、手机号不可，不收起", "[profilefields][contactprofile]")
{
    const ballooncontact::ContactModel model = MakePfxSelfModel();
    const std::vector<ballooncontact::ContactProfileCell> cells = ballooncontact::BuildContactProfileCells(model);
    REQUIRE(cells.size() == (size_t)ballooncontact::kContactProfileFieldCount);
    for (size_t i = 0; i < cells.size(); ++i)
    {
        CAPTURE(i);
        CHECK(cells[i].m_field == (int)i);
        CHECK(cells[i].m_editable == (cells[i].m_field != ballooncontact::kProfileFieldMobile));
    }
    //性别未设置：本人看到的是「保密」，不算没填
    CHECK(cells[ballooncontact::kProfileFieldGender].m_value == CString(_T("保密")));
    CHECK_FALSE(cells[ballooncontact::kProfileFieldGender].m_isEmpty);
    //没填的字段：弱色「未填写」
    CHECK(cells[ballooncontact::kProfileFieldBirthday].m_isEmpty);
    CHECK(cells[ballooncontact::kProfileFieldBirthday].m_value == CString(_T("未填写")));
    CHECK(cells[ballooncontact::kProfileFieldPost].m_value == CString(_T("未填写")));
    //手机号填了，原样显示
    CHECK(cells[ballooncontact::kProfileFieldMobile].m_value == CString(_T("13800000002")));
    //昵称在第一格，没填时同样显示「未填写」（2026-10-07）
    CHECK(cells[0].m_field == ballooncontact::kProfileFieldNickname);
    CHECK(cells[ballooncontact::kProfileFieldNickname].m_isEmpty);
    CHECK(ballooncontact::ContactProfileRowCount(cells) == 6);

    //本人不收起：六行全部显示、没有「展开全部」；八个编辑按钮可见，手机号所在槽位没有
    std::unique_ptr<balloonwjui::DuiControl> root = ballooncontact::BuildContactBody();
    REQUIRE(root != nullptr);
    std::vector<int> slots;
    CHECK(ballooncontact::ConfigureContactProfile(root.get(), cells, false, false, slots) == 6);
    CHECK_FALSE(PfxVisible(root.get(), ballooncontact::kIdProfileToggle));
    int pencils = 0;
    for (int s = 0; s < ballooncontact::kContactProfileSlotCount; ++s)
    {
        CAPTURE(s);
        REQUIRE(slots[s] >= 0);
        const bool pencil = PfxVisible(root.get(), ballooncontact::ContactProfileCtrlId(s, ballooncontact::kProfilePartPencil));
        CHECK(pencil == (slots[s] != ballooncontact::kProfileFieldMobile));
        if (pencil)
        {
            ++pencils;
        }
    }
    CHECK(pencils == ballooncontact::kContactProfileFieldCount - 1);
}

TEST_CASE("PFX11 个人资料区块两列排法：昵称、签名各占整行，其余两列，互不重叠、不越出客户区", "[profilefields][contactprofile]")
{
    const std::vector<ballooncontact::ContactProfileCell> cells =
        ballooncontact::BuildContactProfileCells(MakePfxFullModel(true));
    std::unique_ptr<balloonwjui::DuiControl> root = ballooncontact::BuildContactBody();
    REQUIRE(root != nullptr);
    std::vector<int> slots;
    const int rows = ballooncontact::ConfigureContactProfile(root.get(), cells, false, false, slots);
    REQUIRE(rows == 6);
    LayOutPfxContactBody(root.get(), ballooncontact::ContactBodyHeight(0, 1, rows));

    const int left = ballooncontact::kContactBodyPaddingH;
    const int right = ballooncontact::kContactBodyWidth - ballooncontact::kContactBodyPaddingH;

    //昵称格、签名格各占满整行，昵称在上
    const RECT nick = root->FindCtrlById(ballooncontact::ContactProfileCtrlId(0, ballooncontact::kProfilePartCell))->GetRect();
    CHECK(nick.left == left);
    CHECK(nick.right == right);
    const RECT sig = root->FindCtrlById(ballooncontact::ContactProfileCtrlId(1, ballooncontact::kProfilePartCell))->GetRect();
    CHECK(sig.left == left);
    CHECK(sig.right == right);
    CHECK(nick.bottom <= sig.top);

    //其余两列：同一行的两格上沿对齐、左格在左、互不重叠；每格都在客户区左右内边距之内
    int prevBottom = sig.bottom;
    for (int s = ballooncontact::kContactProfileFirstGridSlot; s < ballooncontact::kContactProfileSlotCount; s += 2)
    {
        CAPTURE(s);
        const RECT a = root->FindCtrlById(ballooncontact::ContactProfileCtrlId(s, ballooncontact::kProfilePartCell))->GetRect();
        CHECK(a.left == left);
        CHECK(a.right > a.left);
        CHECK(a.top >= prevBottom);
        if (s + 1 < ballooncontact::kContactProfileSlotCount)
        {
            const RECT b = root->FindCtrlById(ballooncontact::ContactProfileCtrlId(s + 1, ballooncontact::kProfilePartCell))->GetRect();
            CHECK(b.top == a.top);
            CHECK(b.left > a.right);
            CHECK(b.right <= right);
        }
        prevBottom = a.bottom;
    }
    //值文字所在的控件有高度（不是被压扁成 0）
    const RECT value = root->FindCtrlById(ballooncontact::ContactProfileCtrlId(
        ballooncontact::kContactProfileFirstGridSlot, ballooncontact::kProfilePartValue))->GetRect();
    CHECK(value.bottom - value.top > 0);
}

TEST_CASE("PFX12 个人资料区块高度随显示行数变化；收起时窗口高度有上限且按该高度排得下底部按钮", "[profilefields][contactprofile]")
{
    //区块不显示时与加这块之前完全一致
    CHECK(ballooncontact::ContactProfileBlockHeight(0) == 0);
    CHECK(ballooncontact::ContactBodyHeight(0, 1, 0) == ballooncontact::ContactBodyHeight(0, 1));
    CHECK(ballooncontact::ContactBodyHeight(2, 3, 0) == ballooncontact::ContactBodyHeight(2, 3));
    //每多一行，区块变高
    for (int r = 1; r < ballooncontact::kContactProfileRowSlots; ++r)
    {
        CAPTURE(r);
        CHECK(ballooncontact::ContactProfileBlockHeight(r + 1) > ballooncontact::ContactProfileBlockHeight(r));
        CHECK(ballooncontact::ContactBodyHeight(1, 1, r + 1) - ballooncontact::ContactBodyHeight(1, 1, r)
              == ballooncontact::ContactProfileBlockHeight(r + 1) - ballooncontact::ContactProfileBlockHeight(r));
    }

    //收起规则
    CHECK(ballooncontact::ContactProfileShownRows(5, true, false) == ballooncontact::kContactProfileCollapsedRows);
    CHECK(ballooncontact::ContactProfileShownRows(5, true, true) == 5);
    CHECK(ballooncontact::ContactProfileShownRows(5, false, false) == 5);
    CHECK(ballooncontact::ContactProfileShownRows(2, true, false) == 2);
    CHECK(ballooncontact::ContactProfileShownRows(1, true, false) == 1);
    CHECK(ballooncontact::ContactProfileShownRows(0, true, false) == 0);

    //看别人、八个字段都填（看别人时不列昵称）、部门一行、标签一行：收起时整窗高度不超过 800（全部展开约 950，用户 2026-10-04
    //选定默认收起以控制高度）
    const int kCollapsedWindowMaxHeight = 800;
    const int collapsedWindow = ballooncontact::ContactBodyHeight(1, 1, ballooncontact::kContactProfileCollapsedRows)
                              + ballooncontact::kContactTitleBarHeight;
    INFO("collapsed window height = " << collapsedWindow);
    CHECK(collapsedWindow <= kCollapsedWindowMaxHeight);

    //按收起后的高度排得下：底部按钮没有被压扁、下方留白与空间充裕时相同
    const std::vector<ballooncontact::ContactProfileCell> cells =
        ballooncontact::BuildContactProfileCells(MakePfxFullModel(false));
    std::unique_ptr<balloonwjui::DuiControl> root = ballooncontact::BuildContactBody();
    REQUIRE(root != nullptr);
    std::vector<int> slots;
    const int rows = ballooncontact::ConfigureContactProfile(root.get(), cells, true, false, slots);
    REQUIRE(rows == ballooncontact::kContactProfileCollapsedRows);
    const int contentH = ballooncontact::ContactBodyHeight(0, 1, rows);
    const layoutfit::FitResult send =
        layoutfit::MeasureAt(root.get(), ballooncontact::kIdBtnSend, ballooncontact::kContactBodyWidth, contentH);
    REQUIRE(send.m_found);
    CHECK(send.m_bottom <= contentH);
    CHECK(send.m_height == send.m_roomyHeight);
    CHECK(send.m_bottomGap == send.m_roomyBottomGap);
    //全部展开后同样排得下
    const int fullRows = ballooncontact::ConfigureContactProfile(root.get(), cells, true, true, slots);
    REQUIRE(fullRows == 5);
    const int fullH = ballooncontact::ContactBodyHeight(0, 1, fullRows);
    const layoutfit::FitResult sendFull =
        layoutfit::MeasureAt(root.get(), ballooncontact::kIdBtnSend, ballooncontact::kContactBodyWidth, fullH);
    REQUIRE(sendFull.m_found);
    CHECK(sendFull.m_height == sendFull.m_roomyHeight);
    CHECK(sendFull.m_bottomGap == sendFull.m_roomyBottomGap);
}

TEST_CASE("PFX18 联系人资料窗看本人：隐藏通话、发消息、标签、备注分组、提醒收藏，窗口变矮且排得下", "[profilefields][contactprofile]")
{
    std::unique_ptr<balloonwjui::DuiControl> root = ballooncontact::BuildContactBody();
    REQUIRE(root != nullptr);

    //看本人时隐藏的与始终显示的控件
    const UINT kHiddenForSelf[] =
    {
        ballooncontact::kIdBtnVoice, ballooncontact::kIdBtnVideo, ballooncontact::kIdBtnSend,
        ballooncontact::kIdTagBlock, ballooncontact::kIdTagSeparator,
        ballooncontact::kIdRemarkSection, ballooncontact::kIdRemarkSeparator, ballooncontact::kIdNotifySection,
    };
    const UINT kAlwaysShown[] =
    {
        ballooncontact::kIdBtnHistory, ballooncontact::kIdBtnLocate, ballooncontact::kIdBtnClose,
    };

    ballooncontact::ApplyContactSelfView(root.get(), true);
    for (size_t i = 0; i < sizeof(kHiddenForSelf) / sizeof(kHiddenForSelf[0]); ++i)
    {
        CAPTURE(kHiddenForSelf[i]);
        REQUIRE(root->FindCtrlById(kHiddenForSelf[i]) != nullptr);
        CHECK_FALSE(PfxVisible(root.get(), kHiddenForSelf[i]));
    }
    for (size_t i = 0; i < sizeof(kAlwaysShown) / sizeof(kAlwaysShown[0]); ++i)
    {
        CAPTURE(kAlwaysShown[i]);
        CHECK(PfxVisible(root.get(), kAlwaysShown[i]));
    }

    //切回看别人：全部恢复显示
    ballooncontact::ApplyContactSelfView(root.get(), false);
    for (size_t i = 0; i < sizeof(kHiddenForSelf) / sizeof(kHiddenForSelf[0]); ++i)
    {
        CAPTURE(kHiddenForSelf[i]);
        CHECK(PfxVisible(root.get(), kHiddenForSelf[i]));
    }

    //高度：看本人比看别人矮，少掉的正是隐藏的三块与两条分隔线；标签行数不再影响高度
    //隐藏的部分与内嵌 XML 的 fixedHeight 一致：标签区一行 76、分隔线 1、备注名与分组 132、分隔线 1、提醒与收藏 118
    const int kHiddenSectionsHeight = 76 + 1 + 132 + 1 + 118;
    CHECK(ballooncontact::ContactBodyHeight(1, 1, 5, false) - ballooncontact::ContactBodyHeight(1, 1, 5, true)
          == kHiddenSectionsHeight);
    CHECK(ballooncontact::ContactBodyHeight(0, 3, 5, true) == ballooncontact::ContactBodyHeight(0, 1, 5, true));

    //按看本人的高度排得下：关闭按钮没有被压扁、下方留白与空间充裕时相同；两个快捷按钮拿到矩形
    const std::vector<ballooncontact::ContactProfileCell> cells =
        ballooncontact::BuildContactProfileCells(MakePfxFullModel(true));
    std::unique_ptr<balloonwjui::DuiControl> selfRoot = ballooncontact::BuildContactBody();
    REQUIRE(selfRoot != nullptr);
    ballooncontact::ApplyContactSelfView(selfRoot.get(), true);
    std::vector<int> slots;
    const int rows = ballooncontact::ConfigureContactProfile(selfRoot.get(), cells, false, false, slots);
    REQUIRE(rows == 6);
    const int contentH = ballooncontact::ContactBodyHeight(0, 1, rows, true);
    const layoutfit::FitResult close =
        layoutfit::MeasureAt(selfRoot.get(), ballooncontact::kIdBtnClose, ballooncontact::kContactBodyWidth, contentH);
    REQUIRE(close.m_found);
    CHECK(close.m_bottom <= contentH);
    CHECK(close.m_height == close.m_roomyHeight);
    CHECK(close.m_bottomGap == close.m_roomyBottomGap);
    LayOutPfxContactBody(selfRoot.get(), contentH);
    CHECK(PfxRectNonEmpty(selfRoot->FindCtrlById(ballooncontact::kIdBtnHistory)));
    CHECK(PfxRectNonEmpty(selfRoot->FindCtrlById(ballooncontact::kIdBtnLocate)));
    //个人资料区块最后一行（手机号，最后一个槽位）也在关闭按钮之上
    const RECT mobile = selfRoot->FindCtrlById(ballooncontact::ContactProfileCtrlId(
        ballooncontact::kContactProfileSlotCount - 1, ballooncontact::kProfilePartCell))->GetRect();
    const RECT closeRect = selfRoot->FindCtrlById(ballooncontact::kIdBtnClose)->GetRect();
    CHECK(mobile.bottom <= closeRect.top);
}

TEST_CASE("PFX19 本人资料的入口：头部昵称区、系统菜单、通讯录双击、右键查看资料、搜索选中都打开联系人资料窗", "[profilefields]")
{
    //2026-10-07 起本人资料对话框删除，五个入口统一打开联系人资料窗（看本人模式）
    CHECK(profilefields::SelfProfileOpensContactWindow(profilefields::self_profile_from_header));
    CHECK(profilefields::SelfProfileOpensContactWindow(profilefields::self_profile_from_system_menu));
    CHECK(profilefields::SelfProfileOpensContactWindow(profilefields::self_profile_from_contact_tree));
    CHECK(profilefields::SelfProfileOpensContactWindow(profilefields::self_profile_from_context_menu));
    CHECK(profilefields::SelfProfileOpensContactWindow(profilefields::self_profile_from_search));
    //不认识的入口不打开任何窗口
    const int kUnknownSource = 99;
    CHECK_FALSE(profilefields::SelfProfileOpensContactWindow(kUnknownSource));
    CHECK_FALSE(profilefields::SelfProfileOpensContactWindow(-1));
}

namespace {

//在槽位表里找某个字段所在的槽位；找不到返回 -1
int PfxSlotOf(const std::vector<int>& slots, int field)
{
    for (size_t i = 0; i < slots.size(); ++i)
    {
        if (slots[i] == field)
        {
            return (int)i;
        }
    }
    return -1;
}

} // namespace

TEST_CASE("PFX13 就地编辑：进入或退出只影响当前格，性别用下拉框；提交判定区分没改动、有改动与不合格", "[profilefields][contactprofile]")
{
    const ballooncontact::ContactModel model = MakePfxFullModel(true);
    const std::vector<ballooncontact::ContactProfileCell> cells = ballooncontact::BuildContactProfileCells(model);
    std::unique_ptr<balloonwjui::DuiControl> root = ballooncontact::BuildContactBody();
    REQUIRE(root != nullptr);
    std::vector<int> slots;
    REQUIRE(ballooncontact::ConfigureContactProfile(root.get(), cells, false, false, slots) == 6);

    //出场时所有格子的输入框与下拉框都隐藏
    for (int s = 0; s < ballooncontact::kContactProfileSlotCount; ++s)
    {
        CAPTURE(s);
        CHECK_FALSE(PfxVisible(root.get(), ballooncontact::ContactProfileCtrlId(s, ballooncontact::kProfilePartEdit)));
        CHECK_FALSE(PfxVisible(root.get(), ballooncontact::ContactProfileCtrlId(s, ballooncontact::kProfilePartCombo)));
    }

    //岗位格进入编辑：值与编辑按钮隐藏，输入框显示原值；别的格子不受影响
    const int postSlot = PfxSlotOf(slots, ballooncontact::kProfileFieldPost);
    const int mailSlot = PfxSlotOf(slots, ballooncontact::kProfileFieldMail);
    REQUIRE(postSlot > 0);
    REQUIRE(mailSlot > 0);
    const CString postText = ballooncontact::ProfileCellEditText(model, ballooncontact::kProfileFieldPost);
    CHECK(postText == CString(_T("Designer")));
    ballooncontact::SetContactProfileSlotEditing(root.get(), postSlot, ballooncontact::kProfileFieldPost, true, postText,
                                                 model.Gender());
    CHECK_FALSE(PfxVisible(root.get(), ballooncontact::ContactProfileCtrlId(postSlot, ballooncontact::kProfilePartValue)));
    CHECK_FALSE(PfxVisible(root.get(), ballooncontact::ContactProfileCtrlId(postSlot, ballooncontact::kProfilePartPencil)));
    balloonwjui::DuiEdit* postEdit = dynamic_cast<balloonwjui::DuiEdit*>(
        root->FindCtrlById(ballooncontact::ContactProfileCtrlId(postSlot, ballooncontact::kProfilePartEdit)));
    REQUIRE(postEdit != nullptr);
    CHECK(postEdit->IsVisible());
    CHECK(postEdit->GetText() == CString(_T("Designer")));
    CHECK_FALSE(PfxVisible(root.get(), ballooncontact::ContactProfileCtrlId(postSlot, ballooncontact::kProfilePartCombo)));
    CHECK(PfxVisible(root.get(), ballooncontact::ContactProfileCtrlId(mailSlot, ballooncontact::kProfilePartValue)));
    CHECK(PfxVisible(root.get(), ballooncontact::ContactProfileCtrlId(mailSlot, ballooncontact::kProfilePartPencil)));
    //退出编辑：恢复值与编辑按钮
    ballooncontact::SetContactProfileSlotEditing(root.get(), postSlot, ballooncontact::kProfileFieldPost, false, NULL, 0);
    CHECK(PfxVisible(root.get(), ballooncontact::ContactProfileCtrlId(postSlot, ballooncontact::kProfilePartValue)));
    CHECK(PfxVisible(root.get(), ballooncontact::ContactProfileCtrlId(postSlot, ballooncontact::kProfilePartPencil)));
    CHECK_FALSE(postEdit->IsVisible());

    //性别格进入编辑：显示下拉框（三项）并选中当前性别，不显示输入框
    const int genderSlot = PfxSlotOf(slots, ballooncontact::kProfileFieldGender);
    REQUIRE(genderSlot > 0);
    ballooncontact::SetContactProfileSlotEditing(root.get(), genderSlot, ballooncontact::kProfileFieldGender, true,
                                                 NULL, model.Gender());
    balloonwjui::DuiComboBox* combo = dynamic_cast<balloonwjui::DuiComboBox*>(
        root->FindCtrlById(ballooncontact::ContactProfileCtrlId(genderSlot, ballooncontact::kProfilePartCombo)));
    REQUIRE(combo != nullptr);
    CHECK(combo->IsVisible());
    CHECK(combo->GetCount() == profilefields::kGenderComboCount);
    CHECK(combo->GetCurSel() == profilefields::GenderToComboIndex(profile_gender_female));
    CHECK_FALSE(PfxVisible(root.get(), ballooncontact::ContactProfileCtrlId(genderSlot, ballooncontact::kProfilePartEdit)));

    //提交判定
    net::ProfileUpdateFields fields;
    profilefields::ProfileEditError error = profilefields::profile_edit_ok;
    //与原值相同（含首尾空白）：没改动
    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldPost, _T("  Designer "), -1,
                                                  fields, error) == ballooncontact::profile_cell_unchanged);
    //有改动：只标记这一项
    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldPost, _T("Lead"), -1,
                                                  fields, error) == ballooncontact::profile_cell_submit);
    CHECK(fields.m_hasPost);
    CHECK(fields.m_values.m_post == "Lead");
    CHECK_FALSE(fields.m_hasSignature);
    CHECK_FALSE(fields.m_hasLandline);
    //清空也是改动
    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldPost, _T(""), -1,
                                                  fields, error) == ballooncontact::profile_cell_submit);
    CHECK(fields.m_values.m_post.empty());
    //性别：选了「男」
    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldGender, CString(),
                                                  profilefields::GenderToComboIndex(profile_gender_male),
                                                  fields, error) == ballooncontact::profile_cell_submit);
    CHECK(fields.m_hasGender);
    CHECK(fields.m_values.m_gender == profile_gender_male);
    //性别：仍是原来的「女」
    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldGender, CString(),
                                                  profilefields::GenderToComboIndex(profile_gender_female),
                                                  fields, error) == ballooncontact::profile_cell_unchanged);
    //生日：同一天没改动，换一天有改动
    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldBirthday, _T("1996-08-12"), -1,
                                                  fields, error) == ballooncontact::profile_cell_unchanged);
    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldBirthday, _T("2000-02-29"), -1,
                                                  fields, error) == ballooncontact::profile_cell_submit);
    CHECK(fields.m_values.m_birthday == 20000229);
    //手机号不可修改
    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldMobile, _T("139"), -1,
                                                  fields, error) == ballooncontact::profile_cell_unchanged);
}

TEST_CASE("PFX14b 就地编辑的不合格情况：生日格式、日期不存在、邮箱缺 @、座机字符", "[profilefields][contactprofile]")
{
    const ballooncontact::ContactModel model = MakePfxFullModel(true);
    net::ProfileUpdateFields fields;
    profilefields::ProfileEditError error = profilefields::profile_edit_ok;

    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldBirthday, _T("1996/08/12"), -1,
                                                  fields, error) == ballooncontact::profile_cell_invalid);
    CHECK(error == profilefields::profile_edit_birthday_format);
    CHECK_FALSE(profilefields::ProfileEditErrorText(error).empty());

    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldBirthday, _T("2026-02-30"), -1,
                                                  fields, error) == ballooncontact::profile_cell_invalid);
    CHECK(error == profilefields::profile_edit_birthday_invalid);

    //清空生日是允许的
    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldBirthday, _T("   "), -1,
                                                  fields, error) == ballooncontact::profile_cell_submit);
    CHECK(fields.m_values.m_birthday == 0);

    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldMail, _T("ann.a.com"), -1,
                                                  fields, error) == ballooncontact::profile_cell_invalid);
    CHECK(error == profilefields::profile_edit_mail_invalid);

    CHECK(ballooncontact::DecideProfileCellCommit(model, ballooncontact::kProfileFieldLandline, _T("021#6666"), -1,
                                                  fields, error) == ballooncontact::profile_cell_invalid);
    CHECK(error == profilefields::profile_edit_landline_invalid);

    //就地编辑的生日解析（本人资料对话框删除后只剩这一份规则，细节见 PFX30）
    CHECK(profilefields::ParseBirthdayText(L"1996-08-12") == 19960812);
    CHECK(profilefields::ParseBirthdayText(L"1996/08/12") == -1);
    CHECK(profilefields::ParseBirthdayText(L" ") == 0);
}

TEST_CASE("PFX30 生日文字解析：合法、空白、首尾空白、非法格式与往返（由本人资料对话框的 B1～B5 改写）", "[profilefields]")
{
    //合法 YYYY-MM-DD → YYYYMMDD
    CHECK(profilefields::ParseBirthdayText(L"1995-04-02") == 19950402);
    CHECK(profilefields::ParseBirthdayText(L"2000-12-31") == 20001231);
    CHECK(profilefields::ParseBirthdayText(L"1900-01-01") == 19000101);
    //空串与纯空白 → 0（清空生日）
    CHECK(profilefields::ParseBirthdayText(L"") == 0);
    CHECK(profilefields::ParseBirthdayText(L"   ") == 0);
    //首尾空白裁掉后再解析
    CHECK(profilefields::ParseBirthdayText(L"  1995-04-02  ") == 19950402);
    //非法格式 → -1：不是日期、分隔符不对、位数不对、缺分隔符、月越界、日越界、年越界、含非数字
    CHECK(profilefields::ParseBirthdayText(L"bad") == -1);
    CHECK(profilefields::ParseBirthdayText(L"1995/04/02") == -1);
    CHECK(profilefields::ParseBirthdayText(L"1995-4-2") == -1);
    CHECK(profilefields::ParseBirthdayText(L"19950402") == -1);
    CHECK(profilefields::ParseBirthdayText(L"1995-13-02") == -1);
    CHECK(profilefields::ParseBirthdayText(L"1995-04-32") == -1);
    CHECK(profilefields::ParseBirthdayText(L"1899-04-02") == -1);
    CHECK(profilefields::ParseBirthdayText(L"19x5-04-02") == -1);
    //与 FormatBirthdayText 往返一致
    const int kSamples[] = { 19880808, 20101010, 19960229 };
    for (size_t i = 0; i < sizeof(kSamples) / sizeof(kSamples[0]); ++i)
    {
        CAPTURE(kSamples[i]);
        CHECK(profilefields::ParseBirthdayText(profilefields::FormatBirthdayText(kSamples[i])) == kSamples[i]);
    }
}

TEST_CASE("PFX15 失败回滚：只把这一次提交过的项恢复成提交前的值", "[profilefields][contactprofile]")
{
    ballooncontact::ContactModel model = MakePfxFullModel(true);
    net::ProfileEditableValues before;
    before.m_post = "Designer";
    before.m_landline = "021-6666 8888";

    //第一次提交改岗位，第二次提交改座机，两次都已乐观写入
    net::ProfileUpdateFields first;
    first.m_hasPost = true;
    first.m_values.m_post = "Lead";
    net::ProfileUpdateFields second;
    second.m_hasLandline = true;
    second.m_values.m_landline = "010-1";
    model.ApplyProfileUpdate(first);
    model.ApplyProfileUpdate(second);
    CHECK(model.Post() == CString(_T("Lead")));
    CHECK(model.Landline() == CString(_T("010-1")));

    //第一次失败：把提交的字段换成提交前的值再写一次，只恢复岗位，座机保留第二次的新值
    net::ProfileUpdateFields revert = first;
    revert.m_values = before;
    model.ApplyProfileUpdate(revert);
    CHECK(model.Post() == CString(_T("Designer")));
    CHECK(model.Landline() == CString(_T("010-1")));
    //没提交过的项保持不变
    CHECK(model.Mail() == CString(_T("ann@a.com")));
    CHECK(model.Gender() == profile_gender_female);
}

TEST_CASE("PFX16 进入与退出就地编辑后重排，输入框与值都拿到正确的矩形", "[profilefields][contactprofile]")
{
    const ballooncontact::ContactModel model = MakePfxFullModel(true);
    const std::vector<ballooncontact::ContactProfileCell> cells = ballooncontact::BuildContactProfileCells(model);
    std::unique_ptr<balloonwjui::DuiControl> root = ballooncontact::BuildContactBody();
    REQUIRE(root != nullptr);
    ballooncontact::ApplyContactSelfView(root.get(), true);
    std::vector<int> slots;
    const int rows = ballooncontact::ConfigureContactProfile(root.get(), cells, false, false, slots);
    const int height = ballooncontact::ContactBodyHeight(0, 1, rows, true);
    LayOutPfxContactBody(root.get(), height);

    const int landlineSlot = PfxSlotOf(slots, ballooncontact::kProfileFieldLandline);
    REQUIRE(landlineSlot > 0);
    const RECT cellRect =
        root->FindCtrlById(ballooncontact::ContactProfileCtrlId(landlineSlot, ballooncontact::kProfilePartCell))->GetRect();
    const RECT valueBefore =
        root->FindCtrlById(ballooncontact::ContactProfileCtrlId(landlineSlot, ballooncontact::kProfilePartValue))->GetRect();
    REQUIRE(PfxRectNonEmpty(root->FindCtrlById(ballooncontact::ContactProfileCtrlId(landlineSlot, ballooncontact::kProfilePartValue))));

    //进入编辑后重排：输入框落在格子里、占据原来值的位置
    ballooncontact::SetContactProfileSlotEditing(root.get(), landlineSlot, ballooncontact::kProfileFieldLandline, true,
                                                 _T("021-6666 8888"), 0);
    ballooncontact::RelayoutContactBody(root.get());
    balloonwjui::DuiControl* edit =
        root->FindCtrlById(ballooncontact::ContactProfileCtrlId(landlineSlot, ballooncontact::kProfilePartEdit));
    REQUIRE(PfxRectNonEmpty(edit));
    const RECT editRect = edit->GetRect();
    CHECK(editRect.left >= cellRect.left);
    CHECK(editRect.right <= cellRect.right);
    CHECK(editRect.top == valueBefore.top);
    CHECK(editRect.bottom == valueBefore.bottom);

    //退出编辑后重排：值回到原位
    ballooncontact::SetContactProfileSlotEditing(root.get(), landlineSlot, ballooncontact::kProfileFieldLandline, false,
                                                 NULL, 0);
    ballooncontact::RelayoutContactBody(root.get());
    balloonwjui::DuiControl* value =
        root->FindCtrlById(ballooncontact::ContactProfileCtrlId(landlineSlot, ballooncontact::kProfilePartValue));
    REQUIRE(PfxRectNonEmpty(value));
    CHECK(value->GetRect().top == valueBefore.top);
    CHECK(value->GetRect().bottom == valueBefore.bottom);
}

namespace {

//PFX17 用的宿主查找函数：模拟英文语言包，只认识「个人资料」区块用到的文字，其余返回 NULL（显示原文）
const wchar_t* FakePfxProfileHost(const wchar_t* context, const wchar_t* source)
{
    if (context != NULL)
    {
        return NULL;
    }
    struct Pair
    {
        const wchar_t* m_source;   //原文
        const wchar_t* m_text;     //译文
    };
    static const Pair kPairs[] =
    {
        { L"个性签名", L"Status message" }, { L"性别", L"Gender" },       { L"生日", L"Birthday" },
        { L"所在地", L"Location" },         { L"岗位", L"Role" },          { L"邮箱", L"Email" },
        { L"座机", L"Landline" },           { L"手机号", L"Mobile number" }, { L"未填写", L"Not provided" },
        { L"保密", L"Prefer not to say" },  { L"展开全部", L"Show all" },  { L"收起", L"Collapse" },
    };
    for (size_t i = 0; i < sizeof(kPairs) / sizeof(kPairs[0]); ++i)
    {
        if (wcscmp(source, kPairs[i].m_source) == 0)
        {
            return kPairs[i].m_text;
        }
    }
    return NULL;
}

//按 ContactText 的字号（像素）量一段文字的宽度
int PfxTextWidth(const wchar_t* text, int pixelHeight)
{
    HDC hdc = ::GetDC(NULL);
    HFONT font = ::CreateFontW(-pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                               i18n::UiFontName());
    HGDIOBJ old = ::SelectObject(hdc, font);
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &size);
    ::SelectObject(hdc, old);
    ::DeleteObject(font);
    ::ReleaseDC(NULL, hdc);
    return size.cx;
}

} // namespace

TEST_CASE("PFX17 英文界面：个人资料区块的字段名、未填写、保密与展开全部为译文，字段名在格子里放得下", "[profilefields][contactprofile]")
{
    ScopedPfxI18nReset reset;
    i18n::InitFromHost(&FakePfxProfileHost, L"en-US", L"Segoe UI");
    REQUIRE_FALSE(i18n::IsSourceLanguage());

    //看本人：字段名、没填时的「未填写」、未设置性别的「保密」都是译文
    const std::vector<ballooncontact::ContactProfileCell> selfCells =
        ballooncontact::BuildContactProfileCells(MakePfxSelfModel());
    REQUIRE(selfCells.size() == (size_t)ballooncontact::kContactProfileFieldCount);
    CHECK(selfCells[ballooncontact::kProfileFieldPost].m_label == CString(_T("Role")));
    CHECK(selfCells[ballooncontact::kProfileFieldLandline].m_label == CString(_T("Landline")));
    CHECK(selfCells[ballooncontact::kProfileFieldMobile].m_label == CString(_T("Mobile number")));
    CHECK(selfCells[ballooncontact::kProfileFieldBirthday].m_value == CString(_T("Not provided")));
    CHECK(selfCells[ballooncontact::kProfileFieldGender].m_value == CString(_T("Prefer not to say")));

    //看别人、多于两行：链接是「Show all」；展开后是「Collapse」
    const std::vector<ballooncontact::ContactProfileCell> peerCells =
        ballooncontact::BuildContactProfileCells(MakePfxFullModel(false));
    std::unique_ptr<balloonwjui::DuiControl> root = ballooncontact::BuildContactBody();
    REQUIRE(root != nullptr);
    std::vector<int> slots;
    const int rows = ballooncontact::ConfigureContactProfile(root.get(), peerCells, true, true, slots);
    balloonwjui::DuiLabel* toggle =
        dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(ballooncontact::kIdProfileToggle));
    REQUIRE(toggle != nullptr);
    CHECK(toggle->GetText() == CString(_T("Collapse")));
    REQUIRE(ballooncontact::ConfigureContactProfile(root.get(), peerCells, true, false, slots) == 2);
    CHECK(toggle->GetText() == CString(_T("Show all")));

    //英文字段名在格子里放得下：排版后每个字段名控件都比文字宽（字号与内嵌 XML 的 size="12" 一致）
    ballooncontact::ConfigureContactProfile(root.get(), peerCells, true, true, slots);
    LayOutPfxContactBody(root.get(), ballooncontact::ContactBodyHeight(0, 1, rows));
    const int kLabelPixelHeight = 12;
    for (int s = 0; s < ballooncontact::kContactProfileSlotCount; ++s)
    {
        if (slots[s] < 0)
        {
            continue;
        }
        CAPTURE(s);
        //按槽位上的字段找到对应的格子，量它的字段名
        for (size_t i = 0; i < peerCells.size(); ++i)
        {
            if (peerCells[i].m_field != slots[s])
            {
                continue;
            }
            const RECT rc = root->FindCtrlById(ballooncontact::ContactProfileCtrlId(s, ballooncontact::kProfilePartLabel))->GetRect();
            CHECK(rc.right - rc.left > PfxTextWidth(peerCells[i].m_label, kLabelPixelHeight));
        }
    }
}

namespace {

//PFX20 用的宿主查找函数：只认识「（我）」，其余返回 NULL（显示原文）
const wchar_t* FakePfxSelfMarkHost(const wchar_t* context, const wchar_t* source)
{
    if (context == NULL && wcscmp(source, L"（我）") == 0)
    {
        return L" (me)";
    }
    return NULL;
}

} // namespace

TEST_CASE("PFX20 通讯录树上本人那一行带「（我）」，别人不带，英文界面下为译文", "[profilefields]")
{
    ScopedPfxI18nReset reset;
    CHECK(balloonmain::AppendSelfMark(CString(_T("张三")), true) == CString(_T("张三（我）")));
    CHECK(balloonmain::AppendSelfMark(CString(_T("张三")), false) == CString(_T("张三")));
    //带备注的显示名「真名（备注）」照样只在末尾加
    CHECK(balloonmain::AppendSelfMark(CString(_T("张三（小张）")), true) == CString(_T("张三（小张）（我）")));

    i18n::InitFromHost(&FakePfxSelfMarkHost, L"en-US", L"Segoe UI");
    REQUIRE_FALSE(i18n::IsSourceLanguage());
    CHECK(balloonmain::AppendSelfMark(CString(_T("Ann")), true) == CString(_T("Ann (me)")));
    CHECK(balloonmain::AppendSelfMark(CString(_T("Ann")), false) == CString(_T("Ann")));
}

// =====================================================================
// 手机号、座机对同事的公开范围（2026-10-06，方案第 9 节）
// =====================================================================

namespace {

//PFX26、PFX28、PFX29 用的宿主查找函数：模拟英文语言包，只认识公开范围开关与两行字段名，其余返回 NULL（显示原文）。
//译文与 FlamingoLang/lang/en-US.po 里的一致；字段名取较长的「Mobile number」，检查英文下一行放得下
const wchar_t* FakePfxVisibilityHost(const wchar_t* context, const wchar_t* source)
{
    if (context != NULL)
    {
        return NULL;
    }
    struct Pair
    {
        const wchar_t* m_source;   //原文
        const wchar_t* m_text;     //译文
    };
    static const Pair kPairs[] =
    {
        { L"仅自己可见", L"Only me" }, { L"对同事可见", L"Colleagues" },
        { L"座机", L"Landline" },      { L"手机号", L"Mobile number" },
    };
    for (size_t i = 0; i < sizeof(kPairs) / sizeof(kPairs[0]); ++i)
    {
        if (wcscmp(source, kPairs[i].m_source) == 0)
        {
            return kPairs[i].m_text;
        }
    }
    return NULL;
}

//用标签自己绘制时的字体量一段文字的宽度（像素）；标签没设字体时用 balloonui 的默认字体
int PfxLabelTextWidth(const balloonwjui::DuiLabel* label, const wchar_t* text)
{
    HDC hdc = ::GetDC(NULL);
    HFONT font = label->GetFont();
    if (font == NULL)
    {
        font = balloonwjui::DuiResMgr::Inst().GetDefaultFont();
    }
    HGDIOBJ old = ::SelectObject(hdc, font);
    SIZE size = { 0, 0 };
    ::GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &size);
    ::SelectObject(hdc, old);
    ::ReleaseDC(NULL, hdc);
    return size.cx;
}

//矩形 a 是否完全在矩形 b 之内（含边界）
bool PfxRectInside(const RECT& a, const RECT& b)
{
    return a.left >= b.left && a.right <= b.right && a.top >= b.top && a.bottom <= b.bottom;
}

//从本源文件所在目录逐级往上，找到含 FlamingoLang\lang\en-US.po 的那一级目录（即 flamingoclient）；找不到返回空串
std::string PfxClientRoot()
{
    const int kMaxDepth = 6;   //Source\Tests 往上两级即是，多留几级余量
    std::string dir(__FILE__);
    for (int depth = 0; depth < kMaxDepth; ++depth)
    {
        const size_t pos = dir.find_last_of("\\/");
        if (pos == std::string::npos)
        {
            break;
        }
        dir.erase(pos);
        const std::string probe = dir + "\\FlamingoLang\\lang\\en-US.po";
        if (::GetFileAttributesA(probe.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            return dir;
        }
    }
    return std::string();
}

//读出 .po 里某个 msgid（无上下文）紧跟的 msgstr；找不到该条目时返回 "<missing>"
std::string PfxPoTranslation(const std::string& poText, const std::string& msgid)
{
    const std::string key = "\nmsgid \"" + msgid + "\"\n";
    const size_t pos = poText.find(key);
    if (pos == std::string::npos)
    {
        return "<missing>";
    }
    const std::string strPrefix = "msgstr \"";
    const size_t strPos = poText.find(strPrefix, pos + key.size());
    if (strPos == std::string::npos)
    {
        return "<missing>";
    }
    const size_t begin = strPos + strPrefix.size();
    const size_t end = poText.find('"', begin);
    return (end == std::string::npos) ? std::string("<missing>") : poText.substr(begin, end - begin);
}

//造一份看本人、八个字段都填了的资料模型，并设好两个公开范围
ballooncontact::ContactModel MakePfxSelfVisibilityModel(int phoneVisible, int landlineVisible)
{
    ballooncontact::ContactModel m = MakePfxFullModel(true);
    m.SetProfileVisibility(phoneVisible, landlineVisible);
    return m;
}

//按看本人的视图把「个人资料」区块灌进控件树并排版，返回各槽位的字段
std::vector<int> LayOutPfxSelfProfile(balloonwjui::DuiControl* root, const ballooncontact::ContactModel& model)
{
    const std::vector<ballooncontact::ContactProfileCell> cells = ballooncontact::BuildContactProfileCells(model);
    ballooncontact::ApplyContactSelfView(root, true);
    std::vector<int> slots;
    const int rows = ballooncontact::ConfigureContactProfile(root, cells, false, false, slots);
    LayOutPfxContactBody(root, ballooncontact::ContactBodyHeight(0, 1, rows, true));
    return slots;
}

} // namespace

TEST_CASE("PFX21 登录应答的两个公开范围：经登录结果带给本人资料，缺字段时手机号不公开、座机公开", "[profilefields][loginfill][visibility]")
{
    //新服务端下发：手机号公开、座机不公开
    const std::string body = R"({"code":0,"msg":"ok","userid":"8","servertime":1,"profile":{
        "nickname":"林知夏","phonenumber":"13800000008","landline":"021-6666 8888",
        "phonevisible":1,"landlinevisible":0,"enterpriseid":"5001"}})";
    net::LoginResponse resp;
    REQUIRE(net::LoginProtocolClient::parseLoginResponse(body, resp));
    CHECK(resp.profile.phonevisible == profile_visibility_colleagues);
    CHECK(resp.profile.landlinevisible == profile_visibility_self_only);
    CLoginResult out;
    net::FillLoginResultFromResponse(resp, out);
    CHECK(out.m_nPhoneVisible == profile_visibility_colleagues);
    CHECK(out.m_nLandlineVisible == profile_visibility_self_only);
    CHECK(out.m_strPhoneNumber == "13800000008");

    //老服务端不下发：取与服务端两列相同的默认值
    net::LoginResponse oldResp;
    REQUIRE(net::LoginProtocolClient::parseLoginResponse(
        R"({"code":0,"msg":"ok","userid":"8","servertime":1,"profile":{"nickname":"x"}})", oldResp));
    CHECK(oldResp.profile.phonevisible == profile_visibility_self_only);
    CHECK(oldResp.profile.landlinevisible == profile_visibility_colleagues);
    CLoginResult oldOut;
    net::FillLoginResultFromResponse(oldResp, oldOut);
    CHECK(oldOut.m_nPhoneVisible == profile_visibility_self_only);
    CHECK(oldOut.m_nLandlineVisible == profile_visibility_colleagues);

    //类型不符（字符串）同样按缺字段处理
    net::LoginResponse wrongType;
    REQUIRE(net::LoginProtocolClient::parseLoginResponse(
        R"({"code":0,"msg":"ok","userid":"8","servertime":1,"profile":{"phonevisible":"1"}})", wrongType));
    CHECK(wrongType.profile.phonevisible == profile_visibility_self_only);

    //登录结果的初始值也是默认值
    CLoginResult fresh;
    CHECK(fresh.m_nPhoneVisible == profile_visibility_self_only);
    CHECK(fresh.m_nLandlineVisible == profile_visibility_colleagues);
}

TEST_CASE("PFX22 改资料请求只在公开范围改变时带两个字段，从不带手机号；失败回滚恢复原设置", "[profilefields][visibility]")
{
    //默认值：手机号不公开、座机公开
    net::ProfileEditableValues before;
    CHECK(before.m_phoneVisible == profile_visibility_self_only);
    CHECK(before.m_landlineVisible == profile_visibility_colleagues);
    before.m_landline = "010-1";

    //没有改动：不提交
    CHECK(net::ProfileProtocolClient::diffProfileValues(before, before).IsEmpty());

    //只改手机号的公开范围：请求里恰好一项 phonevisible = 1，不带 phonenumber、landlinevisible
    net::ProfileEditableValues phoneOn = before;
    phoneOn.m_phoneVisible = profile_visibility_colleagues;
    const net::ProfileUpdateFields phoneFields = net::ProfileProtocolClient::diffProfileValues(before, phoneOn);
    CHECK_FALSE(phoneFields.IsEmpty());
    CHECK(phoneFields.m_hasPhoneVisible);
    CHECK_FALSE(phoneFields.m_hasLandlineVisible);
    CHECK_FALSE(phoneFields.m_hasLandline);
    Json::Value phoneJson;
    REQUIRE(ParsePfxJson(net::ProfileProtocolClient::buildUpdateProfileRequest(phoneFields), phoneJson));
    CHECK(phoneJson.size() == 1);
    CHECK(phoneJson["phonevisible"].asInt() == profile_visibility_colleagues);
    CHECK_FALSE(phoneJson.isMember("phonenumber"));
    //只改公开范围时本地校验直接通过
    CHECK(profilefields::ValidateProfileUpdate(phoneFields) == profilefields::profile_edit_ok);

    //把座机改成不公开：整数 0 也要出现在请求里
    net::ProfileEditableValues landlineOff = before;
    landlineOff.m_landlineVisible = profile_visibility_self_only;
    Json::Value landlineJson;
    REQUIRE(ParsePfxJson(net::ProfileProtocolClient::buildUpdateProfileRequest(
        net::ProfileProtocolClient::diffProfileValues(before, landlineOff)), landlineJson));
    CHECK(landlineJson.size() == 1);
    REQUIRE(landlineJson.isMember("landlinevisible"));
    CHECK(landlineJson["landlinevisible"].asInt() == profile_visibility_self_only);

    //乐观写入资料模型后失败：把提交的字段换成提交前的值再写一次，恢复原设置，其它资料不变
    ballooncontact::ContactModel model = MakePfxSelfVisibilityModel(profile_visibility_self_only,
                                                                    profile_visibility_colleagues);
    model.ApplyProfileUpdate(phoneFields);
    CHECK(model.PhoneVisible() == profile_visibility_colleagues);
    CHECK(model.LandlineVisible() == profile_visibility_colleagues);
    net::ProfileUpdateFields revert = phoneFields;
    revert.m_values = before;
    model.ApplyProfileUpdate(revert);
    CHECK(model.PhoneVisible() == profile_visibility_self_only);
    CHECK(model.LandlineVisible() == profile_visibility_colleagues);
    CHECK(model.Landline() == CString(_T("021-6666 8888")));
    CHECK(model.Mobile() == CString(_T("13800000003")));
}

TEST_CASE("PFX23 格子数据：看本人时只有手机号、座机两格带开关，文字随取值；看别人时没有开关", "[profilefields][contactprofile][visibility]")
{
    ScopedPfxI18nReset reset;

    //开关文字：取值文字后加下拉箭头；约定之外的取值按「仅自己可见」
    CHECK(std::wstring(profilefields::VisibilityText(profile_visibility_self_only)) == L"仅自己可见");
    CHECK(std::wstring(profilefields::VisibilityText(profile_visibility_colleagues)) == L"对同事可见");
    CHECK(std::wstring(profilefields::VisibilityText(2)) == L"仅自己可见");
    CHECK(profilefields::VisibilityLinkText(profile_visibility_self_only) == L"仅自己可见 \x25BE");
    CHECK(profilefields::VisibilityLinkText(profile_visibility_colleagues) == L"对同事可见 \x25BE");

    //看本人：手机号不公开、座机公开
    ballooncontact::ContactModel self = MakePfxSelfModel();   //座机没填
    self.SetProfileVisibility(profile_visibility_self_only, profile_visibility_colleagues);
    std::vector<ballooncontact::ContactProfileCell> cells = ballooncontact::BuildContactProfileCells(self);
    REQUIRE(cells.size() == (size_t)ballooncontact::kContactProfileFieldCount);
    for (size_t i = 0; i < cells.size(); ++i)
    {
        CAPTURE(i);
        const bool expected = (cells[i].m_field == ballooncontact::kProfileFieldMobile
                               || cells[i].m_field == ballooncontact::kProfileFieldLandline);
        CHECK(cells[i].m_hasVisibility == expected);
    }
    CHECK(cells[ballooncontact::kProfileFieldMobile].m_visibility == profile_visibility_self_only);
    CHECK(cells[ballooncontact::kProfileFieldLandline].m_visibility == profile_visibility_colleagues);
    //座机没填也带开关：设置对日后填上的号码同样有效
    CHECK(cells[ballooncontact::kProfileFieldLandline].m_isEmpty);

    //对调设置后格子随之变化
    self.SetProfileVisibility(profile_visibility_colleagues, profile_visibility_self_only);
    cells = ballooncontact::BuildContactProfileCells(self);
    CHECK(cells[ballooncontact::kProfileFieldMobile].m_visibility == profile_visibility_colleagues);
    CHECK(cells[ballooncontact::kProfileFieldLandline].m_visibility == profile_visibility_self_only);

    //控件树：只有手机号、座机所在的槽位显示开关，文字与取值一致
    std::unique_ptr<balloonwjui::DuiControl> root = ballooncontact::BuildContactBody();
    REQUIRE(root != nullptr);
    std::vector<int> slots;
    ballooncontact::ConfigureContactProfile(root.get(), cells, false, false, slots);
    for (int s = 0; s < ballooncontact::kContactProfileSlotCount; ++s)
    {
        CAPTURE(s);
        const UINT linkId = ballooncontact::ContactProfileCtrlId(s, ballooncontact::kProfilePartVisibility);
        balloonwjui::DuiLabel* link = dynamic_cast<balloonwjui::DuiLabel*>(root->FindCtrlById(linkId));
        REQUIRE(link != nullptr);
        const bool expected = (slots[s] == ballooncontact::kProfileFieldMobile
                               || slots[s] == ballooncontact::kProfileFieldLandline);
        CHECK(PfxVisible(root.get(), linkId) == expected);
        if (slots[s] == ballooncontact::kProfileFieldMobile)
        {
            CHECK(std::wstring(link->GetText()) == profilefields::VisibilityLinkText(profile_visibility_colleagues));
        }
        if (slots[s] == ballooncontact::kProfileFieldLandline)
        {
            CHECK(std::wstring(link->GetText()) == profilefields::VisibilityLinkText(profile_visibility_self_only));
        }
    }

    //看别人：没有任何开关，控件树里也全部隐藏
    const std::vector<ballooncontact::ContactProfileCell> peerCells =
        ballooncontact::BuildContactProfileCells(MakePfxFullModel(false));
    for (size_t i = 0; i < peerCells.size(); ++i)
    {
        CAPTURE(i);
        CHECK_FALSE(peerCells[i].m_hasVisibility);
    }
    ballooncontact::ConfigureContactProfile(root.get(), peerCells, true, true, slots);
    for (int s = 0; s < ballooncontact::kContactProfileSlotCount; ++s)
    {
        CAPTURE(s);
        CHECK_FALSE(PfxVisible(root.get(), ballooncontact::ContactProfileCtrlId(s, ballooncontact::kProfilePartVisibility)));
    }
}

TEST_CASE("PFX24 公开范围菜单：两项、当前项打勾；选当前项或取消不提交，选另一项只提交这一项", "[profilefields][visibility]")
{
    ScopedPfxI18nReset reset;

    //两项，顺序为「仅自己可见」「对同事可见」，与当前取值相同的那一项打勾
    std::vector<profilefields::VisibilityMenuEntry> menu = profilefields::BuildVisibilityMenu(profile_visibility_self_only);
    REQUIRE(menu.size() == 2);
    CHECK(menu[0].m_id == (unsigned int)profilefields::kVisibilityMenuIdSelfOnly);
    CHECK(menu[0].m_label == L"仅自己可见");
    CHECK(menu[0].m_checked);
    CHECK(menu[1].m_id == (unsigned int)profilefields::kVisibilityMenuIdColleagues);
    CHECK(menu[1].m_label == L"对同事可见");
    CHECK_FALSE(menu[1].m_checked);
    menu = profilefields::BuildVisibilityMenu(profile_visibility_colleagues);
    CHECK_FALSE(menu[0].m_checked);
    CHECK(menu[1].m_checked);
    //不认识的当前值：勾在「仅自己可见」
    const int kUnknownVisibility = 5;
    menu = profilefields::BuildVisibilityMenu(kUnknownVisibility);
    CHECK(menu[0].m_checked);
    CHECK_FALSE(menu[1].m_checked);

    //命令 id 与取值：0（取消）与不认识的 id 为 -1
    CHECK(profilefields::VisibilityFromMenuId(0) == -1);
    CHECK(profilefields::VisibilityFromMenuId(profilefields::kVisibilityMenuIdSelfOnly) == profile_visibility_self_only);
    CHECK(profilefields::VisibilityFromMenuId(profilefields::kVisibilityMenuIdColleagues) == profile_visibility_colleagues);
    const unsigned int kUnknownMenuId = 99;
    CHECK(profilefields::VisibilityFromMenuId(kUnknownMenuId) == -1);

    //选当前项、取消菜单：不提交
    net::ProfileUpdateFields fields;
    CHECK_FALSE(profilefields::DecideVisibilityChange(profilefields::visibility_target_phone, profile_visibility_self_only,
                                                      profilefields::kVisibilityMenuIdSelfOnly, fields));
    CHECK_FALSE(profilefields::DecideVisibilityChange(profilefields::visibility_target_phone, profile_visibility_self_only,
                                                      0, fields));

    //手机号改成公开：只提交 phonevisible = 1
    REQUIRE(profilefields::DecideVisibilityChange(profilefields::visibility_target_phone, profile_visibility_self_only,
                                                  profilefields::kVisibilityMenuIdColleagues, fields));
    CHECK(fields.m_hasPhoneVisible);
    CHECK_FALSE(fields.m_hasLandlineVisible);
    CHECK_FALSE(fields.m_hasLandline);
    CHECK_FALSE(fields.m_hasNickname);
    CHECK(fields.m_values.m_phoneVisible == profile_visibility_colleagues);
    CHECK(net::ProfileProtocolClient::buildUpdateProfileRequest(fields) == "{\"phonevisible\":1}");

    //座机改成不公开：只提交 landlinevisible = 0（上一次判定的结果不会残留）
    REQUIRE(profilefields::DecideVisibilityChange(profilefields::visibility_target_landline, profile_visibility_colleagues,
                                                  profilefields::kVisibilityMenuIdSelfOnly, fields));
    CHECK(fields.m_hasLandlineVisible);
    CHECK_FALSE(fields.m_hasPhoneVisible);
    CHECK(fields.m_values.m_landlineVisible == profile_visibility_self_only);
    CHECK(net::ProfileProtocolClient::buildUpdateProfileRequest(fields) == "{\"landlinevisible\":0}");

    //不认识的当前值按「仅自己可见」比较
    CHECK_FALSE(profilefields::DecideVisibilityChange(profilefields::visibility_target_landline, kUnknownVisibility,
                                                      profilefields::kVisibilityMenuIdSelfOnly, fields));
    CHECK(profilefields::DecideVisibilityChange(profilefields::visibility_target_landline, kUnknownVisibility,
                                                profilefields::kVisibilityMenuIdColleagues, fields));
    //不认识的号码：不提交
    const int kUnknownTarget = 9;
    CHECK_FALSE(profilefields::DecideVisibilityChange(kUnknownTarget, profile_visibility_self_only,
                                                      profilefields::kVisibilityMenuIdColleagues, fields));
}

TEST_CASE("PFX25 同事一侧：推送里号码为空串时资料窗里这一格消失，再推送带号码时重新出现", "[profilefields][contactprofile][visibility]")
{
    //通讯录缓存里同事的手机号与座机都有（对方此前公开）
    net::ContactItem contact;
    contact.m_userid = 100025;
    contact.m_nickname = "Ann";
    contact.m_phonenumber = "13800000025";
    contact.m_landline = "010-25";
    contact.m_mail = "ann@a.com";

    //对方把手机号改成不公开：服务端发给同事的推送里手机号为空串
    const std::string hiddenBody = R"({"userid":"100025","profile":{"nickname":"Ann","phonenumber":"",
        "landline":"010-25","mail":"ann@a.com"}})";
    net::UserInfoChangedNotify hidden;
    REQUIRE(net::ContactsProtocolClient::parseUserInfoChangedNotify(hiddenBody, hidden));
    net::ContactsProtocolClient::applyUserInfoChanged(hidden, contact);
    CHECK(contact.m_phonenumber.empty());
    CHECK(contact.m_landline == "010-25");

    //按缓存拼出看别人的资料模型（与主面板 BuildContactModelFromCache 同样的取法）：手机号那一格不显示
    ballooncontact::ContactModel model;
    model.SetBasicInfo(_T("Ann"), _T("10025"), _T(""), _T(""), _T(""), _T(""), ballooncontact::kContactStatusOnline);
    model.SetProfileFields(profile_gender_unset, 0, CString(contact.m_mail.c_str()),
                           CString(contact.m_phonenumber.c_str()), CString(contact.m_landline.c_str()), _T(""));
    std::vector<ballooncontact::ContactProfileCell> cells = ballooncontact::BuildContactProfileCells(model);
    bool hasMobile = false;
    bool hasLandline = false;
    for (size_t i = 0; i < cells.size(); ++i)
    {
        hasMobile = hasMobile || cells[i].m_field == ballooncontact::kProfileFieldMobile;
        hasLandline = hasLandline || cells[i].m_field == ballooncontact::kProfileFieldLandline;
    }
    CHECK_FALSE(hasMobile);
    CHECK(hasLandline);

    //对方又改回公开：推送带号码，缓存与资料窗里重新出现
    const std::string shownBody = R"({"userid":"100025","profile":{"nickname":"Ann","phonenumber":"13800000025",
        "landline":"010-25","mail":"ann@a.com"}})";
    net::UserInfoChangedNotify shown;
    REQUIRE(net::ContactsProtocolClient::parseUserInfoChangedNotify(shownBody, shown));
    net::ContactsProtocolClient::applyUserInfoChanged(shown, contact);
    CHECK(contact.m_phonenumber == "13800000025");
    model.SetProfileFields(profile_gender_unset, 0, CString(contact.m_mail.c_str()),
                           CString(contact.m_phonenumber.c_str()), CString(contact.m_landline.c_str()), _T(""));
    cells = ballooncontact::BuildContactProfileCells(model);
    hasMobile = false;
    for (size_t i = 0; i < cells.size(); ++i)
    {
        hasMobile = hasMobile || cells[i].m_field == ballooncontact::kProfileFieldMobile;
    }
    CHECK(hasMobile);
}

TEST_CASE("PFX26 联系人资料窗里的开关与字段名、编辑按钮互不重叠，不越出格子，没有被压窄；中英文都成立", "[profilefields][contactprofile][visibility]")
{
    const bool kEnglish[] = { false, true };
    for (size_t lang = 0; lang < sizeof(kEnglish) / sizeof(kEnglish[0]); ++lang)
    {
        CAPTURE(kEnglish[lang]);
        ScopedPfxI18nReset reset;
        if (kEnglish[lang])
        {
            i18n::InitFromHost(&FakePfxVisibilityHost, L"en-US", L"Segoe UI");
            REQUIRE_FALSE(i18n::IsSourceLanguage());
        }
        std::unique_ptr<balloonwjui::DuiControl> root = ballooncontact::BuildContactBody();
        REQUIRE(root != nullptr);
        const std::vector<int> slots = LayOutPfxSelfProfile(
            root.get(), MakePfxSelfVisibilityModel(profile_visibility_self_only, profile_visibility_colleagues));

        const int kFields[] = { ballooncontact::kProfileFieldMobile, ballooncontact::kProfileFieldLandline };
        for (size_t f = 0; f < sizeof(kFields) / sizeof(kFields[0]); ++f)
        {
            CAPTURE(kFields[f]);
            const int slot = PfxSlotOf(slots, kFields[f]);
            REQUIRE(slot > 0);
            const RECT cellRect =
                root->FindCtrlById(ballooncontact::ContactProfileCtrlId(slot, ballooncontact::kProfilePartCell))->GetRect();
            const RECT labelRect =
                root->FindCtrlById(ballooncontact::ContactProfileCtrlId(slot, ballooncontact::kProfilePartLabel))->GetRect();
            balloonwjui::DuiLabel* link = dynamic_cast<balloonwjui::DuiLabel*>(
                root->FindCtrlById(ballooncontact::ContactProfileCtrlId(slot, ballooncontact::kProfilePartVisibility)));
            REQUIRE(link != nullptr);
            REQUIRE(PfxRectNonEmpty(link));
            const RECT linkRect = link->GetRect();
            //开关在格子里、在字段名右边
            CHECK(PfxRectInside(linkRect, cellRect));
            CHECK(linkRect.left >= labelRect.right);
            //编辑按钮（只有座机有）在开关右边
            const UINT pencilId = ballooncontact::ContactProfileCtrlId(slot, ballooncontact::kProfilePartPencil);
            if (PfxVisible(root.get(), pencilId))
            {
                CHECK(linkRect.right <= root->FindCtrlById(pencilId)->GetRect().left);
            }
            //没有被压窄：两种文字都放得下（切换取值时宽度不变）
            const int linkWidth = linkRect.right - linkRect.left;
            CHECK(linkWidth >= PfxLabelTextWidth(link, profilefields::VisibilityLinkText(profile_visibility_self_only).c_str()));
            CHECK(linkWidth >= PfxLabelTextWidth(link, profilefields::VisibilityLinkText(profile_visibility_colleagues).c_str()));
            CHECK(linkWidth == ballooncontact::ProfileVisibilityLinkWidth(link));
            //字段名同样放得下（字号与内嵌 XML 的 size="12" 一致）
            const int kLabelPixelHeight = 12;
            const wchar_t* label = (kFields[f] == ballooncontact::kProfileFieldMobile) ? TR(L"手机号") : TR(L"座机");
            CHECK(labelRect.right - labelRect.left > PfxTextWidth(label, kLabelPixelHeight));
        }
    }
}

TEST_CASE("PFX29 两条新文字在四个 .po 里都有译文；英文界面下开关显示译文", "[profilefields][visibility]")
{
    //从本文件所在目录往上找到 flamingoclient（含 FlamingoLang\lang 的那一级），再读四个 .po
    const std::string clientRoot = PfxClientRoot();
    REQUIRE_FALSE(clientRoot.empty());
    const char* const kLanguages[] = { "zh-TW", "en-US", "ja-JP", "ko-KR" };
    const char* const kMsgids[] = { u8"仅自己可见", u8"对同事可见" };
    for (size_t l = 0; l < sizeof(kLanguages) / sizeof(kLanguages[0]); ++l)
    {
        CAPTURE(kLanguages[l]);
        const std::string path = clientRoot + "\\FlamingoLang\\lang\\" + kLanguages[l] + ".po";
        FILE* fp = fopen(path.c_str(), "rb");
        REQUIRE(fp != NULL);
        std::string text;
        char buf[4096];
        size_t n = 0;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        {
            text.append(buf, n);
        }
        fclose(fp);
        for (size_t m = 0; m < sizeof(kMsgids) / sizeof(kMsgids[0]); ++m)
        {
            CAPTURE(kMsgids[m]);
            const std::string translation = PfxPoTranslation(text, kMsgids[m]);
            CHECK(translation != "<missing>");
            CHECK_FALSE(translation.empty());
        }
    }

    //英文界面：开关文字为译文加下拉箭头，菜单项为译文
    ScopedPfxI18nReset reset;
    i18n::InitFromHost(&FakePfxVisibilityHost, L"en-US", L"Segoe UI");
    REQUIRE_FALSE(i18n::IsSourceLanguage());
    CHECK(profilefields::VisibilityLinkText(profile_visibility_self_only) == L"Only me \x25BE");
    CHECK(profilefields::VisibilityLinkText(profile_visibility_colleagues) == L"Colleagues \x25BE");
    const std::vector<profilefields::VisibilityMenuEntry> menu = profilefields::BuildVisibilityMenu(profile_visibility_colleagues);
    REQUIRE(menu.size() == 2);
    CHECK(menu[0].m_label == L"Only me");
    CHECK(menu[1].m_label == L"Colleagues");
}
