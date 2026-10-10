/**
 *  FlamingoFileShare.dll 加载与模块生命周期单元测试（Suite FSD，标签 [filesharedll]），FileShareModuleTests.cpp
 *  覆盖（对应 docs/文件共享方案.md 第 1 期第 2 步，2026-10-02 与用户确认的用例）：
 *    - PML3 用错误的接口版本号加载，返回「版本不一致」，DLL 被卸载；
 *    - PML4 正常加载、取到模块、卸载，连做两轮；
 *    - FSD1 Init 成功，宿主收到一行带 "[share]" 前缀与调用处函数签名的初始化日志；
 *    - FSD2 重复 Init 只初始化一次；宿主、参数为空或结构体大小不符时 Init 失败；
 *    - FSD3 未初始化就 Shutdown、重复 Shutdown 都安全；Shutdown 之后不再回调宿主；
 *    - FSD4 服务端帧：号段外的命令、未初始化、包体为空而长度不为 0 都返回「未处理」，
 *           初始化之后 2200 段的帧返回「已接收」，并能在 UI 线程上被取出；
 *    - FSD5 第二份 balloonui：测试程序先用自己那份 balloonui 建一个窗口占住同名窗口类，
 *           DLL 再打开「文件共享」窗口，验证它能创建、能显示、窗口类归 DLL 所有，
 *           Shutdown 之后窗口被销毁（方案 3.11 的推测）；
 *    - FSD6 登录后经宿主发出一条 2205（拉取「我的共享」），收到应答后记一行带结果码与条数的摘要
 *           （第 1 期第 10 步，用于验证转发通道；第 12 步起登录时先经 GetSessionInfo 取个人目录）；
 *    - FSD7 上线比对（第 12 步）：预先写好本机库，模拟 2205 应答后，DLL 删除服务端已没有的本机记录、
 *           绑定别的设备的记录保留，2210 只报绑定本机且有记录的共享并按共享根是否存在给出原因；
 *           同一个 seq 的应答重复到达时不再报告；
 *    - FSD8 分享者收到 2222（第 12 步）：本机没有记录回 2221（804）；共享根不存在回 2221（805）并补报 2210；
 *           传输子会话没带方向时回 2225（3）；正常时建提供会话，后台连接中转被拒后回 2221（817）；
 *           收到 2203 已失效时删除本机记录；
 *    - FSD9 宿主取不到会话信息时不拉取共享列表，记一条错误日志；
 *    - FSD10 访问者打开共享（第 13 步）：OpenShareFromCard 打开浏览窗口并发 2220；服务端回错误码时浏览窗口显示失败；
 *           同一个共享再次打开只前置；2220 成功、收到推给访问者的 2222 后连接中转，被拒时回 2221（817）；
 *    - FSD11 共享根可提供与否变化时补报 2210（第 14 步：定时检查代替 WM_DEVICECHANGE）：删掉共享根后补报「共享根不存在」，
 *           恢复后补报「可提供」。
 *    - FSD12 发起共享对话框（第 15 步）：OpenCreateShare 打开对话框并向宿主要会话成员；同一会话再次打开只前置、不重复要；
 *           会话 id 非法时失败；宿主回送成员后对话框记一行到达的日志；退出登录时对话框关闭。
 *    - FSD13 P2P 业务方（docs/P2P通信方案.md 第 6 期）：宿主提供 P2P 服务时，Init 以用途 fileshare 登记业务方、Shutdown
 *           注销同一个用途；宿主不提供时不登记，记一行「走中转」的日志。
 *  PML1 / PML2 / PML5 不需要真实的 DLL，在 Source/Tests/PluginModuleLoaderTests.cpp。
 *
 *  本文件由 FlamingoUnit 编译；FlamingoUnit 构建后把同配置的 FlamingoFileShare.dll 复制到测试程序
 *  所在目录（见 FlamingoUnit.vcxproj 末尾的 CopyFlamingoFileShareDll）。
 *  框架 Catch2 v3。
 *  balloonwj@qq.com   2026-10-02
 */
#include "stdafx.h"

#include "catch_amalgamated.hpp"

#include <windows.h>
#include <string>
#include <utility>
#include <mutex>     //StubHost：DLL 的后台线程也会写日志
#include <vector>

#include <stdio.h>    //_snprintf_s
#include <string.h>   //strcpy_s

#include "../../Source/App/PluginModuleLoader.h"
#include "../../Source/net/Protocol.h"
#include "../FileShareApi.h"
#include "../../FlamingoP2P/FlamingoP2PApi.h"   //FSD13 的假 P2P 服务
#include "../Engine/ShareLocalStore.h"
#include "../Engine/ShareProtocolCodec.h"
#include "ShareTestFs.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"

using balloonapp::CPluginModuleLoader;
using balloonapp::plugin_load_ok;
using balloonapp::plugin_load_version_mismatch;
using fileshare::IFileShareHost;
using fileshare::IFileShareModule;
using fileshare::FileShareInitParams;
using fileshare::CShareLocalStore;
using fileshare::ShareLocalRecord;
using fileshare::ShareProtocolCodec;
using fileshare::ShareServeItem;

