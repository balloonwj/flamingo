/**
 *  传输子会话的端到端联调用例（Suite STL，标签 [.live][fileshare]），ShareTransferLiveTests.cpp
 *
 *  对应 docs/文件共享方案.md 第 1 期第 14 步的验证。分享者是真实运行的 tmp/fileshare_env/clientA（MY0127）；访问者一侧
 *  在本测试进程里直接使用 DLL 的引擎（CShareService、CShareVisitor、CShareTransferSession）与浏览窗口类，宿主经
 *  LiveChatClient 以 MY0308 的身份登录 chatserver。一条专门的线程按到达顺序收服务端的帧，主线程把它们交给 CShareService，
 *  并抽取消息窗口的事件与定时器：
 *    - STL1 下载：整个目录（含子目录、空目录、跨多个分片的文件）下载后与源文件逐字节一致（比 MD5）；再下载一次时顶层
 *           目录自动改名为「data(2)」；预先放好半截文件与续传记录后再下载，大文件从断点续传（日志可见）且结果一致；
 *    - STL2 上传：本机目录上传到共享根，分享者那边的文件逐字节一致、没有留下半成品；同名再上传被拒（807）；
 *    - STL3 浏览窗口：在测试进程里打开浏览窗口，DownloadRow 下载一项，传输列表显示「已完成」；有上传权限时显示上传按钮；
 *           截图存到 snapshots/fileshare_browser_transfer.png 供人工核对界面。
 *  第 15、16 步的联调也放在本文件（共用登录与收帧环境）：
 *    - SML1 本端（MY0308）在与 MY0127 的单聊里发起文件夹共享：应答成功、本机记录报告为可提供、收到服务端发来的共享卡片
 *           （msgtype 20，正文能解析出同一个共享）；改 MY0127 的权限成功；单聊里移除对方被拒（3）；取消成功后本机记录删除，
 *           clientA 的日志显示文件共享模块把「已失效」通知给了宿主、主面板逐窗刷新了卡片；
 *    - SML2 本会话共享浮层：与 MY0127 的单聊里按真实服务端的数据列出共享（含 MY0127 共享给本人的 9006，操作为「打开」），
 *           列表应答顺带更新了卡片状态缓存；点「打开」经模块接口打开 9006。
 *    - SML3 「文件共享」窗口（第 17 步）：「共享给我的」页签里有 9006，详情显示本人的上传权限与所在会话，操作为打开与定位到会话；
 *           定位到会话经宿主打开与 MY0127 的单聊，打开经模块接口打开 9006。
 *  第 18 步第 1 期整体验收（方案 12.4 第 1 期，标签另加 [acceptance]；先跑 tmp/fileshare_env/seed_accept.py，它把每个共享同时传输的
 *  人数上限改成 2、加了群共享 9007 与「中文资料」目录）：
 *    - SAC1 中文名、8 层深目录、一万个小文件、64 MiB 文件的目录整体下载，结果逐一核对；
 *    - SAC2 群聊共享给部分成员：被选中的成员收到卡片，未被选中的收不到、打开被拒（803）、「共享给我的」里没有它；
 *    - SAC3 下载中途分享者掉线：传输中止、卡片变为暂不可用、留下续传记录；分享者恢复后从断点续传，结果一致；
 *    - SAC4 分享者无响应（进程挂起）：访问者打开时在超时后得到 816；
 *    - SAC5 上传：没有上传权限被拒（806）；两名访问者同时上传同名目录，一个成功、一个被拒（807）；
 *    - SAC6 分享者退出后重新登录：期间共享暂不可用（分享者不在线），恢复后可用；绑定另一台电脑的共享显示「位于另一台电脑」；
 *    - SAC7 成员变化：调整权限后访问者即时收到新权限，追加的成员收到新卡片，下载中被移除的成员传输中止、卡片已失效；
 *    - SAC8 取消原因：分享者取消、群主取消、群管理员取消，卡片原因各自正确（群解散、分享者退群由服务端 SHG1~4 覆盖）；
 *    - SAC9 并发：同一共享第 3 名同时下载者被拒（813，上限已改为 2）；
 *    - SAC10 没有 DLL：clientA 照常登录，日志记下模块不可用（卡片与入口的表现由 CFS3、W18、G6c、SYS7 覆盖）。
 *  第 2 期第 2 步（写操作，数据由 tmp/fileshare_env/seed_p2_write.py 准备，须在 clientA 启动之前运行）：
 *    - SWL1 浏览窗口里新建文件夹（同名再建被拒）、重命名、移动（移到自身被拒）、删除（真的放进 E: 盘的回收站，
 *           测完只清理原路径在共享根之下的条目）；没有写权限的共享不显示「新建文件夹」、新建被拒（806）。
 *  第 2 期第 3 步（搜索，共享 9006 的「中文资料」目录由 seed_accept.py 准备）：
 *    - SSL1 浏览窗口里搜索：关键字命中 10 个文件、路径相对共享根；命中上万项的关键字按服务端下发的上限只显示 500 项并
 *           提示截断；清空关键字回到目录列表；截图 snapshots/fileshare_browser_search.png。
 *  第 2 期第 4 步（缩略图与预览，用例自己在共享 9006 的根下建「图片」目录）：
 *    - STH1 缩略图视图里三张图片的缩略图都取到（文本文件不取）；截图 snapshots/fileshare_browser_grid.png；预览一张，
 *           交给看图窗的文件是长边不超过屏幕的 JPEG、可以翻页；翻到下一张换了文件；关闭窗口后不再作为预览的来源。
 *
 *  第 4 期（操作通知与后台传输，2026-10-07）：
 *    - SNL1 分享者与访问者都在测试进程里：关闭浏览窗口后下载、上传在后台完成，两边都收到通知（标签 [sharenoticelive]，
 *           服务端为 tmp/fsnotify/live，见用例前的说明）。
 *
 *  默认不参与常规测试。跑之前：
 *    1. python tmp/fileshare_env/seed_step12.py 与 seed_step14.py（后者建共享 9006：MY0308 有上传权限，共享根 share_xfer 里放
 *       data 目录与几个测试文件）；
 *    2. 起 tmp/fileshare_env 的服务端与 clientA（start_server.ps1、start_client.ps1 clientA），等 clientA 报告 2210；
 *    3. FlamingoUnit.exe "[fileshare]"
 *
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string.h>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <sstream>
#include <stdio.h>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../../Source/Tests/LiveChatClient.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../Source/Common/Util/MD5Core.h"
#include "../../Source/net/Protocol.h"
#include "../FileShareApi.h"
#include "../Engine/ShareFileOps.h"
#include "../Engine/ShareService.h"
#include "../Engine/ShareWinPath.h"
#include "../UI/ShareBrowserWindow.h"
#include "../UI/ShareConversationPopup.h"
#include "../UI/ShareCenterWindow.h"
#include "../Engine/ShareCenterModel.h"
#include "../Engine/ShareBrowseModel.h"
#include "../Engine/ShareLocalStore.h"
#include "../Engine/ShareProtocolCodec.h"
#include <tlhelp32.h>
#include <objidl.h>
#include <gdiplus.h>
#include "../Engine/ShareConversationModel.h"
#include "../../Source/net/MessageProtocolClient.h"
#include "ShareTestFs.h"
#include "ShareTestSnapshot.h"
#include "../../Source/third-party/jsoncpp-1.9.0/json.h"
#include "../UI/ShareDragData.h"
#include "../UI/ShareNoticeCenter.h"   //SNL1：两边的弹窗总控
#include "ShareNoticeTestHost.h"       //SNL1：结束监听与分享者操作监听的桩

using namespace livetest;
using namespace fileshare;

namespace {

//tmp/fileshare_env 的 chatserver 端口、访问者账号与密码、被访问的共享
const unsigned short kStlChatPort = 25000;
const char* const kStlAccount = "MY0308";
const char* const kStlPassword = "perf1234";
const int64_t kStlShareId = 9006;

//共享 9006 在分享者 clientA 上的共享根（与 seed_step14.py 一致）
const wchar_t* const kStlShareRoot = L"E:\\flamingo\\tmp\\fileshare_env\\share_xfer";

//测试程序目录下的工作目录
const wchar_t* const kStlWorkDir = L"stl_work";

//消息窗口：事件通知消息与定时器
const UINT kStlEventMsg = WM_APP + 0x81;
const UINT_PTR kStlTimerId = 1;
const UINT kStlTimerMs = 1000;

//收帧线程每次等待的时长、各步骤的最长等待（毫秒）
const int kStlReadSliceMs = 300;
const DWORD kStlOpenWaitMs = 20000;
const DWORD kStlTransferWaitMs = 60000;
const DWORD kStlPumpSliceMs = 30;
const int kStlMaxPumpRounds = 200;

//测试进程里引擎日志的文件前缀
const wchar_t* const kStlLogPrefix = L"E:\\flamingo\\tmp\\fileshare_step1\\livetest";

//MD5 摘要字节数与读文件的块大小
const int kStlDigestBytes = 16;
const DWORD kStlReadChunk = 1024 * 1024;

//续传用例：预先放好的半截文件长度（字节）
const int64_t kStlResumeBytes = 3 * 1024 * 1024 + 123;

//上传用例的文件大小
const size_t kStlUploadBig = 2 * 1024 * 1024 + 7;
const size_t kStlUploadSmall = 4096;

//64 位文件大小拆成两个 32 位字段时，高位字段的位移；十六进制一位的位数与掩码
const int kStlHighShift = 32;
const int kStlNibbleBits = 4;
const unsigned char kStlNibbleMask = 0x0F;

//截图前再抽几轮消息，让界面画完最后的状态
const int kStlSettleRounds = 10;

//分享者 clientA 的账号 MY0127 的 userid（联调环境的固定数据，与 seed_step12.py 一致）
const int64_t kStlPeerUserId = 5127;

//SML1 发起共享时的共享根（每次运行前重建）与里面放的文件字节数
const wchar_t* const kSmlCreateRoot = L"E:\\flamingo\\tmp\\fileshare_env\\share_create";
const size_t kSmlFileBytes = 100;

//等一次管理请求的结果、等卡片推送、等 clientA 日志的最长时长（毫秒）
const DWORD kSmlWaitMs = 20000;

//截图位置（测试程序目录 Debug_Win32/Tests 往上三级是仓库根）
const wchar_t* const kStlSnapshot = L"\\..\\..\\..\\snapshots\\fileshare_browser_transfer.png";

//宿主：经 LiveChatClient 发信令；日志记在内存里
class StlHost : public IFileShareHost
{
public:
    StlHost()
        : m_seq(1)
        , m_account(kStlAccount)
    {
        memset(m_pubKey, 0, sizeof(m_pubKey));
    }

    virtual void Log(int level, const char* text) override
    {
        (void)level;
        if (text == NULL)
            return;
        std::lock_guard<std::mutex> guard(m_logMutex);
        m_lines.push_back(text);
    }

    virtual uint32_t SendShareCommand(uint16_t cmd, const char* json, uint32_t len) override
    {
        if (!m_client.Send(cmd, std::string(json != NULL ? json : "", len)))
            return 0;
        return ++m_seq;
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
        strcpy_s(info->m_account, kFileShareTextBytes, m_account.c_str());
        strcpy_s(info->m_nickName, kFileShareTextBytes, m_account.c_str());
        //与 LiveChatClient 登录时上报的 deviceid 相同
        strcpy_s(info->m_deviceId, kFileShareTextBytes, ("livetest-" + m_account).c_str());
        wcsncpy_s(info->m_personalDir, kFileSharePathChars, m_personalDir.c_str(), _TRUNCATE);
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

    int Count(const char* marker)
    {
        std::lock_guard<std::mutex> guard(m_logMutex);
        int n = 0;
        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            if (m_lines[i].find(marker) != std::string::npos)
                ++n;
        }
        return n;
    }

    LiveChatClient           m_client;       //以访问者身份登录的连接
    uint32_t                 m_seq;          //最近一次发出的帧的 seq（登录为 1）
    unsigned char            m_pubKey[net::IDENTITY_PUBLICKEY_BYTES];   //服务器公钥
    std::wstring             m_personalDir;  //个人目录（带结尾反斜杠）
    std::string              m_account;      //登录的账号（默认 MY0308；验收用例里还有 MY0309、MY0310）
    std::mutex               m_logMutex;     //保护 m_lines
    std::vector<std::string> m_lines;        //日志
};

//收到的一帧
struct StlFrame
{
    uint16_t    m_cmd;    //命令号
    uint32_t    m_seq;    //帧序号
    std::string m_body;   //包体
};

//专门收帧的线程：按到达顺序放进队列
class StlReader
{
public:
    explicit StlReader(LiveChatClient& client)
        : m_client(client)
        , m_stop(false)
    {
    }

    void Start()
    {
        m_thread = std::thread(&StlReader::Run, this);
    }

    void Stop()
    {
        m_stop = true;
        if (m_thread.joinable())
            m_thread.join();
    }

    //取出全部已收到的帧
    std::deque<StlFrame> Take()
    {
        std::deque<StlFrame> out;
        std::lock_guard<std::mutex> guard(m_mutex);
        out.swap(m_frames);
        return out;
    }

private:
    void Run()
    {
        while (!m_stop)
        {
            StlFrame f;
            if (!m_client.WaitAny(f.m_cmd, f.m_seq, f.m_body, kStlReadSliceMs))
                continue;
            std::lock_guard<std::mutex> guard(m_mutex);
            m_frames.push_back(f);
        }
    }

    LiveChatClient&       m_client;   //连接（不拥有）
    std::atomic<bool>     m_stop;     //是否停止
    std::thread           m_thread;   //收帧线程
    std::mutex            m_mutex;    //保护 m_frames
    std::deque<StlFrame>  m_frames;   //已收到、尚未交给业务总控的帧
};

//消息窗口的窗口过程把事件与定时器交给窗口自己的业务总控（建窗后存在 GWLP_USERDATA 里，同一进程里可以有多个环境）
LRESULT CALLBACK StlWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CShareService* service = reinterpret_cast<CShareService*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (msg == kStlEventMsg && service != NULL)
    {
        service->DrainEvents();
        return 0;
    }
    if (msg == WM_TIMER && wParam == kStlTimerId && service != NULL)
    {
        service->OnTimer();
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

//访问状态机的观察者：记下列目录结果
class StlVisitorObserver : public IShareVisitorObserver
{
public:
    StlVisitorObserver()
        : m_listedCount(0)
        , m_lastCode(-1)
    {
    }

    virtual void OnVisitorStateChanged(int state, int errCode) override
    {
        (void)state;
        (void)errCode;
    }

    virtual void OnVisitorListed(const std::string& path, int code, const std::vector<ShareEntry>& entries) override
    {
        (void)path;
        ++m_listedCount;
        m_lastCode = code;
        m_entries = entries;
    }

    virtual void OnVisitorPermsChanged(int perms) override
    {
        (void)perms;
    }

    virtual void OnVisitorShareState(int state, int reason) override
    {
        (void)state;
        (void)reason;
    }

    int                     m_listedCount;   //列目录结果的次数
    int                     m_lastCode;      //最近一次结果码
    std::vector<ShareEntry> m_entries;       //最近一次的条目
};

//传输观察者：记下各传输的结果
class StlTransferObserver : public IShareTransferObserver
{
public:
    virtual void OnTransferProgress(uint32_t localId, int64_t bytesDone, int64_t bytesTotal, int64_t filesDone,
                                    int64_t filesTotal) override
    {
        (void)bytesTotal;
        (void)filesDone;
        (void)filesTotal;
        m_progress[localId] = bytesDone;
    }

    int64_t Progress(uint32_t localId) const
    {
        std::map<uint32_t, int64_t>::const_iterator it = m_progress.find(localId);
        return (it == m_progress.end()) ? 0 : it->second;
    }

    std::map<uint32_t, int64_t> m_progress;   //编号 → 最近一次进度的已完成字节数

    virtual void OnTransferFinished(uint32_t localId, int code) override
    {
        m_results[localId] = code;
    }

    bool Finished(uint32_t localId) const
    {
        return m_results.find(localId) != m_results.end();
    }

    int Code(uint32_t localId) const
    {
        std::map<uint32_t, int>::const_iterator it = m_results.find(localId);
        return (it == m_results.end()) ? -1 : it->second;
    }

    std::map<uint32_t, int> m_results;   //编号 → 结果码
};

//测试环境：登录、业务总控、消息窗口、收帧线程
class StlEnv
{
public:
    StlEnv()
        : m_reader(m_host.m_client)
        , m_wnd(NULL)
        , m_attached(false)
        , m_logInited(false)
    {
    }

    ~StlEnv()
    {
        Shutdown();
    }

    void Setup(const std::wstring& personalDir, const std::string& account = kStlAccount)
    {
        //测试进程里的引擎经本进程的 CIULog 写日志：联调时写到 tmp/fileshare_step1/livetest.*.log 供排查。
        //同一用例可能同时有几个环境，按引用计数：第一个环境初始化，最后一个环境 Shutdown 时结束
        //（只初始化不结束时，进程退出时日志写线程仍在运行，测试进程无法正常退出）
        if (s_logRefs == 0)
            CIULog::Init(true, false, kStlLogPrefix);
        ++s_logRefs;
        m_logInited = true;
        std::string reason;
        m_host.m_account = account;
        const bool hostLoggedIn = m_host.m_client.ConnectAndLogin(kStlChatPort, kTestServerPubKey, account, kStlPassword, reason);
        INFO("login failed: " << reason);
        REQUIRE(hostLoggedIn);
        REQUIRE(net::parseServerPublicKey(kTestServerPubKey, m_host.m_pubKey));
        m_host.m_personalDir = personalDir;

        WNDCLASSEXW wc = { 0 };
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &StlWndProc;
        wc.hInstance = ::GetModuleHandleW(NULL);
        wc.lpszClassName = L"StlServiceWindow";
        ::RegisterClassExW(&wc);
        m_wnd = ::CreateWindowExW(0, L"StlServiceWindow", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
        REQUIRE(m_wnd != NULL);
        ::SetTimer(m_wnd, kStlTimerId, kStlTimerMs, NULL);
        ::SetWindowLongPtrW(m_wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&m_service));

        m_attached = true;
        m_service.Attach(&m_host, m_wnd, kStlEventMsg);
        m_reader.Start();
        m_service.OnLoggedIn();
    }

    void Shutdown()
    {
        if (m_attached)
        {
            m_service.Detach();
            m_reader.Stop();
            m_attached = false;
            if (m_wnd != NULL)
            {
                ::KillTimer(m_wnd, kStlTimerId);
                ::DestroyWindow(m_wnd);
                m_wnd = NULL;
            }
            m_host.m_client.Close();
        }
        //登录失败时 Setup 中途退出、没有 Attach，日志同样要结束
        if (m_logInited)
        {
            m_logInited = false;
            if (--s_logRefs == 0)
                CIULog::Uninit();
        }
    }

    //把收到的帧交给业务总控、抽取消息一轮
    void PumpOnce()
    {
        std::deque<StlFrame> frames = m_reader.Take();
        for (size_t i = 0; i < frames.size(); ++i)
        {
            if (IsShareCommand(frames[i].m_cmd))
                m_service.HandleServerFrame(frames[i].m_cmd, frames[i].m_seq, frames[i].m_body);
            else
                m_others.push_back(frames[i]);
        }
        ::MsgWaitForMultipleObjects(0, NULL, FALSE, kStlPumpSliceMs, QS_ALLINPUT);
        MSG msg;
        for (int i = 0; i < kStlMaxPumpRounds && ::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE); ++i)
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    //打开共享并列根目录
    CShareVisitor* OpenShare(StlVisitorObserver& observer)
    {
        CShareVisitor* visitor = m_service.AcquireVisitor(kStlShareId, &observer);
        REQUIRE(visitor != NULL);
        visitor->List("", static_cast<int64_t>(::GetTickCount64()));
        const DWORD start = ::GetTickCount();
        while (observer.m_listedCount == 0 && ::GetTickCount() - start < kStlOpenWaitMs)
            PumpOnce();
        REQUIRE(observer.m_listedCount > 0);
        REQUIRE(observer.m_lastCode == 0);
        REQUIRE(visitor->GetState() == share_visitor_ready);
        return visitor;
    }

    //等一次传输结束
    int WaitTransfer(StlTransferObserver& observer, uint32_t localId, DWORD waitMs = kStlTransferWaitMs)
    {
        const DWORD start = ::GetTickCount();
        while (!observer.Finished(localId) && ::GetTickCount() - start < waitMs)
            PumpOnce();
        if (!observer.Finished(localId))
            CIULog::Flush(kStlReadSliceMs);
        REQUIRE(observer.Finished(localId));
        return observer.Code(localId);
    }

    /**
     *  等一张共享卡片的推送（cmd_message_notify，msgtype 20，正文里的 shareid 等于给定值）。
     *  @param shareId 共享 id
     *  @param out     [出参] 卡片正文
     *  @return 等到返回 true
     */
    bool WaitShareCard(int64_t shareId, net::ShareCardContent& out)
    {
        const DWORD start = ::GetTickCount();
        while (::GetTickCount() - start < kSmlWaitMs)
        {
            PumpOnce();
            for (size_t i = 0; i < m_others.size(); ++i)
            {
                net::MessageNotify notify;
                if (m_others[i].m_cmd != cmd_message_notify
                    || !net::MessageProtocolClient::parseMessageNotify(m_others[i].m_body, notify)
                    || notify.m_msgtype != message_type_fileshare)
                    continue;
                if (net::MessageProtocolClient::parseShareCardContent(notify.m_shareContent, out) && out.m_shareId == shareId)
                    return true;
            }
        }
        return false;
    }

    StlHost       m_host;      //宿主
    CShareService m_service;   //被测的业务总控
    StlReader     m_reader;    //收帧线程
    HWND          m_wnd;       //消息窗口
    bool          m_attached;  //业务总控是否已 Attach（Shutdown 据此只做一次）
    bool          m_logInited; //本环境是否持有一份日志引用（Shutdown 时据此释放）
    static int    s_logRefs;   //持有日志引用的环境个数，只在测试主线程上读写
    std::vector<StlFrame> m_others;   //共享段以外的帧（共享卡片等消息推送），按到达顺序
};

