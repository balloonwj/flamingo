/**
 *  按登录账号隔离的本地缓存目录口径，UserCachePath.h
 *
 *  客户端所有"按登录用户分开存"的本地数据都挂在 <主目录>Users\<登录账号>\ 下：
 *  配置（<账号>.ini）、聊天记录（ChatLog）、头像（UserThumb）早就是这个口径，
 *  文件缓存（ChatFile）、图片缓存（ChatImage）、头像缓存（Avatar）此前却用的是
 *  数字 userid，于是同一个人在 Users 下有两个目录（MY0309 与 5309 各一份）。
 *  本文件把这条口径收敛到一处，三处缓存都改用登录账号，不再各拼各的。
 *
 *  除 EnsureUserEssentialDirs 会实际建目录外，其余函数都是纯逻辑：不碰磁盘、不读全局
 *  状态（主目录由调用方传入），因此可以完全用单元测试覆盖（见
 *  flamingoclient/Source/Tests/UserCachePathTests.cpp，Suite UCP 与 ED）。
 *
 *  典型用法：
 *      std::wstring dir = net::UserCacheDir(g_szHomePath, account, L"ChatFile");
 *      // dir == L"...\\Bin\\Users\\MY0309\\ChatFile\\"
 *
 *  balloonwj@qq.com   2026-08-11
 */

#pragma once

#include <string>
#include <vector>

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

/**
 *  拼出账号配置文件的全路径：<homeDir>Users\<账号>\<账号>.ini。
 *
 *  账号配置是 ini 格式的文本（窗口位置、字体、提示音开关等），由 CUserConfig 读写。
 *  客户端里需要这个路径的地方都应调用本函数，不要各自再拼一遍。
 *  本函数只拼字符串，不创建目录、不判断文件是否存在。
 *
 *  @param homeDir 数据根目录（全局 g_szDataRoot），约定同 UserCacheDir。为空时返回空串。
 *  @param account 登录账号；内部经 SanitizeAccountForPath 净化，目录名与文件名用同一个
 *                 净化结果。为空、或净化后为空时返回空串（典型是还没登录）。
 *  @return 配置文件全路径；拼不出时返回空串。
 */
std::wstring UserConfigFilePath(const std::wstring& homeDir, const std::wstring& account);

/**
 *  列出登录后必须事先建好的按账号隔离的目录：<dataRoot>Users\<账号>\ 及其下的
 *  ChatImage\、UserThumb\，结尾都带反斜杠，由浅到深排列（第 0 项是账号目录本身）。
 *
 *  账号配置（<账号>.ini，经 WritePrivateProfileString 写入，父目录不存在时直接失败）、
 *  最近联系人列表、本地消息库（只建最后一级 ChatLog）都假定账号目录已经存在，
 *  因此登录成功后要先把这些目录建好。本函数只拼字符串，不碰磁盘。
 *
 *  @param dataRoot 数据根目录（全局 g_szDataRoot），约定同 UserCacheDir。为空时返回空列表。
 *  @param account  登录账号；内部经 SanitizeAccountForPath 净化。为空、或净化后为空时
 *                  返回空列表。
 *  @return 目录列表；拼不出时返回空列表。
 */
std::vector<std::wstring> UserEssentialDirs(const std::wstring& dataRoot, const std::wstring& account);

/**
 *  逐级创建 UserEssentialDirs 列出的全部目录，数据根目录本身与 Users 这些中间层
 *  不存在时一并创建；已存在的目录视为成功。
 *
 *  与本文件其余函数不同，这个函数会实际修改磁盘。
 *
 *  @param dataRoot 数据根目录（全局 g_szDataRoot），约定同 UserCacheDir。
 *  @param account  登录账号，规则同 UserEssentialDirs。
 *  @return 全部目录都已存在时返回 true；入参拼不出目录（此时不创建任何目录）或
 *          任一目录创建失败时返回 false。
 */
bool EnsureUserEssentialDirs(const std::wstring& dataRoot, const std::wstring& account);

} // namespace net
