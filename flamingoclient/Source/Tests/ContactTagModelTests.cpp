/**
 *  ContactTagModelTests.cpp
 *
 *  联系人标签纯逻辑的测试（Suite CTM）：
 *    · NormalizeTag —— 去首尾空白的口径与各种退化输入。
 *    · FindTagIndexIgnoreCase —— 忽略大小写查找、空串不匹配。
 *    · NormalizeTagList —— 顺序、去空、忽略大小写去重、不限制条数。
 *    · DecideTagAdd —— 追加 / 取消 / 重复 / 超长 / 已满 / 离线的判定顺序。
 *    · DecideTagEdit —— 更新 / 无变化 / 删除 / 合并 / 超长 / 越界 / 离线。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不碰网络。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-14
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonContact/ContactTagModel.h"

using namespace ballooncontact;

namespace {

// 按可变参数拼一个标签列表，省得每个用例都写四行 push_back。
// count 为 0 时返回空列表。
std::vector<CString> MakeTags(const TCHAR* const* items, int count)
{
    std::vector<CString> tags;
    for (int i = 0; i < count; ++i)
    {
        tags.push_back(CString(items[i]));
    }
    return tags;
}

// 多数用例共用的一份三元素列表：「朋友」「设计」「Work」。最后一项特意用
// 英文，好让忽略大小写的分支有东西可测。
std::vector<CString> SampleTags()
{
    const TCHAR* const kItems[] = { _T("朋友"), _T("设计"), _T("Work") };
    return MakeTags(kItems, 3);
}

// 造一个正好 n 个字符的标签（全用 'a'），用来测长度边界。
CString RepeatChar(int n)
{
    CString text;
    for (int i = 0; i < n; ++i)
    {
        text += _T('a');
    }
    return text;
}

// 造一个满额（kContactTagMaxCount 条）的标签列表，用来测"已满"分支。
std::vector<CString> FullTags()
{
    std::vector<CString> tags;
    for (int i = 0; i < kContactTagMaxCount; ++i)
    {
        CString one;
        one.Format(_T("标签%d"), i);
        tags.push_back(one);
    }
    return tags;
}

} // anonymous namespace

// =======================================================================
// NormalizeTag —— 去首尾空白
// =======================================================================

TEST_CASE("CTM1 归一：普通文本原样返回", "[contacttag]")
{
    CHECK(NormalizeTag(_T("朋友")) == _T("朋友"));
    CHECK(NormalizeTag(_T("Work")) == _T("Work"));
}

TEST_CASE("CTM2 归一：去掉首尾空格", "[contacttag]")
{
    CHECK(NormalizeTag(_T("  朋友")) == _T("朋友"));
    CHECK(NormalizeTag(_T("朋友  ")) == _T("朋友"));
    CHECK(NormalizeTag(_T("   朋友   ")) == _T("朋友"));
}

TEST_CASE("CTM3 归一：制表符与换行同样算首尾空白", "[contacttag]")
{
    // 用户从别处粘贴进来的文本常带这些字符，只判空格会让它们混进标签文本里，
    // 存进库之后再也看不出来是哪里多出来的。
    CHECK(NormalizeTag(_T("\t朋友\t")) == _T("朋友"));
    CHECK(NormalizeTag(_T("\r\n朋友\r\n")) == _T("朋友"));
    CHECK(NormalizeTag(_T(" \t\r\n朋友 \t\r\n")) == _T("朋友"));
}

TEST_CASE("CTM4 归一：空串与全空白都归一成空串", "[contacttag]")
{
    CHECK(NormalizeTag(_T("")).IsEmpty());
    CHECK(NormalizeTag(_T("   ")).IsEmpty());
    CHECK(NormalizeTag(_T("\t\r\n ")).IsEmpty());
}

TEST_CASE("CTM5 归一：传 NULL 不崩溃，返回空串", "[contacttag]")
{
    // 控件取文本失败时可能给到空指针，纯函数必须自己兜住。
    CHECK(NormalizeTag(NULL).IsEmpty());
}

TEST_CASE("CTM6 归一：中间的空格必须保留", "[contacttag]")
{
    // 「产品 设计」与「产品设计」是两个不同的标签，不能因为归一被并成一个。
    CHECK(NormalizeTag(_T("  产品 设计  ")) == _T("产品 设计"));
    CHECK(NormalizeTag(_T("New Year")) == _T("New Year"));
}

// =======================================================================
// FindTagIndexIgnoreCase —— 忽略大小写查找
// =======================================================================

TEST_CASE("CTM7 查找：大小写不同也算命中", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    CHECK(FindTagIndexIgnoreCase(tags, _T("work")) == 2);
    CHECK(FindTagIndexIgnoreCase(tags, _T("WORK")) == 2);
    CHECK(FindTagIndexIgnoreCase(tags, _T("Work")) == 2);
}

TEST_CASE("CTM8 查找：比较前两侧都会去首尾空白", "[contacttag]")
{
    const TCHAR* const kItems[] = { _T("  朋友 ") };
    const std::vector<CString> tags = MakeTags(kItems, 1);
    CHECK(FindTagIndexIgnoreCase(tags, _T("朋友")) == 0);
    CHECK(FindTagIndexIgnoreCase(tags, _T(" 朋友  ")) == 0);
}

TEST_CASE("CTM9 查找：未命中返回 -1", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    CHECK(FindTagIndexIgnoreCase(tags, _T("同事")) == -1);
    // 中间带空格的是另一个标签，不该被当成已有的那个。
    CHECK(FindTagIndexIgnoreCase(tags, _T("Wo rk")) == -1);
}

TEST_CASE("CTM10 查找：空列表恒返回 -1", "[contacttag]")
{
    const std::vector<CString> tags;
    CHECK(FindTagIndexIgnoreCase(tags, _T("朋友")) == -1);
}

TEST_CASE("CTM11 查找：空串与全空白永远查不到", "[contacttag]")
{
    // 列表里本来就不该有空标签；让空串命中只会把调用方引向"重复"这种错误分支。
    const std::vector<CString> tags = SampleTags();
    CHECK(FindTagIndexIgnoreCase(tags, _T("")) == -1);
    CHECK(FindTagIndexIgnoreCase(tags, _T("   ")) == -1);
}

// =======================================================================
// NormalizeTagList —— 整份列表归一
// =======================================================================

TEST_CASE("CTM12 列表归一：顺序保持不变", "[contacttag]")
{
    const std::vector<CString> input = SampleTags();
    std::vector<CString> output;
    NormalizeTagList(input, output);
    REQUIRE(output.size() == 3);
    CHECK(output[0] == _T("朋友"));
    CHECK(output[1] == _T("设计"));
    CHECK(output[2] == _T("Work"));
}

TEST_CASE("CTM13 列表归一：逐项去掉首尾空白", "[contacttag]")
{
    const TCHAR* const kItems[] = { _T("  朋友"), _T("设计\t"), _T("\r\nWork ") };
    const std::vector<CString> input = MakeTags(kItems, 3);
    std::vector<CString> output;
    NormalizeTagList(input, output);
    REQUIRE(output.size() == 3);
    CHECK(output[0] == _T("朋友"));
    CHECK(output[1] == _T("设计"));
    CHECK(output[2] == _T("Work"));
}

TEST_CASE("CTM14 列表归一：完全重复的只留一条", "[contacttag]")
{
    const TCHAR* const kItems[] = { _T("朋友"), _T("设计"), _T("朋友") };
    const std::vector<CString> input = MakeTags(kItems, 3);
    std::vector<CString> output;
    NormalizeTagList(input, output);
    REQUIRE(output.size() == 2);
    CHECK(output[0] == _T("朋友"));
    CHECK(output[1] == _T("设计"));
}

TEST_CASE("CTM15 列表归一：大小写重复保留首次出现者的原始写法", "[contacttag]")
{
    const TCHAR* const kItems[] = { _T("Work"), _T("设计"), _T("WORK"), _T("work") };
    const std::vector<CString> input = MakeTags(kItems, 4);
    std::vector<CString> output;
    NormalizeTagList(input, output);
    REQUIRE(output.size() == 2);
    // 留下来的必须是第一条那个「Work」，而不是后面两条里的任何一个写法。
    CHECK(output[0] == _T("Work"));
    CHECK(output[1] == _T("设计"));
}

TEST_CASE("CTM16 列表归一：空串与全空白项被丢弃", "[contacttag]")
{
    const TCHAR* const kItems[] = { _T("朋友"), _T(""), _T("   "), _T("\t\r\n"), _T("设计") };
    const std::vector<CString> input = MakeTags(kItems, 5);
    std::vector<CString> output;
    NormalizeTagList(input, output);
    REQUIRE(output.size() == 2);
    CHECK(output[0] == _T("朋友"));
    CHECK(output[1] == _T("设计"));
}

TEST_CASE("CTM17 列表归一：空列表是合法输入", "[contacttag]")
{
    // 清空全部标签也要能提交，空列表就是"这个联系人一个标签都没有"。
    const std::vector<CString> input;
    std::vector<CString> output;
    output.push_back(_T("残留"));   // 故意先塞一条，验证函数内部会先清空出参
    NormalizeTagList(input, output);
    CHECK(output.empty());
}

TEST_CASE("CTM18 列表归一：本身不限制条数", "[contacttag]")
{
    // 条数上限是调用方的判定，不该藏在归一函数里 —— 归一函数要能原样处理
    // 服务端下发的任意长列表，否则界面上会莫名少几条。
    std::vector<CString> input;
    const int kOverCount = kContactTagMaxCount + 5;
    for (int i = 0; i < kOverCount; ++i)
    {
        CString one;
        one.Format(_T("标签%d"), i);
        input.push_back(one);
    }
    std::vector<CString> output;
    NormalizeTagList(input, output);
    CHECK((int)output.size() == kOverCount);
}

// =======================================================================
// DecideTagAdd —— 新增标签的判定
// =======================================================================

TEST_CASE("CTM19 新增：普通输入直接追加", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    const TagAddDecision d = DecideTagAdd(tags, _T("同事"), true,
                                          kContactTagMaxLen, kContactTagMaxCount);
    CHECK(d.m_action == kTagAddAppend);
    CHECK(d.m_normalized == _T("同事"));
    CHECK(d.m_dupIndex == -1);
}

TEST_CASE("CTM20 新增：追加的是去掉首尾空白之后的文本", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    const TagAddDecision d = DecideTagAdd(tags, _T("  同事 \t"), true,
                                          kContactTagMaxLen, kContactTagMaxCount);
    CHECK(d.m_action == kTagAddAppend);
    CHECK(d.m_normalized == _T("同事"));
}

TEST_CASE("CTM21 新增：空输入与全空白输入都是取消", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    CHECK(DecideTagAdd(tags, _T(""), true,
                       kContactTagMaxLen, kContactTagMaxCount).m_action == kTagAddCancel);
    CHECK(DecideTagAdd(tags, _T("    "), true,
                       kContactTagMaxLen, kContactTagMaxCount).m_action == kTagAddCancel);
    CHECK(DecideTagAdd(tags, _T("\t\r\n"), true,
                       kContactTagMaxLen, kContactTagMaxCount).m_action == kTagAddCancel);
}

TEST_CASE("CTM22 新增：与已有标签重复则不追加", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    const TagAddDecision d = DecideTagAdd(tags, _T(" 设计 "), true,
                                          kContactTagMaxLen, kContactTagMaxCount);
    CHECK(d.m_action == kTagAddDuplicate);
    CHECK(d.m_dupIndex == 1);
}

TEST_CASE("CTM23 新增：大小写不同也算重复", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    const TagAddDecision d = DecideTagAdd(tags, _T("WORK"), true,
                                          kContactTagMaxLen, kContactTagMaxCount);
    CHECK(d.m_action == kTagAddDuplicate);
    CHECK(d.m_dupIndex == 2);
}

TEST_CASE("CTM24 新增：中间带空格的算另一个标签", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    const TagAddDecision d = DecideTagAdd(tags, _T("Wo rk"), true,
                                          kContactTagMaxLen, kContactTagMaxCount);
    CHECK(d.m_action == kTagAddAppend);
    CHECK(d.m_normalized == _T("Wo rk"));
}

TEST_CASE("CTM25 新增：超过字符上限被拦截", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    const TagAddDecision d = DecideTagAdd(tags, RepeatChar(kContactTagMaxLen + 1), true,
                                          kContactTagMaxLen, kContactTagMaxCount);
    CHECK(d.m_action == kTagAddTooLong);
}

TEST_CASE("CTM26 新增：正好到上限的长度放行", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    const CString justFit = RepeatChar(kContactTagMaxLen);
    const TagAddDecision d = DecideTagAdd(tags, justFit, true,
                                          kContactTagMaxLen, kContactTagMaxCount);
    CHECK(d.m_action == kTagAddAppend);
    CHECK(d.m_normalized == justFit);
}

TEST_CASE("CTM27 新增：条数已满时拦截", "[contacttag]")
{
    const std::vector<CString> tags = FullTags();
    REQUIRE((int)tags.size() == kContactTagMaxCount);
    const TagAddDecision d = DecideTagAdd(tags, _T("再来一个"), true,
                                          kContactTagMaxLen, kContactTagMaxCount);
    CHECK(d.m_action == kTagAddFull);
}

TEST_CASE("CTM28 新增：离线时拦截", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    const TagAddDecision d = DecideTagAdd(tags, _T("同事"), false,
                                          kContactTagMaxLen, kContactTagMaxCount);
    CHECK(d.m_action == kTagAddOffline);
    // 拦截时也要把归一后的文本带出来，调用方好把它回填进输入框让用户重试。
    CHECK(d.m_normalized == _T("同事"));
}

TEST_CASE("CTM29 新增：离线且输入为空时走取消而不是离线拦截", "[contacttag]")
{
    // 用户什么都没输入就关掉输入框，不该弹"网络不可用"来烦他。判定顺序与
    // DecideRemarkCommit 把"无变化"排在离线之前是同一个道理。
    const std::vector<CString> tags = SampleTags();
    CHECK(DecideTagAdd(tags, _T(""), false,
                       kContactTagMaxLen, kContactTagMaxCount).m_action == kTagAddCancel);
    CHECK(DecideTagAdd(tags, _T("   "), false,
                       kContactTagMaxLen, kContactTagMaxCount).m_action == kTagAddCancel);
}

// =======================================================================
// DecideTagEdit —— 就地修改标签的判定
// =======================================================================

TEST_CASE("CTM30 修改：改成一个新值", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    const TagEditDecision d = DecideTagEdit(tags, 0, _T("老同学"), true, kContactTagMaxLen);
    CHECK(d.m_action == kTagEditUpdate);
    CHECK(d.m_normalized == _T("老同学"));
    CHECK(d.m_mergeIndex == -1);
}

TEST_CASE("CTM31 修改：一个字都没动视为无变化", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    const TagEditDecision d = DecideTagEdit(tags, 0, _T("朋友"), true, kContactTagMaxLen);
    CHECK(d.m_action == kTagEditNoChange);
}

TEST_CASE("CTM32 修改：只差首尾空白也算无变化", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    CHECK(DecideTagEdit(tags, 0, _T("  朋友  "), true, kContactTagMaxLen).m_action
          == kTagEditNoChange);
    CHECK(DecideTagEdit(tags, 0, _T("\t朋友\r\n"), true, kContactTagMaxLen).m_action
          == kTagEditNoChange);
}

TEST_CASE("CTM33 修改：大小写等价同样视为无变化", "[contacttag]")
{
    // 大小写只影响显示、不影响标签身份；判成"改了"会白发一次包。
    const std::vector<CString> tags = SampleTags();
    CHECK(DecideTagEdit(tags, 2, _T("WORK"), true, kContactTagMaxLen).m_action
          == kTagEditNoChange);
    CHECK(DecideTagEdit(tags, 2, _T("work"), true, kContactTagMaxLen).m_action
          == kTagEditNoChange);
}

TEST_CASE("CTM34 修改：内容全删光等于删除这枚标签", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    CHECK(DecideTagEdit(tags, 1, _T(""), true, kContactTagMaxLen).m_action
          == kTagEditRemove);
    CHECK(DecideTagEdit(tags, 1, _T("    "), true, kContactTagMaxLen).m_action
          == kTagEditRemove);
}

TEST_CASE("CTM35 修改：改成与另一枚重复则合并", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    // 把第 0 枚「朋友」改成「设计」，与第 1 枚重复 —— 留下来的是原来就在的第 1 枚。
    const TagEditDecision d = DecideTagEdit(tags, 0, _T("设计"), true, kContactTagMaxLen);
    CHECK(d.m_action == kTagEditMerge);
    CHECK(d.m_mergeIndex == 1);
    CHECK(d.m_normalized == _T("设计"));
}

TEST_CASE("CTM36 修改：大小写不同的重复同样触发合并", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    const TagEditDecision d = DecideTagEdit(tags, 0, _T("WORK"), true, kContactTagMaxLen);
    CHECK(d.m_action == kTagEditMerge);
    CHECK(d.m_mergeIndex == 2);
    // 合并后留下来的那一枚要采用用户这次输入的写法。
    CHECK(d.m_normalized == _T("WORK"));
}

TEST_CASE("CTM37 修改：超过字符上限被拦截", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    const TagEditDecision d = DecideTagEdit(tags, 0, RepeatChar(kContactTagMaxLen + 1),
                                            true, kContactTagMaxLen);
    CHECK(d.m_action == kTagEditTooLong);
}

TEST_CASE("CTM38 修改：正好到上限的长度放行", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    const CString justFit = RepeatChar(kContactTagMaxLen);
    const TagEditDecision d = DecideTagEdit(tags, 0, justFit, true, kContactTagMaxLen);
    CHECK(d.m_action == kTagEditUpdate);
    CHECK(d.m_normalized == justFit);
}

TEST_CASE("CTM39 修改：下标越界不崩溃且什么都不做", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    CHECK(DecideTagEdit(tags, -1, _T("同事"), true, kContactTagMaxLen).m_action
          == kTagEditBadIndex);
    CHECK(DecideTagEdit(tags, 3, _T("同事"), true, kContactTagMaxLen).m_action
          == kTagEditBadIndex);
    const std::vector<CString> empty;
    CHECK(DecideTagEdit(empty, 0, _T("同事"), true, kContactTagMaxLen).m_action
          == kTagEditBadIndex);
}

TEST_CASE("CTM40 修改：离线时拦截", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    CHECK(DecideTagEdit(tags, 0, _T("同事"), false, kContactTagMaxLen).m_action
          == kTagEditOffline);
    // 删除同样要发一份完整列表上去，离线时一并拦住。
    CHECK(DecideTagEdit(tags, 0, _T(""), false, kContactTagMaxLen).m_action
          == kTagEditOffline);
}

TEST_CASE("CTM41 修改：离线但一个字没动时不弹网络提示", "[contacttag]")
{
    const std::vector<CString> tags = SampleTags();
    CHECK(DecideTagEdit(tags, 0, _T("  朋友 "), false, kContactTagMaxLen).m_action
          == kTagEditNoChange);
}
