/**
 *  FlamingoMedia.dll 源文件依赖守卫（Suite MDG，标签 [mediadll]），MediaDllSourceGuardTests.cpp
 *
 *  编进 FlamingoMedia.dll 的源文件（包括与 exe 共用的公共代码）不能依赖 exe 专属的东西：
 *  主连接 CIUSocket、会话全局数据 UserSessionData.h、网络客户端 FlamingoClient.h、主面板目录
 *  UI/BalloonMain/。DLL 里没有这些模块，这样的依赖要么编不过，要么在 DLL 里悄悄链接到一份
 *  不该存在的副本。本用例在运行时读取「可编入 DLL 清单」里的每个源文件，去掉注释后检查：
 *    - 没有包含上述头文件的 #include；
 *    - 代码里没有 CIUSocket:: 调用。
 *  检查范围：留在 Source/ 原位、与 exe 共用的文件按清单登记；FlamingoMedia/ 下的全部源文件自动扫描。
 *  第 1c 步起另有 G1：解析 Flamingo.vcxproj，确认已迁入 DLL 的代码（1c 通话、1d 远程桌面）不再由 exe 编译。
 *
 *  源文件位置：首选 __FILE__（FlamingoUnit.vcxproj 给本文件单独加了 /FC，编译期即为绝对路径），
 *  退路是从测试程序所在目录往上找客户端根目录。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "catch_amalgamated.hpp"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace {

//往上回溯目录的最多层数
static const int kMaxParentWalkDepth = 12;

//客户端根目录（flamingoclient）的识别标志：其下存在该文件
static const char* kClientRootMarker = "Source\\Flamingo.vcxproj";

//留在 Source/ 原位、与 exe 共用并编进 DLL 的源文件（相对 flamingoclient 根目录）。
//1b：公共代码；1c：通话用到的界面小工具与 MD5（中转自测签令牌）；1d：远程桌面会话对话框用到的标题栏图标与贴角落点。
//1b 登记过的远程桌面中转客户端已于 1d 随远程桌面
//整体搬进 FlamingoMedia/RemoteDesktop/，改由下面的目录扫描检查。
//FlamingoMedia/ 目录下的文件不必登记，用例会自动扫描整个目录。
static const char* kDllSourceList[] = {
    "Source\\net\\Protocol.h",
    "Source\\net\\FrameCodec.h",
    "Source\\net\\FrameCodec.cpp",
    "Source\\net\\FrameCrypto.h",
    "Source\\net\\FrameCrypto.cpp",
    "Source\\net\\FramePayload.h",
    "Source\\net\\FramePayload.cpp",
    "Source\\net\\HandshakeRunner.h",
    "Source\\net\\HandshakeRunner.cpp",
    "Source\\Common\\Util\\ZlibUtil.h",
    "Source\\Common\\Util\\ZlibUtil.cpp",
    "Source\\Common\\Util\\Path.h",
    "Source\\Common\\Util\\Path.cpp",
    "Source\\UI\\BalloonMain\\NotificationSound.h",
    "Source\\UI\\BalloonMain\\NotificationSound.cpp",
    "Source\\UI\\BalloonMain\\NotificationSoundPlayer.cpp",
    "Source\\UI\\BalloonChatWnd\\ChatIcons.h",
    "Source\\UI\\BalloonChatWnd\\ChatIcons.cpp",
    "Source\\Common\\Util\\MD5Core.h",
    "Source\\Common\\Util\\MD5Sum.cpp",
    "Source\\UI\\BalloonMain\\TitleBarIcon.h",
    "Source\\UI\\BalloonMain\\TitleBarIcon.cpp",
    "Source\\UI\\BalloonMain\\CornerPopupPlacement.h",
    "Source\\UI\\BalloonMain\\CornerPopupPlacement.cpp",
    //屏幕共享第 15 期：右下角弹窗（共享提示、录制完成、来电）标题后的倒计时，与 exe 的特别关注提示共用
    "Source\\UI\\BalloonMain\\CornerPopupCountdown.h",
    "Source\\UI\\BalloonMain\\CornerPopupCountdown.cpp",
    //单聊文件 P2P 直传与 P2P 徽章（2026-10-04）：远程桌面、通话、屏幕共享窗口上的「P2P」徽章
    "Source\\UI\\BalloonMain\\P2PBadge.h",
    "Source\\UI\\BalloonMain\\P2PBadge.cpp",
    //音视频通话优化（2026-10-05）：来电窗与通话画面里的头像，与主面板联系人列表同一份画法（文件共享 DLL 也直接编译这几份）
    "Source\\UI\\BalloonMain\\MainPanelAvatar.h",
    "Source\\UI\\BalloonMain\\MainPanelAvatar.cpp",
    "Source\\UI\\BalloonMain\\AvatarGlyph.h",
    "Source\\UI\\BalloonMain\\AvatarGlyph.cpp",
    "Source\\UI\\BalloonMain\\MainPanelIcons.h",
    "Source\\UI\\BalloonMain\\MainPanelIcons.cpp",
    "Source\\UI\\BalloonMain\\PresenceBadge.h",
    "Source\\UI\\BalloonMain\\PresenceBadge.cpp",
    "Source\\UI\\BalloonMain\\MainPanelStatusMenu.h",
    "Source\\UI\\BalloonMain\\MainPanelStatusMenu.cpp",
    "Source\\UI\\BalloonMain\\AvatarImageUtil.h",
    "Source\\UI\\BalloonMain\\AvatarImageUtil.cpp",
    "Source\\UI\\BalloonMain\\AvatarBitmapCache.h",
    "Source\\UI\\BalloonMain\\AvatarBitmapCache.cpp",
};

//#include 中不允许出现的 exe 专属头文件（按路径片段匹配，统一用正斜杠比较）
static const char* kForbiddenIncludes[] = {
    "IUSocket.h",
    "UserSessionData.h",
    "FlamingoClient.h",
    "BalloonMain/",
};

//虽在 UI/BalloonMain/ 下、但属于共用界面小工具、允许 DLL 包含的头文件（它们自身也在 kDllSourceList 里接受检查）
static const char* kAllowedSharedIncludes[] = {
    "BalloonMain/NotificationSound.h",
    "BalloonMain/TitleBarIcon.h",
    "BalloonMain/CornerPopupPlacement.h",
    "BalloonMain/CornerPopupCountdown.h",
    "BalloonMain/P2PBadge.h",
    //音视频通话优化（2026-10-05）：头像的画法（见 kDllSourceList 同一批登记）
    "BalloonMain/MainPanelAvatar.h",
    "BalloonMain/AvatarGlyph.h",
    "BalloonMain/MainPanelIcons.h",
    "BalloonMain/PresenceBadge.h",
    "BalloonMain/MainPanelStatusMenu.h",
    "BalloonMain/AvatarImageUtil.h",
    "BalloonMain/AvatarBitmapCache.h",
};

//自动扫描的 DLL 源码目录（相对 flamingoclient 根目录）与要跳过的构建中间目录名
static const char* kDllSourceRoot = "FlamingoMedia";
static const char* kSkippedDirs[] = { "Debug", "Release", "Debug_x64", "Release_x64" };

//G1：exe 工程里不应再出现的条目（Include 路径前缀，相对 Source 目录）——这些代码已整体迁入 DLL，
//exe 再编译一份就成了两边重复编译
static const char* kMovedOutOfExe[] = {
    "Feature\\Call\\",
    "net\\CallProtocolClient.",
    "UI\\BalloonMain\\MainPanelCallCoordinator.",
    "Feature\\RemoteDesktop\\",
    "UI\\BalloonRemoteDesktop\\",
    "net\\RemoteDesktopProtocolClient.",
    "UI\\BalloonMain\\MainPanelRdCoordinator.",
};

//代码中不允许出现的调用
static const char* kForbiddenCode = "CIUSocket::";

static size_t FindLastSeparator(const std::string& path)
{
    size_t a = path.find_last_of('\\');
    size_t b = path.find_last_of('/');
    if (a == std::string::npos)
    {
        return b;
    }
    if (b == std::string::npos)
    {
        return a;
    }
    return a > b ? a : b;
}

static std::string GetParentDirectory(const std::string& path)
{
    size_t pos = FindLastSeparator(path);
    return (pos == std::string::npos) ? std::string() : path.substr(0, pos);
}

static bool FileExists(const std::string& path)
{
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp == NULL)
    {
        return false;
    }
    fclose(fp);
    return true;
}

static std::string WalkUpToClientRoot(const std::string& startDir)
{
    std::string dir = startDir;
    for (int depth = 0; depth < kMaxParentWalkDepth && !dir.empty(); ++depth)
    {
        if (FileExists(dir + "\\" + kClientRootMarker))
        {
            return dir;
        }
        std::string parent = GetParentDirectory(dir);
        if (parent == dir)
        {
            break;
        }
        dir = parent;
    }
    return std::string();
}

//定位 flamingoclient 根目录，失败返回空串
static std::string ResolveClientRoot()
{
    std::string root = WalkUpToClientRoot(GetParentDirectory(std::string(__FILE__)));
    if (!root.empty())
    {
        return root;
    }
    char moduleName[MAX_PATH] = { 0 };
    DWORD len = ::GetModuleFileNameA(NULL, moduleName, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
    {
        return std::string();
    }
    return WalkUpToClientRoot(GetParentDirectory(std::string(moduleName)));
}

static bool ReadWholeFile(const std::string& path, std::string& out)
{
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp == NULL)
    {
        return false;
    }
    char buf[4096];
    size_t n = 0;
    out.clear();
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
    {
        out.append(buf, n);
    }
    fclose(fp);
    return true;
}

//去掉 // 与 /* */ 注释（字符串字面量里的注释符号不做特殊处理，清单内文件不受影响）
static std::string StripComments(const std::string& src)
{
    std::string out;
    out.reserve(src.size());
    size_t i = 0;
    while (i < src.size())
    {
        if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '/')
        {
            while (i < src.size() && src[i] != '\n')
            {
                ++i;
            }
        }
        else if (src[i] == '/' && i + 1 < src.size() && src[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/'))
            {
                ++i;
            }
            i += 2;
        }
        else
        {
            out += src[i];
            ++i;
        }
    }
    return out;
}

