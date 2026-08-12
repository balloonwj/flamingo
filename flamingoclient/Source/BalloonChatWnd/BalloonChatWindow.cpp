/**
 *  BalloonChatWindow.cpp
 *
 *  新版单聊窗口实现。P0：骨架 —— 以 DuiFrameWindow 形式创建 / 显示一个
 *  气泡聊天窗，客户区暂为占位文字。真正的头部 / 消息区 / 输入区由后续
 *  P1 阶段的 ChatWindowLayout 构建。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#include "stdafx.h"
#include "BalloonChatWindow.h"

#include <memory>

#include "../CustomMsgDef.h"          // WM_CLOSE_MAINDLG
#include "../UserSessionData.h"       // FMG_MSG_OPEN_CONTACT_PROFILE（头部点击路由）
#include "../IULog.h"                 // LOG_INFO
#include "ChatWindowLayout.h"         // BuildChatWindowBody / CtrlId / 分割记忆
#include "ChatImageCache.h"           // 图片后台解码缓存：注册监听 + 就绪重绘
#include "ChatHeaderInfo.h"           // ChatHeaderInfo::SetPresence/SetSignature（头部对端真实状态）
#include "../../balloonui/balloonui/Controls/Layout/DuiSplitter.h"  // 拖动完成后算回分割比例
#include "ChatMessageList.h"          // ChatMessageList::AppendMessage
#include "ChatQuoteModel.h"     // FillQuoteForRender（引用回复的统一入口）
#include "../../balloonui/balloonui/Controls/Layout/DuiLayout.h"  // DuiLayout::SetHint（引用条人名段按内容定宽）
#include "ChatMockData.h"             // ChatMessage / MakeTextMessage / MockAutoReply
#include "ChatFontUtil.h"             // 当前聊天字体的载入/保存/应用到输入框
#include "../FlamingoClient.h"        // CFlamingoClient::m_UserMgr（取对方/自己昵称）
#include "../EncodeUtil.h"            // UTF-8 ↔ Unicode 转换
#include "../BalloonMain/MainPanelAvatar.h"  // MakeMonogramAvatar（窗口图标用对方头像）
#include "../BalloonMain/AvatarSpec.h"        // DecodeAvatar：标题/任务栏图标按对端 (facetype,customface) 取字形/底色
#include "../BalloonMain/AvatarImageUtil.h"   // MakeRoundedAvatarFromFile（真图头像任务栏图标）
#include "ChatAvatarResolve.h"        // ResolveAvatarRender（气泡 / 标题真图头像呈现解析）
#include "ChatTitleIcon.h"            // MakeRoundTitleIcon / MakeRoundTitleIconFromImage / IconFromBitmap
#include "../net/IUSocket.h"          // CIUSocket::SendChatTextMessage
#include "../BalloonMain/OnlineCapability.h"   // 离线门禁：连接态、提示条文案、被拦提示
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"   // 顶部离线提示条

// 顶部提示带配色。离线用琥珀色系（与状态胶囊的"离开"同色系，是"需要你处理"的语气），
// 正在连接用中性灰（只是告知，不需要用户动作）。每种各三个色：整条底色、文字色、
// 下边缘分隔线色。底色取得很淡，压着深色文字仍然清晰；分隔线略深于底色，把提示带
// 与下方内容切开。群聊窗用同一套值（见 BalloonGroupChatWindow.cpp）。
static const COLORREF kOfflineBannerBgColor     = RGB(0xFF, 0xF4, 0xE5);
static const COLORREF kOfflineBannerColor       = RGB(0xB4, 0x53, 0x09);
static const COLORREF kOfflineBannerBorderColor = RGB(0xF3, 0xE0, 0xC0);

static const COLORREF kConnectingBannerBgColor     = RGB(0xF5, 0xF5, 0xF6);
static const COLORREF kConnectingBannerColor       = RGB(0x6B, 0x72, 0x80);
static const COLORREF kConnectingBannerBorderColor = RGB(0xE5, 0xE7, 0xEB);
#include "../net/MessageProtocolClient.h"  // net::MessageNotify
#include "../net/ImageTransfer.h"
#include "../net/CurrentUserAccount.h"   // net::CurrentUserAccount（本地缓存目录按登录账号分）     // net::ImageTransfer（发图工作线程）/ ImageSend* 结构
#include "../net/FileTransfer.h"      // net::FileTransfer（收发文件工作线程）/ FileSend* 结构
#include "ChatFileAction.h"           // ChatFileActionKind / ChatFileActionData（文件气泡按钮动作）
#include "ChatMessageMenu.h"          // ChatImageActionData / ChatMessageMenuId（图片气泡右键动作）
#include "ChatImageSaveAs.h"          // SaveChatImageAs（图片另存为，与群聊窗共用）
#include "ChatFileLocation.h"         // ResolveLocalFilePath（本地落点优先级与回退）
#include "ChatDropModel.h"            // 拖进来的路径归类成 DropPlan
#include "ChatLinkDetect.h"           // IsSafeLinkUrl（打开链接前的协议白名单）
#include <windowsx.h>                 // GET_X_LPARAM / GET_Y_LPARAM（旁听点击定焦点区域）
#include "../../balloonui/balloonui/Controls/Basic/DuiToast.h"   // 拖放跳过项的轻量提示
#include "../BalloonHistory/FileLocalStore.h"  // 文件本地落点登记库（按 md5+文件名查路径）
#include "../BalloonHistory/V2MsgQuery.h"      // HistoryTypeFilter：打开消息记录窗时预选哪个类型胶囊
#include <gdiplus.h>                  // 读图片真实宽高以算展示尺寸
#include <shellapi.h>                 // ShellExecute（文件气泡"打开文件"）
#pragma comment(lib, "gdiplus.lib")
#include "../../balloonui/balloonui/Controls/Input/DuiRichEditHost.h"  // 输入框（富文本，支持内联图片）
#include "../BalloonScreenshot/ScreenCaptureOverlay.h"  // 截图浮层（截图按钮调起）
#include "../BalloonImageViewer/BalloonImageViewerWindow.h"  // 看图窗（双击图片放大）
#include "ChatRichContentSplit.h"     // 输入框图文混排的有序拆分（发送用）
#include "ChatFaceTag.h"              // 内联表情的 dwUser 标记编码 + 显示尺寸
#include "../BalloonChat/BalloonChatMsgFormat.h"  // BodyToDigest（会话摘要里表情→[表情]）
#include "ChatInputScrollBar.h"       // 输入框与接收区一致的细悬浮滚动条
#include "ChatInputKeyFilter.h"       // 输入框 Enter 发送 / Ctrl+Enter 换行
#include "ChatSendMode.h"             // 发送键模式（Enter / Ctrl+Enter）
#include "ChatBannerLabel.h"          // 顶部离线 / 正在连接提示带（带底色的 DuiLabel 子类）
#include "ChatSendButton.h"           // ChatSendButton::DUIN_SENDBTN_DROPDOWN
#include "../../balloonui/balloonui/Controls/List/DuiMenu.h"      // 发送模式切换菜单
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"    // 提示文字 SetText
#include "../../balloonui/balloonui/Controls/Feedback/DuiToolTip.h"
#include <objbase.h>                  // CoCreateGuid（生成 clientmsgid）
#include <vector>
#include <string>

#pragma comment(lib, "ole32.lib")     // CoCreateGuid

// ---- 文本编码与 clientmsgid 辅助（文件作用域） -------------------------

// CString(UTF-16) → UTF-8 std::string。空串安全。
static std::string CStringToUtf8(const CString& text)
{
    int wlen = text.GetLength();
    if (wlen <= 0)
    {
        return std::string();
    }
    // UTF-8 单个 BMP/补充字符最多 4 字节，按 4 倍 + 1 预留缓冲。
    std::vector<char> buf((size_t)wlen * 4 + 1, 0);
    EncodeUtil::UnicodeToUtf8(text.GetString(), buf.data(), (int)buf.size());
    return std::string(buf.data());
}

// UTF-8 std::string → CString(UTF-16)。空串安全。
static CString Utf8ToCString(const std::string& utf8)
{
    if (utf8.empty())
    {
        return CString();
    }
    // wchar 个数不超过 UTF-8 字节数，按字节数 + 1 预留缓冲。
    std::vector<wchar_t> buf(utf8.size() + 1, 0);
    EncodeUtil::Utf8ToUnicode(utf8.c_str(), buf.data(), (int)buf.size());
    return CString(buf.data());
}

// 按 userid 从固定调色板挑头像底色（与联系人树/搜索结果同口径，保证同一人
// 颜色稳定）。
static COLORREF AvatarColorForUser(UINT userid)
{
    static const COLORREF kPalette[] =
    {
        RGB(0xFF, 0x7A, 0x45), RGB(0x3A, 0x86, 0xD9), RGB(0x2B, 0xB6, 0x73),
        RGB(0x8B, 0x5C, 0xF6), RGB(0xEF, 0x6C, 0x9A), RGB(0xF5, 0x9E, 0x0B),
        RGB(0x14, 0xB8, 0xA6), RGB(0xE1, 0x5B, 0x4C),
    };
    const int n = (int)(sizeof(kPalette) / sizeof(kPalette[0]));
    return kPalette[(int)(userid % (UINT)n)];
}

// 生成一个 clientmsgid（UUID 十六进制串，纯 ASCII）。用于消息去重与回执回填。
static std::string GenClientMsgId()
{
    GUID guid = { 0 };
    char sz[40] = { 0 };
    if (::CoCreateGuid(&guid) == S_OK)
    {
        sprintf_s(sz, sizeof(sz),
                  "%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",
                  guid.Data1, guid.Data2, guid.Data3,
                  guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
                  guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    }
    else
    {
        // 极少数失败时用时间戳兜底，保证非空。
        sprintf_s(sz, sizeof(sz), "CM-%lld", (long long)::GetTickCount64());
    }
    return std::string(sz);
}

// ---- 文件作用域常量 -----------------------------------------------------

// 单聊窗固定客户区尺寸（逻辑像素）—— 对照设计稿 07 的气泡聊天窗。
static const int kChatWindowWidth  = 640;
static const int kChatWindowHeight = 620;

// 单聊窗允许缩放到的最小尺寸。
static const int kChatWindowMinWidth  = 460;
static const int kChatWindowMinHeight = 420;

// 标题栏高度（逻辑像素）—— 常规标题栏，承载 min/max/close。
static const int kChatWindowTitleBarHeight = 32;

// mock 自动回复定时器 id 与延时（毫秒）—— 发出消息后过一会儿模拟对方回话。
static const UINT_PTR kReplyTimerId = 0xC001;
static const UINT     kReplyDelayMs = 1400;

// 窗口抖动动画定时器 id 与单步间隔（毫秒）。
static const UINT_PTR kShakeTimerId    = 0xC002;
static const UINT     kShakeStepMs     = 24;

// 「正在输入」定时器 id 与 tick 间隔（毫秒）。同一个定时器兼管两件事：本端停手满
// kTypingIdleMs 后发 state=0，以及对端提示满 kTypingExpireMs 后自动收起。1 秒的
// 精度对这种秒级状态足够，也不至于频繁唤醒 UI 线程。
static const UINT_PTR kTypingTimerId = 0xC003;
static const UINT     kTypingTickMs  = 1000;

// 「打开链接」延后执行的定时器 id。点链接不立刻打开，而是等一个系统双击间隔 ——
// 双击的第一下本身就是一次完整单击，若立刻打开，用户双击链接想选中整条时会平白
// 弹出浏览器。这段时间内若收到双击（WM_CHAT_CANCEL_LINK_OPEN），这次打开就作废。
// 延时取 ::GetDoubleClickTime()（跟随系统设置），故这里只定 id、不定时长。
static const UINT_PTR kLinkOpenTimerId = 0xC004;

// 抖动偏移序列（相对起点的像素位移）：先大后小来回晃几下，最后归位到 (0,0)。
static const int kShakeOffsets[][2] =
{
    {  6, -6 }, { -6, -6 }, { -6,  6 }, {  6,  6 },
    {  4, -4 }, { -4, -4 }, { -4,  4 }, {  4,  4 },
    {  2, -2 }, { -2,  2 }, {  0,  0 },
};
static const int kShakeStepCount = (int)(sizeof(kShakeOffsets) / sizeof(kShakeOffsets[0]));

// 窗口抖动对"同一目标"的最小间隔（毫秒）。发送侧：距上次发送不足此间隔的
// 点击被拦截并在本窗口提示；接收侧：此时间内重复收到的抖动静默忽略，避免被
// 刷屏。收发两侧统一用同一阈值（5 秒，与产品约定一致）。
static const ULONGLONG kShakeMinIntervalMs = 5000;

// 消息内容类型常量，与 net::message_type（net/Protocol.h）对齐 —— 本窗口只用到
// 文本与抖动两类；不直接 include Protocol.h 是为避开它与旧 net/Msg.h 的枚举同名
// 冲突（见 IUSocket.h 顶部说明）。
static const int kMsgTypeText  = 1;    // message_type_text
static const int kMsgTypeImage = 2;    // message_type_image
static const int kMsgTypeFile  = 3;    // message_type_file
static const int kMsgTypeShake = 13;   // message_type_shake

// 会话类型常量，与 net::conversation_type（net/Protocol.h）对齐。本窗口是单聊窗，
// 发撤回请求时固定取单聊；同上，不 include Protocol.h 而是就地取值。
static const int kConvTypeSingle = 1;  // conversation_single

// 文件回执状态常量，与 file_ack_state（net/Protocol.h:398）对齐。理由同上 ——
// 本文件刻意不 include Protocol.h，故就地复述一份取值。
static const int kFileAckReceiving = 1;   // file_ack_receiving：对方点了接收、正在下载
static const int kFileAckReceived  = 2;   // file_ack_received：对方已完整收到并校验通过
static const int kFileAckRejected  = 3;   // file_ack_rejected：对方点了拒绝

// 图片气泡内图片的最大展示尺寸（逻辑像素）：超出按比例缩小，小图保持原尺寸。
static const int kImageDisplayMaxW = 220;
static const int kImageDisplayMaxH = 260;

// 把图片真实宽高按比例缩到不超过 (kImageDisplayMaxW, kImageDisplayMaxH) 的展示
// 尺寸；输出至少 1×1。realW/realH 非正时回退到一个占位方块尺寸。
static void ComputeImageDisplaySize(int realW, int realH, int& dispW, int& dispH)
{
    if (realW <= 0 || realH <= 0)
    {
        dispW = 160;
        dispH = 120;
        return;
    }
    double scale = 1.0;
    if (realW > kImageDisplayMaxW)
        scale = (double)kImageDisplayMaxW / (double)realW;
    if (realH * scale > kImageDisplayMaxH)
        scale = (double)kImageDisplayMaxH / (double)realH;
    if (scale > 1.0)
        scale = 1.0;   // 小图不放大
    dispW = (int)(realW * scale + 0.5);
    dispH = (int)(realH * scale + 0.5);
    if (dispW < 1)
        dispW = 1;
    if (dispH < 1)
        dispH = 1;
}

// 给客户区里指定 id 的控件注册一段 tooltip 文字（控件不存在则静默跳过）。
static void RegisterTip(balloonwjui::DuiControl* content, UINT ctrlId,
                        LPCTSTR text)
{
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* c = content->FindCtrlById(ctrlId);
    if (c != NULL)
    {
        balloonwjui::DuiToolTipMgr::Inst().Register(c, text);
    }
}

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonChatWindow::CBalloonChatWindow(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_hMainDlg(NULL)
    , m_replyIndex(0)
    // kConnUiOnline：能开出聊天窗说明此刻本就在线，随后由主面板把真实状态推过来。
    // 初始化顺序须与声明顺序一致（本成员声明在 m_peerUserId 之前）。
    , m_connUiState(0)
    , m_peerUserId(0)
    , m_hostPanel(NULL)
    , m_pendingLocateMsgid(0)
    , m_pinnedCount(0)          // 开窗先按"一条都没钉"起步，提示带隐藏；随后由服务端全集校准
    , m_peerPresence(-1)        // -1=未注入：头部回退 mock，直到打开方注入真实 presence
    , m_peerSignatureSet(false)
    , m_peerFacetype(0)         // 0=自动：标题/任务栏头像按昵称首字 + userid 哈希底色
    , m_titleIconBmp(NULL)
    , m_hTaskIcon(NULL)
    , m_shakeStep(-1)
    , m_hInputFont(NULL)
    , m_focusArea(CHAT_FOCUS_INPUT)   // 开窗时焦点在输入框
    , m_dropToast(NULL)
{
    // m_sendShakeLimiter / m_recvShakeLimiter 由 ShakeRateLimiter 默认构造，
    // 初始为"从未放行"状态，无需在此显式初始化。
    m_shakeOrigin.x = 0;
    m_shakeOrigin.y = 0;
}

void CBalloonChatWindow::SetPeerUserId(UINT peerUserId)
{
    m_peerUserId = peerUserId;
}

void CBalloonChatWindow::SetPeerName(LPCTSTR name)
{
    m_peerName = (name != NULL) ? name : _T("");
}

void CBalloonChatWindow::SetHostPanelHwnd(HWND hostPanel)
{
    m_hostPanel = hostPanel;
}

void CBalloonChatWindow::SetPeerPresence(int presence)
{
    m_peerPresence = presence;
    ApplyPeerStatusToHeader();
}

void CBalloonChatWindow::SetPeerSignature(const CString& signature)
{
    m_peerSignature    = signature;
    m_peerSignatureSet = true;
    ApplyPeerStatusToHeader();
}

void CBalloonChatWindow::ApplyPeerStatusToHeader()
{
    //窗口尚未建好（InitUi 之前）取不到客户区 / 控件，安全返回；InitUi 末尾会再调一次兜底。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatHeaderInfo* hdr = dynamic_cast<balloonchatwnd::ChatHeaderInfo*>(
        content->FindCtrlById(balloonchatwnd::kIdHeaderInfo));
    if (hdr == NULL)
    {
        return;
    }
    if (m_peerPresence >= 0)
    {
        hdr->SetPresence(m_peerPresence);
    }
    if (m_peerSignatureSet)
    {
        hdr->SetSignature(m_peerSignature);
    }
    //头部信息块是静态宿主里的纯绘制控件，改完数据须显式重绘才生效。
    hdr->Invalidate();
}

void CBalloonChatWindow::RefreshTypingHeader()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatHeaderInfo* hdr = dynamic_cast<balloonchatwnd::ChatHeaderInfo*>(
        content->FindCtrlById(balloonchatwnd::kIdHeaderInfo));
    if (hdr == NULL)
    {
        return;
    }
    hdr->SetTyping(m_peerTyping.IsTyping());
    //头部信息块是纯绘制控件，改完数据须显式重绘才生效（同 ApplyPeerStatusToHeader）。
    hdr->Invalidate();
}

void CBalloonChatWindow::OnPeerTyping(int state)
{
    //状态没变就不重绘 —— 对方持续打字时每 4 秒来一包，没必要每包都刷一次界面。
    if (!m_peerTyping.OnNotify(::GetTickCount64(), state))
    {
        return;
    }
    LOG_INFO("单聊窗刷新对端正在输入提示: peer=%u, typing=%d.",
             m_peerUserId, m_peerTyping.IsTyping() ? 1 : 0);
    RefreshTypingHeader();
}

void CBalloonChatWindow::SendTypingAction(int action)
{
    if (action == balloonchatwnd::typing_action_none)
    {
        return;
    }

    //调试入口没有对端，无处可发。
    if (m_peerUserId == 0)
    {
        return;
    }

    //离线 / 正在连接时不发：这条提示可有可无，发不出去也不该像发消息那样弹提示打扰
    //用户，所以这里不走 CheckOnlineFor（它会弹提示框），只安静地跳过。
    if (m_connUiState != (int)balloonmain::kConnUiOnline)
    {
        return;
    }

    const int state = (action == balloonchatwnd::typing_action_send_typing)
                      ? net::typing_state_typing
                      : net::typing_state_stopped;
    CIUSocket::GetInstance().ReportTyping((int64_t)m_peerUserId, state);
}

void CBalloonChatWindow::StopTypingNotify()
{
    SendTypingAction(m_typingThrottle.OnStop());
}

LRESULT CBalloonChatWindow::OnInputChanged(UINT /*uMsg*/, WPARAM wParam,
                                           LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //失焦（wParam 非 0）：用户切走了，无论输入框里还剩什么都立即收尾。
    if (wParam != 0)
    {
        StopTypingNotify();
        return 0;
    }

    //普通内容变化：取当前文本判断空不空，交节流器决定发不发包。这里只要知道
    //"有没有内容"，不必像 DoSend 那样枚举富文本各段，取纯文本即可。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }
    balloonwjui::DuiRichEditHost* input = dynamic_cast<balloonwjui::DuiRichEditHost*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (input == NULL)
    {
        return 0;
    }

    CString text = input->GetText();
    text.Trim();
    SendTypingAction(m_typingThrottle.OnInputChanged(::GetTickCount64(), text.IsEmpty()));
    return 0;
}

