/**
 *  访问者一端（DLL 的浏览窗口）的端到端联调用例（Suite SBL，标签 [.live][fileshare]），ShareBrowseLiveTests.cpp
 *
 *  对应 docs/文件共享方案.md 第 1 期第 13 步的验证：两端都是产品代码。分享者是真实运行的 tmp/fileshare_env/clientA（MY0127）；
 *  访问者是本测试进程加载的 FlamingoFileShare.dll —— 宿主换成 LiveHost，它经 LiveChatClient 以 MY0308 的身份登录
 *  chatserver，把 DLL 发出的信令转发出去，再把服务端的应答与推送交还给 DLL：
 *    - SBL1 OpenShareFromCard 打开共享 9001：DLL 发 2220、收 2222、连中转、等 sf_hello、列根目录，浏览窗口显示 docs 与
 *           readme.txt 两项；给列表第一行发一次双击，进入 docs（空目录）；浏览窗口截图存到 snapshots/fileshare_browser_*.png
 *           供人工核对界面；关闭模块时 DLL 发 sf_bye 与 2221，服务端应答成功。
 *
 *  默认不参与常规测试。跑之前：
 *    1. python tmp/fileshare_env/seed_step12.py 造数据；
 *    2. 起 tmp/fileshare_env 的服务端与 clientA，等 clientA 报告 2210；
 *    3. FlamingoUnit.exe "[fileshare]"（会同时跑 SPL1）
 *
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <gdiplus.h>
#include <string.h>
#include <map>
#include <stdio.h>
#include <string>
#include <utility>
#include <vector>

#include "../../Source/Tests/LiveChatClient.h"
#include "../../Source/App/PluginModuleLoader.h"
#include "../../Source/net/Protocol.h"
#include "../FileShareApi.h"
#include "../Engine/ShareProtocolCodec.h"

using namespace livetest;
using balloonapp::CPluginModuleLoader;
using balloonapp::plugin_load_ok;
using fileshare::IFileShareHost;
using fileshare::IFileShareModule;
using fileshare::FileShareInitParams;
using fileshare::FileShareSessionInfo;

namespace {

//tmp/fileshare_env 测试服务端的 chatserver 端口、访问者账号与密码、被访问的共享
const unsigned short kSblChatPort = 25000;
const char* const kSblAccount = "MY0308";
const char* const kSblPassword = "perf1234";
const int64_t kSblShareId = 9001;

//等信令的最长时间、等浏览窗口列出目录的最长时间（毫秒）
const int kSblSignalWaitMs = 5000;
const DWORD kSblListWaitMs = 20000;

//抽取消息时每次等待的片长（毫秒）与每轮最多处理的消息数
const DWORD kSblPumpSliceMs = 50;
const int kSblMaxPumpRounds = 200;

//浏览窗口的标题（共享根名 + 「 - 文件共享」）
const wchar_t* const kSblWindowTitle = L"share_root - \x6587\x4EF6\x5171\x4EAB";

//列表第一行的中心点（客户区坐标）：标题栏 32 + 导航行 36 + 操作行 32（第 2 期第 3 步起工具栏拆成两行）+ 表头 28
//之下的第一行（行高 28），横向取名称列靠左处
const int kSblFirstRowX = 80;
const int kSblFirstRowY = 32 + 36 + 32 + 28 + 14;

//PrintWindow 的 PW_RENDERFULLCONTENT 标志（Windows 8.1 起支持）：本工程的 _WIN32_WINNT 较低，头文件里没有这个宏
const UINT kSblPrintFullContent = 0x00000002;

//个人目录（测试程序目录下）
const wchar_t* const kSblPersonalDir = L"sbl_personal";

//截图存放位置（仓库根的 snapshots 目录，相对测试程序目录 Debug_Win32/Tests）
const wchar_t* const kSblSnapshotDir = L"\\..\\..\\..\\snapshots\\";

//测试程序所在目录（不带结尾反斜杠）
std::wstring SblExeDir()
{
    wchar_t buf[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring s(buf);
    const size_t pos = s.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? s : s.substr(0, pos);
}

//宿主：经 LiveChatClient 收发信令，日志记在内存里
class LiveHost : public IFileShareHost
{
public:
    LiveHost()
        : m_seq(1)
    {
        memset(m_pubKey, 0, sizeof(m_pubKey));
    }

    virtual void Log(int level, const char* text) override
    {
        (void)level;
        if (text != NULL)
            m_lines.push_back(text);
    }

    virtual uint32_t SendShareCommand(uint16_t cmd, const char* json, uint32_t len) override
    {
        //LiveChatClient 的 seq 从 1（登录）起每发一帧加 1，这里同步记下，应答按它回带
        if (!m_client.Send(cmd, std::string(json != NULL ? json : "", len)))
            return 0;
        ++m_seq;
        m_lastSeq[cmd] = m_seq;
        return m_seq;
    }

    virtual const unsigned char* GetServerPublicKey() override
    {
        return m_pubKey;
    }

    virtual bool GetSessionInfo(FileShareSessionInfo* info) override
    {
        if (info == NULL || info->m_cbSize != sizeof(FileShareSessionInfo))
            return false;
        info->m_userId = m_client.UserId();
        strcpy_s(info->m_account, fileshare::kFileShareTextBytes, kSblAccount);
        strcpy_s(info->m_nickName, fileshare::kFileShareTextBytes, kSblAccount);
        strcpy_s(info->m_deviceId, fileshare::kFileShareTextBytes, "livetest-MY0308");
        wcsncpy_s(info->m_personalDir, fileshare::kFileSharePathChars, m_personalDir.c_str(), _TRUNCATE);
        return true;
    }

    virtual bool RequestConversationMembers(int ctype, int64_t cid) override
    {
        (void)ctype;
        (void)cid;
        return false;
    }

    virtual bool GetUserDisplayName(int64_t userId, char* buf, uint32_t bufBytes) override
    {
        (void)userId;
        (void)buf;
        (void)bufBytes;
        return false;
    }

    virtual void NotifyShareState(int64_t shareId, int state, int reason, int myPerms) override
    {
        (void)shareId;
        (void)state;
        (void)reason;
        (void)myPerms;
    }

    virtual bool GetConversationName(int ctype, int64_t cid, char* buf, uint32_t bufBytes) override
    {
        if (buf == NULL || bufBytes == 0)
            return false;
        _snprintf_s(buf, bufBytes, _TRUNCATE, "conv%d-%lld", ctype, static_cast<long long>(cid));
        return true;
    }

    virtual bool OpenConversation(int ctype, int64_t cid) override
    {
        m_openedConversations.push_back(std::make_pair(ctype, cid));
        return true;
    }

    virtual bool OpenImageViewer(HWND owner, const wchar_t* imagePath, bool navigable) override
    {
        (void)owner;
        m_openedImages.push_back(imagePath != NULL ? std::wstring(imagePath) : std::wstring());
        m_lastImageNavigable = navigable;
        return imagePath != NULL;
    }

    virtual void NotifyConversationShareCount(int ctype, int64_t cid, int count) override
    {
        (void)ctype;
        (void)cid;
        (void)count;
    }

    virtual bool PickShareTargets(HWND owner) override
    {
        (void)owner;
        return false;
    }

    virtual bool CreateShareGroup(const char* groupName, const int64_t* memberIds, uint32_t count) override
    {
        (void)groupName;
        (void)memberIds;
        (void)count;
        return false;
    }

    //联调用例不看头像：一律不给，界面画空白头像
    virtual bool GetUserAvatar(int64_t userId, fileshare::FileShareAvatarInfo* out) override
    {
        (void)userId;
        (void)out;
        return false;
    }

    //第 11 版接口（多语言）：不翻译，原样返回原文
    virtual const wchar_t* Translate(const wchar_t* context, const wchar_t* source) override
    {
        (void)context;
        return source;
    }

    //第 11 版接口（多语言）：报告简体中文与默认字体，DLL 里的 TR 因此直接返回原文
    virtual bool GetUiLanguage(wchar_t* langBuf, uint32_t langChars, wchar_t* fontBuf, uint32_t fontChars) override
    {
        if (langBuf == NULL || langChars == 0 || fontBuf == NULL || fontChars == 0)
            return false;
        wcsncpy_s(langBuf, langChars, L"zh-CN", _TRUNCATE);
        wcsncpy_s(fontBuf, fontChars, L"Microsoft YaHei", _TRUNCATE);
        return true;
    }

    //P2P 服务：本组用例不走 P2P，返回 NULL（文件共享的传输全部走中转）
    virtual p2p::IP2PService* GetP2PService() override
    {
        return NULL;
    }

    //第 12 版：弹窗通知照常弹出
    virtual int QueryNoticePolicy() override
    {
        return fileshare::fileshare_notice_show;
    }

    //第 12 版：不出声
    virtual void PlayNoticeSound() override
    {
    }

    std::vector<std::pair<int, int64_t> > m_openedConversations;   //请宿主定位过的会话：(会话类型, 会话 id)
    std::vector<std::wstring>             m_openedImages;          //请宿主用看图窗打开过的图片，按顺序
    bool                                  m_lastImageNavigable = false;   //最近一次打开时是否可以翻页

    //等服务端的某条应答或推送并交给模块；应答按发出时的 seq 回带，推送为 0
    bool Relay(IFileShareModule* module, uint16_t cmd, bool isResponse, int timeoutMs)
    {
        std::string body;
        if (!m_client.WaitFor(cmd, body, timeoutMs))
            return false;
        const uint32_t seq = isResponse ? m_lastSeq[cmd] : 0;
        return module->OnServerFrame(cmd, seq, body.data(), static_cast<uint32_t>(body.size()));
    }

    //含指定文字的日志行数
    int Count(const char* marker) const
    {
        int n = 0;
        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            if (m_lines[i].find(marker) != std::string::npos)
                ++n;
        }
        return n;
    }

    LiveChatClient                m_client;        //以访问者身份登录的 chatserver 连接
    uint32_t                      m_seq;           //最近一次发出的帧的 seq（登录为 1）
    std::map<uint16_t, uint32_t>  m_lastSeq;       //各命令最近一次发出时的 seq
    unsigned char                 m_pubKey[net::IDENTITY_PUBLICKEY_BYTES];   //服务器公钥
    std::wstring                  m_personalDir;   //个人目录（带结尾反斜杠）
    std::vector<std::string>      m_lines;         //DLL 写来的日志
};

//抽取本线程的消息，直到宿主日志里出现指定文字或超时
bool PumpUntilLogged(const LiveHost& host, const char* marker, DWORD maxMs)
{
    const DWORD start = ::GetTickCount();
    while (host.Count(marker) == 0)
    {
        if (::GetTickCount() - start > maxMs)
            return false;
        ::MsgWaitForMultipleObjects(0, NULL, FALSE, kSblPumpSliceMs, QS_ALLINPUT);
        MSG msg;
        for (int i = 0; i < kSblMaxPumpRounds && ::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE); ++i)
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }
    return true;
}

//找本进程里标题为 title 的顶层窗口
struct FindContext
{
    const wchar_t* m_title;   //要找的标题
    HWND           m_found;   //找到的窗口
};

BOOL CALLBACK FindOwnWindow(HWND hWnd, LPARAM lParam)
{
    FindContext* ctx = reinterpret_cast<FindContext*>(lParam);
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hWnd, &pid);
    wchar_t title[MAX_PATH] = { 0 };
    ::GetWindowTextW(hWnd, title, MAX_PATH);
    if (pid == ::GetCurrentProcessId() && wcscmp(title, ctx->m_title) == 0)
    {
        ctx->m_found = hWnd;
        return FALSE;
    }
    return TRUE;
}

//把窗口画面存成 PNG（PrintWindow 取窗口自己的绘制结果，不受其它窗口遮挡）
bool SaveWindowPng(HWND hWnd, const std::wstring& path)
{
    RECT rc;
    ::GetWindowRect(hWnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    HDC screen = ::GetDC(NULL);
    HDC mem = ::CreateCompatibleDC(screen);
    HBITMAP bmp = ::CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = ::SelectObject(mem, bmp);
    ::PrintWindow(hWnd, mem, kSblPrintFullContent);
    ::SelectObject(mem, old);

    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput input;
    bool ok = false;
    if (Gdiplus::GdiplusStartup(&token, &input, NULL) == Gdiplus::Ok)
    {
        UINT num = 0;
        UINT size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        std::vector<BYTE> buf(size);
        Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
        Gdiplus::GetImageEncoders(num, size, codecs);
        for (UINT i = 0; i < num; ++i)
        {
            if (wcscmp(codecs[i].MimeType, L"image/png") == 0)
            {
                Gdiplus::Bitmap image(bmp, NULL);
                ok = (image.Save(path.c_str(), &codecs[i].Clsid, NULL) == Gdiplus::Ok);
                break;
            }
        }
        Gdiplus::GdiplusShutdown(token);
    }
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
    ::ReleaseDC(NULL, screen);
    return ok;
}

/**
 *  模块关闭守卫：析构时若模块还没关闭就关闭它。用例中途 REQUIRE 失败时，加载器随后卸载 DLL，若模块没有先关闭，
 *  DLL 建的窗口还在、窗口过程却已卸载，之后这些窗口收到消息就会崩溃（第 2 期第 4 步曾因此让后面的 STL3 崩溃）。
 */
