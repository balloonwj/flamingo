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
#include "../Feature/RemoteDesktop/RdRelaySelfTest.h"            // --rdrelayselftest
#include "../Feature/RemoteDesktop/DxgiScreenCapturer.h"         // --dxgicap
#include "../Feature/RemoteDesktop/RdScreenCapturer.h"           // --dxgipipe / --dxgipipegdi
#include "../UI/BalloonRemoteDesktop/RdViewerToolbar.h"          // --rdtoolbar
#include "../Feature/Call/CallRelaySelfTest.h"                   // --callrelayselftest
#include "../Feature/Call/AudioLoopback.h"                       // --callaudioloop
#include "../Feature/Call/CameraPreviewDebug.h"                  // --camerapreview
#include "../Feature/Call/CameraDisplayTest.h"                   // --cameratest
#include "../Feature/Call/H264SelfTest.h"                        // --h264selftest
#include "../Feature/Call/VideoLoopbackDebug.h"                  // --videoloop
#include "../Feature/Call/CallVideoUiDebug.h"                    // --callvideoui
#include "../Common/Log/IULog.h"
#include "../Session/UserSessionData.h"
#include "../UI/BalloonMain/MainPanelBench.h"                  // --mainpanel --stress / --bench
#include "../UI/BalloonMain/MainPanelStressData.h"             // --forwardpicker 的预览数据
#include "../UI/BalloonMain/ForwardRequest.h"                  // --forwardpicker：模拟的转发请求
#include "../Common/Util/EncodeUtil.h"                         // --forwardpicker：UTF-8 名字转宽字符串
#include "../third-party/jsoncpp-1.9.0/json.h"                // --mergedviewer：构造合并记录的 content
#include <time.h>                                              // --mergedviewer：子消息时间取当前时间往前推
#include "../net/Protocol.h"                                   // conversation_single / conversation_group

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
    // （m_rdSelfTestWindow 不在此列：它不是 ATL 窗口类，自己用原生 Win32 API 管句柄。）
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
    if (m_rdDlg.IsWindow())
        m_rdDlg.CloseDlg();
    if (m_rdControlledBar.IsWindow())
        m_rdControlledBar.Hide();
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
    n += m_rdDlg.IsWindow() ? 1 : 0;
    n += m_rdControlledBar.IsWindow() ? 1 : 0;
    return n;
}