//收集一段源码（已去注释）里违反规则的行，返回描述列表
static std::vector<std::string> FindViolations(const std::string& code)
{
    std::vector<std::string> found;
    size_t start = 0;
    while (start < code.size())
    {
        size_t end = code.find('\n', start);
        if (end == std::string::npos)
        {
            end = code.size();
        }
        std::string line = code.substr(start, end - start);
        std::string normalized = line;
        for (size_t k = 0; k < normalized.size(); ++k)
        {
            if (normalized[k] == '\\')
            {
                normalized[k] = '/';
            }
        }
        if (normalized.find("#include") != std::string::npos)
        {
            bool allowed = false;
            for (size_t a = 0; a < sizeof(kAllowedSharedIncludes) / sizeof(kAllowedSharedIncludes[0]); ++a)
            {
                if (normalized.find(kAllowedSharedIncludes[a]) != std::string::npos)
                {
                    allowed = true;
                }
            }
            for (size_t f = 0; f < sizeof(kForbiddenIncludes) / sizeof(kForbiddenIncludes[0]) && !allowed; ++f)
            {
                if (normalized.find(kForbiddenIncludes[f]) != std::string::npos)
                {
                    found.push_back("include: " + line);
                }
            }
        }
        if (line.find(kForbiddenCode) != std::string::npos)
        {
            found.push_back("call: " + line);
        }
        start = end + 1;
    }
    return found;
}