namespace {

//日志行固定的前缀（见 FlamingoFileShare/Engine/ShareLog.h）
const char* const kSharePrefix = "[share] ";

//初始化日志所含的固定文字与调用处（见 FileShareModule.cpp 的 Init）
const char* const kInitLogMarker   = "FlamingoFileShare initialized";
const char* const kInitLogCallSite = "CFileShareModule::Init";

//关闭日志所含的固定文字（见 FileShareModule.cpp 的 Shutdown）
const char* const kShutdownLogMarker = "FlamingoFileShare shutting down";

//UI 线程取出一帧时写的日志所含的固定文字（见 FileShareModule.cpp 的 DrainFrames）
const char* const kFrameLogMarker = "share frame received, cmd: 2205, seq: 7, body bytes: 7.";

//「文件共享」窗口的标题（见 UI/ShareCenterWindow.cpp）
const wchar_t* const kCenterWindowTitle = L"文件共享";

//几个命令号：文件共享段的首尾、段内一条带应答的命令、段外紧挨着的两个号、一条消息段的命令
const uint16_t kShareFirstCmd   = 2200;
const uint16_t kShareCreateCmd  = 2201;
const uint16_t kShareListCmd    = 2205;
const uint16_t kShareLastCmd    = 2299;
const uint16_t kBelowShareCmd   = 2199;
const uint16_t kAboveShareCmd   = 2300;
const uint16_t kMessageNotify   = 1504;

//FSD4 投递的那一帧的 seq 与包体
const uint32_t kListSeq  = 7;
const char* const kListBody = "{\"a\":1}";

//假宿主 SendShareCommand 返回的帧 seq（模拟已放入发送队列）
const uint32_t kStubSendSeq = 41;

//FSD6：DLL 登录后拉取「我的共享」的请求包体，以及模拟服务端回来的应答与 DLL 记下的摘要
const char* const kListMineBody = "{\"scope\":1}";
const char* const kListResponseHead = "{\"code\":0,\"msg\":\"ok\",\"scope\":1,\"shares\":[";
const char* const kListSummaryMarker = "share list response, seq: 41, code: 0, shares: 2.";

//「包体为空而长度不为 0」用的长度
const uint32_t kBogusLength = 5;

//假宿主给出的本人 userid
const int64_t kStubUserId = 5127;

//FSD6~FSD9 的个人目录（测试程序目录下，每个用例开始前整个删掉）
const wchar_t* const kPersonalDirName = L"fsd_personal";

//FSD7：本机记录与服务端列表里用到的几个共享 id
const int64_t kShareReport = 11;        //服务端有、本机有、绑定本机、共享根存在
const int64_t kShareRootGone = 12;      //服务端有、本机有、绑定本机、共享根已不存在
const int64_t kShareLocalOnly = 13;     //只有本机有：比对后删除本机记录
const int64_t kShareMissingLocal = 14;  //服务端有、绑定本机、本机没有记录
const int64_t kShareOtherDevice = 15;   //绑定别的设备

//FSD8：中转参数里的访问会话 id、传输子会话 id、访问者
const char* const kAccessNoRecord = "share-1-100";
const char* const kAccessRootGone = "share-2-100";
const char* const kAccessConnect = "share-3-100";
const char* const kTransferId = "share-3-100-t1";
const int64_t kVisitorUserId = 10002;

//FSD8：等待连接失败事件的最长时间（毫秒）与每次等待的片长（毫秒）。连接本机未监听的端口在 Windows 上
//会重试几次才失败，最长不超过 FolderRelayClient 的 5 秒连接超时
const DWORD kConnectFailWaitMs = 10000;
const DWORD kPumpSliceMs = 50;

//FSD8：连接失败用的中转地址：本机 1 号端口（tcpmux）几乎不会有程序监听，连接会被拒绝
const char* const kClosedRelayIp = "127.0.0.1";
const int kClosedRelayPort = 1;

//FSD10：访问者打开的两个共享
const int64_t kVisitShareA = 9001;
const int64_t kVisitShareB = 9002;
//FSD10：处理推给访问者的 2222 时，UI 线程在启动连接之后同步写出的那行日志
const char* const kFsd10ConnectingMarker = "share visitor channel connecting, accessid: share-9-100";

//FSD11：被检查共享根的共享，以及等补报的最长时间（毫秒）：检查间隔 5 秒，留出余量
const int64_t kPollShareId = 31;
const DWORD kRootPollWaitMs = 9000;

//FSD7：2205 应答里一项的模板（shareid、onthisdevice 待填）
const char* const kListItemFmt = "{\"shareid\":\"%lld\",\"ownerid\":\"5127\",\"ctype\":1,\"cid\":\"10002\",\"kind\":2,"
                                 "\"name\":\"n\",\"displaypath\":\"d\",\"includehidden\":0,\"state\":1,\"reason\":0,"
                                 "\"createtime\":1,\"onthisdevice\":%d,\"members\":[]}";

//FSD7：拼一项时用的缓冲区字节数
const size_t kListItemBufBytes = 512;

//抽取消息时最多循环的轮数，防止某个窗口不停地给自己发消息让用例卡死
const int kMaxPumpRounds = 200;

//测试用宿主桩：记下 DLL 写来的每一行日志及其级别、每一次发信令请求
class StubHost : public IFileShareHost
{
public:
    virtual void Log(int level, const char* text) override
    {
        if (text != NULL)
        {
            //DLL 的后台线程（中转连接线程等）也会写日志，与测试线程的读取并发（BUG-82）
            std::lock_guard<std::mutex> guard(m_mutex);
            m_levels.push_back(level);
            m_lines.push_back(text);
        }
    }

    virtual uint32_t SendShareCommand(uint16_t cmd, const char* json, uint32_t len) override
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_sentCmds.push_back(cmd);
        m_sentBodies.push_back(std::string(json != NULL ? json : "", len));
        return kStubSendSeq;
    }

    virtual const unsigned char* GetServerPublicKey() override
    {
        return NULL;
    }

    virtual bool GetSessionInfo(fileshare::FileShareSessionInfo* info) override
    {
        if (!m_sessionOk || info == NULL || info->m_cbSize != sizeof(fileshare::FileShareSessionInfo))
            return false;
        info->m_userId = kStubUserId;
        strcpy_s(info->m_account, fileshare::kFileShareTextBytes, "fsdtester");
        strcpy_s(info->m_nickName, fileshare::kFileShareTextBytes, "tester");
        strcpy_s(info->m_deviceId, fileshare::kFileShareTextBytes, "PC-FSD");
        wcsncpy_s(info->m_personalDir, fileshare::kFileSharePathChars, m_personalDir.c_str(), _TRUNCATE);
        return true;
    }

    virtual bool RequestConversationMembers(int ctype, int64_t cid) override
    {
        m_memberRequests.push_back(std::make_pair(ctype, cid));
        return true;
    }

    virtual bool GetUserDisplayName(int64_t userId, char* buf, uint32_t bufBytes) override
    {
        if (buf == NULL || bufBytes == 0)
            return false;
        _snprintf_s(buf, bufBytes, _TRUNCATE, "user%lld", static_cast<long long>(userId));
        return true;
    }

