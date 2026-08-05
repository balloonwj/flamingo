/**
 *  AvatarSpec.h
 *
 *  头像规格编解码 —— 把「存储侧」的 (facetype, customface) 与「UI / 渲染侧」的
 *  「该画什么」(字形 + 底色 / 真实图片文件) 互相转换，收成单一真值源，供主面板
 *  banner、联系人 / 会话列表、聊天窗标题、托盘等所有头像位共用，避免各处各写一套
 *  「取昵称首字 + userid 哈希底色」的回退逻辑。
 *
 *  存储编码约定（复用 cmd_change_avatar 的 {facetype:int, customface:string(32)}，
 *  零服务端改动）：
 *    facetype=0 自动/默认：customface 空。按昵称首字 + userid 哈希底色自动渲染
 *                          （历史账号行为不变）。
 *    facetype=1 字形头像：customface = "<glyph>|<colorIndex>"，如 "L|0"、"🦊|6"。
 *                          系统头像页与首字母页统一走这一种。
 *    facetype=2 自定义图：customface = 上传得到的 fileid（真图渲染留第二期，第一期
 *                          DecodeAvatar 对它回退到自动渲染，保证不崩）。
 *
 *  全部为 header-only 的 inline 纯函数（只做字符串 / 整型 / 颜色映射，不碰窗口、
 *  网络、GDI 资源），便于 FlamingoUnit(Catch2) 直接断言，见
 *  flamingoclient/Source/Tests/AvatarSpecTests.cpp。
 *
 *  balloonwj@qq.com   2026-06-05
 */
#pragma once

#include <windows.h>      // COLORREF / WideCharToMultiByte 等
#include <atlstr.h>       // CString（与 BalloonChatMsgFormat.h 同处理，header 直接引入）
#include <cstdint>        // int64_t
#include <string>

#include "AvatarPalette.h"

namespace balloonmain {

// 头像种类：决定「该画什么」。与存储侧 facetype 取值对齐。
enum AvatarKind
{
    avatar_kind_auto  = 0,   // 自动：未自定义，按昵称首字 + userid 哈希底色回退渲染
    avatar_kind_glyph = 1,   // 字形：用户选定的字母 / emoji + 调色板底色（系统头像 / 首字母统一走这）
    avatar_kind_image = 2,   // 自定义图：真实图片（第二期渲染，第一期回退到字形/自动）
};

// 「字形头像」customface 里 glyph 与 colorIndex 之间的分隔符："<glyph>|<idx>"。
const wchar_t kAvatarGlyphColorSep = L'|';

// 解码后的「该画什么」规格。第一期渲染只用 m_glyph / m_color；m_fileId 留第二期。
struct AvatarSpec
{
    int      m_kind;        // 头像种类，取值见 AvatarKind
    CString  m_glyph;       // m_kind != image 时要画的字形（1~2 字符，可含 emoji）
    COLORREF m_color;       // m_kind != image 时的底色
    int      m_colorIndex;  // m_kind == glyph 时选定的调色板色槽下标 [0,kAvatarPickerSize)；其它为 -1
    CString  m_fileId;      // m_kind == image 时的图片文件 id（第二期下载渲染用）

    AvatarSpec()
        : m_kind(avatar_kind_auto)
        , m_color(RGB(0, 0, 0))
        , m_colorIndex(-1)
    {
    }
};

// 宽字符串 → UTF-8（窄）。空入参返回空串。
//   w：宽字符串指针（可为 NULL）。
// 返回：UTF-8 编码字节串。
inline std::string AvatarUtf8FromWide(LPCWSTR w)
{
    if (w == NULL || w[0] == L'\0')
    {
        return std::string();
    }

    int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 1)
    {
        return std::string();
    }

    std::string s;
    s.resize((size_t)(n - 1));   // n 含结尾 NUL，std::string 自带终止，故 -1
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, NULL, NULL);
    return s;
}

// UTF-8（窄）→ 宽字符串。空入参返回空 CString。
//   s：UTF-8 编码字节串。
// 返回：对应的宽字符 CString。
inline CString AvatarWideFromUtf8(const std::string& s)
{
    if (s.empty())
    {
        return CString();
    }

    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0)
    {
        return CString();
    }

    CString r;
    LPWSTR buf = r.GetBuffer(n);
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), buf, n);
    r.ReleaseBuffer(n);
    return r;
}

// 取回退字形：昵称首字符（无昵称时取 "?"）。
//   name：显示名（可为 NULL / 空）。
// 返回：1 个字符的 CString（与既有 name.Left(1) 口径一致）。
inline CString AvatarFallbackGlyph(LPCTSTR name)
{
    if (name == NULL || name[0] == _T('\0'))
    {
        return CString(_T("?"));
    }
    return CString(name).Left(1);
}

