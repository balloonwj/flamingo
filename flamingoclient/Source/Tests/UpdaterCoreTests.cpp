/**
 *  更新器 FlamingoUpdater 三块纯逻辑的单元测试，UpdaterCoreTests.cpp
 *
 *  覆盖范围与 docs/客户端自动更新方案.md 第 9 节确认过的清单一致：
 *    · Suite UCL —— 命令行解析：带空格的路径、参数顺序颠倒、缺参数、--pid 非法值；
 *    · Suite ZPS —— zip 条目名安全校验：.. 穿越、绝对路径、盘符、UNC 与 \\?\ 前缀、
 *      正常相对路径的归一化；
 *    · Suite ZIP —— zip 中央目录解析：正常包、截断包、空包。
 *
 *  这三块都不碰文件系统、不起进程，纯内存断言。其中 ZPS 那一组尤其要紧：老版本更新器
 *  直接把 zip 条目名拼到安装目录后面就写盘，一个被构造过的升级包可以借提权运行的更新器
 *  往系统任意位置写文件。
 *
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-12
 */

#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <string>
#include <vector>

#include "../../FlamingoUpdater/CommandLine.h"
#include "../../FlamingoUpdater/ZipPathSafety.h"
#include "../../FlamingoUpdater/ZipReader.h"

using namespace iupdate;

namespace {

//—— 造一个最小可用的 zip 字节流，供中央目录解析用例使用 ——
//
//不去磁盘上放真包，是为了让用例能精确构造"截断""空包"这些边界情形，也不受
//工作目录与临时文件的影响。这里手写的是 zip 规范里的三块结构：本地文件头、
//中央目录条目、中央目录结束记录。

void PushU16(std::vector<unsigned char>& buf, unsigned int v)
{
    buf.push_back((unsigned char)(v & 0xFF));
    buf.push_back((unsigned char)((v >> 8) & 0xFF));
}

void PushU32(std::vector<unsigned char>& buf, unsigned long v)
{
    buf.push_back((unsigned char)(v & 0xFF));
    buf.push_back((unsigned char)((v >> 8) & 0xFF));
    buf.push_back((unsigned char)((v >> 16) & 0xFF));
    buf.push_back((unsigned char)((v >> 24) & 0xFF));
}

void PushBytes(std::vector<unsigned char>& buf, const char* s, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        buf.push_back((unsigned char)s[i]);
    }
}

/**
 *  造一个含单个 store（不压缩）条目的 zip。
 *    name：    条目名（ASCII，按 UTF-8 标记写入）
 *    content： 条目内容
 *  @return 完整的 zip 字节流。
 */
std::vector<unsigned char> MakeSingleEntryZip(const char* name, const char* content)
{
    const size_t nameLen = strlen(name);
    const size_t dataLen = strlen(content);

    std::vector<unsigned char> zip;

    //—— 本地文件头 ——
    const unsigned long localOffset = 0;
    PushU32(zip, 0x04034b50);          //签名
    PushU16(zip, 20);                  //解压所需版本
    PushU16(zip, 0x0800);              //通用位标记：bit 11 置位，条目名是 UTF-8
    PushU16(zip, 0);                   //压缩方法：0 = store
    PushU16(zip, 0);                   //修改时间
    PushU16(zip, 0);                   //修改日期
    PushU32(zip, 0);                   //CRC32（本用例不校验内容，填 0）
    PushU32(zip, (unsigned long)dataLen);   //压缩后大小
    PushU32(zip, (unsigned long)dataLen);   //解压后大小
    PushU16(zip, (unsigned int)nameLen);    //名字长度
    PushU16(zip, 0);                   //扩展字段长度
    PushBytes(zip, name, nameLen);
    PushBytes(zip, content, dataLen);

    //—— 中央目录条目 ——
    const unsigned long cdOffset = (unsigned long)zip.size();
    PushU32(zip, 0x02014b50);          //签名
    PushU16(zip, 20);                  //制作版本
    PushU16(zip, 20);                  //解压所需版本
    PushU16(zip, 0x0800);              //通用位标记
    PushU16(zip, 0);                   //压缩方法
    PushU16(zip, 0);                   //修改时间
    PushU16(zip, 0);                   //修改日期
    PushU32(zip, 0);                   //CRC32
    PushU32(zip, (unsigned long)dataLen);
    PushU32(zip, (unsigned long)dataLen);
    PushU16(zip, (unsigned int)nameLen);
    PushU16(zip, 0);                   //扩展字段长度
    PushU16(zip, 0);                   //注释长度
    PushU16(zip, 0);                   //起始卷号
    PushU16(zip, 0);                   //内部属性
    PushU32(zip, 0);                   //外部属性
    PushU32(zip, localOffset);         //本地文件头偏移
    PushBytes(zip, name, nameLen);

    const unsigned long cdSize = (unsigned long)zip.size() - cdOffset;

    //—— 中央目录结束记录 ——
    PushU32(zip, 0x06054b50);          //签名
    PushU16(zip, 0);                   //本卷号
    PushU16(zip, 0);                   //中央目录起始卷号
    PushU16(zip, 1);                   //本卷条目数
    PushU16(zip, 1);                   //总条目数
    PushU32(zip, cdSize);
    PushU32(zip, cdOffset);
    PushU16(zip, 0);                   //注释长度

    return zip;
}