int StlEnv::s_logRefs = 0;

//管理请求的观察者：记下最近一次结果
class SmlManageObserver : public IShareManageObserver
{
public:
    SmlManageObserver()
        : m_done(false)
    {
    }

    virtual void OnShareManageResult(const ShareManageResult& result) override
    {
        m_done = true;
        m_last = result;
    }

    bool              m_done;   //是否有了结果（每次发请求前复位）
    ShareManageResult m_last;   //最近一次结果
};

//模块桩：记下浮层请模块打开的共享
class SmlUiHost : public IShareUiHost
{
public:
    SmlUiHost()
        : m_openShareId(0)
        , m_previewSource(NULL)
    {
    }

    virtual void UiOpenCreateShare(HWND owner, int ctype, int64_t cid, bool openConversationAfter) override
    {
        (void)owner;
        (void)ctype;
        (void)cid;
        (void)openConversationAfter;
    }

    //联调用例不打开「文件共享」窗口
    virtual bool UiOpenShareCenter(HWND owner, int ctype, int64_t cid) override
    {
        (void)owner;
        (void)ctype;
        (void)cid;
        return false;
    }

    virtual void UiOpenShare(HWND owner, int64_t shareId) override
    {
        (void)owner;
        m_openShareId = shareId;
    }

    virtual void UiEditMembers(HWND owner, const ShareListEntry& share) override
    {
        (void)owner;
        (void)share;
    }

    virtual bool UiOpenImage(HWND owner, const std::wstring& path, IShareImagePreviewSource* source) override
    {
        (void)owner;
        m_openedImages.push_back(path);
        m_previewSource = source;
        return true;
    }

    virtual void UiPreviewSourceGone(IShareImagePreviewSource* source) override
    {
        if (m_previewSource == source)
            m_previewSource = NULL;
    }

    virtual bool UiStartDirectShare(HWND owner) override
    {
        (void)owner;
        return false;
    }

    int64_t m_openShareId;   //最近一次请求打开的共享
    std::vector<std::wstring>  m_openedImages;   //请求用看图窗打开过的预览图，按顺序
    IShareImagePreviewSource*  m_previewSource;  //最近一次预览的来源；来源销毁时置空
};

//发一次管理请求后等它的结果；requestId 为 0 时直接失败
int SmlWaitManage(StlEnv& env, SmlManageObserver& obs, uint32_t requestId)
{
    REQUIRE(requestId != 0);
    const DWORD start = ::GetTickCount();
    while (!obs.m_done && ::GetTickCount() - start < kSmlWaitMs)
        env.PumpOnce();
    REQUIRE(obs.m_done);
    REQUIRE(obs.m_last.m_requestId == requestId);
    return obs.m_last.m_code;
}

