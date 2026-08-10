/**
 *  BalloonGroupChatWindow.cpp
 *
 *  新版群聊窗口实现。G-P0：骨架 —— 以 DuiFrameWindow 形式创建 / 显示一个
 *  群聊窗，客户区暂为占位文字。真正的头部 / 消息区 / 输入区 / 边栏由后续
 *  G-P1 阶段的 GroupChatWindowLayout 构建。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#include "stdafx.h"
#include "BalloonGroupChatWindow.h"

#include <memory>

#include "../CustomMsgDef.h"          // WM_CLOSE_MAINDLG
#include "../UserSessionData.h"       // FMG_MSG_CONV_SEND_BUMP / ConvSendBumpData
#include "../IULog.h"                 // LOG_INFO
#include "../EncodeUtil.h"            // UTF-8 ↔ Unicode
#include "../FlamingoClient.h"        // CFlamingoClient::m_UserMgr（取自己 userid）
#include "GroupChatWindowLayout.h"    // BuildGroupChatWindowBody / GroupCtrlId
#include "ChatWindowLayout.h"         // RememberChatSplitFraction（单/群共用分割记忆）
#include "ChatImageCache.h"           // 图片后台解码缓存：注册监听 + 就绪重绘
#include "../../balloonui/balloonui/Controls/Layout/DuiSplitter.h"  // 拖动完成后算回分割比例
#include "ChatMessageList.h"          // ChatMessageList::AppendMessage
#include "ChatMockData.h"             // ChatMessage / MakeTextMessage / MakeGroupAutoReply
#include "ChatFontUtil.h"             // 当前聊天字体的载入/保存/应用到输入框
#include "GroupMentionMenu.h"         // MentionMenuId / MentionActionData（气泡里 @某人 右键菜单的分发负载）
#include "ChatBannerLabel.h"          // 顶部离线 / 正在连接提示带（带底色的 DuiLabel 子类）
#include "GroupMemberOrder.h"         // OrderMemberIndicesByRole（成员侧栏：群主 → 管理员 → 普通成员）
#include "ChatQuoteModel.h"           // FillQuoteForRender（引用回复的统一入口）
#include "ChatMessageMenu.h"          // ChatImageActionData / ChatMessageMenuId（图片气泡右键动作）
#include "ChatImageSaveAs.h"          // SaveChatImageAs（图片另存为，与单聊窗共用）
#include "GroupSideTab.h"             // GroupSideTab::SetActive
#include "SideTabText.h"              // MakeSideTabText（边栏 Tab 的"标题 + 真实数量"文案）
#include "GroupMemberList.h"          // 成员侧栏容器
#include "GroupMemberItem.h"          // 成员行
#include "GroupFileList.h"            // 群文件列表堆叠容器（DuiScrollView 内容）
#include "GroupFileItem.h"            // 群文件行
#include "FileBubbleFormat.h"         // FormatFileSize / FormatRelativeDayTime（文件行说明文字）
#include "../../balloonui/balloonui/Controls/Window/DuiScrollBar.h"  // DuiScrollView（文件列表滚动视口）
#include "../../balloonui/balloonui/Controls/Basic/DuiButton.h"      // 分类筛选 chips 选中态切换
#include "ChatTitleIcon.h"            // MakeRoundTitleIcon / IconFromBitmap（共用标题图标）
#include "../BalloonMain/MainPanelAvatar.h"  // MakeMonogramAvatar（任务栏图标）
#include "../BalloonMain/AvatarImageUtil.h"   // MakeRoundedAvatarFromFile（群头像真图的任务栏图标）
#include "../BalloonMain/AvatarSpec.h"        // DecodeAvatar：群成员/自己气泡按 (facetype,customface) 取字形/底色
#include "../BalloonImageViewer/BalloonImageViewerWindow.h"  // 看图窗（双击图片放大）
#include "../BalloonHistory/V2MsgQuery.h"      // HistoryTypeFilter：打开消息记录窗时预选哪个类型胶囊
#include "../net/IUSocket.h"          // CIUSocket::SendGroupTextMessage / RequestGroupMembers
#include "../BalloonMain/OnlineCapability.h"   // 离线门禁：连接态、提示条文案、被拦提示
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"   // 顶部离线提示条
#include "../../balloonui/balloonui/Controls/Layout/DuiLayout.h"  // DuiLayout::SetHint（引用条人名段按内容定宽）

// 顶部提示带配色，与单聊窗同一套值（见 BalloonChatWindow.cpp 的同名常量与说明）：
// 离线用琥珀色系（需要用户处理），正在连接用中性灰（仅告知）。每种三个色 ——
// 整条底色、文字色、下边缘分隔线色。
static const COLORREF kGroupOfflineBannerBgColor     = RGB(0xFF, 0xF4, 0xE5);
static const COLORREF kGroupOfflineBannerColor       = RGB(0xB4, 0x53, 0x09);
static const COLORREF kGroupOfflineBannerBorderColor = RGB(0xF3, 0xE0, 0xC0);

static const COLORREF kGroupConnectingBannerBgColor     = RGB(0xF5, 0xF5, 0xF6);
static const COLORREF kGroupConnectingBannerColor       = RGB(0x6B, 0x72, 0x80);
static const COLORREF kGroupConnectingBannerBorderColor = RGB(0xE5, 0xE7, 0xEB);
#include "../net/MessageProtocolClient.h"  // net::MessageNotify / digestForMsgType
#include "../net/GroupProtocolClient.h"    // net::GroupMembersResult
#include "../net/Protocol.h"               // group_member_role（群主/管理员/成员角色枚举，全局命名空间）
#include "../net/ImageTransfer.h"          // net::ImageTransfer（发图工作线程）
#include "../net/FileTransfer.h"           // net::FileTransfer（群文件上传/下载）+ GroupFileUpload/DownloadResult
#include "ChatAvatarResolve.h"             // ResolveAvatarRender（气泡 / 成员真图头像呈现解析）
#include "GroupChatHeaderInfo.h"           // 头部群名 / 人数 / 公告摘要行
//公告卡底部那行时间与群管理窗的历史公告列表同一个口径，复用同一个纯函数，
//不在这里另写一份（两处显示不一致比多一个依赖更糟）。
#include "../BalloonGroupManage/GroupManageMapping.h"   // FormatAnnouncementTime
#include <time.h>
#include <gdiplus.h>                       // 读图片真实宽高以算展示尺寸
#pragma comment(lib, "gdiplus.lib")
#include "../../balloonui/balloonui/Controls/Input/DuiEditHost.h"      // 成员搜索框
#include "../../balloonui/balloonui/Controls/Input/DuiRichEditHost.h"  // 消息输入框（富文本）
#include "../BalloonScreenshot/ScreenCaptureOverlay.h"  // 截图浮层（截图按钮调起）
#include "ChatRichContentSplit.h"     // 输入框图文混排的有序拆分（发送用）
#include "ChatFaceTag.h"              // 内联表情的 dwUser 标记编码 + 显示尺寸
#include "../BalloonChat/BalloonChatMsgFormat.h"  // BodyToDigest（会话摘要里表情→[表情]）
#include "ChatInputScrollBar.h"       // 输入框与接收区一致的细悬浮滚动条
#include "ChatInputKeyFilter.h"       // 输入框 Enter 发送 / Ctrl+Enter 换行
#include "ChatSendMode.h"             // 发送键模式（Enter / Ctrl+Enter）
#include "ChatSendButton.h"           // ChatSendButton::DUIN_SENDBTN_DROPDOWN
#include "../../balloonui/balloonui/Controls/List/DuiMenu.h"      // 发送模式切换菜单
#include "../../balloonui/balloonui/Controls/Basic/DuiLabel.h"    // 提示文字 SetText
#include "../../balloonui/balloonui/Controls/Feedback/DuiToolTip.h"
#include <objbase.h>                  // CoCreateGuid（生成 clientmsgid）
#include <shellapi.h>                 // ShellExecuteW（群文件下载完成后打开文件）
#pragma comment(lib, "shell32.lib")
#include <vector>
#include <string>
#include <ctime>                      // time()（群文件相对时间的当下基准）

#pragma comment(lib, "ole32.lib")     // CoCreateGuid

// ---- 文件作用域辅助函数的前置声明 ---------------------------------------

// 判定输入框里光标前一个字符是否为空白。定义在本文件靠后的位置，但插入 @提醒
// 的代码在它前面就要用，故在此前置声明。
//   hEdit：输入框（RichEdit）句柄。
//   pos：  光标位置（字符下标）。
// @return pos 为 0，或前一个字符是空白时返回 true。
static bool PrevCharIsWhitespace(HWND hEdit, int pos);

// ---- 文件作用域常量 -----------------------------------------------------

// 群聊窗固定客户区尺寸（逻辑像素）—— 比单聊窗宽，右侧多一条 240px 边栏。
static const int kGroupChatWindowWidth  = 880;
static const int kGroupChatWindowHeight = 620;

// 群聊窗允许缩放到的最小尺寸。
static const int kGroupChatWindowMinWidth  = 620;
static const int kGroupChatWindowMinHeight = 440;

// 标题栏高度（逻辑像素）—— 常规标题栏，承载 min/max/close。
static const int kGroupChatWindowTitleBarHeight = 32;

// mock 自动回复定时器 id 与延时（毫秒）—— 发出消息后过一会儿模拟群成员回话。
static const UINT_PTR kReplyTimerId = 0xC101;
static const UINT     kReplyDelayMs = 1400;

// 消息类型常量，与 net::message_type（net/Protocol.h）对齐。
static const int kMsgTypeText  = 1;   // message_type_text
static const int kMsgTypeImage = 2;   // message_type_image

// CString(UTF-16) → UTF-8 std::string。空串安全。
static std::string CStringToUtf8(const CString& text)
{
    int wlen = text.GetLength();
    if (wlen <= 0)
    {
        return std::string();
    }
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
    std::vector<wchar_t> buf(utf8.size() + 1, 0);
    EncodeUtil::Utf8ToUnicode(utf8.c_str(), buf.data(), (int)buf.size());
    return CString(buf.data());
}

// 群文件面板每页条数（逻辑常量）。设计稿呈现分页（共 N · 第 x/y 页），此处取 6：
// 一页约填满边栏可视高度、且 14 条样本正好分 3 页与设计稿一致。后续如需更大页可改此值。
static const int kGroupFilePageSize = 6;

// 「拉全量」每次向服务端请求的页大小。服务端 cmd_get_group_files 单页上限 50
// （GROUP_FILE_PAGE_MAX），取满以最少往返拉全。
static const int kGroupFileFetchPageSize = 50;

// 公告边栏一次拉几条。边栏一次只显示一条，故取 1：翻到哪条要哪条，公告有
// 几千条也不受服务端单页上限影响，本地也只留一条。
static const int kNoticeFetchPageSize = 1;

// 当前时间，Unix毫秒。公告卡底部那行"发布人 · 时间"按这个口径算相对日期。
static int64_t NowMillisecondsForNotice()
{
    const int64_t kMillisPerSecond = 1000;
    return static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
}

// 构造一批 mock 群文件条目，仅用于调试入口（--groupchatwindow，无真实群）目检面板。
// 覆盖各扩展名（决定彩色角标 + 分类筛选）与各上传时间（决定相对时间显示：今天/昨天/
// 周X/日期）。真实群的文件由 cmd_get_group_files 应答填充，不走本函数。
static std::vector<net::GroupFileEntry> MockGroupFileEntries()
{
    // 以当前时刻为基准造各种"多久以前"的上传时间。
    long long nowMs = (long long)time(NULL) * 1000LL;
    const long long kHour = 3600LL * 1000;
    const long long kDay  = 24 * kHour;

    struct Spec
    {
        const char* entryid;
        const char* name;
        const char* ext;
        const char* uploader;
        long long   size;
        long long   uploadMs;
    };
    Spec specs[] =
    {
        { "101", "v3.2-发布计划.xlsx",   "xlsx", "谢知行", 131072LL,   nowMs - 2 * kHour  },
        { "102", "登录页 final.fig",      "fig",  "我",     13002342LL, nowMs - 5 * kHour  },
        { "103", "群文件功能 PRD.docx",   "docx", "方亦初", 90112LL,    nowMs - 1 * kDay   },
        { "104", "品牌色规范.pdf",        "pdf",  "江屿白", 1887436LL,  nowMs - 3 * kDay   },
        { "105", "API 接口文档 v3.docx",  "docx", "宋微之", 350208LL,   nowMs - 3 * kDay   },
        { "106", "用户研究报告.pdf",      "pdf",  "许清欢", 5872025LL,  nowMs - 20 * kDay  },
        { "107", "原型交互稿.sketch",     "sketch","林知夏", 9646899LL,  nowMs - 24 * kDay  },
        { "108", "测试用例清单.xlsx",     "xlsx", "周慎",   65536LL,    nowMs - 26 * kDay  },
        { "109", "数据导出.csv",          "csv",  "我",     20480LL,    nowMs - 30 * kDay  },
        { "110", "发布会演示.pptx",       "pptx", "高叙",   4194304LL,  nowMs - 33 * kDay  },
        { "111", "首页大图.png",          "png",  "苏晚",   2359296LL,  nowMs - 40 * kDay  },
        { "112", "源码归档.zip",          "zip",  "陈聿",   41943040LL, nowMs - 60 * kDay  },
        { "113", "需求评审纪要.txt",      "txt",  "我",     8192LL,     nowMs - 90 * kDay  },
        { "114", "旧版设计稿.psd",        "psd",  "黎照",   31457280LL, nowMs - 400 * kDay },
    };

    std::vector<net::GroupFileEntry> out;
    const int kCount = (int)(sizeof(specs) / sizeof(specs[0]));
    for (int i = 0; i < kCount; ++i)
    {
        net::GroupFileEntry e;
        e.m_entryid      = specs[i].entryid;
        e.m_fileid       = specs[i].entryid;   // mock：文件ID 借用条目ID
        e.m_name         = specs[i].name;
        e.m_ext          = specs[i].ext;
        e.m_size         = specs[i].size;
        e.m_uploadername = specs[i].uploader;
        e.m_uploadtime   = specs[i].uploadMs;
        out.push_back(e);
    }
    return out;
}

// 在群文件列表（GroupFileList）里按 entryid 找到对应的文件行控件；找不到返回 NULL
// （该条目不在当前页 / 列表未建时）。下载进度回投后据此定位那一行就地更新进度。
static balloonchatwnd::GroupFileItem* FindGroupFileItemByEntryId(
    balloonwjui::DuiControl* content, const CString& entryid)
{
    if (content == NULL || entryid.IsEmpty())
    {
        return NULL;
    }
    balloonchatwnd::GroupFileList* list =
        dynamic_cast<balloonchatwnd::GroupFileList*>(
            content->FindCtrlById(balloonchatwnd::kGIdFileList));
    if (list == NULL)
    {
        return NULL;
    }
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        balloonchatwnd::GroupFileItem* item =
            dynamic_cast<balloonchatwnd::GroupFileItem*>(kids[i].get());
        if (item != NULL && item->EntryId() == entryid)
        {
            return item;
        }
    }
    return NULL;
}

// 在群文件列表里按 uploadId 找到对应的「上传中」行控件；找不到返回 NULL。上传进度
// 回投后据此定位顶部那一行就地更新进度。
static balloonchatwnd::GroupFileItem* FindGroupFileUploadItem(
    balloonwjui::DuiControl* content, int uploadId)
{
    if (content == NULL || uploadId <= 0)
    {
        return NULL;
    }
    balloonchatwnd::GroupFileList* list =
        dynamic_cast<balloonchatwnd::GroupFileList*>(
            content->FindCtrlById(balloonchatwnd::kGIdFileList));
    if (list == NULL)
    {
        return NULL;
    }
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids = list->Children();
    for (size_t i = 0; i < kids.size(); ++i)
    {
        balloonchatwnd::GroupFileItem* item =
            dynamic_cast<balloonchatwnd::GroupFileItem*>(kids[i].get());
        if (item != NULL && item->UploadId() == uploadId)
        {
            return item;
        }
    }
    return NULL;
}

// 按 userid 从固定调色板挑头像底色（与联系人/单聊同口径，保证同一人颜色稳定）。
static COLORREF AvatarColorForUser(int64_t userid)
{
    static const COLORREF kPalette[] =
    {
        RGB(0xFF, 0x7A, 0x45), RGB(0x3A, 0x86, 0xD9), RGB(0x2B, 0xB6, 0x73),
        RGB(0x8B, 0x5C, 0xF6), RGB(0xEF, 0x6C, 0x9A), RGB(0xF5, 0x9E, 0x0B),
        RGB(0x14, 0xB8, 0xA6), RGB(0xE1, 0x5B, 0x4C),
    };
    const int n = (int)(sizeof(kPalette) / sizeof(kPalette[0]));
    int idx = (int)(((userid % n) + n) % n);
    return kPalette[idx];
}

// 把一条"自己发"的群消息气泡的发送者头像填成本端真实头像（字形 / 真图，按本端
// (facetype, customface) 解析；真图未缓存会入队下载，完成后 OnAvatarDownloaded 回刷）。
// 群聊里自己气泡不显示发送者名，故不设 senderName。
static void FillSelfSenderAvatar(CFlamingoClient* client, balloonchatwnd::ChatMessage& msg)
{
    int64_t     selfId = 0;
    int         facetype = 0;
    std::string customface;
    CString     name = _T("我");
    if (client != NULL)
    {
        const CBuddyInfo& me = client->m_UserMgr.m_UserInfo;
        selfId   = (int64_t)me.m_uUserID;
        facetype = (int)me.m_nFace;   // v2 facetype 存在 m_nFace
        customface.assign(me.m_strCustomFaceName.GetString(),
                          (size_t)me.m_strCustomFaceName.GetLength());   // customface UTF-8 字节
        if (!me.m_strNickName.empty())
        {
            name = me.m_strNickName.c_str();
        }
    }
    balloonchatwnd::AvatarRender ar = balloonchatwnd::ResolveAvatarRender(
        selfId, facetype, customface, name, selfId);
    msg.senderUserId          = selfId;
    msg.senderAvatarText      = ar.m_glyph;
    msg.senderAvatarColor     = ar.m_color;
    msg.senderAvatarImagePath = ar.m_imagePath;
}

// 生成一个客户端消息ID（去横杠的 GUID 十六进制串，纯 ASCII）。
static std::string GenClientMsgId()
{
    GUID guid = { 0 };
    if (::CoCreateGuid(&guid) != S_OK)
    {
        return std::string("cmid");
    }
    char buf[40] = { 0 };
    sprintf_s(buf, sizeof(buf), "%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",
              guid.Data1, guid.Data2, guid.Data3,
              guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
              guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return std::string(buf);
}

// 图片气泡内图片的最大展示尺寸（逻辑像素）：超出按比例缩小，小图保持原尺寸。
// 与单聊同口径（见 BalloonChatWindow.cpp）。
static const int kImageDisplayMaxW = 220;
static const int kImageDisplayMaxH = 260;

// 把图片真实宽高按比例缩到不超过 (kImageDisplayMaxW, kImageDisplayMaxH) 的展示尺寸；
// 输出至少 1×1。realW/realH 非正时回退到一个占位方块尺寸。
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

// DuiRichEditHost::EnumContent 的逐段收集回调：把（文本 / 内嵌图+标记）按文档顺序
// 收进 vector，供 BuildOutgoingParts 聚合成待发送部分。与单聊同口径。ctx 为收集目标。
static void CollectGroupInputSegment(bool isImage, LPCTSTR text, DWORD_PTR tag, void* ctx)
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
// 文档顺序拆分图文。与单聊同口径。
static CString GetRichEditPlainText(HWND hRich)
{
    if (hRich == NULL || !::IsWindow(hRich))
    {
        return CString();
    }
    // RichEdit 的 WM_GETTEXT(GetWindowText) 不一定把内联 OLE 对象（截图）还原成占位符
    // 0xFFFC —— 实测返回空格，导致内联截图在拆图文时丢失。改用 EM_GETTEXTEX：可靠地把
    // 每个嵌入对象表示为 WCH_EMBEDDING(0xFFFC)。
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

CBalloonGroupChatWindow::CBalloonGroupChatWindow(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_hMainDlg(NULL)
    , m_replyIndex(0)
    // kConnUiOnline：能开出群聊窗说明此刻本就在线，随后由主面板把真实状态推过来。
    // 初始化顺序须与声明顺序一致（本成员声明在 m_groupId 之前）。
    , m_connUiState(0)
    , m_groupId(0)
    , m_myRole(0)
    , m_hostPanel(NULL)
    , m_pendingLocateMsgid(0)
    , m_pinnedCount(0)            //开窗先按"一条都没钉"起步，提示带隐藏；随后由服务端全集校准
    , m_titleIconBmp(NULL)
    , m_hTaskIcon(NULL)
    , m_groupFacetype(0)          //默认自动头像（群名首字 + 按 groupid 取色）
    , m_hInputFont(NULL)
    , m_fileCategory(balloonchatwnd::kGroupFileCatAll)
    , m_filePage(0)
    , m_fileLoaded(false)
    , m_fileLoading(false)
    , m_mentionAtPos(-1)   // -1 = 当前不处于 @ 输入状态
    , m_noticeIndex(0)
    , m_noticeTotal(0)
    , m_noticeLoaded(false)
    , m_noticeLoading(false)
    , m_noticeReqOffset(-1)   //-1=还没发过拉公告的请求，此时不认领任何应答
{
}

void CBalloonGroupChatWindow::SetGroupId(int64_t groupId)
{
    m_groupId = groupId;
}

void CBalloonGroupChatWindow::SetGroupName(LPCTSTR name)
{
    m_groupName = (name != NULL) ? name : _T("");
    // 已显示则即时刷新标题。
    if (IsWindow() && !m_groupName.IsEmpty())
    {
        SetTitle(m_groupName);

        // 头部左侧那块也显示群名，一并刷新 —— 标题栏与头部各画一遍同一个名字，
        // 只改一处会让两边对不上。
        balloonwjui::DuiControl* content = GetClientContent();
        if (content != NULL)
        {
            balloonchatwnd::GroupChatHeaderInfo* header =
                dynamic_cast<balloonchatwnd::GroupChatHeaderInfo*>(
                    content->FindCtrlById(balloonchatwnd::kGIdHeaderInfo));
            if (header != NULL)
            {
                header->SetGroupName(m_groupName);
            }
        }
    }
}

void CBalloonGroupChatWindow::SetGroupAvatar(int facetype, const std::string& customfaceUtf8)
{
    //值没变就不必重建图标 —— 群资料应答与变更推送都会调过来，多数时候头像并没改。
    if (m_groupFacetype == facetype && m_groupCustomface == customfaceUtf8)
    {
        return;
    }

    m_groupFacetype   = facetype;
    m_groupCustomface = customfaceUtf8;

    //窗口还没建好时只记值，CreateAndShow 里会调 RebuildGroupIcons 用上它。
    if (IsWindow())
    {
        RebuildGroupIcons();
    }
}

void CBalloonGroupChatWindow::RebuildGroupIcons()
{
    if (m_groupId == 0)
    {
        return;   // 调试入口不设图标
    }

    //按群的 (facetype, customface) 解出该画什么：字形 / 自动头像给字形 + 底色；
    //真图命中本地缓存给路径（未命中会入队下载，下好后 OnAvatarDownloaded 回刷）。
    //取色 id 用 groupid，与联系人树、会话列表里的群头像同口径。
    const int64_t selfId = (m_lpFMGClient != NULL)
                           ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    balloonchatwnd::AvatarRender ar = balloonchatwnd::ResolveAvatarRender(
        m_groupId, m_groupFacetype, m_groupCustomface,
        m_groupName.IsEmpty() ? NULL : (LPCTSTR)m_groupName, selfId);
    const bool useImage = !ar.m_imagePath.IsEmpty();

    //标题栏图标：先释放旧位图再重建（48px 生成，标题栏 StretchBlt 缩到 16px，
    //放大生成再缩更平滑）。
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
        //没有真图、或真图解码失败：回退字形。
        m_titleIconBmp = balloonchatwnd::MakeRoundTitleIcon(
            ar.m_glyph, ar.m_color, balloonchatwnd::kChatTitleBarBg, 48);
    }
    if (m_titleIconBmp != NULL)
    {
        SetIcon(m_titleIconBmp);
    }

    //任务栏 / Alt-Tab 图标：先毁旧 HICON，再用 32px 位图重建。
    if (m_hTaskIcon != NULL)
    {
        ::DestroyIcon(m_hTaskIcon);
        m_hTaskIcon = NULL;
    }
    HBITMAP bmp32 = NULL;
    if (useImage)
    {
        //圆形真图（圆角半径 = 半边长即圆，与联系人树一致）。
        bmp32 = balloonmain::MakeRoundedAvatarFromFile((LPCWSTR)ar.m_imagePath, 32, 16);
    }
    if (bmp32 == NULL)
    {
        bmp32 = balloonmain::MakeMonogramAvatar(ar.m_glyph, ar.m_color, 32);
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

void CBalloonGroupChatWindow::SetHostPanelHwnd(HWND hostPanel)
{
    m_hostPanel = hostPanel;
}

void CBalloonGroupChatWindow::SetMyRole(int role)
{
    m_myRole = role;
    if (IsWindow())
    {
        ApplyAdminGearVisibility();
        //本人角色变了，气泡 @ 菜单里的管理项跟着变，故同步给消息区。
        //成员侧栏各行的 m_myNetRole 在下一次重填成员时一并更新（角色变更推送
        //必定伴随一次成员列表重拉，见 CBalloonMainPanel::OnGroupMemberChanged）。
        balloonwjui::DuiControl* content = GetClientContent();
        if (content != NULL)
        {
            balloonchatwnd::ChatMessageList* msgList =
                dynamic_cast<balloonchatwnd::ChatMessageList*>(
                    content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
            if (msgList != NULL)
            {
                msgList->SetMyNetRole(m_myRole);
            }
        }
    }
}

void CBalloonGroupChatWindow::ApplyAdminGearVisibility()
{
    // 群角色取值：1 管理员、2 群主（与 group_member_role 对齐）。
    const int kRoleAdminValue = 1;
    const int kRoleOwnerValue = 2;

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* gear =
        content->FindCtrlById(balloonchatwnd::kGIdHdrAdmin);
    if (gear == NULL)
    {
        return;
    }

    const bool show = (m_myRole == kRoleAdminValue || m_myRole == kRoleOwnerValue);
    gear->SetVisible(show);

    // 隐藏后头部需重排以收起齿轮槽位；窗口就绪时投递一次重排（HBox 布局会
    // 自动跳过不可见子控件）。
    if (IsWindow())
    {
        ::PostMessage(m_hWnd, kRelayoutMsg, 0, 0);
    }
}

void CBalloonGroupChatWindow::OpenFontPanel()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kGIdTbFont);
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

void CBalloonGroupChatWindow::OnFontChanged(void* userdata, const net::TextFont& font)
{
    CBalloonGroupChatWindow* self = (CBalloonGroupChatWindow*)userdata;
    if (self == NULL)
    {
        return;
    }
    self->m_currentFont = font;

    // 输入框所见即所得。
    balloonwjui::DuiControl* content = self->GetClientContent();
    if (content != NULL)
    {
        balloonwjui::DuiRichEditHost* input = dynamic_cast<balloonwjui::DuiRichEditHost*>(
            content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
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

    // 全局记忆：写回 flamingo.ini。
    balloonchatwnd::SaveChatFont(font);
}

void CBalloonGroupChatWindow::OpenEmojiPicker()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kGIdTbEmoji);
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

    m_facePopup.SetPickCallback(&CBalloonGroupChatWindow::OnFacePicked, this);
    m_facePopup.OpenAt(anchor, m_hWnd);
}

void CBalloonGroupChatWindow::OnFacePicked(void* userdata, int faceId, const wchar_t* facePath)
{
    CBalloonGroupChatWindow* self = (CBalloonGroupChatWindow*)userdata;
    if (self != NULL)
    {
        self->InsertFaceToInput(faceId, facePath);
    }
}

void CBalloonGroupChatWindow::InsertFaceToInput(int faceId, const wchar_t* facePath)
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
        content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
    if (input == NULL)
    {
        return;
    }
    // 以"带 faceId 标记的内联图"插入当前光标处（所见即所得）。标记按 ChatFaceTag
    // 约定编码进 REOBJECT.dwUser，发送时据此把表情与截图区分、内联进文本消息。
    // 面板保持打开便于连续选，故不在此抢焦点。
    input->InsertTaggedImage(facePath,
                             balloonchatwnd::FaceTagFromId(faceId),
                             balloonchatwnd::kFaceDisplayPx);
}

CBalloonGroupChatWindow::~CBalloonGroupChatWindow()
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

void CBalloonGroupChatWindow::CreateAndShow(HWND hMainDlg)
{
    m_hMainDlg = hMainDlg;

    if (Create(NULL, CWindow::rcDefault, _T("FlamingoIM"),
               WS_OVERLAPPEDWINDOW, 0) == NULL)
    {
        LOG_ERROR(_T("群聊窗口 Create 失败。"));
        return;
    }

    // 标题栏：常规（非透明）标题栏，承载 min/max/close 三按钮。标题取真实群名；
    // 群名还没到位（仅占位）时先用占位，群资料到达后主面板会再 SetGroupName 刷新。
    CString title = m_groupName.IsEmpty() ? CString(_T("群聊")) : m_groupName;
    SetTitle(title);
    SetTitleBarHeight(kGroupChatWindowTitleBarHeight);
    SetButtons(true, true, true);
    SetMinSize(kGroupChatWindowMinWidth, kGroupChatWindowMinHeight);

    // 开启双击合成，让群聊气泡能收到双击（双击图片放大查看）。
    EnableDoubleClick(true);

    // 标题栏图标 = 群头像（真图或字形 monogram），同时设到标题栏左侧与任务栏 /
    // Alt-Tab（照单聊窗口径）。仅真实群（绑定了 groupid）设置。
    RebuildGroupIcons();

    InitUi();
    ResizeClient(kGroupChatWindowWidth, kGroupChatWindowHeight);
    CenterWindow();
    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    // 真实群：拉群成员，填成员侧栏并建发送人映射（应答经主面板路由回
    // OnGroupMembersArrived）。无群 id 的调试入口保持 mock，不拉。
    if (m_groupId != 0)
    {
        CIUSocket::GetInstance().RequestGroupMembers(m_groupId);
        // 群文件也在开窗时就拉：边栏「群文件」Tab 上要显示真实数量，不能等用户切到
        // 那个 Tab 才知道有几个（切过去时的那次拉取因此多半直接返回，见 StartLoadGroupFiles）。
        StartLoadGroupFiles();
    }
    else
    {
        // 调试入口（--groupchatwindow）：用 mock 群成员填 m_memberInfo，好让 @提醒
        // 浮层在没有服务器的情况下也能弹出、可点、可插入 —— 否则这条交互只能登录
        // 进真实群才验得了，每改一次都得手工重来一遍。
        // uid 用 1000+下标 造一批不与真实 userid 冲突的占位值。
        const std::vector<balloonchatwnd::GroupMember>& mocks =
            balloonchatwnd::MockGroupMembers();
        for (size_t i = 0; i < mocks.size(); ++i)
        {
            MemberInfo info;
            info.m_name  = mocks[i].name;
            info.m_glyph = mocks[i].avatarText;
            info.m_color = mocks[i].avatarColor;
            m_memberInfo[(int64_t)(1000 + i)] = info;
        }
        LOG_INFO("群聊窗调试入口：已填入 %u 个 mock 群成员供 @提醒 使用。",
                 (uint32_t)m_memberInfo.size());
    }

    // 边栏两个 Tab 的数量文案落一次初值：真实群此刻成员与文件都还在路上，显示光秃秃的
    // "成员" / "群文件"，等应答到达再由各自的回调刷成真实数；调试入口的 mock 成员与
    // mock 文件库都已就位，这一次就能写出真实数量。
    RefreshSideTabCounts();

    LOG_INFO(_T("群聊窗口已创建并显示。"));
}

// =======================================================================
// 客户区
// =======================================================================

void CBalloonGroupChatWindow::InitUi()
{
    // 构建群聊窗客户区控件树（来自 GroupChatWindowLayout 的内嵌 XML）。真实群
    // （m_groupId != 0）起始为空，不预填 mock 消息 / 成员；调试入口才用 mock。
    std::unique_ptr<balloonwjui::DuiControl> root =
        balloonchatwnd::BuildGroupChatWindowBody(m_groupId == 0);
    if (!root)
    {
        // 内嵌 XML 为编译期常量、形态固定，正常情况下不会解析失败。
        LOG_ERROR(_T("BuildGroupChatWindowBody 返回空 —— 群聊窗客户区未能构建。"));
        return;
    }
    SetClientContent(std::move(root));

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        LOG_ERROR(_T("SetClientContent 之后 GetClientContent 仍为空。"));
        return;
    }

    // 调试入口（无真实群）：用 mock 群文件预填库并置已加载，切到群文件 Tab 即渲染。
    // 真实群（m_groupId != 0）的文件由首次切到群文件 Tab 时 cmd_get_group_files 拉取填充。
    if (m_groupId == 0)
    {
        m_fileLibrary.SetAll(MockGroupFileEntries());
        m_fileLoaded = true;
    }

    // 消息输入框是富文本 DuiRichEditHost（内嵌真 RICHEDIT50W，支持后续把截图作为内联
    // 图片塞进来）；成员搜索框仍是纯文本 DuiEditHost。两者都只由 builder 建控件对象、
    // 不调 EnsureCreated，须在 host HWND 就绪后各显式建一次。
    balloonwjui::DuiRichEditHost* input = dynamic_cast<balloonwjui::DuiRichEditHost*>(
        content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
    if (input != NULL)
    {
        input->SetShowBorder(false);   // 输入框无边框，融入输入区
        // 多行 + 自动换行：须在 EnsureCreated 之前设。RichEdit 默认即多行自动换行，
        // 这里显式声明意图、与 XML 一致。
        input->SetMultiLine(true);
        input->SetWordWrap(true);
        // 关掉 RichEdit 原生竖直滚动条槽（须在 EnsureCreated 之前设——WS_VSCROLL 是
        // 创建期 baked-in 风格位）；改用下方自绘的细悬浮滚动条，与接收区风格一致。
        input->SetShowVScroll(false);
        if (!input->EnsureCreated(m_hWnd))
        {
            LOG_WARNING(_T("群聊窗输入框 EnsureCreated 失败。"));
        }
        else
        {
            // 粘贴一律按纯文本（聊天输入不要带来源的颜色/字体/链接/对象）。
            input->SetPasteAsPlainTextDefault(true);
            // 装回车过滤器：Enter 发送、Ctrl+Enter 换行。须在 EnsureCreated 之后调
            // —— 此时内部 RichEdit 子窗口才存在；按 Enter 时投递 kSendOnEnterMsg 回本窗口。
            balloonchatwnd::InstallChatInputEnterToSend(
                input->GetHostedHwnd(), m_hWnd, kSendOnEnterMsg);
            // 装 @提醒 键盘钩子（与回车过滤器各自独立的子类，互不影响）：
            // 输入变化投 kMentionChangedMsg，方向键 / 回车 / Tab / Esc 同步问
            // kMentionNavMsg 要不要交给浮层。
            balloonchatwnd::InstallChatInputMentionHook(
                input->GetHostedHwnd(), m_hWnd, kMentionChangedMsg, kMentionNavMsg,
                kMentionWheelMsg);
            // 浮窗提前建好（此刻不显示）。建窗 + 建控件树是同步的，留到首次键入 '@'
            // 时再做，会把那一拍拖长、让刚敲的 '@' 迟迟不显示。
            m_atDropdown.EnsureCreated();
            m_atDropdown.SetPickCallback(&CBalloonGroupChatWindow::OnAtPickThunk, this);
            // 装与接收区一致的细悬浮滚动条（内容溢出才出现、浮在右沿、可拖动）。
            balloonchatwnd::AttachChatInputScrollBar(input);

            // 给消息区装"成员是否仍在群里"的查询钩子：气泡里 @某人 的右键菜单据此
            // 决定「再 @ 一次 TA」要不要置灰。只有群聊窗需要装。
            balloonchatwnd::ChatMessageList* msgList =
                dynamic_cast<balloonchatwnd::ChatMessageList*>(
                    content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
            if (msgList != NULL)
            {
                msgList->SetMemberInGroupQuery(
                    &CBalloonGroupChatWindow::IsMemberInGroupThunk, this);
                // 同理装"查成员角色"的钩子，并把本人角色同步下去：气泡里 @某人 的
                // 右键菜单末尾要不要给出「设为 / 撤销管理员」，靠这两样判定。
                msgList->SetMemberRoleQuery(
                    &CBalloonGroupChatWindow::MemberRoleThunk, this);
                msgList->SetMyNetRole(m_myRole);
            }
        }
        // 载入上次的聊天字体并应用到输入框（所见即所得）。未设过则保持默认外观。
        // RichEdit 不接管 HFONT 所有权，返回的句柄存入 m_hInputFont 由本窗口持有。
        m_currentFont = balloonchatwnd::LoadChatFont();
        m_hInputFont = balloonchatwnd::ApplyChatFontToEdit(input, m_currentFont);
    }

    // 字体设置浮层：装"变化"回调（this 作上下文）。
    m_fontPopup.SetChangeCallback(&CBalloonGroupChatWindow::OnFontChanged, this);

    balloonwjui::DuiEditHost* memberSearch = dynamic_cast<balloonwjui::DuiEditHost*>(
        content->FindCtrlById(balloonchatwnd::kGIdMemberSearch));
    if (memberSearch != NULL && !memberSearch->EnsureCreated(m_hWnd))
    {
        LOG_WARNING(_T("群聊窗成员搜索框 EnsureCreated 失败。"));
    }

    // 给头部 / 输入工具栏 / 发送按钮 / @提醒 注册 tooltip。
    RegisterTip(content, balloonchatwnd::kGIdHdrVoice,     _T("语音通话"));
    RegisterTip(content, balloonchatwnd::kGIdHdrVideo,     _T("视频通话"));
    RegisterTip(content, balloonchatwnd::kGIdHdrHistory,   _T("消息记录"));
    RegisterTip(content, balloonchatwnd::kGIdHdrAdmin,     _T("群管理"));
    RegisterTip(content, balloonchatwnd::kGIdHdrMore,      _T("更多"));
    RegisterTip(content, balloonchatwnd::kGIdTbEmoji,      _T("表情"));
    RegisterTip(content, balloonchatwnd::kGIdTbImage,      _T("图片"));
    // 群聊里这个按钮上传的是群文件（进右侧群文件库），不是发给某个人的文件消息，
    // 提示文字写清楚，免得用户以为是单聊那种"发文件"。
    RegisterTip(content, balloonchatwnd::kGIdTbFile,       _T("上传群文件"));
    RegisterTip(content, balloonchatwnd::kGIdTbScreenshot, _T("截图"));
    RegisterTip(content, balloonchatwnd::kGIdTbVoice,      _T("语音消息"));
    RegisterTip(content, balloonchatwnd::kGIdTbFont,       _T("字体设置"));
    RegisterTip(content, balloonchatwnd::kGIdTbPlus,       _T("更多功能"));
    RegisterTip(content, balloonchatwnd::kGIdMention,      _T("@提醒成员"));
    RegisterTip(content, balloonchatwnd::kGIdSendButton,   _T("发送"));

    // 按持久化的发送模式刷新左下角提示与输入框 placeholder（重启后保留上次选择）。
    RefreshSendModeUi();

    // 按本人角色决定"群管理"齿轮是否显示（仅群主 / 管理员可见）。
    ApplyAdminGearVisibility();

    // 注册到图片解码缓存：群里图片气泡发起后台解码后，缩放完成经 WM_CHAT_IMAGE_READY
    // 通知本窗口重绘。注销在 OnCloseMsg。
    balloonchatwnd::ChatImageCache::Inst().RegisterListener(m_hWnd);

    // 群文件面板底部的翻页 ‹ › 与「上传文件」按钮改用 Default 变体（白底 + 1px 灰边 +
    // 深字，低调的次操作样式）。默认的 Primary 实心蓝按钮在窄边栏里显得太重，对照
    // design/群文件.png 应是轻量样式。
    {
        const UINT kSubtleBtnIds[3] =
        {
            balloonchatwnd::kGIdFilePagePrev,
            balloonchatwnd::kGIdFilePageNext,
            balloonchatwnd::kGIdFileUpload,
        };
        for (int i = 0; i < 3; ++i)
        {
            balloonwjui::DuiButton* btn =
                dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(kSubtleBtnIds[i]));
            if (btn != NULL)
            {
                btn->SetVariant(balloonwjui::DuiButton::Variant::Default);
            }
        }
    }

    // 调试入口（--groupchatwindow，没有主面板也就没有服务端）下，顶部「已 Pin N 条」
    // 提示带的条数没有别处可问，就照 mock 消息里带 Pin 标记的行数凑一个。理由与真实
    // 会话为何不能这么算，见单聊窗 InitUi 里同一段注释。
    if (m_groupId == 0)
    {
        balloonchatwnd::ChatMessageList* mockList =
            dynamic_cast<balloonchatwnd::ChatMessageList*>(
                content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
        if (mockList != NULL)
        {
            SetPinnedCount(mockList->CountPinned());
        }
    }

    LOG_INFO(_T("群聊窗客户区 UI 初始化完成。"));
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CBalloonGroupChatWindow::OnActivateMsg(UINT /*uMsg*/, WPARAM wParam,
                                               LPARAM /*lParam*/, BOOL& bHandled)
{
    // 不消费 WM_ACTIVATE，理由与口径同单聊窗（见 CBalloonChatWindow::OnActivateMsg）。
    bHandled = FALSE;

    if (LOWORD(wParam) == WA_INACTIVE)
    {
        return 0;
    }
    if (m_hostPanel == NULL || !::IsWindow(m_hostPanel))
    {
        return 0;
    }
    if (m_groupId <= 0)
    {
        return 0;
    }

    long long* pCid = new long long((long long)m_groupId);
    LOG_INFO("群聊窗被激活，请求清除该会话未读: group=%lld.", (long long)m_groupId);
    ::PostMessage(m_hostPanel, FMG_MSG_CONV_MARK_READ, (WPARAM)2, (LPARAM)pCid);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                            LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // m_hMainDlg 非空（调试入口 --groupchatwindow）：本窗口是唯一可见
    // 窗口，关闭即退出程序，交由 CMainDlg 统一收尾。
    // m_hMainDlg 为空（从主面板打开）：关闭只销毁自身。
    if (m_hMainDlg != NULL && ::IsWindow(m_hMainDlg))
    {
        ::PostMessage(m_hMainDlg, WM_CLOSE_MAINDLG, 0, 0);
    }
    // 从图片解码缓存注销本窗口，避免就绪后向已销毁窗口 PostMessage。
    balloonchatwnd::ChatImageCache::Inst().UnregisterListener(m_hWnd);
    if (IsWindow())
    {
        DestroyWindow();
    }
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnViewImageMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM lParam, BOOL& /*bHandled*/)
{
    // 群聊气泡双击图片冒泡上来：lParam 为图片本地路径。打开进程内共享看图窗放大查看。
    LPCTSTR path = (LPCTSTR)lParam;
    if (path != NULL && path[0] != _T('\0'))
    {
        CBalloonImageViewerWindow::OpenShared(m_hWnd, path);
    }
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnImageReady(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 后台某张图片缩放完成：重绘消息区，图片气泡再画一次时查 ChatImageCache 命中、换成
    // 真图。图片展示尺寸不变，无需重排，直接 Invalidate。
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM lParam, BOOL& bHandled)
{
    // WM_DUI_NOTIFY 的 LPARAM 指向一个 DuiNotify（仅在本次同步派发内有效）。
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_CLICK)
    {
        // 「发送」按钮 → 发消息。
        if (pNotify->ctrlId == balloonchatwnd::kGIdSendButton)
        {
            DoSend();
            return 0;
        }
        // 顶部离线提示带只作告知、不可点击，故这里没有它的分支 —— 恢复在线统一走
        // 托盘 / 主面板的状态菜单那个唯一入口。

        // 右侧边栏三个 Tab → 切换选中。
        if (pNotify->ctrlId == balloonchatwnd::kGIdTabMembers ||
            pNotify->ctrlId == balloonchatwnd::kGIdTabFiles ||
            pNotify->ctrlId == balloonchatwnd::kGIdTabNotice)
        {
            SwitchSideTab(pNotify->ctrlId);
            return 0;
        }

        // 公告面板：上一条 / 下一条。
        if (pNotify->ctrlId == balloonchatwnd::kGIdNoticePrev)
        {
            StepNotice(-1);
            return 0;
        }
        if (pNotify->ctrlId == balloonchatwnd::kGIdNoticeNext)
        {
            StepNotice(1);
            return 0;
        }

        // 群文件面板：分类筛选 chip → 切分类、回到第 1 页、重渲染。
        {
            int chipCat = -1;
            switch (pNotify->ctrlId)
            {
            case balloonchatwnd::kGIdFileChipAll:
                chipCat = balloonchatwnd::kGroupFileCatAll;
                break;
            case balloonchatwnd::kGIdFileChipPdf:
                chipCat = balloonchatwnd::kGroupFileCatPdf;
                break;
            case balloonchatwnd::kGIdFileChipDoc:
                chipCat = balloonchatwnd::kGroupFileCatDoc;
                break;
            case balloonchatwnd::kGIdFileChipSheet:
                chipCat = balloonchatwnd::kGroupFileCatSheet;
                break;
            case balloonchatwnd::kGIdFileChipDesign:
                chipCat = balloonchatwnd::kGroupFileCatDesign;
                break;
            default:
                break;
            }
            if (chipCat >= 0)
            {
                ApplyFileChipSelection(chipCat);
                m_filePage = 0;
                RenderFilePanel();
                return 0;
            }
        }
        // 群文件面板：上一页。
        if (pNotify->ctrlId == balloonchatwnd::kGIdFilePagePrev)
        {
            if (m_filePage > 0)
            {
                m_filePage--;
                RenderFilePanel();
            }
            return 0;
        }
        // 群文件面板：下一页（越界由 RenderFilePanel 内的 Query 钳回末页）。
        if (pNotify->ctrlId == balloonchatwnd::kGIdFilePageNext)
        {
            m_filePage++;
            RenderFilePanel();
            return 0;
        }
        // 群文件面板：上传文件 → 选文件并交 FileTransfer 群文件上传。每点一次发起一笔，
        // 可多笔并发（最多 N 笔同时传、超出排队），各占面板顶部一行「上传中」。
        if (pNotify->ctrlId == balloonchatwnd::kGIdFileUpload)
        {
            PickAndUploadGroupFile();
            return 0;
        }
        // 点击"文件"工具栏按钮 → 与群文件面板的「上传文件」按钮走同一条路：选文件并
        // 上传进群文件库（群聊里发文件即入群文件库，不在消息区发文件气泡）。真的发起了
        // 一笔才把边栏切到「群文件」Tab，让用户立刻看到顶部那行「上传中 NN%」；用户在
        // 选文件框点了取消就什么都不做，不平白把边栏切走。
        if (pNotify->ctrlId == balloonchatwnd::kGIdTbFile)
        {
            if (PickAndUploadGroupFile())
            {
                SwitchSideTab(balloonchatwnd::kGIdTabFiles);
            }
            return 0;
        }
        // 点击"字体"工具栏按钮（AF）→ 打开字体设置面板。
        if (pNotify->ctrlId == balloonchatwnd::kGIdTbFont)
        {
            OpenFontPanel();
            return 0;
        }
        // 点击"表情"工具栏按钮 → 打开 Bin/Face 图片表情选择面板。
        if (pNotify->ctrlId == balloonchatwnd::kGIdTbEmoji)
        {
            OpenEmojiPicker();
            return 0;
        }
        // 点击「@提醒」按钮 → 等价于在光标处敲一个 '@'：往输入框插入 '@' 并把焦点
        // 交回去，随后钩子会照常投 kMentionChangedMsg，浮层就按正常路径弹出来 ——
        // 这样按钮和手敲 '@' 走的是同一套逻辑，不会出现两条各自演化的分支。
        // 引用条上的 ✕：取消本次引用（清空待发快照并收起引用条）。
        if (pNotify->ctrlId == balloonchatwnd::kGIdReplyClose)
        {
            CancelPendingReply();
            return 0;
        }

        if (pNotify->ctrlId == balloonchatwnd::kGIdMention)
        {
            HWND hEdit = GetInputEditHwnd();
            if (hEdit != NULL)
            {
                ::SetFocus(hEdit);
                ::SendMessage(hEdit, EM_REPLACESEL, TRUE, (LPARAM)_T("@"));
                ::PostMessage(m_hWnd, kMentionChangedMsg, 0, 0);
            }
            return 0;
        }
        // 点击"图片"工具栏按钮 → 选图并作为群图片消息发送。
        if (pNotify->ctrlId == balloonchatwnd::kGIdTbImage)
        {
            PickAndSendImage();
            return 0;
        }
        // 点击"截图"工具栏按钮 → 发起截图。只投递自定义消息、不直接发起：截图浮层是
        // 模态消息循环，在 DUI 鼠标事件派发栈内启动会引发捕获 / 重入问题，导致截图后
        // 发送按钮等点击失灵（与发送▼菜单同理）。延后到鼠标事件外再发起。
        if (pNotify->ctrlId == balloonchatwnd::kGIdTbScreenshot)
        {
            ::PostMessage(m_hWnd, kDoScreenshotMsg, 0, 0);
            return 0;
        }
        // 头部「消息记录」按钮 → 请主面板打开本群的历史消息记录窗，落在"全部"分类上。
        if (pNotify->ctrlId == balloonchatwnd::kGIdHdrHistory)
        {
            RequestOpenHistory(balloonhistory::kHistoryTypeAll);
            return 0;
        }
        // 头部下方的「已 Pin N 条」提示带 → 同样打开历史消息记录窗，但直接切到 Pin
        // 分类。与单聊窗同一套做法，理由见 CBalloonChatWindow::OnDuiNotify 里那段注释。
        if (pNotify->ctrlId == balloonchatwnd::kGIdPinBanner)
        {
            RequestOpenHistory(balloonhistory::kHistoryTypePinned);
            return 0;
        }
        // 头部「群管理」齿轮 → 请主面板打开本群的群管理对话框（仅群主 / 管理员；
        // 齿轮本就只对他们显示，这里再防御一次）。群 id 经堆上 int64_t 随消息回投，
        // 由主面板处理后 delete。
        if (pNotify->ctrlId == balloonchatwnd::kGIdHdrAdmin)
        {
            const int kRoleAdminValue = 1;
            const int kRoleOwnerValue = 2;
            if ((m_myRole == kRoleAdminValue || m_myRole == kRoleOwnerValue)
                && m_groupId > 0
                && m_hostPanel != NULL && ::IsWindow(m_hostPanel))
            {
                int64_t* pGroupId = new int64_t(m_groupId);
                if (!::PostMessage(m_hostPanel, FMG_MSG_OPEN_GROUP_MANAGE, 0,
                                   (LPARAM)pGroupId))
                {
                    delete pGroupId;   // 投递失败（主面板已销毁）自行回收，杜绝泄漏
                }
            }
            return 0;
        }
        // 头部「语音通话」/「视频通话」按钮 → 群组（一对多）暂不支持音视频通话（本期
        // 只做 1 对 1）；给出明确提示，避免点击"没反应"。
        if (pNotify->ctrlId == balloonchatwnd::kGIdHdrVoice
            || pNotify->ctrlId == balloonchatwnd::kGIdHdrVideo)
        {
            ::MessageBox(m_hWnd, _T("群聊暂不支持语音 / 视频通话。"),
                         _T("提示"), MB_OK | MB_ICONINFORMATION);
            return 0;
        }
    }

    // 群文件面板：搜索框文字变化（每键触发）→ 按文件名实时过滤、回到第 1 页、重渲染。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_VALUECHANGED
        && pNotify->ctrlId == balloonchatwnd::kGIdFileSearch)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonwjui::DuiEditHost* search = (content != NULL)
            ? dynamic_cast<balloonwjui::DuiEditHost*>(
                  content->FindCtrlById(balloonchatwnd::kGIdFileSearch))
            : NULL;
        if (search != NULL)
        {
            m_fileSearch = CStringToUtf8(search->GetText());
            m_filePage = 0;
            RenderFilePanel();
        }
        return 0;
    }

    // 点发送按钮右侧「▼」→ 弹发送模式切换菜单。这里只投递自定义消息、不直接弹，
    // 避免在 DUI 鼠标事件的同步派发栈里启动模态菜单导致捕获/重入问题。
    if (pNotify != NULL
        && pNotify->code == balloonchatwnd::ChatSendButton::DUIN_SENDBTN_DROPDOWN
        && pNotify->ctrlId == balloonchatwnd::kGIdSendButton)
    {
        ::PostMessage(m_hWnd, kOpenSendMenuMsg, 0, 0);
        return 0;
    }

    // 用户拖完左列中间分隔条（kGIdInputSplitter）→ 记下当前"输入区块高度"（与
    // 单聊共用），供本次运行期间新开的聊天窗、以及本窗口后续缩放时套用。
    // 输入区块高度 = 分隔轴可用高度 - 消息区(pane0)高度。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_VALUECHANGED
        && pNotify->ctrlId == balloonchatwnd::kGIdInputSplitter)
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonwjui::DuiSplitter* sp = (content != NULL)
            ? dynamic_cast<balloonwjui::DuiSplitter*>(
                  content->FindCtrlById(balloonchatwnd::kGIdInputSplitter))
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

    // 其它控件的通知本阶段不处理。
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnTimer(UINT /*uMsg*/, WPARAM wParam,
                                         LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam == (WPARAM)kReplyTimerId)
    {
        // 一次性定时器：先关掉，再追加一条某位群成员的 mock 回复。
        ::KillTimer(m_hWnd, kReplyTimerId);

        balloonwjui::DuiControl* content = GetClientContent();
        if (content != NULL)
        {
            balloonchatwnd::ChatMessageList* list =
                dynamic_cast<balloonchatwnd::ChatMessageList*>(
                    content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
            if (list != NULL)
            {
                list->AppendMessage(
                    balloonchatwnd::MakeGroupAutoReply(m_replyIndex));
                ++m_replyIndex;
            }
        }
        return 0;
    }

    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnSendOnEnter(UINT /*uMsg*/, WPARAM /*wParam*/,
                                               LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 群聊输入框按 Enter：等同点「发送」按钮。Ctrl+Enter 的换行已由过滤器在框内
    // 就地处理，不会走到这里。
    DoSend();
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnDoScreenshot(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 已脱离 DUI 鼠标事件派发栈，安全地发起模态截图浮层。
    DoScreenshot();
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnOpenSendMenu(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }
    balloonwjui::DuiControl* btn = content->FindCtrlById(balloonchatwnd::kGIdSendButton);
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

void CBalloonGroupChatWindow::RefreshSendModeUi()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::SendKeyMode mode = balloonchatwnd::GetChatSendMode();

    // 左下角提示文字随模式联动。
    balloonwjui::DuiLabel* hint = dynamic_cast<balloonwjui::DuiLabel*>(
        content->FindCtrlById(balloonchatwnd::kGIdHintLabel));
    if (hint != NULL)
    {
        hint->SetText(balloonchatwnd::GetChatSendHintText(mode));
    }

    // 输入框 placeholder 随模式联动。
    balloonwjui::DuiRichEditHost* input = dynamic_cast<balloonwjui::DuiRichEditHost*>(
        content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
    if (input != NULL)
    {
        input->SetPlaceholder(balloonchatwnd::GetChatSendPlaceholder(mode));
    }
}

void CBalloonGroupChatWindow::ApplyConnectionState(int state)
{
    // 记下变更前的连接态，用于判断本次是不是"从断线恢复到在线"这一跳。
    const bool wasOnline = (m_connUiState == (int)balloonmain::kConnUiOnline);
    m_connUiState = state;

    // 断线期间发出的「拉全量群文件」请求既发不出去也等不到应答，m_fileLoading 会一直
    // 卡在 true —— 此后连切到群文件 Tab 都不会重试，边栏 Tab 上的文件数量就永远出不来。
    // 故在"恢复到在线"这一跳上把那笔作废并重拉一次。只在这一跳做，避免在线态被重复推送
    // 时把正在进行的拉取打断、重复累积。
    if (!wasOnline && state == (int)balloonmain::kConnUiOnline
        && m_groupId != 0 && !m_fileLoaded && m_fileLoading)
    {
        LOG_INFO("群聊窗恢复在线：作废断线期间未完成的群文件拉取并重拉。group=%lld。",
                 (long long)m_groupId);
        m_fileLoading = false;
        m_fileLoadBuffer.clear();
        StartLoadGroupFiles();
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;   // 控件树还没建好，等窗口创建完主面板会再推一次
    }

    const bool bOnline = (state == (int)balloonmain::kConnUiOnline);

    // 顶部提示带：在线时不显示，离线 / 正在连接时显示对应文字。做法与单聊窗一致。
    balloonchatwnd::ChatBannerLabel* banner =
        dynamic_cast<balloonchatwnd::ChatBannerLabel*>(
            content->FindCtrlById(balloonchatwnd::kGIdOfflineBanner));
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
            // 两种态都是普通文字、整条不可点，只有配色不同。与单聊窗同一套色值。
            if (state == (int)balloonmain::kConnUiOffline)
            {
                banner->SetBannerColors(kGroupOfflineBannerBgColor,
                                        kGroupOfflineBannerBorderColor);
                banner->SetTextColor(kGroupOfflineBannerColor);
            }
            else
            {
                banner->SetBannerColors(kGroupConnectingBannerBgColor,
                                        kGroupConnectingBannerBorderColor);
                banner->SetTextColor(kGroupConnectingBannerColor);
            }
            banner->SetVisible(true);
        }
        bannerVisibilityChanged = (banner->IsVisible() != wasVisible);
    }

    // 输入框置为只读（草稿仍可见可复制），发送按钮置灰并不再响应点击。
    balloonwjui::DuiEditHost* edit = dynamic_cast<balloonwjui::DuiEditHost*>(
        content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
    if (edit != NULL)
    {
        edit->SetReadOnly(!bOnline);
    }

    balloonwjui::DuiControl* sendBtn = content->FindCtrlById(balloonchatwnd::kGIdSendButton);
    if (sendBtn != NULL)
    {
        sendBtn->SetEnabled(bOnline);
    }

    // 提示条显隐改变了客户区顶部的占位高度，必须补一次整树布局才生效 ——
    // 原因与单聊窗 CBalloonChatWindow::ApplyConnectionState 完全一致，详见那里的说明。
    if (bannerVisibilityChanged)
    {
        content->ForceLayout(content->GetRect());
        if (IsWindow())
        {
            ::PostMessage(m_hWnd, kRelayoutMsg, 0, 0);
        }
    }

    Invalidate();
    LOG_INFO(_T("群聊窗应用连接态：group=%lld, state=%d(0=在线 1=正在连接 2=离线), 提示条显隐变化=%d。"),
        (long long)m_groupId, state, (int)bannerVisibilityChanged);
}

bool CBalloonGroupChatWindow::CheckOnlineFor(int action)
{
    if (m_connUiState == (int)balloonmain::kConnUiOnline)
    {
        return true;
    }

    LOG_INFO(_T("群聊窗操作被拦截：group=%lld, action=%d, state=%d。"),
        (long long)m_groupId, action, m_connUiState);
    ::MessageBox(m_hWnd, balloonmain::GetBlockedActionTip(action),
                 _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
    return false;
}

void CBalloonGroupChatWindow::DoSend()
{
    // 离线 / 连接未就绪时不发：消息到不了服务器，气泡却会留在窗口里误导用户。
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
        content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (input == NULL || list == NULL)
    {
        return;
    }

    // 按文档顺序枚举输入框内容（文本段 / 内嵌图段+标记），聚合成待发送部分序列：
    // 表情（带 faceId 标记的内嵌图）随文字编码进文本部分，截图等非表情图独立成段。
    std::vector<balloonchatwnd::RichInputSegment> segs;
    input->EnumContent(&CollectGroupInputSegment, &segs);
    std::vector<balloonchatwnd::RichOutgoingPart> parts =
        balloonchatwnd::BuildOutgoingParts(segs);
    if (parts.empty())
    {
        return;   // 空 / 全空白，不发
    }

    // 调试入口（--groupchatwindow，无群 id）：不支持发图。把各文字段拼起来做 mock
    // 回显 + 自动回复，保留离线核对体验。
    if (m_groupId == 0)
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
        // 调试入口也要走一遍 @ 的重扫与换算，否则这条路径下发出来的消息不高亮，
        // 与真实群聊表现不一致，@ 的渲染问题也就没法在调试入口里复现。
        {
            const std::string utf8Echo = CStringToUtf8(joined);
            const std::vector<balloonchatwnd::MentionSpan> atsEcho =
                balloonchatwnd::RescanMentions(utf8Echo, m_pendingMentions);
            if (!atsEcho.empty())
            {
                echo.ats = balloonchatwnd::ConvertSpansForRender(utf8Echo, atsEcho, 0);
            }
        }
        FillSelfSenderAvatar(m_lpFMGClient, echo);   // 自己气泡显示本端真实头像（字形 / 真图）
        list->AppendMessage(echo);
        m_pendingMentions.clear();
        ::SetTimer(m_hWnd, kReplyTimerId, kReplyDelayMs, NULL);
        return;
    }

    // 真实群会话：按文档顺序逐段发出（文字段→群文本消息，内嵌图→群图片消息）。各段
    // 乐观气泡按序追加。注意：发图依赖 RichEdit 里的内嵌对象，故须在全部发送完成<u>之后</u>
    // 才清空输入框（提前清空会把内嵌图一起抹掉）。
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (parts[i].kind == balloonchatwnd::kRichPartText)
        {
            SendGroupTextPart(parts[i].text);
        }
        else
        {
            SendGroupImagePart(parts[i].imageIndex);
        }
    }
    input->SetText(_T(""));
    m_pendingShotPaths.clear();   // 内嵌图已发完，清空源文件追踪
    m_pendingMentions.clear();    // @ 记录随本次发送作废，下一条重新累积
}

// =======================================================================
// @提醒：输入侧
// =======================================================================

// 「@提醒」在输入框里的着色（与气泡里 @ 的主题橙同色系，见 ChatBubble.cpp 的
// kAtMeFgOnOther）。插入时给 `@显示名` 这段设成它，与普通正文一眼分开。
static const COLORREF kMentionInputColor = RGB(0xD9, 0x53, 0x1E);

// @ 浮层相对 '@' 字符再往下让出的高度（像素），免得浮层压住正在输入的那一行。
// 取输入框默认行高的近似值即可，不需要精确。
static const int kMentionAnchorLineHeight = 20;

HWND CBalloonGroupChatWindow::GetInputEditHwnd()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return NULL;
    }
    balloonwjui::DuiRichEditHost* input = dynamic_cast<balloonwjui::DuiRichEditHost*>(
        content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
    if (input == NULL)
    {
        return NULL;
    }
    return input->GetHostedHwnd();
}