void CBalloonChatWindow::SetPeerAvatar(int facetype, const std::string& customfaceUtf8)
{
    m_peerFacetype   = facetype;
    m_peerCustomface = customfaceUtf8;
    if (IsWindow())
    {
        RebuildPeerIcons();
    }
}

void CBalloonChatWindow::UpdatePeerInfo(LPCTSTR name, LPCTSTR signature,
                                        int facetype, const std::string& customfaceUtf8)
{
    // 窗口未打开就不动 UI（重新打开时主面板会按最新缓存注入）。
    if (!IsWindow())
    {
        return;
    }

    // 名字或头像变了都要重建标题栏 / 任务栏图标（头像可能变了而名字没变）。
    bool needRebuild = false;

    CString newName = (name != NULL) ? name : _T("");
    if (!newName.IsEmpty() && newName != m_peerName)
    {
        m_peerName = newName;
        SetTitle(m_peerName);
        needRebuild = true;
    }
    if (facetype != m_peerFacetype || customfaceUtf8 != m_peerCustomface)
    {
        m_peerFacetype   = facetype;
        m_peerCustomface = customfaceUtf8;
        needRebuild = true;
    }
    if (needRebuild)
    {
        RebuildPeerIcons();
    }

    // 签名:复用 SetPeerSignature(内部 ApplyPeerStatusToHeader 刷头部签名行 + 重绘)。
    SetPeerSignature((signature != NULL) ? CString(signature) : CString());
}

void CBalloonChatWindow::RebuildPeerIcons()
{
    if (m_peerUserId == 0)
    {
        return;   // 调试入口不设图标
    }
    // 按对端 (facetype, customface) 解出头像呈现：字形头像 / 自动头像给字形 + 底色；
    // 真图头像(facetype=2)缓存命中给真图路径（未命中会入队下载，完成后 OnAvatarDownloaded
    // 回刷），由本函数据 m_imagePath 是否为空决定标题栏 / 任务栏画真图还是字形。
    int64_t selfId = (m_lpFMGClient != NULL)
                     ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    balloonchatwnd::AvatarRender ar = balloonchatwnd::ResolveAvatarRender(
        (int64_t)m_peerUserId, m_peerFacetype, m_peerCustomface,
        m_peerName.IsEmpty() ? NULL : (LPCTSTR)m_peerName, selfId);
    CString  glyph    = ar.m_glyph;
    COLORREF color    = ar.m_color;
    bool     useImage = !ar.m_imagePath.IsEmpty();

    // 标题栏图标:先释放旧位图,再重建(48px 生成,标题栏 StretchBlt 到 16px)。真图命中画圆形
    // 真图、否则画字形 monogram；真图解码失败也回退字形。
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
    if (useImage)
    {
        m_titleIconBmp = balloonchatwnd::MakeRoundTitleIconFromImage(
            (LPCWSTR)ar.m_imagePath, balloonchatwnd::kChatTitleBarBg, 48);
    }
    if (m_titleIconBmp == NULL)
    {
        m_titleIconBmp = balloonchatwnd::MakeRoundTitleIcon(
            glyph, color, balloonchatwnd::kChatTitleBarBg, 48);
    }
    if (m_titleIconBmp != NULL)
    {
        SetIcon(m_titleIconBmp);
    }

    // 任务栏 / Alt-Tab 图标:先毁旧 HICON,再用 32px 位图重建（真图圆形 / 否则字形 monogram）。
    if (m_hTaskIcon != NULL)
    {
        ::DestroyIcon(m_hTaskIcon);
        m_hTaskIcon = NULL;
    }
    HBITMAP bmp32 = NULL;
    if (useImage)
    {
        // 圆形真图（圆角半径=半边长即圆，与联系人树一致）。
        bmp32 = balloonmain::MakeRoundedAvatarFromFile((LPCWSTR)ar.m_imagePath, 32, 16);
    }
    if (bmp32 == NULL)
    {
        bmp32 = balloonmain::MakeMonogramAvatar(glyph, color, 32);
    }
    if (bmp32 != NULL)
    {
        m_hTaskIcon = balloonchatwnd::IconFromBitmap(bmp32);
        ::DeleteObject(bmp32);
        if (m_hTaskIcon != NULL)
        {
            ::SendMessage(m_hWnd, WM_SETICON, ICON_BIG,   (LPARAM)m_hTaskIcon);
            ::SendMessage(m_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)m_hTaskIcon);
        }
    }
}

CBalloonChatWindow::~CBalloonChatWindow()
{
    // 客户区控件树与 HWND 由 DuiFrameWindow 基类析构兜底。
    // 标题栏图标位图与任务栏图标由本类持有，这里释放。
    if (m_titleIconBmp != NULL)
    {
        ::DeleteObject(m_titleIconBmp);
        m_titleIconBmp = NULL;
    }
    if (m_hTaskIcon != NULL)
    {
        ::DestroyIcon(m_hTaskIcon);
        m_hTaskIcon = NULL;
    }
    // 输入框自定义字体由本窗口持有（RichEdit 不接管所有权），在此释放。
    if (m_hInputFont != NULL)
    {
        ::DeleteObject(m_hInputFont);
        m_hInputFont = NULL;
    }
}

// =======================================================================
// 创建 / 显示
// =======================================================================

void CBalloonChatWindow::CreateAndShow(HWND hMainDlg)
{
    m_hMainDlg = hMainDlg;

    if (Create(NULL, CWindow::rcDefault, _T("FlamingoIM"),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("单聊窗口 Create 失败。"));
        return;
    }

    // 标题栏：常规（非透明）标题栏，承载 min/max/close 三按钮。窗口标题
    // 取对方真实昵称：优先查 CUserMgr，查不到时回退 "用户<id>"；调试入口
    // （m_peerUserId==0）下保留占位名。
    CString title = _T("单聊");
    if (m_peerUserId != 0)
    {
        if (!m_peerName.IsEmpty())
        {
            // 优先用打开方传入的真实昵称（v2 下 CUserMgr 好友列表为空）。
            title = m_peerName;
        }
        else if (m_lpFMGClient != NULL)
        {
            tstring nick = m_lpFMGClient->m_UserMgr.GetNickName(m_peerUserId);
            if (!nick.empty())
            {
                title = nick.c_str();
            }
            else
            {
                title.Format(_T("用户%u"), m_peerUserId);
            }
        }
        else
        {
            title.Format(_T("用户%u"), m_peerUserId);
        }
    }
    SetTitle(title);

    // 窗口图标 = 对端头像（字形 / 真图，按 (facetype, customface) 解析），同时设到标题栏
    // 左侧与任务栏 / Alt-Tab。统一走 RebuildPeerIcons，确保首次打开即按对端头像渲染（含真图
    // 缓存命中）；调试入口（m_peerUserId==0）下 RebuildPeerIcons 早退、保留默认图标。
    RebuildPeerIcons();
    SetTitleBarHeight(kChatWindowTitleBarHeight);
    SetButtons(true, true, true);
    SetMinSize(kChatWindowMinWidth, kChatWindowMinHeight);

    // 开启双击合成（balloonui 窗口类默认无 CS_DBLCLKS）——让气泡能收到双击，
    // 用于双击图片放大查看。
    EnableDoubleClick(true);

    InitUi();
    ResizeClient(kChatWindowWidth, kChatWindowHeight);
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    // 真实会话：拉最近 100 条历史消息渲染（beforeMsgId=0 从最新开始）。应答经
    // 主面板路由回 OnHistoryMessages。调试入口（m_peerUserId==0）不拉。
    if (m_peerUserId != 0)
    {
        CIUSocket::GetInstance().RequestSyncMessages(
            1, (int64_t)m_peerUserId, 0, balloonchatwnd::kHistoryPageSize);

        // 再拉一次本会话被 Pin 的消息。上面那次只覆盖"最近一页"，而被钉的往往恰恰是
        // 更早的老消息 —— 不单独拉一次，历史里那些气泡就不会带图钉标记。应答经主面板
        // 的 OnPinnedMessagesArrived 落库并回投到本窗口。
        CIUSocket::GetInstance().GetPinnedMessages(kConvTypeSingle, (int64_t)m_peerUserId);
    }

    LOG_INFO(_T("单聊窗口已创建并显示。"));
}

// =======================================================================
// 客户区
// =======================================================================

void CBalloonChatWindow::InitUi()
{
    // 构建单聊窗客户区控件树（来自 ChatWindowLayout 的内嵌 XML）。真实会话
    // （m_peerUserId != 0）起始为空，不预填 mock 历史；调试入口才用 mock。
    std::unique_ptr<balloonwjui::DuiControl> root =
        balloonchatwnd::BuildChatWindowBody(m_peerUserId == 0);
    if (!root)
    {
        // 内嵌 XML 为编译期常量、形态固定，正常情况下不会解析失败。
        LOG_ERROR(_T("BuildChatWindowBody 返回空 —— 单聊窗客户区未能构建。"));
        return;
    }
    SetClientContent(std::move(root));

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        LOG_ERROR(_T("SetClientContent 之后 GetClientContent 仍为空。"));
        return;
    }

    // 输入框是富文本 DuiRichEditHost（内嵌真 RICHEDIT50W，支持后续把截图作为内联
    // 图片塞进来）—— XML builder 只建控件对象、不调 EnsureCreated，须在 host HWND
    // 就绪后由调用方显式建一次。
    balloonwjui::DuiRichEditHost* input = dynamic_cast<balloonwjui::DuiRichEditHost*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (input != NULL)
    {
        input->SetShowBorder(false);   // 输入框无边框，融入输入区
        // 多行 + 自动换行：须在 EnsureCreated 之前设（多行/换行是创建期 baked-in
        // 的窗口风格位）。RichEdit 默认即多行自动换行，这里显式声明意图、与 XML 一致。
        input->SetMultiLine(true);
        input->SetWordWrap(true);
        // 关掉 RichEdit 原生竖直滚动条槽（须在 EnsureCreated 之前设——WS_VSCROLL 是
        // 创建期 baked-in 风格位）；改用下方自绘的细悬浮滚动条，与接收区风格一致。
        input->SetShowVScroll(false);
        if (!input->EnsureCreated(m_hWnd))
        {
            LOG_WARNING(_T("单聊窗输入框 EnsureCreated 失败。"));
        }
        else
        {
            // 粘贴一律按纯文本（聊天输入不要带来源的颜色/字体/链接/对象）。
            input->SetPasteAsPlainTextDefault(true);
            // 装回车过滤器：Enter 发送、Ctrl+Enter 换行。须在 EnsureCreated 之后调
            // —— 此时内部 RichEdit 子窗口才存在；按 Enter 时投递 kSendOnEnterMsg 回本窗口。
            balloonchatwnd::InstallChatInputEnterToSend(
                input->GetHostedHwnd(), m_hWnd, kSendOnEnterMsg);
            // 装「正在输入」钩子：内容一变就投 kInputChangedMsg 回本窗口，由节流器
            // 决定要不要给对端发 cmd_typing_notify。与回车过滤器各挂各的、互不影响。
            balloonchatwnd::InstallChatInputTypingHook(
                input->GetHostedHwnd(), m_hWnd, kInputChangedMsg);
            // 装与接收区一致的细悬浮滚动条（内容溢出才出现、浮在右沿、可拖动）。
            balloonchatwnd::AttachChatInputScrollBar(input);
        }
        // 载入上次的聊天字体并应用到输入框（所见即所得）。未设过则保持默认外观。
        // RichEdit 不接管 HFONT 所有权，返回的句柄存入 m_hInputFont 由本窗口持有。
        m_currentFont = balloonchatwnd::LoadChatFont();
        m_hInputFont = balloonchatwnd::ApplyChatFontToEdit(input, m_currentFont);
    }

    // 字体设置浮层：装"变化"回调（this 作上下文）。
    m_fontPopup.SetChangeCallback(&CBalloonChatWindow::OnFontChanged, this);

    // 给头部 / 输入工具栏 / 发送按钮注册 tooltip（图标按钮无文字，
    // 尤其需要提示）。
    RegisterTip(content, balloonchatwnd::kIdHdrVoice,     _T("语音通话"));
    RegisterTip(content, balloonchatwnd::kIdHdrVideo,     _T("视频通话"));
    RegisterTip(content, balloonchatwnd::kIdHdrRemoteDesktop, _T("远程桌面"));
    RegisterTip(content, balloonchatwnd::kIdHdrMore,      _T("更多"));
    RegisterTip(content, balloonchatwnd::kIdTbEmoji,      _T("表情"));
    RegisterTip(content, balloonchatwnd::kIdTbImage,      _T("图片"));
    RegisterTip(content, balloonchatwnd::kIdTbFile,       _T("文件"));
    RegisterTip(content, balloonchatwnd::kIdTbScreenshot, _T("截图"));
    RegisterTip(content, balloonchatwnd::kIdTbVoice,      _T("语音消息"));
    RegisterTip(content, balloonchatwnd::kIdTbFont,       _T("字体设置"));
    RegisterTip(content, balloonchatwnd::kIdTbShake,      _T("窗口抖动"));
    RegisterTip(content, balloonchatwnd::kIdTbPlus,       _T("更多功能"));
    RegisterTip(content, balloonchatwnd::kIdSendButton,   _T("发送"));

    // 按持久化的发送模式刷新左下角提示与输入框 placeholder（重启后保留上次选择）。
    RefreshSendModeUi();

    // 若打开方已在 CreateAndShow 之前注入过对端 presence / 签名，此处把它应用到刚建好的
    // 头部信息块（控件此刻才存在）；未注入则头部继续回退 mock。
    ApplyPeerStatusToHeader();

    // 注册到图片解码缓存：本窗口里的图片气泡发起后台解码后，缩放完成会经
    // WM_CHAT_IMAGE_READY 通知本窗口重绘。注销在 OnCloseMsg。
    balloonchatwnd::ChatImageCache::Inst().RegisterListener(m_hWnd);

    // 「正在输入」秒级定时器：管本端停手收尾与对端提示过期两件事。只有真实会话才需要
    // ——调试入口没有对端可通告，也不会收到推送。定时器随窗口销毁自动停。
    if (m_peerUserId != 0)
    {
        ::SetTimer(m_hWnd, kTypingTimerId, kTypingTickMs, NULL);
    }

    // 调试入口（--chatwindow，没有主面板也就没有服务端）下，顶部「已 Pin N 条」提示带
    // 的条数没有别处可问，就照 mock 消息里带 Pin 标记的行数凑一个。真实会话不能这么算
    // ——列表里只有已加载进当前视图的行，被钉住的老消息往往还没翻到，照这里数会少报；
    // 那条路走的是服务端 total（见 SetPinnedCount）。
    if (m_peerUserId == 0)
    {
        balloonchatwnd::ChatMessageList* mockList =
            dynamic_cast<balloonchatwnd::ChatMessageList*>(
                content->FindCtrlById(balloonchatwnd::kIdMessageArea));
        if (mockList != NULL)
        {
            SetPinnedCount(mockList->CountPinned());
        }
    }

    // 轻量提示条：挂在客户区根容器<u>末尾</u>（基类按子顺序绘制，末尾即最上层），
    // 布局 Fixed(0) 使它不占 vbox 主轴空间 —— DuiToast 自己算"顶部居中"的落点。
    // 客户区根容器是 vbox（见 BuildChatWindowBody 的返回值说明），带布局提示的
    // AddChild 在 DuiVBox 上，故这里要向下转一次。
    balloonwjui::DuiVBox* rootBox = dynamic_cast<balloonwjui::DuiVBox*>(content);
    if (rootBox != NULL)
    {
        std::unique_ptr<balloonwjui::DuiToast> toast(new balloonwjui::DuiToast());
        m_dropToast = toast.get();
        rootBox->AddChild(std::move(toast), balloonwjui::DuiLayout::Hint().Fixed(0));
    }

    // 拖文件进窗口发送：把拖放目标注册到本窗口，并摘掉输入框自带的那个
    // （否则拖到输入框上的文件会被 RichEdit 当内嵌对象吃掉，见 ChatDropController.h）。
    {
        HWND inputHwnd = NULL;
        if (input != NULL)
        {
            inputHwnd = input->GetHostedHwnd();
        }
        m_dropCtrl.Attach(m_hWnd, inputHwnd, this);
    }

    LOG_INFO(_T("单聊窗客户区 UI 初始化完成。"));
}

