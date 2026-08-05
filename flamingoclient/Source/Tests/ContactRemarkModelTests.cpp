/**
 *  ContactRemarkModelTests.cpp
 *
 *  联系人备注名两段纯逻辑的测试（Suite CRM）：
 *    · ResolveContactDisplayName —— 备注优先的取名顺序与各级兜底。
 *    · DecideRemarkCommit —— 无变化不发、离线拦截、超长拦截、清空也要发。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不碰网络。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-08-05
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonMain/ContactRemarkModel.h"

using namespace balloonmain;

// =======================================================================
// ResolveContactDisplayName —— 取名顺序
// =======================================================================

TEST_CASE("CRM1 有备注时备注盖过昵称", "[contactremark]")
{
    // 这正是"改备注"这个功能的意义所在：本人给对方起的名字要压过对方自己的昵称。
    CHECK(ResolveContactDisplayName(_T("产品组同事"), _T("谢知行"), _T("10010001"), 100007)
          == _T("产品组同事"));
}

TEST_CASE("CRM2 没有备注时退回昵称", "[contactremark]")
{
    CHECK(ResolveContactDisplayName(_T(""), _T("谢知行"), _T("10010001"), 100007)
          == _T("谢知行"));
}

TEST_CASE("CRM3 备注与昵称都没有时退回账号", "[contactremark]")
{
    CHECK(ResolveContactDisplayName(_T(""), _T(""), _T("10010001"), 100007)
          == _T("10010001"));
}

TEST_CASE("CRM4 三者皆空时用 userid 拼兜底名", "[contactremark]")
{
    // 显示空白会让用户完全对不上是谁，兜底名至少还能报出账号ID。
    CHECK(ResolveContactDisplayName(_T(""), _T(""), _T(""), 100007) == _T("用户100007"));
}

TEST_CASE("CRM5 只有空白字符的值不算数，继续往下退", "[contactremark]")
{
    // 服务端下发的昵称有可能是几个空格。直接判 IsEmpty 会让它蒙混过关，界面上
    // 就显示成一片空白 —— 必须按 trim 后的结果判定。
    CHECK(ResolveContactDisplayName(_T("   "), _T("谢知行"), _T("10010001"), 100007)
          == _T("谢知行"));
    CHECK(ResolveContactDisplayName(_T("  "), _T("\t "), _T("10010001"), 100007)
          == _T("10010001"));
    CHECK(ResolveContactDisplayName(_T(" "), _T(" "), _T("  "), 100007) == _T("用户100007"));
}

TEST_CASE("CRM6 取到的名字会去掉首尾空白", "[contactremark]")
{
    CHECK(ResolveContactDisplayName(_T("  产品组同事 "), _T("谢知行"), _T(""), 100007)
          == _T("产品组同事"));
}

// =======================================================================
// DecideRemarkCommit —— 提交判定
// =======================================================================

TEST_CASE("CRM7 备注没变化：不发包", "[contactremark]")
{
    RemarkCommitDecision d =
        DecideRemarkCommit(_T("产品组同事"), _T("产品组同事"), true, kContactRemarkMaxLen);
    CHECK(d.m_action == kRemarkCommitNoChange);
}

TEST_CASE("CRM8 只差首尾空白：仍算没变化", "[contactremark]")
{
    // 用户点进编辑框、光标动了动又点出来，不该因为多打了个空格就发一次协议。
    RemarkCommitDecision d =
        DecideRemarkCommit(_T("  产品组同事  "), _T("产品组同事"), true, kContactRemarkMaxLen);
    CHECK(d.m_action == kRemarkCommitNoChange);

    // 原值自己带首尾空白（历史脏数据）时同样不该被判成"改了"。
    RemarkCommitDecision d2 =
        DecideRemarkCommit(_T("产品组同事"), _T(" 产品组同事 "), true, kContactRemarkMaxLen);
    CHECK(d2.m_action == kRemarkCommitNoChange);
}

TEST_CASE("CRM9 有变化且在线：发送，并给出 trim 后的文本", "[contactremark]")
{
    RemarkCommitDecision d =
        DecideRemarkCommit(_T("  新备注 "), _T("旧备注"), true, kContactRemarkMaxLen);
    CHECK(d.m_action == kRemarkCommitSend);
    CHECK(d.m_trimmed == _T("新备注"));
}

TEST_CASE("CRM10 把备注清空也要发", "[contactremark]")
{
    // "清除备注"是一次正常提交（协议上 remark 传空串即表示清除），不是"没改动"。
    RemarkCommitDecision d = DecideRemarkCommit(_T(""), _T("旧备注"), true, kContactRemarkMaxLen);
    CHECK(d.m_action == kRemarkCommitSend);
    CHECK(d.m_trimmed.IsEmpty());

    // 全是空白也等价于清空。
    RemarkCommitDecision d2 =
        DecideRemarkCommit(_T("   "), _T("旧备注"), true, kContactRemarkMaxLen);
    CHECK(d2.m_action == kRemarkCommitSend);
    CHECK(d2.m_trimmed.IsEmpty());
}

TEST_CASE("CRM11 本来就没备注、提交空文本：不发包", "[contactremark]")
{
    // 两边都为空 → 没有变化，不该白发一次协议。
    RemarkCommitDecision d = DecideRemarkCommit(_T(""), _T(""), true, kContactRemarkMaxLen);
    CHECK(d.m_action == kRemarkCommitNoChange);
}

TEST_CASE("CRM12 离线时有变化：拦截", "[contactremark]")
{
    RemarkCommitDecision d =
        DecideRemarkCommit(_T("新备注"), _T("旧备注"), false, kContactRemarkMaxLen);
    CHECK(d.m_action == kRemarkCommitOffline);
}

TEST_CASE("CRM13 离线但没变化：不提示、按无变化处理", "[contactremark]")
{
    // 判定顺序刻意是"先看有没有变化，再看能不能发"：用户一个字没改时，即便当前
    // 离线也不该弹"网络不可用"来烦他。
    RemarkCommitDecision d =
        DecideRemarkCommit(_T("产品组同事"), _T("产品组同事"), false, kContactRemarkMaxLen);
    CHECK(d.m_action == kRemarkCommitNoChange);
}

TEST_CASE("CRM14 超长：拦截", "[contactremark]")
{
    // 库里 f_userX_markname 是 VARCHAR(32)，放过去会被悄悄截断，界面上就会出现
    // "改完显示的是全的、重登之后少一截"的怪事。
    CString tooLong(_T('a'), kContactRemarkMaxLen + 1);
    RemarkCommitDecision d = DecideRemarkCommit(tooLong, _T(""), true, kContactRemarkMaxLen);
    CHECK(d.m_action == kRemarkCommitTooLong);

    // 正好等于上限则放行。
    CString justFit(_T('a'), kContactRemarkMaxLen);
    RemarkCommitDecision d2 = DecideRemarkCommit(justFit, _T(""), true, kContactRemarkMaxLen);
    CHECK(d2.m_action == kRemarkCommitSend);
}