bool CBalloonGroupChatWindow::ResolveMentionPrefix(CString& prefixOut, int& atPosOut)
{
    prefixOut.Empty();
    atPosOut = -1;

    HWND hEdit = GetInputEditHwnd();
    if (hEdit == NULL)
    {
        return false;
    }

    // 取光标位置（选区非空时按选区尾算）与全文。
    DWORD selStart = 0;
    DWORD selEnd   = 0;
    ::SendMessage(hEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    const int caret = (int)selEnd;

    const int len = ::GetWindowTextLength(hEdit);
    if (len <= 0 || caret <= 0 || caret > len)
    {
        return false;
    }
    CString text;
    ::GetWindowText(hEdit, text.GetBuffer(len + 1), len + 1);
    text.ReleaseBuffer();

    // 从光标往前找最近的 '@'。中途遇到空白 / 换行就放弃 —— 说明这一段不是
    // 正在编辑的 @ 片段（名字里不含空格，@ 后跟空格即视为该片段已结束）。
    int pos = -1;
    for (int i = caret - 1; i >= 0; --i)
    {
        const TCHAR ch = text.GetAt(i);
        if (ch == _T('@'))
        {
            pos = i;
            break;
        }
        if (ch == _T(' ') || ch == _T('\t') || ch == _T('\r') || ch == _T('\n'))
        {
            return false;
        }
    }
    if (pos < 0)
    {
        return false;
    }

    // 这个 '@' 必须位于文本开头或紧跟空白之后，否则是邮箱之类，不当 mention
    //（与发送前重扫 RescanMentions 的判定口径保持一致）。
    if (pos > 0)
    {
        const TCHAR prev = text.GetAt(pos - 1);
        if (prev != _T(' ') && prev != _T('\t') && prev != _T('\r') && prev != _T('\n'))
        {
            return false;
        }
    }

    prefixOut = text.Mid(pos + 1, caret - pos - 1);
    atPosOut  = pos;
    return true;
}

void CBalloonGroupChatWindow::InsertMention(int64_t uid, const CString& name)
{
    HWND hEdit = GetInputEditHwnd();
    if (hEdit == NULL || m_mentionAtPos < 0)
    {
        return;
    }

    // @ 浮层这条路：要替换掉的是"从用户键入的 '@' 到当前光标"这一段（即 `@已键入前缀`）。
    DWORD selStart = 0;
    DWORD selEnd   = 0;
    ::SendMessage(hEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
    InsertMentionInRange(uid, name, m_mentionAtPos, (int)selEnd);
    m_mentionAtPos = -1;
}

void CBalloonGroupChatWindow::InsertMentionAtCaret(int64_t uid, const CString& name)
{
    HWND hEdit = GetInputEditHwnd();
    if (hEdit == NULL)
    {
        return;
    }

    // 气泡右键「再 @ 一次 TA」这条路：没有任何已键入的 `@前缀` 要替换，就在光标处插入
    // （若当前有选区则替换选区，与普通编辑器的插入手感一致）。
    DWORD selStart = 0;
    DWORD selEnd   = 0;
    ::SendMessage(hEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);

    // 必要时补前导空格：重扫只认「文本开头、或紧跟空白之后」的 '@'，
    // 直接接在"你好"后面插出来的 "你好@张三 " 不会被识别成 @提醒。
    if (selStart > 0 && !PrevCharIsWhitespace(hEdit, (int)selStart))
    {
        ::SendMessage(hEdit, EM_SETSEL, (WPARAM)selStart, (LPARAM)selEnd);
        ::SendMessage(hEdit, EM_REPLACESEL, TRUE, (LPARAM)_T(" "));
        selStart += 1;
        selEnd = selStart;
    }

    InsertMentionInRange(uid, name, (int)selStart, (int)selEnd);
}

void CBalloonGroupChatWindow::InsertMentionInRange(int64_t uid, const CString& name,
                                                   int begin, int end)
{
    HWND hEdit = GetInputEditHwnd();
    if (hEdit == NULL || begin < 0 || end < begin)
    {
        return;
    }

    ::SendMessage(hEdit, EM_SETSEL, (WPARAM)begin, (LPARAM)end);

    CString inserted;
    inserted.Format(_T("@%s "), (LPCTSTR)name);   // 末尾空格作分隔，且不计入高亮片段
    ::SendMessage(hEdit, EM_REPLACESEL, TRUE, (LPARAM)(LPCTSTR)inserted);

    // 给刚插入的 `@显示名`（不含尾随空格）设主题橙。插入是从 begin 处开始的，
    // 故这一段就是 [begin, begin + 1 + 名字长度)。
    const int nameEnd = begin + 1 + name.GetLength();
    ::SendMessage(hEdit, EM_SETSEL, (WPARAM)begin, (LPARAM)nameEnd);
    CHARFORMAT2 cf;
    ::ZeroMemory(&cf, sizeof(cf));
    cf.cbSize      = sizeof(cf);
    cf.dwMask      = CFM_COLOR;
    cf.crTextColor = kMentionInputColor;
    ::SendMessage(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    // 光标移到尾随空格之后，并把格式恢复成默认色 —— 否则用户接着打的字会
    // 一路继承橙色。
    const int caretAfter = begin + inserted.GetLength();
    ::SendMessage(hEdit, EM_SETSEL, (WPARAM)caretAfter, (LPARAM)caretAfter);
    ::ZeroMemory(&cf, sizeof(cf));
    cf.cbSize      = sizeof(cf);
    cf.dwMask      = CFM_COLOR;
    cf.dwEffects   = CFE_AUTOCOLOR;   // 回到默认文字色
    ::SendMessage(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    // 记一条候选。发送时还会以最终文本为准重扫，这里只是"用户点过谁"的线索。
    balloonchatwnd::MentionCandidate cand;
    cand.m_uid         = uid;
    cand.m_displayName = CStringToUtf8(name);
    m_pendingMentions.push_back(cand);

    // 注意：m_mentionAtPos 由调用方各自收尾 —— @ 浮层那条路要清掉它（那一次
    // "用户键入的 @" 已经被消费），右键菜单这条路它本来就是 -1，不该在这里统一动。
    ::SetFocus(hEdit);   // 焦点交回输入框，用户可以接着打字

    LOG_INFO("群聊窗插入 @提醒: uid=%lld.", (long long)uid);
}

void CBalloonGroupChatWindow::OnAtPickThunk(void* userdata, int64_t uid, const CString& name)
{
    CBalloonGroupChatWindow* self = static_cast<CBalloonGroupChatWindow*>(userdata);
    if (self != NULL)
    {
        self->InsertMention(uid, name);
    }
}

LRESULT CBalloonGroupChatWindow::OnMentionChanged(UINT /*uMsg*/, WPARAM wParam,
                                                  LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 先把输入框待重绘的内容刷出去，再做后面可能很慢的浮层工作。
    //
    // 为什么必须这么做：WM_PAINT 是最低优先级消息，而本消息是 PostMessage 投来的，
    // 会<u>排在重绘之前</u>被处理。下面建浮窗 / 填列表 / 摆位置是同步的，几十毫秒过去
    // 才轮到 EDIT 重绘 —— 用户看到的就是"刚敲的 @ 不显示，等浮层弹出来才冒出来"。
    HWND hEditForPaint = GetInputEditHwnd();
    if (hEditForPaint != NULL)
    {
        ::UpdateWindow(hEditForPaint);
    }

    // wParam 非 0 = 钩子告知输入框已失焦。
    if (wParam != 0)
    {
        // 但要排除"失焦是因为点了浮层本身"这种情形：浮层是独立顶层浮窗，点它的
        // 成员行会让输入框<u>瞬间</u>失焦，而失焦发生在鼠标<u>按下</u>那一刻 ——
        // 此时若直接收起，鼠标还没抬起浮层就没了，那一行根本点不中。
        // 故先看光标是不是正落在浮层上：是就保留，让这次点击走完；否则才收起。
        //（主面板的搜索下拉早踩过同一个坑，见 BalloonMainPanel.cpp 的失焦分支。）
        POINT pt;
        ::GetCursorPos(&pt);
        const HWND hUnder    = ::WindowFromPoint(pt);
        const HWND hDropdown = m_atDropdown.GetHwnd();
        if (hDropdown != NULL && hUnder == hDropdown)
        {
            return 0;
        }

        m_atDropdown.Hide();
        m_mentionAtPos = -1;
        return 0;
    }

    // 群成员还没到（或调试入口无群）时不做 @ —— 没有候选可选。
    if (m_memberInfo.empty())
    {
        m_atDropdown.Hide();
        m_mentionAtPos = -1;
        return 0;
    }

    CString prefix;
    int     atPos = -1;
    if (!ResolveMentionPrefix(prefix, atPos))
    {
        m_atDropdown.Hide();
        m_mentionAtPos = -1;
        return 0;
    }

    const bool wasShowing = (m_mentionAtPos >= 0);
    m_mentionAtPos = atPos;

    if (wasShowing && m_atDropdown.IsShowing())
    {
        m_atDropdown.UpdateFilter(prefix);   // 只是前缀变了，刷新过滤即可
        return 0;
    }

    // 首次进入 @ 状态：把群成员（排除自己）整理成候选并弹出浮层。
    const UINT selfId = (m_lpFMGClient != NULL)
                      ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    std::vector<balloonchatwnd::AtCandidate> cands;

    // 「所有人」置顶，且只对群主 / 管理员开放 —— @所有人 会给全群每个人都推一次
    // 提醒，放开给普通成员等于把打扰全群的权力交给任何人（判定见 CanMentionAll）。
    // 普通成员看不到这一项，也就没法从浮层里选中它；即便手动打出 "@所有人"，发送前
    // 的重扫也只认"用户真正选过的候选"，扫不出片段，于是只当普通文字发出去。
    if (balloonchatwnd::CanMentionAll(m_myRole))
    {
        balloonchatwnd::AtCandidate all;
        all.m_uid  = balloonchatwnd::kMentionUidAll;
        all.m_name = Utf8ToCString(balloonchatwnd::kMentionAllNameUtf8);
        cands.push_back(all);
    }

    for (std::map<int64_t, MemberInfo>::const_iterator it = m_memberInfo.begin();
         it != m_memberInfo.end(); ++it)
    {
        if (selfId != 0 && (UINT)it->first == selfId)
        {
            continue;   // 不用 @ 自己
        }
        balloonchatwnd::AtCandidate one;
        one.m_uid  = it->first;
        one.m_name = it->second.m_name;
        if (one.m_name.IsEmpty())
        {
            one.m_name.Format(_T("用户%lld"), (long long)it->first);
        }
        cands.push_back(one);
    }
    if (cands.empty())
    {
        m_mentionAtPos = -1;
        return 0;
    }

    // 落点取输入框里 '@' 那个字符的位置（转屏幕坐标），并让浮层落在它下一行。
    HWND hEdit = GetInputEditHwnd();
    POINT anchor;
    anchor.x = 0;
    anchor.y = 0;
    if (hEdit != NULL)
    {
        const LRESULT posRet = ::SendMessage(hEdit, EM_POSFROMCHAR, (WPARAM)atPos, 0);
        anchor.x = (short)LOWORD(posRet);
        anchor.y = (short)HIWORD(posRet);
        // 往下挪一行高，免得浮层压住正在输入的那行文字。
        anchor.y += kMentionAnchorLineHeight;
        ::ClientToScreen(hEdit, &anchor);
    }

    m_atDropdown.EnsureCreated();
    m_atDropdown.SetPickCallback(&CBalloonGroupChatWindow::OnAtPickThunk, this);
    m_atDropdown.Show(anchor, cands, prefix);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnMentionNav(UINT /*uMsg*/, WPARAM wParam,
                                              LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (!m_atDropdown.IsShowing())
    {
        return 0;   // 浮层没显示，这次按键与 @ 无关，放行给 EDIT
    }

    switch (wParam)
    {
    case VK_UP:
        m_atDropdown.MoveSelection(-1);
        return 1;
    case VK_DOWN:
        m_atDropdown.MoveSelection(1);
        return 1;
    case VK_RETURN:
    case VK_TAB:
        m_atDropdown.CommitCurrent();   // 回调里会插入并收起浮层
        return 1;
    case VK_ESCAPE:
        m_atDropdown.Hide();
        m_mentionAtPos = -1;
        return 1;
    default:
        break;
    }
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnMentionWheel(UINT /*uMsg*/, WPARAM wParam,
                                                LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (!m_atDropdown.IsShowing())
    {
        return 0;   // 浮层没显示，这次滚轮与 @ 无关，放行给输入框自己滚
    }
    m_atDropdown.ScrollByWheel((short)wParam);
    return 1;
}

LRESULT CBalloonGroupChatWindow::OnLButtonDownForMention(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                         LPARAM /*lParam*/, BOOL& bHandled)
{
    // 能到本窗口的左键按下，说明点的不是输入框内部（点 EDIT 的鼠标消息由系统直送
    // 那个子窗口）。收起浮层，但不消费这次点击 —— 该点的按钮 / 该选的气泡照常。
    if (m_atDropdown.IsShowing())
    {
        m_atDropdown.Hide();
        m_mentionAtPos = -1;
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnActivateForMention(UINT /*uMsg*/, WPARAM wParam,
                                                      LPARAM lParam, BOOL& bHandled)
{
    // 切到别的窗口时收起浮层 —— 它是独立顶层窗口，不会跟着宿主一起隐藏。
    //
    // 但要排除"失活是因为焦点转给了浮层自己"：lParam 是本次得到 / 失去激活的
    // 那个窗口，等于浮层就说明用户正在点它，此时收起会让这一次点击落空。
    //（浮层已在 WM_MOUSEACTIVATE 返回 MA_NOACTIVATE 从根上避免夺焦点，这里再兜一道。）
    const HWND hOther = (HWND)lParam;
    if (LOWORD(wParam) == WA_INACTIVE
        && m_atDropdown.IsShowing()
        && hOther != m_atDropdown.GetHwnd())
    {
        m_atDropdown.Hide();
        m_mentionAtPos = -1;
    }
    bHandled = FALSE;
    return 0;
}

// 前置声明：本函数定义在稍后的接收路径附近，而发送路径（乐观气泡）更靠前，先声明一次。
static CString ResolveQuoteThumbPath(long long selfUserId, const net::ReplyQuote& q,
                                     balloonchatwnd::ChatMessageList* list);

void CBalloonGroupChatWindow::SendGroupTextPart(const CString& text)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL || m_groupId == 0)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 先生成 clientMsgId 再建气泡：气泡要带上它，回执到达后才能按它找回本行、把服务器
    // msgid 与时间戳回填进去（撤回菜单的时限判断依赖这两个值）。
    std::string clientMsgId = GenClientMsgId();

    // @提醒：以本段最终文本为准重扫，扫不到的候选自动作废（用户可能把插入的
    // "@张三" 删了一半、或在中间插了字，插入时记的偏移已经不准）。
    std::string utf8 = CStringToUtf8(text);
    const std::vector<balloonchatwnd::MentionSpan> ats =
        balloonchatwnd::RescanMentions(utf8, m_pendingMentions);

    // 乐观回显气泡（带当前字体）。此刻 msgid / timestampMs 还未知，留 0 待回执回填。
    balloonchatwnd::ChatMessage echo = balloonchatwnd::MakeTextMessage(true, text);
    echo.font = m_currentFont;
    echo.clientMsgId = clientMsgId;
    // 自己发出的这条也要高亮 @ 片段：把协议的字节口径换算成渲染的字符口径。
    // 自己 @ 的人里若正好有自己，isMe 才为真（一般不会），故仍按本人 uid 判定。
    if (!ats.empty())
    {
        const int64_t selfId = (m_lpFMGClient != NULL)
                             ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
        echo.ats = balloonchatwnd::ConvertSpansForRender(utf8, ats, selfId);
    }
    // 本端乐观气泡也要显示引用块 —— 否则"发出去自己看不到引用、对方却看得到"。
    const int64_t selfForQuote = (m_lpFMGClient != NULL)
                               ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    balloonchatwnd::FillQuoteForRender(m_pendingQuote, echo,
                                       ResolveQuoteThumbPath(selfForQuote, m_pendingQuote, list));
    FillSelfSenderAvatar(m_lpFMGClient, echo);   // 自己气泡显示本端真实头像（字形 / 真图）
    list->AppendMessage(echo);

    // 经网络层把文本发给群（ctype=2、cid=群id）。回执经 cmd_send_message 应答异步回投；
    // 服务端随后向全群 fan-out。带 @ 时走能写 content.at 的重载。
    if (ats.empty())
    {
        CIUSocket::GetInstance().SendGroupTextMessage(m_groupId, clientMsgId, utf8, m_currentFont);
    }
    else
    {
        // atAll 由片段推导，不写死 —— 正文里既然有一段 @所有人，协议的
        // content.at.all 就必须跟着置上，否则对端按标志位判"是否@到我"时会漏。
        CIUSocket::GetInstance().SendGroupTextMessageWithMentions(
            m_groupId, clientMsgId, utf8, ats,
            balloonchatwnd::HasMentionAll(ats), m_currentFont, m_pendingQuote);
    }

    // 引用是"一次性"的：这一条带出去之后就该清掉并收起引用条，
    // 否则后面每条消息都会带同一个引用。
    CancelPendingReply();
    LOG_INFO("群聊窗发送一段文字: group=%lld, clientmsgid=%s.",
             (long long)m_groupId, clientMsgId.c_str());

    // 通知主面板把该群会话冒泡到顶部、刷新预览（自己发出，不计未读）。
    if (m_hostPanel != NULL && ::IsWindow(m_hostPanel))
    {
        ConvSendBumpData* pBump = new ConvSendBumpData();
        pBump->ctype   = 2;                 // 群聊
        pBump->cid     = (long long)m_groupId;
        pBump->msgType = kMsgTypeText;
        // 会话列表摘要：内联表情标记替换成 [表情]、转义斜杠还原，不暴露 /f["id"]。
        pBump->digest  = CStringToUtf8(balloonchat::BodyToDigest(text));
        ::PostMessage(m_hostPanel, FMG_MSG_CONV_SEND_BUMP, 0, (LPARAM)pBump);
    }
}

void CBalloonGroupChatWindow::SendGroupImagePart(int imageIndex)
{
    // 第 imageIndex 个内嵌图（文档顺序）对应 m_pendingShotPaths[imageIndex]（插入顺序）。
    // 直接发截图浮层存下的原 PNG 文件——映射靠"插入顺序↔文档顺序"，详见 OnScreenshotStage。
    if (imageIndex < 0 || imageIndex >= (int)m_pendingShotPaths.size())
    {
        LOG_WARNING("群聊窗发送：内嵌图序号 %d 无对应源文件（图序与追踪不一致），跳过。",
                    imageIndex);
        return;
    }
    SendImageFile(m_pendingShotPaths[imageIndex]);
}

void CBalloonGroupChatWindow::PickAndSendImage()
{
    // 图片要先上传到图片服务器再发消息，离线时两步都做不了。
    if (!CheckOnlineFor(balloonmain::kActionTransferFile))
    {
        return;
    }

    // 调试入口（无群绑定）不支持真实发图。
    if (m_groupId == 0)
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

void CBalloonGroupChatWindow::SendImageFile(const std::wstring& path)
{
    // 真实群会话才发图（调试入口无群绑定）。截图「发送」与选图发送共用本函数。
    if (m_groupId == 0)
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
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 乐观回显：先插一条"上传中"的图片气泡（显示本地图），再交工作线程上传。
    std::string clientMsgId = GenClientMsgId();
    balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeImageMessage(
        true, CString(path.c_str()), dispW, dispH, clientMsgId, 1 /*上传中*/);
    // 头像沿用本端真实头像（字形 / 真图，按本端 (facetype, customface) 解析）。
    FillSelfSenderAvatar(m_lpFMGClient, msg);
    list->AppendMessage(msg);

    // 交图片传输工作线程：申请上传→传字节→发图片消息（scene=2 群聊、cid=群 id）。
    // 进度 / 结果经 FMG_MSG_V2_IMAGE_SEND_PROGRESS / _RESULT 回投，主面板再路由回本窗口。
    net::ImageTransfer::GetInstance().EnqueueSend(
        2 /*群聊*/, (int64_t)m_groupId, clientMsgId, path);
    LOG_INFO("群聊窗发图: group=%lld, clientmsgid=%s.", (long long)m_groupId, clientMsgId.c_str());

    // 通知主面板把该群会话冒泡到顶部、刷新预览（"[图片]"）。
    if (m_hostPanel != NULL && ::IsWindow(m_hostPanel))
    {
        ConvSendBumpData* pBump = new ConvSendBumpData();
        pBump->ctype   = 2;                 // 群聊
        pBump->cid     = (long long)m_groupId;
        pBump->msgType = kMsgTypeImage;
        pBump->digest  = net::MessageProtocolClient::digestForMsgType(kMsgTypeImage, std::string());
        ::PostMessage(m_hostPanel, FMG_MSG_CONV_SEND_BUMP, 0, (LPARAM)pBump);
    }
}

void CBalloonGroupChatWindow::DoScreenshot()
{
    // 调试入口（无群绑定）截图无处可发，直接不弹。
    if (m_groupId == 0)
    {
        return;
    }
    // 模态发起截图：复制 / 保存由浮层内部完成；「发送」立即发图、「发送到对话框」
    // 把截图内联插入输入框（经下面两个回调）。
    balloonscreenshot::CScreenCaptureOverlay overlay;
    overlay.Run(m_hWnd, &CBalloonGroupChatWindow::OnScreenshotSend,
                &CBalloonGroupChatWindow::OnScreenshotStage, this);
}

void CBalloonGroupChatWindow::OnScreenshotSend(void* ctx, const wchar_t* pngPath)
{
    CBalloonGroupChatWindow* self = static_cast<CBalloonGroupChatWindow*>(ctx);
    if (self == NULL || pngPath == NULL || pngPath[0] == L'\0')
    {
        return;
    }
    // 截图「发送」走与选图发送同一条链路（乐观气泡 + imgserver 上传 + 会话冒泡）。
    self->SendImageFile(pngPath);
}

void CBalloonGroupChatWindow::OnScreenshotStage(void* ctx, const wchar_t* pngPath)
{
    CBalloonGroupChatWindow* self = static_cast<CBalloonGroupChatWindow*>(ctx);
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
        content->FindCtrlById(balloonchatwnd::kGIdInputEdit));
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

void CBalloonGroupChatWindow::OnImageSendProgress(const std::string& clientMsgId,
                                                  long long sent, long long total)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
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

void CBalloonGroupChatWindow::OnImageSendResult(bool ok, const std::string& clientMsgId,
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
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
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

void CBalloonGroupChatWindow::OnFileSendProgress(const std::string& clientMsgId,
                                                 long long sent, long long total)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
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

void CBalloonGroupChatWindow::OnFileSendResult(bool ok, bool cancelled, const std::string& clientMsgId)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
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

void CBalloonGroupChatWindow::OnFileDownloadProgress(long long msgid, long long received, long long total)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
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

void CBalloonGroupChatWindow::OnFileDownloaded(long long msgid, const std::wstring& localPath)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list != NULL)
    {
        list->SetFileDownloadedByMsgid(msgid, CString(localPath.c_str()));
    }
}

void CBalloonGroupChatWindow::OnFileDownloadFailed(long long msgid, bool cancelled)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
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

void CBalloonGroupChatWindow::RenderIncomingGroupImage(balloonchatwnd::ChatMessageList* list,
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

    // 当前登录用户 userid：缓存按账号隔离（Users\<userId>\ChatImage\），先取好。
    UINT    selfId = 0;
    CString selfName;
    if (m_lpFMGClient != NULL)
    {
        selfId   = m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID;
        selfName = m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName.c_str();
    }

    // 发送人显示信息：自己用本端头像；他人取群成员映射（查不到回退"用户<id>"）。
    // 群聊气泡显示发送人名（仅他人），与文本气泡同口径。真图头像取成员映射里解析好的路径。
    CString  senderName;
    CString  glyph;
    COLORREF color;
    CString  imagePath;   // 发送人真图头像本地路径（命中缓存才非空）
    if (fromMe)
    {
        glyph = selfName.IsEmpty() ? CString(_T("我")) : selfName.Left(1);
        color = AvatarColorForUser(selfId);
    }
    else
    {
        color = AvatarColorForUser(notify.m_from);
        std::map<int64_t, MemberInfo>::iterator it = m_memberInfo.find(notify.m_from);
        if (it != m_memberInfo.end())
        {
            senderName = it->second.m_name;
            color      = it->second.m_color;
            glyph      = it->second.m_glyph;       // 字形头像显示成员选定字形（否则下面回退首字）
            imagePath  = it->second.m_imagePath;   // 真图头像命中缓存的本地路径
        }
        if (senderName.IsEmpty())
        {
            senderName.Format(_T("用户%lld"), (long long)notify.m_from);
        }
        if (glyph.IsEmpty())
        {
            glyph = senderName.Left(1);
        }
    }

    // 缓存命中检查：图片按内容 MD5 缓存在本地账号目录，命中则直接渲染原图、不重复下载
    // （否则每次渲染历史 / 收推送都重下，把 imgserver 刷爆、还堵住串行发送队列）。用文件
    // 实际大小与期望大小比对兜底；期望缺失(<=0)时退回只判存在。与单聊同口径。
    std::wstring cachePath = net::ImageTransfer::CacheFilePathForMd5(
        (int64_t)selfId, notify.m_imageMd5);
    bool cached = false;
    if (!cachePath.empty())
    {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (::GetFileAttributesExW(cachePath.c_str(), GetFileExInfoStandard, &fad)
            && (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            if (notify.m_imageSize <= 0)
            {
                cached = true;
            }
            else
            {
                ULONGLONG actualBytes =
                    (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                cached = (actualBytes == static_cast<ULONGLONG>(notify.m_imageSize));
            }
        }
    }

    balloonchatwnd::ChatMessage msg;
    if (cached)
    {
        msg = balloonchatwnd::MakeImageMessage(
            fromMe, CString(cachePath.c_str()), dispW, dispH, std::string(), 0 /*正常*/);
        msg.msgid = (long long)notify.m_msgid;
    }
    else
    {
        msg = balloonchatwnd::MakeDownloadingImageMessage(
            fromMe, dispW, dispH, (long long)notify.m_msgid);
    }
    // 记下原图的 fileid / md5：本条消息将来被别人引用时，要把它们写进 content.quote，
    // 收方才能在引用块里画出缩略图（按 md5 找本地缓存 / 按 fileid 下载）。
    msg.imageFileId = Utf8ToCString(notify.m_imageFileId);
    msg.imageMd5    = Utf8ToCString(notify.m_imageMd5);

    if (fromMe)
    {
        // 自己发的图：本端真实头像（字形 / 真图），不显示发送人名。
        FillSelfSenderAvatar(m_lpFMGClient, msg);
    }
    else
    {
        msg.senderName            = senderName;
        msg.senderUserId          = notify.m_from;
        msg.senderAvatarText      = glyph;
        msg.senderAvatarColor     = color;
        msg.senderAvatarImagePath = imagePath;
    }
    list->AppendMessage(msg);

    if (cached)
    {
        LOG_INFO("群聊窗收到图片(缓存命中,免下载): msgid=%lld, md5=%s.",
                 (long long)notify.m_msgid, notify.m_imageMd5.c_str());
        return;
    }
    // 未命中才触发原图下载（fileid 来自消息 content）。结果按 msgid 回投，落地到当前
    // 登录账号缓存目录（selfId）。
    if (!notify.m_imageFileId.empty())
    {
        net::ImageTransfer::GetInstance().EnqueueDownload(
            (int64_t)notify.m_msgid, notify.m_imageFileId,
            notify.m_imageMd5, notify.m_imageSize, (int64_t)selfId);
    }
}

void CBalloonGroupChatWindow::OnImageDownloaded(long long msgid, const std::wstring& localPath)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list != NULL)
    {
        list->SetImageDownloadedByMsgid(msgid, CString(localPath.c_str()));
    }
}

void CBalloonGroupChatWindow::OnImageDownloadFailed(long long msgid)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list != NULL)
    {
        list->MarkImageDownloadFailedByMsgid(msgid);
    }
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
    const std::wstring cached = net::ImageTransfer::CacheFilePathForMd5(selfUserId, q.m_md5);
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

void CBalloonGroupChatWindow::OnRecvGroupMessage(const net::MessageNotify& notify)
{
    UINT selfId = (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;

    // 图片消息：插"下载中"气泡（对方发，fromMe=false）并触发原图下载；下载完成后经
    // FMG_MSG_V2_IMAGE_DOWNLOAD_RESULT 回投、按 msgid 回填。自己发的会被服务端 fan-out
    // 回来，本端已乐观回显过，跳过避免重复。
    if (notify.m_msgtype == kMsgTypeImage)
    {
        if (selfId != 0 && (UINT)notify.m_from == selfId)
        {
            return;   // 自己发的群图已乐观回显，忽略 fan-back
        }
        balloonwjui::DuiControl* content = GetClientContent();
        if (content == NULL)
        {
            return;
        }
        balloonchatwnd::ChatMessageList* list =
            dynamic_cast<balloonchatwnd::ChatMessageList*>(
                content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
        if (list == NULL)
        {
            return;
        }
        // 去重：该图（同 msgid）已在列表里（如历史已先渲染）则不重复追加。
        if (list->HasImageMsgid((long long)notify.m_msgid))
        {
            LOG_INFO("群聊窗收到图片已存在，跳过重复追加: msgid=%lld.", (long long)notify.m_msgid);
            return;
        }
        RenderIncomingGroupImage(list, false, notify);
        LOG_INFO("群聊窗收到图片消息: group=%lld, from=%lld, msgid=%lld.",
                 (long long)m_groupId, (long long)notify.m_from, (long long)notify.m_msgid);
        return;
    }

    // 其余非文本类型：解析层只在文本类型时填 m_text，空即非文本（暂不渲染）。
    if (notify.m_text.empty())
    {
        LOG_INFO("群聊窗收到非文本消息，暂不渲染: from=%lld, msgtype=%d.",
                 (long long)notify.m_from, notify.m_msgtype);
        return;
    }

    // 自己发出的文本会被服务端 fan-out 回来（多端同步）；本端已乐观回显过，
    // 避免重复，这里跳过 from==自己 的回显。
    if (selfId != 0 && (UINT)notify.m_from == selfId)
    {
        return;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 发送人显示信息：取自群成员映射（card→nickname），查不到回退"用户<id>"。
    CString senderName;
    CString senderGlyph;
    CString senderImagePath;   // 发送人真图头像本地路径（命中缓存才非空）
    COLORREF senderColor = AvatarColorForUser(notify.m_from);
    std::map<int64_t, MemberInfo>::iterator it = m_memberInfo.find(notify.m_from);
    if (it != m_memberInfo.end())
    {
        senderName      = it->second.m_name;
        senderGlyph     = it->second.m_glyph;       // 字形头像显示成员选定字形
        senderColor     = it->second.m_color;
        senderImagePath = it->second.m_imagePath;   // 真图头像命中缓存的本地路径
    }
    if (senderName.IsEmpty())
    {
        senderName.Format(_T("用户%lld"), (long long)notify.m_from);
    }
    if (senderGlyph.IsEmpty())
    {
        senderGlyph = senderName.Left(1);
    }

    // 渲染为对方气泡（fromMe=false），并设真实发送人名 / 头像（群聊气泡显示发送人）。
    // 按该条消息携带的字体渲染（对端设过字体时与其一致）。
    balloonchatwnd::ChatMessage msg =
        balloonchatwnd::MakeTextMessage(false, Utf8ToCString(notify.m_text));
    msg.font                  = notify.m_font;
    //撤回判定所需：服务器 msgid 与消息时间，按报文原样带上。
    msg.msgid                 = notify.m_msgid;
    msg.timestampMs           = notify.m_timestamp;
    msg.senderName            = senderName;
    msg.senderUserId          = notify.m_from;
    msg.senderAvatarText      = senderGlyph;
    msg.senderAvatarColor     = senderColor;
    msg.senderAvatarImagePath = senderImagePath;
    // @提醒：把协议的 UTF-8 字节偏移换算成渲染用的 UTF-16 字符下标，并按本人 uid
    // 标好 isMe（决定气泡里用橙色还是蓝色）。本条不含 @ 时为空，气泡走原快路径。
    // 与 OnHistoryMessages 走同一个 BuildRenderSpansForMessage，两条路不会再跑偏。
    msg.ats = balloonchatwnd::BuildRenderSpansForMessage(
        notify.m_text, notify.m_ats, notify.m_atAll, (int64_t)selfId);
    // 引用回复：与历史渲染共用 FillQuoteForRender，两条路不会跑偏（@ 那次的教训）。
    balloonchatwnd::FillQuoteForRender(notify.m_quote, msg,
                                       ResolveQuoteThumbPath((int64_t)selfId, notify.m_quote, list));
    list->AppendMessage(msg);

    LOG_INFO("群聊窗渲染收到的群消息: group=%lld, from=%lld, msgid=%lld.",
             (long long)m_groupId, (long long)notify.m_from, (long long)notify.m_msgid);
}

void CBalloonGroupChatWindow::OnGroupMembersArrived(const net::GroupMembersResult& members)
{
    int64_t selfId = (m_lpFMGClient != NULL)
                     ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;

    // 建"发送人 userid → 昵称/头像"映射（群名片优先→账号昵称→"用户<id>"）。
    m_memberInfo.clear();
    for (size_t i = 0; i < members.m_members.size(); ++i)
    {
        const net::GroupMemberItem& m = members.m_members[i];
        CString name = Utf8ToCString(m.m_card);
        if (name.IsEmpty())
        {
            name = Utf8ToCString(m.m_nickname);
        }
        if (name.IsEmpty())
        {
            name.Format(_T("用户%lld"), (long long)m.m_userid);
        }
        // 按成员 (facetype, customface) 解析头像：字形 / 自动头像给字形 + 底色，真图头像
        // 缓存命中给真图路径、未命中入队下载（完成后 OnAvatarDownloaded 回刷）。
        balloonchatwnd::AvatarRender ar = balloonchatwnd::ResolveAvatarRender(
            m.m_userid, m.m_facetype, m.m_customface, name, selfId);
        MemberInfo info;
        info.m_name       = name;
        info.m_glyph      = ar.m_glyph;
        info.m_color      = ar.m_color;
        info.m_facetype   = m.m_facetype;
        info.m_customface = m.m_customface;
        info.m_imagePath  = ar.m_imagePath;
        //气泡里 @某人 的右键菜单要按角色决定管理项，故一并记下（见 MemberRoleThunk）
        info.m_role       = m.m_role;
        m_memberInfo[m.m_userid] = info;
    }

    // 填右侧成员侧栏：清空旧成员行后按真实成员逐个挂（群主用 role==2 标徽标）。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::GroupMemberList* list =
        dynamic_cast<balloonchatwnd::GroupMemberList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMemberList));
    if (list != NULL)
    {
        // 先清空既有成员行（真实群起始为空；防御性支持成员刷新重填）。
        while (!list->Children().empty())
        {
            list->RemoveChild(list->Children()[0].get());
        }
        // 显示顺序：群主 → 管理员 → 普通成员，同一角色内保持服务端返回的原顺序。
        // 服务端不保证按角色排，客户端自己排；抽成纯函数是为了能单测（见 GroupMemberOrder）。
        std::vector<int> roles;
        roles.reserve(members.m_members.size());
        for (size_t i = 0; i < members.m_members.size(); ++i)
        {
            roles.push_back(members.m_members[i].m_role);
        }
        const std::vector<std::size_t> order =
            balloonchatwnd::OrderMemberIndicesByRole(roles);

        for (size_t k = 0; k < order.size(); ++k)
        {
            const net::GroupMemberItem& m = members.m_members[order[k]];
            std::map<int64_t, MemberInfo>::iterator mi = m_memberInfo.find(m.m_userid);
            CString name = (mi != m_memberInfo.end()) ? mi->second.m_name : CString();
            CString glyph = (mi != m_memberInfo.end() && !mi->second.m_glyph.IsEmpty())
                                ? mi->second.m_glyph : name.Left(1);
            COLORREF color = (mi != m_memberInfo.end()) ? mi->second.m_color
                                                        : AvatarColorForUser(m.m_userid);
            CString imagePath = (mi != m_memberInfo.end()) ? mi->second.m_imagePath : CString();
            std::unique_ptr<balloonchatwnd::GroupMemberItem> item(
                new balloonchatwnd::GroupMemberItem());
            item->SetUserId(m.m_userid);   // 真图下载完成后按它定位本行刷新
            // 本人那一行：右键菜单只留「查看资料」，与气泡里 @ 自己的处理一致。
            item->SetIsSelf(selfId != 0 && m.m_userid == selfId);
            // m_role 就是网络侧角色取值，与 grouprole::NetGroupRole 同一套，直接透传。
            item->SetMember(name, glyph, color, m.m_role, imagePath);
            // 本人角色决定这一行的右键菜单里有没有「设为 / 撤销管理员」（仅群主可见）。
            item->SetMyNetRole(m_myRole);
            list->AddChild(std::unique_ptr<balloonwjui::DuiControl>(item.release()));
        }
        // 重新布局成员侧栏并重绘。
        RECT rc = list->GetRect();
        if (rc.right > rc.left && rc.bottom > rc.top)
        {
            list->ForceLayout(rc);
        }
        if (IsWindow())
        {
            ::InvalidateRect(m_hWnd, NULL, FALSE);
        }
    }

    // 成员到齐 → 边栏「成员」Tab 换成真实人数（"成员 12"）。群成员变动推送会让主面板
    // 重新拉一次成员（见 CBalloonMainPanel::OnGroupMemberChanged），故人数能一直是活的。
    RefreshSideTabCounts();

    LOG_INFO("群聊窗成员已填充: group=%lld, 成员 %u 人。",
             (long long)m_groupId, (uint32_t)members.m_members.size());

    // 成员映射就绪后再拉最近一页历史消息，确保历史里的发送人能解析出真实昵称。
    // 更早的消息由用户往上滚时按页续拉（见 OnChatLoadOlder）。
    // 应答经主面板路由回 OnHistoryMessages。
    if (m_groupId != 0)
    {
        CIUSocket::GetInstance().RequestSyncMessages(
            2, m_groupId, 0, balloonchatwnd::kHistoryPageSize);

        // 再拉一次本群被 Pin 的消息。上面那次只覆盖"最近一页"，而被钉的往往恰恰是
        // 更早的老消息 —— 不单独拉一次，历史里那些气泡就不会带图钉标记。应答经主面板
        // 的 OnPinnedMessagesArrived 落库并回投到本窗口。
        CIUSocket::GetInstance().GetPinnedMessages(conversation_group, m_groupId);
    }
}

void CBalloonGroupChatWindow::OnAvatarDownloaded(const std::string& fileid)
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

    // 刚下好的真图缓存路径（取不到则不刷）。
    std::wstring path = net::ImageTransfer::AvatarCacheFilePathForFileId(selfId, fileid);
    if (path.empty() || ::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        return;
    }
    CString imagePath = path.c_str();

    //刚下好的若是本群的群头像，重建标题栏 / 任务栏图标（此前画的是字形占位）。
    if (!m_groupCustomface.empty() && m_groupCustomface == fileid)
    {
        RebuildGroupIcons();
    }

    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;

    // 成员映射里 customface 命中该 fileid 的成员：回填其真图路径 + 刷新气泡 + 成员侧栏。
    for (std::map<int64_t, MemberInfo>::iterator it = m_memberInfo.begin();
         it != m_memberInfo.end(); ++it)
    {
        if (it->second.m_facetype != balloonmain::avatar_kind_image
            || it->second.m_customface != fileid)
        {
            continue;
        }
        it->second.m_imagePath = imagePath;
        if (list != NULL)
        {
            list->RefreshSenderAvatar(it->first, imagePath);
        }
    }

    // 本人真图头像命中（自己的头像通常本地已缓存，少经此路径）：刷新本人气泡。
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

    // 成员侧栏：按 userid 找到引用该真图头像的成员行，更新其真图路径并重绘。
    if (content != NULL)
    {
        balloonchatwnd::GroupMemberList* memberList =
            dynamic_cast<balloonchatwnd::GroupMemberList*>(
                content->FindCtrlById(balloonchatwnd::kGIdMemberList));
        if (memberList != NULL)
        {
            const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids =
                memberList->Children();
            for (size_t i = 0; i < kids.size(); ++i)
            {
                balloonchatwnd::GroupMemberItem* item =
                    dynamic_cast<balloonchatwnd::GroupMemberItem*>(kids[i].get());
                if (item == NULL)
                {
                    continue;
                }
                std::map<int64_t, MemberInfo>::iterator mi =
                    m_memberInfo.find(item->UserId());
                if (mi != m_memberInfo.end()
                    && mi->second.m_facetype == balloonmain::avatar_kind_image
                    && mi->second.m_customface == fileid)
                {
                    item->SetAvatarImage(imagePath);
                }
            }
            memberList->Invalidate();
        }
    }
}

void CBalloonGroupChatWindow::LocateMessage(long long msgid)
{
    m_pendingLocateMsgid = 0;
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list == NULL || !list->ScrollToMsgid(msgid))
    {
        //此刻没找到（历史还在途 / 该消息较早未加载）：记为待定，等历史渲染后补定位。
        m_pendingLocateMsgid = msgid;
    }
}

void CBalloonGroupChatWindow::OnHistoryMessages(const net::SyncMessagesResult& result)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    // 本次应答是首屏还是"往上翻的一页"？服务端应答里不回显 before，只能靠本端
    // 记的在途标志区分 —— 只有 OnChatLoadOlder 发出的翻页请求会把它置真。
    const bool isOlderPage = m_paging.m_loading;

    if (isOlderPage)
    {
        // 翻页：绝不能清空。开一个"头部插入批次"，下面照常按时间正序渲染，
        // 批次结束时这一段会被整体挪到列表头部，滚动位置也一并补偿。
        list->BeginPrependBatch();
    }
    else
    {
        // 首屏是"最近 N 条"完整快照：渲染前先清空列表，避免与窗口刚创建时（拉历史
        // 仍在途中）收到推送、被 OnRecvGroupMessage 乐观追加进来的那几行叠加，导致
        // 这些 live 行卡在最前面 / 重复。清空后按历史时间正序重渲，位置即正确。
        // 与单聊窗 CBalloonChatWindow::OnHistoryMessages 同一套处理。
        list->ClearAllMessages();
    }

    UINT selfId = (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;

    // 服务端以 msgid 升序（时间正序、最旧在前）返回 —— 见 MessageManager.cpp 翻历史
    // 模式把 DESC 查询结果反转为升序（out.assign(rows.rbegin(), rows.rend())），
    // buildSyncMessagesResponse 再按该升序数组原样写 JSON。实测亦然：某群 22 条历史
    // 的 first.msgid=14、last.msgid=168。
    //
    // 故这里必须<u>正序</u>遍历，才得到"最旧在上、最新在下"。此处原先照"最新在前"
    // 倒序遍历，与服务端的升序叠加成两次反转，最新消息被追加到列表最顶端 —— 表现
    // 为重启后打开群聊，滚到底也找不到刚发的那条（它跑到最上面去了）。
    for (size_t k = 0; k < result.m_messages.size(); ++k)
    {
        const net::MessageNotify& m = result.m_messages[k];
        bool fromMe = (selfId != 0 && (UINT)m.m_from == selfId);

        // 图片消息：插"下载中"气泡并触发下载（历史里自己发的图也需下载，本端无原图）。
        if (m.m_msgtype == kMsgTypeImage)
        {
            RenderIncomingGroupImage(list, fromMe, m);
            continue;
        }

        // 文本取正文；非文本用占位文字渲染成文本气泡，避免历史空洞。
        CString text;
        if (!m.m_text.empty())
        {
            text = Utf8ToCString(m.m_text);
        }
        else
        {
            text = Utf8ToCString(net::MessageProtocolClient::digestForMsgType(m.m_msgtype, m.m_text));
        }

        if (selfId != 0 && (UINT)m.m_from == selfId)
        {
            // 自己发的：右侧本端气泡（群聊本端气泡不显示发送人名）。本端真实头像（字形 / 真图）。
            balloonchatwnd::ChatMessage mine = balloonchatwnd::MakeTextMessage(true, text);
            mine.font = m.m_font;
            //历史消息同样要带 msgid 与时间：重开窗口后仍在两分钟内的消息应可撤回。
            mine.msgid = m.m_msgid;
            mine.timestampMs = m.m_timestamp;
            //@提醒同样要还原 —— 历史响应里 content.at 是带回来的（服务端把整个
            //content 原样落库、fillMessageFromObject 也解析了），漏填的话重开窗口
            //后 @ 的高亮就全没了。与实时推送共用 BuildRenderSpansForMessage。
            mine.ats = balloonchatwnd::BuildRenderSpansForMessage(
                m.m_text, m.m_ats, m.m_atAll, (int64_t)selfId);
            //引用回复同样要还原，理由与 ats 相同：漏填的话重开窗口后引用块就没了。
            balloonchatwnd::FillQuoteForRender(m.m_quote, mine,
                ResolveQuoteThumbPath((int64_t)selfId, m.m_quote, list));
            FillSelfSenderAvatar(m_lpFMGClient, mine);
            list->AppendMessage(mine);
        }
        else
        {
            // 对方发的：左侧气泡 + 真实发送人名/头像（取自群成员映射，查不到回退"用户<id>"）。
            CString senderName;
            CString senderGlyph;
            CString senderImagePath;   // 发送人真图头像本地路径（命中缓存才非空）
            COLORREF senderColor = AvatarColorForUser(m.m_from);
            std::map<int64_t, MemberInfo>::iterator it = m_memberInfo.find(m.m_from);
            if (it != m_memberInfo.end())
            {
                senderName      = it->second.m_name;
                senderGlyph     = it->second.m_glyph;       // 字形头像显示成员选定字形
                senderColor     = it->second.m_color;
                senderImagePath = it->second.m_imagePath;   // 真图头像命中缓存的本地路径
            }
            if (senderName.IsEmpty())
            {
                senderName.Format(_T("用户%lld"), (long long)m.m_from);
            }
            if (senderGlyph.IsEmpty())
            {
                senderGlyph = senderName.Left(1);
            }
            balloonchatwnd::ChatMessage msg = balloonchatwnd::MakeTextMessage(false, text);
            msg.font                  = m.m_font;
            msg.msgid                 = m.m_msgid;
            msg.timestampMs           = m.m_timestamp;
            msg.senderName            = senderName;
            msg.senderUserId          = m.m_from;
            msg.senderAvatarText      = senderGlyph;
            msg.senderAvatarColor     = senderColor;
            msg.senderAvatarImagePath = senderImagePath;
            //@提醒还原，理由同上面自己发的那一支。isMe 按<u>当前</u>登录用户重新判定，
            //所以换个账号登录看同一条历史，高亮配色会随之变化，这是预期行为。
            msg.ats = balloonchatwnd::BuildRenderSpansForMessage(
                m.m_text, m.m_ats, m.m_atAll, (int64_t)selfId);
            balloonchatwnd::FillQuoteForRender(m.m_quote, msg,
                ResolveQuoteThumbPath((int64_t)selfId, m.m_quote, list));
            list->AppendMessage(msg);
        }
    }
    // 本批最早那条的 msgid，作为下一页的 before 锚点。服务端按时间正序返回，
    // 故取第一条；本批为空时给 0，表示锚点不变（见 OnOlderPageLoaded）。
    const long long oldestInBatch = result.m_messages.empty()
                                  ? 0
                                  : (long long)result.m_messages[0].m_msgid;

    if (isOlderPage)
    {
        list->EndPrependBatch();
        balloonchatwnd::OnOlderPageLoaded(m_paging, oldestInBatch, result.m_hasmore);
        LOG_INFO("群聊窗向上翻页已渲染: group=%lld, 本页 %u 条, 最早 msgid=%lld, hasmore=%d.",
                 (long long)m_groupId, (uint32_t)result.m_messages.size(),
                 (long long)oldestInBatch, result.m_hasmore ? 1 : 0);
        return;   // 翻页不滚到底、也不参与"定位"补办
    }

    balloonchatwnd::OnFirstPageLoaded(m_paging, oldestInBatch, result.m_hasmore);
    list->ScrollToBottom();
    // 历史已渲染：若有从历史窗"定位"过来、当时消息还没加载的待办，现在补定位，成功则清。
    if (m_pendingLocateMsgid != 0 && list->ScrollToMsgid(m_pendingLocateMsgid))
    {
        m_pendingLocateMsgid = 0;
    }
    // 一并记下首尾 msgid：服务端约定按 msgid 升序（最旧在前）返回，本函数据此正序
    // 渲染。这两个值就是该约定的现场凭据 —— 正常应有 first < last；哪天两边的顺序
    // 约定又对不上（历史上出现过：这里曾按"最新在前"倒序遍历，与服务端的升序叠成
    // 两次反转，最新消息被排到列表最顶端，表现为重开窗口后滚到底也找不到刚发的那
    // 条），看这一行就能立刻发现，不必再埋探针。
    long long firstMsgid = 0;
    long long lastMsgid  = 0;
    if (!result.m_messages.empty())
    {
        firstMsgid = (long long)result.m_messages.front().m_msgid;
        lastMsgid  = (long long)result.m_messages.back().m_msgid;
    }
    LOG_INFO("群聊窗渲染历史消息: group=%lld, count=%u, first.msgid=%lld, last.msgid=%lld.",
             (long long)m_groupId, (uint32_t)result.m_messages.size(),
             firstMsgid, lastMsgid);
}

void CBalloonGroupChatWindow::SwitchSideTab(UINT tabId)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 三个 Tab 中被点的设为选中，其余取消选中。
    const UINT kTabIds[3] =
    {
        balloonchatwnd::kGIdTabMembers,
        balloonchatwnd::kGIdTabFiles,
        balloonchatwnd::kGIdTabNotice,
    };
    for (int i = 0; i < 3; ++i)
    {
        balloonchatwnd::GroupSideTab* tab =
            dynamic_cast<balloonchatwnd::GroupSideTab*>(
                content->FindCtrlById(kTabIds[i]));
        if (tab != NULL)
        {
            tab->SetActive(kTabIds[i] == tabId);
        }
    }

    // 切换边栏下部的内容容器：成员 Tab → 成员面板；群文件 Tab → 群文件面板；
    // 公告 Tab → 公告面板。三者互斥地占同一个槽位。
    balloonwjui::DuiControl* membersPanel =
        content->FindCtrlById(balloonchatwnd::kGIdMembersPanel);
    balloonwjui::DuiControl* filesPanel =
        content->FindCtrlById(balloonchatwnd::kGIdFilesPanel);
    balloonwjui::DuiControl* noticePanel =
        content->FindCtrlById(balloonchatwnd::kGIdNoticePanel);
    const bool showMembers = (tabId == balloonchatwnd::kGIdTabMembers);
    const bool showFiles   = (tabId == balloonchatwnd::kGIdTabFiles);
    const bool showNotice  = (tabId == balloonchatwnd::kGIdTabNotice);

    // 三个容器占边栏下部的同一槽位。切换前先抓住当前可见容器的矩形作槽位——
    // 成员容器一开始就可见、已布局过，矩形可靠；据此给切换后新显示的容器
    // 直接 ForceLayout，绕开「切可见性后子控件停 {0,0,0,0}」的 balloonui 布局 gotcha
    // （见记忆 reference-balloonui-layout-gotchas：须对容器逐个 ForceLayout）。
    RECT slot = { 0, 0, 0, 0 };
    if (membersPanel != NULL && membersPanel->IsVisible())
    {
        slot = membersPanel->GetRect();
    }
    else if (filesPanel != NULL && filesPanel->IsVisible())
    {
        slot = filesPanel->GetRect();
    }
    else if (noticePanel != NULL && noticePanel->IsVisible())
    {
        slot = noticePanel->GetRect();
    }

    if (membersPanel != NULL)
    {
        membersPanel->SetVisible(showMembers);
    }
    if (filesPanel != NULL)
    {
        filesPanel->SetVisible(showFiles);
    }
    if (noticePanel != NULL)
    {
        noticePanel->SetVisible(showNotice);
    }

    // 用槽位矩形强制重排新显示的容器（其子控件随之就位）。槽位无效时退回整树重排兜底。
    const bool slotValid = (slot.right > slot.left && slot.bottom > slot.top);
    if (slotValid && showMembers && membersPanel != NULL)
    {
        membersPanel->ForceLayout(slot);
    }
    else if (slotValid && showFiles && filesPanel != NULL)
    {
        filesPanel->ForceLayout(slot);
    }
    else if (slotValid && showNotice && noticePanel != NULL)
    {
        noticePanel->ForceLayout(slot);
    }
    else
    {
        content->ForceLayout(content->GetRect());
    }

    // 切到群文件 Tab：渲染面板。真实群且尚未拉取时先触发拉全量（异步，应答回来由
    // OnGroupFilesArrived 再渲染——增量3c 接入）；mock 调试入口已在 InitUi 预填库，
    // 此处直接渲染（RenderFilePanel 内部会再按面板矩形重排一次列表）。
    if (showFiles)
    {
        // 兜底触发一次拉取：正常情况下开窗时已经拉过（见 CreateAndShow），此处多半直接
        // 返回；只有开窗那次因故没发出去时才在这里补上。先渲染一次（库为空时显示空列表
        // + "共 0"作加载态），数据到位后 OnGroupFilesArrived 会重渲染。
        StartLoadGroupFiles();
        RenderFilePanel();
    }

    // 切到公告 Tab：首次进来拉一次公告（懒加载，与群文件同一套做法），之后靠
    // 1326 推送更新。先渲染一次（此刻列表为空，显示"正在加载"），数据到位后
    // OnAnnouncementsArrived 会重渲染。
    if (showNotice)
    {
        if (m_groupId != 0 && !m_noticeLoaded && !m_noticeLoading)
        {
            //首次进来看最新的那条（第 0 条），同时拿回总条数
            RequestNoticeAt(0);
        }
        RenderNoticePanel();
    }

    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonGroupChatWindow::RequestNoticeAt(int index)
{
    if (m_groupId == 0 || m_noticeLoading)
    {
        return;
    }
    if (index < 0)
    {
        index = 0;
    }

    m_noticeLoading = true;
    //一次只要一条：offset 走当前下标、count 取 1。公告有几千条也不受服务端
    //单页上限影响，本地也只留手上这一条。
    //记下 offset 供应答认领 —— 群管理窗也在拉公告，两边页大小不同，不比对
    //就会把对方那份收下。
    m_noticeReqOffset = index;
    CIUSocket::GetInstance().SendGetGroupAnnouncements(
        m_groupId, index, kNoticeFetchPageSize);
}

void CBalloonGroupChatWindow::OnAnnouncementsArrived(const net::GroupAnnouncementsResult& result)
{
    //先认领：群管理窗也在拉同一个群的公告，两边页大小不同，收下对方那份会让
    //边栏显示成别人要的内容（见 m_noticeReqOffset 的说明）。
    if (!result.MatchesRequest(m_noticeReqOffset, kNoticeFetchPageSize))
    {
        return;
    }

    m_noticeLoading = false;
    //失败也要落下"已加载" —— 否则面板会一直停在"正在加载"那句话上，用户以为卡死了
    m_noticeLoaded = true;

    if (result.m_code != 0)
    {
        LOG_INFO("群聊窗拉取群公告未通过: groupid=%lld, code=%d。",
                 (long long)m_groupId, result.m_code);
        RenderNoticePanel();
        return;
    }

    m_noticeTotal = result.m_total;

    //本次要的那一条回来了就换上；空的说明这个下标已经越界（比如刚才看的是最后
    //一条而它被删了），把下标夹回最后一条再要一次。
    if (!result.m_announcements.empty())
    {
        m_notice = result.m_announcements[0];
    }
    else if (m_noticeTotal > 0 && m_noticeIndex >= m_noticeTotal)
    {
        m_noticeIndex = m_noticeTotal - 1;
        RequestNoticeAt(m_noticeIndex);
        return;
    }
    else
    {
        //本群一条公告都没有了
        m_notice = net::GroupAnnouncementItem();
        m_noticeIndex = 0;
    }

    RenderNoticePanel();

    LOG_INFO("群聊窗群公告到达: groupid=%lld, index=%d, total=%d。",
             (long long)m_groupId, m_noticeIndex, m_noticeTotal);
}

void CBalloonGroupChatWindow::OnAnnouncementChanged(const net::GroupAnnouncementNotify& notify)
{
    //公告面板还没打开过就什么都不做 —— 用户真去点那个 Tab 时自然会拉到最新的，
    //现在拉一次只是白费一个来回。
    if (m_groupId == 0 || !m_noticeLoaded)
    {
        return;
    }

    //新公告发布时跳到第一条（它排最前，用户多半就想看它）；删除则留在原位，
    //由应答里的 total 与空结果把越界的下标夹回来。
    if (notify.m_changetype == group_announcement_change_publish)
    {
        m_noticeIndex = 0;
    }

    //推送到达时可能正好有一次翻页在途，此时 m_noticeLoading 为真会让这次重拉被
    //丢掉。清掉它 —— 在途那次拿回的是旧数据，本来就该被这次覆盖。
    m_noticeLoading = false;
    RequestNoticeAt(m_noticeIndex);

    LOG_INFO("群聊窗收到群公告变更推送: groupid=%lld, changetype=%d, 重拉第 %d 条。",
             (long long)notify.m_groupid, notify.m_changetype, m_noticeIndex);
}

void CBalloonGroupChatWindow::StepNotice(int delta)
{
    if (m_noticeTotal <= 0)
    {
        return;
    }

    int next = m_noticeIndex + delta;
    //越界就地夹住而不循环：只有两三条时循环翻页会让人分不清自己看到第几条了
    if (next < 0)
    {
        next = 0;
    }
    if (next >= m_noticeTotal)
    {
        next = m_noticeTotal - 1;
    }
    if (next == m_noticeIndex)
    {
        return;
    }

    m_noticeIndex = next;
    //翻到哪条要哪条。请求发出到应答回来之间界面先不动 —— 公告是低频浏览，
    //一个来回的延迟感知不明显，不值得为它加一层本地缓存。
    RequestNoticeAt(m_noticeIndex);
}

void CBalloonGroupChatWindow::RenderNoticePanel()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    balloonwjui::DuiControl* card =
        content->FindCtrlById(balloonchatwnd::kGIdNoticeCard);
    balloonwjui::DuiLabel* empty = dynamic_cast<balloonwjui::DuiLabel*>(
        content->FindCtrlById(balloonchatwnd::kGIdNoticeEmpty));
    balloonwjui::DuiLabel* count = dynamic_cast<balloonwjui::DuiLabel*>(
        content->FindCtrlById(balloonchatwnd::kGIdNoticeCount));
    balloonwjui::DuiLabel* index = dynamic_cast<balloonwjui::DuiLabel*>(
        content->FindCtrlById(balloonchatwnd::kGIdNoticeIndex));

    const bool hasNotice = (m_noticeTotal > 0 && m_notice.m_id > 0);

    //有公告时显示公告卡、藏起空状态提示；没有时反过来
    if (card != NULL)
    {
        card->SetVisible(hasNotice);
    }
    if (empty != NULL)
    {
        empty->SetVisible(!hasNotice);
        //还没拉回来与"拉过了但确实没有公告"要分开说，否则用户会以为公告丢了
        empty->SetText(m_noticeLoaded ? _T("本群还没有群公告") : _T("正在加载公告…"));
    }

    //翻页按钮：只有一条时两个都不可点
    balloonwjui::DuiButton* prev = dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(balloonchatwnd::kGIdNoticePrev));
    balloonwjui::DuiButton* next = dynamic_cast<balloonwjui::DuiButton*>(
        content->FindCtrlById(balloonchatwnd::kGIdNoticeNext));
    if (prev != NULL)
    {
        prev->SetEnabled(m_noticeIndex > 0);
    }
    if (next != NULL)
    {
        next->SetEnabled(m_noticeIndex + 1 < m_noticeTotal);
    }

    if (count != NULL)
    {
        CString text;
        text.Format(_T("群公告 · %d 条"), m_noticeTotal);
        count->SetText(text);
    }
    if (index != NULL)
    {
        CString text;
        //一条都没有时显示 0/0 比 1/0 讲得通
        text.Format(_T("%d/%d"), hasNotice ? (m_noticeIndex + 1) : 0, m_noticeTotal);
        index->SetText(text);
    }

    if (hasNotice)
    {
        const net::GroupAnnouncementItem& a = m_notice;

        balloonwjui::DuiLabel* title = dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kGIdNoticeTitle));
        if (title != NULL)
        {
            title->SetText(Utf8ToCString(a.m_title));
        }

        balloonwjui::DuiLabel* body = dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kGIdNoticeBody));
        if (body != NULL)
        {
            body->SetText(Utf8ToCString(a.m_content));
        }

        balloonwjui::DuiLabel* meta = dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kGIdNoticeMeta));
        if (meta != NULL)
        {
            //发布人昵称由服务端下发（可能已退群，本地查不到）；缺了就只显示时间
            CString who = Utf8ToCString(a.m_publishername);
            CString when = balloongroupmanage::FormatAnnouncementTime(
                a.m_publishtime, NowMillisecondsForNotice());
            CString text;
            if (who.IsEmpty())
            {
                text = when;
            }
            else
            {
                text.Format(_T("%s · %s"), (LPCTSTR)who, (LPCTSTR)when);
            }
            meta->SetText(text);
        }
    }

    //公告面板此刻可能正显示着，改完文本要按当前矩形重排一次（正文是自动换行的
    //多行 label，内容变了行数也会变）。
    balloonwjui::DuiControl* panel =
        content->FindCtrlById(balloonchatwnd::kGIdNoticePanel);
    if (panel != NULL && panel->IsVisible())
    {
        const RECT rc = panel->GetRect();
        if (rc.right > rc.left && rc.bottom > rc.top)
        {
            panel->ForceLayout(rc);
        }
    }

    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonGroupChatWindow::SetAnnouncementTitle(LPCTSTR title)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::GroupChatHeaderInfo* header =
        dynamic_cast<balloonchatwnd::GroupChatHeaderInfo*>(
            content->FindCtrlById(balloonchatwnd::kGIdHeaderInfo));
    if (header != NULL)
    {
        header->SetAnnouncementTitle(title);
    }
}