void CBalloonChatWindow::RefreshLinkRendering()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list != NULL)
    {
        list->RefreshAllRows();
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

balloonwjui::DuiVBox* CBalloonChatWindow::InputBlockCtrl()
{
    // 发送框 = 输入框（richedit）所在的那个 vbox：工具栏 + 输入框 + 底部提示 / 发送行
    // 都在里面（见 ChatWindowLayout.cpp 的内嵌 XML）。拿它的父控件而不是自己按坐标
    // 硬算，布局改了也不会失准。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return NULL;
    }
    balloonwjui::DuiControl* input = content->FindCtrlById(balloonchatwnd::kIdInputEdit);
    if (input == NULL)
    {
        return NULL;
    }
    return dynamic_cast<balloonwjui::DuiVBox*>(input->GetParent());
}

bool CBalloonChatWindow::IsChatDropZone(const POINT& ptClient)
{
    balloonwjui::DuiVBox* block = InputBlockCtrl();
    if (block == NULL)
    {
        return false;
    }
    RECT rc = block->GetRect();
    return ::PtInRect(&rc, ptClient) != FALSE;
}

void CBalloonChatWindow::OnChatDropHintChanged(bool visible)
{
    // 提示落在发送框上：整块铺一层浅蓝底，并把左下角那行提示文字换成"松开发送给
    // XXX"。不再在消息区盖浮层 —— 拖文件的落点就是发送框。
    //
    // 用<u>底色</u>而不是描边：发送框的矩形左右两边正好贴着客户区边缘，描边是压在
    // 矩形边线上画的，贴边那一侧只剩半条、实测左边那条根本看不见（用户报过）。
    // 填充没有这个问题，四周一致，也更显眼。
    balloonwjui::DuiVBox* block = InputBlockCtrl();
    if (block != NULL)
    {
        //拖到发送框上时的底色：浅蓝，压得住白底又不刺眼。
        const COLORREF kDropZoneBg = RGB(0xE6, 0xF1, 0xFD);
        //CLR_INVALID = 不画底，恢复成原本的样子（发送框本来没有自己的底色）。
        block->SetBgColor(visible ? kDropZoneBg : CLR_INVALID);
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        balloonwjui::DuiLabel* hint = dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kIdHintLabel));
        if (hint != NULL)
        {
            if (visible)
            {
                // 文案里带上对方是谁 —— 同时开着几个聊天窗时，这一句是"别拖错窗口"
                // 的最后一道确认。
                CString text;
                if (m_peerName.IsEmpty())
                {
                    text = _T("松开发送文件");
                }
                else
                {
                    text.Format(_T("松开发送给 %s"), (LPCTSTR)m_peerName);
                }
                hint->SetText(text);
            }
            else
            {
                // 收起时把那行字还原成随发送模式联动的原文案。
                RefreshSendModeUi();
            }
        }
    }

    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonChatWindow::OnChatFilesDropped(const std::vector<std::wstring>& paths,
                                            bool forceAsFile)
{
    // 调试入口（无对端绑定）没有服务器可发，直接给一句提示，免得用户以为发出去了。
    if (m_peerUserId == 0)
    {
        if (m_dropToast != NULL)
        {
            m_dropToast->Show(_T("调试窗口没有会话，无法发送文件"));
        }
        return;
    }

    // 归类：图片走图片消息、其余走文件消息；文件夹 / 失效路径 / 空文件进跳过项。
    const balloonchatwnd::DropPlan plan =
        balloonchatwnd::BuildDropPlanFromPaths(paths, forceAsFile);

    // 一次拖太多先问一句，避免误把整个目录拖进来把对方刷屏。
    if (plan.m_needConfirm)
    {
        CString ask;
        ask.Format(_T("确定要发送 %d 个文件吗？"), (int)plan.m_items.size());
        if (::MessageBoxW(m_hWnd, (LPCWSTR)ask, L"发送文件",
                          MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        {
            LOG_INFO("单聊窗拖放：用户取消了 %d 个文件的发送。", (int)plan.m_items.size());
            return;
        }
    }

    // 按拖入顺序逐个发出。两条链路内部都是"先乐观回显气泡、再交工作线程上传"，
    // 故这里连续调用不会卡住 UI 线程。
    for (std::vector<balloonchatwnd::DropSendItem>::size_type i = 0;
         i < plan.m_items.size(); ++i)
    {
        const balloonchatwnd::DropSendItem& item = plan.m_items[i];
        if (item.m_kind == balloonchatwnd::kDropSendImage)
        {
            SendImageFile(item.m_path);
        }
        else
        {
            SendFileData(item.m_path);
        }
    }

    // 有发不出去的，汇总成一句话提示。逐条弹窗会很烦，故只报"几个 + 主要原因"。
    if (!plan.m_skipped.empty() && m_dropToast != NULL)
    {
        const std::wstring note = balloonchatwnd::DescribeDropSkipped(plan.m_skipped);
        m_dropToast->Show(note.c_str());
    }

    LOG_INFO("单聊窗拖放处理完成: 发送 %d 个, 跳过 %d 个, shift=%d。",
             (int)plan.m_items.size(), (int)plan.m_skipped.size(), forceAsFile ? 1 : 0);
}

// =======================================================================
// 消息处理
// =======================================================================

// =======================================================================
// =======================================================================
// 【历史问题存档】最小化本窗口后主面板曾被别的程序盖住（2026-08-12 已修）
//
// 曾经的现象：在主面板前面打开本窗口，再点标题栏最小化，露出来的不是主面板，而是
// 桌面上另一个不相干的程序（复现时是资源管理器），主面板被它盖住。
//
// 原因不在本文件，而在主面板建窗时带的 WS_EX_TOOLWINDOW —— 工具窗口会被系统排除在
// "最小化某窗口后该激活谁"的候选之外，于是激活权跳过主面板，落到 Z 序里下一个普通
// 窗口，那个窗口浮上来就盖住了主面板。修法是把主面板改成普通窗口、改用
// ITaskbarList::DeleteTab 摘任务栏按钮，见 BalloonMainPanel::CreateAndShow 的注释
// 与 `docs/主面板被其它窗口盖住问题排查.md`。
//
// 留这段注释是因为：<u>现象出在本窗口的最小化动作上，症结却在别处</u>。日志实测
// 主面板当时既没有被移动、也没有被隐藏，它一条 Z 序变动消息都没收到 —— 所以将来
// 若再遇到类似现象，不要在本文件里找"哪里把主面板压下去了"，那里什么都没有，
// 该查的是相关窗口的扩展样式与激活权归属。
// =======================================================================

LRESULT CBalloonChatWindow::OnActivateMsg(UINT /*uMsg*/, WPARAM wParam,
                                          LPARAM /*lParam*/, BOOL& bHandled)
{
    // 本处只是搭个便车，不消费 WM_ACTIVATE：置 FALSE 让消息继续沿消息映射
    // 交给 DuiFrameWindow 链（基类目前没有该消息的 handler，最终走 DefWindowProc）。
    bHandled = FALSE;

    // 只关心"被激活"，失活不做任何事。
    if (LOWORD(wParam) == WA_INACTIVE)
    {
        return 0;
    }
    if (m_hostPanel == NULL || !::IsWindow(m_hostPanel))
    {
        return 0;
    }
    if (m_peerUserId == 0)
    {
        return 0;
    }

    // 用户切回这个聊天窗 = 他要看这个会话了：请主面板清未读并上报已读位点。
    // cid 用堆上的 long long 传递（Win32 下 LPARAM 装不下 64 位），主面板处理后 delete。
    long long* pCid = new long long((long long)m_peerUserId);
    LOG_INFO("单聊窗被激活，请求清除该会话未读: peer=%u.", m_peerUserId);
    ::PostMessage(m_hostPanel, FMG_MSG_CONV_MARK_READ, (WPARAM)1, (LPARAM)pCid);
    return 0;
}

LRESULT CBalloonChatWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                       LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 调试入口（--chatwindow）下，本窗口是唯一可见窗口，关闭即退出程序，
    // 交由 CMainDlg 统一收尾。后续真实集成（从主面板打开聊天窗）时关闭
    // 行为另议。
    if (m_hMainDlg != NULL && ::IsWindow(m_hMainDlg))
    {
        ::PostMessage(m_hMainDlg, WM_CLOSE_MAINDLG, 0, 0);
    }
    // 关窗前给对端补一包 state=0：人都把窗口关了，对面那句"正在输入…"不该还挂着。
    // 须在 DestroyWindow 之前发 —— 窗口一销毁就没机会了。
    StopTypingNotify();
    // 从图片解码缓存注销本窗口，避免就绪后向已销毁窗口 PostMessage。
    balloonchatwnd::ChatImageCache::Inst().UnregisterListener(m_hWnd);
    // 注销拖放目标。须在 DestroyWindow 之前 —— RevokeDragDrop 要求窗口还在。
    m_dropCtrl.Detach();
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

LRESULT CBalloonChatWindow::OnChatFocusArea(UINT /*uMsg*/, WPARAM wParam,
                                            LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (wParam == (WPARAM)CHAT_FOCUS_INPUT || wParam == (WPARAM)CHAT_FOCUS_MESSAGES)
    {
        m_focusArea = (int)wParam;
    }
    return 0;
}

LRESULT CBalloonChatWindow::OnLButtonDownMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM lParam, BOOL& bHandled)
{
    // 只旁听：DuiFrameWindow / DuiHost 还要靠这条消息派发点击。
    bHandled = FALSE;

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }
    POINT pt;
    pt.x = GET_X_LPARAM(lParam);
    pt.y = GET_Y_LPARAM(lParam);

    balloonwjui::DuiControl* msgArea = content->FindCtrlById(balloonchatwnd::kIdMessageArea);
    if (msgArea != NULL)
    {
        RECT rc = msgArea->GetRect();
        if (::PtInRect(&rc, pt))
        {
            m_focusArea = CHAT_FOCUS_MESSAGES;
            return 0;
        }
    }

    // 点发送框那一整块（工具栏、提示行、发送按钮等）同样算回到发送框。点输入框
    // 本身不会走到这里 —— 那是子窗口的事，由输入框的键盘钩子报。
    balloonwjui::DuiVBox* block = InputBlockCtrl();
    if (block != NULL)
    {
        RECT rcBlock = block->GetRect();
        if (::PtInRect(&rcBlock, pt))
        {
            m_focusArea = CHAT_FOCUS_INPUT;
        }
    }
    return 0;
}

LRESULT CBalloonChatWindow::OnChatCopySelection(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 焦点在发送框时这一下不归消息区 —— 返回 0，输入框照常复制自己的选中文字。
    // 这正是"接收框里残留一段选中文字，就再也复制不出发送框内容"那个问题的修法。
    if (m_focusArea != CHAT_FOCUS_MESSAGES)
    {
        return 0;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return 0;
    }
    // 返回值要真实反映"有没有复制"：输入框的键盘钩子据它决定 Ctrl+C 是被本窗
    // 接走了、还是该让输入框自己复制。
    return list->CopySelectedTextToClipboard() ? 1 : 0;
}

LRESULT CBalloonChatWindow::OnKeyDownMsg(UINT uMsg, WPARAM wParam,
                                         LPARAM lParam, BOOL& bHandled)
{
    // 焦点不在输入框时（点过消息区空白处等）的 Ctrl+C 走这条路。焦点在输入框里时
    // 这条消息根本到不了本窗口，那条路由输入框的键盘钩子转成 WM_CHAT_COPY_SELECTION。
    if (wParam == 'C' && (::GetKeyState(VK_CONTROL) & 0x8000) != 0)
    {
        BOOL dummy = TRUE;
        if (OnChatCopySelection(uMsg, 0, 0, dummy) != 0)
        {
            return 0;   // 已复制，消费掉
        }
    }
    bHandled = FALSE;   // 其余按键一律不拦，继续走 DuiFrameWindow 链
    (void)lParam;
    return 0;
}

LRESULT CBalloonChatWindow::OnChatOpenLink(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM lParam, BOOL& /*bHandled*/)
{
    std::wstring* pUrl = (std::wstring*)lParam;
    if (pUrl == NULL)
    {
        return 0;
    }

    // 不立刻打开：先记下来、等一个系统双击间隔（理由见 kLinkOpenTimerId 的注释）。
    // 这期间若气泡报来双击，OnChatCancelLinkOpen 会把它作废。
    m_pendingLinkUrl = *pUrl;
    delete pUrl;
    if (IsWindow())
    {
        ::SetTimer(m_hWnd, kLinkOpenTimerId, ::GetDoubleClickTime(), NULL);
    }
    return 0;
}

LRESULT CBalloonChatWindow::OnChatCancelLinkOpen(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                 LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 双击到了：刚才那一下单击排队的"打开链接"作废。
    if (IsWindow())
    {
        ::KillTimer(m_hWnd, kLinkOpenTimerId);
    }
    m_pendingLinkUrl.clear();
    return 0;
}

void CBalloonChatWindow::OpenPendingLink()
{
    if (m_pendingLinkUrl.empty())
    {
        return;
    }
    const std::wstring url = m_pendingLinkUrl;
    m_pendingLinkUrl.clear();

    // 协议白名单：只放行 http / https。file: / javascript: 之类到这一步本来就
    // 不该出现（识别阶段已拒），这里再拦一道 —— 打开外部程序这件事只有这一处入口，
    // 判断也就只留在这一处。
    if (balloonchatwnd::IsSafeLinkUrl(url))
    {
        ::ShellExecuteW(m_hWnd, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    }
    else
    {
        LOG_ERROR("单聊窗拒绝打开非 http/https 链接。");
    }
}

LRESULT CBalloonChatWindow::OnViewImageMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM lParam, BOOL& /*bHandled*/)
{
    // 气泡双击图片冒泡上来：lParam 为图片本地路径。打开进程内共享看图窗放大查看。
    LPCTSTR path = (LPCTSTR)lParam;
    if (path != NULL && path[0] != _T('\0'))
    {
        CBalloonImageViewerWindow::OpenShared(m_hWnd, path);
    }
    return 0;
}

LRESULT CBalloonChatWindow::OnImageReady(UINT /*uMsg*/, WPARAM /*wParam*/,
                                         LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 后台某张图片缩放完成：重绘消息区即可 —— 图片气泡会再画一次、这次查 ChatImageCache
    // 命中、换成真图。图片展示尺寸不变，无需重排，直接 Invalidate。
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
    return 0;
}

// 引用条的配色。人名主题橙（与气泡里引用块的人名同色）、摘要灰 —— 一眼分清
// "回复谁"与"回复的是什么"。与群聊窗取值一致。
static const COLORREF kReplyNameColor   = RGB(0x3A, 0x86, 0xD9);   // 人名：蓝，与截图一致
static const COLORREF kReplyDigestColor = RGB(0x6B, 0x6B, 0x6B);   // 被引用内容：中灰

void CBalloonChatWindow::RefreshReplyBar()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* bar = content->FindCtrlById(balloonchatwnd::kIdReplyBar);
    if (bar == NULL)
    {
        return;
    }

    const bool show = m_pendingQuote.m_valid;
    if (show)
    {
        // 两行：人名一行、被引用内容一行。拆成两个 DuiLabel 是因为一个标签只能一种
        // 颜色，而人名与内容要区分深浅；分两行之后也不必再按文字宽度实测排布了。
        balloonwjui::DuiLabel* nameLabel = dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kIdReplyName));
        balloonwjui::DuiLabel* digestLabel = dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kIdReplyDigest));

        if (nameLabel != NULL)
        {
            CString name = Utf8ToCString(m_pendingQuote.m_name);
            name += _T(":");
            nameLabel->SetText(name);
            nameLabel->SetTextColor(kReplyNameColor);
            nameLabel->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        if (digestLabel != NULL)
        {
            digestLabel->SetText(Utf8ToCString(m_pendingQuote.m_digest));
            digestLabel->SetTextColor(kReplyDigestColor);
            // 内容超长时省略号收尾，卡片高度固定，绝不把输入区撑变形。
            digestLabel->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }

    if (bar->IsVisible() != show)
    {
        bar->SetVisible(show);
    }
    // 卡片占一块固定高度，显隐会改变输入区块的内部排布，必须重排；只 Invalidate
    // 的话输入框不会让出那块位置，卡片会盖在它上面。
    //
    // 注意不能只对根 ForceLayout：根的 Layout 会对各级容器调 SetRect，而 SetRect
    // 在矩形没变时提前返回 —— 输入区块本身的矩形并没有变（变的只是它内部的分配），
    // 于是刚变可见的卡片永远拿不到矩形，表现为引用卡片完全看不见。这是 balloonui
    // 已知的布局坑（群聊窗切边栏 Tab 处也有同样的注释），解法是直接对卡片所在的
    // 那个容器 ForceLayout，绕开 SetRect 的提前返回。
    balloonwjui::DuiControl* holder = bar->GetParent();
    if (holder != NULL)
    {
        holder->ForceLayout(holder->GetRect());
    }
    else
    {
        content->ForceLayout(content->GetRect());
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonChatWindow::CancelPendingReply()
{
    m_pendingQuote = net::ReplyQuote();
    RefreshReplyBar();
}

LRESULT CBalloonChatWindow::OnChatLocateQuoted(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM lParam, BOOL& /*bHandled*/)
{
    long long* pMsgid = (long long*)lParam;
    if (pMsgid == NULL)
    {
        return 0;
    }
    const long long msgid = *pMsgid;
    delete pMsgid;

    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return 0;
    }

    // 原消息不在已加载范围内时 ScrollToMsgid 返回 false —— 本期到此为止，
    // 「自动回拉历史直到那一条出现」属于后续阶段。
    const bool located = list->ScrollToMsgid(msgid);
    LOG_INFO("单聊窗定位被引用消息: msgid=%lld, 命中=%d.", msgid, located ? 1 : 0);
    return 0;
}

LRESULT CBalloonChatWindow::OnChatReplyRequest(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM lParam, BOOL& /*bHandled*/)
{
    long long* pMsgid = (long long*)lParam;
    if (pMsgid == NULL)
    {
        return 0;
    }
    const long long msgid = *pMsgid;
    delete pMsgid;

    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return 0;
    }

    balloonchatwnd::ChatMessage src;
    if (!list->GetMessageByMsgid(msgid, src))
    {
        return 0;   //该行已不在列表里（被删 / 尚未加载），安静忽略
    }

    //单聊里发送人非此即彼：自己发的取本端昵称，对方发的取对端昵称。
    //对端昵称由打开方传入（m_peerName），空则回退成"用户<id>"；本端取登录账号昵称。
    CString senderName;
    if (src.fromMe)
    {
        if (m_lpFMGClient != NULL)
        {
            senderName = m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName.c_str();
        }
    }
    else
    {
        senderName = m_peerName;
    }
    if (senderName.IsEmpty())
    {
        senderName.Format(_T("用户%u"), src.fromMe ? 0u : m_peerUserId);
    }
    // 被引用的若是图片，把原图的 fileid / md5 一并带上 —— 收方据此在引用块里画
    // 缩略图。ChatMessage 上这两项由收图时记下（见 imageFileId / imageMd5 的说明）。
    m_pendingQuote = balloonchatwnd::BuildQuoteFromMessage(
        src, senderName, src.imageFileId, src.imageMd5);
    RefreshReplyBar();
    LOG_INFO("单聊窗挂起引用回复: quote.msgid=%lld, valid=%d.",
             msgid, m_pendingQuote.m_valid ? 1 : 0);
    return 0;
}

