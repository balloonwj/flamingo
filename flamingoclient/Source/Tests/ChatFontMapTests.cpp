/**
 *  ChatFontMapTests.cpp
 *
 *  第零期（聊天输入框迁移到 DuiRichEditHost）测试：覆盖字体翻译纯逻辑
 *  ChatFontToLogFont —— 把 net::TextFont 翻成 LOGFONT + 文字色。该逻辑是
 *  ApplyChatFontToEdit 各重载共用的核心，单独测它即可验证"字号 / 粗体 /
 *  斜体 / 下划线 / 删除线 / 颜色"映射正确，且不必把 ini / 会话全局拖进测试。
 *
 *  对应方案 A 组用例 A7。用 Catch2 v3（amalgamated）。全局 _Module 由
 *  BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-04
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonChatWnd/ChatFontUtil.h"
#include "../net/MessageProtocolClient.h"   // net::TextFont

using namespace balloonchatwnd;

// =====================================================================
// A7. 字体翻译纯逻辑 ChatFontToLogFont
// =====================================================================

TEST_CASE("F1 字体未设（m_valid=false）→ 返回 false、不应用", "[chatfont][map]")
{
    net::TextFont f;            // 默认 m_valid==false
    LOGFONT  lf;
    COLORREF c = 0;
    CHECK_FALSE(ChatFontToLogFont(f, lf, c));
}

TEST_CASE("F2 粗+斜体、字号、颜色映射正确", "[chatfont][map]")
{
    net::TextFont f;
    f.m_valid     = true;
    f.m_size      = 20;
    f.m_bold      = true;
    f.m_italic    = true;
    f.m_underline = false;
    f.m_strike    = false;
    f.m_colorRgb  = 0xFF8800;   // 0xRRGGBB

    LOGFONT  lf;
    COLORREF c = 0;
    REQUIRE(ChatFontToLogFont(f, lf, c));

    CHECK(lf.lfHeight == -20);              // 负字高 = 按像素，对齐历史 EDIT
    CHECK(lf.lfWeight == FW_BOLD);
    CHECK(lf.lfItalic != 0);
    CHECK(lf.lfUnderline == 0);
    CHECK(lf.lfStrikeOut == 0);
    CHECK(c == RGB(0xFF, 0x88, 0x00));      // 0xRRGGBB → COLORREF
    CHECK(CString(lf.lfFaceName) == CString(_T("Microsoft YaHei")));
}

TEST_CASE("F3 非粗非斜 → FW_NORMAL/italic=0；下划线删除线透传", "[chatfont][map]")
{
    net::TextFont f;
    f.m_valid     = true;
    f.m_size      = 14;
    f.m_bold      = false;
    f.m_italic    = false;
    f.m_underline = true;
    f.m_strike    = true;
    f.m_colorRgb  = 0x1A1A1A;

    LOGFONT  lf;
    COLORREF c = 0;
    REQUIRE(ChatFontToLogFont(f, lf, c));

    CHECK(lf.lfWeight == FW_NORMAL);
    CHECK(lf.lfItalic == 0);
    CHECK(lf.lfUnderline != 0);
    CHECK(lf.lfStrikeOut != 0);
    CHECK(c == RGB(0x1A, 0x1A, 0x1A));
}

TEST_CASE("F4 字号<=0 兜底为 14（对齐 SetCtlFont）", "[chatfont][map]")
{
    net::TextFont f;
    f.m_valid = true;
    f.m_size  = 0;

    LOGFONT  lf;
    COLORREF c = 0;
    REQUIRE(ChatFontToLogFont(f, lf, c));
    CHECK(lf.lfHeight == -14);
}