void CBalloonGroupChatWindow::ActivateNoticeTab()
{
    //复用与用户点 Tab 完全相同的那条路径：懒加载、面板显隐、ForceLayout 都在里面，
    //另写一份迟早会跟它走岔。
    SwitchSideTab(balloonchatwnd::kGIdTabNotice);
}

void CBalloonGroupChatWindow::SetHeaderMemberCount(int count)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::GroupChatHeaderInfo* header =
        dynamic_cast<balloonchatwnd::GroupChatHeaderInfo*>(
            content->FindCtrlById(balloonchatwnd::kGIdHeaderInfo));
    if (header != NULL)
    {
        header->SetMemberCount(count);
    }
}

void CBalloonGroupChatWindow::RenderFilePanel()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::GroupFileList* list =
        dynamic_cast<balloonchatwnd::GroupFileList*>(
            content->FindCtrlById(balloonchatwnd::kGIdFileList));
    if (list == NULL)
    {
        return;
    }

    // 取当前分类 / 搜索 / 页的本页条目与分页信息。
    std::vector<net::GroupFileEntry> items;
    int total = 0;
    int pageCount = 0;
    int curPage = 0;
    m_fileLibrary.Query(m_fileCategory, m_fileSearch, m_filePage, kGroupFilePageSize,
                        items, total, pageCount, curPage);
    m_filePage = curPage;   // 钳定后回写（翻到越界页时跟随收回）

    // 清空旧文件行，按本页条目重建。
    while (!list->Children().empty())
    {
        list->RemoveChild(list->Children()[0].get());
    }

    // 先把「正在上传」的若干笔钉在最顶部（不受分类 / 搜索筛选影响，因为它们还没进库）。
    // m_uploadTracker.Items() 已按最近发起在最前排序，依次 AddChild 即新发起的在最上。
    const std::vector<balloonchatwnd::GroupUploadEntry>& ups = m_uploadTracker.Items();
    for (size_t i = 0; i < ups.size(); ++i)
    {
        const balloonchatwnd::GroupUploadEntry& u = ups[i];
        std::unique_ptr<balloonchatwnd::GroupFileItem> uitem(
            new balloonchatwnd::GroupFileItem());
        // 说明行留空：上传态下 OnPaint 会改显「上传中 NN%」，文件名 / 扩展名用于显示与角标。
        uitem->SetFile(CString(u.m_name.c_str()), CString(), CString(u.m_ext.c_str()));
        uitem->SetUploading(u.m_uploadId, u.m_percent);
        list->AddChild(std::unique_ptr<balloonwjui::DuiControl>(uitem.release()));
    }

    int64_t selfId = (m_lpFMGClient != NULL)
                     ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    std::string selfIdStr = (selfId != 0) ? std::to_string(selfId) : std::string();
    long long nowMs = (long long)time(NULL) * 1000LL;

    for (size_t i = 0; i < items.size(); ++i)
    {
        const net::GroupFileEntry& e = items[i];
        // 上传者：本人显示"我"，否则显示服务端给的昵称。
        CString uploader;
        if (!selfIdStr.empty() && e.m_uploaderid == selfIdStr)
        {
            uploader = _T("我");
        }
        else
        {
            uploader = Utf8ToCString(e.m_uploadername);
        }
        CString timeStr = balloonchatwnd::FormatRelativeDayTime(e.m_uploadtime, nowMs).c_str();
        CString sizeStr = balloonchatwnd::FormatFileSize(e.m_size).c_str();
        // 拼「上传者 · 时间 · 大小」，缺项跳过（避免出现多余的" · "）。
        CString meta = uploader;
        if (!timeStr.IsEmpty())
        {
            if (!meta.IsEmpty())
            {
                meta += _T(" · ");
            }
            meta += timeStr;
        }
        if (!sizeStr.IsEmpty())
        {
            if (!meta.IsEmpty())
            {
                meta += _T(" · ");
            }
            meta += sizeStr;
        }

        std::unique_ptr<balloonchatwnd::GroupFileItem> item(
            new balloonchatwnd::GroupFileItem());
        item->SetFile(Utf8ToCString(e.m_name), meta, Utf8ToCString(e.m_ext));
        item->SetIds(Utf8ToCString(e.m_entryid), Utf8ToCString(e.m_fileid));
        // 删除门控：群主 / 管理员可删群内任意，上传者本人可删自己上传的（与服务端同规则）。
        // 可删行在鼠标悬停时于下载箭头左侧显示垃圾桶。
        bool isOwnUpload = (!selfIdStr.empty() && e.m_uploaderid == selfIdStr);
        item->SetDeletable(
            balloonchatwnd::GroupFileLibrary::CanDeleteGroupFile(m_myRole, isOwnUpload));
        // 若该条目正在下载（翻页 / 推送重渲染前已在途）：把"下载中"态补回这一新建行，
        // 保证进度条与百分比跨重渲染不丢。
        std::map<std::string, int>::const_iterator dlIt =
            m_downloadingFiles.find(e.m_entryid);
        if (dlIt != m_downloadingFiles.end())
        {
            item->SetDownloadProgress(dlIt->second);
        }
        list->AddChild(std::unique_ptr<balloonwjui::DuiControl>(item.release()));
    }

    // 同步滚动视口内容高度，并回到顶部（翻页 / 筛选后从头看）。
    balloonwjui::DuiScrollView* sv =
        dynamic_cast<balloonwjui::DuiScrollView*>(
            content->FindCtrlById(balloonchatwnd::kGIdFileScroll));
    if (sv != NULL)
    {
        sv->SetContentHeight(list->ContentHeight());
        sv->SetScrollPos(0);
        // 强制滚动视图重排：DoLayout 据新内容高度重算内容名义矩形，再给内容控件
        // SetRect。否则 SetContentHeight 只更新高度、不重算名义矩形，内容矩形不变、
        // DuiScrollView 给内容用的 SetRect 提前返回，新增文件行就停在 {0,0,0,0}。
        RECT svRect = sv->GetRect();
        if (svRect.right > svRect.left && svRect.bottom > svRect.top)
        {
            sv->ForceLayout(svRect);
        }
    }
    // 再直接强制文件列表容器重排兜底：即便本次行数与上次相同（内容名义矩形未变、
    // 上面那次 SetRect 仍会提前返回），ForceLayout 直接调 Layout 不经 SetRect 的
    // 提前返回，保证本批新建的文件行从 {0,0,0,0} 就位。
    {
        RECT listRect = list->GetRect();
        if (listRect.right > listRect.left)
        {
            list->ForceLayout(listRect);
        }
    }

    // 刷新分页信息文字："共 N · 第 x/y 页"。
    balloonwjui::DuiLabel* pageInfo =
        dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kGIdFilePageInfo));
    if (pageInfo != NULL)
    {
        CString text;
        text.Format(_T("共 %d · 第 %d/%d 页"), total, curPage + 1, pageCount);
        pageInfo->SetText(text);
    }

    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonGroupChatWindow::RefreshSideTabCounts()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 成员数：m_memberInfo 是「userid → 显示信息」映射，规模即群成员总数（真实群由
    // OnGroupMembersArrived 整体重填，调试入口用 mock 填）。还没填过说明成员尚未到达，
    // 按「数量未知」处理，Tab 只显示"成员"。
    const int memberCount = m_memberInfo.empty()
                            ? balloonchatwnd::kSideTabCountUnknown
                            : (int)m_memberInfo.size();
    balloonchatwnd::GroupSideTab* memberTab =
        dynamic_cast<balloonchatwnd::GroupSideTab*>(
            content->FindCtrlById(balloonchatwnd::kGIdTabMembers));
    if (memberTab != NULL)
    {
        memberTab->SetText(
            balloonchatwnd::MakeSideTabText(_T("成员"), memberCount).c_str());
    }

    // 群文件数：取库里的全量条数（不受分类 chip / 搜索框影响）。库还没拉到手时按
    // 「数量未知」处理 —— 此时写 0 会和「群里确实一个文件都没有」混淆。
    // 正在上传、尚未登记进库的那几笔不计入：等服务端登记成功推回来自然会 +1。
    const int fileCount = m_fileLoaded
                          ? m_fileLibrary.TotalAll()
                          : balloonchatwnd::kSideTabCountUnknown;
    balloonchatwnd::GroupSideTab* fileTab =
        dynamic_cast<balloonchatwnd::GroupSideTab*>(
            content->FindCtrlById(balloonchatwnd::kGIdTabFiles));
    if (fileTab != NULL)
    {
        fileTab->SetText(
            balloonchatwnd::MakeSideTabText(_T("群文件"), fileCount).c_str());
    }

    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonGroupChatWindow::StartLoadGroupFiles()
{
    // 调试入口（无群绑定）的库已在 InitUi 用 mock 预填并置 m_fileLoaded，无须拉取。
    if (m_groupId == 0)
    {
        return;
    }
    // 已拉过或正在拉：不重复发请求（本函数在开窗时与首次切到群文件 Tab 时各调一次）。
    if (m_fileLoaded || m_fileLoading)
    {
        return;
    }

    m_fileLoadBuffer.clear();
    m_fileLoading = true;
    CIUSocket::GetInstance().RequestGroupFiles(
        m_groupId, group_file_type_all, 0, kGroupFileFetchPageSize);
    LOG_INFO("群文件拉全量已发起: group=%lld。", (long long)m_groupId);
}