    virtual void NotifyShareState(int64_t shareId, int state, int reason, int myPerms) override
    {
        m_stateNotifies.push_back(shareId);
        m_lastState = state;
        m_lastReason = reason;
        m_lastPerms = myPerms;
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

    //模块骨架用例不看头像：一律不给
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

    //P2P 服务：默认不提供（返回 NULL）；FSD13 给它一个假的服务对象，检查模块登记与注销的用途
    virtual p2p::IP2PService* GetP2PService() override
    {
        return m_p2pService;
    }

    //第 12 版：弹窗通知照常弹出（本组用例不产生通知）
    virtual int QueryNoticePolicy() override
    {
        return fileshare::fileshare_notice_show;
    }

    //第 12 版：不出声
    virtual void PlayNoticeSound() override
    {
    }

    p2p::IP2PService*                     m_p2pService = NULL;     //交给模块的 P2P 服务（借用）；NULL 表示不提供

    std::vector<std::pair<int, int64_t> > m_openedConversations;   //请宿主定位过的会话：(会话类型, 会话 id)
    std::vector<std::wstring>             m_openedImages;          //请宿主用看图窗打开过的图片，按顺序
    bool                                  m_lastImageNavigable = false;   //最近一次打开时是否可以翻页

    //全部日志行，按行拼成一段文字（BUG-82 诊断：用例失败时打印出来）
    std::string AllLines() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        std::string out;
        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            out += m_lines[i];
            out += "\n";
        }
        return out;
    }

    //含指定文字的日志行数
    int CountLinesContaining(const char* marker) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        int n = 0;
        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            if (m_lines[i].find(marker) != std::string::npos)
                ++n;
        }
        return n;
    }

    //第一条含指定文字的日志的下标；没有时返回 -1
    int IndexOfFirstLineContaining(const char* marker) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (size_t i = 0; i < m_lines.size(); ++i)
        {
            if (m_lines[i].find(marker) != std::string::npos)
                return static_cast<int>(i);
        }
        return -1;
    }

    //DLL 请求发送某个命令的次数
    int CountSent(uint16_t cmd) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        int n = 0;
        for (size_t i = 0; i < m_sentCmds.size(); ++i)
        {
            if (m_sentCmds[i] == cmd)
                ++n;
        }
        return n;
    }

    //DLL 最后一次请求发送某个命令时的包体；没有发过时返回空串
    std::string LastSentBody(uint16_t cmd) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (size_t i = m_sentCmds.size(); i > 0; --i)
        {
            if (m_sentCmds[i - 1] == cmd)
                return m_sentBodies[i - 1];
        }
        return std::string();
    }

    //日志行数
    size_t LineCount() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_lines.size();
    }

    //第 index 行日志的正文与级别；越界时返回空串与 -1
    std::string LineAt(size_t index) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return (index < m_lines.size()) ? m_lines[index] : std::string();
    }

    int LevelAt(size_t index) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return (index < m_levels.size()) ? m_levels[index] : -1;
    }

    //DLL 请求发送命令的总次数
    size_t SentCount() const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return m_sentCmds.size();
    }

    //第 index 次请求发送的命令号与包体；越界时返回 0 与空串
    uint16_t SentCmdAt(size_t index) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return (index < m_sentCmds.size()) ? m_sentCmds[index] : 0;
    }

    std::string SentBodyAt(size_t index) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        return (index < m_sentBodies.size()) ? m_sentBodies[index] : std::string();
    }

    StubHost()
        : m_sessionOk(true)
        , m_lastState(0)
        , m_lastReason(0)
        , m_lastPerms(0)
    {
    }

    //保护下面四项：DLL 的后台线程也会写日志、发信令，测试代码经上面的函数读取，不直接访问这四项（BUG-82）
    mutable std::mutex       m_mutex;
    std::vector<int>         m_levels;      //各行日志的级别
    std::vector<std::string> m_lines;       //各行日志正文
    std::vector<uint16_t>    m_sentCmds;    //DLL 请求发送的命令号
    std::vector<std::string> m_sentBodies;  //DLL 请求发送的包体，与 m_sentCmds 一一对应
    bool                     m_sessionOk;   //GetSessionInfo 是否成功（模拟未登录时置假）
    std::wstring             m_personalDir; //GetSessionInfo 给出的个人目录（带结尾反斜杠）；DLL 在其下建 ChatLog\fileshare.db
    std::vector<std::pair<int, int64_t> > m_memberRequests;   //DLL 请求取会话成员的记录：(会话类型, 会话 id)
    std::vector<int64_t>     m_stateNotifies;  //DLL 通知卡片状态变化的共享 id，按到达顺序
    int                      m_lastState;      //最近一次通知的状态
    int                      m_lastReason;     //最近一次通知的原因
    int                      m_lastPerms;      //最近一次通知的写权限位
};

//测试程序所在目录（不带结尾反斜杠）
std::wstring ExeDir()
{
    wchar_t buf[MAX_PATH] = { 0 };
    ::GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring s(buf);
    size_t pos = s.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? s : s.substr(0, pos);
}

//测试程序旁边那份 FlamingoFileShare.dll 的完整路径
std::wstring FileShareDllPath()
{
    return ExeDir() + L"\\" + FLAMINGO_FILESHARE_DLL_NAME;
}

//合法的初始化参数（appDir 取测试程序所在目录，调用方须保证 appDir 在 Init 期间有效）
FileShareInitParams MakeParams(const std::wstring& appDir)
{
    FileShareInitParams p;
    p.m_cbSize = sizeof(FileShareInitParams);
    p.m_appDir = appDir.c_str();
    return p;
}

