/**
 *  单聊「发送文件夹」本地清单单元测试（Suite FMAN），FolderManifestTests.cpp
 *
 *  覆盖 foldertransfer 的三部分能力：
 *    路径安全校验（FMAN1~FMAN10）—— 这是防目录穿越的关键，收发两端共用同一份判断，
 *      每一类必须拒绝的写法各锁一条用例：".."、以分隔符开头的绝对路径、盘符前缀、
 *      Windows 非法字符与控制字符、保留设备名（含 "CON.txt" 这种带扩展名的形式）、
 *      空路径、单段超长、整条超长、"." 段；正常的多层相对路径必须放行。
 *    纯逻辑辅助（FMAN11~FMAN17）—— 相对路径规范化、UTF-16 与 UTF-8 互转、根目录名
 *      提取、长路径前缀拼接、规模统计、整份清单复核。
 *    目录扫描（FMAN18~FMAN21）—— 在临时目录里真造一棵树（嵌套子目录、空目录、中文名
 *      文件、零字节文件）验证清单与统计值；再覆盖根路径异常、层级超限、总长超过 260
 *      的长路径三种边界。
 *
 *  碰磁盘的用例都在系统临时目录下建自己的树，目录名带<u>进程 id</u>，避免多个测试进程
 *  并发跑时互相踩；用例结束时递归删掉。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-08-13
 */
#include "catch_amalgamated.hpp"
#include "../Feature/FolderTransfer/FolderManifest.h"

#include <windows.h>
#include <string>
#include <vector>

using namespace foldertransfer;