//文件内容的 MD5（十六进制）；读失败返回空串
std::string FileMd5(const std::wstring& path)
{
    HANDLE h = ::CreateFileW(ShareToLongPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return std::string();
    MD5_CTX ctx;
    MD5Init(&ctx);
    std::vector<unsigned char> buf(kStlReadChunk);
    DWORD got = 0;
    while (::ReadFile(h, &buf[0], kStlReadChunk, &got, NULL) && got > 0)
        MD5Update(&ctx, &buf[0], got);
    ::CloseHandle(h);
    unsigned char digest[kStlDigestBytes] = { 0 };
    MD5Final(digest, &ctx);
    static const char kHex[] = "0123456789abcdef";
    std::string hex;
    for (int i = 0; i < kStlDigestBytes; ++i)
    {
        hex.push_back(kHex[(digest[i] >> kStlNibbleBits) & kStlNibbleMask]);
        hex.push_back(kHex[digest[i] & kStlNibbleMask]);
    }
    return hex;
}

//把一个文件的前 bytes 字节复制成另一个文件
bool CopyPrefix(const std::wstring& from, const std::wstring& to, int64_t bytes)
{
    HANDLE in = ::CreateFileW(from.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (in == INVALID_HANDLE_VALUE)
        return false;
    HANDLE out = ::CreateFileW(to.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (out == INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(in);
        return false;
    }
    std::vector<unsigned char> buf(kStlReadChunk);
    int64_t left = bytes;
    bool ok = true;
    while (left > 0 && ok)
    {
        const DWORD want = static_cast<DWORD>(left < kStlReadChunk ? left : kStlReadChunk);
        DWORD got = 0;
        DWORD written = 0;
        ok = ::ReadFile(in, &buf[0], want, &got, NULL) && got == want && ::WriteFile(out, &buf[0], got, &written, NULL)
             && written == got;
        left -= got;
    }
    ::CloseHandle(in);
    ::CloseHandle(out);
    return ok;
}

//源文件的修改时间（Unix 毫秒）
int64_t FileMtimeMs(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return 0;
    return ShareFileOps::FileTimeToUnixMs(data.ftLastWriteTime);
}

//分享者 clientA 的日志目录
const wchar_t* const kStlOwnerLogDir = L"E:\\flamingo\\tmp\\fileshare_env\\clientA\\Logs";

//分享者发 big.bin 时从续传位点起传的日志片段
std::string OwnerOffsetMarker()
{
    return "offset=" + std::to_string(static_cast<long long>(kStlResumeBytes)) + ", path=data/big.bin";
}

//在分享者最新的日志文件里数某段文字出现的次数
int CountInOwnerLog(const std::string& marker)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW((std::wstring(kStlOwnerLogDir) + L"\\*.log").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    std::wstring newest;
    FILETIME newestTime = { 0, 0 };
    do
    {
        if (::CompareFileTime(&fd.ftLastWriteTime, &newestTime) > 0)
        {
            newestTime = fd.ftLastWriteTime;
            newest = fd.cFileName;
        }
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);

    HANDLE f = ::CreateFileW((std::wstring(kStlOwnerLogDir) + L"\\" + newest).c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    std::string content;
    std::vector<char> buf(kStlReadChunk);
    DWORD got = 0;
    while (::ReadFile(f, &buf[0], kStlReadChunk, &got, NULL) && got > 0)
        content.append(&buf[0], got);
    ::CloseHandle(f);
    int n = 0;
    for (size_t pos = content.find(marker); pos != std::string::npos; pos = content.find(marker, pos + 1))
        ++n;
    return n;
}

//一个下载条目：共享根下的 data 目录
std::vector<ShareTransferEntry> DataItem()
{
    ShareTransferEntry item;
    item.m_path = "data";
    item.m_isDir = true;
    return std::vector<ShareTransferEntry>(1, item);
}

//工作目录（每个用例开始前清空）
std::wstring FreshWork()
{
    const std::wstring work = sharetestfs::ExeDir() + L"\\" + kStlWorkDir;
    sharetestfs::DeleteTree(work);
    sharetestfs::MakeDirs(work + L"\\personal\\ChatLog");
    return work;
}

} // namespace

TEST_CASE("STL1 下载目录、顶层改名与断点续传", "[.live][fileshare]")
{
    const std::wstring work = FreshWork();
    const std::wstring root = kStlShareRoot;
    const std::wstring src = root + L"\\data";
    REQUIRE(::GetFileAttributesW((src + L"\\big.bin").c_str()) != INVALID_FILE_ATTRIBUTES);

    StlEnv env;
    env.Setup(work + L"\\personal\\");
    StlVisitorObserver vobs;
    env.OpenShare(vobs);
    StlTransferObserver tobs;

    //第一次下载：整个 data 目录
    const std::wstring target1 = work + L"\\t1";
    REQUIRE(sharetestfs::MakeDirs(target1));
    uint32_t id = env.m_service.StartDownload(kStlShareId, DataItem(), target1, &tobs);
    REQUIRE(id != 0);
    REQUIRE(env.WaitTransfer(tobs, id) == 0);
    CHECK(FileMd5(target1 + L"\\data\\a.txt") == FileMd5(src + L"\\a.txt"));
    CHECK(FileMd5(target1 + L"\\data\\sub\\b.bin") == FileMd5(src + L"\\sub\\b.bin"));
    CHECK(FileMd5(target1 + L"\\data\\big.bin") == FileMd5(src + L"\\big.bin"));
    CHECK(::GetFileAttributesW((target1 + L"\\data\\empty").c_str()) != INVALID_FILE_ATTRIBUTES);

    //再下载到同一个目录：顶层目录改名为 data(2)
    id = env.m_service.StartDownload(kStlShareId, DataItem(), target1, &tobs);
    REQUIRE(id != 0);
    REQUIRE(env.WaitTransfer(tobs, id) == 0);
    CHECK(FileMd5(target1 + L"\\data(2)\\sub\\b.bin") == FileMd5(src + L"\\sub\\b.bin"));

    //续传的准备：target3 里放好 big.bin 的前一段（.part）与续传记录。放在前两次下载之后：下载完成时会删除
    //同一共享、同一路径的续传记录，先写就会被前两次下载删掉
    const std::wstring target3 = work + L"\\t3";
    REQUIRE(sharetestfs::MakeDirs(target3 + L"\\data"));
    REQUIRE(CopyPrefix(src + L"\\big.bin", target3 + L"\\data\\big.bin.part", kStlResumeBytes));
    {
        CShareLocalStore store;
        REQUIRE(store.Open((work + L"\\personal\\ChatLog\\fileshare.db").c_str()));
        ShareDownloadRecord rec;
        rec.m_shareId = kStlShareId;
        rec.m_relPath = "data/big.bin";
        WIN32_FILE_ATTRIBUTE_DATA data;
        REQUIRE(::GetFileAttributesExW((src + L"\\big.bin").c_str(), GetFileExInfoStandard, &data));
        rec.m_srcSize = static_cast<int64_t>((static_cast<uint64_t>(data.nFileSizeHigh) << kStlHighShift) | data.nFileSizeLow);
        rec.m_srcMtime = FileMtimeMs(src + L"\\big.bin");
        rec.m_localPath = target3 + L"\\data\\big.bin";
        rec.m_offset = kStlResumeBytes;
        REQUIRE(store.SaveDownloadProgress(rec));
    }

    //续传：big.bin 从预先放好的位点接着下载
    id = env.m_service.StartDownload(kStlShareId, DataItem(), target3, &tobs);
    REQUIRE(id != 0);
    REQUIRE(env.WaitTransfer(tobs, id) == 0);
    //续传的证据：分享者发 big.bin 时从预先放好的位点起传（访问者一侧的引擎编在本测试进程里，日志不经宿主）
    CHECK(CountInOwnerLog(OwnerOffsetMarker()) >= 1);
    CHECK(FileMd5(target3 + L"\\data\\big.bin") == FileMd5(src + L"\\big.bin"));
    CHECK(::GetFileAttributesW((target3 + L"\\data\\big.bin.part").c_str()) == INVALID_FILE_ATTRIBUTES);
    //下载完成后续传记录删除
    {
        CShareLocalStore store;
        REQUIRE(store.Open((work + L"\\personal\\ChatLog\\fileshare.db").c_str()));
        std::vector<ShareDownloadRecord> left;
        REQUIRE(store.ListDownloads(kStlShareId, left));
        CHECK(left.empty());
    }
    env.Shutdown();
}

TEST_CASE("STL2 上传目录与同名拒绝", "[.live][fileshare]")
{
    const std::wstring work = FreshWork();
    const std::wstring root = kStlShareRoot;
    sharetestfs::DeleteTree(root + L"\\photos");
    const std::wstring local = work + L"\\up\\photos";
    REQUIRE(sharetestfs::MakeDirs(local + L"\\sub"));
    REQUIRE(sharetestfs::WriteFileBytes(local + L"\\p1.jpg", kStlUploadBig));
    REQUIRE(sharetestfs::WriteFileBytes(local + L"\\sub\\p2.jpg", kStlUploadSmall));

    StlEnv env;
    env.Setup(work + L"\\personal\\");
    StlVisitorObserver vobs;
    env.OpenShare(vobs);
    StlTransferObserver tobs;

    int err = 0;
    uint32_t id = env.m_service.StartUpload(kStlShareId, std::vector<std::wstring>(1, local), "", &tobs, err);
    REQUIRE(id != 0);
    REQUIRE(env.WaitTransfer(tobs, id) == 0);
    CHECK(FileMd5(root + L"\\photos\\p1.jpg") == FileMd5(local + L"\\p1.jpg"));
    CHECK(FileMd5(root + L"\\photos\\sub\\p2.jpg") == FileMd5(local + L"\\sub\\p2.jpg"));
    CHECK(::GetFileAttributesW((root + L"\\photos\\p1.jpg.part").c_str()) == INVALID_FILE_ATTRIBUTES);

    //同名再上传：分享者拒绝（807）
    id = env.m_service.StartUpload(kStlShareId, std::vector<std::wstring>(1, local), "", &tobs, err);
    REQUIRE(id != 0);
    CHECK(env.WaitTransfer(tobs, id) == error_code_share_name_conflict);

    env.Shutdown();
    sharetestfs::DeleteTree(root + L"\\photos");
}

TEST_CASE("STL3 浏览窗口里下载并显示传输列表", "[.live][fileshare]")
{
    const std::wstring work = FreshWork();
    StlEnv env;
    env.Setup(work + L"\\personal\\");

    CShareBrowserWindow window;
    REQUIRE(window.Open(NULL, &env.m_service, kStlShareId));
    const DWORD start = ::GetTickCount();
    while (window.GetEntryCount() == 0 && ::GetTickCount() - start < kStlOpenWaitMs)
        env.PumpOnce();
    REQUIRE(window.GetEntryCount() > 0);

    //data 目录在列表第一行（目录在前、按名称）
    const std::wstring target = work + L"\\ui";
    REQUIRE(sharetestfs::MakeDirs(target));
    REQUIRE(window.DownloadRow(0, target));
    REQUIRE(window.GetTransferRowCount() == 1);
    const DWORD begin = ::GetTickCount();
    while (window.GetTransferRowState(0) == 0 && ::GetTickCount() - begin < kStlTransferWaitMs)
        env.PumpOnce();
    CHECK(window.GetTransferRowState(0) == 1);
    for (int i = 0; i < kStlSettleRounds; ++i)
        env.PumpOnce();
    CHECK(sharesnapshot::SaveWindowPng(window.m_hWnd, sharetestfs::ExeDir() + kStlSnapshot));

    window.Close();
    env.Shutdown();
}

TEST_CASE("SML1 发起、调整与取消单聊共享，clientA 刷新卡片状态", "[.live][fileshare]")
{
    const std::wstring work = FreshWork();
    sharetestfs::DeleteTree(kSmlCreateRoot);
    REQUIRE(sharetestfs::MakeDirs(kSmlCreateRoot));
    REQUIRE(sharetestfs::WriteFileBytes(std::wstring(kSmlCreateRoot) + L"\\a.txt", kSmlFileBytes));

    StlEnv env;
    env.Setup(work + L"\\personal\\");

    //发起：与 MY0127 的单聊，共享一个文件夹，MY0127 有上传权限
    ShareCreateParams params;
    params.m_ctype = conversation_single;
    params.m_cid = kStlPeerUserId;
    params.m_kind = share_kind_folder;
    params.m_name = "share_create";
    params.m_displayPath = ShareWideToUtf8(kSmlCreateRoot);
    ShareMemberPerm peer;
    peer.m_userId = kStlPeerUserId;
    peer.m_perms = share_perm_upload;
    params.m_members.push_back(peer);
    SmlManageObserver obs;
    REQUIRE(SmlWaitManage(env, obs, env.m_service.CreateShare(params, kSmlCreateRoot, &obs)) == 0);
    const int64_t shareId = obs.m_last.m_shareId;
    REQUIRE(shareId > 0);
    CHECK(env.m_service.GetLocalStatus(shareId) == share_local_status_reported);

    //服务端发来的共享卡片：正文就是这个共享
    net::ShareCardContent card;
    REQUIRE(env.WaitShareCard(shareId, card));
    CHECK(card.m_kind == share_kind_folder);
    CHECK(card.m_name == "share_create");
    CHECK(card.m_ownerId == env.m_host.m_client.UserId());

    //改 MY0127 的权限：上传改为新建文件夹
    obs.m_done = false;
    ShareMemberPerm update;
    update.m_userId = kStlPeerUserId;
    update.m_perms = share_perm_mkdir;
    CHECK(SmlWaitManage(env, obs, env.m_service.UpdateMembers(shareId, std::vector<ShareMemberPerm>(),
                                                              std::vector<int64_t>(),
                                                              std::vector<ShareMemberPerm>(1, update), &obs)) == 0);
    //单聊里不能移除对方（要停止共享请取消）
    obs.m_done = false;
    CHECK(SmlWaitManage(env, obs, env.m_service.UpdateMembers(shareId, std::vector<ShareMemberPerm>(),
                                                              std::vector<int64_t>(1, kStlPeerUserId),
                                                              std::vector<ShareMemberPerm>(), &obs))
          == error_code_bad_request);

    //取消：本机记录删除；clientA 的文件共享模块把「已失效」通知给宿主，主面板逐窗刷新卡片
    const std::string notified = "file share card state changed, shareid=" + std::to_string(static_cast<long long>(shareId))
        + ", state=3";
    const std::string refreshed = "share card state refreshed in chat windows, shareid="
        + std::to_string(static_cast<long long>(shareId)) + ", state=3";
    obs.m_done = false;
    CHECK(SmlWaitManage(env, obs, env.m_service.CancelShare(shareId, &obs)) == 0);
    CHECK(env.m_service.GetLocalStatus(shareId) == 0);
    const DWORD start = ::GetTickCount();
    while ((CountInOwnerLog(notified) == 0 || CountInOwnerLog(refreshed) == 0) && ::GetTickCount() - start < kSmlWaitMs)
        env.PumpOnce();
    CHECK(CountInOwnerLog(notified) == 1);
    CHECK(CountInOwnerLog(refreshed) == 1);

    env.Shutdown();
}

TEST_CASE("SML2 本会话共享浮层列出真实服务端的共享", "[.live][fileshare]")
{
    const std::wstring work = FreshWork();
    StlEnv env;
    env.Setup(work + L"\\personal\\");

    SmlUiHost ui;
    CShareConversationPopup popup;
    REQUIRE(popup.Open(NULL, &env.m_service, &env.m_host, &ui, conversation_single, kStlPeerUserId, NULL));
    const DWORD start = ::GetTickCount();
    while (popup.GetStatusText().find(L"正在加载") != std::wstring::npos && ::GetTickCount() - start < kSmlWaitMs)
        env.PumpOnce();
    REQUIRE(popup.GetStatusText().find(L"正在加载") == std::wstring::npos);
    REQUIRE(popup.GetRowCount() > 0);

    //9006 是 MY0127 共享给本人的，操作是「打开」；所有行都属于这个单聊（分享者是对方或本人）
    size_t row = popup.GetRowCount();
    for (size_t i = 0; i < popup.GetRowCount(); ++i)
    {
        const ShareListEntry& e = popup.GetRow(i);
        CHECK((e.m_ownerId == kStlPeerUserId || e.m_ownerId == env.m_host.m_client.UserId()));
        if (e.m_shareId == kStlShareId)
            row = i;
    }
    REQUIRE(row < popup.GetRowCount());
    CHECK(popup.GetRowActions(row) == share_row_action_open);

    //列表应答顺带更新了卡片状态缓存
    ShareCardState state;
    REQUIRE(env.m_service.GetCardState(kStlShareId, state));
    CHECK(state.m_state == popup.GetRow(row).m_state);

    REQUIRE(popup.DoRowAction(row, share_row_action_open, false));
    CHECK(ui.m_openShareId == kStlShareId);
    popup.Close();
    env.Shutdown();
}

TEST_CASE("SML3 「文件共享」窗口列出真实服务端的共享", "[.live][fileshare]")
{
    const std::wstring work = FreshWork();
    StlEnv env;
    env.Setup(work + L"\\personal\\");

    SmlUiHost ui;
    CShareCenterWindow center;
    REQUIRE(center.Open(NULL, &env.m_service, &env.m_host, &ui));
    const DWORD start = ::GetTickCount();
    while (center.IsLoading() && ::GetTickCount() - start < kSmlWaitMs)
        env.PumpOnce();
    REQUIRE_FALSE(center.IsLoading());
    REQUIRE(center.GetRowCount(share_center_tab_shared) > 0);

    center.SetTab(share_center_tab_shared);
    int row = -1;
    for (size_t i = 0; i < center.GetRowCount(share_center_tab_shared); ++i)
    {
        if (center.GetRow(share_center_tab_shared, i).m_shareId == kStlShareId)
            row = static_cast<int>(i);
    }
    REQUIRE(row >= 0);
    center.SelectRow(row);
    CHECK(center.GetActionMask() == (share_center_action_open | share_center_action_locate));
    const std::vector<std::wstring> lines = center.GetDetailLines();
    bool hasPerms = false;
    bool hasConv = false;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        hasPerms = hasPerms || lines[i] == L"我的权限：下载、上传";
        hasConv = hasConv || lines[i] == L"所在会话：conv1-5127";
    }
    CHECK(hasPerms);
    CHECK(hasConv);

    REQUIRE(center.DoAction(share_center_action_locate, false));
    REQUIRE(!env.m_host.m_openedConversations.empty());
    CHECK(env.m_host.m_openedConversations.back().first == conversation_single);
    CHECK(env.m_host.m_openedConversations.back().second == kStlPeerUserId);
    REQUIRE(center.DoAction(share_center_action_open, false));
    CHECK(ui.m_openShareId == kStlShareId);
    center.Close();
    env.Shutdown();
}

// =====================================================================
// 第 18 步：第 1 期整体验收
// =====================================================================

namespace {

//clientA 的程序与目录
const wchar_t* const kSacClientExe = L"E:\\flamingo\\tmp\\fileshare_env\\clientA\\Flamingo.exe";
const wchar_t* const kSacClientDir = L"E:\\flamingo\\tmp\\fileshare_env\\clientA";
const wchar_t* const kSacClientDll = L"E:\\flamingo\\tmp\\fileshare_env\\clientA\\FlamingoFileShare.dll";
const wchar_t* const kSacClientDllAside = L"E:\\flamingo\\tmp\\fileshare_env\\clientA\\FlamingoFileShare.dll.sac";

//「中文资料」在 9006 共享根里的位置、规模（与 seed_accept.py 一致）
const char* const kSacCnDir = "中文资料";
const wchar_t* const kSacCnDirW = L"中文资料";
const size_t kSacCnFiles = 10000 + 2;   //一万个小文件 + 最深处的说明 + 大文件
const char* const kSacBigRel = "中文资料/大文件.bin";
const int64_t kSacBigBytes = 64LL * 1024 * 1024;

//群与共享（与 seed_accept.py 一致）
const int64_t kSacGroup0308 = 268435459;   //群主 MY0308，成员 5308/5309/5310，5310 是管理员
const int64_t kSacShareGroup = 9007;        //MY0127 在群 268435463 的共享：5308、5309 上传，5310 只能下载
const int64_t kSacShareOtherDevice = 9003;  //绑定另一台电脑的共享（seed_step12.py）
const int64_t kSacShareNoUpload = 9001;     //本人没有上传权限的共享（seed_step12.py）
const int64_t kSacUser0309 = 5309;
const int64_t kSacUser0310 = 5310;
const char* const kSacAccount0309 = "MY0309";
const char* const kSacAccount0310 = "MY0310";

//各步骤的最长等待（毫秒）：大目录下载、clientA 重新登录、等推送、确认收不到推送
const DWORD kSacBigWaitMs = 600000;
const DWORD kSacRestartWaitMs = 60000;
const DWORD kSacPushWaitMs = 15000;
const DWORD kSacQuietMs = 3000;

//下载大文件时等它传过这么多字节再让分享者掉线
const int64_t kSacInterruptBytes = 4LL * 1024 * 1024;

//SAC7 共享根里的大文件字节数：下载要持续一段时间，才来得及在下载中途移除成员
const size_t kSacMemberBigBytes = 96 * 1024 * 1024;

//进程控制：等进程退出的时长
const DWORD kSacKillWaitMs = 10000;

//clientA 的进程 id；没有运行时为 0
DWORD FindClientA()
{
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    DWORD found = 0;
    for (BOOL ok = ::Process32FirstW(snap, &pe); ok && found == 0; ok = ::Process32NextW(snap, &pe))
    {
        if (_wcsicmp(pe.szExeFile, L"Flamingo.exe") != 0)
            continue;
        HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (h == NULL)
            continue;
        wchar_t path[MAX_PATH] = { 0 };
        DWORD len = MAX_PATH;
        if (::QueryFullProcessImageNameW(h, 0, path, &len) && _wcsicmp(path, kSacClientExe) == 0)
            found = pe.th32ProcessID;
        ::CloseHandle(h);
    }
    ::CloseHandle(snap);
    return found;
}

//结束 clientA（模拟掉线：服务端看到连接断开）
bool KillClientA()
{
    const DWORD pid = FindClientA();
    if (pid == 0)
        return true;
    HANDLE h = ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (h == NULL)
        return false;
    const bool ok = ::TerminateProcess(h, 1) && ::WaitForSingleObject(h, kSacKillWaitMs) == WAIT_OBJECT_0;
    ::CloseHandle(h);
    return ok;
}

//启动 clientA（自动登录 MY0127），等它向服务端报告了 2210
bool StartClientAAndWait(const char* marker)
{
    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    std::wstring cmd = std::wstring(L"\"") + kSacClientExe + L"\"";
    if (!::CreateProcessW(kSacClientExe, &cmd[0], NULL, NULL, FALSE, 0, NULL, kSacClientDir, &si, &pi))
        return false;
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    //等新进程的日志文件出现并写出标记
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < kSacRestartWaitMs)
    {
        ::Sleep(kStlPumpSliceMs * kStlSettleRounds);
        if (CountInOwnerLog(marker) > 0)
            return true;
    }
    return false;
}

//挂起或恢复 clientA 的全部线程（模拟断电：连接还在，但不再有任何应答）
bool SuspendClientA(bool suspend)
{
    typedef LONG (NTAPI* SuspendFn)(HANDLE);
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    SuspendFn fn = (ntdll != NULL)
        ? reinterpret_cast<SuspendFn>(::GetProcAddress(ntdll, suspend ? "NtSuspendProcess" : "NtResumeProcess")) : NULL;
    const DWORD pid = FindClientA();
    if (fn == NULL || pid == 0)
        return false;
    HANDLE h = ::OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
    if (h == NULL)
        return false;
    const bool ok = fn(h) == 0;
    ::CloseHandle(h);
    return ok;
}

//分享者最新日志里，给定文件最后一次开始发送时的位点；没有找到返回 -1
int64_t LastOwnerOffset(const std::string& relPathInManifest)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW((std::wstring(kStlOwnerLogDir) + L"\\*.log").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    std::wstring newest;
    FILETIME newestTime = { 0, 0 };
    do
    {
        if (::CompareFileTime(&fd.ftLastWriteTime, &newestTime) > 0)
        {
            newestTime = fd.ftLastWriteTime;
            newest = fd.cFileName;
        }
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    HANDLE f = ::CreateFileW((std::wstring(kStlOwnerLogDir) + L"\\" + newest).c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return -1;
    std::string content;
    std::vector<char> buf(kStlReadChunk);
    DWORD got = 0;
    while (::ReadFile(f, &buf[0], kStlReadChunk, &got, NULL) && got > 0)
        content.append(&buf[0], got);
    ::CloseHandle(f);
    const std::string marker = ", path=" + relPathInManifest + ".";
    const size_t pos = content.rfind(marker);
    if (pos == std::string::npos)
        return -1;
    const size_t off = content.rfind("offset=", pos);
    if (off == std::string::npos)
        return -1;
    return _strtoi64(content.c_str() + off + strlen("offset="), NULL, 10);
}

//在本机目录下数文件个数（递归）
size_t CountFiles(const std::wstring& dir)
{
    size_t n = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW(ShareToLongPath(dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            n += CountFiles(dir + L"\\" + fd.cFileName);
        else
            ++n;
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    return n;
}

//同时抽取几个环境的消息
void PumpAll(StlEnv* const* envs, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        envs[i]->PumpOnce();
}

//一个下载条目（共享内的相对路径）
std::vector<ShareTransferEntry> Item(const char* path, bool isDir, int64_t size)
{
    ShareTransferEntry item;
    item.m_path = path;
    item.m_isDir = isDir;
    item.m_size = size;
    return std::vector<ShareTransferEntry>(1, item);
}

//一个群共享的发起参数：群 268435459，成员与权限由调用方给
ShareCreateParams SacGroupParams(const std::wstring& root, const std::vector<ShareMemberPerm>& members)
{
    ShareCreateParams p;
    p.m_ctype = conversation_group;
    p.m_cid = kSacGroup0308;
    p.m_kind = share_kind_folder;
    p.m_name = "sac_share";
    p.m_displayPath = ShareWideToUtf8(root);
    p.m_members = members;
    return p;
}

ShareMemberPerm Member(int64_t userId, int perms)
{
    ShareMemberPerm m;
    m.m_userId = userId;
    m.m_perms = perms;
    return m;
}

//等一个环境里的某个共享的卡片状态变成给定值
bool WaitCardState(StlEnv& env, int64_t shareId, int state, int reason)
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < kSacPushWaitMs)
    {
        env.PumpOnce();
        ShareCardState st;
        if (env.m_service.GetCardState(shareId, st) && st.m_state == state && st.m_reason == reason)
            return true;
    }
    return false;
}

//纯连接（不带业务总控）等一张共享卡片推送；waitMs 内没有返回 false
bool WaitCardOnClient(LiveChatClient& client, int64_t shareId, DWORD waitMs)
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < waitMs)
    {
        std::string body;
        if (!client.WaitFor(cmd_message_notify, body, kStlReadSliceMs))
            continue;
        net::MessageNotify notify;
        net::ShareCardContent card;
        if (net::MessageProtocolClient::parseMessageNotify(body, notify) && notify.m_msgtype == message_type_fileshare
            && net::MessageProtocolClient::parseShareCardContent(notify.m_shareContent, card) && card.m_shareId == shareId)
            return true;
    }
    return false;
}

//纯连接发一条共享信令并等它的应答，返回结果码；没有应答返回 -1
int ClientShareRequest(LiveChatClient& client, uint16_t cmd, const std::string& body)
{
    client.ClearBacklog();
    if (!client.Send(cmd, body))
        return -1;
    std::string resp;
    if (!client.WaitFor(cmd, resp, static_cast<int>(kSacPushWaitMs)))
        return -1;
    int code = -1;
    std::string msg;
    std::string accessId;
    if (cmd == cmd_share_open)
        return ShareProtocolCodec::ParseOpenResponse(resp, code, msg, accessId) ? code : -1;
    return ShareProtocolCodec::ParseSimpleResponse(resp, code, msg) ? code : -1;
}

//等一次管理请求的结果
int SacManage(StlEnv& env, SmlManageObserver& obs, uint32_t requestId)
{
    obs.m_done = false;
    return SmlWaitManage(env, obs, requestId);
}

} // namespace

TEST_CASE("SAC1 中文名、深目录、一万个小文件与大文件的目录下载", "[.live][fileshare][acceptance]")
{
    const std::wstring work = FreshWork();
    StlEnv env;
    env.Setup(work + L"\\personal\\");
    StlVisitorObserver vobs;
    CShareVisitor* visitor = env.OpenShare(vobs);
    (void)visitor;
    bool hasCn = false;
    for (size_t i = 0; i < vobs.m_entries.size(); ++i)
        hasCn = hasCn || (vobs.m_entries[i].m_name == kSacCnDir && vobs.m_entries[i].m_isDir);
    REQUIRE(hasCn);

    const std::wstring target = work + L"\\sac1";
    REQUIRE(sharetestfs::MakeDirs(target));
    StlTransferObserver tobs;
    const uint32_t id = env.m_service.StartDownload(kStlShareId, Item(kSacCnDir, true, 0), target, &tobs);
    REQUIRE(id != 0);
    CHECK(env.WaitTransfer(tobs, id, kSacBigWaitMs) == 0);
    const std::wstring local = target + L"\\" + kSacCnDirW;
    CHECK(CountFiles(local) == kSacCnFiles);
    CHECK(FileMd5(local + L"\\大文件.bin") == FileMd5(std::wstring(kStlShareRoot) + L"\\中文资料\\大文件.bin"));
    std::wstring deep = local;
    for (int level = 1; level <= 8; ++level)
        deep += L"\\第" + std::to_wstring(level) + L"层";
    CHECK(::GetFileAttributesW(ShareToLongPath(deep + L"\\最深处的说明.txt").c_str()) != INVALID_FILE_ATTRIBUTES);
    CHECK(FileMd5(local + L"\\小文件\\文件09999.txt")
          == FileMd5(std::wstring(kStlShareRoot) + L"\\中文资料\\小文件\\文件09999.txt"));
    env.Shutdown();
}

TEST_CASE("SAC2 群聊共享给部分成员", "[.live][fileshare][acceptance]")
{
    const std::wstring work = FreshWork();
    StlEnv owner;
    owner.Setup(work + L"\\personal\\");
    LiveChatClient member;
    LiveChatClient outsider;
    std::string reason;
    const bool memberLoggedIn = member.ConnectAndLogin(kStlChatPort, kTestServerPubKey, kSacAccount0309, kStlPassword, reason);
    INFO("login failed: " << reason);
    REQUIRE(memberLoggedIn);
    const bool outsiderLoggedIn = outsider.ConnectAndLogin(kStlChatPort, kTestServerPubKey, kSacAccount0310, kStlPassword, reason);
    INFO("login failed: " << reason);
    REQUIRE(outsiderLoggedIn);
    member.ClearBacklog();
    outsider.ClearBacklog();

    const std::wstring root = work + L"\\sac2root";
    REQUIRE(sharetestfs::MakeDirs(root));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\a.txt", kSmlFileBytes));
    SmlManageObserver obs;
    REQUIRE(SacManage(owner, obs, owner.m_service.CreateShare(
        SacGroupParams(root, std::vector<ShareMemberPerm>(1, Member(kSacUser0309, 0))), root, &obs)) == 0);
    const int64_t shareId = obs.m_last.m_shareId;

    //被选中的成员收到卡片；未被选中的收不到，打开被拒，「共享给我的」里也没有
    CHECK(WaitCardOnClient(member, shareId, kSacPushWaitMs));
    CHECK_FALSE(WaitCardOnClient(outsider, shareId, kSacQuietMs));
    CHECK(ClientShareRequest(outsider, cmd_share_open, ShareProtocolCodec::BuildShareIdRequest(shareId))
          == error_code_share_not_member);
    outsider.ClearBacklog();
    REQUIRE(outsider.Send(cmd_share_list, ShareProtocolCodec::BuildListRequest(kShareListScopeSharedMe, 0, 0)));
    std::string listBody;
    REQUIRE(outsider.WaitFor(cmd_share_list, listBody, static_cast<int>(kSacPushWaitMs)));
    ShareListResponse list;
    REQUIRE(ShareProtocolCodec::ParseListResponse(listBody, list));
    for (size_t i = 0; i < list.m_shares.size(); ++i)
        CHECK(list.m_shares[i].m_shareId != shareId);

    REQUIRE(SacManage(owner, obs, owner.m_service.CancelShare(shareId, &obs)) == 0);
    member.Close();
    outsider.Close();
    owner.Shutdown();
}

TEST_CASE("SAC3 下载中途分享者掉线与断点续传", "[.live][fileshare][acceptance]")
{
    const std::wstring work = FreshWork();
    const std::wstring target = work + L"\\sac3";
    REQUIRE(sharetestfs::MakeDirs(target));
    {
        StlEnv env;
        env.Setup(work + L"\\personal\\");
        StlVisitorObserver vobs;
        env.OpenShare(vobs);
        StlTransferObserver tobs;
        const uint32_t id = env.m_service.StartDownload(kStlShareId, Item(kSacBigRel, false, kSacBigBytes), target, &tobs);
        REQUIRE(id != 0);
        const DWORD start = ::GetTickCount();
        while (tobs.Progress(id) < kSacInterruptBytes && !tobs.Finished(id) && ::GetTickCount() - start < kStlTransferWaitMs)
            env.PumpOnce();
        REQUIRE_FALSE(tobs.Finished(id));
        REQUIRE(KillClientA());
        //传输中止，卡片变为暂不可用（分享者不在线）
        CHECK(env.WaitTransfer(tobs, id) != 0);
        CHECK(WaitCardState(env, kStlShareId, share_state_unavailable, share_unavailable_owner_offline));
        env.Shutdown();
    }
    //留下了续传记录（相当于访问者也重启了一次：环境整个销毁，再用同一个个人目录新建）
    {
        CShareLocalStore store;
        REQUIRE(store.Open((work + L"\\personal\\ChatLog\\fileshare.db").c_str()));
        std::vector<ShareDownloadRecord> recs;
        REQUIRE(store.ListDownloads(kStlShareId, recs));
        REQUIRE(recs.size() == 1);
        CHECK(recs[0].m_relPath == kSacBigRel);
        CHECK(recs[0].m_offset > 0);
    }
    REQUIRE(StartClientAAndWait("cmd: 2210, seq"));
    {
        StlEnv env;
        env.Setup(work + L"\\personal\\");
        StlVisitorObserver vobs;
        env.OpenShare(vobs);
        StlTransferObserver tobs;
        const uint32_t id = env.m_service.StartDownload(kStlShareId, Item(kSacBigRel, false, kSacBigBytes), target, &tobs);
        REQUIRE(id != 0);
        CHECK(env.WaitTransfer(tobs, id) == 0);
        CHECK(LastOwnerOffset("大文件.bin") > 0);
        CHECK(FileMd5(target + L"\\大文件.bin") == FileMd5(std::wstring(kStlShareRoot) + L"\\中文资料\\大文件.bin"));
        env.Shutdown();
    }
}

TEST_CASE("SAC4 分享者无响应时打开超时", "[.live][fileshare][acceptance]")
{
    const std::wstring work = FreshWork();
    StlEnv env;
    env.Setup(work + L"\\personal\\");
    REQUIRE(SuspendClientA(true));
    StlVisitorObserver vobs;
    CShareVisitor* visitor = env.m_service.AcquireVisitor(kSacShareNoUpload, &vobs);
    REQUIRE(visitor != NULL);
    visitor->List("", static_cast<int64_t>(::GetTickCount64()));
    const DWORD start = ::GetTickCount();
    while (vobs.m_listedCount == 0 && ::GetTickCount() - start < kStlOpenWaitMs + kSacPushWaitMs)
        env.PumpOnce();
    SuspendClientA(false);
    REQUIRE(vobs.m_listedCount > 0);
    CHECK(vobs.m_lastCode == error_code_share_owner_timeout);
    env.Shutdown();
}

TEST_CASE("SAC5 上传权限与两人同时上传同名目录", "[.live][fileshare][acceptance]")
{
    const std::wstring work = FreshWork();
    const std::wstring local = work + L"\\up\\同名目录";
    REQUIRE(sharetestfs::MakeDirs(local));
    REQUIRE(sharetestfs::WriteFileBytes(local + L"\\x.bin", kStlUploadBig));

    StlEnv a;
    a.Setup(work + L"\\personal\\");
    //没有上传权限：被拒
    StlVisitorObserver vobs1;
    CShareVisitor* v1 = a.m_service.AcquireVisitor(kSacShareNoUpload, &vobs1);
    REQUIRE(v1 != NULL);
    v1->List("", static_cast<int64_t>(::GetTickCount64()));
    DWORD start = ::GetTickCount();
    while (vobs1.m_listedCount == 0 && ::GetTickCount() - start < kStlOpenWaitMs)
        a.PumpOnce();
    StlTransferObserver tobs;
    int err = 0;
    const uint32_t denied = a.m_service.StartUpload(kSacShareNoUpload, std::vector<std::wstring>(1, local), "", &tobs, err);
    if (denied != 0)
        CHECK(a.WaitTransfer(tobs, denied) == error_code_share_permission_denied);
    else
        CHECK(err == error_code_share_permission_denied);
    a.m_service.ReleaseVisitor(kSacShareNoUpload);

    //两人同时上传同名目录到 9007：一个成功，一个被拒（807）
    StlEnv b;
    b.Setup(work + L"\\personal_b\\", kSacAccount0309);
    StlEnv* envs[] = { &a, &b };
    StlVisitorObserver va;
    StlVisitorObserver vb;
    CShareVisitor* pa = a.m_service.AcquireVisitor(kSacShareGroup, &va);
    CShareVisitor* pb = b.m_service.AcquireVisitor(kSacShareGroup, &vb);
    REQUIRE((pa != NULL && pb != NULL));
    pa->List("", static_cast<int64_t>(::GetTickCount64()));
    pb->List("", static_cast<int64_t>(::GetTickCount64()));
    start = ::GetTickCount();
    while ((va.m_listedCount == 0 || vb.m_listedCount == 0) && ::GetTickCount() - start < kStlOpenWaitMs)
        PumpAll(envs, 2);
    REQUIRE(va.m_lastCode == 0);
    REQUIRE(vb.m_lastCode == 0);
    StlTransferObserver ta;
    StlTransferObserver tb;
    int ea = 0;
    int eb = 0;
    const uint32_t ia = a.m_service.StartUpload(kSacShareGroup, std::vector<std::wstring>(1, local), "", &ta, ea);
    const uint32_t ib = b.m_service.StartUpload(kSacShareGroup, std::vector<std::wstring>(1, local), "", &tb, eb);
    REQUIRE((ia != 0 && ib != 0));
    start = ::GetTickCount();
    while ((!ta.Finished(ia) || !tb.Finished(ib)) && ::GetTickCount() - start < kStlTransferWaitMs)
        PumpAll(envs, 2);
    REQUIRE((ta.Finished(ia) && tb.Finished(ib)));
    const int ca = ta.Code(ia);
    const int cb = tb.Code(ib);
    CHECK(((ca == 0 && cb == error_code_share_name_conflict) || (cb == 0 && ca == error_code_share_name_conflict)));
    CHECK(FileMd5(L"E:\\flamingo\\tmp\\fileshare_env\\share_group\\同名目录\\x.bin") == FileMd5(local + L"\\x.bin"));
    b.Shutdown();
    a.Shutdown();
}

TEST_CASE("SAC6 分享者重新登录与另一台电脑", "[.live][fileshare][acceptance]")
{
    const std::wstring work = FreshWork();
    StlEnv env;
    env.Setup(work + L"\\personal\\");
    REQUIRE(KillClientA());
    //分享者不在线：9006 暂不可用
    CHECK(WaitCardState(env, kStlShareId, share_state_unavailable, share_unavailable_owner_offline));
    REQUIRE(StartClientAAndWait("cmd: 2210, seq"));
    //重新登录后恢复可用；绑定另一台电脑的 9003 显示「位于另一台电脑」
    CHECK(WaitCardState(env, kStlShareId, share_state_available, 0));
    SmlUiHost ui;
    CShareConversationPopup popup;
    REQUIRE(popup.Open(NULL, &env.m_service, &env.m_host, &ui, conversation_single, kStlPeerUserId, NULL));
    const DWORD start = ::GetTickCount();
    while (popup.GetStatusText().find(L"正在加载") != std::wstring::npos && ::GetTickCount() - start < kSacPushWaitMs)
        env.PumpOnce();
    bool otherDevice = false;
    for (size_t i = 0; i < popup.GetRowCount(); ++i)
    {
        const ShareListEntry& e = popup.GetRow(i);
        if (e.m_shareId == kSacShareOtherDevice)
            otherDevice = (e.m_state == share_state_unavailable && e.m_reason == share_unavailable_other_device);
    }
    CHECK(otherDevice);
    popup.Close();
    env.Shutdown();
}

TEST_CASE("SAC7 追加、移除成员与调整权限", "[.live][fileshare][acceptance]")
{
    const std::wstring work = FreshWork();
    const std::wstring root = work + L"\\sac7root";
    REQUIRE(sharetestfs::MakeDirs(root));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\big.bin", kSacMemberBigBytes));
    StlEnv owner;
    owner.Setup(work + L"\\personal\\");
    StlEnv visitor;
    visitor.Setup(work + L"\\personal_b\\", kSacAccount0309);
    LiveChatClient late;
    std::string reason;
    const bool lateLoggedIn = late.ConnectAndLogin(kStlChatPort, kTestServerPubKey, kSacAccount0310, kStlPassword, reason);
    INFO("login failed: " << reason);
    REQUIRE(lateLoggedIn);
    late.ClearBacklog();
    StlEnv* envs[] = { &owner, &visitor };

    SmlManageObserver obs;
    REQUIRE(SacManage(owner, obs, owner.m_service.CreateShare(
        SacGroupParams(root, std::vector<ShareMemberPerm>(1, Member(kSacUser0309, 0))), root, &obs)) == 0);
    const int64_t shareId = obs.m_last.m_shareId;
    //等成员一端收到「可用」再打开：分享者在 2201 应答之后才发 2210 报告可提供，成员抢在这之前打开会得到 804
    //（第 18 步验收实测两者相差约 20 毫秒）；界面上成员也是看到卡片显示可用之后才会点开
    REQUIRE(WaitCardState(visitor, shareId, share_state_available, 0));

    //成员打开共享
    StlVisitorObserver vobs;
    CShareVisitor* v = visitor.m_service.AcquireVisitor(shareId, &vobs);
    REQUIRE(v != NULL);
    v->List("", static_cast<int64_t>(::GetTickCount64()));
    DWORD start = ::GetTickCount();
    while (vobs.m_listedCount == 0 && ::GetTickCount() - start < kStlOpenWaitMs)
        PumpAll(envs, 2);
    REQUIRE(vobs.m_lastCode == 0);
    CHECK(v->GetPerms() == 0);

    //调整权限：访问者即时收到上传权限
    REQUIRE(SacManage(owner, obs, owner.m_service.UpdateMembers(shareId, std::vector<ShareMemberPerm>(),
        std::vector<int64_t>(), std::vector<ShareMemberPerm>(1, Member(kSacUser0309, share_perm_upload)), &obs)) == 0);
    start = ::GetTickCount();
    while (v->GetPerms() != share_perm_upload && ::GetTickCount() - start < kSacPushWaitMs)
        PumpAll(envs, 2);
    CHECK(v->GetPerms() == share_perm_upload);

    //追加成员：新成员收到新卡片
    REQUIRE(SacManage(owner, obs, owner.m_service.UpdateMembers(shareId,
        std::vector<ShareMemberPerm>(1, Member(kSacUser0310, 0)), std::vector<int64_t>(), std::vector<ShareMemberPerm>(),
        &obs)) == 0);
    CHECK(WaitCardOnClient(late, shareId, kSacPushWaitMs));

    //下载中途移除成员：传输中止，卡片已失效（成员被移除）
    const std::wstring target = work + L"\\sac7";
    REQUIRE(sharetestfs::MakeDirs(target));
    StlTransferObserver tobs;
    const uint32_t id = visitor.m_service.StartDownload(shareId, Item("big.bin", false, kSacMemberBigBytes), target, &tobs);
    REQUIRE(id != 0);
    start = ::GetTickCount();
    while (tobs.Progress(id) == 0 && !tobs.Finished(id) && ::GetTickCount() - start < kStlTransferWaitMs)
        PumpAll(envs, 2);
    REQUIRE_FALSE(tobs.Finished(id));
    REQUIRE(SacManage(owner, obs, owner.m_service.UpdateMembers(shareId, std::vector<ShareMemberPerm>(),
        std::vector<int64_t>(1, kSacUser0309), std::vector<ShareMemberPerm>(), &obs)) == 0);
    start = ::GetTickCount();
    while (!tobs.Finished(id) && ::GetTickCount() - start < kStlTransferWaitMs)
        PumpAll(envs, 2);
    REQUIRE(tobs.Finished(id));
    CHECK(tobs.Code(id) != 0);
    CHECK(WaitCardState(visitor, shareId, share_state_invalid, share_invalid_member_removed));

    REQUIRE(SacManage(owner, obs, owner.m_service.CancelShare(shareId, &obs)) == 0);
    late.Close();
    visitor.Shutdown();
    owner.Shutdown();
}

TEST_CASE("SAC8 分享者、群主、群管理员取消的原因", "[.live][fileshare][acceptance]")
{
    const std::wstring work = FreshWork();
    const std::wstring root = work + L"\\sac8root";
    REQUIRE(sharetestfs::MakeDirs(root));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\a.txt", kSmlFileBytes));
    StlEnv groupOwner;   //MY0308：群主
    groupOwner.Setup(work + L"\\personal\\");
    StlEnv sharer;       //MY0309：分享者
    sharer.Setup(work + L"\\personal_b\\", kSacAccount0309);
    LiveChatClient admin;   //MY0310：群管理员
    std::string reason;
    const bool adminLoggedIn = admin.ConnectAndLogin(kStlChatPort, kTestServerPubKey, kSacAccount0310, kStlPassword, reason);
    INFO("login failed: " << reason);
    REQUIRE(adminLoggedIn);
    SmlManageObserver obs;

    //群主取消别人的共享：原因为群主或管理员取消
    REQUIRE(SacManage(sharer, obs, sharer.m_service.CreateShare(
        SacGroupParams(root, std::vector<ShareMemberPerm>(1, Member(5308, 0))), root, &obs)) == 0);
    int64_t shareId = obs.m_last.m_shareId;
    REQUIRE(SacManage(groupOwner, obs, groupOwner.m_service.CancelShare(shareId, &obs)) == 0);
    CHECK(WaitCardState(sharer, shareId, share_state_invalid, share_invalid_group_admin_canceled));
    CHECK(sharer.m_service.GetLocalStatus(shareId) == 0);

    //群管理员取消
    REQUIRE(SacManage(sharer, obs, sharer.m_service.CreateShare(
        SacGroupParams(root, std::vector<ShareMemberPerm>(1, Member(5308, 0))), root, &obs)) == 0);
    shareId = obs.m_last.m_shareId;
    CHECK(ClientShareRequest(admin, cmd_share_cancel, ShareProtocolCodec::BuildShareIdRequest(shareId)) == 0);
    CHECK(WaitCardState(sharer, shareId, share_state_invalid, share_invalid_group_admin_canceled));
    CHECK(WaitCardState(groupOwner, shareId, share_state_invalid, share_invalid_group_admin_canceled));

    //分享者自己取消
    REQUIRE(SacManage(sharer, obs, sharer.m_service.CreateShare(
        SacGroupParams(root, std::vector<ShareMemberPerm>(1, Member(5308, 0))), root, &obs)) == 0);
    shareId = obs.m_last.m_shareId;
    REQUIRE(SacManage(sharer, obs, sharer.m_service.CancelShare(shareId, &obs)) == 0);
    CHECK(WaitCardState(groupOwner, shareId, share_state_invalid, share_invalid_owner_canceled));

    admin.Close();
    sharer.Shutdown();
    groupOwner.Shutdown();
}

TEST_CASE("SAC9 同一共享同时下载的人数上限", "[.live][fileshare][acceptance]")
{
    const std::wstring work = FreshWork();
    StlEnv a;
    a.Setup(work + L"\\personal\\");
    StlEnv b;
    b.Setup(work + L"\\personal_b\\", kSacAccount0309);
    StlEnv c;
    c.Setup(work + L"\\personal_c\\", kSacAccount0310);
    StlEnv* envs[] = { &a, &b, &c };
    StlVisitorObserver vo[3];
    for (size_t i = 0; i < 3; ++i)
    {
        CShareVisitor* v = envs[i]->m_service.AcquireVisitor(kSacShareGroup, &vo[i]);
        REQUIRE(v != NULL);
        v->List("", static_cast<int64_t>(::GetTickCount64()));
    }
    DWORD start = ::GetTickCount();
    while ((vo[0].m_listedCount == 0 || vo[1].m_listedCount == 0 || vo[2].m_listedCount == 0)
           && ::GetTickCount() - start < kStlOpenWaitMs)
        PumpAll(envs, 3);
    for (size_t i = 0; i < 3; ++i)
        REQUIRE(vo[i].m_lastCode == 0);

    //前两人开始下载大文件（上限 2），第三人被拒
    StlTransferObserver to[3];
    uint32_t ids[3] = { 0, 0, 0 };
    for (size_t i = 0; i < 3; ++i)
    {
        const std::wstring target = work + L"\\sac9_" + std::to_wstring(i);
        REQUIRE(sharetestfs::MakeDirs(target));
        ids[i] = envs[i]->m_service.StartDownload(kSacShareGroup, Item("big.bin", false, 0), target, &to[i]);
        REQUIRE(ids[i] != 0);
        //等这一笔拿到传输子会话（2224 应答）再发下一笔，让服务端按顺序计数
        start = ::GetTickCount();
        while (to[i].Progress(ids[i]) == 0 && !to[i].Finished(ids[i]) && ::GetTickCount() - start < kStlOpenWaitMs)
            PumpAll(envs, 3);
    }
    start = ::GetTickCount();
    while ((!to[0].Finished(ids[0]) || !to[1].Finished(ids[1]) || !to[2].Finished(ids[2]))
           && ::GetTickCount() - start < kStlTransferWaitMs)
        PumpAll(envs, 3);
    CHECK(to[0].Code(ids[0]) == 0);
    CHECK(to[1].Code(ids[1]) == 0);
    CHECK(to[2].Code(ids[2]) == error_code_share_transfer_busy);
    c.Shutdown();
    b.Shutdown();
    a.Shutdown();
}

TEST_CASE("SAC10 没有文件共享模块时客户端照常运行", "[.live][fileshare][acceptance]")
{
    REQUIRE(KillClientA());
    REQUIRE(::MoveFileExW(kSacClientDll, kSacClientDllAside, MOVEFILE_REPLACE_EXISTING));
    const bool started = StartClientAAndWait("file share module unavailable");
    REQUIRE(KillClientA());
    REQUIRE(::MoveFileExW(kSacClientDllAside, kSacClientDll, MOVEFILE_REPLACE_EXISTING));
    CHECK(started);
    //恢复原状：带模块重新启动，等它报告 2210
    CHECK(StartClientAAndWait("cmd: 2210, seq"));
}

namespace {

//SWL1：带全部写权限的共享（tmp/fileshare_env/seed_p2_write.py 准备）、它的共享根、没有写权限的共享
const int64_t kSwlShareId = 9008;
const wchar_t* const kSwlRoot = L"E:\\flamingo\\tmp\\fileshare_env\\share_write";
const int64_t kSwlNoWriteShareId = 9001;

//SWL1 每次重建共享根时写入的文件字节数
const size_t kSwlSeedFileBytes = 8;

//共享根所在卷的回收站目录；回收站条目是一对文件：$I<id> 记原路径，$R<id> 是内容本身
const wchar_t* const kSwlRecycleBin = L"E:\\$Recycle.Bin";

//$I 文件第 2 版的布局：8 字节版本、8 字节大小、8 字节删除时间、4 字节路径字符数，之后是 UTF-16 路径
const int64_t kRecycleInfoVersion2 = 2;
const size_t kRecycleInfoPathCountOffset = 24;
const size_t kRecycleInfoPathOffset = 28;

/**
 *  在 E: 盘的回收站里找原路径以 prefix 开头的条目（不区分大小写），可选地把它们删掉（只删这些，用户自己的条目不动）。
 *  @param prefix 原路径前缀
 *  @param remove 为真时删除找到的条目
 *  @return 找到的条目的原路径
 */
std::vector<std::wstring> RecycledUnder(const std::wstring& prefix, bool remove)
{
    std::vector<std::wstring> found;
    std::wstring lowerPrefix = prefix;
    ::CharLowerBuffW(&lowerPrefix[0], static_cast<DWORD>(lowerPrefix.size()));
    WIN32_FIND_DATAW sidData;
    HANDLE sids = ::FindFirstFileW((std::wstring(kSwlRecycleBin) + L"\\*").c_str(), &sidData);
    if (sids == INVALID_HANDLE_VALUE)
        return found;
    do
    {
        const std::wstring sid(sidData.cFileName);
        if (sid == L"." || sid == L".." || (sidData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            continue;
        const std::wstring dir = std::wstring(kSwlRecycleBin) + L"\\" + sid;
        WIN32_FIND_DATAW fd;
        HANDLE h = ::FindFirstFileW((dir + L"\\$I*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        std::vector<std::wstring> infoNames;
        do
        {
            infoNames.push_back(fd.cFileName);
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
        for (size_t i = 0; i < infoNames.size(); ++i)
        {
            const std::wstring info = dir + L"\\" + infoNames[i];
            HANDLE f = ::CreateFileW(info.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (f == INVALID_HANDLE_VALUE)
                continue;
            std::vector<char> buf(kRecycleInfoPathOffset + MAX_PATH * sizeof(wchar_t) * 2, 0);
            DWORD got = 0;
            ::ReadFile(f, &buf[0], static_cast<DWORD>(buf.size()), &got, NULL);
            ::CloseHandle(f);
            int64_t version = 0;
            memcpy(&version, &buf[0], sizeof(version));
            if (version != kRecycleInfoVersion2 || got <= kRecycleInfoPathOffset)
                continue;
            int32_t chars = 0;
            memcpy(&chars, &buf[kRecycleInfoPathCountOffset], sizeof(chars));
            const size_t maxChars = (got - kRecycleInfoPathOffset) / sizeof(wchar_t);
            std::wstring original(reinterpret_cast<const wchar_t*>(&buf[kRecycleInfoPathOffset]),
                                  (chars > 0 && static_cast<size_t>(chars) <= maxChars) ? static_cast<size_t>(chars) : 0);
            while (!original.empty() && original[original.size() - 1] == L'\0')
                original.erase(original.size() - 1);
            std::wstring lower = original;
            if (!lower.empty())
                ::CharLowerBuffW(&lower[0], static_cast<DWORD>(lower.size()));
            if (lower.compare(0, lowerPrefix.size(), lowerPrefix) != 0)
                continue;
            found.push_back(original);
            if (remove)
            {
                sharetestfs::DeleteTree(dir + L"\\$R" + infoNames[i].substr(2));
                ::DeleteFileW(info.c_str());
            }
        }
    } while (::FindNextFileW(sids, &sidData));
    ::FindClose(sids);
    return found;
}

//等状态栏出现某段文字；waitMs 内没有返回 false
bool WaitStatus(StlEnv& env, CShareBrowserWindow& window, const wchar_t* text, DWORD waitMs)
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < waitMs)
    {
        env.PumpOnce();
        if (window.GetStatusText().find(text) != std::wstring::npos)
            return true;
    }
    return false;
}

bool Exists(const std::wstring& path)
{
    return ::GetFileAttributesW(ShareToLongPath(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

} // namespace

TEST_CASE("SWL1 浏览窗口里新建、重命名、移动、删除", "[.live][fileshare][sharewrite]")
{
    const std::wstring work = FreshWork();
    const std::wstring root = kSwlRoot;
    //上次运行留在回收站里的本用例条目先清掉
    RecycledUnder(root + L"\\", true);
    //共享根的内容每次重建（与 seed_p2_write.py 相同），用例可以反复运行；分享者按本机记录找到的仍是这个目录
    sharetestfs::DeleteTree(root);
    REQUIRE(sharetestfs::MakeDirs(root + L"\\docs"));
    REQUIRE(sharetestfs::MakeDirs(root + L"\\dst"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\a.txt", kSwlSeedFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\docs\\b.txt", kSwlSeedFileBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\old.txt", kSwlSeedFileBytes));
    StlEnv env;
    env.Setup(work + L"\\personal\\");

    CShareBrowserWindow window;
    REQUIRE(window.Open(NULL, &env.m_service, kSwlShareId));
    DWORD start = ::GetTickCount();
    while (window.GetEntryCount() == 0 && ::GetTickCount() - start < kStlOpenWaitMs)
        env.PumpOnce();
    REQUIRE(window.GetEntryCount() == 3);
    CHECK(window.IsNewFolderButtonVisible());

    //新建文件夹
    REQUIRE(window.MakeFolder(L"新建的文件夹"));
    CHECK(WaitStatus(env, window, L"已新建文件夹", kSacPushWaitMs));
    CHECK(Exists(root + L"\\新建的文件夹"));
    CHECK(window.FindRow(L"新建的文件夹") >= 0);
    //同名再建：被拒
    REQUIRE(window.MakeFolder(L"新建的文件夹"));
    CHECK(WaitStatus(env, window, L"新建文件夹失败", kSacPushWaitMs));

    //重命名
    REQUIRE(window.RenameRow(window.FindRow(L"old.txt"), L"renamed.txt"));
    CHECK(WaitStatus(env, window, L"已重命名", kSacPushWaitMs));
    CHECK(Exists(root + L"\\renamed.txt"));
    CHECK_FALSE(Exists(root + L"\\old.txt"));

    //移动到 dst
    REQUIRE(window.MoveRow(window.FindRow(L"renamed.txt"), "dst"));
    CHECK(WaitStatus(env, window, L"已移动 1 项", kSacPushWaitMs));
    CHECK(Exists(root + L"\\dst\\renamed.txt"));
    //把 dst 移到它自己里面：被拒
    REQUIRE(window.MoveRow(window.FindRow(L"dst"), "dst"));
    CHECK(WaitStatus(env, window, L"移动失败", kSacPushWaitMs));

    //删除：放进分享者电脑（同一台机器的 E: 盘）的回收站
    REQUIRE(window.DeleteRow(window.FindRow(L"docs")));
    CHECK(WaitStatus(env, window, L"已删除 1 项", kSacPushWaitMs));
    CHECK_FALSE(Exists(root + L"\\docs"));
    const std::vector<std::wstring> recycled = RecycledUnder(root + L"\\docs", false);
    REQUIRE(recycled.size() == 1);
    CHECK(recycled[0] == root + L"\\docs");
    //只清理本用例放进去的条目
    RecycledUnder(root + L"\\", true);
    CHECK(RecycledUnder(root + L"\\", false).empty());
    window.Close();

    //没有写权限的共享：新建被拒（806），工具栏不显示「新建文件夹」
    CShareBrowserWindow readOnly;
    REQUIRE(readOnly.Open(NULL, &env.m_service, kSwlNoWriteShareId));
    start = ::GetTickCount();
    while (readOnly.GetEntryCount() == 0 && ::GetTickCount() - start < kStlOpenWaitMs)
        env.PumpOnce();
    CHECK_FALSE(readOnly.IsNewFolderButtonVisible());
    REQUIRE(readOnly.MakeFolder(L"x"));
    CHECK(WaitStatus(env, readOnly, L"新建文件夹失败", kSacPushWaitMs));
    readOnly.Close();
    env.Shutdown();
}

namespace {

//SSL1：一万个小文件里文件名含「文件0001」的个数（文件00010 ~ 文件00019）
const size_t kSslExactHits = 10;

//服务端下发的搜索条数上限（chatserver.conf 的 sharesearchmaxresults）
const size_t kSslServerMax = 500;

//搜索结果的截图
const wchar_t* const kSslSnapshot = L"\\..\\..\\..\\snapshots\\fileshare_browser_search.png";

} // namespace

TEST_CASE("SSL1 浏览窗口里搜索", "[.live][fileshare][sharesearch]")
{
    const std::wstring work = FreshWork();
    StlEnv env;
    env.Setup(work + L"\\personal\\");

    CShareBrowserWindow window;
    REQUIRE(window.Open(NULL, &env.m_service, kStlShareId));
    DWORD start = ::GetTickCount();
    while (window.GetEntryCount() == 0 && ::GetTickCount() - start < kStlOpenWaitMs)
        env.PumpOnce();
    const size_t rootEntries = window.GetEntryCount();
    REQUIRE(rootEntries > 0);

    //关键字命中 10 个文件，路径相对共享根
    REQUIRE(window.StartSearch(L"文件0001"));
    CHECK(window.IsSearchMode());
    //进行中的文字是「正在搜索…已找到 N 项」，结束后是「找到 N 项」：等后者出现在开头
    start = ::GetTickCount();
    while (window.GetStatusText().find(L"找到 ") != 0 && ::GetTickCount() - start < kStlTransferWaitMs)
        env.PumpOnce();
    CHECK(window.GetStatusText().find(L"找到 ") == 0);
    CHECK(window.GetEntryCount() == kSslExactHits);
    CHECK(window.FindRow(L"中文资料/小文件/文件00015.txt") >= 0);
    for (int i = 0; i < kStlSettleRounds; ++i)
        env.PumpOnce();
    CHECK(sharesnapshot::SaveWindowPng(window.m_hWnd, sharetestfs::ExeDir() + kSslSnapshot));

    //命中上万项：按上限截断并提示
    REQUIRE(window.StartSearch(L"文件"));
    CHECK(WaitStatus(env, window, L"只显示前", kStlTransferWaitMs));
    CHECK(window.GetEntryCount() == kSslServerMax);

    //清空关键字：回到目录列表
    window.StartSearch(L"");
    start = ::GetTickCount();
    while ((window.IsSearchMode() || window.GetEntryCount() != rootEntries) && ::GetTickCount() - start < kStlOpenWaitMs)
        env.PumpOnce();
    CHECK_FALSE(window.IsSearchMode());
    CHECK(window.GetEntryCount() == rootEntries);
    window.Close();
    env.Shutdown();
}

namespace {

//STH1：图片目录（在共享 9006 的根下）与几张图片的尺寸
const wchar_t* const kSthDirName = L"图片";
const int kSthWideW = 800;
const int kSthWideH = 600;
const int kSthTallW = 300;
const int kSthTallH = 900;

//STH1 的截图
const wchar_t* const kSthSnapshot = L"\\..\\..\\..\\snapshots\\fileshare_browser_grid.png";

//用 GDI+ 做一张纯色图片存成指定格式
bool SthSaveImage(const std::wstring& path, int w, int h, Gdiplus::ARGB color, const wchar_t* mime)
{
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(&buf[0]);
    Gdiplus::GetImageEncoders(num, size, codecs);
    for (UINT i = 0; i < num; ++i)
    {
        if (wcscmp(codecs[i].MimeType, mime) == 0)
        {
            Gdiplus::Bitmap bmp(w, h, PixelFormat24bppRGB);
            Gdiplus::Graphics g(&bmp);
            g.Clear(Gdiplus::Color(color));
            return bmp.Save(path.c_str(), &codecs[i].Clsid, NULL) == Gdiplus::Ok;
        }
    }
    return false;
}

//读出一个图片文件的尺寸；读不出返回 false
bool SthImageSize(const std::wstring& path, int& w, int& h)
{
    Gdiplus::Bitmap bmp(path.c_str());
    if (bmp.GetLastStatus() != Gdiplus::Ok)
        return false;
    w = static_cast<int>(bmp.GetWidth());
    h = static_cast<int>(bmp.GetHeight());
    return true;
}

} // namespace

TEST_CASE("STH1 缩略图视图与图片预览", "[.live][fileshare][sharethumb]")
{
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput input;
    REQUIRE(Gdiplus::GdiplusStartup(&token, &input, NULL) == Gdiplus::Ok);
    const std::wstring dir = std::wstring(kStlShareRoot) + L"\\" + kSthDirName;
    sharetestfs::DeleteTree(dir);
    REQUIRE(sharetestfs::MakeDirs(dir));
    REQUIRE(SthSaveImage(dir + L"\\a.png", kSthWideW, kSthWideH, 0xFFE04040, L"image/png"));
    REQUIRE(SthSaveImage(dir + L"\\b.jpg", kSthTallW, kSthTallH, 0xFF4060E0, L"image/jpeg"));
    REQUIRE(SthSaveImage(dir + L"\\c.png", kSthWideW, kSthWideW, 0xFF40C060, L"image/png"));
    REQUIRE(sharetestfs::WriteFileBytes(dir + L"\\note.txt", kStlSettleRounds));

    const std::wstring work = FreshWork();
    StlEnv env;
    env.Setup(work + L"\\personal\\");
    SmlUiHost ui;
    {
        CShareBrowserWindow window;
        window.SetUiHost(&ui);
        REQUIRE(window.Open(NULL, &env.m_service, kStlShareId));
        DWORD start = ::GetTickCount();
        while (window.GetEntryCount() == 0 && ::GetTickCount() - start < kStlOpenWaitMs)
            env.PumpOnce();
        const int dirRow = window.FindRow(kSthDirName);
        REQUIRE(dirRow >= 0);
        window.ExecuteMenuItem(dirRow, share_menu_open);
        start = ::GetTickCount();
        while (window.FindRow(L"a.png") < 0 && ::GetTickCount() - start < kStlOpenWaitMs)
            env.PumpOnce();
        REQUIRE(window.GetEntryCount() == 4);

        //缩略图视图：三张图片的缩略图都取到，文本文件不取
        window.SetGridMode(true);
        CHECK(window.IsGridMode());
        start = ::GetTickCount();
        while (window.GetCachedThumbCount() < 3 && ::GetTickCount() - start < kStlTransferWaitMs)
            env.PumpOnce();
        CHECK(window.GetCachedThumbCount() == 3);
        for (int i = 0; i < kStlSettleRounds; ++i)
            env.PumpOnce();
        CHECK(sharesnapshot::SaveWindowPng(window.m_hWnd, sharetestfs::ExeDir() + kSthSnapshot));

        //预览：交给看图窗的是长边不超过屏幕的 JPEG
        REQUIRE(window.PreviewRow(window.FindRow(L"a.png")));
        start = ::GetTickCount();
        while (ui.m_openedImages.empty() && ::GetTickCount() - start < kStlTransferWaitMs)
            env.PumpOnce();
        REQUIRE(ui.m_openedImages.size() == 1);
        CHECK(ui.m_previewSource == &window);
        int w = 0;
        int h = 0;
        REQUIRE(SthImageSize(ui.m_openedImages[0], w, h));
        CHECK(w == kSthWideW);
        CHECK(h == kSthWideH);
        //翻到下一张（按名称排序：a.png、b.jpg、c.png）
        window.OnPreviewNavigate(1);
        start = ::GetTickCount();
        while (ui.m_openedImages.size() < 2 && ::GetTickCount() - start < kStlTransferWaitMs)
            env.PumpOnce();
        REQUIRE(ui.m_openedImages.size() == 2);
        CHECK(ui.m_openedImages[1] != ui.m_openedImages[0]);
        REQUIRE(SthImageSize(ui.m_openedImages[1], w, h));
        CHECK(w == kSthTallW);
        CHECK(h == kSthTallH);
        //翻到头之后再往前：不换
        window.OnPreviewNavigate(-1);
        window.OnPreviewNavigate(-1);
        start = ::GetTickCount();
        while (ui.m_openedImages.size() < 3 && ::GetTickCount() - start < kStlTransferWaitMs)
            env.PumpOnce();
        for (int i = 0; i < kStlSettleRounds; ++i)
            env.PumpOnce();
        CHECK(ui.m_openedImages.size() == 3);
        window.Close();
        CHECK(ui.m_previewSource == NULL);
    }
    env.Shutdown();
    sharetestfs::DeleteTree(dir);
    Gdiplus::GdiplusShutdown(token);
}

namespace {

//SDL1：共享 9008 的根下本用例使用的目录
const wchar_t* const kSdlDirName = L"拖放";

//SDL1：测试文件的字节数
const size_t kSdlSmallBytes = 5000;
const size_t kSdlBigBytes = 3 * 1024 * 1024 + 17;
const size_t kSdlUploadBytes = 777;

//SDL1：拖出的复制在资源管理器的后台线程上进行，等它完成最多多久，单位：毫秒
const DWORD kSdlCopyWaitMs = 60000;

//SDL1：多选的截图
const wchar_t* const kSdlSnapshot = L"\\..\\..\\..\\snapshots\\fileshare_browser_multiselect.png";

//本用例的 COM 初始化（拖放要求单线程套间）
class SdlOleScope
{
public:
    SdlOleScope()
        : m_hr(::OleInitialize(NULL))
    {
    }

    ~SdlOleScope()
    {
        if (SUCCEEDED(m_hr))
            ::OleUninitialize();
    }

    HRESULT m_hr;   //OleInitialize 的结果
};

//等列表里出现某个条目
bool SdlWaitRow(StlEnv& env, CShareBrowserWindow& window, const wchar_t* name, DWORD waitMs)
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < waitMs)
    {
        env.PumpOnce();
        if (window.FindRow(name) >= 0)
            return true;
    }
    return false;
}

//等传输列表里第 index 行结束；返回该行的状态
int SdlWaitTransferRow(StlEnv& env, CShareBrowserWindow& window, size_t index, DWORD waitMs)
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < waitMs)
    {
        env.PumpOnce();
        if (window.GetTransferRowCount() > index && window.GetTransferRowState(index) != 0)
            return window.GetTransferRowState(index);
    }
    return window.GetTransferRowCount() > index ? window.GetTransferRowState(index) : -1;
}

//文件字节数；不存在时为 -1
int64_t SdlFileSize(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!::GetFileAttributesExW(ShareToLongPath(path).c_str(), GetFileExInfoStandard, &d))
        return -1;
    return (static_cast<int64_t>(d.nFileSizeHigh) << kStlHighShift) | d.nFileSizeLow;
}

//一个目录下的子条目数（不含 . 与 ..）；目录不存在时为 0
size_t SdlChildCount(const std::wstring& dir)
{
    size_t n = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = ::FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        const std::wstring name(fd.cFileName);
        if (name != L"." && name != L"..")
            ++n;
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    return n;
}

} // namespace

TEST_CASE("SDL1 浏览窗口里多选、拖出到本机文件夹、拖入上传", "[.live][fileshare][sharedraglive]")
{
    SdlOleScope ole;
    REQUIRE(SUCCEEDED(ole.m_hr));
    const std::wstring root = std::wstring(kSwlRoot) + L"\\" + kSdlDirName;
    RecycledUnder(root + L"\\", true);
    sharetestfs::DeleteTree(root);
    REQUIRE(sharetestfs::MakeDirs(root + L"\\pack\\sub"));
    REQUIRE(sharetestfs::MakeDirs(root + L"\\pack\\empty"));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\pack\\a.txt", kSdlSmallBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\pack\\sub\\b.bin", kSdlBigBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\single.txt", kSdlSmallBytes));
    REQUIRE(sharetestfs::WriteFileBytes(root + L"\\other.txt", kSdlSmallBytes));

    const std::wstring work = FreshWork();
    StlEnv env;
    env.Setup(work + L"\\personal\\");
    {
        CShareBrowserWindow window;
        REQUIRE(window.Open(NULL, &env.m_service, kSwlShareId));
        REQUIRE(SdlWaitRow(env, window, kSdlDirName, kStlOpenWaitMs));
        window.ExecuteMenuItem(window.FindRow(kSdlDirName), share_menu_open);
        REQUIRE(SdlWaitRow(env, window, L"single.txt", kStlOpenWaitMs));
        REQUIRE(window.GetEntryCount() == 3);

        //多选 pack 与 single.txt，截图人工核对两行都有选中的底色
        const int packRow = window.FindRow(L"pack");
        const int singleRow = window.FindRow(L"single.txt");
        std::vector<int> rows;
        rows.push_back(singleRow);
        rows.push_back(packRow);
        window.SelectRows(rows);
        std::vector<int> expected;
        expected.push_back(packRow < singleRow ? packRow : singleRow);
        expected.push_back(packRow < singleRow ? singleRow : packRow);
        CHECK(window.GetSelectedRows() == expected);
        for (int i = 0; i < kStlSettleRounds; ++i)
            env.PumpOnce();
        CHECK(sharesnapshot::SaveWindowPng(window.m_hWnd, sharetestfs::ExeDir() + kSdlSnapshot));

        //拖出到本机文件夹：以程序方式放到资源管理器的放置目标上，资源管理器异步复制
        int result = -1;
        IDataObject* data = window.CreateDragOutData(false, result);
        REQUIRE(data != NULL);
        CHECK(result == share_drag_build_ok);
        const std::wstring target = work + L"\\dropped";
        REQUIRE(sharetestfs::MakeDirs(target));
        REQUIRE(SUCCEEDED(ShareDropOnFolder(data, target)));
        data->Release();
        //拖出的下载在传输列表里照常显示
        CHECK(window.GetTransferRowCount() == 1);
        //资源管理器复制时先按描述表里的大小建好目标文件再写内容：字节数对上不等于复制完成，按内容一致判定
        const std::string bigMd5 = FileMd5(root + L"\\pack\\sub\\b.bin");
        const std::string singleMd5 = FileMd5(root + L"\\single.txt");
        DWORD start = ::GetTickCount();
        while ((FileMd5(target + L"\\pack\\sub\\b.bin") != bigMd5 || FileMd5(target + L"\\single.txt") != singleMd5)
               && ::GetTickCount() - start < kSdlCopyWaitMs)
            env.PumpOnce();
        CHECK(FileMd5(target + L"\\pack\\sub\\b.bin") == bigMd5);
        CHECK(FileMd5(target + L"\\pack\\a.txt") == FileMd5(root + L"\\pack\\a.txt"));
        CHECK(FileMd5(target + L"\\single.txt") == FileMd5(root + L"\\single.txt"));
        const DWORD emptyAttrs = ::GetFileAttributesW((target + L"\\pack\\empty").c_str());
        CHECK((emptyAttrs != INVALID_FILE_ATTRIBUTES && (emptyAttrs & FILE_ATTRIBUTE_DIRECTORY) != 0));
        CHECK(window.GetTransferRowState(0) == 1);
        CHECK(window.GetDragTransferCount() == 0);
        //资源管理器复制完、释放数据对象之后，临时目录被删除
        const std::wstring dragRoot = work + L"\\personal\\ChatLog\\FileShareDrag";
        start = ::GetTickCount();
        while (SdlChildCount(dragRoot) != 0 && ::GetTickCount() - start < kSdlCopyWaitMs)
            env.PumpOnce();
        CHECK(SdlChildCount(dragRoot) == 0);

        //拖入上传：两个来自不同本机目录的文件，分成两次上传
        const std::wstring up1 = work + L"\\up1\\x.txt";
        const std::wstring up2 = work + L"\\up2\\y.txt";
        REQUIRE(sharetestfs::MakeDirs(work + L"\\up1"));
        REQUIRE(sharetestfs::MakeDirs(work + L"\\up2"));
        REQUIRE(sharetestfs::WriteFileBytes(up1, kSdlUploadBytes));
        REQUIRE(sharetestfs::WriteFileBytes(up2, kSdlUploadBytes));
        std::vector<std::wstring> localItems;
        localItems.push_back(up1);
        localItems.push_back(up2);
        IDataObject* files = ShareCreateFileListData(localItems);
        REQUIRE(files != NULL);
        IDropTarget* dropTarget = window.GetUploadDropTarget();
        REQUIRE(dropTarget != NULL);
        POINTL pt = { 0, 0 };
        DWORD effect = DROPEFFECT_COPY;
        REQUIRE(SUCCEEDED(dropTarget->DragEnter(files, MK_LBUTTON, pt, &effect)));
        CHECK(effect != DROPEFFECT_NONE);
        CHECK(window.GetStatusText() == L"松开鼠标即上传到当前文件夹");
        effect = DROPEFFECT_COPY;
        REQUIRE(SUCCEEDED(dropTarget->Drop(files, MK_LBUTTON, pt, &effect)));
        files->Release();
        REQUIRE(window.GetTransferRowCount() == 3);
        CHECK(SdlWaitTransferRow(env, window, 1, kStlTransferWaitMs) == 1);
        CHECK(SdlWaitTransferRow(env, window, 2, kStlTransferWaitMs) == 1);
        CHECK(FileMd5(root + L"\\x.txt") == FileMd5(up1));
        CHECK(FileMd5(root + L"\\y.txt") == FileMd5(up2));

        //上传成功后列表刷新、选择清空：重新选中再拖出。拖出的虚拟文件不带文件列表，拖回本窗口不接收
        REQUIRE(SdlWaitRow(env, window, L"y.txt", kStlOpenWaitMs));
        window.SelectRows(std::vector<int>(1, window.FindRow(L"single.txt")));
        data = window.CreateDragOutData(false, result);
        REQUIRE(data != NULL);
        effect = DROPEFFECT_COPY;
        dropTarget->DragEnter(data, MK_LBUTTON, pt, &effect);
        CHECK(effect == DROPEFFECT_NONE);
        dropTarget->DragLeave();
        data->Release();

        //搜索结果里多选来自两个目录的条目下载：分成两笔传输，第二笔排队等第一笔结束
        REQUIRE(window.StartSearch(L"a.txt"));
        start = ::GetTickCount();
        while (window.GetStatusText().find(L"找到 ") != 0 && ::GetTickCount() - start < kStlTransferWaitMs)
            env.PumpOnce();
        REQUIRE(window.GetEntryCount() == 1);
        REQUIRE(window.StartSearch(L".txt"));
        start = ::GetTickCount();
        while (window.GetStatusText().find(L"找到 ") != 0 && ::GetTickCount() - start < kStlTransferWaitMs)
            env.PumpOnce();
        const int hitA = window.FindRow(std::wstring(kSdlDirName) + L"/pack/a.txt");
        const int hitSingle = window.FindRow(std::wstring(kSdlDirName) + L"/single.txt");
        REQUIRE(hitA >= 0);
        REQUIRE(hitSingle >= 0);
        rows.clear();
        rows.push_back(hitA);
        rows.push_back(hitSingle);
        const std::wstring dl = work + L"\\dl";
        REQUIRE(sharetestfs::MakeDirs(dl));
        const size_t before = window.GetTransferRowCount();
        REQUIRE(window.DownloadRows(rows, dl));
        REQUIRE(window.GetTransferRowCount() == before + 2);
        CHECK(SdlWaitTransferRow(env, window, before, kStlTransferWaitMs) == 1);
        CHECK(SdlWaitTransferRow(env, window, before + 1, kStlTransferWaitMs) == 1);
        CHECK(FileMd5(dl + L"\\a.txt") == FileMd5(root + L"\\pack\\a.txt"));
        CHECK(FileMd5(dl + L"\\single.txt") == FileMd5(root + L"\\single.txt"));
        window.StartSearch(L"");

        //多选删除：x.txt 与 y.txt 一起放进回收站
        REQUIRE(SdlWaitRow(env, window, L"y.txt", kStlOpenWaitMs));
        rows.clear();
        rows.push_back(window.FindRow(L"x.txt"));
        rows.push_back(window.FindRow(L"y.txt"));
        REQUIRE(window.DeleteRows(rows));
        CHECK(WaitStatus(env, window, L"已删除 2 项", kSacPushWaitMs));
        CHECK_FALSE(Exists(root + L"\\x.txt"));
        CHECK_FALSE(Exists(root + L"\\y.txt"));
        window.Close();
    }

    //条目超过上限的目录：拒绝拖出，提示改用下载（共享 9006 的「中文资料」下有一万个小文件）
    {
        CShareBrowserWindow big;
        REQUIRE(big.Open(NULL, &env.m_service, kStlShareId));
        REQUIRE(SdlWaitRow(env, big, L"中文资料", kStlOpenWaitMs));
        big.SelectRows(std::vector<int>(1, big.FindRow(L"中文资料")));
        int result = -1;
        IDataObject* data = big.CreateDragOutData(false, result);
        CHECK(data == NULL);
        CHECK(result == share_drag_build_too_many);
        CHECK(big.GetStatusText().find(L"无法拖出") != std::wstring::npos);
        big.Close();
    }
    env.Shutdown();
    RecycledUnder(root + L"\\", true);
    sharetestfs::DeleteTree(root);
}

namespace {

//第 2 期验收：只读的共享 9001（seed_step12.py，成员 5308 没有写权限）与它的共享根
const int64_t kP2aReadOnlyShareId = 9001;
const wchar_t* const kP2aReadOnlyRoot = L"E:\\flamingo\\tmp\\fileshare_env\\share_root";

//第 2 期验收：共享根是网络路径的共享 9009 与单文件共享 9010（seed_p2_accept.py）
const int64_t kP2aUncShareId = 9009;
const wchar_t* const kP2aUncLocal = L"E:\\flamingo\\tmp\\fileshare_env\\share_unc";
const int64_t kP2aFileShareId = 9010;
const wchar_t* const kP2aFileRoot = L"E:\\flamingo\\tmp\\fileshare_env\\share_file\\report.bin";
const int64_t kP2aFileBytes = 1024 * 1024 + 3;

//SP2A4：在共享 9008 下现做的目录与单个文件的字节数
const wchar_t* const kP2aSingleDir = L"单个";
const size_t kP2aSingleBytes = 2 * 1024 * 1024 + 9;

//SP2A3 / SP2A5：不会命中任何条目的关键字（搜索因此要走遍整个共享）
const wchar_t* const kP2aNoHitKeyword = L"qqq不会命中的关键字";

//在分享者日志里数「取消时找到了正在进行的搜索」「搜索因超时结束」的片段
const char* const kP2aCancelFoundMarker = ", found: 1.";
const char* const kP2aTimeoutMarker = ", hits: 0, outcome: 2.";

//等分享者日志落盘的最长时间（日志异步写），单位：毫秒
const DWORD kP2aOwnerLogWaitMs = 10000;

//等列表里第一次出现条目
bool P2aWaitEntries(StlEnv& env, CShareBrowserWindow& window)
{
    const DWORD start = ::GetTickCount();
    while (window.GetEntryCount() == 0 && ::GetTickCount() - start < kStlOpenWaitMs)
        env.PumpOnce();
    return window.GetEntryCount() > 0;
}

//等分享者日志里某段文字的出现次数达到 expected
bool P2aWaitOwnerLog(StlEnv& env, const std::string& marker, int expected)
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < kP2aOwnerLogWaitMs)
    {
        if (CountInOwnerLog(marker) >= expected)
            return true;
        env.PumpOnce();
    }
    return CountInOwnerLog(marker) >= expected;
}

//等本机文件的内容与源文件一致。资源管理器复制时先按描述表里的大小建好目标文件再写内容，字节数对上不等于复制完成
bool P2aWaitSameContent(StlEnv& env, const std::wstring& copied, const std::wstring& source)
{
    const std::string expected = FileMd5(source);
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < kSdlCopyWaitMs)
    {
        if (!expected.empty() && FileMd5(copied) == expected)
            return true;
        env.PumpOnce();
    }
    return false;
}

//把当前选中的条目拖到本机目录 target，等复制出的 copied 与源文件 source 内容一致
bool P2aDragSelectionTo(StlEnv& env, CShareBrowserWindow& window, const std::wstring& target, const std::wstring& copied,
                        const std::wstring& source)
{
    int result = -1;
    IDataObject* data = window.CreateDragOutData(false, result);
    if (data == NULL)
        return false;
    const bool dropped = SUCCEEDED(ShareDropOnFolder(data, target));
    data->Release();
    if (!dropped)
        return false;
    return P2aWaitSameContent(env, copied, source);
}

} // namespace

TEST_CASE("SP2A1 没有写权限时各项写操作被拒，非固定盘上的删除被拒", "[.live][fileshare][p2accept]")
{
    const std::wstring work = FreshWork();
    const std::wstring roRoot = kP2aReadOnlyRoot;
    StlEnv env;
    env.Setup(work + L"\\personal\\");
    const std::wstring denied = ShareBrowseModel::ErrorText(error_code_share_permission_denied);
    {
        CShareBrowserWindow ro;
        REQUIRE(ro.Open(NULL, &env.m_service, kP2aReadOnlyShareId));
        REQUIRE(SdlWaitRow(env, ro, L"readme.txt", kStlOpenWaitMs));
        CHECK_FALSE(ro.IsNewFolderButtonVisible());
        const int row = ro.FindRow(L"readme.txt");
        REQUIRE(ro.MakeFolder(L"x"));
        CHECK(WaitStatus(env, ro, (L"新建文件夹失败：" + denied).c_str(), kSacPushWaitMs));
        REQUIRE(ro.RenameRow(row, L"renamed.txt"));
        CHECK(WaitStatus(env, ro, (L"重命名失败：" + denied).c_str(), kSacPushWaitMs));
        REQUIRE(ro.MoveRow(row, "docs"));
        CHECK(WaitStatus(env, ro, (L"移动失败：" + denied).c_str(), kSacPushWaitMs));
        REQUIRE(ro.DeleteRow(row));
        CHECK(WaitStatus(env, ro, (L"删除失败：" + denied).c_str(), kSacPushWaitMs));
        CHECK(Exists(roRoot + L"\\readme.txt"));
        CHECK_FALSE(Exists(roRoot + L"\\renamed.txt"));
        CHECK_FALSE(Exists(roRoot + L"\\docs\\readme.txt"));
        CHECK_FALSE(Exists(roRoot + L"\\x"));
        //拖入上传：界面不接收
        const std::wstring local = work + L"\\up.txt";
        REQUIRE(sharetestfs::WriteFileBytes(local, kSmlFileBytes));
        CHECK_FALSE(ro.UploadDroppedItems(std::vector<std::wstring>(1, local)));
        ro.Close();

        //绕过界面直接发起上传：被拒（806）
        StlVisitorObserver vobs;
        CShareVisitor* visitor = env.m_service.AcquireVisitor(kP2aReadOnlyShareId, &vobs);
        REQUIRE(visitor != NULL);
        visitor->List("", static_cast<int64_t>(::GetTickCount64()));
        const DWORD start = ::GetTickCount();
        while (visitor->GetState() != share_visitor_ready && ::GetTickCount() - start < kStlOpenWaitMs)
            env.PumpOnce();
        REQUIRE(visitor->GetState() == share_visitor_ready);
        StlTransferObserver tobs;
        int err = 0;
        const uint32_t id = env.m_service.StartUpload(kP2aReadOnlyShareId, std::vector<std::wstring>(1, local), "", &tobs, err);
        REQUIRE(id != 0);
        CHECK(env.WaitTransfer(tobs, id) == error_code_share_permission_denied);
        CHECK_FALSE(Exists(roRoot + L"\\up.txt"));
        env.m_service.ReleaseVisitor(kP2aReadOnlyShareId);
    }

    //共享根在网络路径上（不是固定盘，没有回收站）：新建照常，删除被拒（810），文件保留
    {
        const std::wstring uncLocal = kP2aUncLocal;
        CShareBrowserWindow unc;
        REQUIRE(unc.Open(NULL, &env.m_service, kP2aUncShareId));
        REQUIRE(SdlWaitRow(env, unc, L"a.txt", kStlOpenWaitMs));
        REQUIRE(unc.MakeFolder(L"新建的文件夹"));
        CHECK(WaitStatus(env, unc, L"已新建文件夹", kSacPushWaitMs));
        CHECK(Exists(uncLocal + L"\\新建的文件夹"));
        REQUIRE(SdlWaitRow(env, unc, L"a.txt", kStlOpenWaitMs));
        REQUIRE(unc.DeleteRow(unc.FindRow(L"a.txt")));
        CHECK(WaitStatus(env, unc, (L"删除失败：" + ShareBrowseModel::ErrorText(error_code_share_recycle_unavailable)).c_str(),
                         kSacPushWaitMs));
        CHECK(Exists(uncLocal + L"\\a.txt"));
        unc.Close();
        ::RemoveDirectoryW((uncLocal + L"\\新建的文件夹").c_str());
    }
    env.Shutdown();
}

TEST_CASE("SP2A3 搜索中取消：分享者停止搜索，界面回到目录列表", "[.live][fileshare][p2accept]")
{
    const std::wstring work = FreshWork();
    StlEnv env;
    env.Setup(work + L"\\personal\\");
    CShareBrowserWindow window;
    REQUIRE(window.Open(NULL, &env.m_service, kStlShareId));
    REQUIRE(P2aWaitEntries(env, window));
    const size_t rootEntries = window.GetEntryCount();
    const int canceledBefore = CountInOwnerLog(kP2aCancelFoundMarker);

    //搜一个不会命中的关键字（要走遍上万个条目），发出后立即刷新：浏览窗口取消搜索、回到目录列表
    REQUIRE(window.StartSearch(kP2aNoHitKeyword));
    CHECK(window.IsSearchMode());
    window.ExecuteMenuItem(-1, share_menu_refresh);
    CHECK_FALSE(window.IsSearchMode());
    //分享者找到了正在进行（或排队）的那次搜索并中止
    CHECK(P2aWaitOwnerLog(env, kP2aCancelFoundMarker, canceledBefore + 1));
    const DWORD start = ::GetTickCount();
    while (window.GetEntryCount() != rootEntries && ::GetTickCount() - start < kStlOpenWaitMs)
        env.PumpOnce();
    CHECK(window.GetEntryCount() == rootEntries);
    CHECK_FALSE(window.IsSearchMode());
    window.Close();
    env.Shutdown();
}

TEST_CASE("SP2A4 拖出单个文件：文件夹共享里的一个文件、单文件共享", "[.live][fileshare][p2accept]")
{
    SdlOleScope ole;
    REQUIRE(SUCCEEDED(ole.m_hr));
    const std::wstring dir = std::wstring(kSwlRoot) + L"\\" + kP2aSingleDir;
    sharetestfs::DeleteTree(dir);
    REQUIRE(sharetestfs::MakeDirs(dir));
    REQUIRE(sharetestfs::WriteFileBytes(dir + L"\\one.bin", kP2aSingleBytes));
    const std::wstring work = FreshWork();
    StlEnv env;
    env.Setup(work + L"\\personal\\");
    {
        CShareBrowserWindow window;
        REQUIRE(window.Open(NULL, &env.m_service, kSwlShareId));
        REQUIRE(SdlWaitRow(env, window, kP2aSingleDir, kStlOpenWaitMs));
        window.ExecuteMenuItem(window.FindRow(kP2aSingleDir), share_menu_open);
        REQUIRE(SdlWaitRow(env, window, L"one.bin", kStlOpenWaitMs));
        window.SelectRows(std::vector<int>(1, window.FindRow(L"one.bin")));
        const std::wstring target = work + L"\\drop1";
        REQUIRE(sharetestfs::MakeDirs(target));
        CHECK(P2aDragSelectionTo(env, window, target, target + L"\\one.bin", dir + L"\\one.bin"));
        window.Close();
    }
    {
        CShareBrowserWindow file;
        REQUIRE(file.Open(NULL, &env.m_service, kP2aFileShareId));
        REQUIRE(SdlWaitRow(env, file, L"report.bin", kStlOpenWaitMs));
        REQUIRE(file.GetEntryCount() == 1);
        file.SelectRows(std::vector<int>(1, 0));
        const std::wstring target = work + L"\\drop2";
        REQUIRE(sharetestfs::MakeDirs(target));
        CHECK(P2aDragSelectionTo(env, file, target, target + L"\\report.bin", kP2aFileRoot));
        CHECK(SdlFileSize(target + L"\\report.bin") == kP2aFileBytes);
        file.Close();
    }
    env.Shutdown();
    sharetestfs::DeleteTree(dir);
}

TEST_CASE("SP2A5 搜索超时：提示结果不完整", "[.live][fileshare][p2timeout]")
{
    //本用例要求测试服务端的 chatserver.conf 临时加上 sharesearchtimeout=1（毫秒），跑完改回并重启服务端
    const std::wstring work = FreshWork();
    StlEnv env;
    env.Setup(work + L"\\personal\\");
    CShareBrowserWindow window;
    REQUIRE(window.Open(NULL, &env.m_service, kStlShareId));
    REQUIRE(P2aWaitEntries(env, window));
    const int timeoutsBefore = CountInOwnerLog(kP2aTimeoutMarker);
    REQUIRE(window.StartSearch(kP2aNoHitKeyword));
    CHECK(WaitStatus(env, window, L"只显示前 0 项：结果太多或搜索用时太长", kStlTransferWaitMs));
    CHECK(P2aWaitOwnerLog(env, kP2aTimeoutMarker, timeoutsBefore + 1));
    window.Close();
    env.Shutdown();
}

namespace {

//第 3 期：测试服务端的 HTTP 端口（admin 控制台走这里）与 seed_p3_admin.py 建的两个测试管理员
const unsigned short kP3HttpPort = 25345;
const char* const kP3PlatformEmail = "p3platform@test.com";
const char* const kP3Ent1Email = "p3ent1@test.com";
const char* const kP3AdminPassword = "p3test123";
const char* const kP3Ent1AdminName = "企业一测试管理员";

//企业 1（测试账号所在）与企业 2；企业 2 的 QN0151、QN0151 与 QN0152 的共享 9011；跨企业测试群
const int64_t kP3Enterprise1 = 1;
const int64_t kP3Enterprise2 = 2;
const int64_t kP3Enterprise3 = 3;
const char* const kP3OtherAccount = "QN0151";
const int64_t kP3OtherUserId = 5151;
const int64_t kP3OtherShareId = 9011;
const int64_t kP3OtherMemberId = 5152;
const int64_t kP3CrossGroup = 268435470;
const int64_t kP3NotExistShareId = 999999;
const int64_t kP3NotExistEnterprise = 999999;

//功能设置项与几个后台错误码（AdminApiCodes.h 的取值，客户端工程里没有那份头文件）
const char* const kP3SettingKey = "fileshare.enabled";
const int kP3AdminFileShareNotExist = 130;
const int kP3AdminFileShareCanceled = 131;
const int kP3AdminFileShareDenied = 132;
const int kP3AdminSettingInvalid = 133;

//HTTP 应答的读取：单次等待与缓冲区
const DWORD kP3HttpTimeoutMs = 10000;
const int kP3RecvChunk = 4096;

//确认别的企业收不到推送时等多久，单位：毫秒
const int kP3QuietWaitMs = 3000;

//审计记录里文件下载那一条要等分享者批量上报，等多久，单位：毫秒
const DWORD kP3AuditWaitMs = 20000;

//SP3A3 在共享 9008 的根目录里自己放一个文件用来下载：SWL1 每次运行都会清空重建这个根目录并删掉其中的文件，
//不能指望根目录下事先有文件（全量回归里 SP3A3 排在 SWL1 之后时曾因此失败）
const wchar_t* const kP3DownloadFileW = L"p3a3_download.bin";
const char* const kP3DownloadFile = "p3a3_download.bin";

//上面那个文件的大小，单位：字节
const size_t kP3DownloadBytes = 4096;

//向测试服务端的 HTTP 端口发一个 POST，应答正文解析成 JSON；失败返回 false
bool P3Post(const std::string& path, const Json::Value& body, Json::Value& resp)
{
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    const std::string payload = Json::writeString(wb, body);
    //管理员登录可能发生在任何聊天连接之前：自己初始化一次 Winsock（按引用计数，与其它连接互不影响）
    WSADATA wsa;
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return false;
    struct WsaGuard
    {
        ~WsaGuard()
        {
            ::WSACleanup();
        }
    } wsaGuard;
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return false;
    DWORD timeout = kP3HttpTimeoutMs;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kP3HttpPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
    {
        ::closesocket(s);
        return false;
    }
    std::ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: "
        << payload.size() << "\r\nConnection: close\r\n\r\n" << payload;
    const std::string text = req.str();
    if (::send(s, text.data(), static_cast<int>(text.size()), 0) != static_cast<int>(text.size()))
    {
        ::closesocket(s);
        return false;
    }
    //读到头部结束，再按 Content-Length 读完正文（服务端不一定主动关连接）
    std::string data;
    char buf[kP3RecvChunk];
    size_t bodyStart = std::string::npos;
    size_t bodyLen = 0;
    while (true)
    {
        if (bodyStart != std::string::npos && data.size() >= bodyStart + bodyLen)
            break;
        const int n = ::recv(s, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        data.append(buf, static_cast<size_t>(n));
        if (bodyStart == std::string::npos)
        {
            const size_t end = data.find("\r\n\r\n");
            if (end == std::string::npos)
                continue;
            bodyStart = end + 4;
            const size_t cl = data.find("Content-Length:");
            if (cl != std::string::npos && cl < end)
                bodyLen = static_cast<size_t>(atoi(data.c_str() + cl + strlen("Content-Length:")));
        }
    }
    ::closesocket(s);
    if (bodyStart == std::string::npos)
        return false;
    const std::string json = data.substr(bodyStart);
    Json::CharReaderBuilder rb;
    std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    std::string errs;
    return reader->parse(json.data(), json.data() + json.size(), &resp, &errs);
}

//管理员登录，返回令牌；失败返回空串
std::string P3Login(const char* email)
{
    Json::Value body;
    body["email"] = email;
    body["password"] = kP3AdminPassword;
    Json::Value resp;
    if (!P3Post("/admin/login.do", body, resp) || resp["code"].asInt() != 0)
        return std::string();
    return resp["token"].asString();
}

//带令牌的请求体
Json::Value P3Req(const std::string& token)
{
    Json::Value body;
    body["token"] = token;
    return body;
}

//发一个后台请求并返回 code；请求失败返回 -1
int P3Call(const std::string& path, const Json::Value& body, Json::Value& resp)
{
    resp = Json::Value();
    if (!P3Post(path, body, resp) || !resp.isMember("code"))
        return -1;
    return resp["code"].asInt();
}

//改企业的文件共享开关（管理员令牌决定作用的企业；平台管理员传 enterpriseId）
int P3SetSwitch(const std::string& token, int64_t enterpriseId, const char* value, Json::Value& resp)
{
    Json::Value body = P3Req(token);
    if (enterpriseId > 0)
        body["enterpriseid"] = static_cast<Json::Int64>(enterpriseId);
    body["key"] = kP3SettingKey;
    body["value"] = value;
    return P3Call("/admin/setting/set.do", body, resp);
}

//等环境里收到一条 1013，且 fileshare 为 expected（收到的 1013 从积压里取走）
bool P3WaitFeatures(StlEnv& env, int expected)
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < kSacPushWaitMs)
    {
        env.PumpOnce();
        for (size_t i = 0; i < env.m_others.size(); ++i)
        {
            if (env.m_others[i].m_cmd != cmd_features_notify)
                continue;
            Json::Value root;
            Json::CharReaderBuilder rb;
            std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
            std::string errs;
            const std::string& body = env.m_others[i].m_body;
            const bool ok = reader->parse(body.data(), body.data() + body.size(), &root, &errs);
            env.m_others.erase(env.m_others.begin() + static_cast<long>(i));
            if (ok && root["fileshare"].asInt() == expected)
                return true;
            break;
        }
    }
    return false;
}

//访问状态机的观察者：在列目录之外再记下状态与失败原因
class P3VisitorObserver : public StlVisitorObserver
{
public:
    P3VisitorObserver()
        : m_state(-1)
        , m_err(-1)
    {
    }

    virtual void OnVisitorStateChanged(int state, int errCode) override
    {
        m_state = state;
        m_err = errCode;
    }

    int m_state;   //最近一次状态
    int m_err;     //最近一次状态变化带的原因
};

//等访问状态机进入某个状态
bool P3WaitVisitor(StlEnv& env, CShareVisitor* visitor, int state)
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < kStlOpenWaitMs)
    {
        env.PumpOnce();
        if (visitor->GetState() == state)
            return true;
    }
    return visitor->GetState() == state;
}