//把本线程消息队列里现有的消息全部取出分发（让 DLL 的消息窗口与 balloonui 窗口处理投递来的消息）
void PumpPendingMessages()
{
    MSG msg;
    for (int round = 0; round < kMaxPumpRounds; ++round)
    {
        if (!::PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            break;
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

//FSD6~FSD9 的个人目录：先整个删掉再返回（带结尾反斜杠，与 CUserMgr::GetPersonalFolder 的形式一致）
std::wstring FreshPersonalDir()
{
    const std::wstring dir = ExeDir() + L"\\" + kPersonalDirName;
    sharetestfs::DeleteTree(dir);
    sharetestfs::MakeDirs(dir);
    return dir + L"\\";
}

//个人目录下 DLL 使用的本机库路径
std::wstring StorePath(const std::wstring& personalDir)
{
    return personalDir + L"ChatLog\\fileshare.db";
}

//在测试程序里预先写一条已经拿到 shareid 的本机记录
void SeedLocalShare(CShareLocalStore& store, int64_t shareId, const std::wstring& root)
{
    const std::string clientId = "fsd-" + std::to_string(static_cast<long long>(shareId));
    REQUIRE(store.AddPendingShare(clientId, root, share_kind_folder, false, 1));
    REQUIRE(store.BindShareId(clientId, shareId));
}

//2210 里「共享根不存在」的一项
ShareServeItem RootMissingItem(int64_t shareId)
{
    ShareServeItem item;
    item.m_shareId = shareId;
    item.m_available = false;
    item.m_reason = share_unavailable_root_missing;
    return item;
}

//2205 应答里的一项
std::string ListItemJson(int64_t shareId, bool onThisDevice)
{
    char buf[kListItemBufBytes] = { 0 };
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, kListItemFmt, static_cast<long long>(shareId), onThisDevice ? 1 : 0);
    return buf;
}

//2222 推给分享者的那一帧
std::string RelayOwnerJson(const char* accessId, int64_t shareId, const char* transferId, const char* ip, int port)
{
    std::string s = "{\"accessid\":\"";
    s += accessId;
    s += "\",\"idletimeoutms\":600000,\"lanes\":0,\"opentimeoutms\":15000,\"perms\":1,\"relayip\":\"";
    s += ip;
    s += "\",\"relayport\":" + std::to_string(static_cast<long long>(port));
    s += ",\"role\":9,\"searchmaxresults\":500,\"searchtimeoutms\":10000,\"shareid\":\"";
    s += std::to_string(static_cast<long long>(shareId));
    s += "\",\"token\":\"tok\",\"transferid\":\"";
    s += transferId;
    s += "\",\"visitorid\":\"" + std::to_string(static_cast<long long>(kVisitorUserId)) + "\",\"visitorname\":\"Bob\"}";
    return s;
}

//把一帧服务端信令交给模块并立即在本线程上处理
void Deliver(IFileShareModule* module, uint16_t cmd, uint32_t seq, const std::string& body)
{
    REQUIRE(module->OnServerFrame(cmd, seq, body.data(), static_cast<uint32_t>(body.size())));
    PumpPendingMessages();
}

//持续抽取消息，直到宿主收到某条命令的次数达到 count，或超过 maxMs 毫秒
bool PumpUntilSent(const StubHost& host, uint16_t cmd, int count, DWORD maxMs)
{
    const DWORD start = ::GetTickCount();
    while (host.CountSent(cmd) < count)
    {
        if (::GetTickCount() - start > maxMs)
            return false;
        ::MsgWaitForMultipleObjects(0, NULL, FALSE, kPumpSliceMs, QS_ALLINPUT);
        PumpPendingMessages();
    }
    return true;
}

//EnumThreadWindows 的上下文：按标题与模块实例找本线程的顶层窗口
struct FindWindowContext
{
    const wchar_t* m_title;      //要找的窗口标题
    HINSTANCE      m_instance;   //要求窗口类所属的模块实例
    HWND           m_found;      //找到的窗口；没找到为 NULL
};

BOOL CALLBACK FindWindowByTitleAndInstance(HWND hWnd, LPARAM lParam)
{
    FindWindowContext* ctx = reinterpret_cast<FindWindowContext*>(lParam);
    wchar_t title[MAX_PATH] = { 0 };
    ::GetWindowTextW(hWnd, title, MAX_PATH);
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(::GetWindowLongPtrW(hWnd, GWLP_HINSTANCE));
    if (wcscmp(title, ctx->m_title) == 0 && instance == ctx->m_instance)
    {
        ctx->m_found = hWnd;
        return FALSE;
    }
    return TRUE;
}

//在本线程的顶层窗口里找「文件共享」窗口，且其窗口类必须归 FlamingoFileShare.dll 所有
HWND FindCenterWindow()
{
    FindWindowContext ctx;
    ctx.m_title = kCenterWindowTitle;
    ctx.m_instance = ::GetModuleHandleW(FLAMINGO_FILESHARE_DLL_NAME);
    ctx.m_found = NULL;
    if (ctx.m_instance == NULL)
        return NULL;
    ::EnumThreadWindows(::GetCurrentThreadId(), &FindWindowByTitleAndInstance,
                        reinterpret_cast<LPARAM>(&ctx));
    return ctx.m_found;
}

//加载 DLL 并返回模块；失败时让用例直接失败
IFileShareModule* LoadModule(CPluginModuleLoader<IFileShareModule>& loader)
{
    std::wstring path = FileShareDllPath();
    REQUIRE(::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES);
    REQUIRE(loader.Load(path.c_str(), FLAMINGO_FILESHARE_FACTORY_NAME, fileshare::kApiVersion) == plugin_load_ok);
    REQUIRE(loader.GetModule() != NULL);
    return loader.GetModule();
}

} // namespace

TEST_CASE("PML3 接口版本不一致", "[pluginloader][filesharedll]")
{
    std::wstring path = FileShareDllPath();
    REQUIRE(::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES);

    CPluginModuleLoader<IFileShareModule> loader;
    CHECK(loader.Load(path.c_str(), FLAMINGO_FILESHARE_FACTORY_NAME, fileshare::kApiVersion + 1)
          == plugin_load_version_mismatch);
    CHECK_FALSE(loader.IsLoaded());
    CHECK(loader.GetModule() == NULL);
    //版本被拒之后 DLL 已经卸载，进程里不应再有它
    CHECK(::GetModuleHandleW(FLAMINGO_FILESHARE_DLL_NAME) == NULL);
}

TEST_CASE("PML4 正常加载、取到模块、卸载，连做两轮", "[pluginloader][filesharedll]")
{
    for (int round = 0; round < 2; ++round)
    {
        CPluginModuleLoader<IFileShareModule> loader;
        IFileShareModule* module = LoadModule(loader);
        CHECK(loader.IsLoaded());
        CHECK(module->GetApiVersion() == fileshare::kApiVersion);
        CHECK(::GetModuleHandleW(FLAMINGO_FILESHARE_DLL_NAME) != NULL);

        loader.Unload();
        CHECK_FALSE(loader.IsLoaded());
        CHECK(loader.GetModule() == NULL);
        CHECK(::GetModuleHandleW(FLAMINGO_FILESHARE_DLL_NAME) == NULL);
    }
}

TEST_CASE("FSD1 Init 成功并经宿主写出带前缀与调用处的初始化日志", "[filesharedll]")
{
    CPluginModuleLoader<IFileShareModule> loader;
    IFileShareModule* module = LoadModule(loader);

    StubHost host;
    std::wstring appDir = ExeDir();
    FileShareInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));

    REQUIRE(host.CountLinesContaining(kInitLogMarker) == 1);
    int index = host.IndexOfFirstLineContaining(kInitLogMarker);
    const std::string line = host.LineAt(static_cast<size_t>(index));
    CHECK(line.compare(0, strlen(kSharePrefix), kSharePrefix) == 0);
    CHECK(line.find(kInitLogCallSite) != std::string::npos);
    CHECK(host.LevelAt(static_cast<size_t>(index)) == fileshare::fileshare_log_info);

    module->Shutdown();
    loader.Unload();
}