// 编码：把 UI 选定的「字形 + 底色槽下标」编成 (facetype, customface)，供
// cmd_change_avatar 发送。
//   glyph：选定字形（字母 / emoji，1~2 字符；NULL 视为空）。
//   colorIndex：选定的调色板色槽下标（任意整数，内部按 kAvatarPickerSize 归一）。
//   outFacetype：[出参] 头像类型，恒为 avatar_kind_glyph。
//   outCustomfaceUtf8：[出参] 编码后的 customface（UTF-8，形如 "L|0"）。
inline void EncodeGlyphAvatar(LPCTSTR glyph, int colorIndex,
                              int& outFacetype, std::string& outCustomfaceUtf8)
{
    outFacetype = avatar_kind_glyph;

    int idx = ((colorIndex % kAvatarPickerSize) + kAvatarPickerSize) % kAvatarPickerSize;

    CString g = (glyph == NULL) ? CString() : CString(glyph);
    // 防御：先剔除分隔符（正常 UI 不会产生 '|'），再限 1~2 字符——顺序不能反，
    // 否则 "a|b" 会先被 Left(2) 截成 "a|"、剔分隔符后只剩 "a"。
    g.Remove(kAvatarGlyphColorSep);
    g = g.Left(2);

    outCustomfaceUtf8 = AvatarUtf8FromWide(g);
    outCustomfaceUtf8 += '|';
    outCustomfaceUtf8 += std::to_string(idx);
}

// 解码：把存储的 (facetype, customface) + 回退信息归一成「该画什么」。
//   facetype：存储的头像类型（见 AvatarKind；未知值按自动处理）。
//   customfaceUtf8：存储的 customface（UTF-8）。
//   fallbackName：回退显示名（自动 / 脏数据 / 第一期 image 回退时取其首字）。
//   userid：回退底色按 userid 稳定取色（与既有 AvatarColorForUser 同口径）。
// 返回：解码后的 AvatarSpec；脏数据 / 解析失败一律安全回退到自动渲染。
inline AvatarSpec DecodeAvatar(int facetype, const std::string& customfaceUtf8,
                               LPCTSTR fallbackName, int64_t userid)
{
    AvatarSpec spec;

    // 通用回退值：任何分支失败都落到这，保证渲染点永远有字形 + 底色可画。
    const CString  fbGlyph = AvatarFallbackGlyph(fallbackName);
    const COLORREF fbColor = AvatarColorForUser(userid);

    if (facetype == avatar_kind_glyph)
    {
        // 解析 "glyph|idx"：按首个分隔符切；glyph 非空、idx 为 1~3 位纯数字才算合法。
        std::string::size_type pos = customfaceUtf8.find('|');
        bool good = false;
        if (pos != std::string::npos && pos > 0 && pos + 1 < customfaceUtf8.size())
        {
            std::string glyphPart = customfaceUtf8.substr(0, pos);
            std::string idxPart   = customfaceUtf8.substr(pos + 1);

            bool allDigit = (!idxPart.empty() && idxPart.size() <= 3);
            for (size_t i = 0; allDigit && i < idxPart.size(); ++i)
            {
                if (idxPart[i] < '0' || idxPart[i] > '9')
                {
                    allDigit = false;
                }
            }

            CString g = AvatarWideFromUtf8(glyphPart);
            if (allDigit && !g.IsEmpty())
            {
                int idx = atoi(idxPart.c_str());
                spec.m_kind       = avatar_kind_glyph;
                spec.m_glyph      = g;
                spec.m_colorIndex = ((idx % kAvatarPickerSize) + kAvatarPickerSize) % kAvatarPickerSize;
                spec.m_color      = AvatarColorByIndex(idx);
                good = true;
            }
        }

        if (!good)
        {
            // customface 脏数据 → 安全回退到自动渲染。
            spec.m_kind  = avatar_kind_auto;
            spec.m_glyph = fbGlyph;
            spec.m_color = fbColor;
        }
        return spec;
    }

    if (facetype == avatar_kind_image)
    {
        // 真图：第二期才画。第一期 glyph/color 给自动回退，让未改造的渲染点继续画
        // monogram，m_fileId 留给第二期下载渲染用。
        spec.m_kind   = avatar_kind_image;
        spec.m_fileId = AvatarWideFromUtf8(customfaceUtf8);
        spec.m_glyph  = fbGlyph;
        spec.m_color  = fbColor;
        return spec;
    }

    // facetype == avatar_kind_auto 或未知值 → 自动渲染。
    spec.m_kind  = avatar_kind_auto;
    spec.m_glyph = fbGlyph;
    spec.m_color = fbColor;
    return spec;
}

} // namespace balloonmain
