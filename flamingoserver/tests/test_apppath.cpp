/**
 *  可执行文件路径工具单元测试（Suite APP），test_apppath.cpp
 *
 *  覆盖 utils/AppPath 的三个函数：
 *    - IsAbsolutePath 对四种绝对路径写法的判定（POSIX、Windows 盘符、UNC、当前盘根目录）；
 *    - ResolveAgainstDirectory 的拼接语义：相对路径接到基准目录后面、绝对路径原样放行、
 *      基准目录自带分隔符时不产生双分隔符、"./" 前缀被去掉、空入参的回落行为；
 *    - GetExecutableDirectory 返回的是一个真实存在、且不带结尾分隔符的绝对路径目录。
 *
 *  前两个函数是纯函数，不碰文件系统，所以用例里的路径都是虚构的，不需要真的建目录。
 *
 *  balloonwj@qq.com   2026-08-10
 */
#include <string>
#include <sys/types.h>
#include <sys/stat.h>

#include "doctest.h"
#include "../utils/AppPath.h"

//用例里统一使用的虚构基准目录。两种分隔符各准备一份，用来覆盖跨平台的写法。
static const char* const BASE_DIR_WIN   = "E:\\flamingo\\flamingoserver\\windowsproj";
static const char* const BASE_DIR_POSIX = "/opt/flamingo/bin";

//判断一个路径是不是真实存在的目录。用于校验 GetExecutableDirectory 的返回值。
static bool DirectoryExists(const std::string& path)
{
    if (path.empty())
        return false;

    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        return false;

    return (st.st_mode & S_IFDIR) != 0;
}

// ---------------------------------------------------------------------------
// APP1  绝对路径判定
// ---------------------------------------------------------------------------

TEST_CASE("APP1 IsAbsolutePath 认得四种绝对路径写法，相对路径与空串则为假")
{
    //POSIX 绝对路径
    CHECK(apppath::IsAbsolutePath("/var/lib/flamingo/flamingo.db"));
    CHECK(apppath::IsAbsolutePath("/"));

    //Windows 盘符
    CHECK(apppath::IsAbsolutePath("D:\\db\\flamingo.db"));
    CHECK(apppath::IsAbsolutePath("D:/db/flamingo.db"));

    //UNC 共享路径
    CHECK(apppath::IsAbsolutePath("\\\\fileserver\\share\\flamingo.db"));

    //Windows 上「当前盘符的根目录」写法
    CHECK(apppath::IsAbsolutePath("\\data\\flamingo.db"));

    //相对路径
    CHECK_FALSE(apppath::IsAbsolutePath("data/flamingo.db"));
    CHECK_FALSE(apppath::IsAbsolutePath("flamingo.db"));
    CHECK_FALSE(apppath::IsAbsolutePath("./data/flamingo.db"));
    CHECK_FALSE(apppath::IsAbsolutePath("../data/flamingo.db"));

    //空串不是绝对路径
    CHECK_FALSE(apppath::IsAbsolutePath(""));
}

// ---------------------------------------------------------------------------
// APP2  相对路径拼接
// ---------------------------------------------------------------------------

TEST_CASE("APP2 相对路径接到基准目录后面，裸文件名与多级路径都成立")
{
    //这是本次改动的主用例：dbpath=data/flamingo.db 要落在可执行文件旁边
    std::string got = apppath::ResolveAgainstDirectory(BASE_DIR_WIN, "data/flamingo.db");
    CHECK(got == "E:\\flamingo\\flamingoserver\\windowsproj\\data/flamingo.db");

    //裸文件名
    got = apppath::ResolveAgainstDirectory(BASE_DIR_POSIX, "flamingo.db");
    CHECK(got == "/opt/flamingo/bin/flamingo.db");

    //多级相对路径
    got = apppath::ResolveAgainstDirectory(BASE_DIR_POSIX, "data/sub/flamingo.db");
    CHECK(got == "/opt/flamingo/bin/data/sub/flamingo.db");

    //".." 不做归并，原样接上去，由操作系统在打开文件时处理
    got = apppath::ResolveAgainstDirectory(BASE_DIR_POSIX, "../data/flamingo.db");
    CHECK(got == "/opt/flamingo/bin/../data/flamingo.db");
}

// ---------------------------------------------------------------------------
// APP3  绝对路径原样放行
// ---------------------------------------------------------------------------

