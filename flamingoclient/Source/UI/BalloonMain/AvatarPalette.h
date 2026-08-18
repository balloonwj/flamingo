/**
 *  AvatarPalette.h
 *
 *  monogram 头像底色调色板 —— 把"按用户稳定取一个柔和底色"这件事收成
 *  单一真值源，供主面板联系人 / 会话 / 群头像与登录窗 Banner 头像共用，
 *  避免各处各抄一份调色板。
 *
 *  取色对调用方稳定：同一 userid / 同一账号字符串恒定映射到同一颜色，
 *  这样头像在不同界面、不同次启动之间颜色一致，用户可凭颜色快速辨认。
 *
 *  全部为 header-only 的 inline 纯函数（只做整型 / 字符串到颜色的映射，
 *  不碰任何 Win32 / GDI 资源），便于 FlamingoUnit 测试工程直接断言。
 *
 *  典型用法：
 *      COLORREF c1 = balloonmain::AvatarColorForUser(userid);     // 主面板按 id
 *      COLORREF c2 = balloonmain::AvatarColorForText(account);    // 登录窗按账号
 *
 *  balloonwj@qq.com   2026-06-03
 */

#pragma once

#include <windows.h>      // COLORREF / RGB
#include <cstdint>        // int64_t
#include <tchar.h>        // TCHAR / LPCTSTR / _T

namespace balloonmain {

// monogram 头像「自动取色」的哈希色槽个数。userid / 账号字符串哈希到 [0,8)
// 落在调色板前 8 槽 —— 此值保持为 8，确保历史账号自动底色在本次扩盘后不变。
const int kAvatarPaletteSize = 8;

// 调色板总色槽个数（供「首字母」页底色选择器与存储 colorIndex 用）。前 8 槽与
// kAvatarPaletteSize 重合（自动取色用），第 8~11 槽是为选择器扩出的备选色。
const int kAvatarPickerSize = 12;

// 按下标取调色板颜色。下标先按调色板总色槽数取模归一（含负数安全），
// 故任意 int 入参都不会越界。
//   index：任意整数下标（自动取色传 [0,8)，选择器/存储传 [0,12)）。
// 返回：调色板内的一个 COLORREF。
inline COLORREF AvatarColorByIndex(int index)
{
    // 取自设计稿的一组柔和底色（与 monogram 头像风格一致）。第 0 个橙色
    // 同时用作"默认头像"底色 —— 未登录账号 / 无可辨识信息时取它。
    // 前 8 项为历史既有色（不可改动，否则老账号自动底色会变）；后 4 项是为
    // 「首字母」页底色选择器扩出的备选色。具体取值后续按设计稿像素再校。
    static const COLORREF kPalette[kAvatarPickerSize] =
    {
        RGB(0xFF, 0x7A, 0x45),   // 0 橙（默认色，前 8 项不可改）
        RGB(0x3A, 0x86, 0xD9),   // 1 蓝
        RGB(0x2B, 0xB6, 0x73),   // 2 绿
        RGB(0x8B, 0x5C, 0xF6),   // 3 紫
        RGB(0xEF, 0x6C, 0x9A),   // 4 粉
        RGB(0xF5, 0x9E, 0x0B),   // 5 琥珀
        RGB(0x14, 0xB8, 0xA6),   // 6 青
        RGB(0xE1, 0x5B, 0x4C),   // 7 砖红
        RGB(0x64, 0x74, 0x8B),   // 8 石板灰（选择器扩充）
        RGB(0x1F, 0x29, 0x37),   // 9 近黑（选择器扩充）
        RGB(0x10, 0xB9, 0x81),   // 10 翡翠绿（选择器扩充）
        RGB(0xEC, 0x48, 0x99),   // 11 玫红（选择器扩充）
    };
    int idx = ((index % kAvatarPickerSize) + kAvatarPickerSize) % kAvatarPickerSize;
    return kPalette[idx];
}

// 按 userid 稳定取一个头像底色。
//   userid：用户数字 id（任意 64 位整数，负数安全）。
// 返回：调色板内的一个 COLORREF；同一 userid 恒定同色。
inline COLORREF AvatarColorForUser(int64_t userid)
{
    return AvatarColorByIndex((int)(userid % (int64_t)kAvatarPaletteSize));
}

// 按文本（如登录账号字符串）稳定取一个头像底色。
//   text：文本指针；NULL 或空串返回默认色（调色板第 0 个橙色）。
// 返回：调色板内的一个 COLORREF；同一文本恒定同色。
inline COLORREF AvatarColorForText(LPCTSTR text)
{
    if (text == NULL || text[0] == _T('\0'))
    {
        return AvatarColorByIndex(0);
    }

    // FNV-1a 32 位哈希 —— 实现简单、分布均匀，足够把账号散到各色槽。
    const unsigned int kFnvOffsetBasis = 2166136261u;   // FNV-1a 32 位初值
    const unsigned int kFnvPrime       = 16777619u;     // FNV-1a 32 位质数
    unsigned int hash = kFnvOffsetBasis;
    for (const TCHAR* p = text; *p != _T('\0'); ++p)
    {
        hash ^= (unsigned int)(unsigned short)(*p);
        hash *= kFnvPrime;
    }
    return AvatarColorByIndex((int)(hash % (unsigned int)kAvatarPaletteSize));
}

} // namespace balloonmain