void CBalloonGroupChatWindow::ApplyFileChipSelection(int category)
{
    m_fileCategory = category;
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    // chip 控件 id ↔ 分类的对应表。
    struct ChipMap
    {
        UINT id;
        int  cat;
    };
    const ChipMap kChips[5] =
    {
        { balloonchatwnd::kGIdFileChipAll,    balloonchatwnd::kGroupFileCatAll    },
        { balloonchatwnd::kGIdFileChipPdf,    balloonchatwnd::kGroupFileCatPdf    },
        { balloonchatwnd::kGIdFileChipDoc,    balloonchatwnd::kGroupFileCatDoc    },
        { balloonchatwnd::kGIdFileChipSheet,  balloonchatwnd::kGroupFileCatSheet  },
        { balloonchatwnd::kGIdFileChipDesign, balloonchatwnd::kGroupFileCatDesign },
    };
    for (int i = 0; i < 5; ++i)
    {
        balloonwjui::DuiButton* btn =
            dynamic_cast<balloonwjui::DuiButton*>(content->FindCtrlById(kChips[i].id));
        if (btn != NULL)
        {
            // 选中=品牌实心（Primary，蓝）；未选=白底灰边（Default）。
            btn->SetVariant(kChips[i].cat == category
                            ? balloonwjui::DuiButton::Variant::Primary
                            : balloonwjui::DuiButton::Variant::Default);
        }
    }
}