TEST_CASE("FSD2 重复 Init 只初始化一次，非法参数 Init 失败", "[filesharedll]")
{
    CPluginModuleLoader<IFileShareModule> loader;
    IFileShareModule* module = LoadModule(loader);

    StubHost host;
    std::wstring appDir = ExeDir();
    FileShareInitParams params = MakeParams(appDir);

    //非法参数：宿主为空、参数为空、结构体大小不符、目录为空，都失败且不写日志
    CHECK_FALSE(module->Init(NULL, &params));
    CHECK_FALSE(module->Init(&host, NULL));
    FileShareInitParams badSize = params;
    badSize.m_cbSize = sizeof(FileShareInitParams) + 1;
    CHECK_FALSE(module->Init(&host, &badSize));
    FileShareInitParams noDir = params;
    noDir.m_appDir = NULL;
    CHECK_FALSE(module->Init(&host, &noDir));
    CHECK(host.LineCount() == 0);

    //合法参数初始化两次：都返回成功，但只初始化一次
    CHECK(module->Init(&host, &params));
    CHECK(module->Init(&host, &params));
    CHECK(host.CountLinesContaining(kInitLogMarker) == 1);

    module->Shutdown();
    loader.Unload();
}

TEST_CASE("FSD3 未初始化就关闭、重复关闭都安全，关闭后不再回调宿主", "[filesharedll]")
{
    CPluginModuleLoader<IFileShareModule> loader;
    IFileShareModule* module = LoadModule(loader);

    //未初始化就关闭
    module->Shutdown();

    StubHost host;
    std::wstring appDir = ExeDir();
    FileShareInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));
    module->Shutdown();
    CHECK(host.CountLinesContaining(kShutdownLogMarker) == 1);

    //关闭之后：任何调用都不再写日志、不再请求发信令
    const size_t linesAfterShutdown = host.LineCount();
    module->Shutdown();
    module->OnLoggedIn();
    module->OnLoggingOut();
    CHECK_FALSE(module->OnServerFrame(kShareCreateCmd, 1, "{}", 2));
    CHECK_FALSE(module->OpenShareCenter(NULL));
    PumpPendingMessages();
    CHECK(host.LineCount() == linesAfterShutdown);
    CHECK(host.SentCount() == 0);

    loader.Unload();
}

TEST_CASE("FSD4 服务端帧的接收与拒收", "[filesharedll]")
{
    CPluginModuleLoader<IFileShareModule> loader;
    IFileShareModule* module = LoadModule(loader);

    //初始化之前：一律拒收
    CHECK_FALSE(module->OnServerFrame(kShareCreateCmd, 1, "{}", 2));

    StubHost host;
    std::wstring appDir = ExeDir();
    FileShareInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));

    //号段外的命令：拒收
    CHECK_FALSE(module->OnServerFrame(kMessageNotify, 1, "{}", 2));
    CHECK_FALSE(module->OnServerFrame(kBelowShareCmd, 1, "{}", 2));
    CHECK_FALSE(module->OnServerFrame(kAboveShareCmd, 1, "{}", 2));

    //包体为空而长度不为 0：拒收；包体为空且长度为 0：接收
    CHECK_FALSE(module->OnServerFrame(kShareCreateCmd, 1, NULL, kBogusLength));
    CHECK(module->OnServerFrame(kShareCreateCmd, 0, NULL, 0));

    //号段首尾与段内的帧：接收
    CHECK(module->OnServerFrame(kShareFirstCmd, 0, "{}", 2));
    CHECK(module->OnServerFrame(kShareLastCmd, 0, "{}", 2));
    CHECK(module->OnServerFrame(kShareListCmd, kListSeq, kListBody,
                                static_cast<uint32_t>(strlen(kListBody))));

    //UI 线程取出排队的帧：cmd、seq 与包体字节数都原样送到
    PumpPendingMessages();
    CHECK(host.CountLinesContaining(kFrameLogMarker) == 1);

    module->Shutdown();
    loader.Unload();
}

TEST_CASE("FSD5 DLL 里的第二份 balloonui 与测试程序那份的同名窗口类互不冲突", "[filesharedll]")
{
    //先用测试程序自己那份 balloonui 建一个不显示的窗口，占住同名窗口类
    balloonwjui::DuiFrameWindow exeFrame;
    RECT rc = { 0, 0, 320, 240 };
    REQUIRE(exeFrame.Create(NULL, rc, _T("FileShareModuleTests"), WS_OVERLAPPEDWINDOW) != NULL);

    CPluginModuleLoader<IFileShareModule> loader;
    IFileShareModule* module = LoadModule(loader);

    StubHost host;
    std::wstring appDir = ExeDir();
    FileShareInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));

    //DLL 打开「文件共享」窗口：能创建、能显示，窗口类归 DLL 的模块实例
    REQUIRE(module->OpenShareCenter(NULL));
    PumpPendingMessages();
    HWND center = FindCenterWindow();
    REQUIRE(center != NULL);
    CHECK(::IsWindowVisible(center));

    //两边的窗口类同名，但分属不同的模块实例
    wchar_t exeClass[MAX_PATH] = { 0 };
    wchar_t dllClass[MAX_PATH] = { 0 };
    ::GetClassNameW(exeFrame.m_hWnd, exeClass, MAX_PATH);
    ::GetClassNameW(center, dllClass, MAX_PATH);
    CHECK(std::wstring(exeClass) == std::wstring(dllClass));
    CHECK(::GetWindowLongPtrW(exeFrame.m_hWnd, GWLP_HINSTANCE)
          != ::GetWindowLongPtrW(center, GWLP_HINSTANCE));

    //再打开一次只前置，不会多出第二个窗口
    REQUIRE(module->OpenShareCenter(NULL));
    CHECK(FindCenterWindow() == center);

    //关闭模块：窗口被销毁
    module->Shutdown();
    PumpPendingMessages();
    CHECK_FALSE(::IsWindow(center));

    loader.Unload();
    exeFrame.DestroyWindow();
}

