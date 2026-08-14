/**
 *  FolderManifest.h
 *
 *  单聊「发送文件夹」的本地目录清单：目录树扫描、相对路径规范化与安全校验、规模统计。
 *
 *  发送方选中一个目录后，第一步就是把这棵树扫成一份清单：所有子目录的相对路径
 *  （<u>含空目录</u>，空目录也要在接收端建出来）、所有文件的相对路径与字节数，以及
 *  目录数 / 文件数 / 总字节数三个汇总值。汇总值随 cmd_folder_invite 上报，服务端据
 *  配置项 foldermaxfiles / foldermaxsize 校验规模上限（客户端不硬编码上限，只负责
 *  给出准确的统计值）；清单本身随控制通道的 ft_manifest 帧发给接收端。
 *
 *  本文件分成两层，界线清楚：
 *    · 纯逻辑层 —— 相对路径规范化 CheckFolderRelativePath / NormalizeFolderRelativePath、
 *      UTF-16 与 UTF-8 互转、根目录名提取、长路径前缀拼接、统计汇总。不碰文件系统、
 *      不依赖 Win32，可脱离磁盘单测（见 Source/Tests/FolderManifestTests.cpp，Suite FMAN）。
 *    · 目录遍历层 —— ScanFolderTree 一个入口，只有它调 FindFirstFileW / FindNextFileW。
 *
 *  路径安全校验（CheckFolderRelativePath）是防目录穿越的关键，<u>收发两端用同一份判断</u>：
 *  发送端生成清单时逐条校验，接收端建目录前再校验一遍。否则对端可以借一条含 ".." 或
 *  盘符的相对路径，把文件写到接收方选定目录之外（见 docs/单聊文件夹传输方案.md 第 9 节）。
 *
 *  本模块被 FlamingoUnit 测试工程直接编译，故不依赖 MFC / ATL / WTL，也不引入客户端的
 *  预编译头；两个工程文件里都须登记为 PrecompiledHeader=NotUsing。
 *
 *  典型用法（发送端）：
 *      foldertransfer::FolderScanResult result;
 *      if (!foldertransfer::ScanFolderTree(L"D:\\work\\报告", result))
 *      {
 *          // result.m_error 说明失败类型，result.m_errorPath 指出出错的相对路径
 *      }
 *      else
 *      {
 *          // result.m_stats.m_fileCount / m_totalBytes 随邀请上报
 *          // result.m_manifest 随 ft_manifest 帧发给接收端
 *      }
 *
 *  balloonwj@qq.com   2026-08-13
 */

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace foldertransfer {

// ======================== 第一部分：纯逻辑（不依赖 Win32） ========================

//清单里相对路径统一使用的分隔符。本地是反斜杠，进清单前一律换成正斜杠，接收端
//落地时再换回去 —— 这样清单在协议上只有一种写法，两端比对与去重都不必考虑变体。
const char kFolderPathSeparator = '/';

//单个路径段（一级目录名或文件名）允许的最大字符数。出处：NTFS 单个路径组件上限为
//255 个 UTF-16 单元。本模块按 UTF-8 串里的码点个数近似计数（基本平面字符一个码点
//正好一个 UTF-16 单元；辅助平面字符会少算一个，那种名字实际落地失败时按建目录 /
//建文件失败上报，不影响安全性）。
const size_t kMaxFolderPathSegmentChars = 255;

//整条相对路径允许的最大字符数（同样按码点计）。出处：加 "\\?\" 前缀后 Windows 路径
//上限为 32767 个 UTF-16 单元，这里取 4000 是给接收端选定的落地根目录留出充足余量，
//同时挡住畸形超长路径 —— 正常业务目录远达不到这个量级。
const size_t kMaxFolderRelativePathChars = 4000;

//Windows 长路径前缀。不带它时路径受 MAX_PATH（260 字符）限制，带上它 Win32 文件 API
//才肯受理更长的路径。注意带该前缀的路径不再经系统规范化，故拼接前须先把正斜杠换成反斜杠。
extern const wchar_t* const kWindowsLongPathPrefix;

/**
 *  相对路径安全校验的判定结果。
 *  除 kFolderPathOk 外的每一种都意味着这条路径不可在接收端落地，整个文件夹传输判失败。
 */
enum FolderPathVerdict
{
    kFolderPathOk             = 0,   // 通过校验：可安全地在接收端选定目录下落地
    kFolderPathEmpty          = 1,   // 空路径（空串，或只由分隔符组成），无法定位落点
    kFolderPathDotSegment     = 2,   // 存在 "." 或 ".." 路径段：".." 可穿越到选定目录之外，"." 是无意义的冗余段
    kFolderPathRooted         = 3,   // 以 '/' 或 '\' 开头：这是绝对路径（含 UNC 的 "\\server\share"），会落到选定目录之外
    kFolderPathDrive          = 4,   // 带盘符前缀（形如 "C:" 开头）：同样会落到选定目录之外
    kFolderPathIllegalChar    = 5,   // 含 Windows 文件名非法字符（<>:"|?* 与 0x00~0x1F、0x7F 控制字符）
    kFolderPathReservedName   = 6,   // 某一段是 Windows 保留设备名（CON / PRN / AUX / NUL / COM1~COM9 / LPT1~LPT9，含 "CON.txt" 这类带扩展名的形式）
    kFolderPathSegmentTooLong = 7,   // 某一段超过 kMaxFolderPathSegmentChars 个字符，文件系统建不出来
    kFolderPathTooLong        = 8,   // 整条路径超过 kMaxFolderRelativePathChars 个字符
};

/**
 *  把一条相对路径规范化成清单里的写法。
 *  做三件事：反斜杠一律换成正斜杠、连续的多个分隔符合并成一个、去掉首尾多余的分隔符。
 *  <u>不</u>做的事：不解析 "." 与 ".."（那两种一律由 CheckFolderRelativePath 判为非法，
 *  在这里悄悄消解掉反而会掩盖对端的恶意路径），不改大小写（Windows 文件名大小写不敏感
 *  但保留大小写，改了会让接收端建出的名字与发送端不一致）。
 *  @param raw 原始相对路径（UTF-8）。允许含反斜杠、重复分隔符、首尾分隔符
 *  @return 规范化后的相对路径（UTF-8，正斜杠分隔，不以分隔符开头或结尾）
 */
std::string NormalizeFolderRelativePath(const std::string& raw);

/**
 *  校验一条相对路径能否安全地在接收端落地。纯函数，收发两端共用同一份判断。
 *  校验顺序为「整体长度 → 是否绝对路径 / 带盘符 → 逐段判定」，先判整体是为了让
 *  一条既超长又带 ".." 的路径给出稳定的结论，便于按判定值写日志与提示。
 *  @param relativePath 待校验的相对路径（UTF-8），可以是未规范化的原始写法
 *  @return 判定结果，见 FolderPathVerdict；kFolderPathOk 表示可安全落地
 */
FolderPathVerdict CheckFolderRelativePath(const std::string& relativePath);

/**
 *  CheckFolderRelativePath 的布尔封装，供只关心「行不行」的调用点使用。
 *  @param relativePath 待校验的相对路径（UTF-8）
 *  @return 可安全落地返回 true
 */
bool IsSafeFolderRelativePath(const std::string& relativePath);

/**
 *  取判定结果对应的英文短语，仅供写日志（日志一律英文，界面提示文案由 UI 层另行给出）。
 *  @param verdict 判定结果
 *  @return 指向静态只读字符串的指针，调用方<u>不得</u>释放，也不必拷贝；未知取值返回 "unknown"
 */
const char* FolderPathVerdictText(FolderPathVerdict verdict);

/**
 *  UTF-16（Windows 宽字符）转 UTF-8。自行实现而不用 WideCharToMultiByte，是为了让
 *  纯逻辑层完全不依赖 Win32，测试里可直接调。
 *  @param in  待转换的宽字符串，允许含代理对（辅助平面字符）
 *  @param out [出参] 转换结果（UTF-8）；函数开头即清空它，失败时为已转换出的部分
 *  @return 全部转换成功返回 true；遇到不成对的代理项返回 false（此时 out 中该位置写入
 *          替换字符 U+FFFD，调用方可按失败处理，也可拿部分结果写日志）
 */
bool Utf16ToUtf8(const std::wstring& in, std::string& out);

/**
 *  UTF-8 转 UTF-16（Windows 宽字符）。接收端把清单里的相对路径还原成本地路径时用。
 *  @param in  待转换的 UTF-8 串
 *  @param out [出参] 转换结果（宽字符）；函数开头即清空它，失败时为已转换出的部分
 *  @return 全部转换成功返回 true；遇到非法 UTF-8 字节序列返回 false（该位置写入
 *          替换字符 U+FFFD）
 */
bool Utf8ToUtf16(const std::string& in, std::wstring& out);

/**
 *  从一条目录路径里取出目录自身的名字（即清单里的「文件夹名」）。
 *  会先去掉末尾的分隔符，再取最后一段，故 "D:\work\报告\" 与 "D:\work\报告" 结果相同。
 *  @param folderPath 目录的完整路径（宽字符）
 *  @return 目录名（宽字符）。盘符根目录（"D:\"）、空串这类取不出名字的输入返回空串，
 *          调用方须据此判失败 —— 整个盘不能当成一个「文件夹」发出去
 */
std::wstring ExtractFolderName(const std::wstring& folderPath);

/**
 *  给一条绝对路径加上长路径前缀 "\\?\"，使超过 260 字符的路径也能被 Win32 文件 API 受理。
 *  同时把路径里的正斜杠换成反斜杠 —— 带该前缀的路径不再经系统规范化，留着正斜杠会打不开。
 *  @param absolutePath 绝对路径（宽字符）。须是已规范化的绝对路径，不含 "." / ".." 段；
 *                      传相对路径时本函数原样返回（无法为它拼出前缀）
 *  @return 加好前缀的路径。已带前缀的原样返回；UNC 路径（"\\server\share"）转成
 *          "\\?\UNC\server\share"；空串返回空串
 */
std::wstring MakeLongPath(const std::wstring& absolutePath);

/**
 *  清单里的一个文件条目。
 */
struct FolderManifestFile
{
    std::string m_relativePath;   // 相对根目录的路径（UTF-8，正斜杠分隔，不以分隔符开头），例如 "sub/a.txt"
    int64_t     m_sizeBytes;      // 文件字节数（0 字节文件合法，照样进清单）

    FolderManifestFile()
        : m_sizeBytes(0)
    {
    }
};

/**
 *  扫描时跳过某一项的原因。
 */
enum FolderSkipReason
{
    kFolderSkipReparsePoint = 1,   // 重解析点（符号链接 / 目录联接 / 装载点）：可能指回上级形成环，一律不跟进
};

/**
 *  被跳过的一项，留在清单里是为了能如实告诉用户「哪些没发出去、为什么」。
 */
struct FolderManifestSkipped
{
    std::string m_relativePath;   // 被跳过项相对根目录的路径（UTF-8，正斜杠分隔）
    int         m_reason;         // 跳过原因，取值见 FolderSkipReason
    bool        m_isDirectory;    // true=被跳过的是目录，false=文件（提示文案要区分这两种）

    FolderManifestSkipped()
        : m_reason(0)
        , m_isDirectory(false)
    {
    }
};

/**
 *  一次目录扫描产出的完整清单，也就是控制通道 ft_manifest 帧的内容来源。
 *
 *  根目录自身<u>不</u>在 m_dirs 里，它的名字单独放在 m_folderName —— 接收端要先按这个
 *  名字在选定位置建根目录（重名时自动改名），再按 m_dirs 逐级建子目录。
 *  m_dirs 与 m_files 都按相对路径升序排列，父目录必定排在其子目录之前，接收端顺序
 *  建目录即可，不必自己排序。
 */
struct FolderManifest
{
    std::string                        m_folderName;   // 根目录自身的名字（UTF-8），不含任何路径部分
    std::vector<std::string>           m_dirs;         // 全部子目录的相对路径（UTF-8，升序，<u>含空目录</u>），不含根目录自身
    std::vector<FolderManifestFile>    m_files;        // 全部文件（升序）
    std::vector<FolderManifestSkipped> m_skipped;      // 扫描时跳过的项（重解析点等），可为空
};

/**
 *  清单的规模汇总，供上层做上限判断与界面显示。
 *  服务端才是规模上限的权威，本结构只负责给出准确的统计值。
 */
struct FolderManifestStats
{
    int64_t m_dirCount;     // 子目录个数（不含根目录自身）
    int64_t m_fileCount;    // 文件个数（含 0 字节文件）
    int64_t m_totalBytes;   // 全部文件的字节数之和

    FolderManifestStats()
        : m_dirCount(0)
        , m_fileCount(0)
        , m_totalBytes(0)
    {
    }
};

/**
 *  按清单算出规模汇总。纯函数，扫描层与接收端复核都用它，避免两处各算一份。
 *  @param manifest 已生成的清单
 *  @return 汇总值；空清单返回三项皆 0
 */
FolderManifestStats ComputeFolderManifestStats(const FolderManifest& manifest);

/**
 *  逐条校验整份清单里的相对路径。接收端在建目录之前必须调一次 —— 清单是对端发来的，
 *  不能假定它已经在发送端做过校验。
 *  @param manifest 待校验的清单（一般是从 ft_manifest 帧解出来的）
 *  @param badPath  [出参] 第一条不合法路径（UTF-8）；全部合法时被清空
 *  @return 第一条不合法路径的判定结果；全部合法返回 kFolderPathOk
 */
FolderPathVerdict CheckFolderManifest(const FolderManifest& manifest, std::string& badPath);

// ======================== 第二部分：目录遍历（Win32） ========================

/**
 *  目录扫描的失败类型。
 *  需求是「任一错误即整个文件夹失败」，故除 kFolderScanOk 外一律表示本次扫描整体失败，
 *  不产出可用清单；失败位置在 FolderScanResult::m_errorPath 里给出。
 */
enum FolderScanError
{
    kFolderScanOk           = 0,   // 扫描成功，清单可用
    kFolderScanBadArgument  = 1,   // 参数非法：根路径为空，或取不出目录名（如直接选了整个盘符根目录）
    kFolderScanRootMissing  = 2,   // 根路径不存在，或它不是一个目录
    kFolderScanAccessDenied = 3,   // 某个目录无权限枚举（含根目录），m_errorPath 指出是哪个
    kFolderScanEnumFailed   = 4,   // 枚举过程中遇到其它系统错误（磁盘 I/O 错误、目录正被删除等）
    kFolderScanUnsafePath   = 5,   // 生成出的相对路径未通过 CheckFolderRelativePath（超长、保留名等）
    kFolderScanTooDeep      = 6,   // 目录层级超过 FolderScanOptions::m_maxDepth，按异常结构处理
};

/**
 *  扫描选项。默认构造即为生产环境要用的取值，一般不必改；单元测试会调小 m_maxDepth 之类。
 */
struct FolderScanOptions
{
    //允许递归进入的最大层数（根目录下第一层子目录记作 1）。超过即判 kFolderScanTooDeep。
    //取 64 的依据：Windows 单条路径受 32767 字符限制，正常业务目录远达不到 64 层；
    //设这道闸是为了让异常结构（含环的目录、被恶意构造的深树）尽早失败而不是扫到天荒地老。
    int  m_maxDepth;

    //是否跳过重解析点（符号链接 / 目录联接 / 装载点）。默认 true：跟进去可能绕回上级
    //形成无限循环，也可能把选定目录之外的内容一并发出去。跳过的项记进 m_skipped。
    bool m_skipReparsePoints;

    FolderScanOptions();
};

/**
 *  一次扫描的完整结果：清单、汇总，以及失败时的定位信息。
 */
struct FolderScanResult
{
    int                 m_error;        // 失败类型，取值见 FolderScanError；kFolderScanOk 表示成功
    unsigned long       m_win32Error;   // 失败时的 GetLastError() 原值（供日志定位），成功时为 0
    std::string         m_errorPath;    // 出错处相对根目录的路径（UTF-8）；根目录自身出错或参数非法时为空串
    FolderManifest      m_manifest;     // 扫描出的清单，仅 m_error == kFolderScanOk 时有效
    FolderManifestStats m_stats;        // 清单的规模汇总，仅 m_error == kFolderScanOk 时有效

    FolderScanResult()
        : m_error(kFolderScanOk)
        , m_win32Error(0)
    {
    }
};

/**
 *  扫描一棵本地目录树，产出清单与规模汇总。这是本模块唯一碰文件系统的入口。
 *
 *  实现要点：内部按广度优先遍历（父目录必定先于子目录被处理，接收端顺序建目录即可）；
 *  所有路径都加长路径前缀后再交给 Win32 文件 API，故可处理超过 260 字符的路径；
 *  遇到任何一处出错（无权限、枚举失败、路径不安全、层级过深）立即中止并整体判失败，
 *  与「任一错误即整个文件夹失败」的需求一致。
 *
 *  @param rootPath 要发送的目录的完整路径（宽字符），须是绝对路径。允许带或不带末尾
 *                  分隔符；不接受盘符根目录（取不出文件夹名，判 kFolderScanBadArgument）
 *  @param options  扫描选项，含义见 FolderScanOptions
 *  @param out      [出参] 扫描结果。函数开头即整体重置，不会留上一次调用的残值；
 *                  失败时 m_error / m_win32Error / m_errorPath 有效，清单与汇总为空
 *  @return 扫描成功返回 true（等价于 out.m_error == kFolderScanOk）；失败返回 false
 *
 *  副作用：只读文件系统，不创建 / 修改 / 删除任何文件；耗时随目录规模线性增长，
 *  上万个文件的目录可能耗时数秒，故须在工作线程上调用，不要在 UI 线程直接调。
 */
bool ScanFolderTree(const std::wstring& rootPath, const FolderScanOptions& options, FolderScanResult& out);

/**
 *  ScanFolderTree 的便捷重载，用默认扫描选项。
 *  @param rootPath 要发送的目录的完整路径（宽字符）
 *  @param out      [出参] 扫描结果，含义同上
 *  @return 扫描成功返回 true
 */
bool ScanFolderTree(const std::wstring& rootPath, FolderScanResult& out);

} // namespace foldertransfer
