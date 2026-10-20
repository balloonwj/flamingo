/**
 *  命令行调试入口的实现。整体说明见 DebugEntryPoints.h。
 *
 *  每个分支的形态一致：匹配命令行开关 → 弹窗口或跑一段自测 → 返回对应的 DebugEntryResult。
 *  借用宿主对象一律经 DebugEntryContext，本类不直接依赖进程外壳。
 *
 *  balloonwj@qq.com   2026-08-19
 */

#include "stdafx.h"
#include "DebugEntryPoints.h"

#include "../resource.h"
#include "FlamingoClient.h"
#include "../UI/BalloonMain/BalloonMainPanel.h"
#include "../UI/BalloonMain/MainPanelTrayController.h"
#include "../UI/BalloonMain/OnlineCapability.h"                  // kConnUiConnecting / kConnUiOffline
#include "../UI/BalloonMain/BalloonKickoutDlg.h"                 // CBalloonKickoutDlg（--kickout）
#include "../UI/BalloonMain/KickoutNotice.h"                     // KickoutReasonValue（--reason=N）
#include "../UI/BalloonUpdate/BalloonUpdateWindow.h"             // --updatewindow
#include "../Feature/Update/UpdateManager.h"                     // update::UpdateTaskResult（--updatewindow）
#include "../../FlamingoMedia/Api/FlamingoMediaApi.h"            // IMediaModule::RunDebugEntry（通话与远程桌面的调试入口在 DLL 里）
#include "../Common/Log/IULog.h"
#include "../Session/UserSessionData.h"
#include "../UI/BalloonMain/MainPanelBench.h"                  // --mainpanel --stress / --bench
#include "../UI/BalloonMain/MainPanelStressData.h"             // --forwardpicker 的预览数据
#include "../UI/BalloonMain/ForwardRequest.h"                  // --forwardpicker：模拟的转发请求
#include "../Common/Util/EncodeUtil.h"                         // --forwardpicker：UTF-8 名字转宽字符串
#include "../third-party/jsoncpp-1.9.0/json.h"                // --mergedviewer：构造合并记录的 content
#include <time.h>                                              // --mergedviewer：子消息时间取当前时间往前推
#include "../net/Protocol.h"                                   // conversation_single / conversation_group
#include "../Feature/Ocr/OcrModuleHost.h"                      // --ocrtest：预加载图片文字识别模块
#include "../Feature/Ocr/OcrLauncher.h"                        // --ocrtest：经正式入口的路径发起识别
#include "../UI/BalloonOcr/OcrEntry.h"                         // --ocrtest：注册发起识别的实现
#include "../Common/I18n/I18n.h"                               // --ocrtest：i18n::GetLanguage 选默认识别语言
#include <shellapi.h>                                          // --ocrtest：CommandLineToArgvW 取图片路径
#include "../Feature/Markdown/MarkdownModuleHost.h"             // --chatwindow / --groupchatwindow：等 Markdown 模块预加载结束

//调试入口开聊天窗之前等待 Markdown 模块预加载结束的最长时间（毫秒）
static const DWORD kMarkdownPreloadWaitMs = 3000;