//等分享者日志里某段文字出现的次数达到 expected
bool P3WaitOwnerLog(StlEnv& env, const std::string& marker, int expected)
{
    const DWORD start = ::GetTickCount();
    while (::GetTickCount() - start < kSacPushWaitMs)
    {
        if (CountInOwnerLog(marker) >= expected)
            return true;
        env.PumpOnce();
    }
    return CountInOwnerLog(marker) >= expected;
}

//列表里有没有某个 shareid
bool P3ListHasShare(const Json::Value& list, int64_t shareId)
{
    for (Json::ArrayIndex i = 0; i < list.size(); ++i)
    {
        if (list[i]["shareid"].asInt64() == shareId)
            return true;
    }
    return false;
}

} // namespace

TEST_CASE("SP3A1 按企业的功能开关：关闭时入口与访问被拒、别的企业不受影响，重新打开后恢复", "[.live][fileshare][p3accept]")
{
    const std::string platform = P3Login(kP3PlatformEmail);
    const std::string ent1 = P3Login(kP3Ent1Email);
    REQUIRE_FALSE(platform.empty());
    REQUIRE_FALSE(ent1.empty());

    //未设置时默认开启；平台管理员必须指定企业；企业管理员一律读本企业
    Json::Value resp;
    Json::Value get = P3Req(platform);
    get["enterpriseid"] = static_cast<Json::Int64>(kP3Enterprise1);
    REQUIRE(P3Call("/admin/setting/get.do", get, resp) == 0);
    REQUIRE(resp["settings"].size() == 1);
    CHECK(resp["settings"][0]["value"].asString() == "1");
    CHECK(resp["settings"][0]["isdefault"].asInt() == 1);
    CHECK(P3Call("/admin/setting/get.do", P3Req(platform), resp) != 0);
    Json::Value entGet = P3Req(ent1);
    entGet["enterpriseid"] = static_cast<Json::Int64>(kP3Enterprise2);
    REQUIRE(P3Call("/admin/setting/get.do", entGet, resp) == 0);
    CHECK(resp["enterpriseid"].asInt64() == kP3Enterprise1);

    const std::wstring work = FreshWork();
    const std::wstring root = work + L"\\p3root";
    REQUIRE(sharetestfs::MakeDirs(root));
    StlEnv env;   //MY0308，企业 1
    env.Setup(work + L"\\personal\\");
    LiveChatClient other;   //QN0151，企业 2
    std::string reason;
    const bool otherLoggedIn = other.ConnectAndLogin(kStlChatPort, kTestServerPubKey, kP3OtherAccount, kStlPassword, reason);
    INFO("login failed: " << reason);
    REQUIRE(otherLoggedIn);

    P3VisitorObserver vobs;
    CShareVisitor* visitor = env.m_service.AcquireVisitor(kSwlShareId, &vobs);
    REQUIRE(visitor != NULL);
    visitor->List("", static_cast<int64_t>(::GetTickCount64()));
    REQUIRE(P3WaitVisitor(env, visitor, share_visitor_ready));
    const int ownerOffBefore = CountInOwnerLog("Features notify handled: fileshare=0, entries changed=1");
    const int ownerLogoutBefore = CountInOwnerLog("file share service: logging out");
    const int ownerOnBefore = CountInOwnerLog("Features notify handled: fileshare=1, entries changed=1");

    //企业 1 的管理员关闭开关
    REQUIRE(P3SetSwitch(ent1, 0, "0", resp) == 0);
    CHECK(resp["enabled"].asInt() == 0);
    CHECK(resp["enterpriseid"].asInt64() == kP3Enterprise1);
    //企业 1 的在线用户收到 1013，企业 2 的收不到
    CHECK(P3WaitFeatures(env, 0));
    std::string body;
    CHECK_FALSE(other.WaitFor(cmd_features_notify, body, kP3QuietWaitMs));
    //正在进行的访问被结束（800），卡片显示功能已关闭
    CHECK(P3WaitVisitor(env, visitor, share_visitor_failed));
    CHECK(vobs.m_err == error_code_share_disabled);
    CHECK(WaitCardState(env, kSwlShareId, share_state_unavailable, share_unavailable_feature_disabled));
    //分享者客户端（clientA）收起入口并让模块按退出登录收尾
    CHECK(P3WaitOwnerLog(env, "Features notify handled: fileshare=0, entries changed=1", ownerOffBefore + 1));
    CHECK(P3WaitOwnerLog(env, "file share service: logging out", ownerLogoutBefore + 1));
    //再打开被拒（800），发起共享被拒（800）
    vobs.m_err = -1;
    visitor->List("", static_cast<int64_t>(::GetTickCount64()));
    CHECK(P3WaitVisitor(env, visitor, share_visitor_failed));
    CHECK(vobs.m_err == error_code_share_disabled);
    SmlManageObserver mobs;
    CHECK(SacManage(env, mobs, env.m_service.CreateShare(SacGroupParams(root, std::vector<ShareMemberPerm>(1, Member(5309, 0))),
                                                         root, &mobs)) == error_code_share_disabled);

    //重新打开：收到 1013，分享者客户端重新激活并上报，卡片恢复可用，又能打开
    REQUIRE(P3SetSwitch(ent1, 0, "1", resp) == 0);
    CHECK(resp["enabled"].asInt() == 1);
    CHECK(P3WaitFeatures(env, 1));
    CHECK(P3WaitOwnerLog(env, "Features notify handled: fileshare=1, entries changed=1", ownerOnBefore + 1));
    CHECK(WaitCardState(env, kSwlShareId, share_state_available, 0));
    visitor->List("", static_cast<int64_t>(::GetTickCount64()));
    CHECK(P3WaitVisitor(env, visitor, share_visitor_ready));
    //管理员操作日志里有两条开关记录
    Json::Value audit = P3Req(platform);
    audit["pagesize"] = 50;
    REQUIRE(P3Call("/admin/audit/list.do", audit, resp) == 0);
    int switchLogs = 0;
    for (Json::ArrayIndex i = 0; i < resp["list"].size(); ++i)
        switchLogs += (resp["list"][i]["action"].asString() == "setting.set") ? 1 : 0;
    CHECK(switchLogs >= 2);

    env.m_service.ReleaseVisitor(kSwlShareId);
    other.Close();
    env.Shutdown();
}