LRESULT CBalloonChatWindow::OnChatLoadOlder(UINT /*uMsg*/, WPARAM /*wParam*/,
                                            LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (m_peerUserId == 0)
    {
        return 0;   // 调试入口没有真实会话，无历史可翻
    }
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return 0;
    }

    if (!balloonchatwnd::ShouldLoadOlderHistory(m_paging, list->ScrollY(),
                                                list->ViewHeight(), list->ContentHeight()))
    {
        return 0;
    }

    m_paging.m_loading = true;
    CIUSocket::GetInstance().RequestSyncMessages(
        1, (int64_t)m_peerUserId, m_paging.m_oldestMsgid, balloonchatwnd::kHistoryPageSize);
    LOG_INFO("单聊窗请求更早一页历史: peer=%u, before=%lld, limit=%d.",
             m_peerUserId, (long long)m_paging.m_oldestMsgid, balloonchatwnd::kHistoryPageSize);
    return 0;
}

LRESULT CBalloonChatWindow::OnChatRecallRequest(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM lParam, BOOL& /*bHandled*/)
{
    //气泡右键「撤回」冒泡上来：lParam 为堆上 long long（服务器 msgid），处理后 delete。
    long long* pMsgid = (long long*)lParam;
    if (pMsgid == NULL)
    {
        return 0;
    }
    long long msgid = *pMsgid;
    delete pMsgid;

    //调试入口（m_peerUserId==0，走 mock 数据）下没有真实会话，不发网络请求。
    if (m_peerUserId == 0)
    {
        return 0;
    }

    //单聊：ctype=1、cid=对端 userid。界面改动等服务端推回撤回通知后再做。
    CIUSocket::GetInstance().RecallMessage(kConvTypeSingle,
                                           (int64_t)m_peerUserId, msgid);
    return 0;
}

LRESULT CBalloonChatWindow::OnChatPinRequest(UINT /*uMsg*/, WPARAM wParam,
                                             LPARAM lParam, BOOL& /*bHandled*/)
{
    //气泡右键「Pin / 取消 Pin」冒泡上来：wParam 为目标状态（1 钉住、0 取消），
    //lParam 为堆上 long long（服务器 msgid），处理后 delete。
    long long* pMsgid = (long long*)lParam;
    if (pMsgid == NULL)
    {
        return 0;
    }
    long long msgid = *pMsgid;
    delete pMsgid;

    //调试入口（m_peerUserId==0，走 mock 数据）下没有真实会话，不发网络请求。
    if (m_peerUserId == 0)
    {
        return 0;
    }

    //单聊：ctype=1、cid=对端 userid。界面改动等服务端推回 Pin 通知后再做。
    CIUSocket::GetInstance().PinMessage(kConvTypeSingle, (int64_t)m_peerUserId,
                                        msgid, wParam != 0);
    return 0;
}

void CBalloonChatWindow::OnMessagePinned(long long msgid, bool pinned)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    //列表里没有这条（该消息尚未加载进当前视图）时安静忽略：本地库那份标记已经落好，
    //下次翻历史渲染出来时自然带着。
    list->SetPinnedByMsgid(msgid, pinned);
}

void CBalloonChatWindow::SetPinnedCount(int count)
{
    m_pinnedCount = (count > 0) ? count : 0;
    RefreshPinBanner();
}

void CBalloonChatWindow::AdjustPinnedCount(int delta)
{
    m_pinnedCount += delta;
    if (m_pinnedCount < 0)
    {
        m_pinnedCount = 0;
    }
    RefreshPinBanner();
}

void CBalloonChatWindow::RefreshPinBanner()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;   // 控件树还没建好，等 InitUi 之后主面板会再校准一次
    }

    const bool visibilityChanged =
        balloonchatwnd::UpdatePinBanner(content, balloonchatwnd::kIdPinBanner, m_pinnedCount);

    //提示带显隐改变了消息区上方的占位高度，处理与离线提示带完全一样：先整树重排让它
    //拿到高度，再走一次本窗自己的 relayout 把输入区块高度钉回用户选定值。
    if (visibilityChanged)
    {
        content->ForceLayout(content->GetRect());
        if (IsWindow())
        {
            ::PostMessage(m_hWnd, kRelayoutMsg, 0, 0);
        }
    }

    Invalidate();
}

void CBalloonChatWindow::RequestOpenHistory(int initialTypeFilter)
{
    if (m_peerUserId == 0 || m_hostPanel == NULL || !::IsWindow(m_hostPanel))
    {
        return;   //调试入口下没有主面板，安静忽略
    }

    //对端 userid 经堆 int64_t 随消息回投，由主面板处理后 delete。会话类型与"预选哪个
    //类型胶囊"一起塞在 wParam 里（约定见 UserSessionData.h 的 FMG_MSG_OPEN_HISTORY）。
    int64_t* pCid = new int64_t((int64_t)m_peerUserId);
    const WPARAM wp = (WPARAM)(kConvTypeSingle | (initialTypeFilter << 8));
    if (!::PostMessage(m_hostPanel, FMG_MSG_OPEN_HISTORY, wp, (LPARAM)pCid))
    {
        delete pCid;   //投递失败（主面板已销毁）自行回收，杜绝泄漏
    }
}

void CBalloonChatWindow::OnSendChatAckBackfill(const std::string& clientMsgId, long long msgid,
                                               long long timestampMs)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    //本窗口没有该 clientMsgId（是别的会话发出的）时安静忽略。
    list->BackfillSentMessage(clientMsgId, msgid, timestampMs);
}

void CBalloonChatWindow::OnMessageRecalled(long long msgid, const CString& tipText)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    //列表里没有该条（消息尚未加载进当前视图）时安静忽略——用户下次翻历史时，
    //本地库里那条已被标记撤回、根本不会再出现。
    list->RecallMessageByMsgid(msgid, tipText);
}

LRESULT CBalloonChatWindow::OnChatFileAction(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM lParam, BOOL& /*bHandled*/)
{
    // 文件气泡按钮点击冒泡上来：lParam 为堆上 ChatFileActionData，处理后 delete。
    balloonchatwnd::ChatFileActionData* d = (balloonchatwnd::ChatFileActionData*)lParam;
    if (d == NULL)
    {
        return 0;
    }

    switch (d->action)
    {
    case balloonchatwnd::kChatFileOpen:
        //用关联程序打开本地文件。
        if (!d->localPath.IsEmpty())
        {
            ::ShellExecuteW(m_hWnd, L"open", (LPCWSTR)d->localPath, NULL, NULL, SW_SHOWNORMAL);
        }
        break;

    case balloonchatwnd::kChatFileOpenFolder:
        //在资源管理器里打开本地文件所在文件夹、并选中该文件（explorer /select）。
        if (!d->localPath.IsEmpty())
        {
            CString param;
            param.Format(_T("/select,\"%s\""), (LPCTSTR)d->localPath);
            ::ShellExecuteW(m_hWnd, L"open", L"explorer.exe", (LPCWSTR)param, NULL, SW_SHOWNORMAL);
        }
        break;

    case balloonchatwnd::kChatFileSaveAs:
        //把本地文件另存到用户选定位置（默认填原文件名）。
        if (!d->localPath.IsEmpty())
        {
            wchar_t szSave[MAX_PATH] = { 0 };
            ::lstrcpynW(szSave, (LPCWSTR)d->fileName, MAX_PATH);
            OPENFILENAMEW ofn;
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = m_hWnd;
            ofn.lpstrFilter = L"所有文件\0*.*\0";
            ofn.lpstrFile   = szSave;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
            if (::GetSaveFileNameW(&ofn))
            {
                ::CopyFileW((LPCWSTR)d->localPath, szSave, FALSE);
            }
        }
        break;

    case balloonchatwnd::kChatFileCancel:
        //取消传输：发送方按 clientMsgId 定位那笔上传，接收方按 msgid 定位那笔下载。
        //两者都只是置位取消标志——真正的收尾（气泡置「已取消」态）等 FileTransfer 的
        //结果回投到达时统一做，避免这里先改状态、worker 稍后又回投一个失败态覆盖掉。
        if (!d->clientMsgId.empty())
        {
            LOG_INFO("单聊窗取消发文件: clientmsgid=%s.", d->clientMsgId.c_str());
            net::FileTransfer::GetInstance().CancelSend(d->clientMsgId);
        }
        else if (d->msgid > 0)
        {
            LOG_INFO("单聊窗取消收文件: msgid=%lld.", (long long)d->msgid);
            net::FileTransfer::GetInstance().CancelDownload(d->msgid);
        }
        else
        {
            LOG_ERROR("单聊窗取消文件传输但两个定位键都为空: action=%d.", d->action);
        }
        break;

    case balloonchatwnd::kChatFileAccept:
        //用户同意接收：这时才真正发起下载（收到消息时只渲染气泡、不自动下载）。
        StartFileDownloadFromBubble(*d);
        break;

    case balloonchatwnd::kChatFileReject:
        //拒绝接收：把本端气泡置「已拒绝」，同时告知发送方（服务端落库后转推，
        //发送方不在线则等其下次拉历史时看到）。用户反悔可再点「重新下载」。
        LOG_INFO("单聊窗拒绝接收文件: msgid=%lld.", (long long)d->msgid);
        MarkFileRejected(d->msgid);
        ReportFileAck(d->msgid, kFileAckRejected);
        break;

    case balloonchatwnd::kChatFileRetry:
        //重试分两种：本端刚发出的那笔（有 clientMsgId + 原始本地文件）是重新发送，
        //其余（对方发来的、以及历史里自己发过的）都是重新从服务器下载。
        if (!d->clientMsgId.empty() && !d->localPath.IsEmpty())
        {
            RestartFileSendFromBubble(*d);
        }
        else
        {
            StartFileDownloadFromBubble(*d);
        }
        break;

    default:
        break;
    }

    delete d;
    return 0;
}

LRESULT CBalloonChatWindow::OnChatImageAction(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM lParam, BOOL& /*bHandled*/)
{
    // 图片气泡右键菜单冒泡上来：lParam 为堆上 ChatImageActionData，处理后 delete。
    balloonchatwnd::ChatImageActionData* d = (balloonchatwnd::ChatImageActionData*)lParam;
    if (d == NULL)
    {
        return 0;
    }

    // 菜单只在图片就绪时才给这两项，正常不会拿到空路径；真拿到就是状态判定出了岔子，
    // 记一条日志便于回查，不再往下走。
    if (d->imagePath.empty())
    {
        LOG_ERROR("单聊窗图片菜单动作缺本地路径: action=%d.", (int)d->id);
        delete d;
        return 0;
    }

    switch (d->id)
    {
    case balloonchatwnd::kChatMsgMenuSaveImageAs:
        //弹保存对话框 + 按原字节拷贝，与群聊窗共用同一份实现。
        balloonchatwnd::SaveChatImageAs(m_hWnd, d->imagePath, d->saveName);
        break;

    case balloonchatwnd::kChatMsgMenuResendImage:
        // 再次发送 = 把这张图作为一条<u>新</u>图片消息发到本会话，与选图 / 截图发送
        // 共用同一条链路（新的 clientMsgId、重新上传），不是重发原来那条。
        LOG_INFO("单聊窗再次发送图片: to=%u, path=%s.", m_peerUserId,
                 CStringToUtf8(CString(d->imagePath.c_str())).c_str());
        SendImageFile(d->imagePath);
        break;

    default:
        LOG_ERROR("单聊窗收到未知的图片菜单动作: action=%d.", (int)d->id);
        break;
    }

    delete d;
    return 0;
}

balloonchatwnd::ChatMessageList* CBalloonChatWindow::GetMessageList()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return NULL;
    }
    return dynamic_cast<balloonchatwnd::ChatMessageList*>(
        content->FindCtrlById(balloonchatwnd::kIdMessageArea));
}

void CBalloonChatWindow::StartFileDownloadFromBubble(const balloonchatwnd::ChatFileActionData& d)
{
    //下载靠 msgid 定位气泡、靠 fileid 向服务端取文件，缺一不可
    if (d.msgid <= 0 || d.fileId.empty())
    {
        LOG_ERROR("单聊窗发起文件下载失败(缺少定位信息): msgid=%lld, fileid=%s.",
                  (long long)d.msgid, d.fileId.c_str());
        return;
    }

    // 下载缓存按账号隔离（Users\<userId>\ChatFile\），取当前登录用户 userid。
    UINT selfId = 0;
    if (m_lpFMGClient != NULL)
    {
        selfId = m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID;
    }

    // 落地按原始文件名命名，传输层要 UTF-8；气泡里存的是显示用的宽字符。
    std::string fileNameUtf8 = CStringToUtf8(d.fileName);

    LOG_INFO("单聊窗发起文件下载: msgid=%lld, fileid=%s, size=%lld.",
             (long long)d.msgid, d.fileId.c_str(), (long long)d.sizeBytes);
    net::FileTransfer::GetInstance().EnqueueDownload(
        (int64_t)d.msgid, d.fileId, fileNameUtf8, d.md5, d.sizeBytes, net::CurrentUserAccount());

    // 先把气泡切到「下载中」，随后的进度 / 结果按 msgid 回投更新本行。
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list != NULL)
    {
        list->MarkFileDownloadStartedByMsgid(d.msgid);
    }

    // 告知发送方"我开始收了"。大文件要传一阵子，先让对方看到进展，别干等着。
    ReportFileAck(d.msgid, kFileAckReceiving);
}

void CBalloonChatWindow::ReportFileAck(long long msgid, int state)
{
    // 只有真实会话下的收到消息才谈得上回执：msgid 是服务端分配的，本端发出的消息
    // 与调试入口都没有它；对端 userid 则是这条回执要送达的目标。
    if (m_peerUserId == 0 || msgid <= 0)
    {
        return;
    }
    CIUSocket::GetInstance().ReportFileTransferAck((int64_t)m_peerUserId, msgid, state);
}

void CBalloonChatWindow::RestartFileSendFromBubble(const balloonchatwnd::ChatFileActionData& d)
{
    // 真实会话才重发（调试入口无对端绑定）。
    if (m_peerUserId == 0)
    {
        return;
    }
    if (d.clientMsgId.empty() || d.localPath.IsEmpty())
    {
        LOG_ERROR("单聊窗重发文件失败(缺少定位信息或本地路径): clientmsgid=%s.",
                  d.clientMsgId.c_str());
        return;
    }

    // 源文件可能已被移走 / 删除 —— 那样重发注定失败，直接把气泡标失败并告知用户，
    // 不必白跑一趟传输流程。
    DWORD attr = ::GetFileAttributesW((LPCWSTR)d.localPath);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        LOG_ERROR("单聊窗重发文件失败(源文件已不存在): clientmsgid=%s.", d.clientMsgId.c_str());
        balloonchatwnd::ChatMessageList* failList = GetMessageList();
        if (failList != NULL)
        {
            failList->MarkFileUploadFailed(d.clientMsgId);
        }
        ::MessageBoxW(m_hWnd, L"源文件已不存在，无法重新发送。", L"提示", MB_OK | MB_ICONWARNING);
        return;
    }

    // 沿用原 clientMsgId：气泡还是那一条，进度 / 结果照旧按它对回，无需另插一行。
    std::wstring localPath((LPCWSTR)d.localPath);
    LOG_INFO("单聊窗重新发送文件: to=%u, clientmsgid=%s.", m_peerUserId, d.clientMsgId.c_str());
    net::FileTransfer::GetInstance().EnqueueSend(
        1 /*单聊*/, (int64_t)m_peerUserId, d.clientMsgId, localPath);

    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list != NULL)
    {
        list->MarkFileUploadStartedByClientMsgId(d.clientMsgId);
    }
}

void CBalloonChatWindow::MarkFileRejected(long long msgid)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list != NULL)
    {
        list->MarkFileRejectedByMsgid(msgid);
    }
}