TEST_CASE("APP3 已经是绝对路径的 dbpath 不被改动，部署方指定的位置说了算")
{
    CHECK(apppath::ResolveAgainstDirectory(BASE_DIR_WIN, "D:\\db\\flamingo.db")
          == "D:\\db\\flamingo.db");
    CHECK(apppath::ResolveAgainstDirectory(BASE_DIR_WIN, "/var/lib/flamingo/flamingo.db")
          == "/var/lib/flamingo/flamingo.db");
    CHECK(apppath::ResolveAgainstDirectory(BASE_DIR_POSIX, "\\\\fileserver\\share\\flamingo.db")
          == "\\\\fileserver\\share\\flamingo.db");
    CHECK(apppath::ResolveAgainstDirectory(BASE_DIR_POSIX, "\\data\\flamingo.db")
          == "\\data\\flamingo.db");
}

// ---------------------------------------------------------------------------
// APP4  分隔符处理
// ---------------------------------------------------------------------------

TEST_CASE("APP4 基准目录自带结尾分隔符时不产生双分隔符，./ 前缀被去掉")
{
    //基准目录末尾已有分隔符：两种分隔符都不该再补一个
    CHECK(apppath::ResolveAgainstDirectory("/opt/flamingo/bin/", "data/flamingo.db")
          == "/opt/flamingo/bin/data/flamingo.db");
    CHECK(apppath::ResolveAgainstDirectory("E:\\flamingo\\", "data\\flamingo.db")
          == "E:\\flamingo\\data\\flamingo.db");

    //根目录这种极端情形也不该拼成 "//"
    CHECK(apppath::ResolveAgainstDirectory("/", "flamingo.db") == "/flamingo.db");

    //"./" 与 ".\" 前缀被去掉，连续多段也一并去掉
    CHECK(apppath::ResolveAgainstDirectory("/opt/flamingo/bin", "./data/flamingo.db")
          == "/opt/flamingo/bin/data/flamingo.db");
    CHECK(apppath::ResolveAgainstDirectory("/opt/flamingo/bin", ".\\data\\flamingo.db")
          == "/opt/flamingo/bin/data\\flamingo.db");
    CHECK(apppath::ResolveAgainstDirectory("/opt/flamingo/bin", "././data/flamingo.db")
          == "/opt/flamingo/bin/data/flamingo.db");
}

// ---------------------------------------------------------------------------
// APP5  空入参的回落
// ---------------------------------------------------------------------------

TEST_CASE("APP5 基准目录为空时回落到旧行为，路径为空时原样返回空")
{
    //取不到可执行文件目录时不能把服务卡住：原样返回，路径仍按工作目录解析
    CHECK(apppath::ResolveAgainstDirectory("", "data/flamingo.db") == "data/flamingo.db");

    //路径为空没有可解析的内容，交给上层去回落到默认值
    CHECK(apppath::ResolveAgainstDirectory(BASE_DIR_POSIX, "").empty());
    CHECK(apppath::ResolveAgainstDirectory("", "").empty());
}

// ---------------------------------------------------------------------------
// APP6  取可执行文件目录
// ---------------------------------------------------------------------------

TEST_CASE("APP6 GetExecutableDirectory 返回真实存在、不带结尾分隔符的绝对路径目录")
{
    std::string dir = apppath::GetExecutableDirectory();

    //本测试程序自身就是一个可执行文件，这里必须取得到
    REQUIRE_FALSE(dir.empty());

    //必须是绝对路径，否则拿去拼 dbpath 就失去意义了
    CHECK(apppath::IsAbsolutePath(dir));

    //目录必须真实存在 —— 测试程序自己就躺在里面
    CHECK(DirectoryExists(dir));

    //不带结尾分隔符：ResolveAgainstDirectory 依赖这个约定来决定要不要补分隔符。
    //根目录是唯一的例外，它本身就是一个分隔符。
    if (dir.size() > 1)
    {
        char tail = dir[dir.size() - 1];
        CHECK(tail != '/');
        CHECK(tail != '\\');
    }

    //拼出来的路径应当落在这个目录下，即以该目录开头
    std::string resolved = apppath::ResolveAgainstDirectory(dir, "data/flamingo.db");
    CHECK(resolved.compare(0, dir.size(), dir) == 0);
    CHECK(resolved.size() > dir.size());
}