TEST_CASE("SP3A2 管理员的企业范围与新企业的默认值", "[.live][fileshare][p3accept]")
{
    const std::string platform = P3Login(kP3PlatformEmail);
    const std::string ent1 = P3Login(kP3Ent1Email);
    REQUIRE_FALSE(platform.empty());
    REQUIRE_FALSE(ent1.empty());
    Json::Value resp;

    //企业管理员改「别的企业」：一律落在本企业；企业 2 的设置不受影响
    REQUIRE(P3SetSwitch(ent1, kP3Enterprise2, "1", resp) == 0);
    CHECK(resp["enterpriseid"].asInt64() == kP3Enterprise1);
    Json::Value get = P3Req(platform);
    get["enterpriseid"] = static_cast<Json::Int64>(kP3Enterprise2);
    REQUIRE(P3Call("/admin/setting/get.do", get, resp) == 0);
    CHECK(resp["settings"][0]["isdefault"].asInt() == 1);
    //新开通、从未设置过的企业：默认开启
    get["enterpriseid"] = static_cast<Json::Int64>(kP3Enterprise3);
    REQUIRE(P3Call("/admin/setting/get.do", get, resp) == 0);
    CHECK(resp["settings"][0]["value"].asString() == "1");
    CHECK(resp["settings"][0]["isdefault"].asInt() == 1);
    //不存在的企业、不认识的设置项与取值
    CHECK(P3SetSwitch(platform, kP3NotExistEnterprise, "0", resp) != 0);
    CHECK(P3SetSwitch(platform, kP3Enterprise2, "2", resp) == kP3AdminSettingInvalid);
    Json::Value badKey = P3Req(platform);
    badKey["enterpriseid"] = static_cast<Json::Int64>(kP3Enterprise2);
    badKey["key"] = "other.key";
    badKey["value"] = "0";
    CHECK(P3Call("/admin/setting/set.do", badKey, resp) == kP3AdminSettingInvalid);

    //企业 1 的管理员看不到、也不能操作企业 2 的共享；平台管理员可以
    Json::Value members = P3Req(ent1);
    members["shareid"] = static_cast<Json::Int64>(kP3OtherShareId);
    CHECK(P3Call("/admin/fileshare/members.do", members, resp) == kP3AdminFileShareDenied);
    Json::Value cancel = P3Req(ent1);
    cancel["shareid"] = static_cast<Json::Int64>(kP3OtherShareId);
    CHECK(P3Call("/admin/fileshare/cancel.do", cancel, resp) == kP3AdminFileShareDenied);
    members["token"] = platform;
    REQUIRE(P3Call("/admin/fileshare/members.do", members, resp) == 0);
    REQUIRE(resp["members"].size() == 1);
    CHECK(resp["members"][0]["userid"].asInt64() == kP3OtherMemberId);
    Json::Value list = P3Req(ent1);
    list["pagesize"] = 200;
    REQUIRE(P3Call("/admin/fileshare/list.do", list, resp) == 0);
    CHECK_FALSE(P3ListHasShare(resp["list"], kP3OtherShareId));
    for (Json::ArrayIndex i = 0; i < resp["list"].size(); ++i)
        CHECK(resp["list"][i]["enterpriseid"].asInt64() == kP3Enterprise1);
    list["token"] = platform;
    list["enterpriseid"] = static_cast<Json::Int64>(kP3Enterprise2);
    REQUIRE(P3Call("/admin/fileshare/list.do", list, resp) == 0);
    CHECK(P3ListHasShare(resp["list"], kP3OtherShareId));
    //不存在的共享
    members["shareid"] = static_cast<Json::Int64>(kP3NotExistShareId);
    CHECK(P3Call("/admin/fileshare/members.do", members, resp) == kP3AdminFileShareNotExist);
}