//造一个不含任何条目的合法空 zip（只有一条中央目录结束记录）。
std::vector<unsigned char> MakeEmptyZip()
{
    std::vector<unsigned char> zip;
    PushU32(zip, 0x06054b50);
    PushU16(zip, 0);
    PushU16(zip, 0);
    PushU16(zip, 0);   //本卷条目数 0
    PushU16(zip, 0);   //总条目数 0
    PushU32(zip, 0);   //中央目录字节数 0
    PushU32(zip, 0);   //中央目录偏移 0
    PushU16(zip, 0);
    return zip;
}

} // namespace

// =====================================================================
// UCL. 命令行解析
// =====================================================================

TEST_CASE("UCL1 四个必填参数齐备时解析成功", "[updater]")
{
    wchar_t* argv[] = {
        (wchar_t*)L"FlamingoUpdater.exe",
        (wchar_t*)L"--zip",    (wchar_t*)L"D:\\app\\update\\Flamingo-3.3.0.zip",
        (wchar_t*)L"--target", (wchar_t*)L"D:\\app",
        (wchar_t*)L"--exec",   (wchar_t*)L"D:\\app\\Flamingo.exe",
        (wchar_t*)L"--pid",    (wchar_t*)L"12345"
    };

    UpdateArgs args;
    std::wstring err;
    REQUIRE(ParseCommandLine(9, argv, args, err));
    CHECK(err.empty());
    CHECK(args.m_zipPath == L"D:\\app\\update\\Flamingo-3.3.0.zip");
    CHECK(args.m_targetDir == L"D:\\app");
    CHECK(args.m_execPath == L"D:\\app\\Flamingo.exe");
    CHECK(args.m_pid == 12345UL);
    //没写 --silent 就不是静默模式
    CHECK_FALSE(args.m_silent);
}

TEST_CASE("UCL2 带空格的路径与顺序颠倒都能正确解析", "[updater]")
{
    //路径带空格是常态（Program Files）。这里的 argv 是 CommandLineToArgvW 拆好的结果，
    //引号已被系统去掉，所以元素里直接就是含空格的完整路径。
    wchar_t* argv[] = {
        (wchar_t*)L"FlamingoUpdater.exe",
        (wchar_t*)L"--silent",
        (wchar_t*)L"--pid",    (wchar_t*)L"7",
        (wchar_t*)L"--exec",   (wchar_t*)L"C:\\Program Files\\Flamingo IM\\Flamingo.exe",
        (wchar_t*)L"--target", (wchar_t*)L"C:\\Program Files\\Flamingo IM",
        (wchar_t*)L"--zip",    (wchar_t*)L"C:\\Program Files\\Flamingo IM\\update\\p.zip"
    };

    UpdateArgs args;
    std::wstring err;
    REQUIRE(ParseCommandLine(10, argv, args, err));
    CHECK(args.m_targetDir == L"C:\\Program Files\\Flamingo IM");
    CHECK(args.m_execPath == L"C:\\Program Files\\Flamingo IM\\Flamingo.exe");
    CHECK(args.m_pid == 7UL);
    CHECK(args.m_silent);
}