LRESULT CBalloonChatWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                        LPARAM lParam, BOOL& bHandled)
{
    // WM_DUI_NOTIFY 的 LPARAM 指向一个 DuiNotify（仅在本次同步派发内有效）。
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdSendButton)
    {
        DoSend();
        return 0;
    }

    // 顶部离线提示带只作告知、不可点击，故这里没有它的分支 —— 恢复在线统一走托盘 /
    // 主面板的状态菜单那个唯一入口。

    // 点发送按钮右侧「▼」→ 弹发送模式切换菜单。这里只投递自定义消息、不直接弹，
    // 避免在 DUI 鼠标事件的同步派发栈里启动模态菜单导致捕获/重入问题。
    if (pNotify != NULL
        && pNotify->code == balloonchatwnd::ChatSendButton::DUIN_SENDBTN_DROPDOWN
        && pNotify->ctrlId == balloonchatwnd::kIdSendButton)
    {
        ::PostMessage(m_hWnd, kOpenSendMenuMsg, 0, 0);
        return 0;
    }

    // 点击头部"消息记录"按钮 → 请主面板打开与对端的历史消息记录窗，落在"全部"分类上。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdHdrHistory)
    {
        RequestOpenHistory(balloonhistory::kHistoryTypeAll);
        return 0;
    }

    // 点击头部下方的「已 Pin N 条」提示带 → 同样是打开历史消息记录窗，但直接切到 Pin
    // 分类。本轮不另做一个"Pin 列表浮层"：消息记录窗里那份列表（可搜索、可翻页、可定位、
    // 可取消 Pin）本来就是要的东西，复用它比再画一份省得多，见 docs/消息Pin接入方案.md §8。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdPinBanner)
    {
        RequestOpenHistory(balloonhistory::kHistoryTypePinned);
        return 0;
    }

    // 点击头部"远程桌面"按钮（显示器图标）→ 通知主面板远程桌面协调器发起邀请
    // （由协调器统一发 cmd_rd_invite 并弹出"呼叫中"对话框）。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdHdrRemoteDesktop)
    {
        // 通话与远程协助都要走信令通道，离线时发起必然失败，在入口就拦住。
        if (!CheckOnlineFor(balloonmain::kActionRealtimeCall))
        {
            return 0;
        }
        if (m_peerUserId != 0 && m_hostPanel != NULL && ::IsWindow(m_hostPanel))
        {
            //单聊窗的 m_hMainDlg 为 NULL（关闭只销毁本窗、不退出程序），故经主面板
            //（m_hostPanel）转发给 CMainDlg 协调器。peerName 走堆 std::wstring，
            //最终由 CMainDlg 处理后 delete。
            ::PostMessage(m_hostPanel, FMG_MSG_RD_START_INVITE,
                          (WPARAM)m_peerUserId,
                          (LPARAM)new std::wstring(m_peerName.GetString()));
        }
        return 0;
    }

    // 点击头部"语音通话"按钮 → 通知主面板通话协调器发起语音呼叫（协调器统一发
    // cmd_call_invite 并弹"呼叫中"对话框）。机制同上方远程桌面：单聊窗 m_hMainDlg 为
    // NULL，故经主面板 m_hostPanel 转发给 CMainDlg 协调器；peerName 走堆 std::wstring，
    // 最终由 CMainDlg 处理后 delete。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdHdrVoice)
    {
        // 通话与远程协助都要走信令通道，离线时发起必然失败，在入口就拦住。
        if (!CheckOnlineFor(balloonmain::kActionRealtimeCall))
        {
            return 0;
        }
        if (m_peerUserId != 0 && m_hostPanel != NULL && ::IsWindow(m_hostPanel))
        {
            ::PostMessage(m_hostPanel, FMG_MSG_CALL_START_INVITE,
                          (WPARAM)m_peerUserId,
                          (LPARAM)new std::wstring(m_peerName.GetString()));
        }
        return 0;
    }

    // 点击头部"视频通话"按钮 → 发起视频呼叫：转投主面板通话协调器（同语音按钮两步路由，
    // 仅消息不同），由其发 cmd_call_invite(media=video) 并弹"呼叫中"对话框。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdHdrVideo)
    {
        // 通话与远程协助都要走信令通道，离线时发起必然失败，在入口就拦住。
        if (!CheckOnlineFor(balloonmain::kActionRealtimeCall))
        {
            return 0;
        }
        if (m_peerUserId != 0 && m_hostPanel != NULL && ::IsWindow(m_hostPanel))
        {
            ::PostMessage(m_hostPanel, FMG_MSG_CALL_START_VIDEO_INVITE,
                          (WPARAM)m_peerUserId,
                          (LPARAM)new std::wstring(m_peerName.GetString()));
        }
        return 0;
    }

    // 点击"窗口抖动"工具栏按钮 → 给对端发一条抖动消息。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbShake)
    {
        DoSendShake();
        return 0;
    }

    // 点击"表情"工具栏按钮 → 打开 emoji 选择面板。
    // 引用条上的 ✕：取消本次引用（清空待发快照并收起引用条）。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdReplyClose)
    {
        CancelPendingReply();
        return 0;
    }

    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbEmoji)
    {
        OpenEmojiPicker();
        return 0;
    }

    // 点击"字体"工具栏按钮（AF）→ 打开字体设置面板。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbFont)
    {
        OpenFontPanel();
        return 0;
    }

    // 点击"图片"工具栏按钮 → 选图并作为图片消息发送。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbImage)
    {
        PickAndSendImage();
        return 0;
    }

    // 点击"文件"工具栏按钮 → 选文件并作为文件消息发送。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbFile)
    {
        PickAndSendFile();
        return 0;
    }

    // 点击"截图"工具栏按钮 → 调起全屏截图浮层。这里只投递自定义消息、不直接发起：
    // 截图浮层是模态消息循环，若在 DUI 鼠标事件派发栈内启动会引发捕获 / 重入问题，
    // 导致截图结束后发送按钮等点击失灵（与发送▼菜单同理）。延后到鼠标事件外再发起。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdTbScreenshot)
    {
        ::PostMessage(m_hWnd, kDoScreenshotMsg, 0, 0);
        return 0;
    }

    // 点击头部信息块（个性签名所在）→ 请主面板打开对方的联系人资料窗。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonchatwnd::kIdHeaderInfo)
    {
        if (m_hostPanel != NULL && m_peerUserId != 0)
        {
            ::PostMessage(m_hostPanel, FMG_MSG_OPEN_CONTACT_PROFILE,
                          (WPARAM)m_peerUserId, 0);
        }
        return 0;
    }

    // 用户拖完中间分隔条（kIdInputSplitter）→ 记下当前"输入区块高度"，供本次
    // 运行期间新开的聊天窗、以及本窗口后续缩放时套用。分隔条松手时发一次
    // DUIN_VALUECHANGED；输入区块高度 = 分隔轴可用高度 - 消息区(pane0)高度。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_VALUECHANGED
        && pNotify->ctrlId == balloonchatwnd::kIdInputSplitter)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonwjui::DuiSplitter* sp = (content != NULL)
            ? dynamic_cast<balloonwjui::DuiSplitter*>(
                  content->FindCtrlById(balloonchatwnd::kIdInputSplitter))
            : NULL;
        if (sp != NULL)
        {
            RECT rc = sp->GetRect();
            int track = (rc.bottom - rc.top) - sp->GetBarThickness();
            if (track > 0)
            {
                balloonchatwnd::RememberChatInputHeightPx(track - sp->GetSplitPx());
            }
        }
        return 0;
    }

    // 其它控件的通知（工具栏 / 头部按钮的点击等）本阶段不处理。
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonChatWindow::OnTimer(UINT /*uMsg*/, WPARAM wParam,
                                    LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam == (WPARAM)kLinkOpenTimerId)
    {
        // 一次性定时器：双击间隔内没等到双击，这次点击算数，真的打开链接。
        ::KillTimer(m_hWnd, kLinkOpenTimerId);
        OpenPendingLink();
        return 0;
    }

    if (wParam == (WPARAM)kReplyTimerId)
    {
        // 一次性定时器：先关掉，再追加一条 mock 对方回复。
        ::KillTimer(m_hWnd, kReplyTimerId);

        balloonwjui::DuiControl* content = GetClientContent();
        if (content != NULL)
        {
            balloonchatwnd::ChatMessageList* list =
                dynamic_cast<balloonchatwnd::ChatMessageList*>(
                    content->FindCtrlById(balloonchatwnd::kIdMessageArea));
            if (list != NULL)
            {
                list->AppendMessage(balloonchatwnd::MakeTextMessage(
                    false, balloonchatwnd::MockAutoReply(m_replyIndex)));
                ++m_replyIndex;
            }
        }
        return 0;
    }

    if (wParam == (WPARAM)kTypingTimerId)
    {
        const unsigned long long now = ::GetTickCount64();

        // 本端：停手满 kTypingIdleMs 就给对端补一包 state=0。
        SendTypingAction(m_typingThrottle.OnTick(now));

        // 对端：满 kTypingExpireMs 还没等到新推送，说明对方多半掉线了（state=0 永远
        // 不会来），自动把提示收起。这是整条链路唯一的兜底，去掉的话那句"正在输入…"
        // 会一直挂在头部。
        if (m_peerTyping.OnTick(now))
        {
            LOG_INFO("对端正在输入提示已过期，自动收起: peer=%u.", m_peerUserId);
            RefreshTypingHeader();
        }
        return 0;
    }

    if (wParam == (WPARAM)kShakeTimerId)
    {
        // 抖动动画：把窗口移到 起点 + 当前步偏移；走完序列后停表并归位。
        if (m_shakeStep < 0 || m_shakeStep >= kShakeStepCount)
        {
            ::KillTimer(m_hWnd, kShakeTimerId);
            m_shakeStep = -1;
            return 0;
        }
        int dx = kShakeOffsets[m_shakeStep][0];
        int dy = kShakeOffsets[m_shakeStep][1];
        ::SetWindowPos(m_hWnd, NULL,
                       m_shakeOrigin.x + dx, m_shakeOrigin.y + dy, 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        ++m_shakeStep;
        if (m_shakeStep >= kShakeStepCount)
        {
            // 最后一步偏移已是 (0,0)（归位），停表。
            ::KillTimer(m_hWnd, kShakeTimerId);
            m_shakeStep = -1;
        }
        return 0;
    }

    bHandled = FALSE;
    return 0;
}

// 被引用的原图是否已经在本地图片缓存里？是就返回它的完整路径，否则空串。
// 引用块据此决定画缩略图还是画占位块。
//
// <u>只查缓存、不发起下载</u>：引用块指向的是另一条消息的图，那条消息未必在已加载
// 范围内，图片气泡那套"按 msgid 回填路径并刷新那一行"在这里用不上。所以效果是
// "引用刚聊过的图有缩略图，引用很早以前的图只有占位块"——完整的下载链路留待后续。
static CString ResolveQuoteThumbPath(long long selfUserId, const net::ReplyQuote& q,
                                     balloonchatwnd::ChatMessageList* list)
{
    if (selfUserId == 0 || q.m_md5.empty())
    {
        // 这里以前是静默返回，导致缩略图空白时日志里一条线索都没有，只能靠推理逐个
        // 排除。md5 为空说明发送方没把原图信息写进 quote（老消息、或发送侧漏填）。
        LOG_INFO("引用图片缺少定位信息，画占位: selfUserId=%lld, md5空=%d.",
                 (long long)selfUserId, q.m_md5.empty() ? 1 : 0);
        return CString();
    }
    const std::wstring cached =
        net::ImageTransfer::CacheFilePathForMd5(net::CurrentUserAccount(), q.m_md5);
    // CacheFilePathForMd5 只拼路径、不判断存在，得自己确认文件真在。
    if (cached.empty() || ::GetFileAttributesW(cached.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        // 本端还没下过这张图，引用块画占位块。这是只查缓存不下载的预期结果；
        // 记一条日志便于分辨两种都表现为空白的情形：路径没找到 vs 解码还没回来。
        // 缓存没命中不代表本端没有这张图：<u>自己发出去的图从来没被下载过</u>，
        // 它还在当初选的那个原始路径上，而缓存目录只存下载来的图。所以再退一步 ——
        // 去列表里找那条原消息，直接用它气泡上已经解析好的本地路径。
        balloonchatwnd::ChatMessage src;
        if (list != NULL && list->GetMessageByMsgid(q.m_msgid, src)
            && !src.imagePath.IsEmpty()
            && ::GetFileAttributesW((LPCWSTR)src.imagePath) != INVALID_FILE_ATTRIBUTES)
        {
            LOG_INFO("引用图片改用原消息气泡上的本地路径: msgid=%lld.", (long long)q.m_msgid);
            return src.imagePath;
        }
        LOG_INFO("引用图片未命中本地缓存且原消息不在列表里，画占位: md5=%s.", q.m_md5.c_str());
        return CString();
    }
    LOG_INFO("引用图片缩略图命中本地缓存: md5=%s.", q.m_md5.c_str());
    return CString(cached.c_str());
}

balloonchatwnd::ChatMessage CBalloonChatWindow::MakeRealTextMessage(bool fromMe, const CString& text,
                                                                    const net::TextFont& font,
                                                                    long long msgid,
                                                                    long long timestampMs,
                                                                    const std::string& clientMsgId,
                                                                    const net::ReplyQuote& quote)
{
    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeTextMessage(fromMe, text);
    msg.font = font;   // 自己发：当前字体；收到 / 历史：该条消息携带的字体

    // 撤回判定所需：服务器 msgid 与消息时间。本端刚发出时两者为 0，回执到达后由
    // ChatMessageList::BackfillSentMessage 按 clientMsgId 找到本行补上。
    msg.msgid       = msgid;
    msg.timestampMs = timestampMs;
    msg.clientMsgId = clientMsgId;

    // 气泡头像按各自 (facetype, customface) 解析：字形 / 自动头像画字形 + 底色，真图头像
    // 缓存命中画真图、未命中入队下载（完成后 OnAvatarDownloaded 回刷本气泡）。
    FillSenderAvatar(msg, fromMe);

    // 引用回复：本端乐观气泡、实时推送、历史渲染三条路都经本工厂，故只在这里填一次
    // 就够 —— 不像群聊窗那样每条路各填一遍，也就没有跑偏的机会。
    const long long selfForQuote = (m_lpFMGClient != NULL)
        ? (long long)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    // 本函数没有现成的 list 变量（它是纯构造消息结构的工厂），就地取一次 —— 引用图片
    // 缓存没命中时要回退去找原消息气泡上的本地路径。
    balloonwjui::DuiControl* quoteContent = GetClientContent();
    balloonchatwnd::ChatMessageList* quoteList = (quoteContent != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              quoteContent->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    balloonchatwnd::FillQuoteForRender(quote, msg,
                                       ResolveQuoteThumbPath(selfForQuote, quote, quoteList));
    return msg;
}

void CBalloonChatWindow::FillSenderAvatar(balloonchatwnd::ChatMessage& msg, bool fromMe)
{
    int64_t     selfId = (m_lpFMGClient != NULL)
                         ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    int64_t     userId;
    int         facetype;
    std::string customface;
    CString     fallbackName;

    if (fromMe)
    {
        userId   = selfId;
        facetype = 0;
        if (m_lpFMGClient != NULL)
        {
            const CBuddyInfo& me = m_lpFMGClient->m_UserMgr.m_UserInfo;
            facetype = (int)me.m_nFace;   // v2 facetype 存在 m_nFace
            customface.assign(me.m_strCustomFaceName.GetString(),
                              (size_t)me.m_strCustomFaceName.GetLength());   // customface UTF-8 字节
            fallbackName = me.m_strNickName.c_str();
        }
        if (fallbackName.IsEmpty())
        {
            fallbackName = _T("我");
        }
    }
    else
    {
        userId       = (int64_t)m_peerUserId;
        facetype     = m_peerFacetype;
        customface   = m_peerCustomface;
        // 对端昵称优先用打开方传入的 m_peerName，回退到窗口标题。
        fallbackName = !m_peerName.IsEmpty() ? m_peerName : GetTitle();
        if (fallbackName.IsEmpty())
        {
            fallbackName = _T("?");
        }
    }

    balloonchatwnd::AvatarRender ar = balloonchatwnd::ResolveAvatarRender(
        userId, facetype, customface, (LPCTSTR)fallbackName, selfId);

    // 只覆盖头像（单聊气泡不显示发送者名，故不设 senderName）。
    msg.senderUserId         = userId;
    msg.senderAvatarText     = ar.m_glyph;
    msg.senderAvatarColor    = ar.m_color;
    msg.senderAvatarImagePath = ar.m_imagePath;
}

void CBalloonChatWindow::OnAvatarDownloaded(const std::string& fileid)
{
    if (fileid.empty())
    {
        return;
    }
    int64_t selfId = (m_lpFMGClient != NULL)
                     ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    if (selfId <= 0)
    {
        return;
    }

    // 真图缓存路径（刚下好的）。取不到（异常）则不刷。
    std::wstring path =
        net::ImageTransfer::AvatarCacheFilePathForFileId(net::CurrentUserAccount(), fileid);
    if (path.empty() || ::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        return;
    }
    CString imagePath = path.c_str();

    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;

    // 对端真图头像命中：重建标题栏 / 任务栏图标（现可画真图）+ 刷新对端气泡头像。
    if (m_peerFacetype == balloonmain::avatar_kind_image && m_peerCustomface == fileid)
    {
        RebuildPeerIcons();
        if (list != NULL)
        {
            list->RefreshSenderAvatar((int64_t)m_peerUserId, imagePath);
        }
    }

    // 本人真图头像命中：刷新本人气泡头像（自己的头像通常本地已缓存，少经此下载路径）。
    if (m_lpFMGClient != NULL && list != NULL)
    {
        const CBuddyInfo& me = m_lpFMGClient->m_UserMgr.m_UserInfo;
        std::string myFace(me.m_strCustomFaceName.GetString(),
                           (size_t)me.m_strCustomFaceName.GetLength());
        if ((int)me.m_nFace == balloonmain::avatar_kind_image && myFace == fileid)
        {
            list->RefreshSenderAvatar(selfId, imagePath);
        }
    }
}

LRESULT CBalloonChatWindow::OnSendOnEnter(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 输入框按 Enter：等同点「发送」按钮。Ctrl+Enter 的换行已由过滤器在框内
    // 就地处理，不会走到这里。
    DoSend();
    return 0;
}

LRESULT CBalloonChatWindow::OnDoScreenshot(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 已脱离 DUI 鼠标事件派发栈，安全地发起模态截图浮层。
    DoScreenshot();
    return 0;
}

LRESULT CBalloonChatWindow::OnOpenSendMenu(UINT /*uMsg*/, WPARAM /*wParam*/,
                                           LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kIdSendButton);
    if (btn == NULL)
    {
        return 0;
    }

    // 取发送按钮左上角的屏幕坐标作锚点。
    RECT rc = btn->GetRect();
    POINT anchor = { rc.left, rc.top };
    ::ClientToScreen(m_hWnd, &anchor);

    const UINT kMenuIdEnter     = 1;   // 「按 Enter 发送消息」
    const UINT kMenuIdCtrlEnter = 2;   // 「按 Ctrl+Enter 发送消息」
    balloonchatwnd::SendKeyMode cur = balloonchatwnd::GetChatSendMode();

    balloonwjui::DuiMenu menu;
    menu.AppendChecked(kMenuIdEnter,     _T("按 Enter 发送消息"),
                       cur == balloonchatwnd::kSendOnEnter);
    menu.AppendChecked(kMenuIdCtrlEnter, _T("按 Ctrl+Enter 发送消息"),
                       cur == balloonchatwnd::kSendOnCtrlEnter);

    // 菜单弹在发送按钮上方、不遮挡按钮：按项数估算菜单高度（DuiMenu 行高 32），
    // 把锚点从按钮顶部再上移「菜单高 + 间隙」。
    const int kMenuRowHeightPx = 32;   // 与 balloonui DuiMenu 的行高一致
    const int kMenuGapPx       = 6;    // 菜单底与按钮顶之间的间隙
    int menuHeight = menu.GetCount() * kMenuRowHeightPx;
    UINT chosen = menu.TrackPopup(anchor.x, anchor.y - menuHeight - kMenuGapPx, m_hWnd);
    if (chosen == kMenuIdEnter)
    {
        balloonchatwnd::SetChatSendMode(balloonchatwnd::kSendOnEnter);
        RefreshSendModeUi();
    }
    else if (chosen == kMenuIdCtrlEnter)
    {
        balloonchatwnd::SetChatSendMode(balloonchatwnd::kSendOnCtrlEnter);
        RefreshSendModeUi();
    }
    return 0;
}

void CBalloonChatWindow::RefreshSendModeUi()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::SendKeyMode mode = balloonchatwnd::GetChatSendMode();

    // 左下角提示文字随模式联动。
    balloonwjui::DuiLabel* hint = dynamic_cast<balloonwjui::DuiLabel*>(
        content->FindCtrlById(balloonchatwnd::kIdHintLabel));
    if (hint != NULL)
    {
        hint->SetText(balloonchatwnd::GetChatSendHintText(mode));
    }

    // 输入框 placeholder 随模式联动（避免空框时提示与实际行为矛盾）。
    balloonwjui::DuiRichEditHost* input = dynamic_cast<balloonwjui::DuiRichEditHost*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (input != NULL)
    {
        input->SetPlaceholder(balloonchatwnd::GetChatSendPlaceholder(mode));
    }
}

