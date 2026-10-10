/**
 *  登录窗的账号头像（2026-10-04 起）：按本机账号列表里存的 userid 与头像，算出账号下拉列表每一行与上方
 *  大头像要画什么，生成并持有对应的位图，并把历史账号连同头像与姓名填进账号下拉框（每行「头像 - 账号 - 姓名」）。
 *
 *  头像的取法与主面板联系人列表同一口径（AvatarSpec.h 的 DecodeAvatar）：字形头像画选定的字形与调色板底色，
 *  自动头像画姓名首字与按 userid 取的底色，照片头像在照片已下载到本机时画照片、否则画字形。这次升级之前存下的
 *  账号还没有 userid，画姓名（没有姓名时账号）首字，底色按账号取，与原来的大头像一致。
 *
 *  照片路径由调用方经函数指针提供：照片缓存的路径规则在 net::ImageTransfer 里，单元测试工程不编译它，测试改传桩函数。
 *
 *  典型用法：
 *      balloonauth::CLoginAvatarBitmaps bitmaps;
 *      balloonauth::FillAccountCombo(combo, &accountList, bitmaps, &LocalPhotoPath);
 *      const balloonauth::LoginAvatarSpec spec = balloonauth::ResolveLoginAvatar(account, info, LocalPhotoPath(account, info));
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include <windows.h>
#include <atlstr.h>   // CString
#include <string>

#include "../../Session/LoginAccountList.h"
#include "../BalloonMain/AvatarBitmapCache.h"

namespace balloonwjui {
class DuiComboBox;
}

namespace balloonauth {

// 账号下拉列表的行高（像素）：放得下 kAccountAvatarPx 的头像，上下各留 6 像素
const int kAccountRowHeight = 40;

// 账号下拉列表每行头像的边长（像素）
const int kAccountAvatarPx = 28;

// 登录窗为一个账号要画的头像
struct LoginAvatarSpec
{
    CString      m_glyph;       // 字形头像的字形（画照片时也备着：照片解码失败就画它）
    COLORREF     m_color;       // 字形头像的底色
    std::wstring m_photoPath;   // 照片在本机的路径；空串表示画字形

    LoginAvatarSpec()
        : m_color(RGB(0, 0, 0))
    {
    }
};

/**
 *  取一个账号的照片头像在本机的路径。
 *    account：账号名。
 *    info：   该账号在本机账号列表里的记录，可为 NULL。
 *  返回：头像类型为照片、照片已下载到本机时返回路径，否则返回空串。
 */
typedef std::wstring (*LoginPhotoPathFn)(const CString& account, const LOGIN_ACCOUNT_INFO* info);

/**
 *  算出一个账号要画的头像（纯函数）。
 *    account：  账号名（输入框里的文字或下拉列表的一项）。
 *    info：     该账号在本机账号列表里的记录；NULL 表示本机没登录过这个账号。
 *    photoPath：照片头像在本机的路径（调用方经 LoginPhotoPathFn 取得）；不是照片头像或照片不在本机时传空串。
 *  返回：本机没登录过 → 账号首字与默认底色（调色板第 0 个，即原来的橙色方块）；
 *        有记录但还没有 userid → 姓名（没有姓名时账号）首字，底色按账号取；
 *        有 userid → 按 DecodeAvatar 取字形与底色，照片头像且 photoPath 非空时 m_photoPath 为它。
 */
LoginAvatarSpec ResolveLoginAvatar(const CString& account, const LOGIN_ACCOUNT_INFO* info, const std::wstring& photoPath);

/**
 *  登录窗用到的头像位图：同一个头像、同一边长只生成一次，本对象持有全部位图，Clear 或析构时释放。
 *  圆形，与主面板联系人列表同一画法（字形 MakeMonogramAvatar，照片 MakeRoundedAvatarFromFile）。
 */
class CLoginAvatarBitmaps
{
public:
    CLoginAvatarBitmaps();

    /**
     *  取一个头像的圆形位图。
     *    spec：  要画的头像（见 ResolveLoginAvatar）。
     *    sizePx：边长（像素），须大于 0。
     *  返回：位图，归本对象所有，调用方不得释放；照片解码失败时改画字形；参数非法或生成失败时返回 NULL。
     */
    HBITMAP GetBitmap(const LoginAvatarSpec& spec, int sizePx);

    // 释放全部位图。之前取到的位图句柄随之失效，调用方须先让控件不再引用它们。
    void Clear();

    // 当前持有的位图张数（测试用）
    size_t Count() const;

private:
    balloonmain::CAvatarBitmapCache m_cache;   // 按内容缓存的位图，本对象析构时随之释放
};

/**
 *  把本机账号列表里的历史账号逐个填进账号下拉框：每项的文字是账号，浮层里该行左侧是头像、右侧是姓名（没有姓名时不显示），
 *  并设好下拉行高与头像边长。不清空下拉框里已有的项，调用方需要时先 ResetContent。
 *    combo：      账号下拉框，不可为 NULL。
 *    list：       本机账号列表，不可为 NULL。
 *    bitmaps：    头像位图的持有者，须比下拉框活得久（或在下拉框销毁、清空之后才 Clear）。
 *    photoPathFn：取照片路径的函数，可为 NULL（此时一律画字形）。
 *  返回：填进去的项数。
 */
int FillAccountCombo(balloonwjui::DuiComboBox* combo, CLoginAccountList* list, CLoginAvatarBitmaps& bitmaps,
                     LoginPhotoPathFn photoPathFn);

} // namespace balloonauth