//判断文件名是否为要检查的源文件（.h / .cpp / .inl）
static bool IsSourceFile(const std::string& name)
{
    const char* exts[] = { ".h", ".cpp", ".inl" };
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i)
    {
        size_t n = strlen(exts[i]);
        if (name.size() > n && name.compare(name.size() - n, n, exts[i]) == 0)
        {
            return true;
        }
    }
    return false;
}

//递归收集 dir 下的源文件（相对 root 的路径追加到 out），跳过构建中间目录
static void CollectSources(const std::string& root, const std::string& rel, std::vector<std::string>& out)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = ::FindFirstFileA((root + "\\" + rel + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        return;
    }
    do
    {
        std::string name(fd.cFileName);
        if (name == "." || name == "..")
        {
            continue;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            bool skipped = false;
            for (size_t i = 0; i < sizeof(kSkippedDirs) / sizeof(kSkippedDirs[0]); ++i)
            {
                if (name == kSkippedDirs[i])
                {
                    skipped = true;
                }
            }
            if (!skipped)
            {
                CollectSources(root, rel + "\\" + name, out);
            }
        }
        else if (IsSourceFile(name))
        {
            out.push_back(rel + "\\" + name);
        }
    } while (::FindNextFileA(h, &fd));
    ::FindClose(h);
}