namespace balloonapp {

namespace {

//--mainpanel --stress 的延迟启动时长（毫秒）。CreateAndShow 返回时消息循环还没开始跑，
//窗口的首次绘制也还没发生；等窗口显示稳定之后再灌数据，计时里才不会混进首次显示的开销。
const UINT kStressStartDelayMs = 800;

//压测对象与"灌完数据后是否接着跑基准场景"。线程定时器的回调拿不到任何用户数据，
//只能经文件内的静态变量传递；对象在回调里用完即释放。仅调试入口使用。
balloonmain::CMainPanelBench* g_pStressBench    = NULL;
bool                          g_bStressRunBench = false;

//压测的延迟启动回调：灌入压测数据，带 --bench 时再跑完全部基准场景。只触发一次。
void CALLBACK StressStartTimerProc(HWND /*hwnd*/, UINT /*uMsg*/, UINT_PTR idEvent, DWORD /*dwTime*/)
{
    ::KillTimer(NULL, idEvent);
    if (g_pStressBench == NULL)
    {
        return;
    }
    balloonmain::StressDataSpec spec;
    g_pStressBench->LoadStressData(spec);
    if (g_bStressRunBench)
    {
        g_pStressBench->RunScenarios();
    }
    delete g_pStressBench;
    g_pStressBench = NULL;
}

//--forwardpicker 用的数据规模：几十个人、几个部门与群，足够核对三个页签的样子
const int kForwardPreviewContacts      = 60;
const int kForwardPreviewDepartments   = 6;
const int kForwardPreviewGroups        = 8;
const int kForwardPreviewConversations = 20;
//预览里本人的 userid：取第一个联系人，验证「不列出自己」
const int kForwardPreviewSelfIndex     = 0;
//预览里模拟转发的消息条数与跳过条数
const int kForwardPreviewMessageCount  = 3;
const size_t kForwardPreviewSkipped    = 1;

//UTF-8 转宽字符串，预览数据里的名字都是 UTF-8
std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
    {
        return std::wstring();
    }
    std::vector<wchar_t> buf(utf8.size() + 1, 0);
    EncodeUtil::Utf8ToUnicode(utf8.c_str(), buf.data(), (int)buf.size());
    return std::wstring(buf.data());
}

//--forwardpicker：用一小份压测数据打开转发目标选择窗。会话列表里的名字按通讯录与
//群列表查出，逻辑是主面板正式代码的简化版（只取昵称，不处理备注）。
//  dlg：  调试入口持有的选择窗对象。
//  merge：true 模拟合并转发（预览显示聊天记录标题），false 模拟逐条转发。
void OpenForwardPickerPreview(balloonmain::CForwardTargetDlg& dlg, bool merge)
{
    balloonmain::StressDataSpec spec;
    spec.m_contactCount      = kForwardPreviewContacts;
    spec.m_departmentCount   = kForwardPreviewDepartments;
    spec.m_groupCount        = kForwardPreviewGroups;
    spec.m_conversationCount = kForwardPreviewConversations;
    balloonmain::StressDataSet data;
    balloonmain::BuildStressData(spec, data);
    const int64_t selfId = data.ContactUserIdAt(kForwardPreviewSelfIndex);

    std::vector<balloonmain::ForwardTarget> groups;
    for (size_t i = 0; i < data.m_groups.m_groups.size(); ++i)
    {
        balloonmain::ForwardTarget t;
        t.m_ctype = conversation_group;
        t.m_cid = data.m_groups.m_groups[i].m_groupid;
        t.m_name = Utf8ToWide(data.m_groups.m_groups[i].m_name);
        groups.push_back(t);
    }

    std::vector<balloonmain::ForwardTarget> convs;
    for (size_t i = 0; i < data.m_conversations.m_items.size(); ++i)
    {
        const net::ConversationItem& item = data.m_conversations.m_items[i];
        balloonmain::ForwardTarget t;
        t.m_ctype = item.m_ctype;
        t.m_cid = item.m_cid;
        if (item.m_ctype == conversation_group)
        {
            for (size_t k = 0; k < groups.size(); ++k)
            {
                if (groups[k].m_cid == item.m_cid)
                {
                    t.m_name = groups[k].m_name;
                    break;
                }
            }
        }
        else
        {
            for (size_t k = 0; k < data.m_contacts.m_contacts.size(); ++k)
            {
                if (data.m_contacts.m_contacts[k].m_userid == item.m_cid)
                {
                    t.m_name = Utf8ToWide(data.m_contacts.m_contacts[k].m_nickname);
                    t.m_account = Utf8ToWide(data.m_contacts.m_contacts[k].m_username);
                    break;
                }
            }
        }
        convs.push_back(t);
    }

    balloonmain::ForwardRequest request;
    request.m_srcCtype = conversation_single;
    request.m_srcCid = data.ContactUserIdAt(kForwardPreviewSelfIndex + 1);
    request.m_merge = merge;
    for (int i = 1; i <= kForwardPreviewMessageCount; ++i)
    {
        request.m_msgids.push_back(i);
    }
    request.m_skipped = kForwardPreviewSkipped;
    if (merge)
    {
        request.m_title = balloonmain::BuildMergedForwardTitle(conversation_single, L"我", L"张三");
    }

    dlg.Open(NULL, selfId, request, balloonmain::BuildRecentForwardTargets(convs, selfId), groups,
             data.m_contacts.m_contacts, data.m_contacts.m_departments);
    LOG_INFO("debug entry: forward picker opened, merge=%d.", merge ? 1 : 0);
}

//--mergedviewer 构造记录用的时间：一天与一分钟的毫秒数、一秒的毫秒数
const int64_t kPreviewDayMs    = 24LL * 60 * 60 * 1000;
const int64_t kPreviewMinuteMs = 60LL * 1000;
const int64_t kPreviewSecondMs = 1000;
//--mergedviewer 里各子消息的发送者 userid。第三个人不带 fromname，验证回退名字表
const int64_t kPreviewUserA = 100001;
const int64_t kPreviewUserB = 100002;
const int64_t kPreviewUserC = 100003;
//--mergedviewer 里文件与图片子消息的大小、原图宽高
const int64_t kPreviewFileSize    = 90112;
const int     kPreviewImageWidth  = 800;
const int     kPreviewImageHeight = 600;

//造一条 --mergedviewer 用的子消息（服务端合并记录里的消息体格式）。
//  msgid：原消息 msgid；from：发送者；fromName：转发时记下的名字，空串表示不带；
//  timestampMs：消息时间；msgtype：消息类型；content：消息内容。
Json::Value MakePreviewSub(int64_t msgid, int64_t from, const char* fromName,
                           int64_t timestampMs, int msgtype, const Json::Value& content)
{
    Json::Value sub(Json::objectValue);
    sub["msgid"]     = std::to_string(msgid);
    sub["from"]      = std::to_string(from);
    sub["msgtype"]   = msgtype;
    sub["timestamp"] = (Json::Int64)timestampMs;
    sub["content"]   = content;
    if (fromName != NULL && fromName[0] != '\0')
    {
        sub["fromname"] = fromName;
    }
    return sub;
}

//造一条文本内容。text：正文（UTF-8，可含 /f["<id>"] 内联表情标记）。
Json::Value MakePreviewText(const char* text)
{
    Json::Value content(Json::objectValue);
    content["text"] = text;
    return content;
}

//--mergedviewer：构造一份覆盖各类子消息的合并记录 content —— 昨天、今天两组日期，文本（含
//表情、链接、长文）、缺名字的子消息、文件、图片与一条嵌套记录。
std::string BuildMergedViewerPreviewContent()
{
    const int64_t now       = (int64_t)::time(NULL) * kPreviewSecondMs;
    const int64_t yesterday = now - kPreviewDayMs;
    const int64_t today     = now - kPreviewMinuteMs * 30;

    Json::Value nestedMessages(Json::arrayValue);
    nestedMessages.append(MakePreviewSub(1, kPreviewUserA, "方亦初", yesterday - kPreviewDayMs,
                                         message_type_text, MakePreviewText("上周的需求先按这个范围做")));
    Json::Value nested(Json::objectValue);
    nested["title"]    = "上周的讨论";
    nested["messages"] = nestedMessages;

    Json::Value file(Json::objectValue);
    file["fileid"]   = "";
    file["filename"] = "周会纪要.docx";
    file["filesize"] = (Json::Int64)kPreviewFileSize;
    file["md5"]      = "";

    Json::Value image(Json::objectValue);
    image["fileid"] = "";
    image["md5"]    = "";
    image["width"]  = kPreviewImageWidth;
    image["height"] = kPreviewImageHeight;

    Json::Value messages(Json::arrayValue);
    int64_t msgid = 101;
    messages.append(MakePreviewSub(msgid++, kPreviewUserA, "方亦初", yesterday, message_type_text,
                                   MakePreviewText("早上好，周会的材料已经放到群文件里了/f[\"5\"]")));
    messages.append(MakePreviewSub(msgid++, kPreviewUserB, "许知远", yesterday + kPreviewMinuteMs,
                                   message_type_text, MakePreviewText("收到，我看一下")));
    messages.append(MakePreviewSub(msgid++, kPreviewUserC, "", yesterday + kPreviewMinuteMs * 2,
                                   message_type_text, MakePreviewText("这条没有带名字，按回退名字表显示")));
    messages.append(MakePreviewSub(msgid++, kPreviewUserA, "方亦初", today, message_type_text,
                                   MakePreviewText("今天的安排：上午把接口联调完，下午提测。测试环境的数据库"
                                                   "昨晚重置过，账号还是原来那几个。如果联调时发现字段对不上，"
                                                   "先在群里说一声，不要直接改表结构。提测之前请把自测记录补齐，"
                                                   "尤其是异常路径。另外周五要做一次演示，演示用的数据请提前"
                                                   "准备好，别等到当天再造。")));
    messages.append(MakePreviewSub(msgid++, kPreviewUserB, "许知远", today + kPreviewMinuteMs,
                                   message_type_file, file));
    messages.append(MakePreviewSub(msgid++, kPreviewUserB, "许知远", today + kPreviewMinuteMs * 2,
                                   message_type_image, image));
    messages.append(MakePreviewSub(msgid++, kPreviewUserA, "方亦初", today + kPreviewMinuteMs * 3,
                                   message_type_merged, nested));
    messages.append(MakePreviewSub(msgid++, kPreviewUserA, "方亦初", today + kPreviewMinuteMs * 4,
                                   message_type_text, MakePreviewText("方案见 https://example.com/doc")));

    Json::Value root(Json::objectValue);
    root["title"]    = "方亦初和许知远的聊天记录";
    root["messages"] = messages;
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    return Json::writeString(w, root);
}

//--mergedviewer：用构造的记录打开查看窗。第三个人不带 fromname，回退名字表里给出他的名字。
void OpenMergedViewerPreview(CMergedRecordWindow& wnd)
{
    std::map<int64_t, CString> fallbackNames;
    fallbackNames[kPreviewUserC] = _T("沈清");
    wnd.Open(BuildMergedViewerPreviewContent(), fallbackNames, NULL);
    LOG_INFO("debug entry: merged record viewer opened.");
}

#ifdef _DEBUG
//--ocrtest / --withocr 等待识别模块预加载完成的最长时间（毫秒）。仅调试入口在界面线程上等待，正式流程从不等待
const DWORD kOcrPreloadWaitMs = 5000;

//--ocrtest / --withocr：注册发起实现、预加载识别模块（不登录）并等待完成；返回预加载是否在时限内结束
bool PreloadOcrForDebug()
{
    balloonocr::RegisterOcrLaunchers();
    CString dllPath(g_szHomePath);
    dllPath += FLAMINGO_OCR_DLL_NAME;
    ocr::COcrModuleHost& module = ocr::COcrModuleHost::GetInstance();
    module.StartPreload(std::wstring((LPCWSTR)dllPath), i18n::GetLanguage());
    const bool finished = module.WaitForPreload(kOcrPreloadWaitMs);
    LOG_INFO("debug entry: ocr preload finished=%d, state=%d.", finished ? 1 : 0, module.GetState());
    return finished;
}

//--ocrtest：取命令行里 --ocrtest 后面的那个参数作为图片路径；没有时返回空串
std::wstring OcrTestImagePath()
{
    std::wstring path;
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv == NULL)
        return path;
    for (int i = 0; i + 1 < argc; ++i)
    {
        if (::lstrcmpiW(argv[i], L"--ocrtest") == 0)
        {
            path = argv[i + 1];
            break;
        }
    }
    ::LocalFree(argv);
    return path;
}