void CBalloonGroupChatWindow::OnGroupFilesArrived(const net::GetGroupFilesResult& result)
{
    // 失败应答：收尾本次拉取，用已累积的（可能为空）灌库并渲染，记日志。
    if (result.m_code != 0)
    {
        LOG_ERROR("群文件列表应答错误: group=%lld, code=%d, msg=%s。",
                  (long long)m_groupId, result.m_code, result.m_msg.c_str());
        m_fileLibrary.SetAll(m_fileLoadBuffer);
        m_fileLoadBuffer.clear();
        m_fileLoading = false;
        m_fileLoaded  = true;
        m_filePage    = 0;
        RenderFilePanel();
        RefreshSideTabCounts();
        return;
    }

    // 累积本页条目。
    for (size_t i = 0; i < result.m_files.size(); ++i)
    {
        m_fileLoadBuffer.push_back(result.m_files[i]);
    }

    // 还有后续页：从下一偏移继续拉（Option B 全量缓存）。
    if (result.m_hasmore)
    {
        int nextOffset = result.m_offset + (int)result.m_files.size();
        CIUSocket::GetInstance().RequestGroupFiles(
            m_groupId, group_file_type_all, nextOffset, kGroupFileFetchPageSize);
        return;
    }

    // 拉全：整批灌进库、回到第 1 页、渲染。
    m_fileLibrary.SetAll(m_fileLoadBuffer);
    m_fileLoadBuffer.clear();
    m_fileLoading = false;
    m_fileLoaded  = true;
    m_filePage    = 0;
    LOG_INFO("群文件拉全完成: group=%lld, 共 %d 个文件。",
             (long long)m_groupId, m_fileLibrary.TotalAll());
    RenderFilePanel();
    // 文件数到手 → 边栏「群文件」Tab 换成真实数量（"群文件 8"）。
    RefreshSideTabCounts();
}

