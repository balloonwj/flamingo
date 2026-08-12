/**
 *  按登录账号隔离的本地缓存目录口径，UserCachePath.h
 *
 *  客户端所有"按登录用户分开存"的本地数据都挂在 <主目录>Users\<登录账号>\ 下：
 *  配置（<账号>.cfg）、聊天记录（ChatLog）、头像（UserThumb）早就是这个口径，
 *  文件缓存（ChatFile）、图片缓存（ChatImage）、头像缓存（Avatar）此前却用的是
 *  数字 userid，于是同一个人在 Users 下有两个目录（MY0309 与 5309 各一份）。
 *  本文件把这条口径收敛到一处，三处缓存都改用登录账号，不再各拼各的。
 *
 *  纯逻辑：不碰磁盘、不读全局状态（主目录由调用方传入），因此可以完全用单元测试
 *  覆盖（见 flamingoclient/Source/Tests/UserCachePathTests.cpp，Suite UCP）。
 *
 *  典型用法：
 *      std::wstring dir = net::UserCacheDir(g_szHomePath, account, L"ChatFile");
 *      // dir == L"...\\Bin\\Users\\MY0309\\ChatFile\\"
 *
 *  balloonwj@qq.com   2026-08-11
 */

#pragma once

#include <string>

namespace net {

/**
 *  把登录账号净化成可安全用作目录名的形式。
 *
 *  账号是服务端按固定规则生成的（形如 EMP1024 / MY0089），正常情况下本函数原样
 *  返回。它防的是两种意外：账号规则将来放宽，以及本地历史数据里混进了异常账号 ——
 *  账号一旦含有 '\'、'/'、':' 或 ".."，直接拼进路径就能把缓存写到 Users 目录之外去。
 *
 *  规则：Windows 文件名保留字符（\ / : * ? " < > |）与控制字符一律换成下划线，
 *  再去掉首尾的点与空白（Windows 会把结尾的点和空格吃掉，留着会让"拼出来的路径"
 *  与"磁盘上真实的目录名"对不上）。
 *
 *  @param account 登录账号（宽字符）。
 *  @return 净化后的账号；入参为空、或净化后不剩任何字符时返回空串。
 */
std::wstring SanitizeAccountForPath(const std::wstring& account);

/**
 *  拼出某一类本地缓存的目录：<homeDir>Users\<账号>\<subDir>\，结尾带反斜杠。
 *
 *  本函数只拼字符串，不创建目录、不判断存在 —— 建目录由各缓存自己在落地写文件前做。
 *
 *  @param homeDir 程序主目录，须以反斜杠结尾（全局 g_szHomePath 即是这个约定）。
 *                 为空时返回空串。
 *  @param account 登录账号；内部会经 SanitizeAccountForPath 净化。为空、或净化后
 *                 为空时返回空串 —— 表示当前拼不出有效路径（典型是还没登录），
 *                 调用方据此判定为"没有缓存"。
 *  @param subDir  缓存类别子目录名，如 L"ChatFile" / L"ChatImage" / L"Avatar"。
 *                 传空指针或空串时返回 <homeDir>Users\<账号>\。
 *  @return 目录路径（以反斜杠结尾）；拼不出时返回空串。
 */
std::wstring UserCacheDir(const std::wstring& homeDir, const std::wstring& account,
                          const wchar_t* subDir);

} // namespace net