TEST_CASE("FSD6 登录后拉取「我的共享」，收到应答后记一行摘要", "[filesharedll]")
{
    CPluginModuleLoader<IFileShareModule> loader;
    IFileShareModule* module = LoadModule(loader);

    StubHost host;
    host.m_personalDir = FreshPersonalDir();
    std::wstring appDir = ExeDir();
    FileShareInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));

    //登录成功：经宿主发出一条 2205，包体只带 scope=1（我的共享）
    module->OnLoggedIn();
    REQUIRE(host.SentCount() == 1);
    CHECK(host.SentCmdAt(0) == kShareListCmd);
    CHECK(host.SentBodyAt(0) == kListMineBody);

    //服务端的应答带回同一个 seq：UI 线程取出后记一行带结果码与条数的摘要。第 12 步起应答按完整格式解析，
    //两项都给出全部字段（都绑定别的设备，因此不会触发 2210）
    const std::string listResponse = std::string(kListResponseHead) + ListItemJson(1, false) + ","
                                   + ListItemJson(2, false) + "]}";
    Deliver(module, kShareListCmd, kStubSendSeq, listResponse);
    CHECK(host.CountLinesContaining(kListSummaryMarker) == 1);
    CHECK(host.CountSent(cmd_share_serve_report) == 0);

    module->Shutdown();
    loader.Unload();
}

TEST_CASE("FSD7 上线比对：删除本机多余记录、按共享根是否存在报告 2210", "[filesharedll]")
{
    StubHost host;
    host.m_personalDir = FreshPersonalDir();
    const std::wstring rootOk = host.m_personalDir + L"shareroot";
    REQUIRE(sharetestfs::MakeDirs(rootOk));
    REQUIRE(sharetestfs::MakeDirs(host.m_personalDir + L"ChatLog"));
    {
        CShareLocalStore store;
        REQUIRE(store.Open(StorePath(host.m_personalDir).c_str()));
        SeedLocalShare(store, kShareReport, rootOk);
        SeedLocalShare(store, kShareRootGone, host.m_personalDir + L"gone");
        SeedLocalShare(store, kShareLocalOnly, rootOk);
        SeedLocalShare(store, kShareOtherDevice, rootOk);
    }

    CPluginModuleLoader<IFileShareModule> loader;
    IFileShareModule* module = LoadModule(loader);
    std::wstring appDir = ExeDir();
    FileShareInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));
    module->OnLoggedIn();
    REQUIRE(host.CountSent(kShareListCmd) == 1);

    //服务端：11、12、14 绑定本机，15 绑定别的设备；13 已不存在
    std::string resp = "{\"code\":0,\"msg\":\"ok\",\"scope\":1,\"shares\":[";
    resp += ListItemJson(kShareReport, true) + ",";
    resp += ListItemJson(kShareRootGone, true) + ",";
    resp += ListItemJson(kShareMissingLocal, true) + ",";
    resp += ListItemJson(kShareOtherDevice, false) + "]}";
    Deliver(module, kShareListCmd, kStubSendSeq, resp);

    //2210 只报 11（可提供）与 12（共享根不存在），且只发一次
    std::vector<ShareServeItem> expected;
    ShareServeItem ok;
    ok.m_shareId = kShareReport;
    ok.m_available = true;
    expected.push_back(ok);
    ShareServeItem gone;
    gone.m_shareId = kShareRootGone;
    gone.m_available = false;
    gone.m_reason = share_unavailable_root_missing;
    expected.push_back(gone);
    REQUIRE(host.CountSent(cmd_share_serve_report) == 1);
    CHECK(host.LastSentBody(cmd_share_serve_report) == ShareProtocolCodec::BuildServeReport(expected));

    //同一个 seq 的应答再来一次：不再重复比对与报告
    Deliver(module, kShareListCmd, kStubSendSeq, resp);
    CHECK(host.CountSent(cmd_share_serve_report) == 1);

    module->Shutdown();
    loader.Unload();

    //本机记录：13 被删除，其余保留（15 绑定别的设备也不删）
    CShareLocalStore store;
    REQUIRE(store.Open(StorePath(host.m_personalDir).c_str()));
    ShareLocalRecord rec;
    CHECK(store.GetByShareId(kShareReport, rec));
    CHECK(store.GetByShareId(kShareRootGone, rec));
    CHECK_FALSE(store.GetByShareId(kShareLocalOnly, rec));
    CHECK(store.GetByShareId(kShareOtherDevice, rec));
}