void CBalloonGroupChatWindow::OnGroupFilesChanged(const net::GroupFilesChangedNotify& notify)
{
    // 库尚未拉取时不动：待首次拉全量时 SetAll 会带上服务端最新状态。
    if (!m_fileLoaded)
    {
        return;
    }
    if (notify.m_changetype == group_file_change_added)
    {
        m_fileLibrary.Upsert(notify.m_file);
    }
    else if (notify.m_changetype == group_file_change_removed)
    {
        m_fileLibrary.RemoveByEntryId(notify.m_entryid);
    }
    else
    {
        return;   // 未知变更类型，忽略
    }
    // 重渲染（当前页越界由 RenderFilePanel 内 Query 钳定；面板隐藏时渲染也无妨，
    // 下次切到群文件 Tab 会再渲染）。文件数变了，Tab 上的数量同步跟上 —— 边栏此刻
    // 停在成员 Tab 也要更新，否则切过去才看到新数。
    RenderFilePanel();
    RefreshSideTabCounts();
}

void CBalloonGroupChatWindow::OnGroupFileUploaded(const net::GroupFileUploadResult& result)
{
    // 取消与真失败的区分：用户取消时窗口已先把这一行从跟踪器移除（见 OnGroupFileUploadCancelMsg），
    // 故此处按 uploadId 查不到 → 判定为「已取消」，静默忽略（不弹失败框、不刷面板）。
    if (m_uploadTracker.Find(result.m_uploadid) == NULL)
    {
        LOG_INFO("群文件上传结果到达但该笔已不在跟踪器(多半已取消): group=%lld, uploadId=%d, ok=%d。",
                 (long long)m_groupId, result.m_uploadid, result.m_ok ? 1 : 0);
        return;
    }

    // 上传结束（成功 / 失败）：把这一笔从跟踪器移除，面板顶部那一行随之消失。
    m_uploadTracker.Remove(result.m_uploadid);
    RenderFilePanel();

    if (!result.m_ok)
    {
        LOG_ERROR("群文件上传失败: group=%lld, uploadId=%d, code=%d, msg=%s。",
                  (long long)m_groupId, result.m_uploadid, result.m_errcode, result.m_errmsg.c_str());
        // 给用户可见反馈（否则上传被拒后没动静、易困惑）。群文件已取消大小上限，
        // 失败一般是网络 / 服务端原因，统一通用提示。
        if (IsWindow())
        {
            ::MessageBoxW(m_hWnd, _T("文件上传失败，请稍后重试。"),
                          _T("群文件上传"), MB_OK | MB_ICONWARNING);
        }
        return;
    }
    // 上传成功 → 发 cmd_add_group_file 登记进群文件库。服务端登记成功后向全群（含本人）
    // 推 1806，本端经 OnGroupFilesChanged Upsert 刷新面板，无需在此手动改库。
    CIUSocket::GetInstance().RequestAddGroupFile(m_groupId, result.m_fileid);
    LOG_INFO("群文件上传成功, 发起登记: group=%lld, uploadId=%d, fileid=%s, name=%s。",
             (long long)m_groupId, result.m_uploadid, result.m_fileid.c_str(), result.m_filename.c_str());
}

