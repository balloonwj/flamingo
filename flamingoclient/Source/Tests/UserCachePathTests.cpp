/**
 *  按登录账号隔离的本地缓存目录口径单元测试（Suite UCP），UserCachePathTests.cpp
 *
 *  背景：Users 目录下原本混着两套命名 —— 配置 / 聊天记录 / 头像按登录账号
 *  （Users\MY0309\），而文件、图片、头像三处缓存按数字 userid（Users\5309\），
 *  同一个人被劈成两个目录。现已统一到登录账号，本组用例钉住这个口径。
 *
 *  覆盖：目录拼接、结尾反斜杠的容错、空入参、账号净化与目录穿越防护。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-11
 */
#include "catch_amalgamated.hpp"
#include "../net/UserCachePath.h"

using namespace net;

TEST_CASE("UCP1 按登录账号拼出缓存目录", "[usercachepath]")
{
    // 这是本次改动的核心口径：Users 下一律用登录账号，不再出现数字 userid。
    CHECK(UserCacheDir(L"E:\\flamingo\\flamingoclient\\Bin\\", L"MY0309", L"ChatFile")
          == L"E:\\flamingo\\flamingoclient\\Bin\\Users\\MY0309\\ChatFile\\");
    CHECK(UserCacheDir(L"E:\\flamingo\\flamingoclient\\Bin\\", L"MY0309", L"ChatImage")
          == L"E:\\flamingo\\flamingoclient\\Bin\\Users\\MY0309\\ChatImage\\");
    CHECK(UserCacheDir(L"E:\\flamingo\\flamingoclient\\Bin\\", L"EMP5321", L"Avatar")
          == L"E:\\flamingo\\flamingoclient\\Bin\\Users\\EMP5321\\Avatar\\");
}

TEST_CASE("UCP2 子目录留空时只拼到账号那一层", "[usercachepath]")
{
    CHECK(UserCacheDir(L"C:\\app\\", L"MY0309", NULL) == L"C:\\app\\Users\\MY0309\\");
    CHECK(UserCacheDir(L"C:\\app\\", L"MY0309", L"") == L"C:\\app\\Users\\MY0309\\");
}

TEST_CASE("UCP3 主目录没带结尾反斜杠也能拼对", "[usercachepath]")
{
    // 全局 g_szHomePath 的约定是以反斜杠结尾，但这个约定全靠各处自觉。少了一个
    // 反斜杠会拼出 "C:\appUsers\..." 这种查不出问题的路径，故在此兜一道。
    CHECK(UserCacheDir(L"C:\\app", L"MY0309", L"ChatFile") == L"C:\\app\\Users\\MY0309\\ChatFile\\");
}

TEST_CASE("UCP4 主目录或账号为空：返回空串，交调用方按无缓存处理", "[usercachepath]")
{
    // 未登录时账号是空的。这时候宁可返回空串让调用方判"没有缓存"，也不能退化成
    // 相对路径，把缓存写到进程当前工作目录里去。
    CHECK(UserCacheDir(L"", L"MY0309", L"ChatFile").empty());
    CHECK(UserCacheDir(L"C:\\app\\", L"", L"ChatFile").empty());
    CHECK(UserCacheDir(L"", L"", L"ChatFile").empty());
}

TEST_CASE("UCP5 正常账号原样保留", "[usercachepath]")
{
    // 服务端生成的账号只有字母和数字，净化不该动它们。
    CHECK(SanitizeAccountForPath(L"MY0309") == L"MY0309");
    CHECK(SanitizeAccountForPath(L"EMP5321") == L"EMP5321");
}

TEST_CASE("UCP6 账号里的路径分隔符被净化，写不出 Users 目录之外", "[usercachepath]")
{
    // 账号规则将来放宽、或本地历史数据里混进异常账号时，不能让它把缓存写到别处去。
    // 分隔符换成下划线后，开头那两个点被"去首尾点"一并吃掉，剩下的 ".." 只是普通
    // 目录名的一部分 —— 没有分隔符跟着，它跳不出任何一层。
    CHECK(SanitizeAccountForPath(L"..\\..\\windows") == L"_.._windows");
    CHECK(SanitizeAccountForPath(L"a/b") == L"a_b");
    CHECK(SanitizeAccountForPath(L"C:evil") == L"C_evil");

    // 拼进完整路径后，账号仍是 Users 下的一层普通目录，跳不出去。
    CHECK(UserCacheDir(L"C:\\app\\", L"..\\..\\windows", L"ChatFile")
          == L"C:\\app\\Users\\_.._windows\\ChatFile\\");
}

TEST_CASE("UCP7 账号里的保留字符与控制字符换成下划线", "[usercachepath]")
{
    CHECK(SanitizeAccountForPath(L"a*b?c\"d<e>f|g") == L"a_b_c_d_e_f_g");
    CHECK(SanitizeAccountForPath(L"a\tb") == L"a_b");
}

TEST_CASE("UCP8 首尾的点与空白被去掉", "[usercachepath]")
{
    // Windows 建目录时会把结尾的点和空格默默吃掉。留着的话，本函数拼出来的路径
    // 与磁盘上真实的目录名对不上，缓存就会"每次都找不到"。
    CHECK(SanitizeAccountForPath(L"  MY0309  ") == L"MY0309");
    CHECK(SanitizeAccountForPath(L"MY0309...") == L"MY0309");
    CHECK(SanitizeAccountForPath(L"...") == L"");
    CHECK(SanitizeAccountForPath(L"   ") == L"");

    // 净化后什么都不剩，等同于拿不到账号，须返回空串。
    CHECK(UserCacheDir(L"C:\\app\\", L"...", L"ChatFile").empty());
}