TEST_CASE("FSD8 分享者收到中转参数、会话结束与共享失效", "[filesharedll]")
{
    StubHost host;
    host.m_personalDir = FreshPersonalDir();
    const std::wstring rootOk = host.m_personalDir + L"shareroot";
    REQUIRE(sharetestfs::MakeDirs(rootOk));
    REQUIRE(sharetestfs::MakeDirs(host.m_personalDir + L"ChatLog"));
    {
        CShareLocalStore store;
        REQUIRE(store.Open(StorePath(host.m_personalDir).c_str()));
        SeedLocalShare(store, kShareReport, rootOk);
        SeedLocalShare(store, kShareRootGone, host.m_personalDir + L"gone");
    }

    CPluginModuleLoader<IFileShareModule> loader;
    IFileShareModule* module = LoadModule(loader);
    std::wstring appDir = ExeDir();
    FileShareInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));
    //登录后不回 2205 应答：本机记录保持原样
    module->OnLoggedIn();

    //本机没有这个共享的记录：回 2221（804）
    Deliver(module, cmd_share_relay_info, 0,
            RelayOwnerJson(kAccessNoRecord, kShareMissingLocal, "", kClosedRelayIp, kClosedRelayPort));
    REQUIRE(host.CountSent(cmd_share_close_access) == 1);
    CHECK(host.LastSentBody(cmd_share_close_access)
          == ShareProtocolCodec::BuildCloseAccessRequest(kAccessNoRecord, error_code_share_unavailable));

    //共享根已不存在：回 2221（805），并补报一次 2210
    Deliver(module, cmd_share_relay_info, 0,
            RelayOwnerJson(kAccessRootGone, kShareRootGone, "", kClosedRelayIp, kClosedRelayPort));
    REQUIRE(host.CountSent(cmd_share_close_access) == 2);
    CHECK(host.LastSentBody(cmd_share_close_access)
          == ShareProtocolCodec::BuildCloseAccessRequest(kAccessRootGone, error_code_share_root_missing));
    REQUIRE(host.CountSent(cmd_share_serve_report) == 1);
    CHECK(host.LastSentBody(cmd_share_serve_report)
          == ShareProtocolCodec::BuildServeReport(std::vector<ShareServeItem>(1, RootMissingItem(kShareRootGone))));

    //传输子会话却没有带方向（第 14 步起 2222 的传输子会话都带 direction）：回 2225（3）
    Deliver(module, cmd_share_relay_info, 0,
            RelayOwnerJson(kAccessConnect, kShareReport, kTransferId, kClosedRelayIp, kClosedRelayPort));
    REQUIRE(host.CountSent(cmd_share_transfer_close) == 1);
    CHECK(host.LastSentBody(cmd_share_transfer_close)
          == ShareProtocolCodec::BuildTransferClose(kTransferId, 1, error_code_bad_request));

    //本机有记录、共享根存在：建提供会话，后台连接中转；连接被拒后回 2221（817）
    Deliver(module, cmd_share_relay_info, 0,
            RelayOwnerJson(kAccessConnect, kShareReport, "", kClosedRelayIp, kClosedRelayPort));
    CHECK(host.CountLinesContaining("share provider session started, accessid: share-3-100") == 1);
    REQUIRE(PumpUntilSent(host, cmd_share_close_access, 3, kConnectFailWaitMs));
    CHECK(host.LastSentBody(cmd_share_close_access)
          == ShareProtocolCodec::BuildCloseAccessRequest(kAccessConnect, error_code_share_relay_broken));

    //2203 已失效：本人是分享者，删除本机记录；不认识的共享不受影响
    Deliver(module, cmd_share_state_notify, 0, "{\"reason\":1,\"shareid\":\"11\",\"state\":3}");
    Deliver(module, cmd_share_state_notify, 0, "{\"reason\":1,\"shareid\":\"999\",\"state\":3}");
    CHECK(host.CountLinesContaining("share invalidated, local record removed, shareid: 11") == 1);

    module->Shutdown();
    loader.Unload();

    CShareLocalStore store;
    REQUIRE(store.Open(StorePath(host.m_personalDir).c_str()));
    ShareLocalRecord rec;
    CHECK_FALSE(store.GetByShareId(kShareReport, rec));
    CHECK(store.GetByShareId(kShareRootGone, rec));
}

TEST_CASE("FSD9 宿主取不到会话信息时不拉取共享列表", "[filesharedll]")
{
    CPluginModuleLoader<IFileShareModule> loader;
    IFileShareModule* module = LoadModule(loader);

    StubHost host;
    host.m_personalDir = FreshPersonalDir();
    host.m_sessionOk = false;
    std::wstring appDir = ExeDir();
    FileShareInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));

    module->OnLoggedIn();
    CHECK(host.CountSent(kShareListCmd) == 0);
    CHECK(host.CountLinesContaining("host did not provide session info") == 1);

    module->Shutdown();
    loader.Unload();
}

TEST_CASE("FSD10 访问者打开共享：打开失败与中转连接失败", "[filesharedll]")
{
    CPluginModuleLoader<IFileShareModule> loader;
    IFileShareModule* module = LoadModule(loader);

    StubHost host;
    host.m_personalDir = FreshPersonalDir();
    std::wstring appDir = ExeDir();
    FileShareInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));
    module->OnLoggedIn();

    //打开共享 9001：浏览窗口出现并发出 2220；服务端回 804，浏览窗口显示失败
    REQUIRE(module->OpenShareFromCard(NULL, kVisitShareA));
    REQUIRE(host.CountSent(cmd_share_open) == 1);
    CHECK(host.LastSentBody(cmd_share_open) == ShareProtocolCodec::BuildShareIdRequest(kVisitShareA));
    Deliver(module, cmd_share_open, kStubSendSeq, "{\"code\":804,\"msg\":\"owner offline\"}");
    CHECK(host.CountLinesContaining("share browser list failed, shareid: 9001, code: 804") == 1);

    //再打开同一个共享：窗口已在，只前置，不重复发 2220
    REQUIRE(module->OpenShareFromCard(NULL, kVisitShareA));
    CHECK(host.CountSent(cmd_share_open) == 1);

    //打开共享 9002：2220 成功、收到推给访问者的 2222，连接中转被拒后回 2221（817），浏览窗口显示失败
    REQUIRE(module->OpenShareFromCard(NULL, kVisitShareB));
    REQUIRE(host.CountSent(cmd_share_open) == 2);
    Deliver(module, cmd_share_open, kStubSendSeq, "{\"accessid\":\"share-9-100\",\"code\":0,\"msg\":\"ok\"}");
    Deliver(module, cmd_share_relay_info, 0,
            "{\"accessid\":\"share-9-100\",\"idletimeoutms\":600000,\"lanes\":0,\"opentimeoutms\":15000,"
            "\"relayip\":\"127.0.0.1\",\"relayport\":1,\"role\":10,\"token\":\"tok\",\"transferid\":\"\"}");
    //BUG-82 诊断：先记下此刻数到的行数；没数到时，等连接失败处理完、后台线程安静下来之后再数一次并打印全部日志，
    //以区分「这一行没写出来」（2222 没被处理）与「写出来了但此刻没读到」
    const int connectingAtCheck = host.CountLinesContaining(kFsd10ConnectingMarker);
    REQUIRE(PumpUntilSent(host, cmd_share_close_access, 1, kConnectFailWaitMs));
    if (connectingAtCheck != 1)
    {
        UNSCOPED_INFO("connecting lines at check: " << connectingAtCheck << ", later: "
                      << host.CountLinesContaining(kFsd10ConnectingMarker));
        UNSCOPED_INFO("host log:\n" << host.AllLines());
    }
    CHECK(connectingAtCheck == 1);
    CHECK(host.LastSentBody(cmd_share_close_access)
          == ShareProtocolCodec::BuildCloseAccessRequest("share-9-100", error_code_share_relay_broken));
    CHECK(host.CountLinesContaining("share browser list failed, shareid: 9002, code: 817") == 1);

    //模块关闭：浏览窗口一并关闭
    module->Shutdown();
    loader.Unload();
}