void CBalloonGroupChatWindow::OnGroupFileDownloaded(const net::GroupFileDownloadResult& result)
{
    // 下载结束（成功 / 失败）：清掉该条目的"下载中"态（移出进度表 + 恢复可见行的下载箭头）。
    m_downloadingFiles.erase(result.m_entryid);
    {
        balloonwjui::DuiControl* content = GetClientContent();
        balloonchatwnd::GroupFileItem* item =
            FindGroupFileItemByEntryId(content, CString(Utf8ToCString(result.m_entryid)));
        if (item != NULL)
        {
            item->ClearDownloadProgress();
        }
    }

    if (!result.m_ok || result.m_localpath.empty())
    {
        LOG_ERROR("群文件下载失败: group=%lld, entryid=%s, code=%d。",
                  (long long)m_groupId, result.m_entryid.c_str(), result.m_errcode);
        return;
    }
    // 已下载到用户「另存为」选定的位置，按其意愿仅保存、不自动打开。
    // 路径先转 UTF-8 再以 %s 记日志：narrow 日志器对 %ls + 含中文宽字符串会因
    // 默认 "C" locale 转多字节失败导致缓冲溢出断言（见本轮上传崩溃修复）。
    std::string pathUtf8 = CStringToUtf8(CString(result.m_localpath.c_str()));
    LOG_INFO("群文件下载完成并保存: group=%lld, entryid=%s, path=%s。",
             (long long)m_groupId, result.m_entryid.c_str(), pathUtf8.c_str());
}

void CBalloonGroupChatWindow::OnGroupFileUploadProgress(const net::GroupFileUploadProgress& prog)
{
    int pct = (prog.m_total > 0)
              ? static_cast<int>((prog.m_sent * 100) / prog.m_total) : 0;
    if (pct < 0)
    {
        pct = 0;
    }
    if (pct > 100)
    {
        pct = 100;
    }
    // 已不在跟踪器（已取消 / 已完成）：忽略迟到的进度。
    if (m_uploadTracker.Find(prog.m_uploadid) == NULL)
    {
        return;
    }
    // 记进跟踪器（跨重渲染保持），并就地更新顶部那一行「上传中」的百分比。
    m_uploadTracker.SetProgress(prog.m_uploadid, pct);
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::GroupFileItem* item = FindGroupFileUploadItem(content, prog.m_uploadid);
    if (item != NULL)
    {
        item->SetUploading(prog.m_uploadid, pct);
    }
}

void CBalloonGroupChatWindow::OnGroupFileDownloadProgress(const net::GroupFileDownloadProgress& prog)
{
    int pct = (prog.m_total > 0)
              ? static_cast<int>((prog.m_received * 100) / prog.m_total) : 0;
    if (pct < 0)
    {
        pct = 0;
    }
    if (pct > 100)
    {
        pct = 100;
    }
    // 记进"下载中"表（跨换页 / 推送重渲染保持该行的下载态）。
    m_downloadingFiles[prog.m_entryid] = pct;
    // 若该条目当前可见：就地更新那一行的进度。
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::GroupFileItem* item =
        FindGroupFileItemByEntryId(content, Utf8ToCString(prog.m_entryid));
    if (item != NULL)
    {
        item->SetDownloadProgress(pct);
    }
}