// ---- 输入框图文混排发送用的本地辅助 -----------------------------------

// DuiRichEditHost::EnumContent 的逐段收集回调：把（文本 / 内嵌图+标记）按文档顺序
// 收进 vector，供 BuildOutgoingParts 聚合成待发送部分。ctx 为收集目标 vector。
static void CollectInputSegment(bool isImage, LPCTSTR text, DWORD_PTR tag, void* ctx)
{
    std::vector<balloonchatwnd::RichInputSegment>* segs =
        (std::vector<balloonchatwnd::RichInputSegment>*)ctx;
    if (segs == NULL)
    {
        return;
    }
    balloonchatwnd::RichInputSegment seg;
    seg.isImage = isImage;
    seg.text = (text != NULL) ? text : _T("");
    seg.tag = tag;
    segs->push_back(seg);
}

// 取 RichEdit 子窗口的纯文本（内嵌对象处为占位符 WCH_EMBEDDING 0xFFFC），用于按
// 文档顺序拆分图文。
static CString GetRichEditPlainText(HWND hRich)
{
    if (hRich == NULL || !::IsWindow(hRich))
    {
        return CString();
    }
    // RichEdit 的 WM_GETTEXT(GetWindowText) 不一定把内联 OLE 对象（截图）还原成占位符
    // 0xFFFC —— 实测返回空格，导致 SplitRichInputToParts 取不到内联图、纯图输入拆出空段、
    // DoSend 提前返回不发。改用 EM_GETTEXTEX：它把每个嵌入对象可靠地表示为
    // WCH_EMBEDDING(0xFFFC)，供按文档顺序拆分图文。
    GETTEXTLENGTHEX gtl;
    ::ZeroMemory(&gtl, sizeof(gtl));
    gtl.flags    = GTL_NUMCHARS;          // 字符数（每个嵌入对象计 1）
    gtl.codepage = 1200;                  // Unicode
    LONG nChars = (LONG)::SendMessage(hRich, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    if (nChars <= 0)
    {
        return CString();
    }

    CString s;
    GETTEXTEX gt;
    ::ZeroMemory(&gt, sizeof(gt));
    gt.cb       = (DWORD)((nChars + 1) * sizeof(WCHAR));   // 缓冲字节数（含结尾 NUL）
    gt.flags    = GT_RAWTEXT;
    gt.codepage = 1200;
    ::SendMessage(hRich, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)s.GetBuffer(nChars + 1));
    s.ReleaseBuffer();
    return s;
}

void CBalloonChatWindow::SendTextPartReal(const CString& text)
{
    // 真实会话的一段文字：乐观回显气泡 + 经网络发文本消息 + 冒泡会话。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL || m_peerUserId == 0)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    //先生成 clientMsgId 再建气泡：气泡要带上它，回执到达后才能按它找回本行、
    //把服务器 msgid 与时间戳回填进去（撤回菜单的时限判断依赖这两个值）。
    std::string clientMsgId = GenClientMsgId();
    //本端乐观气泡也要显示引用块 —— 否则"发出去自己看不到引用、对方却看得到"。
    list->AppendMessage(MakeRealTextMessage(true, text, m_currentFont, 0, 0,
                                            clientMsgId, m_pendingQuote));

    std::string utf8 = CStringToUtf8(text);
    CIUSocket::GetInstance().SendChatTextMessage((int64_t)m_peerUserId, clientMsgId,
                                                 utf8, m_currentFont, m_pendingQuote);

    //引用是"一次性"的：带出去之后就清掉并收起引用条，
    //否则后面每条消息都会带同一个引用。
    CancelPendingReply();
    LOG_INFO("单聊窗发送一段文字: to=%u, clientmsgid=%s.", m_peerUserId, clientMsgId.c_str());

    if (m_hostPanel != NULL && ::IsWindow(m_hostPanel))
    {
        ConvSendBumpData* pBump = new ConvSendBumpData();
        pBump->ctype   = 1;
        pBump->cid     = (long long)m_peerUserId;
        pBump->msgType = kMsgTypeText;
        // 会话列表摘要：内联表情标记替换成 [表情]、转义斜杠还原，不暴露 /f["id"]。
        pBump->digest  = CStringToUtf8(balloonchat::BodyToDigest(text));
        ::PostMessage(m_hostPanel, FMG_MSG_CONV_SEND_BUMP,
                      (WPARAM)m_peerUserId, (LPARAM)pBump);
    }
}

void CBalloonChatWindow::SendImagePartReal(int imageIndex)
{
    // 第 imageIndex 个内嵌图（文档顺序）对应 m_pendingShotPaths[imageIndex]（插入顺序）。
    // 直接发截图浮层存下的原 PNG 文件——不从 OLE 对象抽位图（那条路在静态图对象上取
    // 到的是黑块），也不重新编码。映射靠"插入顺序↔文档顺序"，详见 OnScreenshotStage。
    if (imageIndex < 0 || imageIndex >= (int)m_pendingShotPaths.size())
    {
        LOG_WARNING("单聊窗发送：内嵌图序号 %d 无对应源文件（图序与追踪不一致），跳过。",
                    imageIndex);
        return;
    }
    SendImageFile(m_pendingShotPaths[imageIndex]);
}

void CBalloonChatWindow::ApplyConnectionState(int state)
{
    m_connUiState = state;

    // 掉线 / 主动离线：本端的"正在输入"通告静默复位（此刻发 state=0 也送不出去，
    // 且重连后节流器应从干净状态起步）；对端那句提示同样收起 —— 连都断了，界面上
    // 再说人家正在输入只会误导。
    if (state != (int)balloonmain::kConnUiOnline)
    {
        m_typingThrottle.Reset();
        if (m_peerTyping.Clear())
        {
            RefreshTypingHeader();
        }
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;   // 控件树还没建好，等窗口创建完主面板会再推一次
    }

    const bool bOnline = (state == (int)balloonmain::kConnUiOnline);

    // 顶部提示带：在线时不显示（GetConnectionBannerText 返回 NULL），离线 / 正在连接
    // 时显示对应文字。不可见的子控件不占布局高度，故无需另调整尺寸。
    balloonchatwnd::ChatBannerLabel* banner =
        dynamic_cast<balloonchatwnd::ChatBannerLabel*>(
            content->FindCtrlById(balloonchatwnd::kIdOfflineBanner));
    // 提示带显隐是否真的发生了变化。变化了才需要补一次布局（见本函数末尾）。
    bool bannerVisibilityChanged = false;
    if (banner != NULL)
    {
        const bool wasVisible = banner->IsVisible();
        const wchar_t* text = balloonmain::GetConnectionBannerText(state);
        if (text == NULL)
        {
            banner->SetVisible(false);
        }
        else
        {
            banner->SetText(text);
            // 离线用琥珀色系（"需要你处理"的语气），正在连接用中性灰（只是告知）。
            // 两种都是普通文字、整条不可点 —— 恢复在线走托盘 / 主面板的状态菜单。
            if (state == (int)balloonmain::kConnUiOffline)
            {
                banner->SetBannerColors(kOfflineBannerBgColor,
                                        kOfflineBannerBorderColor);
                banner->SetTextColor(kOfflineBannerColor);
            }
            else
            {
                banner->SetBannerColors(kConnectingBannerBgColor,
                                        kConnectingBannerBorderColor);
                banner->SetTextColor(kConnectingBannerColor);
            }
            banner->SetVisible(true);
        }
        bannerVisibilityChanged = (banner->IsVisible() != wasVisible);
    }

    // 输入框：离线时置为只读而不是禁用 —— 已经打好的草稿还看得见、能复制，只是打不了字。
    balloonwjui::DuiEditHost* edit = dynamic_cast<balloonwjui::DuiEditHost*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (edit != NULL)
    {
        edit->SetReadOnly(!bOnline);
    }

    // 发送按钮置灰并不再响应点击。
    balloonwjui::DuiControl* sendBtn = content->FindCtrlById(balloonchatwnd::kIdSendButton);
    if (sendBtn != NULL)
    {
        sendBtn->SetEnabled(bOnline);
    }

    // 提示条显隐改变了客户区顶部的占位高度，必须补一次整树布局才生效 ——
    // DuiControl::SetVisible 只 Invalidate 重绘、不重新布局，光设可见的话提示条
    // 拿不到高度，画出来是空的（反过来隐藏它，那条空白也不会自动收掉）。
    // 整树重排会顺带打乱消息区 / 输入区的分割，故随后走一次窗口自己的 relayout，
    // 把输入区块高度重新钉回用户选定值 —— 与窗口缩放后的处理是同一条路。
    if (bannerVisibilityChanged)
    {
        content->ForceLayout(content->GetRect());
        if (IsWindow())
        {
            ::PostMessage(m_hWnd, kRelayoutMsg, 0, 0);
        }
    }

    Invalidate();
    LOG_INFO(_T("单聊窗应用连接态：peer=%u, state=%d(0=在线 1=正在连接 2=离线), 提示条显隐变化=%d。"),
        m_peerUserId, state, (int)bannerVisibilityChanged);
}

bool CBalloonChatWindow::CheckOnlineFor(int action)
{
    // 门禁只看本窗记下的连接态：它由 CMainDlg 在每次状态变化时推下来，与托盘图标、
    // 状态胶囊同源。
    if (m_connUiState == (int)balloonmain::kConnUiOnline)
    {
        return true;
    }

    LOG_INFO(_T("单聊窗操作被拦截：peer=%u, action=%d, state=%d。"),
        m_peerUserId, action, m_connUiState);
    ::MessageBox(m_hWnd, balloonmain::GetBlockedActionTip(action),
                 _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
    return false;
}

void CBalloonChatWindow::DoSend()
{
    // 离线 / 连接未就绪时不发：消息发出去也到不了服务器，气泡却会留在窗口里，
    // 让用户误以为发出去了。
    if (!CheckOnlineFor(balloonmain::kActionSendMessage))
    {
        return;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    balloonwjui::DuiRichEditHost* input = dynamic_cast<balloonwjui::DuiRichEditHost*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (input == NULL || list == NULL)
    {
        return;
    }

    // 按文档顺序枚举输入框内容（文本段 / 内嵌图段+标记），聚合成待发送部分序列：
    // 表情（带 faceId 标记的内嵌图）随文字编码进文本部分，截图等非表情图独立成段。
    std::vector<balloonchatwnd::RichInputSegment> segs;
    input->EnumContent(&CollectInputSegment, &segs);
    std::vector<balloonchatwnd::RichOutgoingPart> parts =
        balloonchatwnd::BuildOutgoingParts(segs);
    if (parts.empty())
    {
        return;   // 空 / 全空白，不发
    }

    bool hasImage = false;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (parts[i].kind == balloonchatwnd::kRichPartImage)
        {
            hasImage = true;
            break;
        }
    }

    // 调试入口（--chatwindow，无对端绑定）：不支持发图。把各文字段拼起来做 mock 回显
    // 与自动回复，保留离线核对体验。
    if (m_peerUserId == 0)
    {
        CString joined;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (parts[i].kind == balloonchatwnd::kRichPartText)
            {
                joined += parts[i].text;
            }
        }
        joined.Trim();
        input->SetText(_T(""));
        if (joined.IsEmpty())
        {
            return;
        }
        balloonchatwnd::ChatMessage echo = balloonchatwnd::MakeTextMessage(true, joined);
        echo.font = m_currentFont;
        list->AppendMessage(echo);
        ::SetTimer(m_hWnd, kReplyTimerId, kReplyDelayMs, NULL);
        //调试入口没有对端可通告，这里只是把节流器复位，不会真的发包。
        StopTypingNotify();
        return;
    }

    // 真实会话：按文档顺序逐段发出（文字段→文字消息，内嵌图→图片消息）。各段乐观
    // 气泡按序追加。注意：发图要从 RichEdit 抽内嵌对象，故须在全部抽取 / 发送完成<u>之后</u>
    // 才清空输入框（提前清空会把内嵌图一起抹掉、抽取失败）。
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (parts[i].kind == balloonchatwnd::kRichPartText)
        {
            SendTextPartReal(parts[i].text);
        }
        else
        {
            SendImagePartReal(parts[i].imageIndex);
        }
    }
    input->SetText(_T(""));
    m_pendingShotPaths.clear();   // 内嵌图已发完，清空源文件追踪

    //消息都发出去了，"正在输入"自然该收尾：立即补一包 state=0，别让对端那句提示
    //干等到过期才消失。
    StopTypingNotify();
}

void CBalloonChatWindow::PickAndSendImage()
{
    // 图片要先上传到图片服务器再发消息，离线时两步都做不了。
    if (!CheckOnlineFor(balloonmain::kActionTransferFile))
    {
        return;
    }

    // 调试入口（无对端绑定）不支持真实发图。
    if (m_peerUserId == 0)
    {
        return;
    }

    // 弹文件选择对话框选一张图片。
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hWnd;
    ofn.lpstrFilter = L"图片文件\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0所有文件\0*.*\0";
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!::GetOpenFileNameW(&ofn))
    {
        return;   // 用户取消选择
    }
    SendImageFile(szFile);
}

void CBalloonChatWindow::SendImageFile(const std::wstring& path)
{
    // 真实会话才发图（调试入口无对端绑定）。选图发送与截图「发送」共用本函数。
    if (m_peerUserId == 0)
    {
        return;
    }

    // 读图片真实宽高以算气泡内展示尺寸；解码失败则用占位尺寸（上传仍按字节流走）。
    int realW = 0;
    int realH = 0;
    {
        Gdiplus::Image img(path.c_str());
        if (img.GetLastStatus() == Gdiplus::Ok)
        {
            realW = (int)img.GetWidth();
            realH = (int)img.GetHeight();
        }
    }
    int dispW = 0;
    int dispH = 0;
    ComputeImageDisplaySize(realW, realH, dispW, dispH);

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 乐观回显：先插一条"上传中"的图片气泡（显示本地图），再交工作线程上传。
    std::string clientMsgId = GenClientMsgId();
    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeImageMessage(
        true, CString(path.c_str()), dispW, dispH, clientMsgId, 1 /*上传中*/);
    // 头像沿用本端真实头像（字形 / 真图，按本端 (facetype, customface) 解析）。
    FillSenderAvatar(msg, true);
    list->AppendMessage(msg);

    // 交图片传输工作线程：申请上传→传字节→发图片消息。进度 / 结果经
    // FMG_MSG_V2_IMAGE_SEND_PROGRESS / _RESULT 回投，主面板再路由回本窗口。
    net::ImageTransfer::GetInstance().EnqueueSend(
        1 /*单聊*/, (int64_t)m_peerUserId, clientMsgId, path);
    LOG_INFO("单聊窗发图: to=%u, clientmsgid=%s.", m_peerUserId, clientMsgId.c_str());

    // 通知主面板把该会话冒泡到顶部、刷新预览（"[图片]"）。
    if (m_hostPanel != NULL && ::IsWindow(m_hostPanel))
    {
        ConvSendBumpData* pBump = new ConvSendBumpData();
        pBump->ctype   = 1;
        pBump->cid     = (long long)m_peerUserId;
        pBump->msgType = kMsgTypeImage;
        pBump->digest  = net::MessageProtocolClient::digestForMsgType(kMsgTypeImage, std::string());
        ::PostMessage(m_hostPanel, FMG_MSG_CONV_SEND_BUMP,
                      (WPARAM)m_peerUserId, (LPARAM)pBump);
    }
}

void CBalloonChatWindow::PickAndSendFile()
{
    // 文件要先上传到文件服务器再发消息，离线时两步都做不了。
    if (!CheckOnlineFor(balloonmain::kActionTransferFile))
    {
        return;
    }

    // 调试入口（无对端绑定）不支持真实发文件。
    if (m_peerUserId == 0)
    {
        return;
    }

    // 弹文件选择对话框选一个任意类型文件。
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hWnd;
    ofn.lpstrFilter = L"所有文件\0*.*\0";
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!::GetOpenFileNameW(&ofn))
    {
        return;   // 用户取消选择
    }
    SendFileData(szFile);
}

