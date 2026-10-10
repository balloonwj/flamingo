/**
 *  ProfileTests.cpp
 *
 *  个人信息对话框的契约测试：
 *    P1-P8   BuildProfileBody 产出的控件树符合"控件点名契约" —— 头像 /
 *            四个状态胶囊 / 查看态六个值文本 / 编辑态五个文本框 + 一个
 *            下拉框 / 底部按钮都能按既定数字 id 找到、类型正确、id 互不
 *            重复；查看态与编辑态各建一棵树。
 *    P9-P14  ProfileModel 数据模型：默认值、各字段读写、状态越界忽略、
 *            "编辑提交"语义（setter 改值后 getter 可见）。
 *    P15     两态客户区按设计尺寸排得下底部按钮。
 *    P16-P17 性别：模型存中文规范值，下拉框按序号对应，显示文字随界面语言（2026-10-04）。
 *
 *  用 Catch2 v3（amalgamated 形态）作测试框架。本工程不创建任何窗口，
 *  纯粹在内存里 build 控件树 / 操作模型 + 断言。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../UI/BalloonProfile/ProfileLayout.h"
#include "LayoutFitCheck.h"                 // layoutfit::MeasureAt：按设计尺寸排得下的检查
#include "../UI/BalloonProfile/ProfileModel.h"
#include "../Common/I18n/I18n.h"   // P17：模拟英文界面
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiComboBox.h"

using namespace balloonwjui;
using namespace balloonprofile;

namespace {

// 递归收集子树里所有非 0 的 ctrlId。
void CollectProfileIds(const DuiControl* node, std::vector<UINT>& out)
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
        CollectProfileIds(child.get(), out);
    }
}

// 判断一组 id 里是否存在重复值。
bool HasDuplicateProfileId(std::vector<UINT> ids)
{
    std::sort(ids.begin(), ids.end());
    return std::adjacent_find(ids.begin(), ids.end()) != ids.end();
}

} // anonymous namespace

// =====================================================================
// P1-P8 个人信息对话框布局契约
// =====================================================================

TEST_CASE("P1 BuildProfileBody 两态都返回非空控件树", "[profile]")
{
    auto viewRoot = BuildProfileBody(false);
    auto editRoot = BuildProfileBody(true);
    REQUIRE(viewRoot != nullptr);
    REQUIRE(editRoot != nullptr);
}

TEST_CASE("P2 查看态 / 编辑态都含头像控件", "[profile]")
{
    auto viewRoot = BuildProfileBody(false);
    auto editRoot = BuildProfileBody(true);
    REQUIRE(viewRoot != nullptr);
    REQUIRE(editRoot != nullptr);

    CHECK(viewRoot->FindCtrlById(kIdAvatar) != nullptr);
    CHECK(editRoot->FindCtrlById(kIdAvatar) != nullptr);
}

TEST_CASE("P3 查看态 / 编辑态都含四个状态胶囊", "[profile]")
{
    auto viewRoot = BuildProfileBody(false);
    REQUIRE(viewRoot != nullptr);

    CHECK(viewRoot->FindCtrlById(kIdPillOnline)    != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdPillAway)      != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdPillBusy)      != nullptr);
    // 隐身状态已去掉，编号 913 的胶囊不应再存在（防止有人把它加回来）
    CHECK(viewRoot->FindCtrlById(913)              == nullptr);

    // 胶囊代表的状态值应与 id 顺序一致。
    CHECK(GetProfilePillStatus(viewRoot.get(), kIdPillOnline)
          == kProfileStatusOnline);
    CHECK(GetProfilePillStatus(viewRoot.get(), kIdPillAway)
          == kProfileStatusAway);
    CHECK(GetProfilePillStatus(viewRoot.get(), kIdPillBusy)
          == kProfileStatusBusy);
}

TEST_CASE("P4 查看态含六个只读值文本控件", "[profile]")
{
    auto viewRoot = BuildProfileBody(false);
    REQUIRE(viewRoot != nullptr);

    CHECK(viewRoot->FindCtrlById(kIdViewNickname)  != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdViewSignature) != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdViewGender)    != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdViewBirthday)  != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdViewRegion)    != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdViewEmail)     != nullptr);
}

TEST_CASE("P5 编辑态含五个文本框且类型为 DuiEdit", "[profile]")
{
    auto editRoot = BuildProfileBody(true);
    REQUIRE(editRoot != nullptr);

    CHECK(dynamic_cast<DuiEdit*>(
        editRoot->FindCtrlById(kIdEditNickname))  != nullptr);
    CHECK(dynamic_cast<DuiEdit*>(
        editRoot->FindCtrlById(kIdEditSignature)) != nullptr);
    CHECK(dynamic_cast<DuiEdit*>(
        editRoot->FindCtrlById(kIdEditBirthday))  != nullptr);
    CHECK(dynamic_cast<DuiEdit*>(
        editRoot->FindCtrlById(kIdEditRegion))    != nullptr);
    CHECK(dynamic_cast<DuiEdit*>(
        editRoot->FindCtrlById(kIdEditEmail))     != nullptr);
}

TEST_CASE("P6 编辑态性别为下拉框且含女 / 男 / 保密三项", "[profile]")
{
    auto editRoot = BuildProfileBody(true);
    REQUIRE(editRoot != nullptr);

    DuiComboBox* gender = dynamic_cast<DuiComboBox*>(
        editRoot->FindCtrlById(kIdEditGender));
    REQUIRE(gender != nullptr);
    CHECK(gender->GetCount() == 3);
    CHECK(gender->GetItemText(0) == CString(_T("女")));
    CHECK(gender->GetItemText(1) == CString(_T("男")));
    CHECK(gender->GetItemText(2) == CString(_T("保密")));
}

TEST_CASE("P7 两态底部按钮按模式各自存在", "[profile]")
{
    auto viewRoot = BuildProfileBody(false);
    auto editRoot = BuildProfileBody(true);
    REQUIRE(viewRoot != nullptr);
    REQUIRE(editRoot != nullptr);

    // 查看态：关闭 + 编辑资料。
    CHECK(viewRoot->FindCtrlById(kIdBtnClose) != nullptr);
    CHECK(viewRoot->FindCtrlById(kIdBtnEdit)  != nullptr);
    // 编辑态：取消 + 保存。
    CHECK(editRoot->FindCtrlById(kIdBtnCancel) != nullptr);
    CHECK(editRoot->FindCtrlById(kIdBtnSave)   != nullptr);
}

TEST_CASE("P8 两态控件 id 各自互不重复", "[profile]")
{
    auto viewRoot = BuildProfileBody(false);
    auto editRoot = BuildProfileBody(true);
    REQUIRE(viewRoot != nullptr);
    REQUIRE(editRoot != nullptr);

    std::vector<UINT> viewIds;
    CollectProfileIds(viewRoot.get(), viewIds);
    CHECK_FALSE(viewIds.empty());
    CHECK_FALSE(HasDuplicateProfileId(viewIds));

    std::vector<UINT> editIds;
    CollectProfileIds(editRoot.get(), editIds);
    CHECK_FALSE(editIds.empty());
    CHECK_FALSE(HasDuplicateProfileId(editIds));
}

// =====================================================================
// P9-P14 ProfileModel 数据模型契约
// =====================================================================

TEST_CASE("P9 ProfileModel 默认值为林知夏那组 mock 数据", "[profile][model]")
{
    ProfileModel model;
    CHECK(model.Name()      == CString(_T("林知夏")));
    CHECK(model.Account()   == CString(_T("88234561")));
    CHECK(model.Nickname()  == CString(_T("林知夏")));
    CHECK(model.Signature() == CString(_T("想做一件让自己骄傲的事")));
    CHECK(model.Gender()    == CString(_T("女")));
    CHECK(model.Birthday()  == CString(_T("1996-08-12")));
    CHECK(model.Region()    == CString(_T("上海")));
    CHECK(model.Email()     == CString(_T("lin.zhixia@flamingo.im")));
}

TEST_CASE("P10 ProfileModel 默认状态为在线", "[profile][model]")
{
    ProfileModel model;
    CHECK(model.Status() == kProfileStatusOnline);
}

TEST_CASE("P11 ProfileModel 可编辑字段写入后读回一致", "[profile][model]")
{
    ProfileModel model;

    model.SetNickname(_T("阿夏"));
    model.SetSignature(_T("继续努力"));
    model.SetGender(_T("保密"));
    model.SetBirthday(_T("2000-01-01"));
    model.SetRegion(_T("杭州"));
    model.SetEmail(_T("xia@flamingo.im"));

    CHECK(model.Nickname()  == CString(_T("阿夏")));
    CHECK(model.Signature() == CString(_T("继续努力")));
    CHECK(model.Gender()    == CString(_T("保密")));
    CHECK(model.Birthday()  == CString(_T("2000-01-01")));
    CHECK(model.Region()    == CString(_T("杭州")));
    CHECK(model.Email()     == CString(_T("xia@flamingo.im")));

    // 只读字段不应被上述写入影响。
    CHECK(model.Name()    == CString(_T("林知夏")));
    CHECK(model.Account() == CString(_T("88234561")));
}

TEST_CASE("P12 ProfileModel.SetStatus 合法值生效", "[profile][model]")
{
    ProfileModel model;

    model.SetStatus(kProfileStatusBusy);
    CHECK(model.Status() == kProfileStatusBusy);

    model.SetStatus(kProfileStatusAway);
    CHECK(model.Status() == kProfileStatusAway);
}

TEST_CASE("P13 ProfileModel.SetStatus 越界被忽略", "[profile][model]")
{
    ProfileModel model;

    model.SetStatus(kProfileStatusAway);
    CHECK(model.Status() == kProfileStatusAway);

    model.SetStatus(99);    // 越界 —— 应被忽略
    CHECK(model.Status() == kProfileStatusAway);

    model.SetStatus(-1);    // 越界 —— 应被忽略
    CHECK(model.Status() == kProfileStatusAway);
}

TEST_CASE("P14 ProfileStatusText / ProfileStatusColor 边界正确", "[profile][model]")
{
    // 四个合法状态的文字。
    CHECK(CString(ProfileStatusText(kProfileStatusOnline))
          == CString(_T("在线")));
    CHECK(CString(ProfileStatusText(kProfileStatusAway))
          == CString(_T("离开")));
    CHECK(CString(ProfileStatusText(kProfileStatusBusy))
          == CString(_T("忙碌")));

    // 越界回退到"在线"。
    CHECK(CString(ProfileStatusText(999)) == CString(_T("在线")));
    CHECK(ProfileStatusColor(999) == ProfileStatusColor(kProfileStatusOnline));

    // 在线状态点为绿色（设计稿 #22C55E）。
    CHECK(ProfileStatusColor(kProfileStatusOnline) == RGB(0x22, 0xC5, 0x5E));
}

// P15：两态的内容区按设计宽度与 ProfileBodyHeight 算出的高度排版后，要完整容下各自底部的两个
// 按钮，且按钮没有被压扁。窗口高度是这个内容高度加上 kProfileTitleBarHeight。
TEST_CASE("P15 两态客户区按设计宽度与 ProfileBodyHeight 排版后排得下底部按钮", "[profile][layoutfit]")
{
    const bool modes[] = { false, false, true, true };
    const UINT btns[]  = { (UINT)kIdBtnClose, (UINT)kIdBtnEdit, (UINT)kIdBtnCancel, (UINT)kIdBtnSave };
    for (size_t i = 0; i < sizeof(btns) / sizeof(btns[0]); ++i)
    {
        INFO("editMode = " << modes[i] << ", button id = " << btns[i]);
        const int contentH = ProfileBodyHeight(modes[i]);
        std::unique_ptr<DuiControl> root = BuildProfileBody(modes[i]);
        REQUIRE(root != nullptr);
        const layoutfit::FitResult r = layoutfit::MeasureAt(root.get(), btns[i], kProfileWindowWidth, contentH);
        REQUIRE(r.m_found);
        INFO("button bottom = " << r.m_bottom << ", content height = " << contentH
             << ", height = " << r.m_height << ", roomy height = " << r.m_roomyHeight
             << ", bottom gap = " << r.m_bottomGap << ", roomy bottom gap = " << r.m_roomyBottomGap);
        CHECK(r.m_bottom <= contentH);
        CHECK(r.m_height == r.m_roomyHeight);
        CHECK(r.m_bottomGap == r.m_roomyBottomGap);
    }
}

TEST_CASE("P16 性别规范值、下拉框序号与显示文字的对应", "[profile][model]")
{
    //序号与规范值互逆，顺序与下拉框一致
    CHECK(ProfileGenderValueAt(0) == CString(_T("女")));
    CHECK(ProfileGenderValueAt(1) == CString(_T("男")));
    CHECK(ProfileGenderValueAt(2) == CString(_T("保密")));
    for (int i = 0; i < kProfileGenderCount; ++i)
    {
        CHECK(ProfileGenderIndex(ProfileGenderValueAt(i)) == i);
    }
    //越界序号、不是规范值的取值
    CHECK(ProfileGenderValueAt(-1).IsEmpty());
    CHECK(ProfileGenderValueAt(kProfileGenderCount).IsEmpty());
    CHECK(ProfileGenderIndex(_T("Female")) == -1);
    CHECK(ProfileGenderIndex(_T("")) == -1);
    //简体中文界面：显示文字就是规范值；不是规范值的原样显示
    CHECK(ProfileGenderText(_T("男")) == CString(_T("男")));
    CHECK(ProfileGenderText(_T("其他")) == CString(_T("其他")));
}

namespace {

//P17 用的宿主查找函数：只认识三个性别选项，其余返回 NULL（表示没有译文）
const wchar_t* FakeGenderHost(const wchar_t* context, const wchar_t* source)
{
    if (context != NULL)
        return NULL;
    if (wcscmp(source, L"女") == 0)
        return L"Female";
    if (wcscmp(source, L"男") == 0)
        return L"Male";
    if (wcscmp(source, L"保密") == 0)
        return L"Prefer not to say";
    return NULL;
}

//用例结束时把 i18n 模块恢复为未初始化（简体中文），不影响后面的用例
struct ScopedGenderI18nReset
{
    ScopedGenderI18nReset()
    {
        i18n::ResetForTest();
    }
    ~ScopedGenderI18nReset()
    {
        i18n::ResetForTest();
    }
};

} // namespace

TEST_CASE("P17 英文界面：性别显示译文，下拉框选项为译文且序号仍对应规范值", "[profile][model]")
{
    ScopedGenderI18nReset reset;
    i18n::InitFromHost(&FakeGenderHost, L"en-US", L"Segoe UI");
    REQUIRE_FALSE(i18n::IsSourceLanguage());

    //查看态的显示文字
    CHECK(ProfileGenderText(_T("女")) == CString(_T("Female")));
    CHECK(ProfileGenderText(_T("保密")) == CString(_T("Prefer not to say")));
    //规范值与序号不随界面语言变化：写回模型的仍是中文
    CHECK(ProfileGenderIndex(_T("男")) == 1);
    CHECK(ProfileGenderValueAt(1) == CString(_T("男")));

    //编辑态下拉框的选项是译文，顺序与规范值一致
    std::unique_ptr<DuiControl> editRoot = BuildProfileBody(true);
    REQUIRE(editRoot);
    DuiComboBox* gender = dynamic_cast<DuiComboBox*>(editRoot->FindCtrlById(kIdEditGender));
    REQUIRE(gender != nullptr);
    REQUIRE(gender->GetCount() == kProfileGenderCount);
    CHECK(gender->GetItemText(0) == CString(_T("Female")));
    CHECK(gender->GetItemText(1) == CString(_T("Male")));
    CHECK(gender->GetItemText(2) == CString(_T("Prefer not to say")));
}
