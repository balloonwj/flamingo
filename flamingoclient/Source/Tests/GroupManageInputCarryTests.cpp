/**
 *  GroupManageInputCarryTests.cpp
 *
 *  群管理窗整页重建 / 切页时输入框草稿的带入规则，以及重建后的滚动位置
 *  （BalloonGroupManage/GroupManageInputCarry）的单元测试，标签 [groupmanage][inputcarry]。
 *
 *  每个用例都按窗口里的真实顺序调用：页面被换掉之前 OnInputLeaving 采集，新页面建好之后
 *  OnInputBuilt 登记并取回草稿。用例 GMIC1～10 与修复"群管理窗整页重建清空输入框"时同用户
 *  确认的清单一致（GMIC10 是实机验证时发现「重置」被草稿抵消后补充的）。
 *
 *  balloonwj@qq.com   2026-10-01
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>

#include "../UI/BalloonGroupManage/GroupManageInputCarry.h"
#include "../UI/BalloonGroupManage/GroupManageLayout.h"   // kIdEditXxx / kPageXxx

using namespace balloongroupmanage;

namespace {

//建页时为空的输入框，建好后读回的文字
const std::wstring kEmpty;

//模拟"重建"：先对旧页面上的输入框采集，再对新页面上建好的同一个输入框登记。
//builtText 为新输入框建好时的文字；返回值与 *shown 为新输入框最终应显示的文字。
bool Rebuild(InputDraftTable& table, int ctrlId, const std::wstring& currentText,
             const std::wstring& builtText, std::wstring* shown)
{
    table.OnInputLeaving(ctrlId, currentText);
    std::wstring draft;
    const bool carried = table.OnInputBuilt(ctrlId, builtText, &draft);
    *shown = carried ? draft : builtText;
    return carried;
}

} // anonymous namespace

TEST_CASE("GMIC1 公告标题与正文建页时为空，用户输入后重建会填回", "[groupmanage][inputcarry]")
{
    InputDraftTable table;
    std::wstring draft;
    CHECK_FALSE(table.OnInputBuilt(kIdEditAnnounceTitle, kEmpty, &draft));
    CHECK_FALSE(table.OnInputBuilt(kIdEditAnnounceBody, kEmpty, &draft));

    std::wstring shown;
    CHECK(Rebuild(table, kIdEditAnnounceTitle, L"下周一系统升级", kEmpty, &shown));
    CHECK(shown == L"下周一系统升级");
    CHECK(Rebuild(table, kIdEditAnnounceBody, L"第一行\r\n第二行", kEmpty, &shown));
    CHECK(shown == L"第一行\r\n第二行");

    //连续重建多次（例如四类数据先后返回）仍然保留
    CHECK(Rebuild(table, kIdEditAnnounceTitle, L"下周一系统升级", kEmpty, &shown));
    CHECK(shown == L"下周一系统升级");
}

TEST_CASE("GMIC2 用户没有改动时不填回，保留重建出来的内容", "[groupmanage][inputcarry]")
{
    InputDraftTable table;
    std::wstring draft;
    table.OnInputBuilt(kIdEditAnnounceTitle, kEmpty, &draft);
    table.OnInputBuilt(kIdEditAnnounceBody, kEmpty, &draft);

    std::wstring shown;
    CHECK_FALSE(Rebuild(table, kIdEditAnnounceTitle, kEmpty, kEmpty, &shown));
    CHECK(shown.empty());
    CHECK_FALSE(Rebuild(table, kIdEditAnnounceBody, kEmpty, kEmpty, &shown));
    CHECK_FALSE(table.HasDraft(kIdEditAnnounceTitle));
    CHECK_FALSE(table.HasDraft(kIdEditAnnounceBody));
}

TEST_CASE("GMIC3 群名称用户没改，服务端推来新群名后显示新群名", "[groupmanage][inputcarry]")
{
    InputDraftTable table;
    std::wstring draft;
    table.OnInputBuilt(kIdEditGroupName, L"研发部", &draft);

    std::wstring shown;
    CHECK_FALSE(Rebuild(table, kIdEditGroupName, L"研发部", L"研发中心", &shown));
    CHECK(shown == L"研发中心");
}

TEST_CASE("GMIC4 群名称改了但没保存，服务端推来新群名后保留用户的修改", "[groupmanage][inputcarry]")
{
    InputDraftTable table;
    std::wstring draft;
    table.OnInputBuilt(kIdEditIntro, L"旧简介", &draft);

    std::wstring shown;
    CHECK(Rebuild(table, kIdEditIntro, L"新简介（未保存）", L"别人改的简介", &shown));
    CHECK(shown == L"新简介（未保存）");
    //再来一次推送，用户的修改仍然保留
    CHECK(Rebuild(table, kIdEditIntro, L"新简介（未保存）", L"别人改的简介", &shown));
    CHECK(shown == L"新简介（未保存）");
}

TEST_CASE("GMIC5 保存成功后不再当作草稿，之后的推送值能正常显示", "[groupmanage][inputcarry]")
{
    InputDraftTable table;
    std::wstring draft;
    table.OnInputBuilt(kIdEditGroupName, L"研发部", &draft);

    //用户改成「研发中心」并保存，服务端推回的正是这段文字
    std::wstring shown;
    CHECK_FALSE(Rebuild(table, kIdEditGroupName, L"研发中心", L"研发中心", &shown));
    CHECK(shown == L"研发中心");
    CHECK_FALSE(table.HasDraft(kIdEditGroupName));

    //之后别的管理员又改了群名
    CHECK_FALSE(Rebuild(table, kIdEditGroupName, L"研发中心", L"研发一部", &shown));
    CHECK(shown == L"研发一部");
}

TEST_CASE("GMIC6 发布或点「清空」后输入框为空，草稿作废", "[groupmanage][inputcarry]")
{
    InputDraftTable table;
    std::wstring draft;
    table.OnInputBuilt(kIdEditAnnounceTitle, kEmpty, &draft);

    //先输入、经历一次重建，草稿生效
    std::wstring shown;
    CHECK(Rebuild(table, kIdEditAnnounceTitle, L"标题", kEmpty, &shown));
    CHECK(table.HasDraft(kIdEditAnnounceTitle));

    //发布后表单就地清空，下一次重建时采集到的是空串
    CHECK_FALSE(Rebuild(table, kIdEditAnnounceTitle, kEmpty, kEmpty, &shown));
    CHECK(shown.empty());
    CHECK_FALSE(table.HasDraft(kIdEditAnnounceTitle));
}

TEST_CASE("GMIC7 切到别的页再切回来，原页的草稿仍在", "[groupmanage][inputcarry]")
{
    InputDraftTable table;
    std::wstring draft;
    //停在群公告页，输入了标题
    table.OnInputBuilt(kIdEditAnnounceTitle, kEmpty, &draft);
    table.OnInputLeaving(kIdEditAnnounceTitle, L"写了一半的标题");

    //切到基本信息页：只有群名称、群简介两个输入框参与，公告标题不在当前页、不被采集
    table.OnInputBuilt(kIdEditGroupName, L"研发部", &draft);
    table.OnInputBuilt(kIdEditIntro, kEmpty, &draft);
    table.OnInputLeaving(kIdEditGroupName, L"研发部");
    table.OnInputLeaving(kIdEditIntro, kEmpty);
    CHECK(table.HasDraft(kIdEditAnnounceTitle));

    //切回群公告页
    draft.clear();
    CHECK(table.OnInputBuilt(kIdEditAnnounceTitle, kEmpty, &draft));
    CHECK(draft == L"写了一半的标题");
}

TEST_CASE("GMIC8 输入框暂时不存在时草稿保留到它建出来为止", "[groupmanage][inputcarry]")
{
    InputDraftTable table;
    std::wstring draft;
    table.OnInputBuilt(kIdEditGroupName, L"研发部", &draft);
    table.OnInputLeaving(kIdEditGroupName, L"改了没保存");

    //这一次重建时群资料处于未加载状态，基本信息页只显示一行提示、不建表单：
    //调用方对群名称输入框既不采集也不登记
    CHECK(table.HasDraft(kIdEditGroupName));

    //群资料到达，表单建出来了
    draft.clear();
    CHECK(table.OnInputBuilt(kIdEditGroupName, L"研发部", &draft));
    CHECK(draft == L"改了没保存");
}

TEST_CASE("GMIC10 点「重置」时撤销草稿并取回建页时的文字，之后重建显示服务端的值", "[groupmanage][inputcarry]")
{
    InputDraftTable table;
    std::wstring draft;
    table.OnInputBuilt(kIdEditGroupName, L"研发部", &draft);

    //改了群名称，经历一次后台重建，已存为草稿
    std::wstring shown;
    CHECK(Rebuild(table, kIdEditGroupName, L"改了没保存", L"研发部", &shown));
    REQUIRE(table.HasDraft(kIdEditGroupName));

    //点「重置」：草稿作废，取回建页时的文字，调用方当场把输入框改回去
    std::wstring baseline;
    CHECK(table.Revert(kIdEditGroupName, &baseline));
    CHECK(baseline == L"研发部");
    CHECK_FALSE(table.HasDraft(kIdEditGroupName));

    //「重置」随后重新拉取群资料，返回后整页重建：输入框已是原值，不再带入修改
    CHECK_FALSE(Rebuild(table, kIdEditGroupName, baseline, L"研发部", &shown));
    CHECK(shown == L"研发部");

    //改了但还没经历过重建（尚无草稿）时点「重置」，同样取回建页时的文字
    table.OnInputBuilt(kIdEditIntro, L"旧简介", &draft);
    CHECK(table.Revert(kIdEditIntro, &baseline));
    CHECK(baseline == L"旧简介");

    //从未登记过的输入框无从取回
    CHECK_FALSE(table.Revert(kIdSearchMembers, &baseline));
}

TEST_CASE("GMIC9 重建后停在同一页时保持滚动位置，换了页时回到顶部", "[groupmanage][inputcarry]")
{
    const int kScrolled = 360;   //重建前向下滚过的像素数，取值无特殊含义
    CHECK(ScrollPosAfterRebuild(kPageMembers, kPageMembers, kScrolled) == kScrolled);
    CHECK(ScrollPosAfterRebuild(kPageAnnouncement, kPageAnnouncement, 0) == 0);
    //本人被降级，原来停留的页已不可见，退回成员管理页
    CHECK(ScrollPosAfterRebuild(kPageAnnouncement, kPageMembers, kScrolled) == 0);
    //非法的负数位置按 0 处理
    CHECK(ScrollPosAfterRebuild(kPageMembers, kPageMembers, -1) == 0);
}
