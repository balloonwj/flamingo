/**
 *  「文件共享」窗口纯逻辑单元测试（Suite SCC，标签 [sharecenter]），ShareCenterModelTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 17 步）：
 *    - SCC1 权限文字：先写默认的下载，再按界面列的顺序列出写权限，没有写权限时「浏览、下载」，单文件共享「只能下载」；类型文字；
 *    - SCC2 两个页签的第二行：路径、会话、成员数、状态与本机标注；分享者、会话、我的权限、状态；会话名为空时省略；
 *    - SCC3 每行的操作：已失效的只能定位；本机有记录才能在本机打开；共享给我的可以打开；
 *    - SCC4 正在访问的人与最近操作的文字；访问者行第二行的文字（不带名字）；
 *    - SCC5 本人视角的会话 id：单聊里别人共享给本人的取分享者，本人的与群聊的原样（第 18 步联调 SML3 发现的问题）；
 *    - SCC6 最近操作的文字覆盖四种写操作（第 2 期第 2 步）；
 *    - SCC7 列表行右端的状态胶囊文字与两个页签的第二行（2026-10-03 美化起）；
 *    - SCC8 搜索框的过滤：名称、路径、会话名、分享者名，不区分大小写，空关键字都匹配；
 *    - SCC9 详情里成员权限的小胶囊：第一个是「下载」，后面逐项列出写权限，没有写权限时只有「下载」，单个文件「只能下载」。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../Engine/ShareCenterModel.h"
#include "../../Source/net/Protocol.h"

using namespace fileshare;

namespace {

//几名成员
const int64_t kAlice = 50001;
const int64_t kBob = 50002;

//传输进度（字节）
const int64_t kDone = 35;
const int64_t kTotal = 100;

ShareListEntry Entry(int state, int kind)
{
    ShareListEntry e;
    e.m_shareId = 1;
    e.m_state = state;
    e.m_kind = kind;
    e.m_displayPath = "D:\\Projects";
    return e;
}

} // namespace

TEST_CASE("SCC1 perms and kind text", "[fileshare][sharecenter]")
{
    CHECK(ShareCenterModel::PermsText(0, share_kind_folder) == L"浏览、下载");
    CHECK(ShareCenterModel::PermsText(share_perm_rename | share_perm_upload, share_kind_folder) == L"下载、上传、重命名");
    CHECK(ShareCenterModel::PermsText(share_perm_upload | share_perm_mkdir | share_perm_rename | share_perm_delete | share_perm_move,
                                      share_kind_disk) == L"下载、上传、新建文件夹、重命名、删除、移动");
    CHECK(ShareCenterModel::PermsText(share_perm_upload, share_kind_file) == L"只能下载");
    CHECK(ShareCenterModel::KindText(share_kind_disk) == L"磁盘");
    CHECK(ShareCenterModel::KindText(share_kind_folder) == L"文件夹");
    CHECK(ShareCenterModel::KindText(share_kind_file) == L"文件");
}

TEST_CASE("SCC2 subtitles", "[fileshare][sharecenter]")
{
    ShareListEntry mine = Entry(share_state_available, share_kind_folder);
    ShareMemberPerm m;
    m.m_userId = kAlice;
    mine.m_members.push_back(m);
    m.m_userId = kBob;
    mine.m_members.push_back(m);
    CHECK(ShareCenterModel::MineSubtitle(mine, L"项目群", share_local_status_reported)
          == L"D:\\Projects · 项目群 · 2 名成员 · 可用");
    CHECK(ShareCenterModel::MineSubtitle(mine, L"", share_local_status_other_device)
          == L"D:\\Projects · 2 名成员 · 可用（位于另一台电脑）");
    CHECK(ShareCenterModel::MineSubtitle(mine, L"张三", share_local_status_missing_local).find(L"（本机缺少记录）")
          != std::wstring::npos);

    ShareListEntry shared = Entry(share_state_unavailable, share_kind_folder);
    shared.m_reason = share_unavailable_owner_offline;
    shared.m_myPerms = share_perm_upload;
    CHECK(ShareCenterModel::SharedSubtitle(shared, L"李四", L"项目群")
          == L"李四的共享 · 项目群 · 我的权限：下载、上传 · 共享暂不可用：分享者不在线");
    CHECK(ShareCenterModel::SharedSubtitle(shared, L"李四", L"").find(L"李四的共享 · 我的权限") == 0);
}

TEST_CASE("SCC3 row actions", "[fileshare][sharecenter]")
{
    const ShareListEntry ok = Entry(share_state_available, share_kind_folder);
    const ShareListEntry gone = Entry(share_state_invalid, share_kind_folder);
    CHECK(ShareCenterModel::MineActions(ok, share_local_status_reported)
          == (share_center_action_open_local | share_center_action_edit | share_center_action_cancel
              | share_center_action_locate));
    CHECK(ShareCenterModel::MineActions(ok, share_local_status_other_device)
          == (share_center_action_edit | share_center_action_cancel | share_center_action_locate));
    CHECK(ShareCenterModel::MineActions(gone, share_local_status_reported) == share_center_action_locate);
    CHECK(ShareCenterModel::SharedActions(ok) == (share_center_action_open | share_center_action_locate));
    CHECK(ShareCenterModel::SharedActions(gone) == share_center_action_locate);
}

TEST_CASE("SCC4 visitors and activities", "[fileshare][sharecenter]")
{
    ShareActiveVisitor v;
    v.m_visitorId = kAlice;
    v.m_visitorName = "Alice";
    CHECK(ShareCenterModel::VisitorText(v, L"") == L"Alice 正在浏览");
    CHECK(ShareCenterModel::VisitorText(v, L"爱丽丝") == L"爱丽丝 正在浏览");
    v.m_downloads = 1;
    CHECK(ShareCenterModel::VisitorText(v, L"") == L"Alice 正在下载 1 项");
    v.m_uploads = 2;
    v.m_bytesDone = kDone;
    v.m_bytesTotal = kTotal;
    CHECK(ShareCenterModel::VisitorText(v, L"") == L"Alice 正在下载 1 项、上传 2 项（35%）");
    v.m_downloads = 0;
    CHECK(ShareCenterModel::VisitorText(v, L"") == L"Alice 正在上传 2 项（35%）");

    //访问者行的第二行（2026-10-03 起）：不带名字的那一部分，与 VisitorText 去掉「名字 + 空格」相同
    CHECK(ShareCenterModel::VisitorStatusText(v) == L"正在上传 2 项（35%）");
    ShareActiveVisitor idle;
    CHECK(ShareCenterModel::VisitorStatusText(idle) == L"正在浏览");

    ShareActivityRecord rec;
    rec.m_visitorId = kAlice;
    rec.m_action = share_audit_download_file;
    rec.m_path = "docs";
    rec.m_result = 0;
    CHECK(ShareCenterModel::ActivityText(rec, L"Alice", L"10-03 14:22") == L"10-03 14:22  Alice 下载 docs · 成功");
    rec.m_action = share_audit_upload_file;
    rec.m_result = error_code_share_name_conflict;
    const std::wstring failed = ShareCenterModel::ActivityText(rec, L"Alice", L"");
    CHECK(failed.find(L"Alice 上传 docs · 失败：") == 0);
}

TEST_CASE("SCC5 viewer conversation id", "[fileshare][sharecenter]")
{
    ShareListEntry e = Entry(share_state_available, share_kind_folder);
    e.m_ctype = conversation_single;
    e.m_ownerId = kAlice;
    e.m_cid = kBob;   //分享者视角：对方是本人 kBob
    CHECK(ShareCenterModel::ViewerCid(e, kBob) == kAlice);
    CHECK(ShareCenterModel::ViewerCid(e, kAlice) == kBob);
    e.m_ctype = conversation_group;
    e.m_cid = 900001;
    CHECK(ShareCenterModel::ViewerCid(e, kBob) == 900001);
}

TEST_CASE("SCC6 activity text for write ops", "[fileshare][sharecenter]")
{
    CHECK(ShareCenterModel::ActionVerb(share_audit_download_file) == L"下载");
    CHECK(ShareCenterModel::ActionVerb(share_audit_upload_file) == L"上传");
    CHECK(ShareCenterModel::ActionVerb(share_audit_mkdir) == L"新建文件夹");
    CHECK(ShareCenterModel::ActionVerb(share_audit_rename) == L"重命名");
    CHECK(ShareCenterModel::ActionVerb(share_audit_delete) == L"删除");
    CHECK(ShareCenterModel::ActionVerb(share_audit_move) == L"移动");

    ShareActivityRecord rec;
    rec.m_action = share_audit_delete;
    rec.m_path = "docs/a.txt";
    rec.m_result = error_code_share_recycle_unavailable;
    const std::wstring text = ShareCenterModel::ActivityText(rec, L"Bob", L"");
    CHECK(text.find(L"Bob 删除 docs/a.txt · 失败：") == 0);
}

TEST_CASE("SCC7 list row chip and subtitles", "[fileshare][sharecenter]")
{
    //行右端的状态胶囊：只有状态，不带原因；不认识的状态不画
    CHECK(ShareCenterModel::StateChipText(share_state_available) == L"可用");
    CHECK(ShareCenterModel::StateChipText(share_state_unavailable) == L"暂不可用");
    CHECK(ShareCenterModel::StateChipText(share_state_invalid) == L"已失效");
    CHECK(ShareCenterModel::StateChipText(0).empty());

    //「我的共享」第二行：会话 · 成员数，不再带路径与状态
    ShareListEntry mine = Entry(share_state_available, share_kind_folder);
    ShareMemberPerm member;
    member.m_userId = kAlice;
    mine.m_members.push_back(member);
    member.m_userId = kBob;
    mine.m_members.push_back(member);
    CHECK(ShareCenterModel::MineRowSubtitle(mine, L"项目群") == L"项目群 · 2 名成员");
    CHECK(ShareCenterModel::MineRowSubtitle(mine, L"") == L"2 名成员");

    //「共享给我的」第二行：分享者 · 我的权限
    ShareListEntry shared = Entry(share_state_available, share_kind_folder);
    shared.m_myPerms = share_perm_upload;
    CHECK(ShareCenterModel::SharedRowSubtitle(shared, L"张三") == L"张三 · 下载、上传");
    CHECK(ShareCenterModel::SharedRowSubtitle(shared, L"") == L"下载、上传");
}

TEST_CASE("SCC8 search filter", "[fileshare][sharecenter]")
{
    ShareListEntry e = Entry(share_state_available, share_kind_folder);
    e.m_name = "Design Docs";
    //空关键字、只有空白：都匹配
    CHECK(ShareCenterModel::MatchesFilter(e, L"项目群", L"张三", L""));
    CHECK(ShareCenterModel::MatchesFilter(e, L"项目群", L"张三", L"  "));
    //名称（不区分大小写）、路径、会话名、分享者名任何一项包含即匹配；首尾空白不计
    CHECK(ShareCenterModel::MatchesFilter(e, L"项目群", L"张三", L"design"));
    CHECK(ShareCenterModel::MatchesFilter(e, L"项目群", L"张三", L" docs "));
    CHECK(ShareCenterModel::MatchesFilter(e, L"项目群", L"张三", L"projects"));
    CHECK(ShareCenterModel::MatchesFilter(e, L"项目群", L"张三", L"项目"));
    CHECK(ShareCenterModel::MatchesFilter(e, L"项目群", L"张三", L"张"));
    //都不包含：不匹配
    CHECK_FALSE(ShareCenterModel::MatchesFilter(e, L"项目群", L"张三", L"李四"));
}

TEST_CASE("SCC9 permission chips in the detail", "[fileshare][sharecenter]")
{
    //有写权限：按界面列的顺序逐项列出
    const std::vector<std::wstring> two = ShareCenterModel::PermChipTexts(share_perm_delete | share_perm_upload, share_kind_folder);
    REQUIRE(two.size() == 3);
    CHECK(two[0] == L"下载");
    CHECK(two[1] == L"上传");
    CHECK(two[2] == L"删除");
    //没有写权限：只有「下载」；单个文件：一个「只能下载」
    const std::vector<std::wstring> none = ShareCenterModel::PermChipTexts(0, share_kind_disk);
    REQUIRE(none.size() == 1);
    CHECK(none[0] == L"下载");
    const std::vector<std::wstring> file = ShareCenterModel::PermChipTexts(share_perm_upload, share_kind_file);
    REQUIRE(file.size() == 1);
    CHECK(file[0] == L"只能下载");
}
