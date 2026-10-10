/**
 *  主面板搜索下拉结果行文案的单元测试（Suite GST），GlobalSearchTextTests.cpp
 *
 *  覆盖 balloonsearch 的 GlobalSearchText：
 *    GST1  FindKeywordNoCase：ASCII 字母不区分大小写、中文原样比较、找不到与空关键字返回 -1；
 *    GST2  关键字靠前时不截断，高亮区间就是关键字所在位置；
 *    GST3  关键字前面的文字过长时截去开头、以省略号起头，保留 kSnippetLeadChars 个字的上文；
 *    GST4  换行、回车、制表符换成空格，高亮下标不因此偏移；
 *    GST5  截断点落在代理对后半时往后挪一个字符，片段不以半个字开头；
 *    GST6  超长文字截到 kSnippetMaxChars，关键字仍完整保留在片段里；
 *    GST7  找不到关键字时返回开头部分、不带高亮；
 *    GST8  BuildMessageHitSubText：命中一条显示片段并高亮，命中多条显示「N 条相关聊天记录」；
 *    GST9  FormatHitTime：今天、昨天（含跨月与跨年）、今年其余日子、更早四种格式；
 *    GST10 BuildFileHitTexts：四种来源的副行文案，缺哪段就省哪段、不留多余的分隔点；
 *    GST11 BuildFileHitTexts：文件名里的关键字标黄，名字过长时截去开头；
 *    GST12 DescribeLocalFileRecord：文件、文件夹、共享卡片三类取名字、大小与图标类型，
 *          内容缺字段时名字退回搜索文本；
 *    GST13 PickAllTabSection：「全部」标签每段最多 3 条，先取第一个来源再取第二个，
 *          合计超出时才显示「查看更多」；
 *    GST14 ComputeSearchTabCounts：聊天记录、本地文件各自在后台查询结果回来后才计入，
 *          群文件应答成功到达后才计入，「全部」为各类之和；
 *    GST15 PickFileHitActions：本机有文件的聊天文件给「打开文件」「打开所在文件夹」，文件夹
 *          传输记录只给打开文件夹本身，共享卡片、群文件与本机没有的都不给；
 *    GST16 BuildDepartmentParentPath：从顶级到直接上级用「 / 」连接，不含部门自己；顶级部门、上级
 *          查不到时为空串；名字为空的上级跳过；上级链成环时不会死循环；
 *    GST17 BuildMemoHitTexts：标题或备注命中才算命中；标题里的关键字标黄；备注命中时副行接上
 *          「 · 」与片段，高亮下标算上前面的归属文字；英文字母不区分大小写；
 *    GST18 MemoStartToSystemTime：十进制 yyyymmddhhmm 拆成本地时间，非法取值返回 false。
 *  框架 Catch2 v3。
 *
 *  balloonwj@qq.com   2026-10-03
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"
#include "../UI/BalloonSearch/GlobalSearchText.h"

#include <string>

using balloonsearch::HighlightedText;

namespace {

// 构造一个本地时间（只填年月日时分，其余字段为 0）。
SYSTEMTIME MakeTime(int year, int month, int day, int hour, int minute)
{
    SYSTEMTIME st;
    ::ZeroMemory(&st, sizeof(st));
    st.wYear   = (WORD)year;
    st.wMonth  = (WORD)month;
    st.wDay    = (WORD)day;
    st.wHour   = (WORD)hour;
    st.wMinute = (WORD)minute;
    return st;
}

// 取片段里高亮区间对应的那段文字，便于直接与关键字比较。
std::wstring HighlightedPart(const HighlightedText& t)
{
    if (t.m_hiStart < 0 || t.m_hiLen <= 0)
    {
        return std::wstring();
    }
    return t.m_text.substr((size_t)t.m_hiStart, (size_t)t.m_hiLen);
}

} // namespace

TEST_CASE("GST1 FindKeywordNoCase 只对 ASCII 字母不区分大小写", "[globalsearch]")
{
    CHECK(balloonsearch::FindKeywordNoCase(L"Hello World", L"world") == 6);
    CHECK(balloonsearch::FindKeywordNoCase(L"hello world", L"WORLD") == 6);
    CHECK(balloonsearch::FindKeywordNoCase(L"明天下午开会", L"下午") == 2);
    CHECK(balloonsearch::FindKeywordNoCase(L"明天下午开会", L"上午") == -1);
    CHECK(balloonsearch::FindKeywordNoCase(L"明天下午开会", L"") == -1);
    CHECK(balloonsearch::FindKeywordNoCase(L"短", L"更长的关键字") == -1);
    //全角字母不做大小写折叠，与 SQLite LIKE 一致。
    CHECK(balloonsearch::FindKeywordNoCase(L"\xFF21\xFF22", L"\xFF41\xFF42") == -1);
}

TEST_CASE("GST2 关键字靠前时不截断", "[globalsearch]")
{
    HighlightedText t = balloonsearch::MakeHitSnippet(L"周五下午三点开评审会", L"三点",
                                                      balloonsearch::kSnippetLeadChars);
    CHECK(t.m_text == L"周五下午三点开评审会");
    CHECK(t.m_hiStart == 4);
    CHECK(t.m_hiLen == 2);
    CHECK(HighlightedPart(t) == L"三点");
}

TEST_CASE("GST3 关键字前面过长时截去开头并以省略号起头", "[globalsearch]")
{
    const std::wstring text = L"0123456789ABCDEFGHIJ评审会议纪要";
    HighlightedText t = balloonsearch::MakeHitSnippet(text, L"评审", balloonsearch::kSnippetLeadChars);

    //关键字在下标 20，保留它前面 6 个字符 "EFGHIJ"，再在最前面放一个省略号。
    CHECK(t.m_text == std::wstring(L"…") + L"EFGHIJ评审会议纪要");
    CHECK(t.m_hiStart == 1 + balloonsearch::kSnippetLeadChars);
    CHECK(HighlightedPart(t) == L"评审");

    //恰好等于 leadChars 时不截断。
    HighlightedText exact = balloonsearch::MakeHitSnippet(L"123456评审", L"评审", 6);
    CHECK(exact.m_text == L"123456评审");
    CHECK(exact.m_hiStart == 6);
}

TEST_CASE("GST4 控制字符换成空格且下标不偏移", "[globalsearch]")
{
    HighlightedText t = balloonsearch::MakeHitSnippet(L"第一行\r\n第二\t行", L"第二", 10);
    CHECK(t.m_text == L"第一行  第二 行");
    CHECK(t.m_hiStart == 5);
    CHECK(HighlightedPart(t) == L"第二");
}

TEST_CASE("GST5 截断点不落在代理对中间", "[globalsearch]")
{
    //U+1F600（笑脸）在 UTF-16 里占两个单元 D83D DE00。leadChars=2 时，截断点本应落在
    //低代理 DE00 上，实际要往后挪一个，片段从 "x" 开始。
    const std::wstring text = std::wstring(L"abc\xD83D\xDE00") + L"x关键字";
    HighlightedText t = balloonsearch::MakeHitSnippet(text, L"关键字", 2);
    CHECK(t.m_text == std::wstring(L"…") + L"x关键字");
    CHECK(HighlightedPart(t) == L"关键字");
}

TEST_CASE("GST6 超长文字截到上限且关键字完整保留", "[globalsearch]")
{
    const std::wstring tail(500, L'z');
    const std::wstring text = L"开头文字关键字" + tail;
    HighlightedText t = balloonsearch::MakeHitSnippet(text, L"关键字", balloonsearch::kSnippetLeadChars);
    CHECK(t.m_text.size() == (size_t)balloonsearch::kSnippetMaxChars);
    CHECK(HighlightedPart(t) == L"关键字");

    //关键字本身超出上限时，片段延长到关键字末尾为止。
    const std::wstring longKeyword(balloonsearch::kSnippetMaxChars + 10, L'k');
    HighlightedText k = balloonsearch::MakeHitSnippet(L"ab" + longKeyword + L"cd", longKeyword, 6);
    CHECK(HighlightedPart(k) == longKeyword);
}

TEST_CASE("GST7 找不到关键字时只显示开头部分", "[globalsearch]")
{
    const std::wstring text(300, L'a');
    HighlightedText t = balloonsearch::MakeHitSnippet(text, L"不存在", 6);
    CHECK(t.m_text.size() == (size_t)balloonsearch::kSnippetMaxChars);
    CHECK(t.m_hiStart == -1);
    CHECK(t.m_hiLen == 0);
}

TEST_CASE("GST8 命中一条显示片段，命中多条显示条数", "[globalsearch]")
{
    HighlightedText one = balloonsearch::BuildMessageHitSubText(1, L"明天交周报", L"周报");
    CHECK(one.m_text == L"明天交周报");
    CHECK(HighlightedPart(one) == L"周报");

    HighlightedText many = balloonsearch::BuildMessageHitSubText(12, L"明天交周报", L"周报");
    CHECK(many.m_text == L"12 条相关聊天记录");
    CHECK(many.m_hiStart == -1);

    //只命中一条但文字为空时退回条数文案；条数不大于 0 时按 1 处理。
    HighlightedText empty = balloonsearch::BuildMessageHitSubText(1, L"", L"周报");
    CHECK(empty.m_text == L"1 条相关聊天记录");
    HighlightedText zero = balloonsearch::BuildMessageHitSubText(0, L"", L"周报");
    CHECK(zero.m_text == L"1 条相关聊天记录");
}

TEST_CASE("GST9 命中时间按会话列表的规则格式化", "[globalsearch]")
{
    const SYSTEMTIME now = MakeTime(2026, 10, 3, 19, 45);

    //今天
    CHECK(balloonsearch::FormatHitTime(MakeTime(2026, 10, 3, 9, 5), now) == L"09:05");
    //昨天
    CHECK(balloonsearch::FormatHitTime(MakeTime(2026, 10, 2, 23, 59), now) == L"昨天");
    //今年其余日子
    CHECK(balloonsearch::FormatHitTime(MakeTime(2026, 10, 1, 8, 0), now) == L"10-01");
    CHECK(balloonsearch::FormatHitTime(MakeTime(2026, 1, 15, 8, 0), now) == L"01-15");
    //更早
    CHECK(balloonsearch::FormatHitTime(MakeTime(2025, 12, 30, 8, 0), now) == L"2025-12-30");

    //跨月：10 月 1 日看 9 月 30 日是昨天
    CHECK(balloonsearch::FormatHitTime(MakeTime(2026, 9, 30, 12, 0),
                                       MakeTime(2026, 10, 1, 8, 0)) == L"昨天");
    //跨年：1 月 1 日看去年 12 月 31 日是昨天，不是"更早"
    CHECK(balloonsearch::FormatHitTime(MakeTime(2025, 12, 31, 22, 0),
                                       MakeTime(2026, 1, 1, 0, 30)) == L"昨天");

    //时间不大于 0 时返回空串。
    CHECK(balloonsearch::FormatHitTimeFromUnixMs(0).empty());
}

TEST_CASE("GST10 文件结果的副行按来源拼接且不留多余的分隔点", "[globalsearch]")
{
    using balloonsearch::BuildFileHitTexts;

    //聊天文件不写种类
    CHECK(BuildFileHitTexts(balloonsearch::kFileHitChatFile, L"周报.docx", L"张三", L"1.8 MB", L"周报").m_sub
          == L"张三 · 1.8 MB");
    //文件夹
    CHECK(BuildFileHitTexts(balloonsearch::kFileHitFolder, L"项目资料", L"研发群", L"2.4 GB", L"项目").m_sub
          == L"文件夹 · 研发群 · 2.4 GB");
    //共享卡片不显示大小
    CHECK(BuildFileHitTexts(balloonsearch::kFileHitFileShare, L"设计稿", L"李四", L"10 MB", L"设计").m_sub
          == L"共享 · 李四");
    //群文件
    CHECK(BuildFileHitTexts(balloonsearch::kFileHitGroupFile, L"合同.pdf", L"法务群", L"300 KB", L"合同").m_sub
          == L"群文件 · 法务群 · 300 KB");

    //缺来源、缺大小
    CHECK(BuildFileHitTexts(balloonsearch::kFileHitChatFile, L"a.txt", L"", L"1 KB", L"a").m_sub == L"1 KB");
    CHECK(BuildFileHitTexts(balloonsearch::kFileHitChatFile, L"a.txt", L"张三", L"", L"a").m_sub == L"张三");
    CHECK(BuildFileHitTexts(balloonsearch::kFileHitChatFile, L"a.txt", L"", L"", L"a").m_sub.empty());
    CHECK(BuildFileHitTexts(balloonsearch::kFileHitGroupFile, L"a.txt", L"", L"", L"a").m_sub == L"群文件");
    //不认识的来源按聊天文件处理
    CHECK(BuildFileHitTexts(99, L"a.txt", L"张三", L"1 KB", L"a").m_sub == L"张三 · 1 KB");
}

TEST_CASE("GST11 文件名里的关键字标黄，名字过长时截去开头", "[globalsearch]")
{
    using balloonsearch::BuildFileHitTexts;

    balloonsearch::FileHitTexts t =
        BuildFileHitTexts(balloonsearch::kFileHitChatFile, L"2026年第三季度经营分析报告.xlsx", L"", L"", L"报告");
    //「报」在下标 13，保留它前面 6 个字「季度经营分析」
    CHECK(t.m_name.m_text == std::wstring(L"…") + L"季度经营分析报告.xlsx");
    CHECK(HighlightedPart(t.m_name) == L"报告");

    //英文文件名大小写不敏感
    balloonsearch::FileHitTexts e =
        BuildFileHitTexts(balloonsearch::kFileHitGroupFile, L"Readme.MD", L"", L"", L"readme");
    CHECK(e.m_name.m_text == L"Readme.MD");
    CHECK(HighlightedPart(e.m_name) == L"Readme");
}

TEST_CASE("GST12 从本地库记录取文件结果行的名字、大小与图标类型", "[globalsearch]")
{
    using balloonsearch::DescribeLocalFileRecord;
    using balloonsearch::LocalFileHitInfo;

    //消息类型与共享类型的取值（与 net/Protocol.h 一致）
    const int kTypeFile      = 3;
    const int kTypeFolder    = 18;
    const int kTypeFileShare = 20;
    const int kIconDisk      = 1;
    const int kIconFolder    = 2;
    const int kIconFile      = 3;

    //文件：名字与大小取自 file 子对象，大小接受字符串写法
    LocalFileHitInfo f = DescribeLocalFileRecord(
        kTypeFile, "{\"file\":{\"name\":\"周报.docx\",\"size\":\"2048\",\"fileid\":\"9\"}}", "周报.docx");
    CHECK(f.m_kind == balloonsearch::kFileHitChatFile);
    CHECK(f.m_nameUtf8 == "周报.docx");
    CHECK(f.m_sizeBytes == 2048);
    CHECK(f.m_iconKind == kIconFile);

    //文件夹：名字是文件夹名，大小是总字节数
    LocalFileHitInfo d = DescribeLocalFileRecord(
        kTypeFolder, "{\"folder\":{\"foldername\":\"项目资料\",\"totalfiles\":12,\"totalsize\":4096}}", "项目资料");
    CHECK(d.m_kind == balloonsearch::kFileHitFolder);
    CHECK(d.m_nameUtf8 == "项目资料");
    CHECK(d.m_sizeBytes == 4096);
    CHECK(d.m_iconKind == kIconFolder);

    //共享卡片：名字是搜索文本，图标按共享类型；没有大小
    LocalFileHitInfo s = DescribeLocalFileRecord(
        kTypeFileShare, "{\"share\":{\"shareid\":\"5\",\"kind\":1,\"name\":\"D盘\"}}", "D盘");
    CHECK(s.m_kind == balloonsearch::kFileHitFileShare);
    CHECK(s.m_nameUtf8 == "D盘");
    CHECK(s.m_sizeBytes == 0);
    CHECK(s.m_iconKind == kIconDisk);

    //共享卡片内容解析不了：图标按文件夹显示
    LocalFileHitInfo bad = DescribeLocalFileRecord(kTypeFileShare, "not json", "设计稿");
    CHECK(bad.m_nameUtf8 == "设计稿");
    CHECK(bad.m_iconKind == kIconFolder);

    //文件内容缺 name 字段：名字退回搜索文本
    LocalFileHitInfo noName = DescribeLocalFileRecord(kTypeFile, "{\"file\":{}}", "合同.pdf");
    CHECK(noName.m_nameUtf8 == "合同.pdf");
    CHECK(noName.m_sizeBytes == 0);
}

TEST_CASE("GST13 「全部」标签每段最多 3 条，合计超出时才显示查看更多", "[globalsearch]")
{
    using balloonsearch::AllTabSectionPick;
    using balloonsearch::PickAllTabSection;

    //每段上限（与 SearchDropdown.cpp 的 kAllTabSectionMax 一致）
    const size_t kMax = 3;

    //聊天记录段：只有一个来源
    AllTabSectionPick a = PickAllTabSection(2, 0, kMax);
    CHECK(a.m_firstShown == 2);
    CHECK(a.m_secondShown == 0);
    CHECK_FALSE(a.m_showMore);

    AllTabSectionPick b = PickAllTabSection(3, 0, kMax);
    CHECK(b.m_firstShown == 3);
    CHECK_FALSE(b.m_showMore);

    AllTabSectionPick c = PickAllTabSection(7, 0, kMax);
    CHECK(c.m_firstShown == 3);
    CHECK(c.m_showMore);

    //文件段：先列本地聊天文件，不够再列群文件
    AllTabSectionPick d = PickAllTabSection(1, 1, kMax);
    CHECK(d.m_firstShown == 1);
    CHECK(d.m_secondShown == 1);
    CHECK_FALSE(d.m_showMore);

    AllTabSectionPick e = PickAllTabSection(1, 5, kMax);
    CHECK(e.m_firstShown == 1);
    CHECK(e.m_secondShown == 2);
    CHECK(e.m_showMore);

    AllTabSectionPick f = PickAllTabSection(4, 2, kMax);
    CHECK(f.m_firstShown == 3);
    CHECK(f.m_secondShown == 0);
    CHECK(f.m_showMore);

    AllTabSectionPick g = PickAllTabSection(0, 3, kMax);
    CHECK(g.m_firstShown == 0);
    CHECK(g.m_secondShown == 3);
    CHECK_FALSE(g.m_showMore);

    AllTabSectionPick none = PickAllTabSection(0, 0, kMax);
    CHECK(none.m_firstShown == 0);
    CHECK(none.m_secondShown == 0);
    CHECK_FALSE(none.m_showMore);
}

TEST_CASE("GST14 各标签计数随后台查询结果与群文件应答逐步计入", "[globalsearch]")
{
    using balloonsearch::ComputeSearchTabCounts;
    using balloonsearch::SearchTabCounts;

    //刚输入、后台查询结果都还没回来：只计联系人与群组，「消息」「文件」不显示数字
    SearchTabCounts before = ComputeSearchTabCounts(2, 1, 5, 4, 3, false, false, false);
    CHECK(before.m_all == 3);
    CHECK_FALSE(before.m_showMessages);
    CHECK_FALSE(before.m_showFiles);
    CHECK(before.m_messages == 0);
    CHECK(before.m_files == 0);

    //聊天记录先回来（「文件」查询还在排队）：只计入聊天记录
    SearchTabCounts msgOnly = ComputeSearchTabCounts(2, 1, 5, 4, 3, true, false, false);
    CHECK(msgOnly.m_showMessages);
    CHECK_FALSE(msgOnly.m_showFiles);
    CHECK(msgOnly.m_messages == 5);
    CHECK(msgOnly.m_files == 0);
    CHECK(msgOnly.m_all == 2 + 1 + 5);

    //本地文件也回来了、群文件应答未到：计入本地文件，沿用的群文件不计
    SearchTabCounts local = ComputeSearchTabCounts(2, 1, 5, 4, 3, true, true, false);
    CHECK(local.m_showFiles);
    CHECK(local.m_files == 4);
    CHECK(local.m_all == 2 + 1 + 5 + 4);

    //群文件应答成功到达：群文件也计入
    SearchTabCounts done = ComputeSearchTabCounts(2, 1, 5, 4, 3, true, true, true);
    CHECK(done.m_files == 4 + 3);
    CHECK(done.m_all == 2 + 1 + 5 + 4 + 3);

    //全部为 0
    SearchTabCounts zero = ComputeSearchTabCounts(0, 0, 0, 0, 0, true, true, true);
    CHECK(zero.m_all == 0);
    CHECK(zero.m_files == 0);
}

TEST_CASE("GST15 本机文件行悬停时提供的快捷操作", "[globalsearch]")
{
    using balloonsearch::FileHitActions;
    using balloonsearch::PickFileHitActions;
    using balloonsearch::kFileHitChatFile;
    using balloonsearch::kFileHitFolder;
    using balloonsearch::kFileHitFileShare;
    using balloonsearch::kFileHitGroupFile;

    //聊天文件、本机有：打开文件 + 打开所在文件夹（选中该文件）
    FileHitActions file = PickFileHitActions(kFileHitChatFile, true);
    CHECK(file.m_openFile);
    CHECK(file.m_openFolder);
    CHECK_FALSE(file.m_folderIsTarget);

    //文件夹传输记录、本机有：只打开文件夹本身
    FileHitActions folder = PickFileHitActions(kFileHitFolder, true);
    CHECK_FALSE(folder.m_openFile);
    CHECK(folder.m_openFolder);
    CHECK(folder.m_folderIsTarget);

    //共享卡片与群文件：没有本机文件的概念，都不给
    FileHitActions share = PickFileHitActions(kFileHitFileShare, true);
    CHECK_FALSE(share.m_openFile);
    CHECK_FALSE(share.m_openFolder);
    FileHitActions group = PickFileHitActions(kFileHitGroupFile, true);
    CHECK_FALSE(group.m_openFile);
    CHECK_FALSE(group.m_openFolder);

    //本机找不到：都不给
    FileHitActions missing = PickFileHitActions(kFileHitChatFile, false);
    CHECK_FALSE(missing.m_openFile);
    CHECK_FALSE(missing.m_openFolder);
    FileHitActions missingFolder = PickFileHitActions(kFileHitFolder, false);
    CHECK_FALSE(missingFolder.m_openFolder);
}

TEST_CASE("GST16 部门的上级路径", "[globalsearch]")
{
    using balloonsearch::BuildDepartmentParentPath;
    //公司(1) > 技术中心(2) > 研发部(3) > 前端组(4)；空名部门(5) 挂在 2 下，6 挂在 5 下
    std::map<int64_t, int64_t> parentOf;
    std::map<int64_t, std::wstring> nameOf;
    parentOf[1] = 0;
    nameOf[1] = L"公司";
    parentOf[2] = 1;
    nameOf[2] = L"技术中心";
    parentOf[3] = 2;
    nameOf[3] = L"研发部";
    parentOf[4] = 3;
    nameOf[4] = L"前端组";
    parentOf[5] = 2;
    nameOf[5] = L"";
    parentOf[6] = 5;
    nameOf[6] = L"测试组";

    CHECK(BuildDepartmentParentPath(4, parentOf, nameOf) == L"公司 / 技术中心 / 研发部");
    CHECK(BuildDepartmentParentPath(2, parentOf, nameOf) == L"公司");
    CHECK(BuildDepartmentParentPath(1, parentOf, nameOf).empty());
    //名字为空的上级跳过
    CHECK(BuildDepartmentParentPath(6, parentOf, nameOf) == L"公司 / 技术中心");
    //不在表里的部门、上级查不到
    CHECK(BuildDepartmentParentPath(99, parentOf, nameOf).empty());
    parentOf[7] = 98;
    nameOf[7] = L"孤儿部门";
    CHECK(BuildDepartmentParentPath(7, parentOf, nameOf).empty());

    //上级链成环：8 → 9 → 8，在上限内停下，不会死循环
    parentOf[8] = 9;
    nameOf[8] = L"甲";
    parentOf[9] = 8;
    nameOf[9] = L"乙";
    const std::wstring loop = BuildDepartmentParentPath(8, parentOf, nameOf);
    CHECK(loop == L"乙");
}

TEST_CASE("GST17 备忘结果行的文案", "[globalsearch]")
{
    using balloonsearch::BuildMemoHitTexts;
    using balloonsearch::MemoHitTexts;

    //标题命中：主行标出关键字，副行只有归属
    MemoHitTexts t = BuildMemoHitTexts(L"交设计稿", L"周五之前", L"个人备忘", L"设计稿");
    REQUIRE(t.m_matched);
    CHECK(t.m_name.m_text == L"交设计稿");
    CHECK(t.m_name.m_hiStart == 1);
    CHECK(t.m_name.m_hiLen == 3);
    CHECK(t.m_sub.m_text == L"个人备忘");
    CHECK(t.m_sub.m_hiStart == -1);

    //备注命中：副行接上片段，高亮下标加上「群名 · 」的长度
    MemoHitTexts n = BuildMemoHitTexts(L"周会", L"带上设计稿", L"产品组", L"设计稿");
    REQUIRE(n.m_matched);
    CHECK(n.m_name.m_hiStart == -1);
    CHECK(n.m_sub.m_text == std::wstring(L"产品组 \x00B7 带上设计稿"));
    CHECK(n.m_sub.m_hiStart == 3 + 3 + 2);
    CHECK(n.m_sub.m_hiLen == 3);

    //英文字母不区分大小写
    CHECK(BuildMemoHitTexts(L"Weekly REPORT", L"", L"个人备忘", L"report").m_matched);
    //都不命中、关键字为空
    CHECK_FALSE(BuildMemoHitTexts(L"周会", L"带上材料", L"个人备忘", L"设计稿").m_matched);
    CHECK_FALSE(BuildMemoHitTexts(L"周会", L"带上材料", L"个人备忘", L"").m_matched);
}

TEST_CASE("GST18 备忘首次事件时间的换算", "[globalsearch]")
{
    SYSTEMTIME st;
    REQUIRE(balloonsearch::MemoStartToSystemTime(202610040930LL, st));
    CHECK(st.wYear == 2026);
    CHECK(st.wMonth == 10);
    CHECK(st.wDay == 4);
    CHECK(st.wHour == 9);
    CHECK(st.wMinute == 30);
    CHECK(st.wSecond == 0);
    CHECK_FALSE(balloonsearch::MemoStartToSystemTime(0, st));
    CHECK_FALSE(balloonsearch::MemoStartToSystemTime(202613010000LL, st));   //13 月
    CHECK_FALSE(balloonsearch::MemoStartToSystemTime(202610042460LL, st));   //24 时
    CHECK_FALSE(balloonsearch::MemoStartToSystemTime(202610040960LL, st));   //60 分
}