void CBalloonChatWindow::SendFileData(const std::wstring& path)
{
    // 真实会话才发文件（调试入口无对端绑定）。
    if (m_peerUserId == 0)
    {
        return;
    }

    // 取文件大小（仅取属性、不读盘）。空文件 / 取不到大小则不发。
    long long sizeBytes = 0;
    {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)
            && (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            sizeBytes = ((long long)fad.nFileSizeHigh << 32) | (long long)fad.nFileSizeLow;
        }
    }
    if (sizeBytes <= 0)
    {
        LOG_ERROR("单聊窗发文件: 文件为空或读不到大小, to=%u.", m_peerUserId);
        return;
    }

    // 文件名取路径基名（随消息发出、气泡显示）。
    CString fileName;
    {
        size_t sep = path.find_last_of(L"\\/");
        fileName = (sep == std::wstring::npos)
                       ? CString(path.c_str())
                       : CString(path.substr(sep + 1).c_str());
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 乐观回显：先插一条"上传中"文件气泡，再交工作线程上传。
    std::string clientMsgId = GenClientMsgId();
    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeFileMessage(
        true, fileName, sizeBytes, CString(path.c_str()), clientMsgId, 1 /*上传中*/);
    // 头像沿用本端真实头像（字形 / 真图，按本端 (facetype, customface) 解析）。
    FillSenderAvatar(msg, true);
    list->AppendMessage(msg);

    // 交文件传输工作线程：申请上传→传字节→发文件消息。进度 / 结果经
    // FMG_MSG_V2_FILE_SEND_PROGRESS / _RESULT 回投，主面板再路由回本窗口。
    net::FileTransfer::GetInstance().EnqueueSend(
        1 /*单聊*/, (int64_t)m_peerUserId, clientMsgId, path);
    LOG_INFO("单聊窗发文件: to=%u, clientmsgid=%s, size=%lld.",
             m_peerUserId, clientMsgId.c_str(), sizeBytes);

    // 通知主面板把该会话冒泡到顶部、刷新预览（"[文件]"）。
    if (m_hostPanel != NULL && ::IsWindow(m_hostPanel))
    {
        ConvSendBumpData* pBump = new ConvSendBumpData();
        pBump->ctype   = 1;
        pBump->cid     = (long long)m_peerUserId;
        pBump->msgType = kMsgTypeFile;
        pBump->digest  = net::MessageProtocolClient::digestForMsgType(kMsgTypeFile, std::string());
        ::PostMessage(m_hostPanel, FMG_MSG_CONV_SEND_BUMP,
                      (WPARAM)m_peerUserId, (LPARAM)pBump);
    }
}

void CBalloonChatWindow::DoScreenshot()
{
    // 调试入口（无对端绑定）不支持真实发图，截图也就无处可发，直接不弹。
    if (m_peerUserId == 0)
    {
        return;
    }
    // 模态发起截图：复制 / 保存由浮层内部完成；「发送」立即发图、「发送到对话框」
    // 把截图内联插入输入框（经下面两个回调）。
    balloonscreenshot::CScreenCaptureOverlay overlay;
    overlay.Run(m_hWnd, &CBalloonChatWindow::OnScreenshotSend,
                &CBalloonChatWindow::OnScreenshotStage, this);
}

void CBalloonChatWindow::OnScreenshotSend(void* ctx, const wchar_t* pngPath)
{
    CBalloonChatWindow* self = static_cast<CBalloonChatWindow*>(ctx);
    if (self == NULL || pngPath == NULL || pngPath[0] == L'\0')
    {
        return;
    }
    // 截图「发送」走与选图发送同一条链路（乐观气泡 + imgserver 上传 + 会话冒泡）。
    self->SendImageFile(pngPath);
}

void CBalloonChatWindow::OnScreenshotStage(void* ctx, const wchar_t* pngPath)
{
    CBalloonChatWindow* self = static_cast<CBalloonChatWindow*>(ctx);
    if (self == NULL || pngPath == NULL || pngPath[0] == L'\0')
    {
        return;
    }
    // 截图「发送到对话框」：把截图作为内联图片插入输入框（按上限缩放展示），不立即发送；
    // 用户可再配文字，按「发送」时由 DoSend 按文档顺序连同文字一起逐段发出。
    balloonwjui::DuiControl* content = self->GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiRichEditHost* input = dynamic_cast<balloonwjui::DuiRichEditHost*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (input == NULL)
    {
        return;
    }
    // 内联展示按 320×320 上限缩放（保宽高比），避免大截图把输入框撑得过高。
    const int kInlineMax = 320;
    if (input->InsertImageFromFile(pngPath, kInlineMax, kInlineMax))
    {
        // 记下源 PNG（插入顺序）：发送时第 k 个内嵌图即取第 k 个源文件直接发，不抽 OLE。
        self->m_pendingShotPaths.push_back(pngPath);
    }
}

void CBalloonChatWindow::OnImageSendProgress(const std::string& clientMsgId,
                                             long long sent, long long total)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }
    int percent = 0;
    if (total > 0)
    {
        percent = (int)(sent * 100 / total);
    }
    list->UpdateImageUploadProgress(clientMsgId, percent);
}

void CBalloonChatWindow::OnImageSendResult(bool ok, const std::string& clientMsgId,
                                           const std::string& fileid,
                                           const std::string& md5)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }
    if (ok)
    {
        list->MarkImageUploadDone(clientMsgId);
        // 回填 fileid / md5：本条将来被引用时要写进 content.quote，
        // 收方据此在引用块里画缩略图。不回填的话引用自己发的图带不出定位信息。
        list->SetImageIdsByClientMsgId(clientMsgId,
                                       Utf8ToCString(fileid), Utf8ToCString(md5));
    }
    else
    {
        list->MarkImageUploadFailed(clientMsgId);
    }
}

void CBalloonChatWindow::OnFileSendProgress(const std::string& clientMsgId,
                                            long long sent, long long total)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }
    int percent = 0;
    if (total > 0)
    {
        percent = (int)(sent * 100 / total);
    }
    list->UpdateFileUploadProgress(clientMsgId, percent);
}

void CBalloonChatWindow::OnFileSendResult(bool ok, bool cancelled, const std::string& clientMsgId)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }
    if (ok)
    {
        list->MarkFileUploadDone(clientMsgId);
    }
    else if (cancelled)
    {
        //用户主动取消：置「已取消」态（保留进度作灰色停位），不按失败处理
        list->MarkFileCancelledByClientMsgId(clientMsgId);
    }
    else
    {
        list->MarkFileUploadFailed(clientMsgId);
    }
}

void CBalloonChatWindow::DoSendShake()
{
    if (!CheckOnlineFor(balloonmain::kActionShakeWindow))
    {
        return;
    }

    // 抖动只在真实会话下发送（调试入口无对端绑定，仅本地晃一下窗口看效果）。
    if (m_peerUserId == 0)
    {
        ShakeWindow();
        return;
    }

    // 发送侧频率限制：对同一目标，两次窗口抖动的间隔不得小于 kShakeMinIntervalMs
    // （5 秒）。距上次成功发送不足该间隔的点击被拦截 —— 不发送、不晃自己，只在
    // 本窗口居中提示，避免对对端刷屏式抖动。被拦截不刷新计时（间隔始终从上次
    // 成功发送算起，连点不会把可发送时刻一直往后推）。
    if (!m_sendShakeLimiter.TryPass(::GetTickCount64(), kShakeMinIntervalMs))
    {
        CString tip;
        tip.Format(_T("发送窗口抖动过于频繁，请 %d 秒后再试"),
                   (int)(kShakeMinIntervalMs / 1000));
        AppendSystemTip(tip);
        LOG_INFO("单聊窗发送窗口抖动被节流（距上次不足 %llu ms 忽略）: to=%u.",
                 (unsigned long long)kShakeMinIntervalMs, m_peerUserId);
        return;
    }

    // 经网络层发送抖动消息（msgtype=13，content={}）。回执经 cmd_send_message
    // 应答异步回投，失败时由主面板提示（与文本同路径）。
    std::string clientMsgId = GenClientMsgId();
    CIUSocket::GetInstance().SendChatShakeMessage((int64_t)m_peerUserId, clientMsgId);
    LOG_INFO("单聊窗发送窗口抖动: to=%u, clientmsgid=%s.", m_peerUserId, clientMsgId.c_str());

    // 本地反馈：聊天区居中提示 + 晃自己窗口。
    AppendSystemTip(_T("你向对方发送了窗口抖动"));
    ShakeWindow();

    // 通知主面板冒泡该会话（自己发出，不计未读）；预览统一用 digestForMsgType 取占位。
    if (m_hostPanel != NULL && ::IsWindow(m_hostPanel))
    {
        ConvSendBumpData* pBump = new ConvSendBumpData();
        pBump->ctype   = 1;                   // 单聊
        pBump->cid     = (long long)m_peerUserId;
        pBump->msgType = kMsgTypeShake;
        pBump->digest  = net::MessageProtocolClient::digestForMsgType(kMsgTypeShake, std::string());
        ::PostMessage(m_hostPanel, FMG_MSG_CONV_SEND_BUMP,
                      (WPARAM)m_peerUserId, (LPARAM)pBump);
    }
}

void CBalloonChatWindow::BringChatWindowToFront()
{
    if (!IsWindow())
    {
        return;
    }
    // 最小化则还原，否则确保可见。
    if (IsIconic())
    {
        ShowWindow(SW_RESTORE);
    }
    else
    {
        ShowWindow(SW_SHOW);
    }
    // 置顶并尽力抢到前台（后台进程抢前台受系统前台锁限制，BringWindowToTop +
    // SetForegroundWindow 是常规尽力做法；失败时至少会闪烁任务栏提示）。
    ::BringWindowToTop(m_hWnd);
    ::SetForegroundWindow(m_hWnd);
}

void CBalloonChatWindow::ShakeWindow()
{
    if (!IsWindow())
    {
        return;
    }
    // 最小化状态下先还原，否则晃动不可见。
    if (IsIconic())
    {
        ShowWindow(SW_RESTORE);
    }
    // 仅当当前不在抖动时记录起点（避免在已偏移位置上取到错误基准）；
    // 已在抖动则只把步数复位到 0，从同一起点重放。
    if (m_shakeStep < 0)
    {
        RECT rc = { 0 };
        ::GetWindowRect(m_hWnd, &rc);
        m_shakeOrigin.x = rc.left;
        m_shakeOrigin.y = rc.top;
    }
    m_shakeStep = 0;
    ::SetTimer(m_hWnd, kShakeTimerId, kShakeStepMs, NULL);
}

void CBalloonChatWindow::AppendSystemTip(const CString& text)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }
    // 复用 MakeTextMessage 填默认值，再改成居中提示类型（kMsgTime）。
    balloonchatwnd::ChatMessage tip = balloonchatwnd::MakeTextMessage(false, text);
    tip.kind = balloonchatwnd::kMsgTime;
    list->AppendMessage(tip);
}

void CBalloonChatWindow::OpenEmojiPicker()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kIdTbEmoji);
    if (btn == NULL)
    {
        return;
    }

    // 取表情按钮屏幕矩形作锚点（控件 rect 是 host 客户区坐标，转屏幕）。
    RECT rc = btn->GetRect();
    POINT tl = { rc.left, rc.top };
    POINT br = { rc.right, rc.bottom };
    ::ClientToScreen(m_hWnd, &tl);
    ::ClientToScreen(m_hWnd, &br);
    RECT anchor = { tl.x, tl.y, br.x, br.y };

    // 弹 Bin/Face 图片表情浮层（已取代旧的 Unicode emoji 浮层 m_emojiPopup）。
    m_facePopup.SetPickCallback(&CBalloonChatWindow::OnFacePicked, this);
    m_facePopup.OpenAt(anchor, m_hWnd);
}

void CBalloonChatWindow::OnEmojiPicked(void* userdata, const wchar_t* emoji)
{
    CBalloonChatWindow* self = (CBalloonChatWindow*)userdata;
    if (self != NULL)
    {
        self->InsertEmojiToInput(emoji);
    }
}

void CBalloonChatWindow::OnFacePicked(void* userdata, int faceId, const wchar_t* facePath)
{
    CBalloonChatWindow* self = (CBalloonChatWindow*)userdata;
    if (self != NULL)
    {
        self->InsertFaceToInput(faceId, facePath);
    }
}

void CBalloonChatWindow::OpenFontPanel()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kIdTbFont);
    if (btn == NULL)
    {
        return;
    }

    // 取「AF」按钮屏幕矩形作锚点（控件 rect 是 host 客户区坐标，转屏幕）。
    RECT rc = btn->GetRect();
    POINT tl = { rc.left, rc.top };
    POINT br = { rc.right, rc.bottom };
    ::ClientToScreen(m_hWnd, &tl);
    ::ClientToScreen(m_hWnd, &br);
    RECT anchor = { tl.x, tl.y, br.x, br.y };

    m_fontPopup.SetFont(m_currentFont);   // 回显当前设置
    m_fontPopup.OpenAt(anchor, m_hWnd);
}

void CBalloonChatWindow::OnFontChanged(void* userdata, const net::TextFont& font)
{
    CBalloonChatWindow* self = (CBalloonChatWindow*)userdata;
    if (self == NULL)
    {
        return;
    }
    self->m_currentFont = font;

    // 输入框所见即所得：把字体应用到输入框。
    balloonwjui::DuiControl* content = self->GetClientContent();
    if (content != NULL)
    {
        balloonwjui::DuiRichEditHost* input = dynamic_cast<balloonwjui::DuiRichEditHost*>(
            content->FindCtrlById(balloonchatwnd::kIdInputEdit));
        // 应用新字体：RichEdit 不接管 HFONT 所有权，先换新句柄再释放上一只，避免泄漏。
        HFONT hNewFont = balloonchatwnd::ApplyChatFontToEdit(input, font);
        if (hNewFont != NULL)
        {
            if (self->m_hInputFont != NULL)
            {
                ::DeleteObject(self->m_hInputFont);
            }
            self->m_hInputFont = hNewFont;
        }
    }

    // 全局记忆：写回 flamingo.ini（下次打开聊天窗沿用）。
    balloonchatwnd::SaveChatFont(font);
}

void CBalloonChatWindow::InsertEmojiToInput(const wchar_t* emoji)
{
    if (emoji == NULL || emoji[0] == L'\0')
    {
        return;
    }
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiRichEditHost* input = dynamic_cast<balloonwjui::DuiRichEditHost*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (input == NULL)
    {
        return;
    }
    // 追加到输入框末尾（面板保持打开便于连续选；不抢焦点，避免面板因失焦关闭）。
    CString cur = input->GetText();
    cur += emoji;
    input->SetText(cur);
}

void CBalloonChatWindow::InsertFaceToInput(int faceId, const wchar_t* facePath)
{
    if (facePath == NULL || facePath[0] == L'\0')
    {
        return;
    }
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiRichEditHost* input = dynamic_cast<balloonwjui::DuiRichEditHost*>(
        content->FindCtrlById(balloonchatwnd::kIdInputEdit));
    if (input == NULL)
    {
        return;
    }
    // 以"带 faceId 标记的内联图"插入当前光标处（所见即所得）。标记按 ChatFaceTag 约定
    // 编码进 REOBJECT.dwUser，发送时据此把表情与截图区分、内联进文本消息。面板保持
    // 打开便于连续选，故不在此抢焦点。
    input->InsertTaggedImage(facePath,
                             balloonchatwnd::FaceTagFromId(faceId),
                             balloonchatwnd::kFaceDisplayPx);
}