TEST_CASE("SP3A3 后台查看共享与成员、强制取消、审计的两个来源", "[.live][fileshare][p3accept]")
{
    const std::string platform = P3Login(kP3PlatformEmail);
    const std::string ent1 = P3Login(kP3Ent1Email);
    REQUIRE_FALSE(platform.empty());
    REQUIRE_FALSE(ent1.empty());
    Json::Value resp;

    //共享列表：按分享者关键字与状态过滤，带会话名与成员数
    Json::Value list = P3Req(platform);
    list["keyword"] = "MY0127";
    list["state"] = 1;
    list["pagesize"] = 200;
    REQUIRE(P3Call("/admin/fileshare/list.do", list, resp) == 0);
    REQUIRE(P3ListHasShare(resp["list"], kSwlShareId));
    for (Json::ArrayIndex i = 0; i < resp["list"].size(); ++i)
    {
        if (resp["list"][i]["shareid"].asInt64() != kSwlShareId)
            continue;
        CHECK(resp["list"][i]["membercount"].asInt() == 1);
        CHECK(resp["list"][i]["owneraccount"].asString() == "MY0127");
        CHECK_FALSE(resp["list"][i]["conversationname"].asString().empty());
    }
    Json::Value members = P3Req(ent1);
    members["shareid"] = static_cast<Json::Int64>(kSwlShareId);
    REQUIRE(P3Call("/admin/fileshare/members.do", members, resp) == 0);
    REQUIRE(resp["members"].size() == 1);
    CHECK(resp["members"][0]["userid"].asInt64() == 5308);
    CHECK(resp["members"][0]["perms"].asInt() == 31);

    const std::wstring work = FreshWork();
    const std::wstring root = work + L"\\p3cancel";
    REQUIRE(sharetestfs::MakeDirs(root));
    StlEnv groupOwner;   //MY0308
    groupOwner.Setup(work + L"\\personal\\");
    StlEnv sharer;       //MY0309
    sharer.Setup(work + L"\\personal_b\\", kSacAccount0309);

    //MY0309 在群里发起一个共享，企业 1 的管理员在后台强制取消
    SmlManageObserver obs;
    REQUIRE(SacManage(sharer, obs, sharer.m_service.CreateShare(
        SacGroupParams(root, std::vector<ShareMemberPerm>(1, Member(5308, 0))), root, &obs)) == 0);
    const int64_t shareId = obs.m_last.m_shareId;
    Json::Value cancel = P3Req(ent1);
    cancel["shareid"] = static_cast<Json::Int64>(shareId);
    REQUIRE(P3Call("/admin/fileshare/cancel.do", cancel, resp) == 0);
    CHECK(WaitCardState(groupOwner, shareId, share_state_invalid, share_invalid_admin_canceled));
    CHECK(WaitCardState(sharer, shareId, share_state_invalid, share_invalid_admin_canceled));
    CHECK(P3Call("/admin/fileshare/cancel.do", cancel, resp) == kP3AdminFileShareCanceled);
    //审计：发起（服务端，操作人 MY0309）与后台取消（操作人为管理员）
    Json::Value audit = P3Req(ent1);
    audit["shareid"] = static_cast<Json::Int64>(shareId);
    REQUIRE(P3Call("/admin/fileshare/audit.do", audit, resp) == 0);
    bool sawCreate = false;
    bool sawAdminCancel = false;
    for (Json::ArrayIndex i = 0; i < resp["list"].size(); ++i)
    {
        const Json::Value& r = resp["list"][i];
        if (r["action"].asInt() == share_audit_create_share && r["source"].asInt() == share_audit_source_server)
            sawCreate = true;
        if (r["action"].asInt() == share_audit_cancel_share && r["actorisadmin"].asInt() == 1
            && r["actorname"].asString() == kP3Ent1AdminName)
            sawAdminCancel = true;
    }
    CHECK(sawCreate);
    CHECK(sawAdminCancel);

    //MY0308 从共享 9008 下载一个文件：服务端记「申请下载」（来源服务端），分享者上报「下载文件」（来源分享者客户端）
    REQUIRE(sharetestfs::WriteFileBytes(std::wstring(kSwlRoot) + L"\\" + kP3DownloadFileW, kP3DownloadBytes));
    P3VisitorObserver vobs;
    CShareVisitor* visitor = groupOwner.m_service.AcquireVisitor(kSwlShareId, &vobs);
    REQUIRE(visitor != NULL);
    visitor->List("", static_cast<int64_t>(::GetTickCount64()));
    REQUIRE(P3WaitVisitor(groupOwner, visitor, share_visitor_ready));
    const DWORD listStart = ::GetTickCount();
    while (vobs.m_listedCount == 0 && ::GetTickCount() - listStart < kStlOpenWaitMs)
        groupOwner.PumpOnce();
    std::string fileName;
    int64_t fileSize = 0;
    for (size_t i = 0; i < vobs.m_entries.size() && fileName.empty(); ++i)
    {
        if (!vobs.m_entries[i].m_isDir && vobs.m_entries[i].m_name == kP3DownloadFile)
        {
            fileName = vobs.m_entries[i].m_name;
            fileSize = vobs.m_entries[i].m_size;
        }
    }
    REQUIRE_FALSE(fileName.empty());
    const std::wstring dl = work + L"\\p3dl";
    REQUIRE(sharetestfs::MakeDirs(dl));
    StlTransferObserver tobs;
    std::vector<ShareTransferEntry> items(1);
    items[0].m_path = fileName;
    items[0].m_size = fileSize;
    const uint32_t id = groupOwner.m_service.StartDownload(kSwlShareId, items, dl, &tobs);
    REQUIRE(id != 0);
    REQUIRE(groupOwner.WaitTransfer(tobs, id) == 0);
    Json::Value byShare = P3Req(platform);
    byShare["shareid"] = static_cast<Json::Int64>(kSwlShareId);
    byShare["actorkeyword"] = "MY0308";
    bool sawServer = false;
    bool sawClient = false;
    const DWORD auditStart = ::GetTickCount();
    while ((!sawServer || !sawClient) && ::GetTickCount() - auditStart < kP3AuditWaitMs)
    {
        groupOwner.PumpOnce();
        if (P3Call("/admin/fileshare/audit.do", byShare, resp) != 0)
            continue;
        for (Json::ArrayIndex i = 0; i < resp["list"].size(); ++i)
        {
            const Json::Value& r = resp["list"][i];
            if (r["action"].asInt() == share_audit_request_download && r["source"].asInt() == share_audit_source_server)
                sawServer = true;
            if (r["action"].asInt() == share_audit_download_file && r["source"].asInt() == share_audit_source_owner_client)
                sawClient = true;
        }
    }
    CHECK(sawServer);
    CHECK(sawClient);
    groupOwner.m_service.ReleaseVisitor(kSwlShareId);
    sharer.Shutdown();
    groupOwner.Shutdown();
}