DebugEntryResult CDebugEntryRunner::TryRun(DebugEntryContext& ctx)
{
#ifdef _DEBUG
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
        m_chatWindow.CreateAndShow(ctx.m_hHost);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --groupchatwindow 时跳过登录，直接弹出新版气泡式
    // 群聊窗口（mock 数据），便于开发期截图核对群聊窗视觉。
    if (::_tcsstr(::GetCommandLine(), _T("--groupchatwindow")) != NULL)
    {
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
        seed.mute        = false;
        seed.pinned      = true;
        // 调试入口无宿主主面板：pHost 传 NULL，开关 / 退群退化为直接发协议、不更新缓存。
        m_groupInfoDlg.OpenForGroup(ctx.m_hHost, m_lpFMGClient, NULL, seed);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --rddlg 时跳过登录，直接弹出远程桌面会话提示对话框（假数据），
    // 便于核对它贴桌面右下角的落点、置顶效果与标题栏按钮，不必真的发起一次远程桌面。
    // 默认弹被控端那一路（"XXX 请求远程控制你的电脑"，拒绝 / 接受两个按钮）；
    // 再带 --calling 则弹发起方那一路（"正在呼叫 XXX…"，只有取消按钮）。
    // 两路的显示收尾都走 ShowAtWorkAreaCorner，位置与置顶行为一致。
    if (::_tcsstr(::GetCommandLine(), _T("--rddlg")) != NULL)
    {
        const bool bCalling = (::_tcsstr(::GetCommandLine(), _T("--calling")) != NULL);
        if (bCalling)
        {
            m_rdDlg.OpenCalling(ctx.m_hHost, _T("林知夏"), 700002);
        }
        else
        {
            m_rdDlg.OpenAuth(ctx.m_hHost, _T("林知夏"), std::string("mock-rd-session"), 700002);
        }
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
    // 调试入口：命令行带 --rdpselftest 时跳过登录，直接弹出远程桌面引擎自环
    // 测试窗口（抓本机屏→编码→解码→绘制），便于开发期目视确认引擎闭环出画面。
    if (::_tcsstr(::GetCommandLine(), _T("--rdpselftest")) != NULL)
    {
        m_rdSelfTestWindow.CreateAndShow(ctx.m_hHost);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --rdrelayselftest 时跳过登录，进程内开两条 relay 连接连
    // imgserver 中转端口(20003)，互发字节验证「连接→握手→双向透传」整条中转通路。
    // 需 imgserver 在跑、且其 relaytokenkey 为 flamingo-relay-token-secret（见 imgserver.conf）。
    if (::_tcsstr(::GetCommandLine(), _T("--rdrelayselftest")) != NULL)
    {
        bool selfTestOk = false;
        std::string report = remotedesktop::RunRdRelaySelfTest(
            "127.0.0.1", 20003, "flamingo-relay-token-secret", selfTestOk);
        CString text(CA2W(report.c_str(), CP_UTF8));
        ::MessageBox(ctx.m_hHost, text, _T("远程桌面中转自环自测"),
                     MB_OK | (selfTestOk ? MB_ICONINFORMATION : MB_ICONWARNING));
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --callrelayselftest 时跳过登录，进程内开两条 relay 连接（主叫 /
    // 被叫，同 sessionid 相反 role）连 imgserver 中转端口(20003)，双向互发测试帧验证
    // 「连接→握手→透传」整条通话中转通路。需 imgserver 在跑、且其 relaytokenkey 为
    // flamingo-relay-token-secret（见 imgserver.conf）。
    if (::_tcsstr(::GetCommandLine(), _T("--callrelayselftest")) != NULL)
    {
        bool selfTestOk = false;
        std::string report = call::RunCallRelaySelfTest(
            "127.0.0.1", 20003, "flamingo-relay-token-secret", selfTestOk);
        CString text(CA2W(report.c_str(), CP_UTF8));
        ::MessageBox(ctx.m_hHost, text, _T("通话中转自环自测"),
                     MB_OK | (selfTestOk ? MB_ICONINFORMATION : MB_ICONWARNING));
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --callaudioloop 时跳过登录，做本地音频回环自测：WASAPI 麦克风采集
    // → Opus 编码 → 解码 → WASAPI 扬声器播放（对着麦克风说话应能听到自己）。弹框运行期间
    // 回环持续运行，点确定停止。需默认麦克风/扬声器可用且采集率为 Opus 支持率（常 48kHz）。
    if (::_tcsstr(::GetCommandLine(), _T("--callaudioloop")) != NULL)
    {
        call::RunAudioLoopbackDebug();
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --camerapreview 时跳过登录，打开默认摄像头，把采集到的画面实时
    // 显示在预览窗口里（验证 MF 摄像头采集 + NV12→BGRA 显示这层）。关窗即停。需有摄像头。
    if (::_tcsstr(::GetCommandLine(), _T("--camerapreview")) != NULL)
    {
        call::RunCameraPreviewDebug();
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --cameratest 时跳过登录，运行自包含的摄像头显示测试——全程打日志到
    // E:\flamingo\cameratest.log，逐步定位「摄像头数据显示不出来」卡在哪一步（初始化/枚举/打开/
    // 协商/取帧/绘制）。弹窗显示画面，关窗弹统计框。
    if (::_tcsstr(::GetCommandLine(), _T("--cameratest")) != NULL)
    {
        call::RunCameraDisplayTest();
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --h264selftest 时跳过登录，做 H.264 编解码自测：合成 NV12 灰度
    // 渐变 → MF H.264 编码 → 解码 → 首帧落 BMP（E:\flamingo\h264_selftest_decoded.bmp）。
    // 不需要摄像头，验证 MF H.264 编解码 round-trip。
    if (::_tcsstr(::GetCommandLine(), _T("--h264selftest")) != NULL)
    {
        call::RunH264SelfTest();
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --videoloop 时跳过登录，做本地视频回环：摄像头采集 → H.264 编码 →
    // 回灌解码 → 首个解码帧落 BMP（E:\flamingo\videoloop_decoded.bmp），验证整条视频管线。
    if (::_tcsstr(::GetCommandLine(), _T("--videoloop")) != NULL)
    {
        call::RunVideoLoopbackDebug();
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --callvideoui 时跳过登录，以"视频通话中"形态打开通话窗，并用本机
    // 摄像头回环填充画面（本地预览=原始采集，远端区=自采→编码→解码后的画面），便于单机目检
    // 通话窗的视频布局与画面渲染（无需服务器 / 第二台机器）。关窗即停。
    if (::_tcsstr(::GetCommandLine(), _T("--callvideoui")) != NULL)
    {
        call::RunCallVideoUiDebug();
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --rdbar 时跳过登录，直接在屏幕顶部弹出被控端受控提示条，
    // 便于开发期目视核对其外观 / 布局 / 结束按钮（点"结束控制"会关掉提示条）。
    if (::_tcsstr(::GetCommandLine(), _T("--rdbar")) != NULL)
    {
        m_rdControlledBar.Show(L"张三", ctx.m_hHost);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --rdtoolbar 时跳过登录，直接在主显示器顶部弹出控制端全屏
    // 工具条，便于目视核对其外观 / 布局 / 两个按钮（实际运行时它只在观看窗全屏态出现）。
    if (::_tcsstr(::GetCommandLine(), _T("--rdtoolbar")) != NULL)
    {
        //调试期单弹一个工具条实例看外观；用静态对象使其在函数返回后仍存活。命令消息传
        //WM_NULL（点了按钮也只是给本窗口投递一条无副作用的空消息）。
        static CRdViewerToolbar s_dbgToolbar;
        HMONITOR mon = ::MonitorFromWindow(ctx.m_hHost, MONITOR_DEFAULTTOPRIMARY);
        s_dbgToolbar.ShowOnMonitor(mon, ctx.m_hHost, WM_NULL);
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --dxgicap 时跳过登录，用 DXGI Desktop Duplication 抓一帧屏幕
    // 存成 exe 同目录的 dxgicap.bmp，弹框报结果。用于在本机显卡上验证 DuplicateOutput
    // 能否跑通（不同显卡 / 虚拟机 / RDP 下行为差异大，先探路）。
    if (::_tcsstr(::GetCommandLine(), _T("--dxgicap")) != NULL)
    {
        wchar_t path[MAX_PATH] = { 0 };
        ::GetModuleFileNameW(NULL, path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash != NULL)
            wcscpy(slash + 1, L"dxgicap.bmp");
        else
            wcscpy(path, L"dxgicap.bmp");

        std::wstring msg;
        bool ok = remotedesktop::CDxgiScreenCapturer::DebugCaptureToBmp(path, msg);
        ::MessageBoxW(ctx.m_hHost, msg.c_str(), L"DXGI 抓屏自测",
                      MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --dxgipipegdi 时强制走 GDI 降级后端跑同一闭环，存 dxgipipegdi.bmp，
    // 用于本地验证 GDI 抓取（增量3 写的降级后端，此前只跑过 DXGI 主路径）出图正确。
    // 须在 --dxgipipe 之前判断（--dxgipipegdi 含 "--dxgipipe" 子串）。
    if (::_tcsstr(::GetCommandLine(), _T("--dxgipipegdi")) != NULL)
    {
        wchar_t path[MAX_PATH] = { 0 };
        ::GetModuleFileNameW(NULL, path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash != NULL)
            wcscpy(slash + 1, L"dxgipipegdi.bmp");
        else
            wcscpy(path, L"dxgipipegdi.bmp");

        std::wstring msg;
        bool ok = remotedesktop::RdScreenCapturer::DebugPipelineToBmp(path, msg, true);   // 强制 GDI 后端
        ::MessageBoxW(ctx.m_hHost, msg.c_str(), L"GDI 降级抓屏链路闭环自测",
                      MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
        return kDebugEntryHandled;
    }
    // 调试入口：命令行带 --dxgipipe 时跳过登录，本地走通整条抓屏链路（DXGI/GDI 抓 →
    // 16 位编码 → DIFF → 解码 → 绘制）并把还原画面存成 exe 同目录的 dxgipipe.bmp，
    // 不连服务器即可肉眼确认编码链路出图正确（方向 / 颜色 / 光标）。后端走 DXGI 还是
    // 降级 GDI 看客户端日志。
    if (::_tcsstr(::GetCommandLine(), _T("--dxgipipe")) != NULL)
    {
        wchar_t path[MAX_PATH] = { 0 };
        ::GetModuleFileNameW(NULL, path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash != NULL)
            wcscpy(slash + 1, L"dxgipipe.bmp");
        else
            wcscpy(path, L"dxgipipe.bmp");

        std::wstring msg;
        bool ok = remotedesktop::RdScreenCapturer::DebugPipelineToBmp(path, msg);
        ::MessageBoxW(ctx.m_hHost, msg.c_str(), L"DXGI 抓屏链路闭环自测",
                      MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
        return kDebugEntryHandled;
    }
#else
    // 正式发布构建不含任何调试入口。
    (void)ctx;
#endif

    return kDebugEntryNotMatched;
}

} // namespace balloonapp