void CBalloonChatWindow::OnRecvMessage(const net::MessageNotify& notify)
{
    // 对方的消息都收到了，"正在输入…"就该收起 —— 不必干等它过期，也不必指望对端
    // 一定会补发 state=0（那包不可靠）。放在函数最前面，抖动等提前 return 的分支也覆盖到。
    if (m_peerTyping.Clear())
    {
        RefreshTypingHeader();
    }

    // 窗口抖动：先于"非文本即跳过"处理（抖动无正文）。居中提示 + 晃动本窗口，
    // 并对短时间内的连发抖动做节流，避免被刷屏式晃动。
    if (notify.m_msgtype == kMsgTypeShake)
    {
        // 接收侧节流：同一来源两次抖动间隔不足 kShakeMinIntervalMs（5 秒）则
        // 静默忽略，避免被对端连发刷屏式晃动。被拦截不刷新计时。
        if (!m_recvShakeLimiter.TryPass(::GetTickCount64(), kShakeMinIntervalMs))
        {
            LOG_INFO("单聊窗收到抖动但在节流窗内，忽略: from=%lld.",
                     (long long)notify.m_from);
            return;
        }
        // 接收方窗口若未显示 / 最小化 / 未置顶，先弹到前台再抖动，确保对方能看到。
        BringChatWindowToFront();
        AppendSystemTip(_T("对方向你发送了窗口抖动"));
        ShakeWindow();
        LOG_INFO("单聊窗收到窗口抖动: from=%lld, msgid=%lld.",
                 (long long)notify.m_from, (long long)notify.m_msgid);
        return;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 图片消息：插一条"下载中"气泡（对方发，fromMe=false），并触发原图下载；
    // 下载完成后经 FMG_MSG_V2_IMAGE_DOWNLOAD_RESULT 回投、按 msgid 回填渲染。
    if (notify.m_msgtype == kMsgTypeImage)
    {
        // 去重：若该图片（同 msgid）已在列表里（如历史已先渲染），不再重复追加，避免错位 / 重复。
        if (list->HasImageMsgid((long long)notify.m_msgid))
        {
            LOG_INFO("单聊窗收到图片已存在，跳过重复追加: msgid=%lld.", (long long)notify.m_msgid);
            return;
        }
        RenderIncomingImage(list, false, notify);
        LOG_INFO("单聊窗收到图片消息: from=%lld, msgid=%lld, fileid=%s.",
                 (long long)notify.m_from, (long long)notify.m_msgid,
                 notify.m_imageFileId.c_str());
        return;
    }

    // 文件消息：插一条气泡（缓存命中=完成态、否则"下载中"并触发下载），按 msgid 去重。
    if (notify.m_msgtype == kMsgTypeFile)
    {
        if (list->HasFileMsgid((long long)notify.m_msgid))
        {
            LOG_INFO("单聊窗收到文件已存在，跳过重复追加: msgid=%lld.", (long long)notify.m_msgid);
            return;
        }
        RenderIncomingFile(list, false, notify);
        LOG_INFO("单聊窗收到文件消息: from=%lld, msgid=%lld, fileid=%s.",
                 (long long)notify.m_from, (long long)notify.m_msgid,
                 notify.m_fileFileId.c_str());
        return;
    }

    // 其余非文本类型（解析层未填 m_text）暂不渲染（后续轮次补）。
    if (notify.m_text.empty())
    {
        LOG_INFO("单聊窗收到非文本消息，暂不渲染: from=%lld, msgtype=%d.",
                 (long long)notify.m_from, notify.m_msgtype);
        return;
    }

    // 渲染为对方气泡（fromMe=false），用对端真实头像；按该条消息携带的字体渲染。
    CString text = Utf8ToCString(notify.m_text);
    list->AppendMessage(MakeRealTextMessage(false, text, notify.m_font, notify.m_msgid,
                                            notify.m_timestamp, std::string(), notify.m_quote));
    LOG_INFO("单聊窗渲染收到的消息: from=%lld, msgid=%lld.",
             (long long)notify.m_from, (long long)notify.m_msgid);
}

void CBalloonChatWindow::RenderIncomingImage(balloonchatwnd::ChatMessageList* list,
                                             bool fromMe, const net::MessageNotify& notify)
{
    if (list == NULL)
    {
        return;
    }

    // 按消息里的真实宽高算气泡内展示尺寸（缺失则用占位尺寸）。
    int dispW = 0;
    int dispH = 0;
    ComputeImageDisplaySize(notify.m_imageWidth, notify.m_imageHeight, dispW, dispH);

    // 当前登录用户 userid：缓存按账号隔离（Users\<userId>\ChatImage\），故先取好。
    UINT selfId = 0;
    if (m_lpFMGClient != NULL)
    {
        selfId = m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID;
    }

    // 缓存命中检查：图片按内容 MD5 缓存在本地账号目录，若已存在则直接渲染原图、
    // 不再触发下载——否则每次渲染历史/收到推送都会把同一张图重下一遍，把 imgserver
    // 刷爆、还把发送任务堵在串行队列后面。
    // 下载侧用"临时文件写完再改名"原子落地，故 <md5> 一旦存在即为完整文件；这里再
    // 用文件实际大小与消息里的期望大小比对兜底（防住历史遗留 / 非原子写入的半截、
    // 空文件被误判命中），不读盘、仅取属性，开销极低。期望大小缺失(<=0)时退回只判存在。
    std::wstring cachePath = net::ImageTransfer::CacheFilePathForMd5(
        net::CurrentUserAccount(), notify.m_imageMd5);
    bool cached = false;
    if (!cachePath.empty())
    {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (::GetFileAttributesExW(cachePath.c_str(), GetFileExInfoStandard, &fad)
            && (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            if (notify.m_imageSize <= 0)
            {
                cached = true;   //无期望大小可比，退回只判存在
            }
            else
            {
                ULONGLONG actualBytes =
                    (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                cached = (actualBytes == static_cast<ULONGLONG>(notify.m_imageSize));
                if (!cached)
                {
                    LOG_INFO("单聊窗图片缓存大小不符(重下): msgid=%lld, 实际=%llu, 期望=%lld.",
                             (long long)notify.m_msgid, (unsigned long long)actualBytes,
                             (long long)notify.m_imageSize);
                }
            }
        }
    }

    balloonchatwnd::ChatMessage msg;
    if (cached)
    {
        //缓存命中：正常态图片气泡，直接用本地缓存路径渲染。
        msg = balloonchatwnd::MakeImageMessage(
            fromMe, CString(cachePath.c_str()), dispW, dispH, std::string(), 0 /*正常*/);
        msg.msgid = (long long)notify.m_msgid;
    }
    else
    {
        //未命中：插"下载中"气泡，记 msgid 供下载结果回填定位本行。
        msg = balloonchatwnd::MakeDownloadingImageMessage(
            fromMe, dispW, dispH, (long long)notify.m_msgid);
    }
    // 记下原图的 fileid / md5：本条消息将来被别人引用时，要把它们写进 content.quote，
    // 收方才能在引用块里画出缩略图（按 md5 找本地缓存 / 按 fileid 下载）。
    msg.imageFileId = Utf8ToCString(notify.m_imageFileId);
    msg.imageMd5    = Utf8ToCString(notify.m_imageMd5);

    // 头像：自己发的用本端头像、否则对端头像（字形 / 真图，与文本气泡同口径）。
    FillSenderAvatar(msg, fromMe);
    list->AppendMessage(msg);

    if (cached)
    {
        LOG_INFO("单聊窗收到图片(缓存命中,免下载): msgid=%lld, md5=%s.",
                 (long long)notify.m_msgid, notify.m_imageMd5.c_str());
        return;
    }

    // 未命中才触发原图下载（fileid 来自消息 content）。下载结果按 msgid 回投，
    // 落地到当前登录账号的缓存目录（selfId）。
    if (!notify.m_imageFileId.empty())
    {
        net::ImageTransfer::GetInstance().EnqueueDownload(
            (int64_t)notify.m_msgid, notify.m_imageFileId,
            notify.m_imageMd5, notify.m_imageSize, net::CurrentUserAccount());
    }
}

bool CBalloonChatWindow::LocalFileUsable(const std::wstring& path, long long expectSize)
{
    if (path.empty())
    {
        return false;
    }
    //仅取文件属性、不读盘，开销极低（一屏几十条气泡逐条查也无感）。
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
    {
        return false;
    }
    if ((fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return false;   //同名目录不能当文件用
    }
    //期望大小缺失（<=0）时退回只判存在；否则大小必须相符——半截的下载残留、或被
    //改写过的同名文件都不该冒充完整文件。
    if (expectSize <= 0)
    {
        return true;
    }
    ULONGLONG actualBytes =
        (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    return actualBytes == static_cast<ULONGLONG>(expectSize);
}

void CBalloonChatWindow::RenderIncomingFile(balloonchatwnd::ChatMessageList* list,
                                            bool fromMe, const net::MessageNotify& notify)
{
    if (list == NULL)
    {
        return;
    }

    // 当前登录用户 userid：下载缓存按账号隔离（Users\<userId>\ChatFile\），先取好。
    UINT selfId = 0;
    if (m_lpFMGClient != NULL)
    {
        selfId = m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID;
    }

    CString fileName = Utf8ToCString(notify.m_fileName);

    // 本地落点有两处可能：一是登记表里记下的路径（本端发出的源文件、或此前下载的落地
    // 位置），二是按内容 MD5 推导的下载缓存路径。两处都可能失效（文件被移走 / 删除 /
    // 清了缓存），故各自查一次是否真实存在，再由 ResolveLocalFilePath 定优先级。
    std::wstring registeredPath;
    balloonhistory::CFileLocalStore::GetInstance().Query(
        notify.m_fileMd5, notify.m_fileName, registeredPath);

    std::wstring cachePath = net::FileTransfer::CacheFilePathFor(
        net::CurrentUserAccount(), notify.m_fileMd5, notify.m_fileName);

    const bool registeredExists = LocalFileUsable(registeredPath, notify.m_fileSize);
    const bool cacheExists      = LocalFileUsable(cachePath, notify.m_fileSize);

    std::wstring localPath = balloonchatwnd::ResolveLocalFilePath(
        registeredPath, registeredExists, cachePath, cacheExists);
    const bool cached = !localPath.empty();

    balloonchatwnd::ChatMessage msg;
    if (cached)
    {
        //本地已有这份文件：完成态气泡，路径填好（可直接打开 / 打开所在文件夹 / 另存为）。
        msg = balloonchatwnd::MakeFileMessage(
            fromMe, fileName, notify.m_fileSize, CString(localPath.c_str()),
            std::string(), 0 /*完成*/);
        msg.fileMsgid = (long long)notify.m_msgid;
        //完成态气泡也要带上 md5 / fileid：文件日后被删掉、用户点「重新下载」时还要用
        msg.fileFileId = notify.m_fileFileId;
        msg.fileMd5    = notify.m_fileMd5;
    }
    else
    {
        //未命中：插「待接收」气泡（不自动下载），记 msgid / fileid / md5 —— 用户点
        //「接收」时靠它们发起下载，下载结果也按 msgid 回填本行。
        //之所以不再自动下载：一是收不收该由用户决定（大文件尤其）；二是自己发出的文件
        //从没进过下载缓存，翻历史时必然"未命中"，旧逻辑会把刚上传的文件又整个拉回来。
        msg = balloonchatwnd::MakeIncomingFileMessage(
            fromMe, fileName, notify.m_fileSize, (long long)notify.m_msgid,
            notify.m_fileFileId, notify.m_fileMd5, 6 /*待接收*/);
    }
    // 本端发出的文件：带上对方的收取状态。服务端只对"拉取者自己发出的文件消息"回填
    // 该字段，所以这里能拿到值就说明是自己发的那条——离线期间对方的接收 / 拒绝正是
    // 靠它补看到的（实时的状态变化另经 cmd_file_transfer_ack 推送）。
    msg.filePeerState = notify.m_fileAckState;

    // 头像：自己发的用本端头像、否则对端头像（字形 / 真图，与文本气泡同口径）。
    FillSenderAvatar(msg, fromMe);
    list->AppendMessage(msg);

    if (cached)
    {
        LOG_INFO("单聊窗收到文件(缓存命中,免下载): msgid=%lld.", (long long)notify.m_msgid);
    }
    else
    {
        LOG_INFO("单聊窗收到文件(待用户接收): msgid=%lld, fromMe=%d.",
                 (long long)notify.m_msgid, fromMe ? 1 : 0);
    }
}

void CBalloonChatWindow::OnImageDownloaded(long long msgid, const std::wstring& localPath)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list != NULL)
    {
        list->SetImageDownloadedByMsgid(msgid, CString(localPath.c_str()));
    }
}

void CBalloonChatWindow::OnImageDownloadFailed(long long msgid)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list != NULL)
    {
        list->MarkImageDownloadFailedByMsgid(msgid);
    }
}

void CBalloonChatWindow::OnFileDownloadProgress(long long msgid, long long received, long long total)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }
    int percent = 0;
    if (total > 0)
    {
        percent = (int)(received * 100 / total);
    }
    list->UpdateFileDownloadProgress(msgid, percent);
}

void CBalloonChatWindow::OnFileDownloaded(long long msgid, const std::wstring& localPath)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list != NULL)
    {
        list->SetFileDownloadedByMsgid(msgid, CString(localPath.c_str()));
    }

    // 文件已完整落地并校验通过，这时才告诉发送方"收到了" —— 中途失败 / 取消都不会
    // 走到这里，所以对方看到的「已接收」是准的。
    ReportFileAck(msgid, kFileAckReceived);
}

void CBalloonChatWindow::OnFilePeerAck(long long msgid, int peerState)
{
    balloonchatwnd::ChatMessageList* list = GetMessageList();
    if (list == NULL)
    {
        LOG_ERROR("单聊窗更新对方收取状态失败(消息区未就绪): msgid=%lld.", (long long)msgid);
        return;
    }
    //命中与否都记一笔：没命中说明本窗口没有这条消息（别的会话、或尚未加载进视图），
    //与"命中了但没刷新"是两类完全不同的问题，日志里必须能分开。
    const bool hit = list->SetFilePeerStateByMsgid(msgid, peerState);
    LOG_INFO("单聊窗更新对方收取状态: msgid=%lld, state=%d, 命中气泡=%d.",
             (long long)msgid, peerState, hit ? 1 : 0);
}

void CBalloonChatWindow::OnFileDownloadFailed(long long msgid, bool cancelled)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list != NULL)
    {
        if (cancelled)
        {
            //用户主动取消下载：置「已取消」态，不按失败处理
            list->MarkFileCancelledByMsgid(msgid);
        }
        else
        {
            list->MarkFileDownloadFailedByMsgid(msgid);
        }
    }
}

void CBalloonChatWindow::LocateMessage(long long msgid)
{
    m_pendingLocateMsgid = 0;
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kIdMessageArea))
        : NULL;
    if (list == NULL || !list->ScrollToMsgid(msgid))
    {
        //此刻没找到（历史还在途 / 该消息较早未加载）：记为待定，等历史渲染后补定位。
        m_pendingLocateMsgid = msgid;
    }
}

void CBalloonChatWindow::OnHistoryMessages(const net::SyncMessagesResult& result)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 本次应答是首屏还是"往上翻的一页"？服务端应答里不回显 before，只能靠本端
    // 记的在途标志区分 —— 只有 OnChatLoadOlder 发出的翻页请求会把它置真。
    const bool isOlderPage = m_paging.m_loading;

    if (isOlderPage)
    {
        // 翻页：绝不能清空。开"头部插入批次"，下面照常按时间正序渲染。
        list->BeginPrependBatch();
    }
    else
    {
        // 首屏是"最近 N 条"完整快照：渲染前先清空列表，避免与窗口刚创建时（拉历史
        // 仍在途中）收到推送、被 OnRecvMessage 乐观追加进来的那几行叠加，导致这些
        // live 行卡在最前面 / 重复。清空后按历史时间正序重渲，位置即正确。
        list->ClearAllMessages();
    }

    // 当前登录用户 userid，用于区分自己发的（右侧气泡）与对方发的（左侧气泡）。
    UINT selfId = (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;

    // 服务端以 msgid 升序（时间正序、最旧在前）返回（见 MessageManager.cpp 翻历史
    // 模式把 DESC 查询反转为升序）。正序遍历即得"最旧在上、最新在下"，逐条追加。
    for (size_t k = 0; k < result.m_messages.size(); ++k)
    {
        const net::MessageNotify& m = result.m_messages[k];
        bool fromMe = (selfId != 0 && (UINT)m.m_from == selfId);

        // 图片消息：插"下载中"气泡并触发下载（历史里自己发的图也需下载，本端无原图）。
        if (m.m_msgtype == kMsgTypeImage)
        {
            RenderIncomingImage(list, fromMe, m);
            continue;
        }

        // 文件消息：缓存命中=完成态、否则"下载中"并触发下载（历史里自己发的也按缓存判定）。
        if (m.m_msgtype == kMsgTypeFile)
        {
            RenderIncomingFile(list, fromMe, m);
            continue;
        }

        // 文本取正文；其余非文本（文件/语音等，m_text 空）用占位文字渲染成文本气泡，
        // 避免历史出现空洞。
        CString text;
        if (!m.m_text.empty())
        {
            text = Utf8ToCString(m.m_text);
        }
        else
        {
            text = Utf8ToCString(net::MessageProtocolClient::digestForMsgType(m.m_msgtype, m.m_text));
        }
        list->AppendMessage(MakeRealTextMessage(fromMe, text, m.m_font, m.m_msgid,
                                                m.m_timestamp, std::string(), m.m_quote));
    }
    // 本批最早那条的 msgid，作为下一页的 before 锚点（服务端按时间正序返回）。
    const long long oldestInBatch = result.m_messages.empty()
                                  ? 0
                                  : (long long)result.m_messages[0].m_msgid;

    if (isOlderPage)
    {
        list->EndPrependBatch();
        balloonchatwnd::OnOlderPageLoaded(m_paging, oldestInBatch, result.m_hasmore);
        LOG_INFO("单聊窗向上翻页已渲染: peer=%u, 本页 %u 条, 最早 msgid=%lld, hasmore=%d.",
                 m_peerUserId, (uint32_t)result.m_messages.size(),
                 (long long)oldestInBatch, result.m_hasmore ? 1 : 0);
        return;   // 翻页不滚到底
    }

    balloonchatwnd::OnFirstPageLoaded(m_paging, oldestInBatch, result.m_hasmore);
    list->ScrollToBottom();
    // 历史已渲染：若有从历史窗"定位"过来、当时消息还没加载的待办，现在补定位，成功则清。
    if (m_pendingLocateMsgid != 0 && list->ScrollToMsgid(m_pendingLocateMsgid))
    {
        m_pendingLocateMsgid = 0;
    }
    // 一并记下首尾 msgid：服务端约定按 msgid 升序（最旧在前）返回，本函数据此正序
    // 渲染。正常应有 first < last；两边的顺序约定一旦对不上，看这一行即可发现
    // （群聊窗曾因按"最新在前"倒序遍历，把整个历史列表颠倒过来）。
    long long firstMsgid = 0;
    long long lastMsgid  = 0;
    if (!result.m_messages.empty())
    {
        firstMsgid = (long long)result.m_messages.front().m_msgid;
        lastMsgid  = (long long)result.m_messages.back().m_msgid;
    }
    LOG_INFO("单聊窗渲染历史消息: peer=%u, count=%u, first.msgid=%lld, last.msgid=%lld.",
             m_peerUserId, (uint32_t)result.m_messages.size(),
             firstMsgid, lastMsgid);
}

LRESULT CBalloonChatWindow::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/,
                                   LPARAM /*lParam*/, BOOL& bHandled)
{
    // 不消费 WM_SIZE —— 仍交 DuiFrameWindow 链缩放客户区与后备缓冲。
    // 投递一个自定义消息：等 WM_SIZE 全程走完、客户区尺寸更新后，再强制
    // 重排客户区控件树。
    if (IsWindow())
    {
        ::PostMessage(m_hWnd, kRelayoutMsg, 0, 0);
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonChatWindow::OnRelayout(UINT /*uMsg*/, WPARAM /*wParam*/,
                                       LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }

    // 把输入区块稳定在用户选定（或默认）的高度：窗口缩放时让消息区吃掉高度
    // 变化、输入区高度基本不变 —— 与改造前"输入区固定、消息区撑满"的手感一致。
    // 此时 DuiHost::OnSize 已按新尺寸把含 splitter 的客户区树重排过，故直接读
    // splitter 现矩形算分割点；SetSplitPx 会按 splitter 自身 min-size 夹取并重排
    // 其两个 pane。须放在下面消息区 ForceLayout 之前 —— 否则消息区按旧矩形重排。
    balloonwjui::DuiSplitter* sp = dynamic_cast<balloonwjui::DuiSplitter*>(
        content->FindCtrlById(balloonchatwnd::kIdInputSplitter));
    if (sp != NULL)
    {
        RECT rcSp = sp->GetRect();
        int track = (rcSp.bottom - rcSp.top) - sp->GetBarThickness();
        if (track > 0)
        {
            sp->SetSplitPx(track - balloonchatwnd::GetRememberedChatInputHeightPx());
        }
    }

    // 窗口缩放时 DuiHost::OnSize 已按新尺寸把整棵客户区树（含消息区）重排
    // 过了（m_root == frame 的 skeleton，客户区是其 Weight(1) 子节点），故
    // 此处不再重复整树 ForceLayout —— 那是每步缩放的冗余重排。仅对消息区
    // 单独强制一次重排，作为"气泡按新高度重新裁剪、不溢出到输入区"的定向
    // 保险（代价极低：折行结果已被 ChatBubble 的宽度缓存命中）。
    balloonwjui::DuiControl* msgArea =
        content->FindCtrlById(balloonchatwnd::kIdMessageArea);
    if (msgArea != NULL)
    {
        msgArea->ForceLayout(msgArea->GetRect());
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
    return 0;
}