namespace {

//造一条本套件专用的临时目录路径。带进程 id 与用例自己的后缀，两个测试进程同时跑
//也不会用到同一个目录。
std::wstring MakeTempRootPath(const wchar_t* caseSuffix)
{
    wchar_t tempDir[MAX_PATH] = { 0 };
    const DWORD n = ::GetTempPathW(MAX_PATH, tempDir);
    REQUIRE(n > 0);

    std::wstring path = tempDir;
    path += L"flamingo_fman_";
    path += std::to_wstring(static_cast<unsigned long>(::GetCurrentProcessId()));
    path += L"_";
    path += caseSuffix;
    return path;
}

//建一层目录。已存在也算成功（用例里会反复建同一条前缀）。
bool MakeDir(const std::wstring& path)
{
    if (::CreateDirectoryW(MakeLongPath(path).c_str(), NULL))
        return true;

    return ::GetLastError() == ERROR_ALREADY_EXISTS;
}

//写一个指定字节数的文件。size 传 0 就是造一个零字节文件（清单里同样要有它）。
bool WriteBytes(const std::wstring& path, const char* data, DWORD size)
{
    HANDLE handle = ::CreateFileW(MakeLongPath(path).c_str(), GENERIC_WRITE, 0, NULL,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return false;

    bool ok = true;
    if (size > 0)
    {
        DWORD wrote = 0;
        ok = (::WriteFile(handle, data, size, &wrote, NULL) != FALSE) && (wrote == size);
    }

    ::CloseHandle(handle);
    return ok;
}

//递归删掉一棵目录树。用例收尾用，故不关心中途失败（临时目录留下残余不影响判定）。
void RemoveTree(const std::wstring& path)
{
    std::wstring pattern = MakeLongPath(path);
    pattern += L"\\*";

    WIN32_FIND_DATAW findData;
    ::memset(&findData, 0, sizeof(findData));

    HANDLE hFind = ::FindFirstFileW(pattern.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        for (;;)
        {
            const wchar_t* const name = findData.cFileName;
            const bool isDotEntry = (name[0] == L'.')
                                    && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'));
            if (!isDotEntry)
            {
                std::wstring child = path;
                child += L"\\";
                child += name;

                if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                    RemoveTree(child);
                else
                    ::DeleteFileW(MakeLongPath(child).c_str());
            }

            if (!::FindNextFileW(hFind, &findData))
                break;
        }

        ::FindClose(hFind);
    }

    ::RemoveDirectoryW(MakeLongPath(path).c_str());
}

//在目录清单里找一条相对路径，返回下标；找不到返回 -1。
ptrdiff_t IndexOfDir(const std::vector<std::string>& dirs, const std::string& value)
{
    for (size_t i = 0; i < dirs.size(); ++i)
    {
        if (dirs[i] == value)
            return static_cast<ptrdiff_t>(i);
    }

    return -1;
}

//在文件清单里找一条相对路径，返回下标；找不到返回 -1。
ptrdiff_t IndexOfFile(const std::vector<FolderManifestFile>& files, const std::string& value)
{
    for (size_t i = 0; i < files.size(); ++i)
    {
        if (files[i].m_relativePath == value)
            return static_cast<ptrdiff_t>(i);
    }

    return -1;
}

} // namespace

// ==================== 路径安全校验 ====================

TEST_CASE("FMAN1 正常的多层相对路径通过校验")
{
    CHECK(CheckFolderRelativePath("a.txt") == kFolderPathOk);
    CHECK(CheckFolderRelativePath("sub/a.txt") == kFolderPathOk);
    CHECK(CheckFolderRelativePath("sub/deeper/b.dat") == kFolderPathOk);
    //中文名、空格、多个点号都是日常会遇到的合法文件名
    CHECK(CheckFolderRelativePath("项目资料/第一版 说明.docx") == kFolderPathOk);
    CHECK(CheckFolderRelativePath("archive.tar.gz") == kFolderPathOk);
    //名字里带点号但不是保留名
    CHECK(CheckFolderRelativePath("sub/.gitignore") == kFolderPathOk);
}

TEST_CASE("FMAN2 含 \"..\" 的路径一律拒绝")
{
    //这是目录穿越的典型写法：不拦住的话，对端能把文件写到接收方选定目录之外
    CHECK(CheckFolderRelativePath("..") == kFolderPathDotSegment);
    CHECK(CheckFolderRelativePath("../a.txt") == kFolderPathDotSegment);
    CHECK(CheckFolderRelativePath("sub/../../a.txt") == kFolderPathDotSegment);
    CHECK(CheckFolderRelativePath("sub/..") == kFolderPathDotSegment);
    //反斜杠写法同样要拦（清单里虽然统一用正斜杠，但校验不能假定对端遵守）
    CHECK(CheckFolderRelativePath("sub\\..\\a.txt") == kFolderPathDotSegment);
    //"..foo" 只是以点号开头的普通名字，不是上级引用，不能误伤
    CHECK(CheckFolderRelativePath("..foo/a.txt") == kFolderPathOk);
}

TEST_CASE("FMAN3 以分隔符开头的绝对路径一律拒绝")
{
    CHECK(CheckFolderRelativePath("/etc/passwd") == kFolderPathRooted);
    CHECK(CheckFolderRelativePath("\\Windows\\system32") == kFolderPathRooted);
    //UNC 路径也是以分隔符开头，同样在这里被挡下
    CHECK(CheckFolderRelativePath("\\\\server\\share\\a.txt") == kFolderPathRooted);
    //单个分隔符
    CHECK(CheckFolderRelativePath("/") == kFolderPathRooted);
}

TEST_CASE("FMAN4 带盘符前缀的路径一律拒绝")
{
    CHECK(CheckFolderRelativePath("C:\\Windows\\a.txt") == kFolderPathDrive);
    CHECK(CheckFolderRelativePath("C:/Windows/a.txt") == kFolderPathDrive);
    //"C:a.txt" 是「C 盘当前目录下的 a.txt」，同样不是相对本次传输根目录的路径
    CHECK(CheckFolderRelativePath("C:a.txt") == kFolderPathDrive);
    //小写盘符
    CHECK(CheckFolderRelativePath("d:\\data") == kFolderPathDrive);
    //盘符出现在中间时按非法字符处理（冒号本身就是 Windows 文件名非法字符）
    CHECK(CheckFolderRelativePath("sub/C:a.txt") == kFolderPathIllegalChar);
}

TEST_CASE("FMAN5 含 Windows 非法字符或控制字符的路径拒绝")
{
    CHECK(CheckFolderRelativePath("a<b.txt") == kFolderPathIllegalChar);
    CHECK(CheckFolderRelativePath("a>b.txt") == kFolderPathIllegalChar);
    CHECK(CheckFolderRelativePath("a\"b.txt") == kFolderPathIllegalChar);
    CHECK(CheckFolderRelativePath("a|b.txt") == kFolderPathIllegalChar);
    CHECK(CheckFolderRelativePath("a?b.txt") == kFolderPathIllegalChar);
    CHECK(CheckFolderRelativePath("a*b.txt") == kFolderPathIllegalChar);
    CHECK(CheckFolderRelativePath("sub/a:b.txt") == kFolderPathIllegalChar);

    //控制字符：换行与 0x01 这类正常文件名里不会出现，出现即是构造出来的
    std::string withNewline = "a";
    withNewline.push_back('\n');
    withNewline += "b.txt";
    CHECK(CheckFolderRelativePath(withNewline) == kFolderPathIllegalChar);

    std::string withSoh = "sub/a";
    withSoh.push_back(static_cast<char>(0x01));
    CHECK(CheckFolderRelativePath(withSoh) == kFolderPathIllegalChar);
}

TEST_CASE("FMAN6 Windows 保留设备名拒绝（含带扩展名的形式）")
{
    CHECK(CheckFolderRelativePath("CON") == kFolderPathReservedName);
    CHECK(CheckFolderRelativePath("PRN") == kFolderPathReservedName);
    CHECK(CheckFolderRelativePath("AUX") == kFolderPathReservedName);
    CHECK(CheckFolderRelativePath("NUL") == kFolderPathReservedName);
    CHECK(CheckFolderRelativePath("COM1") == kFolderPathReservedName);
    CHECK(CheckFolderRelativePath("COM9") == kFolderPathReservedName);
    CHECK(CheckFolderRelativePath("LPT1") == kFolderPathReservedName);
    CHECK(CheckFolderRelativePath("LPT9") == kFolderPathReservedName);

    //带扩展名的形式同样是设备（系统只看第一个点号之前那截）
    CHECK(CheckFolderRelativePath("CON.txt") == kFolderPathReservedName);
    CHECK(CheckFolderRelativePath("nul.dat") == kFolderPathReservedName);
    //大小写不敏感，且出现在任意一级都要拦
    CHECK(CheckFolderRelativePath("sub/con/a.txt") == kFolderPathReservedName);
    CHECK(CheckFolderRelativePath("sub/Com1.log") == kFolderPathReservedName);

    //相近但不是保留名的，不能误伤：编号 0 与两位编号都不是设备名，CONFIG 也不是
    CHECK(CheckFolderRelativePath("COM0") == kFolderPathOk);
    CHECK(CheckFolderRelativePath("COM10") == kFolderPathOk);
    CHECK(CheckFolderRelativePath("CONFIG") == kFolderPathOk);
    CHECK(CheckFolderRelativePath("console.log") == kFolderPathOk);
}

TEST_CASE("FMAN7 空路径拒绝")
{
    CHECK(CheckFolderRelativePath("") == kFolderPathEmpty);
    //只由分隔符组成的路径也定位不到任何落点，先被「以分隔符开头」拦下
    CHECK(CheckFolderRelativePath("//") == kFolderPathRooted);
}

TEST_CASE("FMAN8 单段超长与整条超长分别拒绝")
{
    //单段超过 kMaxFolderPathSegmentChars：文件系统建不出这个名字
    const std::string longSegment(kMaxFolderPathSegmentChars + 1, 'x');
    CHECK(CheckFolderRelativePath(longSegment) == kFolderPathSegmentTooLong);
    CHECK(CheckFolderRelativePath("sub/" + longSegment) == kFolderPathSegmentTooLong);
    //正好等于上限的一段仍然放行（边界内侧）
    const std::string maxSegment(kMaxFolderPathSegmentChars, 'x');
    CHECK(CheckFolderRelativePath(maxSegment) == kFolderPathOk);

    //整条超过 kMaxFolderRelativePathChars：每段都合法，但拼起来太长
    std::string longPath;
    while (longPath.size() <= kMaxFolderRelativePathChars)
    {
        longPath += "abcdefgh";
        longPath += "/";
    }
    longPath += "a.txt";
    CHECK(CheckFolderRelativePath(longPath) == kFolderPathTooLong);
}

TEST_CASE("FMAN9 \".\" 段拒绝")
{
    //"." 段不会造成穿越，但它是无意义的冗余写法，放行会让同一个文件出现两种清单写法
    CHECK(CheckFolderRelativePath(".") == kFolderPathDotSegment);
    CHECK(CheckFolderRelativePath("./a.txt") == kFolderPathDotSegment);
    CHECK(CheckFolderRelativePath("sub/./a.txt") == kFolderPathDotSegment);
    //".hidden" 与 "a." 都是普通名字，不能误伤
    CHECK(CheckFolderRelativePath(".hidden") == kFolderPathOk);
}

TEST_CASE("FMAN10 布尔封装与判定文字与判定函数口径一致")
{
    CHECK(IsSafeFolderRelativePath("sub/a.txt"));
    CHECK(IsSafeFolderRelativePath("../a.txt") == false);
    CHECK(IsSafeFolderRelativePath("") == false);

    //判定文字只供日志，但不能是空指针 —— 日志里打出空串会让排查无从下手
    const FolderPathVerdict kAllVerdicts[] =
    {
        kFolderPathOk, kFolderPathEmpty, kFolderPathDotSegment, kFolderPathRooted,
        kFolderPathDrive, kFolderPathIllegalChar, kFolderPathReservedName,
        kFolderPathSegmentTooLong, kFolderPathTooLong
    };

    for (size_t i = 0; i < sizeof(kAllVerdicts) / sizeof(kAllVerdicts[0]); ++i)
    {
        const char* const text = FolderPathVerdictText(kAllVerdicts[i]);
        INFO("verdict = " << static_cast<int>(kAllVerdicts[i]));
        REQUIRE(text != NULL);
        CHECK(text[0] != '\0');
    }
}

// ==================== 纯逻辑辅助 ====================

TEST_CASE("FMAN11 相对路径规范化：反斜杠转正斜杠、重复与首尾分隔符去掉")
{
    CHECK(NormalizeFolderRelativePath("sub\\a.txt") == "sub/a.txt");
    CHECK(NormalizeFolderRelativePath("sub\\deeper\\b.dat") == "sub/deeper/b.dat");
    //连续分隔符合并成一个
    CHECK(NormalizeFolderRelativePath("sub//a.txt") == "sub/a.txt");
    CHECK(NormalizeFolderRelativePath("sub\\\\/a.txt") == "sub/a.txt");
    //首尾的分隔符去掉
    CHECK(NormalizeFolderRelativePath("/sub/a.txt") == "sub/a.txt");
    CHECK(NormalizeFolderRelativePath("sub/a.txt/") == "sub/a.txt");
    CHECK(NormalizeFolderRelativePath("\\sub\\") == "sub");
    //空串与纯分隔符归到空串
    CHECK(NormalizeFolderRelativePath("").empty());
    CHECK(NormalizeFolderRelativePath("///").empty());
}

TEST_CASE("FMAN12 规范化不改大小写、不解析 \".\" 与 \"..\"")
{
    //Windows 文件名大小写不敏感但保留大小写，改了会让接收端建出的名字与发送端不一致
    CHECK(NormalizeFolderRelativePath("Sub\\ReadMe.TXT") == "Sub/ReadMe.TXT");
    CHECK(NormalizeFolderRelativePath("项目 A/说明.MD") == "项目 A/说明.MD");
    //".." 必须原样留着交给校验函数去拒绝，在这里悄悄消解掉会掩盖对端的恶意路径
    CHECK(NormalizeFolderRelativePath("sub\\..\\a.txt") == "sub/../a.txt");
    CHECK(NormalizeFolderRelativePath("./a.txt") == "./a.txt");
}

TEST_CASE("FMAN13 UTF-16 与 UTF-8 互转往返一致")
{
    std::string utf8;
    REQUIRE(Utf16ToUtf8(L"report.txt", utf8));
    CHECK(utf8 == "report.txt");

    //中文名：本模块自己实现转换，必须与源文件里的 UTF-8 字面量逐字节相同
    REQUIRE(Utf16ToUtf8(L"项目资料", utf8));
    CHECK(utf8 == "项目资料");

    std::wstring wide;
    REQUIRE(Utf8ToUtf16("项目资料/说明.txt", wide));
    CHECK(wide == L"项目资料/说明.txt");

    //辅助平面字符（U+1F600）在 UTF-16 里是一对代理项、在 UTF-8 里占 4 个字节，
    //这一对不能被拆错，否则文件名会变成两个问号
    std::wstring supplementary;
    supplementary.push_back(static_cast<wchar_t>(0xD83D));
    supplementary.push_back(static_cast<wchar_t>(0xDE00));
    REQUIRE(Utf16ToUtf8(supplementary, utf8));
    REQUIRE(utf8.size() == 4);
    CHECK(static_cast<unsigned char>(utf8[0]) == 0xF0);
    CHECK(static_cast<unsigned char>(utf8[1]) == 0x9F);
    CHECK(static_cast<unsigned char>(utf8[2]) == 0x98);
    CHECK(static_cast<unsigned char>(utf8[3]) == 0x80);

    REQUIRE(Utf8ToUtf16(utf8, wide));
    CHECK(wide == supplementary);

    //空串转出空串，不返回失败
    REQUIRE(Utf16ToUtf8(L"", utf8));
    CHECK(utf8.empty());
    REQUIRE(Utf8ToUtf16("", wide));
    CHECK(wide.empty());

    //落单的高位代理项是坏数据：函数须报失败，而不是悄悄产出乱码
    std::wstring lonelySurrogate;
    lonelySurrogate.push_back(static_cast<wchar_t>(0xD83D));
    CHECK(Utf16ToUtf8(lonelySurrogate, utf8) == false);
}

TEST_CASE("FMAN14 ExtractFolderName 取末段，盘符根目录取不出名字")
{
    CHECK(ExtractFolderName(L"D:\\work\\报告") == L"报告");
    //末尾带分隔符（文件夹选择对话框有时会返回这种）结果相同
    CHECK(ExtractFolderName(L"D:\\work\\报告\\") == L"报告");
    CHECK(ExtractFolderName(L"D:/work/报告") == L"报告");
    //整个盘不能当成一个「文件夹」发出去，这里要返回空串让扫描判参数非法
    CHECK(ExtractFolderName(L"D:\\").empty());
    CHECK(ExtractFolderName(L"D:").empty());
    CHECK(ExtractFolderName(L"").empty());
}

TEST_CASE("FMAN15 MakeLongPath 加前缀、幂等、UNC 与正斜杠处理")
{
    CHECK(MakeLongPath(L"D:\\work\\报告") == L"\\\\?\\D:\\work\\报告");
    //正斜杠必须换成反斜杠 —— 带前缀的路径不再经系统规范化，留着正斜杠会打不开
    CHECK(MakeLongPath(L"D:/work/报告") == L"\\\\?\\D:\\work\\报告");
    //已经带前缀的原样返回（幂等），否则重复调用会拼出两层前缀
    CHECK(MakeLongPath(L"\\\\?\\D:\\work") == L"\\\\?\\D:\\work");
    //UNC 路径有专门的写法
    CHECK(MakeLongPath(L"\\\\server\\share\\a.txt") == L"\\\\?\\UNC\\server\\share\\a.txt");
    //相对路径拼不出前缀，原样返回（只做分隔符统一）
    CHECK(MakeLongPath(L"sub/a.txt") == L"sub\\a.txt");
    CHECK(MakeLongPath(L"").empty());
}

TEST_CASE("FMAN16 ComputeFolderManifestStats 统计目录数 / 文件数 / 总字节数")
{
    FolderManifest manifest;
    //空清单三项皆 0（选中一个空文件夹时就是这种）
    FolderManifestStats stats = ComputeFolderManifestStats(manifest);
    CHECK(stats.m_dirCount == 0);
    CHECK(stats.m_fileCount == 0);
    CHECK(stats.m_totalBytes == 0);

    manifest.m_folderName = "报告";
    manifest.m_dirs.push_back("sub");
    manifest.m_dirs.push_back("sub/deeper");

    FolderManifestFile fileA;
    fileA.m_relativePath = "a.txt";
    fileA.m_sizeBytes = 3;
    manifest.m_files.push_back(fileA);

    //零字节文件照样算一个文件，只是不贡献字节数
    FolderManifestFile fileB;
    fileB.m_relativePath = "sub/empty.bin";
    fileB.m_sizeBytes = 0;
    manifest.m_files.push_back(fileB);

    //单个文件超过 4 GiB 时高位不能被截断，这里用一个跨过 32 位边界的值锁住
    FolderManifestFile fileC;
    fileC.m_relativePath = "sub/deeper/big.iso";
    fileC.m_sizeBytes = 5000000000LL;
    manifest.m_files.push_back(fileC);

    stats = ComputeFolderManifestStats(manifest);
    CHECK(stats.m_dirCount == 2);
    CHECK(stats.m_fileCount == 3);
    CHECK(stats.m_totalBytes == 5000000003LL);
}

TEST_CASE("FMAN17 CheckFolderManifest 报出第一条不合法路径")
{
    FolderManifest manifest;
    manifest.m_folderName = "报告";
    manifest.m_dirs.push_back("sub");

    FolderManifestFile good;
    good.m_relativePath = "sub/a.txt";
    good.m_sizeBytes = 1;
    manifest.m_files.push_back(good);

    std::string badPath = "残值";
    CHECK(CheckFolderManifest(manifest, badPath) == kFolderPathOk);
    //全部合法时出参要被清空，免得调用方拿着上一次的残值去写日志
    CHECK(badPath.empty());

    //目录清单里混进一条穿越路径：接收端建目录前就要把整个传输判失败
    manifest.m_dirs.push_back("../外面");
    CHECK(CheckFolderManifest(manifest, badPath) == kFolderPathDotSegment);
    CHECK(badPath == "../外面");

    //目录都合法时，文件清单里的非法路径同样要报出来
    manifest.m_dirs.pop_back();
    FolderManifestFile bad;
    bad.m_relativePath = "C:\\Windows\\evil.dll";
    bad.m_sizeBytes = 1;
    manifest.m_files.push_back(bad);
    CHECK(CheckFolderManifest(manifest, badPath) == kFolderPathDrive);
    CHECK(badPath == "C:\\Windows\\evil.dll");
}

// ==================== 目录扫描 ====================

TEST_CASE("FMAN18 扫描含嵌套子目录 / 空目录 / 中文名 / 零字节文件的树")
{
    const std::wstring base = MakeTempRootPath(L"tree");
    const std::wstring root = base + L"\\发送测试";

    //先清掉可能残留的上一次运行结果，再造这一次的树
    RemoveTree(base);
    REQUIRE(MakeDir(base));
    REQUIRE(MakeDir(root));
    REQUIRE(MakeDir(root + L"\\子目录1"));
    REQUIRE(MakeDir(root + L"\\子目录1\\深层"));
    //空目录也要进清单：接收端得把它建出来，否则对方看到的目录结构就缺了一块
    REQUIRE(MakeDir(root + L"\\空目录"));

    REQUIRE(WriteBytes(root + L"\\a.txt", "abc", 3));
    //零字节文件是常见边界：不能因为没有字节要传就把它从清单里漏掉
    REQUIRE(WriteBytes(root + L"\\空文件.bin", NULL, 0));
    REQUIRE(WriteBytes(root + L"\\子目录1\\b.dat", "12345", 5));
    REQUIRE(WriteBytes(root + L"\\子目录1\\深层\\c.txt", "xy", 2));

    FolderScanResult result;
    const bool ok = ScanFolderTree(root, result);

    INFO("scan error = " << result.m_error << ", win32 = " << result.m_win32Error
         << ", path = " << result.m_errorPath);
    REQUIRE(ok);
    CHECK(result.m_error == kFolderScanOk);

    //根目录自身不进目录清单，它的名字单独给出
    CHECK(result.m_manifest.m_folderName == "发送测试");

    CHECK(result.m_stats.m_dirCount == 3);
    CHECK(result.m_stats.m_fileCount == 4);
    CHECK(result.m_stats.m_totalBytes == 10);
    //汇总值必须与清单本身算出来的一致（上层拿它去和服务端的上限比）
    const FolderManifestStats recomputed = ComputeFolderManifestStats(result.m_manifest);
    CHECK(recomputed.m_dirCount == result.m_stats.m_dirCount);
    CHECK(recomputed.m_fileCount == result.m_stats.m_fileCount);
    CHECK(recomputed.m_totalBytes == result.m_stats.m_totalBytes);

    REQUIRE(result.m_manifest.m_dirs.size() == 3);
    const ptrdiff_t idxSub = IndexOfDir(result.m_manifest.m_dirs, "子目录1");
    const ptrdiff_t idxDeep = IndexOfDir(result.m_manifest.m_dirs, "子目录1/深层");
    CHECK(idxSub >= 0);
    CHECK(idxDeep >= 0);
    CHECK(IndexOfDir(result.m_manifest.m_dirs, "空目录") >= 0);
    //父目录必须排在子目录之前，接收端才能照着顺序一级级建下去
    CHECK(idxSub < idxDeep);

    REQUIRE(result.m_manifest.m_files.size() == 4);
    const ptrdiff_t idxA = IndexOfFile(result.m_manifest.m_files, "a.txt");
    const ptrdiff_t idxEmpty = IndexOfFile(result.m_manifest.m_files, "空文件.bin");
    const ptrdiff_t idxB = IndexOfFile(result.m_manifest.m_files, "子目录1/b.dat");
    const ptrdiff_t idxC = IndexOfFile(result.m_manifest.m_files, "子目录1/深层/c.txt");
    REQUIRE(idxA >= 0);
    REQUIRE(idxEmpty >= 0);
    REQUIRE(idxB >= 0);
    REQUIRE(idxC >= 0);
    CHECK(result.m_manifest.m_files[static_cast<size_t>(idxA)].m_sizeBytes == 3);
    CHECK(result.m_manifest.m_files[static_cast<size_t>(idxEmpty)].m_sizeBytes == 0);
    CHECK(result.m_manifest.m_files[static_cast<size_t>(idxB)].m_sizeBytes == 5);
    CHECK(result.m_manifest.m_files[static_cast<size_t>(idxC)].m_sizeBytes == 2);

    //清单里的每一条都得能过安全校验，否则接收端会整体判失败
    std::string badPath;
    CHECK(CheckFolderManifest(result.m_manifest, badPath) == kFolderPathOk);
    CHECK(badPath.empty());

    //没有符号链接，跳过清单应当是空的
    CHECK(result.m_manifest.m_skipped.empty());

    RemoveTree(base);
}

TEST_CASE("FMAN19 扫描根路径异常：不存在 / 不是目录 / 空串 / 盘符根目录")
{
    const std::wstring base = MakeTempRootPath(L"root");
    RemoveTree(base);
    REQUIRE(MakeDir(base));

    //不存在的目录
    FolderScanResult missing;
    CHECK(ScanFolderTree(base + L"\\没有这个目录", missing) == false);
    CHECK(missing.m_error == kFolderScanRootMissing);
    CHECK(missing.m_manifest.m_files.empty());
    CHECK(missing.m_stats.m_fileCount == 0);

    //传进来的是文件而不是目录
    const std::wstring filePath = base + L"\\plain.txt";
    REQUIRE(WriteBytes(filePath, "abc", 3));
    FolderScanResult notDir;
    CHECK(ScanFolderTree(filePath, notDir) == false);
    CHECK(notDir.m_error == kFolderScanRootMissing);

    //空串
    FolderScanResult empty;
    CHECK(ScanFolderTree(L"", empty) == false);
    CHECK(empty.m_error == kFolderScanBadArgument);

    //盘符根目录取不出文件夹名：整个盘不能当成一个文件夹发出去
    FolderScanResult driveRoot;
    CHECK(ScanFolderTree(L"D:\\", driveRoot) == false);
    CHECK(driveRoot.m_error == kFolderScanBadArgument);

    RemoveTree(base);
}

TEST_CASE("FMAN20 扫描层级超过上限判失败")
{
    const std::wstring base = MakeTempRootPath(L"depth");
    const std::wstring root = base + L"\\deep";

    RemoveTree(base);
    REQUIRE(MakeDir(base));
    REQUIRE(MakeDir(root));
    REQUIRE(MakeDir(root + L"\\lv1"));
    REQUIRE(MakeDir(root + L"\\lv1\\lv2"));

    //限一层：lv1 还在允许范围内，它下面的 lv2 就越界了
    FolderScanOptions options;
    options.m_maxDepth = 1;

    FolderScanResult result;
    CHECK(ScanFolderTree(root, options, result) == false);
    CHECK(result.m_error == kFolderScanTooDeep);
    //失败时要指出是哪一条越界，便于日志与提示定位
    CHECK(result.m_errorPath == "lv1/lv2");
    //整次扫描判失败，清单不留半成品
    CHECK(result.m_manifest.m_dirs.empty());
    CHECK(result.m_manifest.m_files.empty());

    //放开到两层就能扫完
    options.m_maxDepth = 2;
    FolderScanResult ok;
    REQUIRE(ScanFolderTree(root, options, ok));
    CHECK(ok.m_stats.m_dirCount == 2);

    RemoveTree(base);
}

TEST_CASE("FMAN21 扫描总长超过 260 的长路径")
{
    const std::wstring base = MakeTempRootPath(L"long");
    const std::wstring root = base + L"\\longpath";

    //每级 90 个字符，三级下来连同临时目录前缀肯定越过 260 —— 不加 "\\?\" 前缀的话
    //FindFirstFileW 在这里就会失败，本用例正是为了守住这一点
    const size_t kLevelNameChars = 90;
    const std::wstring levelName(kLevelNameChars, L'n');

    RemoveTree(base);
    REQUIRE(MakeDir(base));
    REQUIRE(MakeDir(root));

    std::wstring current = root;
    std::string expectedRelative;
    for (int i = 0; i < 3; ++i)
    {
        current += L"\\";
        current += levelName;
        REQUIRE(MakeDir(current));

        if (!expectedRelative.empty())
            expectedRelative += "/";
        expectedRelative += std::string(kLevelNameChars, 'n');
    }

    const std::wstring deepFile = current + L"\\deep.txt";
    REQUIRE(WriteBytes(deepFile, "1234", 4));
    //确认这棵树确实越过了传统 260 上限，否则这条用例就白测了
    REQUIRE(deepFile.size() > 260);

    FolderScanResult result;
    const bool ok = ScanFolderTree(root, result);

    INFO("scan error = " << result.m_error << ", win32 = " << result.m_win32Error
         << ", path = " << result.m_errorPath);
    REQUIRE(ok);
    CHECK(result.m_stats.m_dirCount == 3);
    CHECK(result.m_stats.m_fileCount == 1);
    CHECK(result.m_stats.m_totalBytes == 4);

    const std::string expectedFile = expectedRelative + "/deep.txt";
    CHECK(IndexOfFile(result.m_manifest.m_files, expectedFile) >= 0);

    RemoveTree(base);
}
