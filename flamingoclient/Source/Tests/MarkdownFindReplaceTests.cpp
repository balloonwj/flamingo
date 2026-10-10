/**
 *  Markdown 编辑器查找与替换（UI/BalloonMarkdown/MarkdownFindReplace）的单元测试，用例 FR1 ~ FR3
 *  （docs/Markdown消息方案.md 第 13 节）：
 *    - FR1 查找下一个 / 上一个：从光标处开始，到末尾（开头）后回绕；区分与不区分大小写；找不到、查找词为空时返回失败
 *    - FR2 全部替换：替换次数与结果正确，是一个替换区间（一步撤销）；替换词里含查找词时不重复替换
 *    - FR3 替换当前：选区正好是匹配时替换并找下一个，否则只找下一个
 *  纯函数，不需要窗口。框架 Catch2 v3，标签 [mdedit]。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#include "stdafx.h"
#include "catch_amalgamated.hpp"

#include <string>

#include "../UI/BalloonMarkdown/MarkdownFindReplace.h"

using namespace balloonmd;

namespace {

//查找结果：是否找到、起止、是否回绕
struct FindHit
{
    bool m_found;     //是否找到
    int  m_start;     //起点
    int  m_end;       //终点（不含）
    bool m_wrapped;   //是否回绕后才找到
};

//查找一次
FindHit Find(const std::wstring& text, const std::wstring& query, int from, bool forward, bool matchCase)
{
    FindHit hit;
    hit.m_start = -1;
    hit.m_end = -1;
    hit.m_wrapped = false;
    hit.m_found = FindMarkdownText(text, query, from, forward, matchCase, hit.m_start, hit.m_end, &hit.m_wrapped);
    return hit;
}

//替换区间与替换文字能还原出新全文
void CheckRebuild(const std::wstring& oldText, const balloonchatwnd::MarkdownEditResult& r)
{
    REQUIRE(r.m_replaceStart >= 0);
    REQUIRE(r.m_replaceEnd >= r.m_replaceStart);
    REQUIRE(r.m_replaceEnd <= static_cast<int>(oldText.size()));
    CHECK(oldText.substr(0, r.m_replaceStart) + r.m_replacement + oldText.substr(r.m_replaceEnd) == r.m_newText);
}

} // namespace

TEST_CASE("FR1 查找下一个 / 上一个：从光标处开始，到末尾或开头后回绕；区分与不区分大小写；找不到或查找词为空时失败",
          "[mdedit]")
{
    //下标：「ab Ab ab」里三处 ab 分别在 0、3、6
    const std::wstring text = L"ab Ab ab";

    //向后找：从光标处开始（含光标处），不区分大小写时 Ab 也算
    FindHit h = Find(text, L"ab", 1, true, false);
    CHECK(h.m_found);
    CHECK(h.m_start == 3);
    CHECK(h.m_end == 5);
    CHECK_FALSE(h.m_wrapped);
    CHECK(Find(text, L"ab", 3, true, false).m_start == 3);

    //区分大小写时跳过 Ab
    CHECK(Find(text, L"ab", 1, true, true).m_start == 6);

    //到末尾后回到开头
    h = Find(text, L"ab", 7, true, false);
    CHECK(h.m_found);
    CHECK(h.m_start == 0);
    CHECK(h.m_wrapped);

    //向前找：终点不超过光标的最后一个匹配；到开头后回到末尾
    CHECK(Find(text, L"ab", 6, false, false).m_start == 3);
    CHECK(Find(text, L"ab", 5, false, false).m_start == 3);
    CHECK(Find(text, L"ab", 6, false, true).m_start == 0);
    h = Find(text, L"ab", 1, false, false);
    CHECK(h.m_found);
    CHECK(h.m_start == 6);
    CHECK(h.m_wrapped);

    //中文与 \r 分段的文字
    const std::wstring zh = L"第一段\r第二段";
    h = Find(zh, L"二段", 0, true, true);
    CHECK(h.m_start == 5);
    CHECK(h.m_end == 7);

    //只有一处匹配、光标就在它之后：回绕找到它本身
    h = Find(L"xyz", L"y", 2, true, false);
    CHECK(h.m_start == 1);
    CHECK(h.m_wrapped);

    //找不到、查找词为空、查找词比全文长、光标越界
    CHECK_FALSE(Find(text, L"zz", 0, true, false).m_found);
    CHECK_FALSE(Find(text, L"", 0, true, false).m_found);
    CHECK_FALSE(Find(L"a", L"ab", 0, true, false).m_found);
    CHECK(Find(text, L"ab", 99, true, false).m_start == 0);
    CHECK(Find(text, L"ab", -5, true, false).m_start == 0);

    //匹配个数（不重叠）
    CHECK(CountMarkdownMatches(text, L"ab", false) == 3);
    CHECK(CountMarkdownMatches(text, L"ab", true) == 2);
    CHECK(CountMarkdownMatches(L"aaaa", L"aa", true) == 2);
    CHECK(CountMarkdownMatches(text, L"", false) == 0);
}

TEST_CASE("FR2 全部替换：次数与结果正确，是一个替换区间；替换词里含查找词时不重复替换；没有匹配时不改动", "[mdedit]")
{
    balloonchatwnd::MarkdownEditResult r;
    const std::wstring text = L"cat Cat dog cat";
    CHECK(ReplaceAllMarkdownText(text, L"cat", L"fox", false, r) == 3);
    CHECK(r.m_newText == L"fox fox dog fox");
    CheckRebuild(text, r);
    //一个替换区间：从第一处匹配到最后一处匹配
    CHECK(r.m_replaceStart == 0);
    CHECK(r.m_replaceEnd == static_cast<int>(text.size()));
    //光标停在最后一处替换之后
    CHECK(r.m_selStart == static_cast<int>(r.m_newText.size()));
    CHECK(r.m_selEnd == r.m_selStart);

    //区分大小写
    CHECK(ReplaceAllMarkdownText(text, L"cat", L"fox", true, r) == 2);
    CHECK(r.m_newText == L"fox Cat dog fox");
    CheckRebuild(text, r);

    //替换词里含查找词：只替换原有的两处
    CHECK(ReplaceAllMarkdownText(L"a-a", L"a", L"aa", true, r) == 2);
    CHECK(r.m_newText == L"aa-aa");

    //替换为空（删除）、中文
    CHECK(ReplaceAllMarkdownText(L"第一段\r第二段", L"段", L"", true, r) == 2);
    CHECK(r.m_newText == L"第一\r第二");

    //没有匹配、查找词为空：不改动
    CHECK(ReplaceAllMarkdownText(text, L"bird", L"x", false, r) == 0);
    CHECK(r.m_newText == text);
    CHECK(r.m_replacement.empty());
    CHECK(r.m_replaceStart == r.m_replaceEnd);
    CHECK(ReplaceAllMarkdownText(text, L"", L"x", false, r) == 0);
    CHECK(r.m_newText == text);
}

TEST_CASE("FR3 替换当前：选区正好是匹配时替换它并选中下一个，否则只找下一个；全部替换完后光标停在替换词之后", "[mdedit]")
{
    const std::wstring text = L"ab ab ab";
    balloonchatwnd::MarkdownEditResult r;
    bool next = false;

    //选区不是匹配（只有光标）：不替换，选中下一个
    CHECK_FALSE(ReplaceCurrentMarkdownText(text, 1, 1, L"ab", L"X", false, r, &next));
    CHECK(next);
    CHECK(r.m_newText == text);
    CHECK(r.m_replacement.empty());
    CHECK(r.m_selStart == 3);
    CHECK(r.m_selEnd == 5);

    //选区正好是匹配（大小写不同也算）：替换它，选中下一个（下标按替换后的全文计）
    CHECK(ReplaceCurrentMarkdownText(L"AB ab ab", 0, 2, L"ab", L"X", false, r, &next));
    CHECK(r.m_newText == L"X ab ab");
    CheckRebuild(L"AB ab ab", r);
    CHECK(next);
    CHECK(r.m_selStart == 2);
    CHECK(r.m_selEnd == 4);

    //区分大小写时 AB 不是匹配：不替换，只找下一个
    CHECK_FALSE(ReplaceCurrentMarkdownText(L"AB ab", 0, 2, L"ab", L"X", true, r, &next));
    CHECK(r.m_selStart == 3);

    //替换最后一处：之后回绕找到前面还没替换的一处
    CHECK(ReplaceCurrentMarkdownText(text, 6, 8, L"ab", L"X", false, r, &next));
    CHECK(r.m_newText == L"ab ab X");
    CHECK(next);
    CHECK(r.m_selStart == 0);

    //替换掉唯一的一处：找不到下一个，光标停在替换词之后
    CHECK(ReplaceCurrentMarkdownText(L"x ab y", 2, 4, L"ab", L"XYZ", false, r, &next));
    CHECK(r.m_newText == L"x XYZ y");
    CHECK_FALSE(next);
    CHECK(r.m_selStart == 5);
    CHECK(r.m_selEnd == 5);

    //查找词为空：不做任何事
    CHECK_FALSE(ReplaceCurrentMarkdownText(text, 0, 2, L"", L"X", false, r, &next));
    CHECK_FALSE(next);
    CHECK(r.m_newText == text);
}