TEST_CASE("SP3A4 跨企业：单聊对方或群里勾选的成员属于别的企业时发起被拒并标出这些人", "[.live][fileshare][p3accept]")
{
    const std::wstring work = FreshWork();
    const std::wstring root = work + L"\\p3cross";
    REQUIRE(sharetestfs::MakeDirs(root));
    StlEnv env;   //MY0308
    env.Setup(work + L"\\personal\\");
    SmlManageObserver obs;

    //单聊：对方 QN0151 属于企业 2
    ShareCreateParams single;
    single.m_ctype = conversation_single;
    single.m_cid = kP3OtherUserId;
    single.m_kind = share_kind_folder;
    single.m_name = "p3_cross";
    single.m_displayPath = ShareWideToUtf8(root);
    single.m_members.push_back(Member(kP3OtherUserId, 0));
    CHECK(SacManage(env, obs, env.m_service.CreateShare(single, root, &obs)) == error_code_share_cross_enterprise);
    REQUIRE(obs.m_last.m_invalidMembers.size() == 1);
    CHECK(obs.m_last.m_invalidMembers[0] == kP3OtherUserId);

    //群聊：跨企业测试群里同时勾选 MY0309 与 QN0151，只标出 QN0151
    ShareCreateParams group = single;
    group.m_ctype = conversation_group;
    group.m_cid = kP3CrossGroup;
    group.m_members.clear();
    group.m_members.push_back(Member(5309, 0));
    group.m_members.push_back(Member(kP3OtherUserId, 0));
    CHECK(SacManage(env, obs, env.m_service.CreateShare(group, root, &obs)) == error_code_share_cross_enterprise);
    REQUIRE(obs.m_last.m_invalidMembers.size() == 1);
    CHECK(obs.m_last.m_invalidMembers[0] == kP3OtherUserId);
    env.Shutdown();
}