//检查一个源文件（相对 root），违规时记录到 Catch2 的输出并判失败
static void CheckOneFile(const std::string& root, const std::string& rel)
{
    INFO("file: " << rel);
    std::string src;
    REQUIRE(ReadWholeFile(root + "\\" + rel, src));
    std::vector<std::string> violations = FindViolations(StripComments(src));
    for (size_t v = 0; v < violations.size(); ++v)
    {
        UNSCOPED_INFO(violations[v]);
    }
    CHECK(violations.empty());
}

} // namespace

TEST_CASE("MDG-D1 可编入 DLL 的源文件不依赖 exe 专属模块", "[mediadll]")
{
    std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());
    //与 exe 共用、留在 Source/ 原位的文件
    for (size_t i = 0; i < sizeof(kDllSourceList) / sizeof(kDllSourceList[0]); ++i)
    {
        CheckOneFile(root, kDllSourceList[i]);
    }
    //FlamingoMedia/ 下的全部源文件（自动扫描，新增文件无需登记）
    std::vector<std::string> dllFiles;
    CollectSources(root, kDllSourceRoot, dllFiles);
    REQUIRE_FALSE(dllFiles.empty());
    for (size_t i = 0; i < dllFiles.size(); ++i)
    {
        CheckOneFile(root, dllFiles[i]);
    }
    //1c：通话代码已整体迁入 FlamingoMedia/Call/；1d：远程桌面代码已整体迁入 FlamingoMedia/RemoteDesktop/
    bool hasCallDir = false;
    bool hasRdDir = false;
    for (size_t i = 0; i < dllFiles.size(); ++i)
    {
        if (dllFiles[i].find("FlamingoMedia\\Call\\") == 0)
        {
            hasCallDir = true;
        }
        if (dllFiles[i].find("FlamingoMedia\\RemoteDesktop\\") == 0)
        {
            hasRdDir = true;
        }
    }
    CHECK(hasCallDir);
    CHECK(hasRdDir);
}

TEST_CASE("MDG-G1 exe 工程不再编译已迁入 DLL 的代码", "[mediadll]")
{
    std::string root = ResolveClientRoot();
    REQUIRE_FALSE(root.empty());
    std::string proj;
    REQUIRE(ReadWholeFile(root + "\\Source\\Flamingo.vcxproj", proj));
    const std::string key = "Include=\"";
    size_t pos = 0;
    int checked = 0;
    while ((pos = proj.find(key, pos)) != std::string::npos)
    {
        pos += key.size();
        size_t end = proj.find('"', pos);
        if (end == std::string::npos)
        {
            break;
        }
        std::string include = proj.substr(pos, end - pos);
        ++checked;
        for (size_t i = 0; i < sizeof(kMovedOutOfExe) / sizeof(kMovedOutOfExe[0]); ++i)
        {
            INFO("exe project still contains: " << include);
            CHECK(include.compare(0, strlen(kMovedOutOfExe[i]), kMovedOutOfExe[i]) != 0);
        }
    }
    CHECK(checked > 0);
}
