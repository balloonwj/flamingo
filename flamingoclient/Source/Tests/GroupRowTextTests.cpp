/**
 *  GroupRowTextTests.cpp
 *
 *  群列表行文案拼装（GroupRowText）的纯函数测试。覆盖与用户对齐过的四类情形：
 *    · 正常群 —— 第一行给出群名 + "M 人"，第二行给出 "群号 N"；
 *    · 成员数为 0 / 负数 —— 人数未知，成员数文字为空串（不显示骗人的 "0 人"）；
 *    · 群名超长 —— 原样透传、不在这一层截断（截断是绘制层 DT_END_ELLIPSIS 的事）；
 *    · 群号为 0 / 负数 —— 群号非法，群号文字为空串。
 *  另补两条边界：群名为空时的占位名回落，以及 64 位大群号的格式化。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不绘制。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-03
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonMain/GroupRowText.h"

using namespace balloonmain;

// 测试里反复用到的一个"正常群"：群号 700001、28 人。
static const __int64 kNormalGroupId    = 700001;
static const int     kNormalMemberCnt  = 28;

TEST_CASE("GROW1 正常群：第一行群名 + 人数，第二行群号", "[grouprow]")
{
    GroupRowTexts t = BuildGroupRowTexts(_T("研发一组"), kNormalMemberCnt, kNormalGroupId);

    CHECK(t.m_name        == CString(_T("研发一组")));
    CHECK(t.m_memberText  == CString(_T("28 人")));
    CHECK(t.m_groupNoText == CString(_T("群号 700001")));
}

TEST_CASE("GROW2 成员数为 0 或负数：不显示人数", "[grouprow]")
{
    GroupRowTexts zero = BuildGroupRowTexts(_T("研发一组"), 0, kNormalGroupId);
    CHECK(zero.m_memberText.IsEmpty());
    // 人数不显示不影响另外两段。
    CHECK(zero.m_name        == CString(_T("研发一组")));
    CHECK(zero.m_groupNoText == CString(_T("群号 700001")));

    GroupRowTexts negative = BuildGroupRowTexts(_T("研发一组"), -5, kNormalGroupId);
    CHECK(negative.m_memberText.IsEmpty());

    // 1 人是有效人数，必须显示 —— 与 0 的边界要分清楚。
    GroupRowTexts one = BuildGroupRowTexts(_T("研发一组"), 1, kNormalGroupId);
    CHECK(one.m_memberText == CString(_T("1 人")));
}

TEST_CASE("GROW3 群名超长：原样透传，不在本层截断", "[grouprow]")
{
    // 构造一个远超列表行宽度的群名（100 个汉字）。
    CString longName;
    for (int i = 0; i < 100; ++i)
    {
        longName += _T("群");
    }

    GroupRowTexts t = BuildGroupRowTexts(longName, kNormalMemberCnt, kNormalGroupId);

    CHECK(t.m_name.GetLength() == longName.GetLength());
    CHECK(t.m_name == longName);
    // 超长群名不应该把另外两段挤掉 —— 它们由绘制层各自的矩形负责。
    CHECK(t.m_memberText  == CString(_T("28 人")));
    CHECK(t.m_groupNoText == CString(_T("群号 700001")));
}

TEST_CASE("GROW4 群号为 0 或负数：不显示群号", "[grouprow]")
{
    GroupRowTexts zero = BuildGroupRowTexts(_T("研发一组"), kNormalMemberCnt, 0);
    CHECK(zero.m_groupNoText.IsEmpty());
    // 群号非法不影响另外两段。
    CHECK(zero.m_name       == CString(_T("研发一组")));
    CHECK(zero.m_memberText == CString(_T("28 人")));

    GroupRowTexts negative = BuildGroupRowTexts(_T("研发一组"), kNormalMemberCnt, -1);
    CHECK(negative.m_groupNoText.IsEmpty());
}

TEST_CASE("GROW5 群名缺失：按群号回落为占位名", "[grouprow]")
{
    // 空串群名 + 可用群号 → "群聊(<群号>)"。
    GroupRowTexts empty = BuildGroupRowTexts(_T(""), kNormalMemberCnt, kNormalGroupId);
    CHECK(empty.m_name == CString(_T("群聊(700001)")));

    // NULL 群名与空串同等对待。
    GroupRowTexts null = BuildGroupRowTexts(NULL, kNormalMemberCnt, kNormalGroupId);
    CHECK(null.m_name == CString(_T("群聊(700001)")));

    // 群名与群号都取不到时的兜底名，且此时不显示群号那一行。
    GroupRowTexts nothing = BuildGroupRowTexts(NULL, 0, 0);
    CHECK(nothing.m_name == CString(_T("群聊")));
    CHECK(nothing.m_memberText.IsEmpty());
    CHECK(nothing.m_groupNoText.IsEmpty());
}

TEST_CASE("GROW6 大群号：按 64 位整数格式化，不溢出成负数", "[grouprow]")
{
    // 超出 32 位范围的群号，验证格式串用的是 %I64d 而不是 %d。
    const __int64 kBigGroupId = 4000000000;
    GroupRowTexts big = BuildGroupRowTexts(_T("大群"), kNormalMemberCnt, kBigGroupId);
    CHECK(big.m_groupNoText == CString(_T("群号 4000000000")));

    // int64 上限，格式化不应截断。
    const __int64 kMaxGroupId = 9223372036854775807;
    GroupRowTexts max = BuildGroupRowTexts(_T("大群"), kNormalMemberCnt, kMaxGroupId);
    CHECK(max.m_groupNoText == CString(_T("群号 9223372036854775807")));
}
