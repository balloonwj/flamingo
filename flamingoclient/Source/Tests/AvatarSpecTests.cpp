/**
 *  AvatarSpecTests.cpp
 *
 *  头像规格编解码（BalloonMain/AvatarSpec.h）的测试：
 *    · EncodeGlyphAvatar 产出 facetype=1 与 "glyph|idx" 形式的 customface（含 emoji / CJK）。
 *    · Encode → Decode 往返字形与色槽一致。
 *    · facetype=0 / 未知 / 第一期 image / 各类脏 customface 一律安全回退到自动渲染。
 *    · 越界 colorIndex 经调色板归一、不越界不崩。
 *
 *  用 Catch2 v3（amalgamated）作框架，纯内存断言，不创建窗口 / 不依赖网络。
 *  全局 _Module 由 BalloonChatLayoutTests.cpp 统一定义，本文件只引用。
 *
 *  balloonwj@qq.com   2026-06-05
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include "../BalloonMain/AvatarSpec.h"

using namespace balloonmain;

// =====================================================================
// AS. 编码（EncodeGlyphAvatar）
// =====================================================================

TEST_CASE("AS1 Encode 产出 facetype=glyph 与 \"L|0\"", "[avatarspec]")
{
    int facetype = -1;
    std::string customface;
    EncodeGlyphAvatar(_T("L"), 0, facetype, customface);

    CHECK(facetype == avatar_kind_glyph);
    CHECK(customface == "L|0");
}

TEST_CASE("AS2 Encode 限 1~2 字符、剔除分隔符", "[avatarspec]")
{
    int facetype = -1;
    std::string customface;

    // 超过 2 字符截断到 2。
    EncodeGlyphAvatar(_T("ABC"), 1, facetype, customface);
    CHECK(customface == "AB|1");

    // glyph 内若含分隔符 '|' 被剔除，避免破坏结构。
    EncodeGlyphAvatar(_T("a|b"), 2, facetype, customface);
    CHECK(customface == "ab|2");
}

TEST_CASE("AS3 Encode 越界 colorIndex 归一到 [0,12)", "[avatarspec]")
{
    int facetype = -1;
    std::string customface;
    EncodeGlyphAvatar(_T("L"), 99, facetype, customface);   // 99 % 12 == 3
    CHECK(customface == "L|3");

    EncodeGlyphAvatar(_T("L"), -1, facetype, customface);    // (-1 % 12 + 12) % 12 == 11
    CHECK(customface == "L|11");
}

// =====================================================================
// AS. 解码（DecodeAvatar）—— 往返与回退
// =====================================================================

TEST_CASE("AS4 Encode→Decode 往返：字母字形", "[avatarspec]")
{
    int facetype = -1;
    std::string customface;
    EncodeGlyphAvatar(_T("L"), 5, facetype, customface);

    AvatarSpec spec = DecodeAvatar(facetype, customface, _T("Lily"), 123);
    CHECK(spec.m_kind == avatar_kind_glyph);
    CHECK(spec.m_glyph == _T("L"));
    CHECK(spec.m_colorIndex == 5);
    CHECK(spec.m_color == AvatarColorByIndex(5));
}

TEST_CASE("AS5 Encode→Decode 往返：emoji 与 CJK 字形", "[avatarspec]")
{
    int facetype = -1;
    std::string customface;

    EncodeGlyphAvatar(_T("🦊"), 6, facetype, customface);
    AvatarSpec s1 = DecodeAvatar(facetype, customface, _T("Fox"), 1);
    CHECK(s1.m_kind == avatar_kind_glyph);
    CHECK(s1.m_glyph == _T("🦊"));
    CHECK(s1.m_colorIndex == 6);

    EncodeGlyphAvatar(_T("张"), 2, facetype, customface);
    AvatarSpec s2 = DecodeAvatar(facetype, customface, _T("张三"), 2);
    CHECK(s2.m_kind == avatar_kind_glyph);
    CHECK(s2.m_glyph == _T("张"));
    CHECK(s2.m_colorIndex == 2);
}

TEST_CASE("AS6 facetype=0 自动：取昵称首字 + userid 哈希底色", "[avatarspec]")
{
    AvatarSpec spec = DecodeAvatar(avatar_kind_auto, "", _T("张三"), 5);
    CHECK(spec.m_kind == avatar_kind_auto);
    CHECK(spec.m_glyph == _T("张"));
    CHECK(spec.m_color == AvatarColorForUser(5));
    CHECK(spec.m_colorIndex == -1);
}

TEST_CASE("AS7 空昵称回退字形为 \"?\"", "[avatarspec]")
{
    AvatarSpec spec = DecodeAvatar(avatar_kind_auto, "", _T(""), 7);
    CHECK(spec.m_glyph == _T("?"));

    AvatarSpec spec2 = DecodeAvatar(avatar_kind_auto, "", NULL, 7);
    CHECK(spec2.m_glyph == _T("?"));
}

TEST_CASE("AS8 越界存储 colorIndex 经调色板归一、不崩", "[avatarspec]")
{
    AvatarSpec spec = DecodeAvatar(avatar_kind_glyph, "L|99", _T("Lily"), 1);
    CHECK(spec.m_kind == avatar_kind_glyph);
    CHECK(spec.m_glyph == _T("L"));
    CHECK(spec.m_colorIndex == 99 % kAvatarPickerSize);   // 3
    CHECK(spec.m_color == AvatarColorByIndex(99));
}

TEST_CASE("AS9 字形头像脏 customface 一律回退自动", "[avatarspec]")
{
    // 无分隔符 / 空 idx / 空 glyph / idx 非数字 / 空串。
    const char* dirty[] = { "Labc", "L|", "|3", "L|3|x", "", "L|abc" };
    for (size_t i = 0; i < sizeof(dirty) / sizeof(dirty[0]); ++i)
    {
        AvatarSpec spec = DecodeAvatar(avatar_kind_glyph, dirty[i], _T("Kate"), 9);
        INFO("dirty customface = " << dirty[i]);
        CHECK(spec.m_kind == avatar_kind_auto);
        CHECK(spec.m_glyph == _T("K"));
        CHECK(spec.m_color == AvatarColorForUser(9));
    }
}

TEST_CASE("AS10 第一期 image：kind=image、fileId 解出、字形/底色回退", "[avatarspec]")
{
    AvatarSpec spec = DecodeAvatar(avatar_kind_image, "abc123fileid", _T("Mona"), 11);
    CHECK(spec.m_kind == avatar_kind_image);
    CHECK(spec.m_fileId == _T("abc123fileid"));
    // 第一期真图未就绪：glyph/color 回退到自动，未改造的渲染点照画 monogram。
    CHECK(spec.m_glyph == _T("M"));
    CHECK(spec.m_color == AvatarColorForUser(11));
}

TEST_CASE("AS11 未知 facetype 当作自动处理", "[avatarspec]")
{
    AvatarSpec spec = DecodeAvatar(999, "whatever", _T("Zoe"), 3);
    CHECK(spec.m_kind == avatar_kind_auto);
    CHECK(spec.m_glyph == _T("Z"));
    CHECK(spec.m_color == AvatarColorForUser(3));
}
