/**
 *  发起共享与调整成员纯逻辑单元测试（Suite SCM，标签 [sharecreate]），ShareCreateModelTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 15 步的 15a）：
 *    - SCM1 勾选与逐人权限：未勾选的成员设不了权限，取消勾选清掉权限，固定成员取消不掉；
 *    - SCM2 列标题的「全选此列」：只作用于已勾选的成员，列状态有无 / 部分 / 全部三种；单文件共享没有写权限，
 *           发起时改为单文件共享清掉写权限、保留勾选，改回来可以重新设权限；
 *    - SCM3 生成 2201 的成员与 2204 的三组：追加、移除、改权限，没有变化返回 false；按已有成员预先勾选，
 *           不在候选里的成员追加到表末尾；
 *    - SCM4 标出服务端拒绝的成员，再次标记时清掉上一次的标记；
 *    - SCM5 敏感位置：就是敏感目录、是它的上级、位于整棵子树都敏感的目录之下都算；用户目录下的子目录不算；
 *           不区分大小写，结尾反斜杠不影响，前缀相同的兄弟目录不算；
 *    - SCM6 显示名：磁盘有卷标用卷标、没有用「本地磁盘」，文件夹与文件取最后一段；
 *    - SCM7 本机信息：clientshareid 的格式与唯一性，敏感位置含 Windows 目录且其子树敏感，驱动器列表含系统盘；
 *    - SCM8 错误提示文字；
 *    - SCM9 多人共享新建的群的群名（2026-10-03 用户要求）：「共享 <共享名> 给 <成员名>」，成员名用「、」分隔；超过上限时
 *           少列几个人、以「等 N 人」收尾；只列一个人仍超长时截短共享名、以「…」收尾；结果不超过上限、不拆开代理对；
 *    - SCM10 共享名称（2026-10-04 用户要求）：默认名称「XXX的文件共享」，不知道本人名字时为「我的文件共享」；
 *           没填或只有空白（含全角空格）时用默认名称，填了时去掉首尾空白；超过上限截短、不拆开代理对；822 的提示文字。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>
#include <vector>

#include "../Engine/ShareCreateModel.h"
#include "../../Source/net/Protocol.h"

using namespace fileshare;

namespace {

//三名候选成员的 userid
const int64_t kAlice = 10001;
const int64_t kBob = 10002;
const int64_t kCarol = 10003;

//候选里没有、已有成员里才有的 userid（调整成员时已退群的人）
const int64_t kDave = 10004;

//clientshareid 的长度：32 个十六进制字符加 4 个连字符
const size_t kClientShareIdLen = 36;

//clientshareid 里连字符的位置
const size_t kHyphenPos[] = { 8, 13, 18, 23 };

//候选成员：Alice、Bob、Carol
std::vector<ShareMemberRow> Candidates()
{
    std::vector<ShareMemberRow> out;
    const int64_t ids[] = { kAlice, kBob, kCarol };
    const char* const names[] = { "Alice", "Bob", "Carol" };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
        ShareMemberRow row;
        row.m_userId = ids[i];
        row.m_name = names[i];
        out.push_back(row);
    }
    return out;
}

//按 userid 取一行；没有时测试失败
const ShareMemberRow& RowOf(const CShareMemberTable& table, int64_t userId)
{
    const std::vector<ShareMemberRow>& rows = table.Rows();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].m_userId == userId)
            return rows[i];
    }
    FAIL("row not found");
    return rows[0];
}

//构造一名成员及其权限
ShareMemberPerm Member(int64_t userId, int perms)
{
    ShareMemberPerm m;
    m.m_userId = userId;
    m.m_perms = perms;
    return m;
}

//构造一个敏感位置
ShareSensitivePath Sensitive(const wchar_t* path, bool withSubtree)
{
    ShareSensitivePath s;
    s.m_path = path;
    s.m_withSubtree = withSubtree;
    return s;
}

} // namespace

TEST_CASE("SCM1 check and per-member perms", "[fileshare][sharecreate]")
{
    CShareMemberTable table;
    table.Reset(Candidates(), false);
    REQUIRE(table.Rows().size() == 3);
    CHECK_FALSE(RowOf(table, kAlice).m_checked);

    //未勾选的成员设不了权限
    table.SetPerm(kAlice, share_perm_upload, true);
    CHECK(RowOf(table, kAlice).m_perms == 0);

    table.SetChecked(kAlice, true);
    table.SetPerm(kAlice, share_perm_upload, true);
    table.SetPerm(kAlice, share_perm_delete, true);
    CHECK(RowOf(table, kAlice).m_perms == (share_perm_upload | share_perm_delete));
    table.SetPerm(kAlice, share_perm_upload, false);
    CHECK(RowOf(table, kAlice).m_perms == share_perm_delete);

    //取消勾选清掉权限，再勾选回来权限为空
    table.SetChecked(kAlice, false);
    CHECK(RowOf(table, kAlice).m_perms == 0);
    table.SetChecked(kAlice, true);
    CHECK(RowOf(table, kAlice).m_perms == 0);

    //固定成员（单聊的对方）勾选后取消不掉
    table.SetFixed(kBob);
    CHECK(RowOf(table, kBob).m_checked);
    table.SetChecked(kBob, false);
    CHECK(RowOf(table, kBob).m_checked);

    //不存在的 userid 不影响表格
    table.SetChecked(kDave, true);
    CHECK(table.Rows().size() == 3);
}

TEST_CASE("SCM2 column select-all and file share", "[fileshare][sharecreate]")
{
    CShareMemberTable table;
    table.Reset(Candidates(), false);
    CHECK(table.ColumnState(share_perm_upload) == share_column_none);

    table.SetChecked(kAlice, true);
    table.SetChecked(kBob, true);
    table.SetPerm(kAlice, share_perm_upload, true);
    CHECK(table.ColumnState(share_perm_upload) == share_column_some);

    //全选此列只作用于已勾选的成员
    table.SetColumn(share_perm_upload, true);
    CHECK(table.ColumnState(share_perm_upload) == share_column_all);
    CHECK(RowOf(table, kBob).m_perms == share_perm_upload);
    CHECK(RowOf(table, kCarol).m_perms == 0);
    CHECK(table.ColumnState(share_perm_mkdir) == share_column_none);

    table.SetColumn(share_perm_upload, false);
    CHECK(table.ColumnState(share_perm_upload) == share_column_none);
    CHECK(RowOf(table, kAlice).m_perms == 0);

    //界面上的列顺序：上传、新建文件夹、重命名、删除、移动
    REQUIRE(kShareMemberPermColumnCount == 5);
    CHECK(kShareMemberPermColumns[0] == share_perm_upload);
    CHECK(kShareMemberPermColumns[4] == share_perm_move);

    //单文件共享：设权限一律无效，生成的成员权限为 0
    CShareMemberTable fileTable;
    fileTable.Reset(Candidates(), true);
    CHECK(fileTable.IsFileShare());
    fileTable.SetChecked(kAlice, true);
    fileTable.SetPerm(kAlice, share_perm_upload, true);
    fileTable.SetColumn(share_perm_delete, true);
    CHECK(RowOf(fileTable, kAlice).m_perms == 0);
    std::vector<ShareMemberPerm> existing(1, Member(kBob, share_perm_upload));
    fileTable.ApplyExisting(existing, std::vector<ShareMemberRow>());
    const std::vector<ShareMemberPerm> members = fileTable.CheckedMembers();
    REQUIRE(members.size() == 2);
    CHECK(members[0].m_perms == 0);
    CHECK(members[1].m_perms == 0);

    //发起时换了共享对象：改为单文件共享清掉写权限、保留勾选；改回文件夹后可以重新设权限
    CShareMemberTable switching;
    switching.Reset(Candidates(), false);
    switching.SetChecked(kAlice, true);
    switching.SetPerm(kAlice, share_perm_rename, true);
    switching.SetFileShare(true);
    CHECK(RowOf(switching, kAlice).m_checked);
    CHECK(RowOf(switching, kAlice).m_perms == 0);
    switching.SetFileShare(false);
    switching.SetPerm(kAlice, share_perm_rename, true);
    CHECK(RowOf(switching, kAlice).m_perms == share_perm_rename);
}

TEST_CASE("SCM3 create members and update diff", "[fileshare][sharecreate]")
{
    CShareMemberTable table;
    table.Reset(Candidates(), false);
    table.SetChecked(kAlice, true);
    table.SetChecked(kCarol, true);
    table.SetPerm(kCarol, share_perm_rename, true);
    std::vector<ShareMemberPerm> members = table.CheckedMembers();
    REQUIRE(members.size() == 2);
    CHECK(members[0].m_userId == kAlice);
    CHECK(members[0].m_perms == 0);
    CHECK(members[1].m_userId == kCarol);
    CHECK(members[1].m_perms == share_perm_rename);

    //调整成员：已有 Alice（上传）、Bob（无）、Dave（删除，候选里没有）
    std::vector<ShareMemberPerm> before;
    before.push_back(Member(kAlice, share_perm_upload));
    before.push_back(Member(kBob, 0));
    before.push_back(Member(kDave, share_perm_delete));
    std::vector<ShareMemberRow> names;
    ShareMemberRow dave;
    dave.m_userId = kDave;
    dave.m_name = "Dave";
    names.push_back(dave);

    CShareMemberTable edit;
    edit.Reset(Candidates(), false);
    edit.ApplyExisting(before, names);
    REQUIRE(edit.Rows().size() == 4);
    CHECK(edit.Rows()[3].m_userId == kDave);
    CHECK(edit.Rows()[3].m_name == "Dave");
    CHECK(RowOf(edit, kAlice).m_perms == share_perm_upload);

    std::vector<ShareMemberPerm> add;
    std::vector<int64_t> remove;
    std::vector<ShareMemberPerm> update;
    CHECK_FALSE(edit.Diff(before, add, remove, update));
    CHECK(add.empty());
    CHECK(remove.empty());
    CHECK(update.empty());

    //Alice 改为上传 + 移动，移除 Bob 与 Dave，追加 Carol（重命名）
    edit.SetPerm(kAlice, share_perm_move, true);
    edit.SetChecked(kBob, false);
    edit.SetChecked(kDave, false);
    edit.SetChecked(kCarol, true);
    edit.SetPerm(kCarol, share_perm_rename, true);
    REQUIRE(edit.Diff(before, add, remove, update));
    REQUIRE(add.size() == 1);
    CHECK(add[0].m_userId == kCarol);
    CHECK(add[0].m_perms == share_perm_rename);
    REQUIRE(remove.size() == 2);
    CHECK(remove[0] == kBob);
    CHECK(remove[1] == kDave);
    REQUIRE(update.size() == 1);
    CHECK(update[0].m_userId == kAlice);
    CHECK(update[0].m_perms == (share_perm_upload | share_perm_move));
}

TEST_CASE("SCM4 mark invalid members", "[fileshare][sharecreate]")
{
    CShareMemberTable table;
    table.Reset(Candidates(), false);
    std::vector<int64_t> invalid;
    invalid.push_back(kBob);
    invalid.push_back(kCarol);
    table.MarkInvalid(invalid);
    CHECK_FALSE(RowOf(table, kAlice).m_invalid);
    CHECK(RowOf(table, kBob).m_invalid);
    CHECK(RowOf(table, kCarol).m_invalid);

    //再次标记时清掉上一次的标记
    table.MarkInvalid(std::vector<int64_t>(1, kAlice));
    CHECK(RowOf(table, kAlice).m_invalid);
    CHECK_FALSE(RowOf(table, kBob).m_invalid);
    table.MarkInvalid(std::vector<int64_t>());
    CHECK_FALSE(RowOf(table, kAlice).m_invalid);
}

TEST_CASE("SCM5 sensitive paths", "[fileshare][sharecreate]")
{
    std::vector<ShareSensitivePath> s;
    s.push_back(Sensitive(L"C:\\Windows", true));
    s.push_back(Sensitive(L"C:\\", false));
    s.push_back(Sensitive(L"C:\\Program Files", true));
    s.push_back(Sensitive(L"C:\\Users\\alice", false));

    //就是敏感目录本身
    CHECK(ShareCreateModel::IsSensitivePath(L"C:\\Windows", s));
    CHECK(ShareCreateModel::IsSensitivePath(L"c:\\windows\\", s));
    CHECK(ShareCreateModel::IsSensitivePath(L"C:\\", s));
    CHECK(ShareCreateModel::IsSensitivePath(L"C:\\Users\\alice", s));
    //是敏感目录的上级
    CHECK(ShareCreateModel::IsSensitivePath(L"C:\\Users", s));
    //位于整棵子树都敏感的目录之下
    CHECK(ShareCreateModel::IsSensitivePath(L"C:\\Windows\\System32", s));
    CHECK(ShareCreateModel::IsSensitivePath(L"C:\\PROGRAM FILES\\Some App", s));
    //用户目录与系统盘根目录之下的普通目录不算
    CHECK_FALSE(ShareCreateModel::IsSensitivePath(L"C:\\Users\\alice\\Documents", s));
    CHECK_FALSE(ShareCreateModel::IsSensitivePath(L"C:\\Work\\proj", s));
    //前缀相同的兄弟目录不算
    CHECK_FALSE(ShareCreateModel::IsSensitivePath(L"C:\\Windows2", s));
    CHECK_FALSE(ShareCreateModel::IsSensitivePath(L"C:\\Program Files Data", s));
    //别的盘
    CHECK_FALSE(ShareCreateModel::IsSensitivePath(L"D:\\", s));
    CHECK_FALSE(ShareCreateModel::IsSensitivePath(L"D:\\Windows", s));
    //空路径、空的敏感位置
    CHECK_FALSE(ShareCreateModel::IsSensitivePath(L"", s));
    CHECK_FALSE(ShareCreateModel::IsSensitivePath(L"C:\\", std::vector<ShareSensitivePath>(1, Sensitive(L"", true))));
}

TEST_CASE("SCM6 display names", "[fileshare][sharecreate]")
{
    CHECK(ShareCreateModel::DisplayName(L"D:\\", share_kind_disk, L"") == L"本地磁盘 (D:)");
    CHECK(ShareCreateModel::DisplayName(L"E:\\", share_kind_disk, L"资料盘") == L"资料盘 (E:)");
    CHECK(ShareCreateModel::DisplayName(L"D:\\Projects\\Flamingo", share_kind_folder, L"") == L"Flamingo");
    CHECK(ShareCreateModel::DisplayName(L"D:\\Projects\\Flamingo\\", share_kind_folder, L"") == L"Flamingo");
    CHECK(ShareCreateModel::DisplayName(L"D:\\docs\\方案.docx", share_kind_file, L"") == L"方案.docx");
}

TEST_CASE("SCM7 local machine info", "[fileshare][sharecreate]")
{
    const std::string a = ShareCreateModel::NewClientShareId();
    const std::string b = ShareCreateModel::NewClientShareId();
    REQUIRE(a.size() == kClientShareIdLen);
    CHECK(a != b);
    for (size_t i = 0; i < a.size(); ++i)
    {
        bool hyphen = false;
        for (size_t h = 0; h < sizeof(kHyphenPos) / sizeof(kHyphenPos[0]); ++h)
            hyphen = hyphen || (kHyphenPos[h] == i);
        if (hyphen)
            CHECK(a[i] == '-');
        else
            CHECK(((a[i] >= '0' && a[i] <= '9') || (a[i] >= 'a' && a[i] <= 'f')));
    }

    wchar_t windows[MAX_PATH] = { 0 };
    REQUIRE(::GetWindowsDirectoryW(windows, MAX_PATH) > 0);
    const std::vector<ShareSensitivePath> sensitive = ShareCreateModel::CollectSensitivePaths();
    CHECK(sensitive.size() >= 4);
    CHECK(ShareCreateModel::IsSensitivePath(windows, sensitive));
    CHECK(ShareCreateModel::IsSensitivePath(std::wstring(windows) + L"\\System32", sensitive));
    CHECK(ShareCreateModel::IsSensitivePath(std::wstring(windows).substr(0, 3), sensitive));

    const std::vector<ShareDriveInfo> drives = ShareCreateModel::ListLocalDrives();
    bool foundSystem = false;
    for (size_t i = 0; i < drives.size(); ++i)
    {
        CHECK(drives[i].m_root.size() == 3);
        if (drives[i].m_system)
        {
            foundSystem = true;
            CHECK(::towupper(drives[i].m_root[0]) == ::towupper(windows[0]));
        }
    }
    CHECK(foundSystem);
}

TEST_CASE("SCM8 error texts", "[fileshare][sharecreate]")
{
    CHECK(ShareCreateModel::CreateErrorText(error_code_share_cross_enterprise).find(L"其他企业") != std::wstring::npos);
    CHECK(ShareCreateModel::CreateErrorText(error_code_share_invalid_member).find(L"不在这个会话") != std::wstring::npos);
    CHECK(ShareCreateModel::CreateErrorText(error_code_share_too_many).find(L"上限") != std::wstring::npos);
    CHECK(ShareCreateModel::CreateErrorText(error_code_share_disabled).find(L"关闭") != std::wstring::npos);
    CHECK(ShareCreateModel::CreateErrorText(12345).find(L"12345") != std::wstring::npos);
}

TEST_CASE("SCM9 group name for a direct share", "[fileshare][sharecreate]")
{
    //放得下：全部列出
    std::vector<std::wstring> two;
    two.push_back(L"Tom");
    two.push_back(L"Jim");
    CHECK(ShareCreateModel::DirectShareGroupName(L"share_direct", two, kShareGroupNameMaxChars)
          == L"共享 share_direct 给 Tom、Jim");

    //人多放不下：少列几个人，以「等 N 人」收尾
    const size_t kManyMembers = 12;
    std::vector<std::wstring> many;
    for (size_t i = 0; i < kManyMembers; ++i)
        many.push_back(L"成员名字" + std::to_wstring(static_cast<unsigned long long>(i + 1)));
    const std::wstring crowded = ShareCreateModel::DirectShareGroupName(L"项目资料", many, kShareGroupNameMaxChars);
    CHECK(crowded.size() <= kShareGroupNameMaxChars);
    CHECK(crowded.find(L"共享 项目资料 给 成员名字1、成员名字2") == 0);
    const std::wstring moreSuffix = L"等12人";
    REQUIRE(crowded.size() > moreSuffix.size());
    CHECK(crowded.compare(crowded.size() - moreSuffix.size(), moreSuffix.size(), moreSuffix) == 0);

    //共享名特别长：只列一个人仍放不下时截短共享名
    const size_t kLongShareName = 100;
    const std::wstring longShare(kLongShareName, L'a');
    const std::wstring cut = ShareCreateModel::DirectShareGroupName(longShare, two, kShareGroupNameMaxChars);
    CHECK(cut.size() <= kShareGroupNameMaxChars);
    CHECK(cut.find(L"共享 aaa") == 0);
    CHECK(cut.find(L"…") != std::wstring::npos);
    const std::wstring cutTail = L" 给 Tom等2人";
    REQUIRE(cut.size() > cutTail.size());
    CHECK(cut.compare(cut.size() - cutTail.size(), cutTail.size(), cutTail) == 0);

    //只有一个人、名字本身就超长：整体截到上限
    std::vector<std::wstring> longMember(1, std::wstring(kLongShareName, L'b'));
    CHECK(ShareCreateModel::DirectShareGroupName(L"x", longMember, kShareGroupNameMaxChars).size() <= kShareGroupNameMaxChars);

    //截断处不拆开代理对：共享名全是表情（每个占两个 UTF-16 码元）
    std::wstring emojis;
    for (size_t i = 0; i < kLongShareName; ++i)
        emojis += L"\U0001F600";
    const std::wstring emojiName = ShareCreateModel::DirectShareGroupName(emojis, two, kShareGroupNameMaxChars);
    CHECK(emojiName.size() <= kShareGroupNameMaxChars);
    for (size_t i = 0; i < emojiName.size(); ++i)
    {
        //高位代理之后必须紧跟低位代理
        if (emojiName[i] >= 0xD800 && emojiName[i] <= 0xDBFF)
        {
            REQUIRE(i + 1 < emojiName.size());
            CHECK((emojiName[i + 1] >= 0xDC00 && emojiName[i + 1] <= 0xDFFF));
        }
    }
}

TEST_CASE("SCM10 share name default and resolve", "[fileshare][sharecreate]")
{
    //超出上限的字符数（构造超长名字用）
    const size_t kOverChars = 5;

    //默认名称：「XXX的文件共享」；不知道本人名字时用「我的文件共享」整句
    CHECK(ShareCreateModel::DefaultShareName(L"张勇") == L"张勇的文件共享");
    CHECK(ShareCreateModel::DefaultShareName(L"") == L"我的文件共享");

    //没填、只有空白（含全角空格）：用默认名称
    CHECK(ShareCreateModel::ResolveShareName(L"", L"张勇") == L"张勇的文件共享");
    CHECK(ShareCreateModel::ResolveShareName(L" \t\x3000 ", L"张勇") == L"张勇的文件共享");

    //填了：去掉首尾空白，中间的空白保留
    CHECK(ShareCreateModel::ResolveShareName(L"\x3000 Q3 资料 ", L"张勇") == L"Q3 资料");

    //超过上限截短到 kShareTitleNameMaxChars
    const std::wstring longName(kShareTitleNameMaxChars + kOverChars, L'名');
    CHECK(ShareCreateModel::ResolveShareName(longName, L"张勇") == std::wstring(kShareTitleNameMaxChars, L'名'));

    //截断处落在代理对中间时少取一个，不留下孤立的高位代理
    std::wstring emoji(kShareTitleNameMaxChars - 1, L'a');
    emoji += L"\U0001F600";
    emoji += L"tail";
    CHECK(ShareCreateModel::ResolveShareName(emoji, L"张勇") == std::wstring(kShareTitleNameMaxChars - 1, L'a'));

    //本人名字很长、默认名称超过上限时同样截短
    const std::wstring longSelf(kShareTitleNameMaxChars, L'x');
    CHECK(ShareCreateModel::ResolveShareName(L"", longSelf).size() == kShareTitleNameMaxChars);

    //共享名称命中敏感词（822）的提示文字
    CHECK(ShareCreateModel::CreateErrorText(error_code_share_name_sensitive).find(L"敏感词") != std::wstring::npos);
}