TEST_CASE("UCL3 缺任一必填参数都失败，且错误信息指明缺了哪个", "[updater]")
{
    //缺 --pid
    wchar_t* noPid[] = {
        (wchar_t*)L"FlamingoUpdater.exe",
        (wchar_t*)L"--zip",    (wchar_t*)L"a.zip",
        (wchar_t*)L"--target", (wchar_t*)L"C:\\app",
        (wchar_t*)L"--exec",   (wchar_t*)L"C:\\app\\a.exe"
    };
    UpdateArgs args;
    std::wstring err;
    CHECK_FALSE(ParseCommandLine(7, noPid, args, err));
    CHECK(err.find(L"--pid") != std::wstring::npos);

    //缺 --zip
    wchar_t* noZip[] = {
        (wchar_t*)L"FlamingoUpdater.exe",
        (wchar_t*)L"--target", (wchar_t*)L"C:\\app",
        (wchar_t*)L"--exec",   (wchar_t*)L"C:\\app\\a.exe",
        (wchar_t*)L"--pid",    (wchar_t*)L"1"
    };
    CHECK_FALSE(ParseCommandLine(7, noZip, args, err));
    CHECK(err.find(L"--zip") != std::wstring::npos);

    //完全没有参数
    wchar_t* bare[] = { (wchar_t*)L"FlamingoUpdater.exe" };
    CHECK_FALSE(ParseCommandLine(1, bare, args, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("UCL4 带值参数后面没有值、以及 --pid 取值非法", "[updater]")
{
    //--zip 是最后一个参数，后面没有值
    wchar_t* dangling[] = {
        (wchar_t*)L"FlamingoUpdater.exe",
        (wchar_t*)L"--target", (wchar_t*)L"C:\\app",
        (wchar_t*)L"--exec",   (wchar_t*)L"C:\\app\\a.exe",
        (wchar_t*)L"--pid",    (wchar_t*)L"1",
        (wchar_t*)L"--zip"
    };
    UpdateArgs args;
    std::wstring err;
    CHECK_FALSE(ParseCommandLine(8, dangling, args, err));
    CHECK(err.find(L"--zip") != std::wstring::npos);

    //--pid 不是纯数字：与其当成 12 处理，不如判非法，免得去等一个错误的进程
    wchar_t* badPid[] = {
        (wchar_t*)L"FlamingoUpdater.exe",
        (wchar_t*)L"--zip",    (wchar_t*)L"a.zip",
        (wchar_t*)L"--target", (wchar_t*)L"C:\\app",
        (wchar_t*)L"--exec",   (wchar_t*)L"C:\\app\\a.exe",
        (wchar_t*)L"--pid",    (wchar_t*)L"12ab"
    };
    CHECK_FALSE(ParseCommandLine(9, badPid, args, err));

    //--pid 为 0：0 不是有效的进程 id
    wchar_t* zeroPid[] = {
        (wchar_t*)L"FlamingoUpdater.exe",
        (wchar_t*)L"--zip",    (wchar_t*)L"a.zip",
        (wchar_t*)L"--target", (wchar_t*)L"C:\\app",
        (wchar_t*)L"--exec",   (wchar_t*)L"C:\\app\\a.exe",
        (wchar_t*)L"--pid",    (wchar_t*)L"0"
    };
    CHECK_FALSE(ParseCommandLine(9, zeroPid, args, err));
}

TEST_CASE("UCL5 未知参数被忽略而不是报错", "[updater]")
{
    //将来加了新开关，旧版更新器碰到它不该直接罢工
    wchar_t* argv[] = {
        (wchar_t*)L"FlamingoUpdater.exe",
        (wchar_t*)L"--brandnew",
        (wchar_t*)L"--zip",    (wchar_t*)L"a.zip",
        (wchar_t*)L"--target", (wchar_t*)L"C:\\app",
        (wchar_t*)L"--exec",   (wchar_t*)L"C:\\app\\a.exe",
        (wchar_t*)L"--pid",    (wchar_t*)L"9"
    };
    UpdateArgs args;
    std::wstring err;
    CHECK(ParseCommandLine(10, argv, args, err));
    CHECK(args.m_pid == 9UL);
}

TEST_CASE("UCL6 --lang 可选：解析出语言代码，入口可以先单独取出它", "[updater]")
{
    //带 --lang：完整解析与只取语言两条路都能拿到值
    wchar_t* withLang[] = {
        (wchar_t*)L"FlamingoUpdater.exe",
        (wchar_t*)L"--zip",    (wchar_t*)L"a.zip",
        (wchar_t*)L"--lang",   (wchar_t*)L"en-US",
        (wchar_t*)L"--target", (wchar_t*)L"C:\\app",
        (wchar_t*)L"--exec",   (wchar_t*)L"C:\\app\\a.exe",
        (wchar_t*)L"--pid",    (wchar_t*)L"9"
    };
    const int kWithLangCount = static_cast<int>(sizeof(withLang) / sizeof(withLang[0]));
    UpdateArgs args;
    std::wstring err;
    REQUIRE(ParseCommandLine(kWithLangCount, withLang, args, err));
    CHECK(args.m_language == L"en-US");
    CHECK(PeekLanguageArg(kWithLangCount, withLang) == L"en-US");

    //不带 --lang：语言为空（跟随系统），其余照常解析
    wchar_t* noLang[] = {
        (wchar_t*)L"FlamingoUpdater.exe",
        (wchar_t*)L"--zip",    (wchar_t*)L"a.zip",
        (wchar_t*)L"--target", (wchar_t*)L"C:\\app",
        (wchar_t*)L"--exec",   (wchar_t*)L"C:\\app\\a.exe",
        (wchar_t*)L"--pid",    (wchar_t*)L"9"
    };
    const int kNoLangCount = static_cast<int>(sizeof(noLang) / sizeof(noLang[0]));
    REQUIRE(ParseCommandLine(kNoLangCount, noLang, args, err));
    CHECK(args.m_language.empty());
    CHECK(PeekLanguageArg(kNoLangCount, noLang).empty());

    //--lang 在最后且没有值：完整解析报错并点名 --lang，只取语言时返回空串
    wchar_t* dangling[] = {
        (wchar_t*)L"FlamingoUpdater.exe",
        (wchar_t*)L"--zip",    (wchar_t*)L"a.zip",
        (wchar_t*)L"--target", (wchar_t*)L"C:\\app",
        (wchar_t*)L"--exec",   (wchar_t*)L"C:\\app\\a.exe",
        (wchar_t*)L"--pid",    (wchar_t*)L"9",
        (wchar_t*)L"--lang"
    };
    const int kDanglingCount = static_cast<int>(sizeof(dangling) / sizeof(dangling[0]));
    CHECK_FALSE(ParseCommandLine(kDanglingCount, dangling, args, err));
    CHECK(err.find(L"--lang") != std::wstring::npos);
    CHECK(PeekLanguageArg(kDanglingCount, dangling).empty());

    //参数为空
    CHECK(PeekLanguageArg(0, NULL).empty());
}

// =====================================================================
// ZPS. zip 条目名安全校验
// =====================================================================

TEST_CASE("ZPS1 正常相对路径通过并归一化为反斜杠", "[updater]")
{
    std::wstring out;

    REQUIRE(NormalizeZipEntryName(L"Flamingo.exe", out));
    CHECK(out == L"Flamingo.exe");

    //正斜杠统一转成反斜杠
    REQUIRE(NormalizeZipEntryName(L"Skins/default/main.xml", out));
    CHECK(out == L"Skins\\default\\main.xml");

    //连续分隔符与 . 段都被丢掉
    REQUIRE(NormalizeZipEntryName(L"Image//./icon.png", out));
    CHECK(out == L"Image\\icon.png");

    //中文名照常通过
    REQUIRE(NormalizeZipEntryName(L"配置\\flamingo.ini", out));
    CHECK(out == L"配置\\flamingo.ini");
}

TEST_CASE("ZPS2 .. 穿越一律拒绝", "[updater]")
{
    // 这是最要紧的一条：老版本会把这种条目名直接拼到安装目录后面写盘，
    // 而更新器往往是提权运行的，等于把系统目录的写权限交给了升级包。
    std::wstring out;

    CHECK_FALSE(NormalizeZipEntryName(L"..\\evil.dll", out));
    CHECK(out.empty());
    CHECK_FALSE(NormalizeZipEntryName(L"../evil.dll", out));
    //藏在中间的
    CHECK_FALSE(NormalizeZipEntryName(L"Skins\\..\\..\\Windows\\System32\\evil.dll", out));
    //藏在结尾的
    CHECK_FALSE(NormalizeZipEntryName(L"Skins\\..", out));
    //多层
    CHECK_FALSE(NormalizeZipEntryName(L"../../../../../../etc/passwd", out));
}

TEST_CASE("ZPS3 绝对路径、盘符、UNC 与 \\\\?\\ 前缀都拒绝", "[updater]")
{
    std::wstring out;

    //以分隔符开头的绝对路径
    CHECK_FALSE(NormalizeZipEntryName(L"\\Windows\\System32\\evil.dll", out));
    CHECK_FALSE(NormalizeZipEntryName(L"/etc/passwd", out));
    //盘符形式
    CHECK_FALSE(NormalizeZipEntryName(L"C:\\Windows\\evil.dll", out));
    //"相对于该盘当前目录"，同样不能接受
    CHECK_FALSE(NormalizeZipEntryName(L"C:evil.dll", out));
    //UNC 与长路径前缀（两者都以分隔符开头，被同一条规则挡掉）
    CHECK_FALSE(NormalizeZipEntryName(L"\\\\server\\share\\evil.dll", out));
    CHECK_FALSE(NormalizeZipEntryName(L"\\\\?\\C:\\evil.dll", out));
}

TEST_CASE("ZPS4 空串与只剩 . 的路径拒绝", "[updater]")
{
    std::wstring out;

    CHECK_FALSE(NormalizeZipEntryName(L"", out));
    CHECK_FALSE(NormalizeZipEntryName(L".", out));
    CHECK_FALSE(NormalizeZipEntryName(L"./", out));
    CHECK_FALSE(NormalizeZipEntryName(L"./././", out));
}

TEST_CASE("ZPS5 目录项按结尾分隔符识别", "[updater]")
{
    CHECK(IsDirectoryEntry(L"Skins/"));
    CHECK(IsDirectoryEntry(L"Skins\\default\\"));
    CHECK_FALSE(IsDirectoryEntry(L"Skins/default/main.xml"));
    CHECK_FALSE(IsDirectoryEntry(L""));
}

// =====================================================================
// ZIP. zip 中央目录解析
// =====================================================================

TEST_CASE("ZIP1 正常单条目包解析出条目元信息", "[updater]")
{
    const std::vector<unsigned char> zip = MakeSingleEntryZip("Flamingo.exe", "hello-world");

    std::vector<ZipEntry> entries;
    std::wstring err;
    REQUIRE(ParseCentralDirectory(&zip[0], zip.size(), entries, err));
    CHECK(err.empty());
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].m_name == L"Flamingo.exe");
    CHECK(entries[0].m_method == zip_method_store);
    CHECK(entries[0].m_uncompressedSize == 11);
    CHECK_FALSE(entries[0].m_isDirectory);

    //定位数据：store 方式下这段就是原文
    size_t dataOffset = 0;
    REQUIRE(LocateEntryData(&zip[0], zip.size(), entries[0], dataOffset, err));
    REQUIRE(dataOffset + 11 <= zip.size());
    CHECK(memcmp(&zip[dataOffset], "hello-world", 11) == 0);
}

TEST_CASE("ZIP2 空包解析成功且条目数为零", "[updater]")
{
    // 合法的空 zip：格式上没有任何问题，只是不含条目。这不该被当成解析错误——
    // "包里没内容"是业务问题，由调用方判断。
    const std::vector<unsigned char> zip = MakeEmptyZip();

    std::vector<ZipEntry> entries;
    std::wstring err;
    REQUIRE(ParseCentralDirectory(&zip[0], zip.size(), entries, err));
    CHECK(entries.empty());
    CHECK(err.empty());
}

TEST_CASE("ZIP3 截断的包如实报错而不是解出半截数据", "[updater]")
{
    const std::vector<unsigned char> full = MakeSingleEntryZip("Flamingo.exe", "hello-world");

    std::vector<ZipEntry> entries;
    std::wstring err;

    //① 砍掉尾部：连中央目录结束记录都找不到了
    std::vector<unsigned char> noEocd(full.begin(), full.end() - 10);
    CHECK_FALSE(ParseCentralDirectory(&noEocd[0], noEocd.size(), entries, err));
    CHECK_FALSE(err.empty());

    //② 完全空的内容
    CHECK_FALSE(ParseCentralDirectory(NULL, 0, entries, err));

    //③ 只有几个字节，连结束记录的固定长度都不够
    const unsigned char tiny[4] = { 'P', 'K', 3, 4 };
    CHECK_FALSE(ParseCentralDirectory(tiny, sizeof(tiny), entries, err));
}

TEST_CASE("ZIP4 中央目录偏移越界时报错", "[updater]")
{
    std::vector<unsigned char> zip = MakeSingleEntryZip("Flamingo.exe", "hello-world");

    // 把结束记录里的"中央目录偏移"改成一个超出文件的值，模拟损坏的包。
    // 结束记录固定 22 字节，偏移字段在其起点 +16 处。
    const size_t eocd = zip.size() - 22;
    zip[eocd + 16] = 0xFF;
    zip[eocd + 17] = 0xFF;
    zip[eocd + 18] = 0x00;
    zip[eocd + 19] = 0x00;

    std::vector<ZipEntry> entries;
    std::wstring err;
    CHECK_FALSE(ParseCentralDirectory(&zip[0], zip.size(), entries, err));
    CHECK_FALSE(err.empty());
}