//==================== 第 4 期：操作通知与后台传输（docs/文件共享方案.md 第 14 节）====================

namespace {

//SNL1 的共享根（每次运行前重建）、下载与上传的本机目录
const wchar_t* const kSnlRoot = L"E:\\flamingo\\tmp\\fsnotify\\live\\share_root";
const wchar_t* const kSnlDownloadDir = L"E:\\flamingo\\tmp\\fsnotify\\live\\download";
const wchar_t* const kSnlUploadSrc = L"E:\\flamingo\\tmp\\fsnotify\\live\\upload_src";

//共享根里的大文件字节数：几十兆，传输要持续一小段时间
const size_t kSnlBigBytes = 32 * 1024 * 1024;
//上传的小文件字节数
const size_t kSnlUpBytes = 4096;

//访问者账号 MY0308 的 userid（联调数据库里的固定值）
const int64_t kSnlVisitorUserId = 5308;

//各步骤的最长等待（毫秒）与回收检查的等待（多于两次定时检查）
const DWORD kSnlWaitMs = 60000;
const DWORD kSnlReapWaitMs = 5000;

//弹窗截图位置
const wchar_t* const kSnlSnapshotOwner = L"\\..\\..\\..\\snapshots\\fileshare_notice_live_owner.png";
const wchar_t* const kSnlSnapshotVisitor = L"\\..\\..\\..\\snapshots\\fileshare_notice_live_visitor.png";

//访问状态机的观察者：在 StlVisitorObserver 之上记下写操作的结果
class SnlVisitorObserver : public StlVisitorObserver
{
public:
    virtual void OnVisitorOpResult(int64_t reqId, int opType, int code, const std::vector<ShareOpItem>& items) override
    {
        (void)opType;
        (void)items;
        m_ops[reqId] = code;
    }

    std::map<int64_t, int> m_ops;   //请求号 → 结果码
};

//模块动作桩：浏览窗口一律不在前台（测试里没有窗口），其余动作只记下
class SnlActions : public IShareNoticeActions
{
public:
    virtual bool IsShareBrowserForeground(int64_t shareId) override
    {
        (void)shareId;
        return false;
    }

    virtual void NoticeOpenShareCenter(int64_t shareId) override
    {
        (void)shareId;
    }

    virtual void NoticeBrowse(int64_t shareId, const std::string& dir) override
    {
        (void)shareId;
        (void)dir;
    }

    virtual bool NoticeRetry(const ShareVisitorTransferResult& result) override
    {
        (void)result;
        return false;
    }

    virtual bool NoticeOpenPath(const std::wstring& path) override
    {
        (void)path;
        return false;
    }

    virtual bool NoticeRevealPath(const std::wstring& path) override
    {
        (void)path;
        return false;
    }
};

//把业务总控的两种通知同时交给记录桩与弹窗总控
class SnlFanout : public IShareTransferFinishListener, public IShareOwnerActivityListener
{
public:
    SnlFanout(CShareNoticeCenter& center)
        : m_center(center)
    {
    }

    virtual void OnVisitorTransferFinished(const ShareVisitorTransferResult& result) override
    {
        m_finish.OnVisitorTransferFinished(result);
        m_center.OnVisitorTransferFinished(result);
    }

    virtual void OnOwnerActivity(const ShareOwnerActivity& activity) override
    {
        m_activity.OnOwnerActivity(activity);
        m_center.OnOwnerActivity(activity);
    }

    CShareNoticeCenter&          m_center;     //弹窗总控（不拥有）
    noticetest::FinishRecorder   m_finish;     //访问者一端传输结束的记录
    noticetest::ActivityRecorder m_activity;   //分享者一端操作的记录
};

//两个环境轮流收帧、抽取消息，直到条件成立或超时
template <typename Pred>
bool SnlPumpUntil(StlEnv& a, StlEnv& b, Pred done, DWORD waitMs)
{
    const DWORD start = ::GetTickCount();
    while (!done() && ::GetTickCount() - start < waitMs)
    {
        a.PumpOnce();
        b.PumpOnce();
    }
    return done();
}

//文件字节数；不存在时为 -1
int64_t SnlFileSize(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!::GetFileAttributesExW(ShareToLongPath(path).c_str(), GetFileExInfoStandard, &data))
        return -1;
    return (static_cast<int64_t>(data.nFileSizeHigh) << kStlHighShift) | data.nFileSizeLow;
}

//条件：结束监听收到的结果条数达到 n
struct SnlFinishCount
{
    const noticetest::FinishRecorder& m_rec;   //记录桩
    size_t                            m_n;     //要达到的条数
    bool operator()() const
    {
        return m_rec.m_results.size() >= m_n;
    }
};

//条件：分享者操作的条数达到 n
struct SnlActivityCount
{
    const noticetest::ActivityRecorder& m_rec;   //记录桩
    size_t                              m_n;     //要达到的条数
    bool operator()() const
    {
        return m_rec.m_activities.size() >= m_n;
    }
};

//条件：列目录有了结果
struct SnlListed
{
    const StlVisitorObserver& m_obs;   //观察者
    int                       m_count; //要超过的次数
    bool operator()() const
    {
        return m_obs.m_listedCount > m_count;
    }
};

//条件：访问状态机已被回收
struct SnlVisitorGone
{
    const CShareService& m_service;   //访问者的业务总控
    int64_t              m_shareId;   //共享 id
    bool operator()() const
    {
        return !m_service.HasVisitor(m_shareId);
    }
};

} // namespace

/**
 *  SNL1（标签 [.live][fileshare][sharenoticelive]，第 4 期）：分享者 MY0127 与访问者 MY0308 都在测试进程里（各一个 StlEnv），
 *  服务端为 tmp/fsnotify/live（python tmp/fsnotify/live/setup_fslive.py 后用 start_server.ps1 启动，端口 25000）。
 *    1. 分享者在与 MY0308 的单聊里共享一个文件夹（含 32 MiB 的 big.bin），给全部写权限；
 *    2. 访问者打开、发起下载后立即「关闭浏览窗口」（ReleaseVisitor）：访问会话保留（分享者那边的提供会话还在），
 *       下载在后台完成、文件大小一致，结束监听收到成功结果与落地路径，已关闭的窗口不再收到回调；分享者收到一次下载通知；
 *       全部结束后的定时检查回收访问状态机，分享者的提供会话随之结束；
 *    3. 访问者重新打开并把 big.bin 重命名为 big2.bin：分享者收到重命名通知，选中的本机路径是共享根下的 big2.bin；
 *    4. 访问者上传一个文件后立即关闭窗口：上传在后台完成，分享者收到上传通知，选中的本机路径是共享根下的那个文件；
 *    5. 两边的弹窗总控各显示了对应的条目，截图存到 snapshots 供人工核对；最后取消共享。
 */
TEST_CASE("SNL1 transfers continue after the browser closes and both sides are notified", "[.live][fileshare][sharenoticelive]")
{
    //共享根与本机目录
    sharetestfs::DeleteTree(kSnlRoot);
    sharetestfs::DeleteTree(kSnlDownloadDir);
    sharetestfs::DeleteTree(kSnlUploadSrc);
    REQUIRE(sharetestfs::MakeDirs(kSnlRoot));
    REQUIRE(sharetestfs::MakeDirs(kSnlDownloadDir));
    REQUIRE(sharetestfs::MakeDirs(kSnlUploadSrc));
    REQUIRE(sharetestfs::WriteFileBytes(std::wstring(kSnlRoot) + L"\\big.bin", kSnlBigBytes));
    REQUIRE(sharetestfs::WriteFileBytes(std::wstring(kSnlUploadSrc) + L"\\up.txt", kSnlUpBytes));
    const std::wstring base = sharetestfs::ExeDir() + L"\\snl_work";
    sharetestfs::DeleteTree(base);
    REQUIRE(sharetestfs::MakeDirs(base + L"\\owner\\ChatLog"));
    REQUIRE(sharetestfs::MakeDirs(base + L"\\visitor\\ChatLog"));

    //两边的弹窗总控与通知分发：声明在环境之前，环境先 Shutdown（会结束传输并回调它们）
    SnlActions actions;
    noticetest::NoticeTestHost noticeHost;
    CShareNoticeCenter ownerNotices;
    CShareNoticeCenter visitorNotices;
    ownerNotices.Attach(&noticeHost, &actions);
    visitorNotices.Attach(&noticeHost, &actions);
    SnlFanout ownerFan(ownerNotices);
    SnlFanout visitorFan(visitorNotices);

    StlEnv owner;
    owner.Setup(base + L"\\owner\\", "MY0127");
    StlEnv visitor;
    visitor.Setup(base + L"\\visitor\\", "MY0308");
    owner.m_service.SetNoticeListeners(&ownerFan, &ownerFan);
    visitor.m_service.SetNoticeListeners(&visitorFan, &visitorFan);

    //1. 分享者发起共享
    ShareCreateParams params;
    params.m_ctype = conversation_single;
    params.m_cid = kSnlVisitorUserId;
    params.m_kind = share_kind_folder;
    params.m_name = "snl-share";
    params.m_displayPath = ShareWideToUtf8(kSnlRoot);
    ShareMemberPerm member;
    member.m_userId = kSnlVisitorUserId;
    member.m_perms = share_perm_upload | share_perm_mkdir | share_perm_rename | share_perm_delete | share_perm_move;
    params.m_members.push_back(member);
    SmlManageObserver manage;
    REQUIRE(SmlWaitManage(owner, manage, owner.m_service.CreateShare(params, kSnlRoot, &manage)) == 0);
    const int64_t shareId = manage.m_last.m_shareId;
    REQUIRE(shareId > 0);

    //2. 访问者打开共享、列根目录
    SnlVisitorObserver window;
    CShareVisitor* visitorSm = visitor.m_service.AcquireVisitor(shareId, &window);
    REQUIRE(visitorSm != NULL);
    visitorSm->List("", static_cast<int64_t>(::GetTickCount64()));
    REQUIRE(SnlPumpUntil(owner, visitor, SnlListed{ window, 0 }, kSnlWaitMs));
    REQUIRE(window.m_lastCode == 0);
    REQUIRE(visitorSm->GetState() == share_visitor_ready);

    //发起下载后立即关闭窗口：访问会话保留，下载在后台继续
    StlTransferObserver xfer;
    ShareTransferEntry big;
    big.m_path = "big.bin";
    big.m_size = static_cast<int64_t>(kSnlBigBytes);
    ShareTransferOptions options;
    options.m_label = L"big.bin";
    const uint32_t downloadId = visitor.m_service.StartDownload(shareId, std::vector<ShareTransferEntry>(1, big), kSnlDownloadDir,
                                                                &xfer, options);
    REQUIRE(downloadId != 0);
    visitor.m_service.ReleaseVisitor(shareId);
    CHECK(visitor.m_service.HasVisitor(shareId));
    CHECK_FALSE(visitor.m_service.IsVisitorAttached(shareId));
    CHECK(visitor.m_service.GetTransferCount(false) == 1);

    REQUIRE(SnlPumpUntil(owner, visitor, SnlFinishCount{ visitorFan.m_finish, 1 }, kSnlWaitMs));
    const ShareVisitorTransferResult done = visitorFan.m_finish.m_results[0];
    CHECK(done.m_localId == downloadId);
    CHECK(done.m_code == 0);
    CHECK_FALSE(done.m_userCanceled);
    CHECK(done.m_shareName == "snl-share");
    CHECK(done.m_localTopPath == std::wstring(kSnlDownloadDir) + L"\\big.bin");
    CHECK(SnlFileSize(std::wstring(kSnlDownloadDir) + L"\\big.bin") == static_cast<int64_t>(kSnlBigBytes));
    //已关闭的窗口不再收到回调
    CHECK_FALSE(xfer.Finished(downloadId));

    //分享者收到一次下载通知
    REQUIRE(SnlPumpUntil(owner, visitor, SnlActivityCount{ ownerFan.m_activity, 1 }, kSnlWaitMs));
    CHECK(ownerFan.m_activity.m_activities[0].m_action == share_audit_download_file);
    CHECK(ownerFan.m_activity.m_activities[0].m_firstName == "big.bin");
    CHECK(ownerFan.m_activity.m_activities[0].m_shareName == "snl-share");
    CHECK(ownerFan.m_activity.m_activities[0].m_visitorId == kSnlVisitorUserId);

    //全部结束后的定时检查回收访问状态机（结束访问会话），分享者的提供会话随之结束
    REQUIRE(SnlPumpUntil(owner, visitor, SnlVisitorGone{ visitor.m_service, shareId }, kSnlReapWaitMs));
    struct OwnerIdle
    {
        const CShareService& m_service;
        int64_t              m_shareId;
        bool operator()() const
        {
            return m_service.GetActiveVisitors(m_shareId).empty();
        }
    };
    CHECK(SnlPumpUntil(owner, visitor, OwnerIdle{ owner.m_service, shareId }, kSnlReapWaitMs));

    //3. 重新打开并重命名：分享者收到重命名通知，选中的本机路径是新名字
    SnlVisitorObserver window2;
    visitorSm = visitor.m_service.AcquireVisitor(shareId, &window2);
    REQUIRE(visitorSm != NULL);
    visitorSm->Rename("big.bin", "big2.bin", static_cast<int64_t>(::GetTickCount64()));
    REQUIRE(SnlPumpUntil(owner, visitor, SnlActivityCount{ ownerFan.m_activity, 2 }, kSnlWaitMs));
    const ShareOwnerActivity renamed = ownerFan.m_activity.m_activities[1];
    CHECK(renamed.m_action == share_audit_rename);
    CHECK(renamed.m_firstName == "big.bin");
    CHECK(renamed.m_targetName == "big2.bin");
    CHECK(renamed.m_revealPath == std::wstring(kSnlRoot) + L"\\big2.bin");

    //4. 上传后立即关闭窗口：上传在后台完成，分享者收到上传通知
    int errCode = 0;
    ShareTransferOptions upOptions;
    upOptions.m_label = L"up.txt";
    const uint32_t uploadId = visitor.m_service.StartUpload(shareId, std::vector<std::wstring>(1, std::wstring(kSnlUploadSrc)
                                                            + L"\\up.txt"), std::string(), &xfer, errCode, upOptions);
    REQUIRE(uploadId != 0);
    visitor.m_service.ReleaseVisitor(shareId);
    REQUIRE(SnlPumpUntil(owner, visitor, SnlFinishCount{ visitorFan.m_finish, 2 }, kSnlWaitMs));
    CHECK(visitorFan.m_finish.m_results[1].m_localId == uploadId);
    CHECK(visitorFan.m_finish.m_results[1].m_code == 0);
    CHECK(visitorFan.m_finish.m_results[1].m_direction == share_transfer_upload);
    CHECK(SnlFileSize(std::wstring(kSnlRoot) + L"\\up.txt") == static_cast<int64_t>(kSnlUpBytes));
    REQUIRE(SnlPumpUntil(owner, visitor, SnlActivityCount{ ownerFan.m_activity, 3 }, kSnlWaitMs));
    const ShareOwnerActivity uploaded = ownerFan.m_activity.m_activities[2];
    CHECK(uploaded.m_action == share_audit_upload_file);
    CHECK(uploaded.m_firstName == "up.txt");
    CHECK(uploaded.m_revealPath == std::wstring(kSnlRoot) + L"\\up.txt");

    //5. 两边的弹窗：分享者三条（下载、重命名、上传），访问者两条（下载完成、上传完成）
    CHECK(ownerNotices.GetEntryCount(share_notice_popup_owner) == 3);
    CHECK(visitorNotices.GetEntryCount(share_notice_popup_visitor) == 2);
    CShareNoticePopup& ownerPopup = ownerNotices.GetPopup(share_notice_popup_owner);
    CShareNoticePopup& visitorPopup = visitorNotices.GetPopup(share_notice_popup_visitor);
    REQUIRE(ownerPopup.IsShowing());
    REQUIRE(visitorPopup.IsShowing());
    CHECK(ownerPopup.GetRowTitle(0).find(L"上传了 up.txt") != std::wstring::npos);
    CHECK(visitorPopup.GetRowTitle(0) == L"上传完成：up.txt");
    CHECK(visitorPopup.GetRowTitle(1) == L"下载完成：big.bin");
    for (int i = 0; i < kStlSettleRounds; ++i)
    {
        owner.PumpOnce();
        visitor.PumpOnce();
    }
    CHECK(sharesnapshot::SaveWindowPng(ownerPopup.m_hWnd, sharetestfs::ExeDir() + kSnlSnapshotOwner));
    CHECK(sharesnapshot::SaveWindowPng(visitorPopup.m_hWnd, sharetestfs::ExeDir() + kSnlSnapshotVisitor));

    //收尾：取消共享
    manage.m_done = false;
    CHECK(SmlWaitManage(owner, manage, owner.m_service.CancelShare(shareId, &manage)) == 0);
    owner.m_service.SetNoticeListeners(NULL, NULL);
    visitor.m_service.SetNoticeListeners(NULL, NULL);
    visitor.Shutdown();
    owner.Shutdown();
    ownerNotices.Detach();
    visitorNotices.Detach();
}