LRESULT CBalloonGroupChatWindow::OnGroupFileDownloadMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                        LPARAM lParam, BOOL& /*bHandled*/)
{
    // lParam 为 GroupFileItem 堆分配的 CString*（entryid），处理后须 delete。
    CString* pEntryId = (CString*)lParam;
    if (pEntryId == NULL)
    {
        return 0;
    }
    std::string entryid = CStringToUtf8(*pEntryId);
    delete pEntryId;

    // 调试入口（mock）无服务器，下载不可行；记日志返回。
    if (m_groupId == 0)
    {
        LOG_INFO("群文件下载：调试入口无群绑定，忽略 entryid=%s。", entryid.c_str());
        return 0;
    }

    // 据 entryid 在库里取完整条目（拿 fileid / name / md5 / size 供下载）。先把要用的字段
    // 拷出来——下面的「另存为」对话框是模态、会泵消息，期间若收到 1806 推送会改动
    // m_fileLibrary（Upsert/Remove 可能令 vector 重分配），直接持 e 指针会失效。
    const net::GroupFileEntry* e = m_fileLibrary.FindByEntryId(entryid);
    if (e == NULL)
    {
        LOG_ERROR("群文件下载：库中找不到 entryid=%s。", entryid.c_str());
        return 0;
    }
    std::string dlEntryId = e->m_entryid;
    std::string dlFileId  = e->m_fileid;
    std::string dlName    = e->m_name;
    std::string dlMd5     = e->m_md5;
    int64_t     dlSize    = e->m_size;

    // 弹「另存为」对话框选保存位置，默认填原文件名。用户取消则不下载。
    wchar_t szFile[1024] = { 0 };
    CString defName = Utf8ToCString(dlName);
    wcsncpy_s(szFile, defName, _TRUNCATE);
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hWnd;
    ofn.lpstrFilter = L"所有文件\0*.*\0";
    ofn.lpstrFile   = szFile;
    ofn.nMaxFile    = 1024;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOREADONLYRETURN;
    if (!::GetSaveFileNameW(&ofn))
    {
        return 0;   // 用户取消
    }
    std::wstring destPath = szFile;

    int64_t selfId = (m_lpFMGClient != NULL)
                     ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    net::FileTransfer::GetInstance().EnqueueGroupFileDownload(
        m_groupId, dlEntryId, dlFileId, dlName, dlMd5, dlSize, selfId, destPath);

    // 即时进入「下载中 0%」态：记进进度表并刷新可见行，不必等第一个进度回投。
    m_downloadingFiles[dlEntryId] = 0;
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::GroupFileItem* item =
        FindGroupFileItemByEntryId(content, Utf8ToCString(dlEntryId));
    if (item != NULL)
    {
        item->SetDownloadProgress(0);
    }

    // 路径先转 UTF-8 再以 %s 记日志（含中文路径用 %ls 会触发 narrow 日志器缓冲溢出断言）。
    std::string destUtf8 = CStringToUtf8(CString(destPath.c_str()));
    LOG_INFO("群文件下载已入队: group=%lld, entryid=%s, name=%s, dest=%s。",
             (long long)m_groupId, dlEntryId.c_str(), dlName.c_str(), destUtf8.c_str());
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnGroupFileUploadCancelMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                            LPARAM lParam, BOOL& /*bHandled*/)
{
    // lParam 即被取消那笔的 uploadId（int 直接装在 LPARAM 里）。
    int uploadId = (int)lParam;

    // 先从跟踪器移除（顶部那一行随重渲染消失），再调 FileTransfer 真正取消传输。
    // 顺序要紧：先移除，使稍后工作线程回投的失败结果在 OnGroupFileUploaded 里查不到这笔、
    // 据此判定为「已取消」而静默忽略，不弹失败框。
    bool removed = m_uploadTracker.Remove(uploadId);
    net::FileTransfer::GetInstance().CancelGroupFileUpload(uploadId);
    if (removed)
    {
        RenderFilePanel();
    }
    LOG_INFO("群文件上传取消: group=%lld, uploadId=%d。", (long long)m_groupId, uploadId);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnGroupFileDownloadCancelMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                              LPARAM lParam, BOOL& /*bHandled*/)
{
    // lParam 为 GroupFileItem 堆分配的 CString*（entryid），处理后须 delete。
    CString* pEntryId = (CString*)lParam;
    if (pEntryId == NULL)
    {
        return 0;
    }
    std::string entryid = CStringToUtf8(*pEntryId);
    CString entryidCStr = *pEntryId;
    delete pEntryId;

    // 移出「下载中」表（跨重渲染不再恢复下载态），调 FileTransfer 真正取消，并就地把这一行
    // 恢复成下载箭头。下载失败本就静默，故无需像上传那样区分取消与失败。
    m_downloadingFiles.erase(entryid);
    net::FileTransfer::GetInstance().CancelGroupFileDownload(entryid);
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::GroupFileItem* item = FindGroupFileItemByEntryId(content, entryidCStr);
    if (item != NULL)
    {
        item->ClearDownloadProgress();
    }
    LOG_INFO("群文件下载取消: group=%lld, entryid=%s。", (long long)m_groupId, entryid.c_str());
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatRecallRequest(UINT /*uMsg*/, WPARAM /*wParam*/,
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

    //调试入口（m_groupId==0，走 mock 数据）下没有真实会话，不发网络请求。
    if (m_groupId == 0)
    {
        return 0;
    }

    //群聊：ctype=2、cid=群 id。界面改动等服务端推回撤回通知后再做。
    CIUSocket::GetInstance().RecallMessage(conversation_group, m_groupId, msgid);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatPinRequest(UINT /*uMsg*/, WPARAM wParam,
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

    //调试入口（m_groupId==0，走 mock 数据）下没有真实会话，不发网络请求。
    if (m_groupId == 0)
    {
        return 0;
    }

    //群聊：ctype=2、cid=群 id。标记改动等服务端推回 Pin 通知后再做。
    CIUSocket::GetInstance().PinMessage(conversation_group, m_groupId, msgid, wParam != 0);
    return 0;
}

// 输入框里 pos 之前那一个字符是不是空白（或者 pos 本身就在文本开头）。
//
// 「再 @ 一次 TA」据此决定要不要先补一个空格：发送前的重扫只认「位于文本开头、
// 或紧跟空白之后」的 '@'（见 GroupMentionModel 的 RescanMentions），直接接在
// "你好" 后面插出来的 "你好@张三 " 会被当成普通文字，@ 不生效。
//   hEdit：输入框（RichEdit）句柄。
//   pos：  光标位置（字符下标）。
// @return pos 为 0，或前一个字符是空白时返回 true。
static bool PrevCharIsWhitespace(HWND hEdit, int pos)
{
    if (pos <= 0)
    {
        return true;   // 已在文本开头，本来就满足重扫的要求
    }
    // 选中 [pos-1, pos) 把那一个字符读回来。内嵌对象（表情图）会读成 U+FFFC，
    // 它不是空白，于是照样补空格 —— 正是想要的。
    ::SendMessage(hEdit, EM_SETSEL, (WPARAM)(pos - 1), (LPARAM)pos);
    wchar_t buf[8] = { 0 };
    ::SendMessage(hEdit, EM_GETSELTEXT, 0, (LPARAM)buf);
    return buf[0] == L' ' || buf[0] == L'\t' || buf[0] == L'\r' || buf[0] == L'\n';
}

// 把一段文字放进剪贴板。@ 菜单的「复制 TA 的昵称」用。
//   hOwner：剪贴板属主窗口。
//   text：  待复制的文字；空串直接返回，不清空用户原有的剪贴板内容。
static void PutMentionNameToClipboard(HWND hOwner, const CString& text)
{
    if (text.IsEmpty() || !::OpenClipboard(hOwner))
    {
        return;
    }
    ::EmptyClipboard();
    const std::size_t bytes = ((std::size_t)text.GetLength() + 1) * sizeof(WCHAR);
    HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem != NULL)
    {
        void* p = ::GlobalLock(hMem);
        if (p != NULL)
        {
            ::memcpy(p, (LPCWSTR)text, bytes);
            ::GlobalUnlock(hMem);
            //交给剪贴板之后所有权即归系统，不能再 GlobalFree。
            ::SetClipboardData(CF_UNICODETEXT, hMem);
        }
        else
        {
            ::GlobalFree(hMem);
        }
    }
    ::CloseClipboard();
}

bool CBalloonGroupChatWindow::IsMemberInGroupThunk(void* userdata, long long uid)
{
    CBalloonGroupChatWindow* self = static_cast<CBalloonGroupChatWindow*>(userdata);
    if (self == NULL)
    {
        return true;   // 拿不到窗口就别平白置灰菜单项
    }
    return self->m_memberInfo.find(uid) != self->m_memberInfo.end();
}

int CBalloonGroupChatWindow::MemberRoleThunk(void* userdata, long long uid)
{
    CBalloonGroupChatWindow* self = static_cast<CBalloonGroupChatWindow*>(userdata);
    if (self == NULL)
    {
        return -1;   // 拿不到窗口：按"角色未知"处理，管理项不出现
    }
    std::map<int64_t, MemberInfo>::const_iterator it = self->m_memberInfo.find(uid);
    if (it == self->m_memberInfo.end())
    {
        return -1;   // 不在本群成员表里
    }
    return it->second.m_role;
}

LRESULT CBalloonGroupChatWindow::OnChatLoadOlder(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                 LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    if (m_groupId == 0)
    {
        return 0;   // 调试入口没有真实会话，无历史可翻
    }
    balloonwjui::DuiControl* content = GetClientContent();
    balloonchatwnd::ChatMessageList* list = (content != NULL)
        ? dynamic_cast<balloonchatwnd::ChatMessageList*>(
              content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
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
        2, m_groupId, m_paging.m_oldestMsgid, balloonchatwnd::kHistoryPageSize);
    LOG_INFO("群聊窗请求更早一页历史: group=%lld, before=%lld, limit=%d.",
             (long long)m_groupId, (long long)m_paging.m_oldestMsgid,
             balloonchatwnd::kHistoryPageSize);
    return 0;
}

// 引用条的配色。人名用主题橙（与气泡里引用块的人名同色），摘要用灰 —— 一眼分清
// "回复谁"和"回复的是什么内容"。
static const COLORREF kReplyNameColor   = RGB(0x3A, 0x86, 0xD9);   // 人名：蓝，与截图一致
static const COLORREF kReplyDigestColor = RGB(0x6B, 0x6B, 0x6B);   // 被引用内容：中灰

void CBalloonGroupChatWindow::RefreshReplyBar()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* bar = content->FindCtrlById(balloonchatwnd::kGIdReplyBar);
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
            content->FindCtrlById(balloonchatwnd::kGIdReplyName));
        balloonwjui::DuiLabel* digestLabel = dynamic_cast<balloonwjui::DuiLabel*>(
            content->FindCtrlById(balloonchatwnd::kGIdReplyDigest));

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
    // 卡片占一块固定高度，显隐会改变输入区块的内部排布，必须重排。
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

void CBalloonGroupChatWindow::CancelPendingReply()
{
    m_pendingQuote = net::ReplyQuote();
    RefreshReplyBar();
}

LRESULT CBalloonGroupChatWindow::OnChatLocateQuoted(UINT /*uMsg*/, WPARAM /*wParam*/,
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
              content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return 0;
    }

    // 原消息不在已加载范围内时 ScrollToMsgid 返回 false —— 本期到此为止，
    // 「自动回拉历史直到那一条出现」属于后续阶段。
    const bool located = list->ScrollToMsgid(msgid);
    LOG_INFO("群聊窗定位被引用消息: msgid=%lld, 命中=%d.", msgid, located ? 1 : 0);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatReplyRequest(UINT /*uMsg*/, WPARAM /*wParam*/,
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
              content->FindCtrlById(balloonchatwnd::kGIdMessageArea))
        : NULL;
    if (list == NULL)
    {
        return 0;
    }

    balloonchatwnd::ChatMessage src;
    if (!list->GetMessageByMsgid(msgid, src))
    {
        return 0;   // 该行已不在列表里（被删 / 尚未加载），安静忽略
    }

    // 发送人显示名：群聊取群名片优先的成员映射，查不到回退"用户<id>"。快照在此刻定格，
    // 群名片之后再改也不影响已发出的引用 —— 这正是存快照而不是只存 msgid 的意义。
    CString senderName;
    std::map<int64_t, MemberInfo>::iterator it = m_memberInfo.find(src.senderUserId);
    if (it != m_memberInfo.end())
    {
        senderName = it->second.m_name;
    }
    if (senderName.IsEmpty())
    {
        senderName.Format(_T("用户%lld"), (long long)src.senderUserId);
    }

    // 被引用的若是图片，把原图的 fileid / md5 一并带上 —— 收方据此在引用块里画
    // 缩略图。ChatMessage 上这两项由收图时记下（见 imageFileId / imageMd5 的说明）。
    m_pendingQuote = balloonchatwnd::BuildQuoteFromMessage(
        src, senderName, src.imageFileId, src.imageMd5);
    RefreshReplyBar();
    LOG_INFO("群聊窗挂起引用回复: quote.msgid=%lld, valid=%d.",
             msgid, m_pendingQuote.m_valid ? 1 : 0);
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatImageAction(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                   LPARAM lParam, BOOL& /*bHandled*/)
{
    //图片气泡右键菜单冒泡上来：lParam 为堆上 ChatImageActionData，处理后 delete。
    balloonchatwnd::ChatImageActionData* d = (balloonchatwnd::ChatImageActionData*)lParam;
    if (d == NULL)
    {
        return 0;
    }

    //菜单只在图片就绪时才给这两项，正常不会拿到空路径；真拿到就是状态判定出了岔子，
    //记一条日志便于回查，不再往下走。
    if (d->imagePath.empty())
    {
        LOG_ERROR("群聊窗图片菜单动作缺本地路径: group=%lld, action=%d.",
                  (long long)m_groupId, (int)d->id);
        delete d;
        return 0;
    }

    switch (d->id)
    {
    case balloonchatwnd::kChatMsgMenuSaveImageAs:
        //弹保存对话框 + 按原字节拷贝，与单聊窗共用同一份实现。
        balloonchatwnd::SaveChatImageAs(m_hWnd, d->imagePath, d->saveName);
        break;

    case balloonchatwnd::kChatMsgMenuResendImage:
        //再次发送 = 把这张图作为一条<u>新</u>图片消息发到本群，与选图 / 截图发送
        //共用同一条链路（新的 clientMsgId、重新上传），不是重发原来那条。
        LOG_INFO("群聊窗再次发送图片: group=%lld.", (long long)m_groupId);
        SendImageFile(d->imagePath);
        break;

    default:
        LOG_ERROR("群聊窗收到未知的图片菜单动作: action=%d.", (int)d->id);
        break;
    }

    delete d;
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnChatMentionAction(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                     LPARAM lParam, BOOL& /*bHandled*/)
{
    //气泡里 @某人 的右键菜单选中项冒泡上来：lParam 为堆上负载，处理后 delete。
    balloonchatwnd::MentionActionData* pData = (balloonchatwnd::MentionActionData*)lParam;
    if (pData == NULL)
    {
        return 0;
    }
    const balloonchatwnd::MentionMenuId action = pData->id;
    const long long    uid  = pData->uid;
    const CString      name = pData->displayName.c_str();
    delete pData;

    LOG_INFO("群聊窗 @ 菜单动作: group=%lld, uid=%lld, action=%d.",
             (long long)m_groupId, (long long)uid, (int)action);

    switch (action)
    {
    case balloonchatwnd::kMentionMenuChat:
        //打开与该成员的单聊窗。本窗口不持有会话管理，交主面板统一路由（与聊天窗
        //头部点击打开资料窗是同一条路子）。
        if (m_hostPanel != NULL && ::IsWindow(m_hostPanel))
        {
            ::PostMessage(m_hostPanel, FMG_MSG_OPEN_CHAT, (WPARAM)uid, 0);
        }
        break;
    case balloonchatwnd::kMentionMenuProfile:
        //打开该成员的联系人资料窗，同样经主面板路由。
        if (m_hostPanel != NULL && ::IsWindow(m_hostPanel))
        {
            ::PostMessage(m_hostPanel, FMG_MSG_OPEN_CONTACT_PROFILE, (WPARAM)uid, 0);
        }
        break;
    case balloonchatwnd::kMentionMenuMentionMore:
        //在输入框光标处插入「@显示名 」，并把这一次 @ 记进待发候选。走的是
        //InsertMentionAtCaret 而不是 InsertMention —— 后者要替换用户已键入的
        //`@前缀`、依赖 m_mentionAtPos，而从气泡右键进来时用户根本没打过 '@'。
        InsertMentionAtCaret(uid, name);
        break;
    case balloonchatwnd::kMentionMenuCopyName:
        PutMentionNameToClipboard(m_hWnd, name);
        break;
    case balloonchatwnd::kMentionMenuSetAdmin:
        //设为管理员。菜单只对群主出现，服务端还会再校验一次（仅群主可设），
        //本端不预改界面 —— 等 cmd_group_member_changed_notify 推回来统一刷新。
        LOG_INFO("请求设为管理员: group=%lld, uid=%lld.",
                 (long long)m_groupId, (long long)uid);
        CIUSocket::GetInstance().SendSetMemberRole(m_groupId, uid, true);
        break;
    case balloonchatwnd::kMentionMenuCancelAdmin:
        LOG_INFO("请求撤销管理员: group=%lld, uid=%lld.",
                 (long long)m_groupId, (long long)uid);
        CIUSocket::GetInstance().SendSetMemberRole(m_groupId, uid, false);
        break;
    case balloonchatwnd::kMentionMenuKick:
    {
        //移出不可撤销，先确认。文案与群管理窗的「移出成员」保持一致。
        CString tip;
        tip.Format(_T("确定把「%s」移出本群吗？\n\n移出后该成员将无法再看到本群消息。"),
                   (LPCTSTR)name);
        if (::MessageBox(m_hWnd, tip, _T("移出成员"),
                         MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
        {
            break;
        }
        LOG_INFO("请求移出群成员: group=%lld, uid=%lld.",
                 (long long)m_groupId, (long long)uid);
        std::vector<int64_t> ids;
        ids.push_back(uid);
        CIUSocket::GetInstance().SendKickGroupMembers(m_groupId, ids);
        break;
    }
    default:
        break;
    }
    return 0;
}

void CBalloonGroupChatWindow::OnSendChatAckBackfill(const std::string& clientMsgId, long long msgid,
                                                    long long timestampMs)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    //本窗口没有该 clientMsgId（是别的会话发出的）时安静忽略。
    list->BackfillSentMessage(clientMsgId, msgid, timestampMs);
}

void CBalloonGroupChatWindow::OnMessageRecalled(long long msgid, const CString& tipText)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    //列表里没有该条（消息尚未加载进当前视图）时安静忽略。
    list->RecallMessageByMsgid(msgid, tipText);
}

void CBalloonGroupChatWindow::OnMessagePinned(long long msgid, bool pinned)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonchatwnd::ChatMessageList* list =
        dynamic_cast<balloonchatwnd::ChatMessageList*>(
            content->FindCtrlById(balloonchatwnd::kGIdMessageArea));
    if (list == NULL)
    {
        return;
    }

    //列表里没有该条（消息尚未加载进当前视图）时安静忽略：本地库那份标记已经落好，
    //下次翻历史渲染出来时自然带着。
    list->SetPinnedByMsgid(msgid, pinned);
}

void CBalloonGroupChatWindow::SetPinnedCount(int count)
{
    m_pinnedCount = (count > 0) ? count : 0;
    RefreshPinBanner();
}

void CBalloonGroupChatWindow::AdjustPinnedCount(int delta)
{
    m_pinnedCount += delta;
    if (m_pinnedCount < 0)
    {
        m_pinnedCount = 0;
    }
    RefreshPinBanner();
}

void CBalloonGroupChatWindow::RefreshPinBanner()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;   //控件树还没建好，等 InitUi 之后主面板会再校准一次
    }

    const bool visibilityChanged =
        balloonchatwnd::UpdatePinBanner(content, balloonchatwnd::kGIdPinBanner, m_pinnedCount);

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

void CBalloonGroupChatWindow::RequestOpenHistory(int initialTypeFilter)
{
    if (m_groupId <= 0 || m_hostPanel == NULL || !::IsWindow(m_hostPanel))
    {
        return;   //调试入口下没有主面板，安静忽略
    }

    //群 id 经堆 int64_t 随消息回投，由主面板处理后 delete。会话类型与"预选哪个类型
    //胶囊"一起塞在 wParam 里（约定见 UserSessionData.h 的 FMG_MSG_OPEN_HISTORY）。
    int64_t* pCid = new int64_t(m_groupId);
    const WPARAM wp = (WPARAM)(conversation_group | (initialTypeFilter << 8));
    if (!::PostMessage(m_hostPanel, FMG_MSG_OPEN_HISTORY, wp, (LPARAM)pCid))
    {
        delete pCid;   //投递失败（主面板已销毁）自行回收，杜绝泄漏
    }
}

LRESULT CBalloonGroupChatWindow::OnGroupFileDeleteMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                      LPARAM lParam, BOOL& /*bHandled*/)
{
    // lParam 为 GroupFileItem 堆分配的 CString*（entryid），处理后须 delete。
    CString* pEntryId = (CString*)lParam;
    if (pEntryId == NULL)
    {
        return 0;
    }
    std::string entryid = CStringToUtf8(*pEntryId);
    delete pEntryId;

    // 调试入口（mock）无服务器，删除不可行；记日志返回。
    if (m_groupId == 0)
    {
        LOG_INFO("群文件删除：调试入口无群绑定，忽略 entryid=%s。", entryid.c_str());
        return 0;
    }

    // 据 entryid 在库里取完整条目（拿文件名做确认提示 + 上传者做权限复检）。
    const net::GroupFileEntry* e = m_fileLibrary.FindByEntryId(entryid);
    if (e == NULL)
    {
        LOG_ERROR("群文件删除：库中找不到 entryid=%s。", entryid.c_str());
        return 0;
    }

    // 权限复检（与垃圾桶门控、服务端规则同一判据）：群主 / 管理员可删任意，上传者本人可删自己的。
    int64_t selfId = (m_lpFMGClient != NULL)
                     ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    std::string selfIdStr = (selfId != 0) ? std::to_string(selfId) : std::string();
    bool isOwnUpload = (!selfIdStr.empty() && e->m_uploaderid == selfIdStr);
    if (!balloonchatwnd::GroupFileLibrary::CanDeleteGroupFile(m_myRole, isOwnUpload))
    {
        LOG_ERROR("群文件删除：无权限(role=%d, own=%d), entryid=%s。",
                  m_myRole, isOwnUpload ? 1 : 0, entryid.c_str());
        return 0;
    }

    // 确认（删除是破坏性且影响全群，必弹确认框）。
    CString name = Utf8ToCString(e->m_name);
    CString prompt;
    prompt.Format(_T("确定删除群文件「%s」吗？\n删除后全群成员都将看不到此文件。"), (LPCTSTR)name);
    if (::MessageBoxW(m_hWnd, prompt, _T("删除群文件"), MB_YESNO | MB_ICONWARNING) != IDYES)
    {
        return 0;
    }

    // 发删除请求；删成功后服务端推 1806(removed)，经 OnGroupFilesChanged 把该行移出全群面板。
    CIUSocket::GetInstance().RequestDeleteGroupFile(m_groupId, entryid);
    LOG_INFO("群文件删除已发起: group=%lld, entryid=%s, name=%s。",
             (long long)m_groupId, entryid.c_str(), e->m_name.c_str());
    return 0;
}

bool CBalloonGroupChatWindow::PickAndUploadGroupFile()
{
    // 调试入口（无群绑定）无服务器，不支持真实上传。
    if (m_groupId == 0)
    {
        LOG_INFO("群文件上传：调试入口无群绑定，忽略。");
        return false;
    }

    // 弹系统选文件框选一个文件（任意类型）。
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
        return false;   // 用户取消
    }

    int64_t selfId = (m_lpFMGClient != NULL)
                     ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    // 入队上传，拿回客户端分配的 uploadId（可多笔并发，每笔一个 id）。
    int uploadId = net::FileTransfer::GetInstance().EnqueueGroupFileUpload(m_groupId, szFile, selfId);

    // 从路径拆出文件名与扩展名，登记进上传跟踪器，使面板顶部立刻冒出一行「上传中 0%」。
    std::wstring fullPath(szFile);
    size_t slash = fullPath.find_last_of(L"\\/");
    std::wstring nameW = (slash == std::wstring::npos) ? fullPath : fullPath.substr(slash + 1);
    size_t dot = nameW.find_last_of(L'.');
    std::wstring extW = (dot == std::wstring::npos) ? std::wstring() : nameW.substr(dot + 1);
    m_uploadTracker.Begin(uploadId, nameW, extW);
    RenderFilePanel();

    // 路径先转 UTF-8 再以 %s 记日志（含中文路径用 %ls 会触发 narrow 日志器缓冲溢出断言）。
    std::string pathUtf8 = CStringToUtf8(CString(szFile));
    LOG_INFO("群文件上传已入队: group=%lld, uploadId=%d, path=%s。",
             (long long)m_groupId, uploadId, pathUtf8.c_str());
    return true;
}

LRESULT CBalloonGroupChatWindow::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/,
                                        LPARAM /*lParam*/, BOOL& bHandled)
{
    // 不消费 WM_SIZE —— 仍交 DuiFrameWindow 链缩放客户区与后备缓冲。
    // 投递自定义消息：等客户区尺寸更新后再强制重排控件树。
    if (IsWindow())
    {
        ::PostMessage(m_hWnd, kRelayoutMsg, 0, 0);
    }
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonGroupChatWindow::OnRelayout(UINT /*uMsg*/, WPARAM /*wParam*/,
                                            LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return 0;
    }

    // 把输入区块稳定在用户选定（或默认）的高度：窗口缩放时让消息区吃掉高度
    // 变化、输入区高度基本不变 —— 与改造前"输入区固定、消息区撑满"的手感一致。
    // 与单聊共用同一记忆。须放在下面消息区 ForceLayout 之前。
    balloonwjui::DuiSplitter* sp = dynamic_cast<balloonwjui::DuiSplitter*>(
        content->FindCtrlById(balloonchatwnd::kGIdInputSplitter));
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
        content->FindCtrlById(balloonchatwnd::kGIdMessageArea);
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