TEST_CASE("FSD11 共享根可提供与否变化时补报 2210", "[filesharedll]")
{
    StubHost host;
    host.m_personalDir = FreshPersonalDir();
    const std::wstring root = host.m_personalDir + L"pollroot";
    REQUIRE(sharetestfs::MakeDirs(root));
    REQUIRE(sharetestfs::MakeDirs(host.m_personalDir + L"ChatLog"));
    {
        CShareLocalStore store;
        REQUIRE(store.Open(StorePath(host.m_personalDir).c_str()));
        SeedLocalShare(store, kPollShareId, root);
    }

    CPluginModuleLoader<IFileShareModule> loader;
    IFileShareModule* module = LoadModule(loader);
    std::wstring appDir = ExeDir();
    FileShareInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));
    module->OnLoggedIn();
    Deliver(module, kShareListCmd, kStubSendSeq, std::string(kListResponseHead) + ListItemJson(kPollShareId, true) + "]}");
    REQUIRE(host.CountSent(cmd_share_serve_report) == 1);

    //共享根被删：几秒内补报「共享根不存在」
    sharetestfs::DeleteTree(root);
    REQUIRE(PumpUntilSent(host, cmd_share_serve_report, 2, kRootPollWaitMs));
    CHECK(host.LastSentBody(cmd_share_serve_report)
          == ShareProtocolCodec::BuildServeReport(std::vector<ShareServeItem>(1, RootMissingItem(kPollShareId))));

    //共享根恢复：再补报「可提供」
    REQUIRE(sharetestfs::MakeDirs(root));
    REQUIRE(PumpUntilSent(host, cmd_share_serve_report, 3, kRootPollWaitMs));
    ShareServeItem back;
    back.m_shareId = kPollShareId;
    back.m_available = true;
    CHECK(host.LastSentBody(cmd_share_serve_report) == ShareProtocolCodec::BuildServeReport(std::vector<ShareServeItem>(1, back)));

    module->Shutdown();
    loader.Unload();
}

TEST_CASE("FSD12 发起共享对话框：打开、要成员、退出登录时关闭", "[filesharedll]")
{
    CPluginModuleLoader<IFileShareModule> loader;
    IFileShareModule* module = LoadModule(loader);

    StubHost host;
    host.m_personalDir = FreshPersonalDir();
    std::wstring appDir = ExeDir();
    FileShareInitParams params = MakeParams(appDir);
    REQUIRE(module->Init(&host, &params));
    module->OnLoggedIn();

    //群聊 900200：对话框出现并向宿主要成员
    const int64_t groupId = 900200;
    REQUIRE(module->OpenCreateShare(NULL, conversation_group, groupId));
    REQUIRE(host.m_memberRequests.size() == 1);
    CHECK(host.m_memberRequests[0].first == conversation_group);
    CHECK(host.m_memberRequests[0].second == groupId);
    CHECK(::FindWindowW(NULL, L"发起共享") != NULL);

    //同一会话再次打开：只前置，不重复要成员；会话 id 非法时失败
    REQUIRE(module->OpenCreateShare(NULL, conversation_group, groupId));
    CHECK(host.m_memberRequests.size() == 1);
    CHECK_FALSE(module->OpenCreateShare(NULL, conversation_group, 0));

    //宿主回送成员：本人之外还有两人
    fileshare::FileShareMemberInfo members[3];
    memset(members, 0, sizeof(members));
    members[0].m_userId = kStubUserId;
    members[1].m_userId = kStubUserId + 1;
    members[2].m_userId = kStubUserId + 2;
    strcpy_s(members[1].m_name, fileshare::kFileShareTextBytes, "Alice");
    strcpy_s(members[2].m_name, fileshare::kFileShareTextBytes, "Bob");
    module->OnConversationMembers(conversation_group, groupId, 0, members, 3);
    CHECK(host.CountLinesContaining("share create dialog: members arrived, ctype: 2, cid: 900200, candidates: 2") == 1);

    //退出登录：对话框关闭
    module->OnLoggingOut();
    CHECK(::FindWindowW(NULL, L"发起共享") == NULL);

    module->Shutdown();
    loader.Unload();
}

namespace {

//假的 P2P 服务：记录模块登记与注销的用途（DLL 经宿主取到它的指针，跨模块调用它的虚函数）
class FakeP2PService : public p2p::IP2PService
{
public:
    virtual bool RegisterHandler(const char* purpose, p2p::IP2PLinkHandler* handler) override
    {
        (void)handler;
        m_registered.push_back(purpose);
        return true;
    }

    virtual bool UnregisterHandler(const char* purpose) override
    {
        m_unregistered.push_back(purpose);
        return true;
    }

    std::vector<std::string> m_registered;     //登记过的用途，按调用顺序
    std::vector<std::string> m_unregistered;   //注销过的用途，按调用顺序
};

} // namespace

TEST_CASE("FSD13 宿主提供 P2P 服务时以 fileshare 登记业务方、关闭时注销，不提供时不登记", "[filesharedll]")
{
    //宿主不提供 P2P 服务：不登记，记一行走中转的日志
    {
        CPluginModuleLoader<IFileShareModule> loader;
        IFileShareModule* module = LoadModule(loader);
        StubHost host;
        std::wstring appDir = ExeDir();
        FileShareInitParams params = MakeParams(appDir);
        REQUIRE(module->Init(&host, &params));
        CHECK(host.CountLinesContaining("host has no p2p service") == 1);
        module->Shutdown();
        loader.Unload();
    }

    //宿主提供 P2P 服务：Init 登记 fileshare，Shutdown 注销同一个用途
    FakeP2PService p2pService;
    {
        CPluginModuleLoader<IFileShareModule> loader;
        IFileShareModule* module = LoadModule(loader);
        StubHost host;
        host.m_p2pService = &p2pService;
        std::wstring appDir = ExeDir();
        FileShareInitParams params = MakeParams(appDir);
        REQUIRE(module->Init(&host, &params));
        REQUIRE(p2pService.m_registered.size() == 1);
        CHECK(p2pService.m_registered[0] == "fileshare");
        CHECK(p2pService.m_unregistered.empty());
        CHECK(host.CountLinesContaining("host has no p2p service") == 0);
        module->Shutdown();
        loader.Unload();
    }
    REQUIRE(p2pService.m_unregistered.size() == 1);
    CHECK(p2pService.m_unregistered[0] == "fileshare");
}