class ModuleShutdownGuard
{
public:
    explicit ModuleShutdownGuard(IFileShareModule* module)
        : m_module(module)
    {
    }

    ~ModuleShutdownGuard()
    {
        Release();
    }

    //关闭模块（只关闭一次）
    void Release()
    {
        if (m_module != NULL)
            m_module->Shutdown();
        m_module = NULL;
    }

private:
    IFileShareModule* m_module;   //要关闭的模块（不拥有）；已关闭时为 NULL
};

} // namespace

TEST_CASE("SBL1 浏览窗口经真实中转列出分享者的目录", "[.live][fileshare]")
{
    LiveHost host;
    std::string reason;
    const bool hostLoggedIn = host.m_client.ConnectAndLogin(kSblChatPort, kTestServerPubKey, kSblAccount, kSblPassword, reason);
    INFO("login failed: " << reason);
    REQUIRE(hostLoggedIn);
    REQUIRE(net::parseServerPublicKey(kTestServerPubKey, host.m_pubKey));
    host.m_personalDir = SblExeDir() + L"\\" + kSblPersonalDir + L"\\";
    ::CreateDirectoryW((SblExeDir() + L"\\" + kSblPersonalDir).c_str(), NULL);

    CPluginModuleLoader<IFileShareModule> loader;
    const std::wstring dllPath = SblExeDir() + L"\\" + FLAMINGO_FILESHARE_DLL_NAME;
    REQUIRE(loader.Load(dllPath.c_str(), FLAMINGO_FILESHARE_FACTORY_NAME, fileshare::kApiVersion) == plugin_load_ok);
    IFileShareModule* module = loader.GetModule();
    const std::wstring appDir = SblExeDir();
    FileShareInitParams params;
    params.m_cbSize = sizeof(params);
    params.m_appDir = appDir.c_str();
    REQUIRE(module->Init(&host, &params));
    //守卫声明在加载器之后：先析构，先关闭模块再卸载 DLL
    ModuleShutdownGuard guard(module);

    //打开共享：2220 应答、推给访问者的 2222 依次交给 DLL，之后 DLL 自己连中转、等 sf_hello、列根目录
    REQUIRE(module->OpenShareFromCard(NULL, kSblShareId));
    REQUIRE(host.Relay(module, cmd_share_open, true, kSblSignalWaitMs));
    REQUIRE(host.Relay(module, cmd_share_relay_info, false, kSblSignalWaitMs));
    REQUIRE(PumpUntilLogged(host, "share browser listed, shareid: 9001, entries: 2.", kSblListWaitMs));

    //截图：根目录
    FindContext ctx;
    ctx.m_title = kSblWindowTitle;
    ctx.m_found = NULL;
    ::EnumWindows(&FindOwnWindow, reinterpret_cast<LPARAM>(&ctx));
    REQUIRE(ctx.m_found != NULL);
    const std::wstring snapDir = SblExeDir() + kSblSnapshotDir;
    CHECK(SaveWindowPng(ctx.m_found, snapDir + L"fileshare_browser_root.png"));

    //双击第一行（目录 docs）：进入 docs，空目录
    const LPARAM pt = MAKELPARAM(kSblFirstRowX, kSblFirstRowY);
    ::PostMessageW(ctx.m_found, WM_LBUTTONDOWN, MK_LBUTTON, pt);
    ::PostMessageW(ctx.m_found, WM_LBUTTONUP, 0, pt);
    ::PostMessageW(ctx.m_found, WM_LBUTTONDBLCLK, MK_LBUTTON, pt);
    ::PostMessageW(ctx.m_found, WM_LBUTTONUP, 0, pt);
    REQUIRE(PumpUntilLogged(host, "share browser listed, shareid: 9001, entries: 0.", kSblListWaitMs));
    CHECK(SaveWindowPng(ctx.m_found, snapDir + L"fileshare_browser_docs.png"));

    //关闭模块：浏览窗口关闭，DLL 发 sf_bye 与 2221，服务端应答成功
    guard.Release();
    std::string body;
    REQUIRE(host.m_client.WaitFor(cmd_share_close_access, body, kSblSignalWaitMs));
    int code = -1;
    std::string msg;
    REQUIRE(fileshare::ShareProtocolCodec::ParseSimpleResponse(body, code, msg));
    CHECK(code == 0);
    loader.Unload();
}