//--ocrtest：预加载识别模块（不登录），完成后按正式入口的路径识别命令行给出的图片；
//不带图片路径时按主面板系统菜单「图片文字识别」的路径打开还没有图片的识别窗口（docs/图片文字识别方案.md 第 14 节）
void RunOcrDebugEntry(HWND host)
{
    PreloadOcrForDebug();
    ocr::COcrModuleHost& module = ocr::COcrModuleHost::GetInstance();
    const std::wstring path = OcrTestImagePath();
    LOG_INFO("debug entry: ocr test, image path given=%d.", path.empty() ? 0 : 1);
    if (path.empty())
    {
        balloonocr::LaunchOcrWindow(host);
        return;
    }
    if (!module.CanShowEntry())
    {
        //正式流程里此时入口不显示；调试入口照样走一遍，弹出的是「组件不可用」提示
        LOG_INFO("debug entry: ocr module unavailable: %s.", module.GetSnapshot().m_failure.c_str());
    }
    ocr::LaunchOcrForFile(host, path, 0);
}
#endif // _DEBUG

} // anonymous namespace

CDebugEntryRunner::CDebugEntryRunner(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_chatWindow(pFMGClient)
    , m_groupChatWindow(pFMGClient)
    , m_registerWindow(pFMGClient)
    , m_profileWindow(pFMGClient)
    , m_contactWindow(pFMGClient)
    , m_groupManageWindow(pFMGClient)
    , m_historyWindow(pFMGClient)
    , m_systemSettingDlg(pFMGClient)
{
}

CDebugEntryRunner::~CDebugEntryRunner()
{
    DestroyPreviewWindows();
}

void CDebugEntryRunner::DestroyPreviewWindows()
{
    // 逐个销毁。这些都是 ATL 窗口类，对象析构时仍持有 HWND 会触发断言，所以宁可
    // 全部列全 —— 漏一个的代价是「用过某个调试入口再退出就崩」，而且现象离原因很远。
    if (m_chatWindow.IsWindow())
        m_chatWindow.DestroyWindow();
    if (m_groupChatWindow.IsWindow())
        m_groupChatWindow.DestroyWindow();
    if (m_registerWindow.IsWindow())
        m_registerWindow.DestroyWindow();
    if (m_profileWindow.IsWindow())
        m_profileWindow.DestroyWindow();
    if (m_contactWindow.IsWindow())
        m_contactWindow.DestroyWindow();
    if (m_groupManageWindow.IsWindow())
        m_groupManageWindow.DestroyWindow();
    if (m_historyWindow.IsWindow())
        m_historyWindow.DestroyWindow();
    if (m_systemSettingDlg.IsWindow())
        m_systemSettingDlg.DestroyWindow();
    if (m_aboutDlg.IsWindow())
        m_aboutDlg.DestroyWindow();
    if (m_groupInfoDlg.IsWindow())
        m_groupInfoDlg.DestroyWindow();
    if (m_forwardPickerDlg.IsWindow())
        m_forwardPickerDlg.DestroyWindow();
    if (m_mergedViewer.IsWindow())
        m_mergedViewer.DestroyWindow();
    if (m_imageViewerWindow.IsWindow())
        m_imageViewerWindow.DestroyWindow();
    if (m_codeSnippetDemo.IsWindow())
        m_codeSnippetDemo.DestroyWindow();
}

int CDebugEntryRunner::CountAlivePreviewWindows() const
{
    int n = 0;
    n += m_chatWindow.IsWindow() ? 1 : 0;
    n += m_groupChatWindow.IsWindow() ? 1 : 0;
    n += m_registerWindow.IsWindow() ? 1 : 0;
    n += m_profileWindow.IsWindow() ? 1 : 0;
    n += m_contactWindow.IsWindow() ? 1 : 0;
    n += m_groupManageWindow.IsWindow() ? 1 : 0;
    n += m_historyWindow.IsWindow() ? 1 : 0;
    n += m_systemSettingDlg.IsWindow() ? 1 : 0;
    n += m_aboutDlg.IsWindow() ? 1 : 0;
    n += m_groupInfoDlg.IsWindow() ? 1 : 0;
    n += m_forwardPickerDlg.IsWindow() ? 1 : 0;
    n += m_mergedViewer.IsWindow() ? 1 : 0;
    n += m_imageViewerWindow.IsWindow() ? 1 : 0;
    n += m_codeSnippetDemo.IsWindow() ? 1 : 0;
    return n;
}

DebugEntryResult CDebugEntryRunner::TryRun(DebugEntryContext& ctx)
{
#ifdef _DEBUG
    // 已迁入 FlamingoMedia.dll 的调试入口先交给媒体模块：一对一通话的 --callrelayselftest /
    // --callaudioloop / --camerapreview / --cameratest / --h264selftest / --videoloop / --callvideoui，
    // 各入口的说明见 FlamingoMedia/Call/CallDebugEntries.h；远程桌面的 --rddlg / --rdpselftest / --rdrelayselftest /
    // --rdbar / --dxgicap / --dxgipipe / --dxgipipegdi，说明见 FlamingoMedia/RemoteDesktop/RdDebugEntries.h。
    // 模块未命中再检查下面本类自己的开关。
    if (ctx.m_pMediaModule != NULL)
    {
        const int mediaResult = ctx.m_pMediaModule->RunDebugEntry(::GetCommandLine(), ctx.m_hHost);
        if (mediaResult == flamingomedia::media_debug_request_exit)
            return kDebugEntryRequestExit;
        if (mediaResult == flamingomedia::media_debug_handled)
            return kDebugEntryHandled;
    }

    // 调试入口的附加开关：命令行带 --withocr 时，先预加载图片文字识别模块再打开下面的调试窗口，
    // 看图窗工具栏、截图操作栏、聊天图片右键菜单因此带上识别入口（docs/图片文字识别方案.md 第 11 节）。
    if (::_tcsstr(::GetCommandLine(), _T("--withocr")) != NULL)
    {
        PreloadOcrForDebug();
    }

    // 调试入口：命令行带 --mainpanel 时跳过登录，直接用 mock 数据弹出
    // 新版 balloonui 主面板，便于开发期截图核对主面板视觉。正式发布
    // 构建（非 _DEBUG）不含此分支。
    if (::_tcsstr(::GetCommandLine(), _T("--mainpanel")) != NULL)
    {
        ctx.m_pPanel->CreateAndShow();

        // 本入口模拟的是"已经登录、主面板已打开"这个局面，故一并把登录标志置真：
        // 托盘右键菜单的形态就是据它选的（见 OnTrayIcon_RButtunUp），不置真的话
        // 弹出来的是"未登录"那套两行菜单，四个在线状态项根本不出现，也就无从核对
        // 断线时的置灰与"当前项"文字。仅 _DEBUG 构建有本分支。
        ctx.m_bFakeLoggedIn = true;

        // 再带 --connecting / --offline 时把对应连接态推给面板，便于截图核对
        // "掉线重连中"与"用户主动离线"两种状态下的胶囊文字与头像置灰。不带则
        // 保持默认的在线态（彩色头像 + "在线"胶囊）。
        if (::_tcsstr(::GetCommandLine(), _T("--connecting")) != NULL)
        {
            ctx.m_pPanel->ApplyConnectionState((int)balloonmain::kConnUiConnecting);
        }
        else if (::_tcsstr(::GetCommandLine(), _T("--offline")) != NULL)
        {
            ctx.m_pPanel->ApplyConnectionState((int)balloonmain::kConnUiOffline);
        }

        //再带 --stress 时灌入几千人规模的压测数据（见 MainPanelStressData.h），用于体验与测量
        //大数据量下的流畅度；同时带 --bench 则灌完后自动跑完全部基准场景，结果写日志
        //（见 MainPanelBench.h）。
        if (::_tcsstr(::GetCommandLine(), _T("--stress")) != NULL && g_pStressBench == NULL)
        {
            g_pStressBench    = new balloonmain::CMainPanelBench(ctx.m_pPanel, m_lpFMGClient);
            g_bStressRunBench = (::_tcsstr(::GetCommandLine(), _T("--bench")) != NULL);
            ::SetTimer(NULL, 0, kStressStartDelayMs, StressStartTimerProc);
            LOG_INFO("debug entry: main panel stress mode scheduled, bench=%d.", g_bStressRunBench ? 1 : 0);
        }

        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --chatwindow 时跳过登录，直接弹出新版气泡式
    // 单聊窗口（mock 数据），便于开发期截图核对单聊窗视觉。
    if (::_tcsstr(::GetCommandLine(), _T("--chatwindow")) != NULL)
    {
        //正式流程里 Markdown 模块在登录完成前早已加载完；调试入口不登录、启动后立即开窗，
        //先等后台预加载结束，Markdown 开关才会出现（只在调试入口等待，最多 kMarkdownPreloadWaitMs）
        markdown::CMarkdownModuleHost::GetInstance().WaitForPreload(kMarkdownPreloadWaitMs);
        m_chatWindow.CreateAndShow(ctx.m_hHost);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --groupchatwindow 时跳过登录，直接弹出新版气泡式
    // 群聊窗口（mock 数据），便于开发期截图核对群聊窗视觉。
    if (::_tcsstr(::GetCommandLine(), _T("--groupchatwindow")) != NULL)
    {
        //同上：等 Markdown 模块预加载结束再开窗
        markdown::CMarkdownModuleHost::GetInstance().WaitForPreload(kMarkdownPreloadWaitMs);
        m_groupChatWindow.CreateAndShow(ctx.m_hHost);
        return kDebugEntryHandled;
    }

    // 调试入口：命令行带 --forwardpicker 时跳过登录，用一小份压测数据打开转发目标选择窗，
    // 便于核对三个页签、已选区与预览的界面。不接结果接收方，点「发送」只记日志；
    // 关闭窗口后进程不会自动退出，截图脚本负责结束进程。
    if (::_tcsstr(::GetCommandLine(), _T("--forwardpicker")) != NULL)
    {
        OpenForwardPickerPreview(m_forwardPickerDlg,
                                 ::_tcsstr(::GetCommandLine(), _T("--merge")) != NULL);
        return kDebugEntryHandled;
    }

    // 调试入口：命令行带 --mergedviewer 时跳过登录，用构造的合并记录打开聊天记录查看窗，
    // 便于核对各类子消息的行、日期分组头与嵌套记录。关闭窗口后进程不会自动退出，截图
    // 脚本负责结束进程。
    if (::_tcsstr(::GetCommandLine(), _T("--mergedviewer")) != NULL)
    {
        OpenMergedViewerPreview(m_mergedViewer);
        return kDebugEntryHandled;
    }

    // 调试入口：命令行带 --history 时跳过登录，直接弹出新版历史消息记录窗口
    //（mock 数据），便于开发期核对消息记录窗视觉。
    if (::_tcsstr(::GetCommandLine(), _T("--history")) != NULL)
    {
        m_historyWindow.CreateAndShow(ctx.m_hHost);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --netsettings 时跳过登录，直接弹出新版网络设置
    // 窗口（读写真实 flamingo.ini），便于开发期核对网络设置窗视觉与
    // 三组服务器切换。
    if (::_tcsstr(::GetCommandLine(), _T("--netsettings")) != NULL)
    {
        // 传宿主窗口句柄：这条路径下网络设置窗是唯一可见窗口，关掉它就该结束进程。
        ctx.m_pPanel->OpenNetSettings(ctx.m_hHost);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --register 时跳过登录，直接弹出新版注册对话框，
    // 便于开发期核对注册窗（三步流程）视觉。
    if (::_tcsstr(::GetCommandLine(), _T("--register")) != NULL)
    {
        m_registerWindow.CreateAndShow(ctx.m_hHost);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --profile 时跳过登录，直接弹出新版个人信息
    // 对话框（mock 数据），便于开发期核对个人信息窗视觉与查看/编辑切换。
    if (::_tcsstr(::GetCommandLine(), _T("--profile")) != NULL)
    {
        m_profileWindow.CreateAndShow(ctx.m_hHost);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --contact 时跳过登录，直接弹出新版联系人资料
    // 对话框（mock 数据），便于开发期核对联系人窗视觉。
    if (::_tcsstr(::GetCommandLine(), _T("--contact")) != NULL)
    {
        m_contactWindow.CreateAndShow(ctx.m_hHost);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --groupmanage 时跳过登录，直接弹出新版群管理
    // 窗口（mock 数据），便于开发期核对群管理窗视觉与各页切换。
    if (::_tcsstr(::GetCommandLine(), _T("--groupmanage")) != NULL)
    {
        //群管理窗的模型默认为空、各页显示"正在加载"，调试入口没有服务端应答，
        //须显式填入示例数据
        m_groupManageWindow.LoadMockData();
        m_groupManageWindow.CreateAndShow(ctx.m_hHost);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --ocrtest <图片路径> 时跳过登录，预加载图片文字识别模块，完成后经正式入口
    // 同一条路径（ocr::LaunchOcrForFile）打开识别结果窗口识别这张图，便于开发期核对结果窗口与识别效果
    //（docs/图片文字识别方案.md 第 11 节）。模块不可用时弹出与正式入口相同的提示。
    // 只写 --ocrtest、不带图片路径时，按系统菜单「图片文字识别」的路径打开还没有图片的识别窗口（第 14 节）。
    if (::_tcsstr(::GetCommandLine(), _T("--ocrtest")) != NULL)
    {
        RunOcrDebugEntry(ctx.m_hHost);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --syssettings 时跳过登录，直接弹出新版系统设置
    // 对话框（配置取默认值），便于开发期核对系统设置窗视觉与三页切换。
    // 再带 --chatclean 时接着弹出「清理聊天记录」对话框，按已连接显示；同时带 --offline
    // 则按未连接显示（「清理」置灰并说明原因）。调试入口没有服务端，点「清理」会在 15 秒后
    // 按超时处理。
    if (::_tcsstr(::GetCommandLine(), _T("--syssettings")) != NULL)
    {
        m_systemSettingDlg.OpenSettings(ctx.m_hHost);
        if (::_tcsstr(::GetCommandLine(), _T("--chatclean")) != NULL)
        {
            m_systemSettingDlg.SetServerConnected(::_tcsstr(::GetCommandLine(), _T("--offline")) == NULL);
            m_systemSettingDlg.OpenChatCleanDialog();
        }
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --about 时跳过登录，直接弹出新版关于对话框，
    // 便于开发期核对关于窗视觉（品牌区 app 图标 + 标题栏进程 id + 五个功能行）。
    if (::_tcsstr(::GetCommandLine(), _T("--about")) != NULL)
    {
        m_aboutDlg.OpenAbout(ctx.m_hHost);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --updatewindow 时跳过登录，直接弹出升级窗，便于开发期核对
    // 它的视觉与四种形态。默认呈现"发现新版本"；再带 --forced 看强制升级形态（隐藏
    // 「稍后」、去掉关闭按钮），带 --latest 看"已是最新"，带 --failed 看检查失败。
    // 这里喂的是构造出来的结果，不连服务器，故不受联调环境影响。
    if (::_tcsstr(::GetCommandLine(), _T("--updatewindow")) != NULL)
    {
        update::UpdateTaskResult mock;
        mock.m_valid       = true;
        mock.m_transportOk = true;
        mock.m_code        = 0;

        if (::_tcsstr(::GetCommandLine(), _T("--failed")) != NULL)
        {
            mock.m_transportOk = false;
            mock.m_errmsg      = "check-update timeout";
        }
        else if (::_tcsstr(::GetCommandLine(), _T("--latest")) != NULL)
        {
            mock.m_hasUpdate     = false;
            mock.m_latestVersion = "3.2.1";
        }
        else
        {
            mock.m_hasUpdate     = true;
            mock.m_packageUsable = true;
            mock.m_latestVersion = "3.3.0";
            mock.m_size          = 38219776;
            //说明文字刻意写得**超过说明区的高度**：说明区高度固定，内容装不下
            //时才会出现滚动条。写短了的话滚动、滚动条淡入淡出这些都核对不到，
            //而那恰恰是这块最容易出问题的地方。
            mock.m_releaseNote   = "1. 新增群接龙，支持在群里发起接龙并统计参与人；\r\n"
                                   "2. 修复了聊天记录搜索在消息量较大时偶尔卡顿的问题；\r\n"
                                   "3. 优化了文件传输的断点续传逻辑，弱网下更稳定；\r\n"
                                   "4. 新增消息引用回复，可在会话里直接引用某条历史消息；\r\n"
                                   "5. 群公告支持富文本排版，可设置字号、颜色与对齐方式；\r\n"
                                   "6. 优化了大群成员列表的加载速度，千人群进入不再卡顿；\r\n"
                                   "7. 修复了极少数情况下托盘图标闪烁不停的问题；\r\n"
                                   "8. 修复了切换网络环境后需要手动重连的问题；\r\n"
                                   "9. 调整了截图工具的快捷键冲突检测逻辑；\r\n"
                                   "10. 修复若干已知问题。";
            mock.m_forced = (::_tcsstr(::GetCommandLine(), _T("--forced")) != NULL);

            //--emptynote：把更新说明置空，用来复现"发布时没填说明"那条路径 ——
            //说明区整块隐藏后，下面各控件的位置会变，容易暴露重排相关的问题。
            if (::_tcsstr(::GetCommandLine(), _T("--emptynote")) != NULL)
            {
                mock.m_releaseNote.clear();
            }
        }

        CBalloonUpdateWindow::GetInstance().ShowResult(mock);

        //再带 --progress 时用假数据把下载进度演一遍，便于目检进度条与进度文字。
        //走的是与真实下载相同的消息通路，观感一致。
        if (::_tcsstr(::GetCommandLine(), _T("--progress")) != NULL && mock.m_packageUsable)
        {
            CBalloonUpdateWindow::GetInstance().StartMockProgress(mock.m_size);
        }

        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --kickout 时跳过登录，直接弹出"被踢下线"提示框，便于开发期
    // 核对提示框视觉与落点（贴着托盘图标弹出）。原因默认取"异地登录"，可再带
    // --reason=N 指定别的原因（N 取值见 BalloonMain/KickoutNotice.h 的 KickoutReasonValue），
    // 用来核对各条文案的换行与窗口高度。关掉提示框即退出程序，不走回登录界面那条路。
    if (::_tcsstr(::GetCommandLine(), _T("--kickout")) != NULL)
    {
        int nReason = balloonmain::kKickoutReasonOtherDevice;
        LPCTSTR pszReason = ::_tcsstr(::GetCommandLine(), _T("--reason="));
        if (pszReason != NULL)
        {
            nReason = ::_ttoi(pszReason + _tcslen(_T("--reason=")));
        }

        // 托盘图标此刻已经挂上（本函数前面 AddIcon 过），取得到它的位置，落点与实战一致。
        RECT rcTrayIcon;
        ::SetRectEmpty(&rcTrayIcon);
        if (!ctx.m_pTray->GetTrayIconRect(&rcTrayIcon))
        {
            ::SetRectEmpty(&rcTrayIcon);
        }


        CBalloonKickoutDlg dlg;
        dlg.DoModal(ctx.m_hHost, nReason, rcTrayIcon);

        return kDebugEntryRequestExit;
    }
    // 调试入口：命令行带 --groupinfo 时跳过登录，直接弹出新版群资料对话框
    // （假数据，对照 design/群资料.png），便于开发期核对版式、开关拨动、群名片
    // 整行点击就地编辑等交互。
    if (::_tcsstr(::GetCommandLine(), _T("--groupinfo")) != NULL)
    {
        balloonmain::GroupInfoViewData seed;
        seed.groupid     = 700001;
        seed.groupName   = _T("FlamingoIM 项目组");
        seed.memberCount = 28;
        seed.ownerName   = _T("谢知行");
        seed.announcement= _T("v3.2 版本即将发布，全员准备。");
        seed.myCard      = _T("林知夏");
        seed.notifyMode  = notify_mode_allow;
        seed.pinned      = true;
        // 调试入口无宿主主面板：pHost 传 NULL，开关 / 退群退化为直接发协议、不更新缓存。
        m_groupInfoDlg.OpenForGroup(ctx.m_hHost, m_lpFMGClient, NULL, seed);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --imageviewer 时跳过登录，直接弹出新版聊天图片放大
    // 看图窗（弹文件选择框挑一张本地图片），便于开发期核对看图窗视觉与缩放 /
    // 旋转 / 平移交互。
    if (::_tcsstr(::GetCommandLine(), _T("--imageviewer")) != NULL)
    {
        m_imageViewerWindow.CreateAndShow(ctx.m_hHost);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --codesnippet 时跳过登录，直接弹出代码片段效果演示窗：左边粘贴代码，右边按聊天窗的
    // 样式对照「作为普通文字发送」与「作为代码片段发送」两种效果。关闭窗口即请求退出进程。
    if (::_tcsstr(::GetCommandLine(), _T("--codesnippet")) != NULL)
    {
        m_codeSnippetDemo.CreateAndShow(ctx.m_hHost);
        return kDebugEntryHandled;
    }
#else
    // 正式发布构建不含任何调试入口。
    (void)ctx;
#endif

    return kDebugEntryNotMatched;
}

} // namespace balloonapp
