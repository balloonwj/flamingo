/**
 *  BalloonMainPanel.cpp
 *
 *  登录后主面板实现。P0：骨架 —— 以 DuiFrameWindow 形式创建 / 显示一个
 *  窄高窗口。关闭本面板即请求退出程序。真正的
 *  Header / 搜索 / Tab / 列表由后续 P1 阶段的 BalloonMainLayout 构建。
 *
 *  balloonwj@qq.com   2026-05-21
 */

#include "stdafx.h"
#include "BalloonMainPanel.h"

#include <shobjidl.h>                 // ITaskbarList / CLSID_TaskbarList（方案 C 验证：摘任务栏按钮）
#include <memory>
#include <string>
#include <map>
#include <set>
#include "../BalloonGroupManage/MemberPickerRequest.h"   // 打开选人对话框的请求载荷
#include "../BalloonGroupManage/GroupAvatarDlgRequest.h" // 打开修改群头像对话框的请求载荷
#include "../BalloonGroupManage/AnnouncePopupPolicy.h"   // 新公告弹不弹的判定与"已弹过哪条"的记账
#include "../BalloonGroupManage/GroupManageMapping.h"    // FormatAnnouncementTime（与公告页同一口径）
#include <vector>
#include <algorithm>
#include <ctime>                      // ::time —— 会话冒泡的本地时间戳

#include "../../resource.h"             // IDM_MODIFYPASSWORD / IDM_SYSTEMSETTINGS / ID_ABOUT（系统菜单命令号）
#include "../../Common/Graphics/ImageEx.h"             // CImageEx（系统菜单项图标）
#include "MainPanelSystemMenu.h"    // BuildSystemMenuModel / SystemMenuId（系统菜单行模型）
#include "MainPanelConversationMenu.h" // BuildConversationMenuModel / ConversationMenuId（会话右键菜单行模型）
#include "MainPanelContactMenu.h"     // BuildContactMenuModel / ContactMenuId（联系人右键菜单行模型）
#include "MainPanelGroupMenu.h"       // BuildGroupMenuModel / GroupMenuId / FormatGroupNumber（群列表右键菜单行模型）
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiToast.h"    // 主面板轻提示（退群 / 解散 / 转让的结果）
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"  // 轻提示挂在客户区根容器（DuiVBox）上
#include "../../Common/Util/ClipboardText.h"   // 右键菜单「复制联系人名 / 复制群号 / 复制群名」写剪贴板
#include "ConversationListOps.h"    // 会话列表纯操作（排序 / 查找 / 删除 / 置顶 / 免打扰）
#include "GroupListOps.h"           // 群 Tab 群列表纯操作（置顶优先的显示顺序 / 改置顶状态）
#include "FolderInviteNotifyPolicy.h" // 文件夹邀请的会话列表预览文字与"记未读 / 闪托盘"判据
#include "../BalloonChatWnd/ChatLinkDetect.h"  // SetChatLinkOptionEnabled（链接识别开关改动后转发到各会话窗）
#include "../BalloonChatWnd/ChatSendLimit.h"   // SendFailureText（发送失败回执的提示文案）
#include "MainPanelStatusMenu.h"    // BuildStatusMenuModel / StatusMenuEntry / GetPresenceVisual（状态菜单）
#include "PresenceBadge.h"          // CreatePresenceBadgeBitmap（树里行右端的状态徽标位图）
#include "OnlineCapability.h"       // 离线门禁：连接态、被拦操作的提示文案
#include "MainPanelMenuIcons.h"     // CreateMenuIconImage（系统菜单项图标渲染）
#include "MainPanelIcons.h"         // IconKind / kIconOnline（状态菜单彩色圆点图标）
#include "../../App/CustomMsgDef.h"          // WM_CLOSE_MAINDLG
#include "../../Common/Log/IULog.h"                 // LOG_INFO
#include "../../net/FolderProtocolClient.h"           // net::FolderInviteNotify / FolderStateNotify（文件夹传输推送载体）
#include "../../Feature/FolderTransfer/FolderTransferManager.h" // foldertransfer::FolderScanNotice / FolderBindNotice / FolderProgressNotice / FolderResultNotice
#include "../../App/FlamingoClient.h"        // CFlamingoClient::m_UserMgr
#include "../../Session/UserMgr.h"               // CUserMgr / CBuddyList / CGroupList / CRecentList
#include "../../Common/Util/EncodeUtil.h"            // UTF-8 → Unicode 转换（通讯录字段为 UTF-8）
#include "../../net/IUProtocolData.h"    // STATUS_ONLINE / STATUS_OFFLINE 等
#include "BalloonMainLayout.h"        // BuildMainPanelBody / CtrlId / RefreshXxxPane
#include "MainPanelHeader.h"          // MainPanelHeader::SetUserData / GetSignHitRect / 签名点击通知码
#include "SignatureEditModel.h"       // DecideSignatureCommit（签名提交判定）
#include "../../net/ProfileProtocolClient.h"  // net::UpdateProfileResponse（更新资料应答）
#include "MainPanelTab.h"             // MainPanelTab（切换选中态）
#include "MainPanelButtons.h"         // MainPanelActionButton（刷新文字）
#include "MainPanelSectionHeader.h"   // MainPanelSectionHeader（展开/收起）
#include "MainPanelListItem.h"        // MainPanelListItem（列表项点击）
#include "MainPanelMockData.h"        // MockFavorites / MockServiceAccounts(19a 填 Contacts TreeView)
#include "MainPanelAvatar.h"          // 19f:monogram avatar HBITMAP 生成
#include "AvatarPalette.h"            // 头像底色调色板（与登录窗共用一份）
#include "AvatarSpec.h"               // avatar_kind_image（判断是否真图头像）
#include "AvatarImageUtil.h"          // MakeRoundedAvatarFromFile（真图头像渲染）
#include "MainPanelScrollView.h"      // MainPanelScrollView(继承 DuiScrollView)
#include "MainPanelTreeView.h"        // MainPanelTreeView(会话树:未读数字红点角标;群树:第一行右侧成员数)
#include "GroupRowText.h"             // BuildGroupRowTexts(群列表行的"群名+人数 / 群号"文案)
#include "MainPanelUserData.h"        // P2:把当前登录用户字段注入到 Header
#include "NotificationSound.h"        // 收消息 / 好友上线时异步播放提示音（PlayNotifySound）
#include "../../net/MessageProtocolClient.h"  // net::MessageNotify / net::SendMessageResponse
#include "../../../third-party/balloonui/balloonui/DuiTrace.h"   // BUI_TRACE：历史应答落库与渲染的时序跟踪
#include "../../Common/Util/PerfStopWatch.h"                      // 历史应答落库、渲染的计时
#include "SyncArrivalFlow.h"                                    // 历史应答各项处理的先后顺序
#include "../../net/Protocol.h"                   // conversation_group / message_type_system（撤回处理用）、announcement_change_*（平台公告推送用）
#include "../../Session/GroupRoleRules.h"          // ShouldRefetchMyRoleOnMemberChange：本人角色变更后补拉群资料
#include "../BalloonChatWnd/GroupMemberItem.h"     // kMemberPresenceUnknown：群成员在线状态查不到时的取值
#include "../BalloonChat/BalloonChatMsgFormat.h"  // BuildRecallTipText（撤回提示文案）
#include "../../net/ContactsProtocolClient.h" // net::ContactsResult / net::ContactItem
#include "../../net/GroupProtocolClient.h"    // net::GroupInfoResult / net::GroupMembersResult
#include "../../net/GroupFileProtocolClient.h" // net::GetGroupFilesResult / GroupFilesChangedNotify
#include "../../net/AnnounceProtocolClient.h"  // net::AnnouncementsResult / AnnouncementNotify / AnnounceMarkReadResult（平台公告 = 消息盒子）
#include "../../net/FileTransfer.h"            // net::GroupFileUploadResult / GroupFileDownloadResult
#include "../../net/IUSocket.h"               // CIUSocket::RequestGroupInfo 等
#include "../../net/ImageTransfer.h"
#include "../../net/UserCachePath.h"        // net::UserConfigFilePath：账号配置文件路径
#include "../../net/CurrentUserAccount.h"   // net::CurrentUserAccount（本地缓存目录按登录账号分）          // net::ImageSendProgress / net::ImageSendResult
#include "../../net/FileTransfer.h"           // net::FileSendProgress / net::FileSendResult / net::FileDownload*
#include "../../../third-party/balloonui/balloonui/Controls/Input/DuiEdit.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"   // 通讯录标题刷新
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiTreeView.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"   // 联系人右键菜单
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"  // DuiScrollView
#include "PopupMenuPlacement.h"            // ClampPopupMenuToWorkArea（保证菜单在桌面内）
#include "PanelDragZone.h"                 // IsPointInDragZone（空白处按住拖窗口的命中判定）
#include "TitleBarIcon.h"                  // CreateProgramIconBitmap（标题栏左侧图标）
#include "ContactsOrgStats.h"              // FindCompanyRootDeptIndex / CountDeptMembersRecursive（公司节点与人数）
#include "TreeLocate.h"                    // CollectAncestorChain / ComputeLocateScrollPos（搜索定位到通讯录树）
#include <windowsx.h>                      // GET_X_LPARAM / GET_Y_LPARAM（非客户区消息取坐标）

// ---- 文件作用域常量 -----------------------------------------------------

// 主面板固定客户区尺寸（逻辑像素）—— 对照设计稿的窄高面板。
static const int kMainPanelWidth  = 320;
static const int kMainPanelHeight = 680;

// 主面板允许缩放到的最小尺寸。
static const int kMainPanelMinWidth  = 300;
static const int kMainPanelMinHeight = 480;

// 标题栏高度（逻辑像素）—— 常规标题栏，承载 min/max/close。
static const int kMainPanelTitleBarHeight = 32;

// 标题栏左侧图标的位图边长（像素）。balloonui 画标题栏图标时固定按 16x16 输出
// （见 DuiFrameWindow.cpp 的 "Optional left-edge icon" 一段），取同值可免去缩放。
static const int kMainPanelTitleIconSize = 16;

// 标题栏底色。主面板用的是常规（非透明）标题栏，balloonui 实际填的就是这个颜色
// （同上，DuiFrameWindow.cpp 的 "Title strip background"）。图标必须先合成到这层
// 底色上再挂给 SetIcon —— 标题栏是用 SRCCOPY 直接搬运位图、不做 alpha 混合的，
// 带透明通道的图标交过去会变成黑块（详见 TitleBarIcon.h）。
static const COLORREF kMainPanelTitleBarBg = RGB(245, 246, 250);

// WM_SYSCOMMAND 的命令码在低 4 位保留作系统内部用途，取命令须与此掩码相与
// （与登录窗 CBalloonLoginDlg 同口径）。
static const WPARAM kSysCommandMask = 0xFFF0;

// 联系人树 monogram 头像边长（逻辑像素）—— 与 FillContactsTree 的 mock 头像同尺寸。
static const int kContactAvatarSizePx = 32;

// 树里行右端状态徽标位图的边长（逻辑像素）。徽标圆底只占绘制方框的 20/24，故 12 对应
// 一个直径 10px 的圆点 —— 与改版前 DuiTreeView 那个纯色圆点（kStatusDotR = 5，直径 10）
// 一样大，行的观感不变，多出来的只是圆点里的字形。
// 取 12 而不是 10：10 的方框只剩 8.3px 的圆底，时钟指针会糊成一团（2026-08-12 实拍确认）。
static const int kPresenceBadgeSizePx = 12;

// 联系人页标题行高（逻辑像素）—— 取 0，因为联系人页 treeview 之上已经没有独立的
// 标题 label："通讯录 · N 人"那一行已去掉，人数改为跟在树里公司根节点的名字后面
// （见 BalloonMainLayout.cpp 的 BuildContactsListXml 注释）。SyncPaneContentHeight
// 计算滚动内容高度时，联系人页只需算 treeview 自身高度加 vbox 上下 padding。
static const int kContactsPaneTitleHeight = 0;

// 三个列表面板里，内容 vbox 的上、下内边距（逻辑像素，单边）。与 BalloonMainLayout
// 里 BuildXxxListXml 的 padding="6,4,6,4"（L,T,R,B 顺序）中的 T / B 两个数一致。
// 两处用到：算滚动内容总高（要把上下两边都算进去），以及算某一行在滚动内容里的
// 纵向位置（只算上边那一份，见 LocateContactInTree）。
static const int kContactsPaneContentPadTop = 4;

// 群页标题行高（逻辑像素）—— 取 0，因为群页在 treeview 之上没有独立的标题
// label：分组标题由树的根节点"我的群聊 · N"承担（见 BalloonMainLayout.cpp
// 的 BuildGroupsListXml 注释）。SyncPaneContentHeight 计算滚动内容高度时，
// 群页只需算 treeview 自身高度加 vbox 上下 padding。
static const int kGroupsPaneTitleHeight = 0;

// 群成员角色取值，与 Protocol.h 的 group_member_role 对齐（该枚举在全局
// 命名空间，这里取本地常量避免跨头依赖）。-1 另表"角色未知"。
static const int kGroupRoleMember = 0;   // 普通成员
static const int kGroupRoleAdmin  = 1;   // 管理员
static const int kGroupRoleOwner  = 2;   // 群主

// 联系人右键菜单项 id 已抽到 balloonmain::ContactMenuId（MainPanelContactMenu.h），
// 与菜单结构一起由纯函数给出、被单测覆盖。这里给常用的几个起短别名，让下面的
// switch 分发读起来仍然紧凑；本文件不再自持一份 id 定义，避免两处数值漂移。
const UINT kCMenuSendMsg      = balloonmain::kContactMenuSendMsg;
const UINT kCMenuVoice        = balloonmain::kContactMenuVoice;
const UINT kCMenuVideo        = balloonmain::kContactMenuVideo;
const UINT kCMenuViewProfile  = balloonmain::kContactMenuViewProfile;
const UINT kCMenuMute         = balloonmain::kContactMenuMute;
const UINT kCMenuViewHistory  = balloonmain::kContactMenuViewHistory;
const UINT kCMenuFavorite     = balloonmain::kContactMenuFavorite;
const UINT kCMenuStarred      = balloonmain::kContactMenuStarred;

// 调试入口（--mainpanel）的假数据里每隔几行标一个「特别关注」。取 3 只是为了让
// 标记行与未标记行同屏出现、便于核对观感；正式数据不走这个值。
const int kMockStarredStride = 3;

// 刚登录（或断线重连回来）之后多久之内不弹「特别关注」的上下线提示，毫秒。
// 通讯录还没拉回来时，OnPresenceChanged 对查不到的人一律按离线处理，此时收到的
// presence 推送会被判成"刚上线"——不静默的话，登录瞬间会把在线的同事全弹一遍。
// 取 10 秒：够通讯录应答回来，短到不至于漏掉登录后不久真正发生的上下线。
const DWORD kStarredSilenceMs = 10 * 1000;
const UINT kCMenuRemark       = balloonmain::kContactMenuRemark;
const UINT kCMenuCopyName     = balloonmain::kContactMenuCopyName;

// 群聊右键菜单的项 id 与行模型见 MainPanelGroupMenu.h（2026-10-01 从本文件抽出，便于单测）。

// 系统菜单（左下角汉堡按钮弹出菜单）相关常量。
// 图标边长必须与 DuiMenu 的图标列尺寸一致（DuiMenu 以源矩形 (0,0,16,16) 采样）。
static const int      kSystemMenuIconSize  = 16;
// 图标描边色（design --text-muted，比菜单文字略浅的中性灰）。
static const COLORREF kSystemMenuIconColor = RGB(0x5B, 0x5B, 0x5B);
// 菜单底边与按钮顶边之间留的竖直间距（逻辑像素），避免菜单贴住按钮。
static const int      kSystemMenuGap       = 6;

// 从主面板客户区控件树里取 Header 控件（状态胶囊、昵称、签名、通知盒都在它上面）。
// 读当前在线状态、乐观更新胶囊、应答失败回退胶囊这几处都要先拿到它，故收拢成一个
// helper，免得同一段 dynamic_cast 各写一份。content 为空或树上没有 Header 时返回 NULL。
static balloonmain::MainPanelHeader* FindMainPanelHeader(balloonwjui::DuiControl* content)
{
    if (content == NULL)
    {
        return NULL;
    }
    return dynamic_cast<balloonmain::MainPanelHeader*>(
        content->FindCtrlById(balloonmain::kIdHeader));
}

// UTF-8 std::string → CString(UTF-16)。通讯录字段（昵称/签名/企业名）为
// UTF-8，渲染前转 Unicode。空串安全返回空 CString。
static CString Utf8ToCStr(const std::string& utf8)
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

// 当前时间，Unix毫秒。新公告提示窗上那行"发布人 · 时间"按这个口径算相对日期。
static int64_t NowMillisecondsForPanel()
{
    const int64_t kMillisPerSecond = 1000;
    return static_cast<int64_t>(time(NULL)) * kMillisPerSecond;
}

// CString(UTF-16) → UTF-8 std::string。提交个性签名时把编辑框文本转 UTF-8 发往
// 服务端。空串安全返回空 std::string。
static std::string CStrToUtf8(const CString& text)
{
    int wlen = text.GetLength();
    if (wlen <= 0)
    {
        return std::string();
    }
    // UTF-8 字节数不超过 wchar 个数的 4 倍，按此预留缓冲。
    std::vector<char> buf((size_t)wlen * 4 + 1, 0);
    EncodeUtil::UnicodeToUtf8(text.GetString(), buf.data(), (int)buf.size());
    return std::string(buf.data());
}

// 部门同级排序比较：deptorder 小者靠前，相等按 deptid 稳定。
static bool DeptOrderLess(const net::DepartmentItem* a, const net::DepartmentItem* b)
{
    if (a->m_deptorder != b->m_deptorder)
    {
        return a->m_deptorder < b->m_deptorder;
    }
    return a->m_deptid < b->m_deptid;
}

// 部门成员排序比较：memberorder 小者靠前，相等按 userid 稳定。
static bool MemberOrderLess(const net::DeptMemberItem& a, const net::DeptMemberItem& b)
{
    if (a.m_memberorder != b.m_memberorder)
    {
        return a.m_memberorder < b.m_memberorder;
    }
    return a.m_userid < b.m_userid;
}

// 按 userid 从固定调色板挑一个头像底色，保证同一用户颜色稳定。
// 调色板已收敛到 BalloonMain/AvatarPalette.h（与登录窗 Banner 头像共用
// 一份），此处仅作转调，保持原有调用点不变。
static COLORREF AvatarColorForUser(int64_t userid)
{
    return balloonmain::AvatarColorForUser(userid);
}

// 把 Unix 毫秒时间戳转换为本地时区的 SYSTEMTIME。成功返回 true。
// 本翻译单元是 ATL/WTL 环境（CString 来自 ATL），没有 MFC 的 CTime，故用
// Win32 时间 API 做换算。
static bool UnixMsToLocalSystemTime(int64_t timestampMs, SYSTEMTIME& outLocal)
{
    const int64_t   kMsPerSecond           = 1000;                  // 一秒的毫秒数
    const ULONGLONG kUnixEpochAsFileTime   = 116444736000000000ULL; // 1970-01-01 对应的 FILETIME 值（100ns 自 1601）
    const ULONGLONG kFileTimePer100nsSecond = 10000000ULL;         // 一秒含的 100ns 数

    int64_t seconds = timestampMs / kMsPerSecond;
    if (seconds < 0)
    {
        seconds = 0;
    }
    ULONGLONG ull = kUnixEpochAsFileTime + (ULONGLONG)seconds * kFileTimePer100nsSecond;

    FILETIME ftUtc;
    ftUtc.dwLowDateTime  = (DWORD)(ull & 0xFFFFFFFFULL);
    ftUtc.dwHighDateTime = (DWORD)(ull >> 32);

    FILETIME ftLocal;
    if (!::FileTimeToLocalFileTime(&ftUtc, &ftLocal))
    {
        return false;
    }
    if (!::FileTimeToSystemTime(&ftLocal, &outLocal))
    {
        return false;
    }
    return true;
}

// 把一个本地 SYSTEMTIME 往前推一天，得到"昨天"的本地日期。成功返回 true。
// 只用于"昨天"日期比较，DST 边界的细微偏差不影响按 年/月/日 的判定。
static bool ComputeYesterdayLocal(const SYSTEMTIME& now, SYSTEMTIME& outYesterday)
{
    FILETIME ft;
    if (!::SystemTimeToFileTime(&now, &ft))
    {
        return false;
    }
    ULONGLONG ull = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    const ULONGLONG kFileTimePerDay = 864000000000ULL;   // 一天的 100ns 数 = 86400 * 1e7
    if (ull < kFileTimePerDay)
    {
        return false;
    }
    ull -= kFileTimePerDay;
    ft.dwLowDateTime  = (DWORD)(ull & 0xFFFFFFFFULL);
    ft.dwHighDateTime = (DWORD)(ull >> 32);
    return ::FileTimeToSystemTime(&ft, &outYesterday) != FALSE;
}

// 把会话最后消息的时间戳（Unix 毫秒）格式化成会话列表右上角的相对时间文字：
//   今天      → "HH:MM"
//   昨天      → "昨天"
//   今年其余  → "MM-DD"
//   更早      → "YYYY-MM-DD"
// 时间戳为 0（无最后消息）或换算失败返回空串。换算用本地时区。
static CString FormatConversationTime(int64_t timestampMs)
{
    if (timestampMs <= 0)
    {
        return CString();
    }

    SYSTEMTIME stMsg;
    if (!UnixMsToLocalSystemTime(timestampMs, stMsg))
    {
        return CString();
    }

    SYSTEMTIME stNow;
    ::GetLocalTime(&stNow);

    CString text;
    if (stMsg.wYear == stNow.wYear
        && stMsg.wMonth == stNow.wMonth
        && stMsg.wDay == stNow.wDay)
    {
        // 今天：显示 时:分。
        text.Format(_T("%02d:%02d"), stMsg.wHour, stMsg.wMinute);
        return text;
    }

    // "昨天" 判定：把"现在"往前推一天得到昨天的本地日期再比较。
    SYSTEMTIME stYesterday;
    if (ComputeYesterdayLocal(stNow, stYesterday)
        && stMsg.wYear == stYesterday.wYear
        && stMsg.wMonth == stYesterday.wMonth
        && stMsg.wDay == stYesterday.wDay)
    {
        return CString(_T("昨天"));
    }

    if (stMsg.wYear == stNow.wYear)
    {
        // 今年其余：月-日。
        text.Format(_T("%02d-%02d"), stMsg.wMonth, stMsg.wDay);
        return text;
    }

    // 更早：年-月-日。
    text.Format(_T("%d-%02d-%02d"), stMsg.wYear, stMsg.wMonth, stMsg.wDay);
    return text;
}

// 会话列表的排序口径（置顶优先 → 时间倒序 → cid 兜底）已收敛到
// BalloonMain/ConversationListOps.h 的 SortConversations，本文件各处直接调它；
// 那边是纯函数，有单元测试（Tests/ConversationListOpsTests.cpp）。

// (PR9 占位): 真数据 XML 构造 helper 与 RefreshXxx 实现待 PR8 完成 balloonui
// DuiTreeView 增强后,改用 DuiTreeView API 直接构造,不再走 listitem XML 路径。

// =======================================================================
// 构造 / 析构
// =======================================================================

CBalloonMainPanel::CBalloonMainPanel(CFlamingoClient* pFMGClient)
    : m_lpFMGClient(pFMGClient)
    , m_pAppHost(NULL)
    , m_sessionController(pFMGClient)
    , m_netSettingsWindow(pFMGClient)
    , m_hTitleIcon(NULL)
    , m_searchWarmUpPending(false)   // 构造时没有在途的预热消息
    , m_contactWindow(pFMGClient)
    , m_createGroupDlg(pFMGClient)
    , m_forwardTimerOn(false)
    , m_changePwdDlg(pFMGClient)
    , m_modifyAvatarDlg(pFMGClient)
    , m_systemSettingDlg(pFMGClient)
    , m_historyWindow(pFMGClient)
    , m_announceBoxWindow(pFMGClient)
    , m_currentTabId(balloonmain::kIdTabContacts)
    , m_avatarCache(balloonmain::MakeMonogramAvatar, balloonmain::MakeRoundedAvatarFromFile)
    , m_favoritesRootId(-1)
    , m_pGroupManageWindow(NULL)
    , m_groupManageGroupId(0)
    , m_groupInfoDlgGroupId(0)
    , m_transferPickGroupId(0)
    , m_pPanelToast(NULL)
    , m_rangeDeletedRows(0)          // 按时间范围清理本地库：当前区间已删行数
    , m_rangeDeleteRunning(false)    // 按时间范围清理本地库：没有续批消息在途
    , m_presenceBeforeChange(kMyPresenceOnline)   // 默认在线(与 v2 presence_online 同值)；ShowStatusMenu 改状态前会刷新它
    , m_connUiState(0)                            // kConnUiOnline：主面板是登录成功后才创建的
    // 主面板是登录成功后才创建的，故构造这一刻就是"刚登录"，静默期从这里起算。
    , m_starredSilenceUntilTick(::GetTickCount() + kStarredSilenceMs)
{
    // 状态徽标位图按需创建（见 GetPresenceBadgeBitmap），构造期先全部置空。
    for (int i = 0; i < kPresenceBadgeSlotCount; ++i)
    {
        m_presenceBadges[i] = NULL;
    }
}

CBalloonMainPanel::~CBalloonMainPanel()
{
    // 子窗口与头像位图缓存的回收，与"登出复位"要做的事完全一样，故统一收敛到两个私有
    // 辅助函数里，避免析构与 ResetForLogout 各写一份、日后改动只改了其中一份。
    DestroyOwnedSubWindows();

    // 搜索下拉只在 InitUi 里创建一次、登出时不销毁（理由见 DestroyOwnedSubWindows），
    // 因此它的销毁单独放在这里。不销毁的话，本对象析构时它仍持有有效 HWND，会触发
    // ATL CWindowImplRoot 的析构断言。
    if (m_searchDropdown.IsWindow())
        m_searchDropdown.DestroyWindow();

    // 状态徽标位图缓存由本类持有（DuiTreeView 只借用指针），这里统一释放。
    // 这批位图与账号无关（固定 4 槽、按 presence 取值，见 GetPresenceBadgeBitmap），
    // 因此只在析构时释放；ResetForLogout 不动它们，留给下一个账号继续用。
    for (int i = 0; i < kPresenceBadgeSlotCount; ++i)
    {
        if (m_presenceBadges[i] != NULL)
        {
            ::DeleteObject(m_presenceBadges[i]);
            m_presenceBadges[i] = NULL;
        }
    }

    // 客户区控件树与 HWND 由 DuiFrameWindow 基类析构兜底。
    // 头像位图缓存由本类持有，这里统一释放。
    ReleaseAvatarBitmapCache();

    // 标题栏图标位图：DuiFrameWindow::SetIcon 只借用不释放，由本类收回。
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
}

void CBalloonMainPanel::DestroyOwnedSubWindows()
{
    // 单聊窗口由本类堆分配持有，这里统一销毁并释放。
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL)
        {
            if (it->second->IsWindow())
            {
                it->second->DestroyWindow();
            }
            delete it->second;
        }
    }
    m_chatWindows.clear();

    // 群聊窗口同样由本类堆分配持有，这里统一销毁并释放。
    for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.begin();
         it != m_groupChatWindows.end(); ++it)
    {
        if (it->second != NULL)
        {
            if (it->second->IsWindow())
            {
                it->second->DestroyWindow();
            }
            delete it->second;
        }
    }
    m_groupChatWindows.clear();

    // 群管理对话框（同一时刻最多一个）由本类堆分配持有，这里销毁并释放。
    if (m_pGroupManageWindow != NULL)
    {
        if (m_pGroupManageWindow->IsWindow())
        {
            m_pGroupManageWindow->DestroyWindow();
        }
        delete m_pGroupManageWindow;
        m_pGroupManageWindow = NULL;
    }
    m_groupManageGroupId = 0;

    // 特别关注上线提醒浮窗：先清掉已经攒下、用户还没看到的提醒行，再销毁窗口。
    // HideAndClear 内部会判断窗口是否存在，窗口未创建时也可以照常调用。
    m_starredPopup.HideAndClear();

    // 本类按值持有的顶层窗口成员（搜索下拉 / 联系人资料窗 / 各对话框 / 各浮窗）。
    // 它们都是独立顶层窗口（DuiFrameWindow / DuiHost / DuiPopupHost 派生，皆 ATL
    // CWindowImpl 派生），不是本面板的子窗口，DestroyWindow(本面板) 不会连带销毁；
    // 若不在此先销毁，它们各自的析构会在仍持有有效 HWND 时触发 ATL CWindowImplRoot
    // 断言（atlwin.h:3238 "Object deleted before window was destroyed"）——这正是
    // "登录成功后从托盘菜单退出崩溃"的根因（最常见的是搜索框用过后残留的 m_searchDropdown）。
    // C++ 成员析构在析构函数体执行完之后、按声明逆序进行，故在此显式 DestroyWindow
    // 即可保证它们的 HWND 先被清掉。
    // 注意这里<u>没有</u>销毁 m_searchDropdown。它与其它窗口不同：其余各窗都是每次
    // 用到时现开（OpenForXxx / ShowAnnouncement 内部建窗），而搜索下拉只在 InitUi 里
    // 创建一次。主面板改成常驻窗口之后 InitUi 不会再跑第二次，登出时若把它销毁，
    // 下次登录搜索框就再也弹不出结果了。它的销毁只在析构函数里做。
    if (m_contactWindow.IsWindow())
        m_contactWindow.DestroyWindow();
    if (m_profileDlg.IsWindow())
        m_profileDlg.DestroyWindow();
    if (m_groupInfoDlg.IsWindow())
        m_groupInfoDlg.DestroyWindow();
    if (m_signEditPopup.IsWindow())
        m_signEditPopup.DestroyWindow();
    if (m_createGroupDlg.IsWindow())
        m_createGroupDlg.DestroyWindow();
    if (m_forwardDlg.IsWindow())
        m_forwardDlg.DestroyWindow();
    // 聊天记录查看窗同样是独立的顶层窗口，逐个销毁后清空（对象析构时仍持有窗口句柄会触发断言）
    for (size_t i = 0; i < m_mergedViewers.size(); ++i)
    {
        if (m_mergedViewers[i] && m_mergedViewers[i]->IsWindow())
            m_mergedViewers[i]->DestroyWindow();
    }
    m_mergedViewers.clear();
    if (m_changePwdDlg.IsWindow())
        m_changePwdDlg.DestroyWindow();
    if (m_modifyAvatarDlg.IsWindow())
        m_modifyAvatarDlg.DestroyWindow();
    if (m_closePromptDlg.IsWindow())
        m_closePromptDlg.DestroyWindow();
    if (m_systemSettingDlg.IsWindow())
        m_systemSettingDlg.DestroyWindow();
    if (m_aboutDlg.IsWindow())
        m_aboutDlg.DestroyWindow();
    // 下面五个同样是按值持有的独立顶层窗口，2026-08-18 修复「登出后窗口不关闭」时补齐 —— 此前它们
    // 既不在析构里销毁、也不随主面板一起销毁，用户登出后会一直留在屏幕上。
    if (m_historyWindow.IsWindow())
        m_historyWindow.DestroyWindow();
    if (m_announceBoxWindow.IsWindow())
        m_announceBoxWindow.DestroyWindow();
    if (m_announcePopup.IsWindow())
        m_announcePopup.DestroyWindow();
    if (m_starredPopup.IsWindow())
        m_starredPopup.DestroyWindow();
    if (m_announcePopupDlg.IsWindow())
        m_announcePopupDlg.DestroyWindow();
}

void CBalloonMainPanel::ReleaseAvatarBitmapCache()
{
    LOG_INFO("MainPanel: releasing %u cached avatar bitmaps.", (unsigned)m_avatarCache.Count());
    m_avatarCache.Clear();
}

void CBalloonMainPanel::ResetForLogout()
{
    LOG_INFO("MainPanel::ResetForLogout: destroy sub-windows and clear per-login state.");

    // 已读上报的去重记录只在本次登录内有效，换账号或重新登录后须重新上报
    ResetReadReportDedupe();

    // 退群 / 解散 / 转让的待应答登记属于上一条连接，重新登录后旧应答不会再来
    m_groupExitTracker.Clear();
    m_transferPickGroupId = 0;

    // 先把远程桌面会话收尾：停被控端推流、关控制端观看窗、隐藏受控提示条。
    // 这一步在协作类抽出之前是缺的 —— 正在被控时登出，屏幕仍会
    // 继续往中转服务器推送，而用户看到的已经是登录窗，会以为早就断开了。
    m_rdCoordinator.ResetForLogout();

    // 通话同理：停铃声、停中转会话释放摄像头与麦克风、关掉通话窗。抽出之前
    // 这一步也是缺的，采集线程会跨越登出边界继续运行。
    m_callCoordinator.ResetForLogout();

    // 第一步：关掉本面板拉起的全部子窗口。
    //
    // 单聊窗、群聊窗、历史记录窗等都是 Create(NULL, ...) 建出来的独立顶层窗口，既不是
    // 本面板的子窗口、也没有把本面板设为 owner，所以销毁本面板并不会连带关掉它们。不在
    // 这里显式销毁的话，用户登出后这些窗口会继续留在屏幕上，而且还带着上一个账号的聊天
    // 内容。
    DestroyOwnedSubWindows();

    // 搜索下拉不销毁、只收起（理由见 DestroyOwnedSubWindows）。它内部引用的是
    // m_contacts / m_myGroups / m_contactDeptPaths 这三个容器本身，下面把容器清空
    // 之后它拿到的就是空结果，不会残留上一个账号的联系人。
    m_searchDropdown.HideDropdown();

    // 第二步：清空三棵树的节点。必须排在头像位图缓存释放之前 —— 树节点里存的是缓存中
    // HBITMAP 的原值（DuiTreeView::SetItemIcon 只借用、不复制），先释放缓存会让树在
    // 随后的重绘中用到已经销毁的 GDI 对象。
    // 本类不持有树控件指针，三棵树都按控件 id 现查（与各 FillXxxTree 的做法一致）。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content != NULL)
    {
        balloonwjui::DuiTreeView* tvContacts = dynamic_cast<balloonwjui::DuiTreeView*>(
            content->FindCtrlById(balloonmain::kIdContactsTree));
        if (tvContacts != NULL)
        {
            tvContacts->Clear();
        }

        // 群树与会话树还带行装饰（成员数 / 未读角标 / 置顶与免打扰标记）。基类 Clear
        // 不认识子类的装饰映射，须另外清一次。
        balloonmain::MainPanelTreeView* tvGroups = dynamic_cast<balloonmain::MainPanelTreeView*>(
            content->FindCtrlById(balloonmain::kIdGroupsTree));
        if (tvGroups != NULL)
        {
            tvGroups->Clear();
            tvGroups->ClearAllRowDecorations();
        }

        balloonmain::MainPanelTreeView* tvMessages = dynamic_cast<balloonmain::MainPanelTreeView*>(
            content->FindCtrlById(balloonmain::kIdMessagesTree));
        if (tvMessages != NULL)
        {
            tvMessages->Clear();
            tvMessages->ClearAllRowDecorations();
        }
    }

    // 第三步：释放头像位图缓存（三棵树已在上一步清空，不再有节点引用这些位图）。
    // 换账号登录后真图头像的本地路径会变，旧账号的位图留着也用不上。
    ReleaseAvatarBitmapCache();

    // 第四步：清空本次登录期间累积的全部数据缓存。这些容器都以 userid / groupid /
    // 树节点 id 为键，换个账号登录后键的含义完全变了，不清就会串账号：轻则新账号看到上
    // 一个账号的联系人与会话，重则点某一行跳到不相干的人。
    m_contacts.clear();
    m_contactIndex.Clear();   // 资料池清空后索引必须同步清空，否则下标会越界
    m_departments.clear();
    m_contactNodeUserId.clear();
    m_contactNodeParent.clear();
    m_contactOrgNode.clear();
    m_contactDeptPaths.clear();
    m_contactDeptInfo.clear();
    m_lastContactsResult = net::ContactsResult();

    m_conversations.clear();
    m_msgNodeConv.clear();
    m_convNodeId.clear();
    m_peerReadMsgids.clear();

    m_myGroups.clear();
    m_groupNodeId.clear();
    m_groupInfo.clear();
    // 这一项漏清的后果最不容易察觉：它记录"哪些群已经发过资料请求"。不清的话，新账号的
    // 群一旦群 id 与旧账号的记录重合，就再也不会发出资料请求，群名与成员数会一直为空。
    m_groupInfoRequested.clear();
    m_favoriteGroupNodeId.clear();
    m_favoritesRootId = -1;
    m_favoriteSectionNodes.clear();

    // 搜索候选缓存里存着上一个账号联系人与群的名字。下次按键时的 Sync 本来也会把它
    // 截成空，这里直接清掉，不让这些数据在登出后继续留在内存里。
    m_searchDropdown.ClearCandidates();
    // 在途的预热消息若因窗口销毁而不再送达，标志会一直停在 true、以后再也不会预热，
    // 故一并复位；即使那条消息稍后仍被处理，也只是多做一次 Sync，结果不变。
    m_searchWarmUpPending = false;

    // 乐观更新的回滚快照：都是"请求已发出、应答未回"期间的临时存档。连接已经断开，对应
    // 的应答不会再来，留着只会在下次登录时被当成本次的回滚数据用。
    m_pendingRemarkRollback.clear();
    m_pendingTagsRollback.clear();
    m_signBeforeUpdate.Empty();
    m_presenceBeforeChange = kMyPresenceOnline;

    // 收藏与特别关注两个模型同属账号数据，一并清空。
    m_favorites.Reset(std::vector<net::FavoriteItem>());
    m_starred.Clear();

    // 陌生人来消息时"要不要重新拉取通讯录"的限流记录，同样只在本次登录内有意义。
    m_unknownSenderPolicy.Reset();

    // 群资料对话框的随行状态（对话框本身已在第一步销毁）。
    m_groupInfoDlgGroupId = 0;
    m_groupInfoDlgAnnouncement.Empty();
    m_groupInfoDlgOwnerName.Empty();
    m_groupInfoDlgMyCard.Empty();

    // 本地消息库按账号分库，登出时必须关掉，否则下次登录会继续往上一个账号的库里写。
    m_msgStore.Close();
    m_msgStoreDbPath.clear();

    // 界面状态回到"刚建好"的样子：默认停在联系人页，连接提示条按在线态显示。
    m_currentTabId = balloonmain::kIdTabContacts;
    m_connUiState = balloonmain::kConnUiOnline;

    // 重新起算特别关注上线提醒的静默期。下次登录时服务端会把所有在线的关注对象一次性推
    // 过来，不静默的话屏幕右下角会瞬间弹出一整列提醒。
    m_starredSilenceUntilTick = ::GetTickCount() + kStarredSilenceMs;
}

// =======================================================================
// 创建 / 显示
// =======================================================================

/**
 *  把指定窗口的任务栏按钮摘掉（走 ITaskbarList::DeleteTab）。
 *
 *  主面板不该占用任务栏按钮（它由系统托盘图标调出），但<u>不能</u>用
 *  WS_EX_TOOLWINDOW 来实现这一点 —— 那个样式会连带改掉窗口的激活语义，引出
 *  "最小化聊天窗后主面板被别的程序盖住"的问题，来龙去脉见
 *  `docs/主面板被其它窗口盖住问题排查.md`。本函数是它的替代品：窗口保持普通
 *  顶层窗口（激活语义不变），只把任务栏里那个按钮摘掉。
 *
 *  两个实现上的坑：
 *    · UI 线程并没有全局初始化过 COM（客户端只在个别对话框里就地 CoInitialize），
 *      所以这里自行初始化并配平；已初始化时返回 S_FALSE，同样要 CoUninitialize
 *      才不改变引用计数语义。
 *    · 任务栏按钮是在窗口<u>显示之后</u>才存在的，且每次重新显示都会再长出来
 *      （托盘收起再调出主面板即属此列），故建窗显示后与后续每次 WM_SHOWWINDOW
 *      都要各调一次，见 CreateAndShow 与 OnShowWindowForTaskbar。
 *
 *    hWnd：要摘掉任务栏按钮的窗口；NULL / 无效窗口时直接返回。
 */
static void RemoveFromTaskbar(HWND hWnd)
{
    if (hWnd == NULL || !::IsWindow(hWnd))
    {
        return;
    }

    const HRESULT hrCo = ::CoInitialize(NULL);
    const bool bNeedUninit = SUCCEEDED(hrCo);

    HRESULT hr = E_FAIL;
    ITaskbarList* pTaskbar = NULL;
    hr = ::CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER,
                            IID_ITaskbarList, (void**)&pTaskbar);
    if (SUCCEEDED(hr) && pTaskbar != NULL)
    {
        hr = pTaskbar->HrInit();
        if (SUCCEEDED(hr))
        {
            hr = pTaskbar->DeleteTab(hWnd);
        }
        pTaskbar->Release();
    }

    // 失败不影响功能（顶多任务栏上多一个按钮），但要留痕：这类问题只在特定
    // 系统 / 时序下出现，没有日志根本无从查起。
    if (FAILED(hr))
    {
        LOG_WARNING(_T("摘除主面板任务栏按钮失败: hwnd=0x%p hr=0x%08X。"),
                    (void*)hWnd, (unsigned int)hr);
    }

    if (bNeedUninit)
    {
        ::CoUninitialize();
    }
}

void CBalloonMainPanel::EnsureCreated()
{
    // 幂等：窗口已经建出来就直接返回。主面板是常驻窗口，进程启动时建一次，
    // 之后每次登录都复用同一个 HWND，中途的登出只隐藏、不销毁。
    if (IsWindow())
    {
        return;
    }

    // 主面板不占用任务栏按钮（由系统托盘图标 / 托盘左键调出），但实现方式是
    // <u>普通窗口 + ITaskbarList::DeleteTab</u>，扩展样式必须留空。
    //
    // <u>这里曾经用的是 WS_EX_TOOLWINDOW，2026-08-12 因下述问题改掉，不要改回去</u>
    // （完整排查记录见 `docs/主面板被其它窗口盖住问题排查.md`）：
    //
    //   工具窗口会被系统排除在"最小化某个窗口之后该激活谁"的候选之外。于是在主
    //   面板前面打开一个聊天窗、再最小化该聊天窗时，激活权<u>不会</u>回到紧挨其下
    //   的主面板，而是落到 Z 序里下一个普通窗口（复现时是资源管理器）；那个窗口被
    //   激活后浮到主面板上方，观感上就是"主面板沉到了最底层"。
    //
    //   注意主面板此时既没有被移动、也没有被隐藏 —— 日志实测它一条 Z 序变动消息都
    //   没收到、始终可见。所以排查这类现象时不要去找"哪里把主面板压下去了"，那里
    //   什么都没有，症结是它<u>拿不到激活权</u>。该机理波及所有在主面板前面打开、
    //   又可被最小化的独立顶层窗口（群聊窗、历史记录窗等），不止单聊窗。
    //
    //   改成普通窗口后，最小化聊天窗时主面板会正常收到 WM_ACTIVATE 并浮到最前，
    //   已实测确认。<u>已知代价</u>：Alt+Tab 的收录规则与任务栏不是一回事，主面板
    //   会出现在 Alt+Tab 列表里 —— 这一点是权衡后接受的。
    //
    // owner 仍为 NULL：无 owner 的普通顶层窗口本会进任务栏，靠建窗后与每次显示时
    // 调 RemoveFromTaskbar 摘掉按钮（按钮在窗口显示后才存在，且每次重新显示都会
    // 再长出来，故两处都要调）。
    const DWORD dwMainPanelExStyle = 0;
    if (Create(NULL, CWindow::rcDefault, _T("FlamingoIM"),
               WS_OVERLAPPEDWINDOW, dwMainPanelExStyle) == NULL)
    {
        LOG_ERROR(_T("主面板窗口 Create 失败。"));
        return;
    }

    // 标题栏：常规（非透明）标题栏，只承载 min/close —— 主面板为固定
    // 窄高窗，不提供最大化。
    SetTitle(_T("FlamingoIM"));
    SetTitleBarHeight(kMainPanelTitleBarHeight);
    SetButtons(true, false, true);

    // 标题栏左侧图标：程序主图标合成到标题栏底色上再挂上去（合成的缘由见
    // TitleBarIcon.h）。SetIcon 只借用位图，故位图由本类持有、析构时释放。
    //
    // 先释放上一次创建的位图：本方法在"登出后重新登录"这条路径上会被调用第二次，
    // 直接覆盖 m_hTitleIcon 会让前一只位图再也没有机会被释放。
    if (m_hTitleIcon != NULL)
    {
        ::DeleteObject(m_hTitleIcon);
        m_hTitleIcon = NULL;
    }
    m_hTitleIcon = balloonmain::CreateProgramIconBitmap(kMainPanelTitleIconSize,
                                                        kMainPanelTitleBarBg);
    if (m_hTitleIcon != NULL)
    {
        SetIcon(m_hTitleIcon);
    }
    else
    {
        LOG_WARNING(_T("主面板标题栏图标位图创建失败，标题栏只显示文字。"));
    }

    SetMinSize(kMainPanelMinWidth, kMainPanelMinHeight);

    // 开启双击合成：联系人树需要"双击联系人才开聊"，而 balloonui 窗口默认
    // 无 CS_DBLCLKS、收不到双击，须显式开启。
    EnableDoubleClick(true);

    InitUi();

    // 建窗阶段到此为止，窗口保持隐藏。尺寸、居中、显示都留给 ShowAfterLogin —— 
    // 进程启动时还没有登录用户，这时候把窗口显示出来是不对的。
    ResizeClient(kMainPanelWidth, kMainPanelHeight);
    CenterWindow();

    // 协调器要用本窗口句柄做提示框属主，并接收受控提示条投回来的「结束控制」。
    m_rdCoordinator.Attach(m_hWnd);
    // 通话协调器还要拿主面板与网络客户端：前者用来把通话结果写进聊天窗，
    // 后者用来读静音开关与本人 userid。
    m_callCoordinator.Attach(m_hWnd, this, m_lpFMGClient);

    LOG_INFO("MainPanel::EnsureCreated: window created (hidden).");
}

void CBalloonMainPanel::ShowAfterLogin()
{
    if (!IsWindow())
    {
        LOG_ERROR("MainPanel::ShowAfterLogin called before the window was created.");
        return;
    }

    // 用本次登录用户的资料刷新 Header（昵称 / 签名 / 头像 / 在线状态胶囊）。
    // 建窗发生在登录之前，那时 m_UserInfo 还是空的，所以这一步必须放在这里，
    // 不能留在 InitUi 里只做一次。
    RefreshSelfAvatar();

    ShowWindow(SW_SHOW);
    UpdateWindow();
    ::SetForegroundWindow(m_hWnd);

    // 窗口已显示，任务栏按钮此时才存在，摘掉它。后续每次重新显示（托盘调出主面板）
    // 都会再长出来，那一路由 OnShowWindowForTaskbar 兜住。
    RemoveFromTaskbar(m_hWnd);

    LOG_INFO("MainPanel::ShowAfterLogin: panel shown.");
}

void CBalloonMainPanel::HideForLogout()
{
    if (!IsWindow())
    {
        return;
    }
    ShowWindow(SW_HIDE);
    LOG_INFO("MainPanel::HideForLogout: panel hidden.");
}

void CBalloonMainPanel::CreateAndShow()
{
    // 保留这个入口是为了不改动既有调用方（登录成功后的显示、命令行调试入口）。
    EnsureCreated();
    ShowAfterLogin();
}

// =======================================================================
// 客户区
// =======================================================================

void CBalloonMainPanel::InitUi()
{
    // 构建主面板客户区控件树（来自 BalloonMainLayout 的内嵌 XML）。
    std::unique_ptr<balloonwjui::DuiControl> root = balloonmain::BuildMainPanelBody();
    if (!root)
    {
        // 内嵌 XML 为编译期常量、形态固定，正常情况下不会解析失败。
        LOG_ERROR(_T("BuildMainPanelBody 返回空 —— 主面板客户区未能构建。"));
        return;
    }
    SetClientContent(std::move(root));

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        LOG_ERROR(_T("SetClientContent 之后 GetClientContent 仍为空。"));
        return;
    }

    // 轻提示条挂在客户区根容器末尾（基类按子控件顺序绘制，末尾即最上层），布局 Fixed(0)
    // 使它不占竖直方向的空间，落点由 DuiToast 自己按"顶部居中"计算。与群资料窗口同一做法
    m_pPanelToast = NULL;
    balloonwjui::DuiVBox* rootBox = dynamic_cast<balloonwjui::DuiVBox*>(content);
    if (rootBox != NULL)
    {
        std::unique_ptr<balloonwjui::DuiToast> toast(new balloonwjui::DuiToast());
        m_pPanelToast = toast.get();
        rootBox->AddChild(std::move(toast), balloonwjui::DuiLayout::Hint().Fixed(0));
    }

    // 搜索框是无窗口控件，构建出来就能用 —— 既不必在宿主窗口就绪后补一次创建，
    // 也没有可供挂接键盘拦截的子窗口。按 Esc 收起结果下拉改由控件发出的
    // DUIN_EDIT_ESCAPE 通知驱动，见 OnDuiNotify 里对应的分支。

    // 搜索结果下拉：作为主面板的 WS_CHILD 子窗口创建一次（初始隐藏）。用户在上面
    // 的搜索框输入时，由 OnDuiNotify 的 DUIN_VALUECHANGED 分支驱动它定位 / 显示 /
    // 刷新（见 UpdateSearchDropdown）。数据源在每次刷新时实时绑定主面板缓存。
    if (m_searchDropdown.EnsureCreated(m_hWnd))
    {
        m_searchDropdown.SetPickCallbacks(&CBalloonMainPanel::OnSearchPick,
                                          &CBalloonMainPanel::OnSearchPickGroup, this);
        // 结果行右侧那个悬停出现的「定位」小按钮。注册了它，联系人结果行才会画出
        // 这个按钮；点行本身仍然是打开单聊，两条路各走各的。
        m_searchDropdown.SetLocateCallback(&CBalloonMainPanel::OnSearchLocate);
    }

    // 三个 Tab 内容面板默认只显示"联系人"页（与当前选中 Tab 一致），
    // 其余两页隐藏 —— DuiLayout 跳过隐藏子控件，当前页撑满内容区。
    // Tab 点击切换显隐留待后续 P2 阶段。
    balloonwjui::DuiControl* paneMsg =
        content->FindCtrlById(balloonmain::kIdPaneMessages);
    balloonwjui::DuiControl* paneCt =
        content->FindCtrlById(balloonmain::kIdPaneContacts);
    balloonwjui::DuiControl* paneGrp =
        content->FindCtrlById(balloonmain::kIdPaneGroups);
    if (paneMsg != NULL)
    {
        paneMsg->SetVisible(false);
    }
    if (paneGrp != NULL)
    {
        paneGrp->SetVisible(false);
    }

    // 三个面板的悬浮滚动条要给窗口右边框的缩放抓握带让位，否则整条滚动条都
    // 落在缩放边框里、鼠标只会变成左右双向箭头、滑块抓不住。
    RefreshScrollBarEdgeInset();

    // 19a:Contacts 页改用 DuiTreeView,节点由这里填充(数据源仍是 mock)。
    FillContactsTree();
    // 19b:Groups 页同样换 DuiTreeView,这里填群列表。
    FillGroupsTree();
    // 19c:Messages 页扁平 DuiTreeView,填会话。
    FillMessagesTree();

    // Header 的昵称 / 签名 / 头像 / 在线状态胶囊。数据源是 CFlamingoClient::OnLoginResult
    // 填好的 m_UserMgr.m_UserInfo，helper 的测试覆盖见 MainPanelUserDataTests.cpp。
    //
    // 注意建窗发生在登录<u>之前</u>（主面板是常驻窗口，进程启动即创建），此刻 m_UserInfo
    // 还是空的，所以这里填进去的是一份空壳；真正有内容的那一次由 ShowAfterLogin 再刷一遍。
    RefreshSelfAvatar();

    LOG_INFO(_T("主面板客户区 UI 初始化完成。"));
}

void CBalloonMainPanel::FillContactsTree()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    balloonwjui::DuiTreeView* tv = dynamic_cast<balloonwjui::DuiTreeView*>(
        content->FindCtrlById(balloonmain::kIdContactsTree));
    if (tv == NULL)
    {
        LOG_WARNING(_T("Contacts TreeView(kIdContactsTree) 未找到,跳过填充。"));
        return;
    }

    // 行高 40 = 主行 (备注名) + 副标签 (职位 / 签名);与 BalloonMainLayout
    // 里 <treeview row-height="40"> 对齐。SetRowHeight 是全局值。
    tv->SetRowHeight(40);

    // 19f:头像走 monogram bitmap(GDI+ AA 圆 + AA 文字)。SetIconUsesAlpha(true)
    // 启用 DuiTreeView 的 AlphaBlend 绘制路径——位图自身就是带 AA 边缘的圆形,
    // 边缘无锯齿。SetIconCornerRadius 在 alpha 路径下被忽略,保留是为了便于
    // 将来想退回 HRGN 路径(SetIconUsesAlpha(false))时不用补回来。
    const int kAvatarSizePx = 32;
    tv->SetIconSize(kAvatarSizePx);
    tv->SetIconCornerRadius(kAvatarSizePx / 2);
    tv->SetIconUsesAlpha(true);

    // 「我的好友」分组 —— 根节点显示 "我的好友 (在线N/总M)"。
    {
        const std::vector<balloonmain::MockContact>& favs = balloonmain::MockFavorites();
        int favOnline = 0;
        for (size_t i = 0; i < favs.size(); ++i)
        {
            if (favs[i].status == balloonmain::kMockOnline)
            {
                ++favOnline;
            }
        }
        CString rootLabel;
        rootLabel.Format(_T("我的好友  %d / %d"), favOnline, (int)favs.size());
        int rootId = tv->AddRoot(rootLabel);
        tv->SetExpanded(rootId, true);
        // root 节点是分组标题,点击只 toggle 展开,不显示选中底色。
        tv->SetItemSelectable(rootId, false);

        for (size_t i = 0; i < favs.size(); ++i)
        {
            const balloonmain::MockContact& c = favs[i];
            CString display = c.remark.IsEmpty() ? c.name : c.remark;
            int id = tv->AddChild(rootId, display);
            if (!c.title.IsEmpty())
            {
                tv->SetItemSubLabel(id, c.title);
            }
            // 在线状态徽标:mock status 先换算到 presence,再取徽标位图,与登录后
            // 真实数据那条路径逐字同源 —— 否则调试入口里核对过的观感到了正式环境又变一副样子。
            tv->SetItemStatusIcon(id, GetPresenceBadgeBitmap(
                balloonmain::MockPresenceFromStatus(c.status)));
            // 「特别关注」星标：调试入口没有真实关注名单，按下标每隔两个标一个，
            // 好让 --mainpanel 能直接核对观感（标记行与未标记行同屏对照）。
            // 正式数据那条路走 ApplyStarredMark，读的是 m_starred 名单。
            balloonmain::MainPanelTreeView* mtv =
                dynamic_cast<balloonmain::MainPanelTreeView*>(tv);
            if (mtv != NULL)
            {
                mtv->SetItemStarred(id, (i % kMockStarredStride) == 0);
            }
            // monogram avatar(底色 + 单字)。位图归头像缓存所有,这里只借用。
            HBITMAP hAvatar = m_avatarCache.GetMonogram(c.avatarText, c.avatarColor, kAvatarSizePx);
            if (hAvatar != NULL)
            {
                tv->SetItemIcon(id, hAvatar);
            }
        }
    }

    // 「服务号」分组 —— 根节点显示 "服务号 (N)";服务号无在线状态点。
    {
        const std::vector<balloonmain::MockContact>& svc = balloonmain::MockServiceAccounts();
        CString rootLabel;
        rootLabel.Format(_T("服务号  %d"), (int)svc.size());
        int rootId = tv->AddRoot(rootLabel);
        tv->SetExpanded(rootId, true);
        // root 节点是分组标题,点击只 toggle 展开,不显示选中底色。
        tv->SetItemSelectable(rootId, false);

        for (size_t i = 0; i < svc.size(); ++i)
        {
            const balloonmain::MockContact& c = svc[i];
            CString display = c.remark.IsEmpty() ? c.name : c.remark;
            int id = tv->AddChild(rootId, display);
            if (!c.signature.IsEmpty())
            {
                tv->SetItemSubLabel(id, c.signature);
            }
            // 服务号不带状态点,不调 SetItemStatusColor。
            HBITMAP hAvatar = m_avatarCache.GetMonogram(c.avatarText, c.avatarColor, kAvatarSizePx);
            if (hAvatar != NULL)
            {
                tv->SetItemIcon(id, hAvatar);
            }
        }
    }

    // 两个分组都填完之后,同步 scrollview 内容高度。
    SyncPaneContentHeight(balloonmain::kIdPaneContacts,
                          balloonmain::kIdContactsTree, kContactsPaneTitleHeight);
}

void CBalloonMainPanel::FillGroupsTree()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    balloonmain::MainPanelTreeView* tv = dynamic_cast<balloonmain::MainPanelTreeView*>(
        content->FindCtrlById(balloonmain::kIdGroupsTree));
    if (tv == NULL)
    {
        LOG_WARNING(_T("Groups TreeView(kIdGroupsTree) 未找到,跳过填充。"));
        return;
    }

    // 用真实群替换 mock：清空旧节点、旧行装饰（第一行右侧的成员数）、旧"节点→groupid"
    // 映射。基类 Clear 不认识子类的行装饰映射，而节点 id 会被复用，不清的话新群会继承
    // 上一批群的人数。头像位图归头像缓存所有，重建时不释放，新节点直接复用。
    tv->Clear();
    tv->ClearAllRowDecorations();
    m_groupNodeId.clear();

    // 行高同 Contacts 页(40px);与 XML row-height 对齐。
    tv->SetRowHeight(40);

    const int kAvatarSizePx = 32;
    tv->SetIconSize(kAvatarSizePx);
    tv->SetIconCornerRadius(kAvatarSizePx / 2);
    tv->SetIconUsesAlpha(true);   // 走 AlphaBlend 路径,圆形无锯齿

    // 群源 = 本人加入的全部群（cmd_get_user_groups 拉来的真实群列表，含无消息空群）。
    // 单根节点 —— "我的群聊 · N";子节点为每个群。
    CString rootLabel;
    rootLabel.Format(_T("我的群聊 · %d"), (int)m_myGroups.size());
    int rootId = tv->AddRoot(rootLabel);
    tv->SetExpanded(rootId, true);
    // root 节点是分组标题,点击只 toggle 展开,不显示选中底色。
    tv->SetItemSelectable(rootId, false);

    // 置顶的群排在最前面，置顶群之间、其余群之间各自保持群列表原有的顺序。
    // 只取显示顺序、不改 m_myGroups 本身，取消置顶后群会回到原来的位置。
    const std::vector<size_t> order = balloonmain::OrderGroupsForList(m_myGroups);
    for (size_t k = 0; k < order.size(); ++k)
    {
        const size_t i = order[k];
        int64_t gid = m_myGroups[i].m_groupid;

        // 行版式：第一行是"群名（左） + 成员数（右）"，第二行是群号。三段文案统一由
        // BuildGroupRowTexts 拼，与搜索下拉的群结果行共用同一口径。
        balloonmain::GroupRowTexts texts = balloonmain::BuildGroupRowTexts(
            GroupDisplayName(gid), m_myGroups[i].m_membercount, (__int64)gid);

        int id = tv->AddChild(rootId, texts.m_name);

        // 成员数放在第一行右侧。这里用的是 MainPanelTreeView 的上排文字，不是基类的
        // SetItemRightText —— 后者按整行垂直居中，两行节点里会落在群名与群号的中缝上。
        tv->SetItemRightTopText(id, texts.m_memberText);

        // 置顶样式与会话 Tab 完全一致（整行淡灰底 + 左侧橙色竖条）。群 Tab 不显示
        // 免打扰标记 —— 群行下排是群号，没有未读胶囊可以配合静音图标。
        if (m_myGroups[i].m_pinned != 0)
        {
            tv->SetItemConversationFlags(id, true, false);
        }

        // 「我的收藏」徽章：画在群名之后。收藏状态在别处改动时由 RefreshFavoriteMarks
        // 就地刷新。
        ApplyFavoriteMark(tv, id, 2, gid);

        // 副标签（第二行）放群号。人数未知时这一行仍在，各行高度保持一致。
        tv->SetItemSubLabel(id, texts.m_groupNoText);

        // 群头像：尊重群主设置的 (facetype, customface)——字形自定义现画、真图命中
        // 缓存即画、真图未缓存回退 monogram 并触发下载，与联系人树同一套。位图归
        // 头像缓存所有。
        //
        // 头像两项取自 m_groupInfo 而<u>不是</u> m_myGroups：后者只在拉群列表(1320)
        // 时整体替换，改群头像并不会让它重拉，从它取会一直画着登录时那份旧头像
        // （会话列表读的是 m_groupInfo，于是出现"会话列表换了、群列表没换"）。
        // m_myGroups 在这里只负责"有哪些群"。
        int         gFacetype   = m_myGroups[i].m_facetype;
        std::string gCustomface = m_myGroups[i].m_customface;
        std::map<int64_t, GroupBrief>::const_iterator brIt = m_groupInfo.find(gid);
        if (brIt != m_groupInfo.end())
        {
            gFacetype   = brIt->second.m_facetype;
            gCustomface = brIt->second.m_customface;
        }
        HBITMAP hAvatar = GetEntityAvatarBitmap(
            gid, texts.m_name, gFacetype, gCustomface, kAvatarSizePx);
        if (hAvatar != NULL)
        {
            tv->SetItemIcon(id, hAvatar);
        }

        // 记录"节点 → groupid"，供点击开群聊。
        m_groupNodeId[id] = gid;
    }

    // 节点填完之后,同步 scrollview 内容高度。
    // 群页 treeview 之上没有标题 label("我的群聊 · N"由上面的根节点承担),
    // 所以标题高度传 0。
    SyncPaneContentHeight(balloonmain::kIdPaneGroups,
                          balloonmain::kIdGroupsTree, kGroupsPaneTitleHeight);
}

void CBalloonMainPanel::FillMessagesTree()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    balloonwjui::DuiTreeView* tv = dynamic_cast<balloonwjui::DuiTreeView*>(
        content->FindCtrlById(balloonmain::kIdMessagesTree));
    if (tv == NULL)
    {
        LOG_WARNING(_T("Messages TreeView(kIdMessagesTree) 未找到,跳过填充。"));
        return;
    }

    // 会话行高 48px:容纳头像 + 主行(标题) + 副行(末条消息);与 XML 对齐。
    tv->SetRowHeight(48);

    // 会话头像稍大一点,与设计稿对齐。
    const int kAvatarSizePx = 36;
    tv->SetIconSize(kAvatarSizePx);
    tv->SetIconCornerRadius(kAvatarSizePx / 2);
    tv->SetIconUsesAlpha(true);   // 走 AlphaBlend 路径,圆形无锯齿(详见 FillContactsTree 同一段说明)

    const std::vector<balloonmain::MockConversation>& convs = balloonmain::MockConversations();

    // 假数据里标「特别关注」时按<u>单聊行</u>计数，而不是按数组下标 —— 下标里混着群会话，
    // 按下标隔几个标一个，很可能一连几次都落在群上（群不能被关注），一个星也标不出来。
    int mockSingleIndex = 0;

    for (size_t i = 0; i < convs.size(); ++i)
    {
        const balloonmain::MockConversation& c = convs[i];
        // 扁平结构 —— 每个会话作为顶层 AddRoot(没子节点,DuiTreeView 不画 ▶)。
        int id = tv->AddRoot(c.title);
        if (!c.lastMsg.IsEmpty())
        {
            tv->SetItemSubLabel(id, c.lastMsg);
        }
        // 时间右对齐显示。若有未读数,拼到时间右侧括号里(DuiTreeView 暂未
        // 提供 badge API,这里用简化文字呈现;视觉 badge 待后续阶段)。
        CString rightText = c.time;
        if (c.unread > 0)
        {
            CString unreadText;
            unreadText.Format(_T("  (%d)"), c.unread);
            rightText += unreadText;
        }
        if (!rightText.IsEmpty())
        {
            tv->SetItemRightText(id, rightText);
        }
        // 单聊带在线状态徽标(同 MockContact 的逻辑);群会话不带。
        if (!c.isGroup)
        {
            tv->SetItemStatusIcon(id, GetPresenceBadgeBitmap(
                balloonmain::MockPresenceFromStatus(c.status)));
            // 「特别关注」星标：与通讯录树的假数据同一口径，每隔几行标一个，
            // 好在 --mainpanel 下核对会话行的观感（星标 + 时间 + 未读同时在场）。
            balloonmain::MainPanelTreeView* mtv =
                dynamic_cast<balloonmain::MainPanelTreeView*>(tv);
            if (mtv != NULL)
            {
                mtv->SetItemStarred(id, (mockSingleIndex % kMockStarredStride) == 0);
            }
            ++mockSingleIndex;
        }
        // monogram avatar。位图归头像缓存所有,这里只借用。
        HBITMAP hAvatar = m_avatarCache.GetMonogram(c.avatarText, c.avatarColor, kAvatarSizePx);
        if (hAvatar != NULL)
        {
            tv->SetItemIcon(id, hAvatar);
        }
    }

    // 节点填完之后,同步 scrollview 内容高度。Messages 页没有顶部标题行(看
    // BalloonMainLayout::BuildMessagesListXml,只有一个 treeview),传 0。
    SyncPaneContentHeight(balloonmain::kIdPaneMessages,
                          balloonmain::kIdMessagesTree, 0);
}

// =======================================================================
// 滚动视口内容高度同步
// =======================================================================

void CBalloonMainPanel::SyncPaneContentHeight(UINT paneId, UINT treeViewId,
                                              int paneTitleHeight)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiScrollView* sv = dynamic_cast<balloonwjui::DuiScrollView*>(
        content->FindCtrlById(paneId));
    if (sv == NULL)
    {
        return;
    }
    balloonwjui::DuiTreeView* tv = dynamic_cast<balloonwjui::DuiTreeView*>(
        content->FindCtrlById(treeViewId));
    if (tv == NULL)
    {
        return;
    }

    // 内容 vbox 上下 padding 之和（上下两边取值相同，见 kContactsPaneContentPadTop）。
    const int kVboxPadV = kContactsPaneContentPadTop * 2;
    const int totalH = paneTitleHeight + tv->GetContentHeight() + kVboxPadV;
    sv->SetContentHeight(totalH);

    // DuiScrollView::SetContentHeight 只调 UpdateRange + ApplyScrollToContent,
    // 不会重算 m_contentNominalRect。这里 ForceLayout 一次让 nominal rect
    // 跟上新高度。InitUi 阶段 scrollview 还没经历过 Layout,GetRect 是空的,
    // ForceLayout 此时无意义(也无害);真正的 Layout 由后续 ResizeClient +
    // 窗口可见触发,届时 DoLayout 会用本次设好的 m_contentH 算 nominal rect。
    RECT rc = sv->GetRect();
    if (rc.right > rc.left && rc.bottom > rc.top)
    {
        sv->ForceLayout(rc);
    }
}

// =======================================================================
// 19e:RefreshXxx 公开 API —— 用真实数据 (CUserMgr) 刷新对应 TreeView。
//      会话控制器 / CFlamingoClient 在好友列表 / 群 / 会话数据 ready 时调用。
// =======================================================================

void CBalloonMainPanel::RefreshContacts()
{
    // 企业版的联系人树来自 v2 通讯录应答（组织架构），由 RebuildContactsTree 用
    // 缓存的应答整棵重建。这里原本走的是 v1 好友列表适配器 —— 好友功能删除后那条
    // 路径已经不存在，直接委托过去，免得留下一个"调了等于没调"的方法。
    RebuildContactsTree();
}

void CBalloonMainPanel::RefreshGroups()
{
    // 群 Tab 现已由 v2 会话同步驱动（FillGroupsTree 从 m_conversations 的群会话取
    // 群源、群名取群资料缓存）。这里不再走 v1 CUserMgr 适配器（否则会用空的 v1
    // 群列表清掉真实群）；统一委托给真实填充，保证任何刷新触发都重绘真实群。
    FillGroupsTree();
}

void CBalloonMainPanel::RefreshMessages()
{
    // 会话列表同理：真实数据来自 v2 会话同步（m_conversations），由
    // FillConversationsTree 重建。原先那条 v1 适配器路径随好友功能一并删除。
    FillConversationsTree();
}

void CBalloonMainPanel::RefreshAll()
{
    RefreshContacts();
    RefreshGroups();
    RefreshMessages();
}

// =======================================================================
// 消息处理
// =======================================================================

LRESULT CBalloonMainPanel::OnCloseMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                      LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 点关闭按钮分三种情况：
    //   1) 还需提示（IsEnableExitPrompt 为真，默认）→ 弹关闭确认对话框，由它据用户
    //      选择执行最小化 / 退出；本面板保留不动。
    //   2) 不提示 + 记住的是"退出" → 隐藏本面板并请求退出程序。
    //   3) 不提示 + 记住的是"最小化" → 仅隐藏本面板（托盘图标常驻，可再唤出）。
    if (m_lpFMGClient != NULL && m_lpFMGClient->m_UserConfig.IsEnableExitPrompt())
    {
        m_closePromptDlg.OpenForClose(m_hWnd, m_hWnd, m_lpFMGClient);
        return 0;
    }

    BOOL exitWhenClose = (m_lpFMGClient != NULL)
                       ? m_lpFMGClient->m_UserConfig.IsEnableExitWhenCloseMainDlg()
                       : FALSE;
    if (IsWindow())
    {
        ShowWindow(SW_HIDE);
    }
    // 请求退出进程。本窗口自己处理 WM_CLOSE_MAINDLG（见 OnRequestAppExitMsg）。
    if (exitWhenClose)
    {
        ::PostMessage(m_hWnd, WM_CLOSE_MAINDLG, 0, 0);
    }
    return 0;
}

LRESULT CBalloonMainPanel::OnShowWindowForTaskbar(UINT /*uMsg*/, WPARAM wParam,
                                                  LPARAM /*lParam*/, BOOL& bHandled)
{
    // 只搭便车、不消费消息：置 FALSE 让它继续沿消息映射交给后面的 handler 与基类。
    bHandled = FALSE;

    // 任务栏按钮在窗口每次重新显示时都会再长出来（托盘收起主面板再调出即属此列），
    // 所以每次显示都要补摘一次。建窗那一次的调用在 CreateAndShow 里，两处配合才稳 ——
    // 本消息是在窗口"正在被显示"的过程中到达的，此刻按钮未必已经建好。
    if (wParam != FALSE)
    {
        RemoveFromTaskbar(m_hWnd);
    }
    return 0;
}

LRESULT CBalloonMainPanel::OnSysCommand(UINT /*uMsg*/, WPARAM wParam,
                                        LPARAM /*lParam*/, BOOL& bHandled)
{
    // 最小化 —— 主面板是 WS_EX_TOOLWINDOW 工具窗口，任务栏没有它的按钮，真让系统
    // 最小化会剩一条老式标题栏卡在屏幕左下角。故这里一律改为隐藏到托盘。
    if ((wParam & kSysCommandMask) == SC_MINIMIZE)
    {
        HideToTray();
        return 0;
    }

    // 其余 syscommand（含关闭按钮转来的 SC_CLOSE、拖动、缩放等）交回基类 /
    // DefWindowProc；SC_CLOSE 经 DefWindowProc 会再生成 WM_CLOSE，由 OnCloseMsg 接住。
    bHandled = FALSE;
    return 0;
}

bool CBalloonMainPanel::CollectDragZoneRects(RECT* dragRects, int& dragCount,
                                             RECT* excludeRects, int& excludeCount)
{
    dragCount = 0;
    excludeCount = 0;

    if (dragRects == NULL || excludeRects == NULL)
    {
        return false;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        // 客户区控件树还没建（InitUi 之前），此刻窗口上哪里都不该能拖。
        return false;
    }

    // ---- 顶部渐变 Banner：整块可拖，再把里面 5 块可点击区域挖掉 ----
    // 这 5 块命中矩形都是 Banner 自己在 OnPaint 里按实际绘制位置缓存的，随昵称 /
    // 签名的长度变化，故每次判定都重新取，不做缓存。
    balloonmain::MainPanelHeader* header = dynamic_cast<balloonmain::MainPanelHeader*>(
        content->FindCtrlById(balloonmain::kIdHeader));
    if (header != NULL && header->IsVisible())
    {
        dragRects[dragCount++] = header->GetRect();
        excludeRects[excludeCount++] = header->GetAvatarHitRect();   // 头像：点了开"修改头像"
        excludeRects[excludeCount++] = header->GetNameHitRect();     // 昵称：点了开个人信息窗
        excludeRects[excludeCount++] = header->GetSignHitRect();     // 个性签名：点了就地编辑
        excludeRects[excludeCount++] = header->GetStatusHitRect();   // 状态胶囊：点了弹状态菜单
        excludeRects[excludeCount++] = header->GetBellHitRect();     // 消息盒子：点了打开公告收件箱
    }

    // ---- 底部栏：整条可拖，再把两个按钮挖掉 ----
    balloonwjui::DuiControl* bottomBar =
        content->FindCtrlById(balloonmain::kIdBottomBar);
    if (bottomBar != NULL && bottomBar->IsVisible())
    {
        dragRects[dragCount++] = bottomBar->GetRect();

        balloonwjui::DuiControl* menuBtn =
            content->FindCtrlById(balloonmain::kIdBottomMenu);
        if (menuBtn != NULL && menuBtn->IsVisible())
        {
            excludeRects[excludeCount++] = menuBtn->GetRect();
        }

        balloonwjui::DuiControl* actionBtn =
            content->FindCtrlById(balloonmain::kIdBottomAction);
        if (actionBtn != NULL && actionBtn->IsVisible())
        {
            excludeRects[excludeCount++] = actionBtn->GetRect();
        }
    }

    return dragCount > 0;
}

bool CBalloonMainPanel::IsPointInPanelDragZone(POINT ptScreen)
{
    if (!IsWindow())
    {
        return false;
    }

    RECT dragRects[balloonmain::kMaxDragRects];
    RECT excludeRects[balloonmain::kMaxDragExcludeRects];
    int  dragCount    = 0;
    int  excludeCount = 0;
    if (!CollectDragZoneRects(dragRects, dragCount, excludeRects, excludeCount))
    {
        return false;
    }

    // 控件矩形是窗口客户区坐标，非客户区消息给的是屏幕坐标，换算到同一坐标系再判。
    // 用 ScreenToClient 而不是自己减窗口矩形左上角：DuiFrameWindow 让客户区铺满整个
    // 窗口矩形，但窗口最大化时客户区会按 ComputeMaximizedClientInsets 内缩，交给系统
    // 换算才不会在那种情形下整体偏移。
    POINT ptClient = ptScreen;
    ::ScreenToClient(m_hWnd, &ptClient);

    return balloonmain::IsPointInDragZone(ptClient, dragRects, dragCount,
                                          excludeRects, excludeCount);
}

LRESULT CBalloonMainPanel::OnPanelSetFocusMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM /*lParam*/, BOOL& bHandled)
{
    //面板内已经有控件持有 DUI 焦点：交回基类，由它恢复给原来那个控件。
    //典型场景是用户本来就在搜索框里打字，切到别的程序再切回来。
    if (GetDuiFocus() != NULL)
    {
        bHandled = FALSE;
        return 0;
    }

    //没有任何控件持有焦点：就地消费掉，不让基类走 FocusNext(false) 那条回退分支，
    //否则焦点会落到控件树里第一个 tab stop（顶部搜索框）。详见头文件里的说明。
    bHandled = TRUE;
    return 0;
}

LRESULT CBalloonMainPanel::OnPanelNcHitTestMsg(UINT uMsg, WPARAM wParam,
                                               LPARAM lParam, BOOL& bHandled)
{
    // 先取基类判定 —— 缩放边框、标题栏、min/close 三按钮的口径原样保留。
    LRESULT ht = balloonwjui::DuiFrameWindow::OnNcHitTestMsg(uMsg, wParam, lParam, bHandled);
    if (ht != (LRESULT)HTCLIENT)
    {
        // 非客户区（边框 / 标题栏）优先：底部栏最下面那几个像素仍归"拉伸窗口高度"的
        // 抓握区，不能被拖动抢走，否则底边就再也拉不动了。
        return ht;
    }

    POINT ptScreen;
    ptScreen.x = GET_X_LPARAM(lParam);
    ptScreen.y = GET_Y_LPARAM(lParam);
    if (!IsPointInPanelDragZone(ptScreen))
    {
        return ht;
    }

    // 落在 Banner / 底部栏的空白处：报"标题栏"，系统据此提供原生拖窗（含吸附、贴边）。
    bHandled = TRUE;
    return (LRESULT)HTCAPTION;
}

LRESULT CBalloonMainPanel::OnPanelNcLButtonDblClkMsg(UINT /*uMsg*/, WPARAM wParam,
                                                     LPARAM lParam, BOOL& bHandled)
{
    // 只管"被判成标题栏"的双击；落在缩放边框等其它非客户区的双击保持系统默认。
    if (wParam != (WPARAM)HTCAPTION)
    {
        bHandled = FALSE;
        return 0;
    }

    POINT ptScreen;
    ptScreen.x = GET_X_LPARAM(lParam);
    ptScreen.y = GET_Y_LPARAM(lParam);
    if (!IsPointInPanelDragZone(ptScreen))
    {
        // 真·标题栏上的双击：维持原有行为，原样交回基类 / DefWindowProc。
        bHandled = FALSE;
        return 0;
    }

    // Banner / 底部栏空白处的双击：吞掉。这里之所以会收到双击，只是因为上面把空白
    // 判成了 HTCAPTION；主面板是固定窄高面板、连最大化按钮都没有，被双击拉成全屏
    // 观感不对。
    bHandled = TRUE;
    return 0;
}

LRESULT CBalloonMainPanel::OnPanelMoveMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM /*lParam*/, BOOL& bHandled)
{
    // 消息继续走原有链路（本处理只是搭个便车）。
    bHandled = FALSE;

    // 主面板刚移动完（WM_MOVE 在窗口位置生效之后到达，此刻 ClientToScreen 已能换算出
    // 新坐标）：把搜索结果下拉挪到新的搜索框下方。拖动过程中本消息会连续到达，故这里
    // 只摆位置、不重算结果。
    SyncSearchDropdownPos();
    return 0;
}

LRESULT CBalloonMainPanel::OnPanelSizeMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                          LPARAM /*lParam*/, BOOL& bHandled)
{
    // 同上：置 FALSE 让 DuiFrameWindow（维护最大化态）与 DuiHost（重排客户区）照常
    // 处理本消息。
    bHandled = FALSE;

    // 尺寸变化也可能是跨显示器 / 改缩放引起的（WM_DPICHANGED 会带来新的窗口
    // 矩形，最终落成一次 WM_SIZE），故顺便按当前 DPI 重算滚动条的让位宽度。
    RefreshScrollBarEdgeInset();

    // 主面板改变尺寸：下拉宽度跟随主面板客户区宽，落点也随之变化。
    // 注意本处理跑在 DuiHost 重排客户区<u>之前</u>，此刻搜索框的控件内矩形还是上一次
    // 布局的结果；但搜索框是顶部定高、左对齐的一行（见 BuildMainPanelBody：Header 之下
    // 紧跟 fixedHeight=36 的 edit），横向拉伸不改变它的 left / bottom，纵向拉伸也不会
    // 移动顶部的行，故据旧矩形算出的锚点与新布局一致；宽度则直接取本次刷新后的客户区宽。
    SyncSearchDropdownPos();
    return 0;
}

balloonmain::MainPanelScrollView* CBalloonMainPanel::FindScrollPane(UINT paneId)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return NULL;
    }
    return dynamic_cast<balloonmain::MainPanelScrollView*>(content->FindCtrlById(paneId));
}

void CBalloonMainPanel::RefreshScrollBarEdgeInset()
{
    // 与 DuiFrameWindow::OnNcHitTestMsg 同一口径：边框宽度是 96-dpi 逻辑像素，
    // 按当前显示器 DPI 折算成物理像素后才能和布局矩形对得上。
    const int inset = balloonmain::ComputeResizeBorderInsetPx(GetBorderPx(),
                                                              GetDpi(),
                                                              IsResizable());

    const UINT paneIds[3] = { balloonmain::kIdPaneMessages,
                              balloonmain::kIdPaneContacts,
                              balloonmain::kIdPaneGroups };
    for (int i = 0; i < 3; ++i)
    {
        balloonmain::MainPanelScrollView* pane = FindScrollPane(paneIds[i]);
        if (pane != NULL)
        {
            pane->SetEdgeInsetPx(inset);
        }
    }
}

void CBalloonMainPanel::AttachAppHost(balloonmain::ISessionAppHost* pAppHost)
{
    m_pAppHost = pAppHost;

    if (!IsWindow() || pAppHost == NULL)
    {
        LOG_ERROR("MainPanel::AttachAppHost: window not created or host is null, "
            "collaborators stay disabled.");
        return;
    }

    // 会话控制器排在最前：另外两个控制器把它当作 ISessionStateHost，绑定时就要拿到它。
    m_sessionController.Attach(m_hWnd, this, m_lpFMGClient, pAppHost);

    // 自动离开控制器要在本窗口上起定时器、订阅锁屏事件，故须等窗口建好之后再启动。
    m_autoAwayController.Attach(m_hWnd, m_lpFMGClient, &m_sessionController);

    // 托盘图标挂在本窗口句柄上（系统按窗口投递托盘回调消息），同样要等窗口建好。
    // 此刻尚未登录，图标按「离线」取灰色的那只，登录成功后再刷新。
    m_trayController.Attach(m_hWnd, this, m_lpFMGClient, &m_sessionController);
    m_trayController.AddIcon(_T("Flamingo未登录"));
}

void CBalloonMainPanel::NotifyMyPresenceChanged(int presence, int source)
{
    m_autoAwayController.OnPresenceChanged(presence, source);

    //已打开的每个群聊窗的成员侧栏里都有本人，本人那一行头像角上的状态徽标跟着变。
    //本人的取值与 presence_type 一致（kMyPresenceOnline 等，见 UserSessionData.h）
    const UINT selfId = SelfUserId();
    if (selfId == 0)
    {
        return;
    }
    for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.begin();
         it != m_groupChatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
        {
            it->second->SetMemberPresence((int64_t)selfId, presence);
        }
    }
}

LRESULT CBalloonMainPanel::OnPanelDestroyMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM /*lParam*/, BOOL& bHandled)
{
    // 自动离开控制器在本窗口上起了定时器、并按本窗口句柄订阅了锁屏 / 解锁事件。
    // 系统按 HWND 记录订阅方，窗口销毁后再退订就找不到对应项了，所以必须在这里做。
    m_autoAwayController.Shutdown();

    // 托盘同理：定时器与托盘图标都挂在本窗口上，窗口没了再摘就摘不掉了，
    // 图标会以「幽灵图标」的形式留在托盘里直到用户把鼠标移上去。
    m_trayController.Shutdown();

    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonMainPanel::OnRequestAppExitMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                               LPARAM /*lParam*/)
{
    if (m_pAppHost != NULL)
    {
        m_pAppHost->RequestAppExit();
    }
    return (LRESULT)1;
}

void CBalloonMainPanel::OnPanelHotKey(int nHotKeyID, UINT /*uModifiers*/, UINT /*uVirtKey*/)
{
    if (nHotKeyID != balloonmain::kShowPanelHotKeyId)
    {
        SetMsgHandled(FALSE);
        return;
    }

    // 本面板可见就把它隐藏；否则走与「单击托盘图标」完全相同的那条路径，
    // 这样两个入口的行为（最小化时用 SW_RESTORE 还原、随后前置并取焦点）必然一致。
    if (::IsWindowVisible(m_hWnd))
    {
        ::ShowWindow(m_hWnd, SW_HIDE);
        return;
    }

    m_trayController.OnTrayIconNotify(WM_TRAYICON_NOTIFY, NULL, WM_LBUTTONUP);
}

LRESULT CBalloonMainPanel::OnCollaboratorTimer(UINT /*uMsg*/, WPARAM wParam,
                                               LPARAM /*lParam*/, BOOL& bHandled)
{
    if (m_autoAwayController.PumpTimer((UINT_PTR)wParam))
    {
        bHandled = TRUE;
        return 0;
    }

    // 托盘那几个定时器（新消息闪烁、登录动画，以及 CTrayIcon 自己检测鼠标停留的那个）。
    if (m_trayController.PumpTimer((UINT_PTR)wParam))
    {
        bHandled = TRUE;
        return 0;
    }

    // 自动重连定时器。
    if (m_sessionController.PumpTimer((UINT_PTR)wParam))
    {
        bHandled = TRUE;
        return 0;
    }

    // 转发应答的超时检查定时器（见 BalloonMainPanelForward.cpp）。
    if ((UINT_PTR)wParam == balloonmain::kForwardTimeoutTimerId)
    {
        OnForwardTimeoutTimer();
        bHandled = TRUE;
        return 0;
    }

    // 不是协作类的定时器，放行给后面的处理器。
    bHandled = FALSE;
    return 0;
}

LRESULT CBalloonMainPanel::OnScrollBarAnimTimer(UINT /*uMsg*/, WPARAM wParam,
                                                LPARAM /*lParam*/, BOOL& bHandled)
{
    if ((UINT_PTR)wParam != balloonmain::kScrollbarAnimTimerId)
    {
        // 别人的定时器，放行给基类 / DefWindowProc。
        bHandled = FALSE;
        return 0;
    }

    bHandled = TRUE;

    // 三个面板各推进一帧。只要还有一个需要下一帧，定时器就继续跑。
    const UINT paneIds[3] = { balloonmain::kIdPaneMessages,
                              balloonmain::kIdPaneContacts,
                              balloonmain::kIdPaneGroups };
    bool needMore = false;
    for (int i = 0; i < 3; ++i)
    {
        balloonmain::MainPanelScrollView* pane = FindScrollPane(paneIds[i]);
        if (pane == NULL)
        {
            continue;
        }
        if (pane->TickScrollBarFade((int)balloonmain::kScrollbarAnimIntervalMs))
        {
            needMore = true;
        }
    }

    if (!needMore)
    {
        // 三个滚动条都已稳定（淡出到底 / 无内容可滚），停掉 pulse 省电；
        // 下次滚动或鼠标移到滚动条上时，面板会自己把它重新起起来。
        KillTimer(balloonmain::kScrollbarAnimTimerId);
    }
    return 0;
}

void CBalloonMainPanel::HideToTray()
{
    if (!IsWindow())
    {
        return;
    }

    // 搜索结果下拉是独立顶层浮窗，不是本窗口的子窗口，主面板隐藏不会连带隐藏它。
    // 若不先收起，它会孤零零留在桌面上。
    m_searchDropdown.HideDropdown();

    ShowWindow(SW_HIDE);
    LOG_INFO(_T("主面板最小化：已隐藏到托盘（工具窗口无任务栏按钮，不做真正的最小化）。"));

    // 该账号首次隐藏到托盘：请主面板 弹一次气泡告诉用户去托盘找它，
    // 并把"已提示过"立刻落盘（每账号只提示一次，之后静默隐藏）。
    if (m_lpFMGClient == NULL || m_lpFMGClient->m_UserConfig.IsTrayMinimizeTipShown())
    {
        return;
    }

    m_lpFMGClient->m_UserConfig.SetTrayMinimizeTipShown(TRUE);
    m_lpFMGClient->SaveUserConfig();

    //投给本窗口自己：托盘控制器挂在本面板的消息派发链上。
    ::PostMessage(m_hWnd, FMG_MSG_TRAY_MINIMIZE_TIP, 0, 0);
    LOG_INFO("MainPanel: first time minimized to tray, tray balloon tip requested.");
}

LRESULT CBalloonMainPanel::OnDuiNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                       LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* pNotify =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);

    // 说明：搜索结果下拉是<u>独立的顶层浮窗</u>（owner 为空、通知自路由），其结果行 /
    // 筛选标签的点击在下拉自己的 OnChildNotify 内处理，<u>不会</u>冒泡到这里，故主
    // 面板无需为它分发。命中结果时下拉通过回调（OnSearchPick / OnSearchPickGroup）
    // 反过来调本面板开聊 / 开群。

    // Header 签名区点击 —— 个性签名就地编辑。<u>必须放在最前</u>：签名通知码
    // DUIN_HEADER_SIGN_CLICK 与 DuiTreeView::DUITVN_EXPAND_TOGGLED 数值相同（都是
    // DUIN_CUSTOM+1 —— 自定义码各控件独立命名空间，数值会重合），而下面处理
    // EXPAND_TOGGLED 的分支<u>不判 ctrlId 就 return</u>，会把本通知吞掉。放最前 +
    // 带 ctrlId==kIdHeader 守卫：既不会被吞，也不会误伤树的展开通知（树的 ctrlId 不等）。
    if (pNotify != NULL
        && pNotify->code == balloonmain::MainPanelHeader::DUIN_HEADER_SIGN_CLICK
        && pNotify->ctrlId == balloonmain::kIdHeader)
    {
        BeginSignatureEdit();
        return 0;
    }

    // Header 状态胶囊点击 —— 弹出在线状态选择菜单。与签名点击同理放在前面并带
    // ctrlId==kIdHeader 守卫：DUIN_HEADER_STATUS_CLICK(DUIN_CUSTOM+2) 可能与其它控件的
    // 自定义码数值重合，靠 ctrlId 区分、靠靠前避免被后面不判 ctrlId 的分支吞掉。
    if (pNotify != NULL
        && pNotify->code == balloonmain::MainPanelHeader::DUIN_HEADER_STATUS_CLICK
        && pNotify->ctrlId == balloonmain::kIdHeader)
    {
        ShowStatusMenu();
        return 0;
    }

    // Header 头像格点击 —— 打开修改头像对话框。与签名 / 状态点击同理放在前面并带
    // ctrlId==kIdHeader 守卫：DUIN_HEADER_AVATAR_CLICK(DUIN_CUSTOM+3) 可能与其它控件
    // 自定义码数值重合，靠 ctrlId 区分、靠靠前避免被后面不判 ctrlId 的分支吞掉。
    // 点昵称区仍走下面的 DUIN_CLICK 分支打开个人信息窗（头像→改头像、昵称→看资料）。
    if (pNotify != NULL
        && pNotify->code == balloonmain::MainPanelHeader::DUIN_HEADER_AVATAR_CLICK
        && pNotify->ctrlId == balloonmain::kIdHeader)
    {
        OpenModifyAvatarDlg();
        return 0;
    }

    // Header 底部「消息盒子」行点击 —— 打开平台公告收件箱。同样带 ctrlId==kIdHeader
    // 守卫并放在前面，理由与上面几条一致（DUIN_HEADER_BELL_CLICK 取 DUIN_CUSTOM+4，
    // 数值可能与其它控件的自定义码重合）。
    if (pNotify != NULL
        && pNotify->code == balloonmain::MainPanelHeader::DUIN_HEADER_BELL_CLICK
        && pNotify->ctrlId == balloonmain::kIdHeader)
    {
        OpenAnnounceBox();
        return 0;
    }

    // 节点展开 / 折叠完成 —— treeview 内部可见行数变了,scrollview 的
    // m_contentH 必须跟着更新,否则花屏 / 滚不到底。按通知来源 ctrlId
    // 分发到对应 pane。
    //
    // 注意这里<u>只在 ctrlId 确实是那三棵树之一时才 return</u>：自定义通知码是按控件
    // 各自编号的（每个控件都从 balloonwjui::DUIN_CUSTOM 起算），所以 DUITVN_EXPAND_TOGGLED
    // 与 MainPanelHeader::DUIN_HEADER_SIGN_CLICK、DuiEdit::DUIEN_LEFT_ICON_CLICK 等
    // 数值完全相同。原先此处无条件 return，等于把本面板内其它控件发出的同值通知统统
    // 吞掉，症状是"点了没反应"且不报错、不崩溃——登录窗的状态胶囊就栽在同一个坑上
    // （2026-08-12 定位并修复，见 CBalloonLoginDlg::OnDuiNotify 的注释）。
    if (pNotify != NULL &&
        pNotify->code == balloonwjui::DuiTreeView::DUITVN_EXPAND_TOGGLED)
    {
        if (pNotify->ctrlId == balloonmain::kIdContactsTree)
        {
            SyncPaneContentHeight(balloonmain::kIdPaneContacts,
                                  balloonmain::kIdContactsTree,
                                  kContactsPaneTitleHeight);
            return 0;
        }
        else if (pNotify->ctrlId == balloonmain::kIdGroupsTree)
        {
            SyncPaneContentHeight(balloonmain::kIdPaneGroups,
                                  balloonmain::kIdGroupsTree,
                                  kGroupsPaneTitleHeight);
            return 0;
        }
        else if (pNotify->ctrlId == balloonmain::kIdMessagesTree)
        {
            SyncPaneContentHeight(balloonmain::kIdPaneMessages,
                                  balloonmain::kIdMessagesTree, 0);
            return 0;
        }
        // 其它控件发来的同值通知：交给后面的分支继续判，不能在这里截断。
    }

    // 搜索框文字变化 —— 实时驱动搜索结果下拉：去空白后为空则隐藏下拉（"空内容
    // 不显示"，同时避免点 Tab 时焦点回落搜索框误弹），非空则定位到搜索框下方展示
    // 并刷新结果。DuiEdit 每键变化发 DUIN_VALUECHANGED。
    if (pNotify != NULL
        && pNotify->ctrlId == balloonmain::kIdSearchBox
        && pNotify->code == balloonwjui::DUIN_VALUECHANGED)
    {
        UpdateSearchDropdown();
        return 0;
    }

    // 搜索框按 Esc —— 收起结果下拉（搜索框文字保留，用户可继续改词）。无窗口输入框
    // 把 Esc 转成了这条通知，不再需要往子窗口挂键盘拦截。判定条件里带上控件编号：
    // balloonui 的自定义通知码按控件各自编号，不同控件的同序号自定义码数值相同，
    // 只比通知码会把别的控件的通知一并截住。
    if (pNotify != NULL
        && pNotify->ctrlId == balloonmain::kIdSearchBox
        && pNotify->code == (UINT)balloonwjui::DuiEdit::DUIN_EDIT_ESCAPE)
    {
        m_searchDropdown.HideDropdown();
        return 0;
    }

    // 搜索框失焦 —— 收起下拉，但要排除"失焦是因为点了下拉本身"的情形：下拉是
    // 独立顶层浮窗，点它的结果行 / 筛选标签会让搜索框瞬间失焦；失焦在鼠标按下时
    // 发生，此刻若直接隐藏，会在点击尚未完成（鼠标抬起）前把下拉收起，导致结果
    // 行 / 标签点不动。故失焦时先看光标是否正落在下拉上：是则保留（让点击走完），
    // 否则才收起，达成"点别处收起"。
    if (pNotify != NULL
        && pNotify->ctrlId == balloonmain::kIdSearchBox
        && pNotify->code == balloonwjui::DUIN_KILLFOCUS)
    {
        POINT pt;
        ::GetCursorPos(&pt);
        HWND hUnder = ::WindowFromPoint(pt);
        HWND hDropdown = m_searchDropdown.GetHwnd();
        bool cursorOnDropdown = (hDropdown != NULL && hUnder == hDropdown);
        if (!cursorOnDropdown)
        {
            m_searchDropdown.HideDropdown();
        }
        return 0;
    }

    // 联系人树节点"双击" —— 取该节点对应 userid 打开单聊（单击只选中，不开聊）。
    // 收藏分区的<u>群行</u>也在这棵树里，它不在 m_contactNodeUserId 而在
    // m_favoriteGroupNodeId，故两张表都要查。本分支一旦匹配 ctrlId 就无条件 return，
    // 后面再写第二个同 ctrlId 的分支是走不到的（曾因此漏掉群行的双击与右键）。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_DBLCLK
        && pNotify->ctrlId == balloonmain::kIdContactsTree)
    {
        int nodeId = (int)pNotify->extra;
        std::map<int, UINT>::iterator it = m_contactNodeUserId.find(nodeId);
        std::map<int, int64_t>::iterator git = m_favoriteGroupNodeId.find(nodeId);
        LOG_INFO("联系人树双击: node=%d, 命中联系人=%d, 命中收藏群=%d "
                 "(联系人表 %u 项, 收藏群表 %u 项)。",
                 nodeId, (it != m_contactNodeUserId.end()) ? 1 : 0,
                 (git != m_favoriteGroupNodeId.end()) ? 1 : 0,
                 (uint32_t)m_contactNodeUserId.size(),
                 (uint32_t)m_favoriteGroupNodeId.size());

        if (it != m_contactNodeUserId.end())
        {
            // 双击自己那一行：聊天不允许，就把双击映射到唯一允许的动作——看自己的
            // 资料。什么都不做会让人以为界面卡住了，反倒像个 bug。
            if (IsSelfUserId(it->second))
            {
                LOG_INFO("双击通讯录里的自己，打开个人信息窗。");
                OpenProfileDlg();
                return 0;
            }
            OpenSingleChat(it->second);
            return 0;
        }

        if (git != m_favoriteGroupNodeId.end())
        {
            OpenGroupChat(git->second);
        }
        return 0;
    }

    // 联系人树节点"右键" —— 弹联系人右键菜单；收藏分区的群行则弹群聊右键菜单
    // （与"群"Tab 上那份是同一个菜单）。两张节点表都要查，理由同上面的双击分支。
    if (pNotify != NULL
        && pNotify->code == (UINT)balloonwjui::DuiTreeView::DUITVN_RCLICK
        && pNotify->ctrlId == balloonmain::kIdContactsTree)
    {
        int nodeId = (int)pNotify->extra;
        std::map<int, UINT>::iterator it = m_contactNodeUserId.find(nodeId);
        if (it != m_contactNodeUserId.end())
        {
            ShowContactContextMenu(it->second);
            return 0;
        }

        std::map<int, int64_t>::iterator git = m_favoriteGroupNodeId.find(nodeId);
        if (git != m_favoriteGroupNodeId.end())
        {
            ShowGroupContextMenu(git->second);
        }
        return 0;
    }

    // 会话列表（Messages 树）节点"单击" —— 按 (ctype,cid) 分流：单聊开单聊窗、
    // 群聊开群聊窗，并把该会话未读本地清零（会话列表沿用 IM 习惯：单击即打开）。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_CLICK
        && pNotify->ctrlId == balloonmain::kIdMessagesTree)
    {
        int nodeId = (int)pNotify->extra;
        std::map<int, std::pair<int, int64_t> >::iterator it = m_msgNodeConv.find(nodeId);
        if (it != m_msgNodeConv.end())
        {
            int     ctype = it->second.first;
            int64_t cid   = it->second.second;
            if (ctype == 2)
            {
                OpenGroupChat(cid);
            }
            else
            {
                OpenSingleChat((UINT)cid);
            }
            MarkConversationRead(ctype, cid);
        }
        return 0;
    }

    // 会话列表（Messages 树）节点"右键" —— 弹出会话右键菜单。右键只让基类改选中态
    // （DuiTreeView::OnRButtonDown 里 SetCurSel 发的是 DUIN_VALUECHANGED），不会走到
    // 上面那条 DUIN_CLICK 分支，所以右键不会顺手把会话打开。
    if (pNotify != NULL
        && pNotify->code == (UINT)balloonwjui::DuiTreeView::DUITVN_RCLICK
        && pNotify->ctrlId == balloonmain::kIdMessagesTree)
    {
        int nodeId = (int)pNotify->extra;
        std::map<int, std::pair<int, int64_t> >::iterator it = m_msgNodeConv.find(nodeId);
        if (it != m_msgNodeConv.end())
        {
            ShowConversationContextMenu(it->second.first, it->second.second);
        }
        return 0;
    }

    // "群"Tab 群树节点"双击" —— 取该节点 groupid 打开群聊窗（单击只选中，不开窗，
    // 与联系人树口径一致）。
    if (pNotify != NULL
        && pNotify->code == balloonwjui::DUIN_DBLCLK
        && pNotify->ctrlId == balloonmain::kIdGroupsTree)
    {
        int nodeId = (int)pNotify->extra;
        std::map<int, int64_t>::iterator it = m_groupNodeId.find(nodeId);
        if (it != m_groupNodeId.end())
        {
            OpenGroupChat(it->second);
        }
        return 0;
    }

    // "群"Tab 群树节点"右键" —— 弹出群聊右键菜单。
    if (pNotify != NULL
        && pNotify->code == (UINT)balloonwjui::DuiTreeView::DUITVN_RCLICK
        && pNotify->ctrlId == balloonmain::kIdGroupsTree)
    {
        int nodeId = (int)pNotify->extra;
        std::map<int, int64_t>::iterator it = m_groupNodeId.find(nodeId);
        if (it != m_groupNodeId.end())
        {
            ShowGroupContextMenu(it->second);
        }
        return 0;
    }

    if (pNotify != NULL && pNotify->code == balloonwjui::DUIN_CLICK)
    {
        // 顶部 Banner Header 点击 —— 打开个人信息对话框(MainPanelHeader
        // 整片区域可点击,见 MainPanelHeader::OnLButtonDown)。
        if (pNotify->ctrlId == balloonmain::kIdHeader)
        {
            OpenProfileDlg();
            return 0;
        }

        // 三个 Tab 的点击 —— 点击即切换 Tab。
        if (pNotify->ctrlId == balloonmain::kIdTabMessages ||
            pNotify->ctrlId == balloonmain::kIdTabContacts ||
            pNotify->ctrlId == balloonmain::kIdTabGroups)
        {
            SwitchTab(pNotify->ctrlId);
            return 0;
        }

        // 底部主操作按钮 —— 群页是"建群"（打开新建群对话框），其余页是"查找"
        // （把键盘焦点移到搜索框，用户随即输入即弹出结果下拉）。
        if (pNotify->ctrlId == balloonmain::kIdBottomAction)
        {
            if (m_currentTabId == balloonmain::kIdTabGroups)
            {
                OpenCreateGroupDlg();
            }
            else
            {
                FocusSearchBox();
            }
            return 0;
        }

        // 左下角汉堡按钮 —— 弹出主面板系统菜单。
        if (pNotify->ctrlId == balloonmain::kIdBottomMenu)
        {
            ShowSystemMenu();
            return 0;
        }

        // 联系人树 / 群树节点"单击" —— 只做选中，不开窗（开窗改为双击，见上面的
        // DUIN_DBLCLK 分支）。这里消费掉，避免落到下面的分组/列表项分支。
        if (pNotify->ctrlId == balloonmain::kIdContactsTree
            || pNotify->ctrlId == balloonmain::kIdGroupsTree)
        {
            return 0;
        }

        // 分组标题的点击 —— 展开 / 收起该分组（分组标题无 ctrlId，
        // 经 pCtrl 动态判型识别）。
        balloonmain::MainPanelSectionHeader* sec =
            dynamic_cast<balloonmain::MainPanelSectionHeader*>(
                static_cast<balloonwjui::DuiControl*>(pNotify->pCtrl));
        if (sec != NULL)
        {
            ToggleSection(sec);
            return 0;
        }

        // 列表项的点击 —— 联系人 / 会话行，弹出单聊窗口（列表项无 ctrlId，
        // 经 pCtrl 动态判型识别）。
        balloonmain::MainPanelListItem* item =
            dynamic_cast<balloonmain::MainPanelListItem*>(
                static_cast<balloonwjui::DuiControl*>(pNotify->pCtrl));
        if (item != NULL)
        {
            // 按列表项所属面板路由：群组面板 → 群聊窗，其余（联系人 /
            // 会话）→ 单聊窗。沿父链向上找面板 id 判定。
            bool inGroupsPane = false;
            for (balloonwjui::DuiControl* p = item->GetParent();
                 p != NULL; p = p->GetParent())
            {
                if (p->GetCtrlId() == balloonmain::kIdPaneGroups)
                {
                    inGroupsPane = true;
                    break;
                }
            }
            if (inGroupsPane)
            {
                // 群页现已改用 DuiTreeView(kIdGroupsTree) + 真实群路由（见上面的群树
                // 点击分支）；这条 mock listitem 路径无群 id，已不再用于开群聊。
                LOG_INFO(_T("点击 mock 群列表卡片，无真实 groupid，跳过开聊。"));
            }
            else
            {
                // mock 卡片（星标/服务号 listitem）无真实 userid，本轮不开聊；
                // 真实联系人走上面的联系人树点击分支。
                LOG_INFO(_T("点击 mock 列表卡片，无真实 userid，跳过开聊。"));
            }
            return 0;
        }
    }

    // 其余通知交回 DuiFrameWindow 链处理。
    bHandled = FALSE;
    return 0;
}

void CBalloonMainPanel::OpenProfileDlg()
{
    // 部门 / 职位不在 m_UserInfo 里，只有主面板持有的通讯录缓存算得出来，
    // 故每次打开前灌一次 —— 通讯录可能在两次打开之间刷新过。
    CString position;
    CString deptPath;
    ComputeContactDeptInfo(SelfUserId(), position, deptPath);
    m_profileDlg.SetOrgInfo(deptPath, position);

    // m_profileDlg.OpenForCurrentUser 内部判 IsWindow:已打开则前置,
    // 未打开则 Create。
    m_profileDlg.OpenForCurrentUser(m_hWnd, m_lpFMGClient);
}

bool CBalloonMainPanel::IsSelfUserId(UINT userId) const
{
    if (m_lpFMGClient == NULL || userId == 0)
    {
        return false;
    }
    return userId == m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID;
}

UINT CBalloonMainPanel::SelfUserId() const
{
    return (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
}

void CBalloonMainPanel::OpenModifyAvatarDlg()
{
    // 改头像要把新头像上传并同步到服务器，离线时做不了，在入口就拦住并给提示。
    if (!CheckOnlineFor(balloonmain::kActionAccountOp))
    {
        return;
    }

    if (m_lpFMGClient == NULL)
    {
        return;
    }
    // 当前用户头像存储：m_nFace = v2 facetype；m_strCustomFaceName 是 CStringA、
    // 内含 customface 的 UTF-8 原始字节（登录映射处按字节拷入，未经 ANSI 转换）。
    const CBuddyInfo& me = m_lpFMGClient->m_UserMgr.m_UserInfo;
    std::string customface(me.m_strCustomFaceName.GetString(),
                           (size_t)me.m_strCustomFaceName.GetLength());
    m_modifyAvatarDlg.OpenForCurrentUser(m_hWnd, (int)me.m_nFace, customface,
                                         CString(me.m_strNickName.c_str()),
                                         (__int64)me.m_uUserID);
}

void CBalloonMainPanel::RefreshSelfAvatar()
{
    if (m_lpFMGClient == NULL)
    {
        return;
    }
    const CBuddyInfo& me = m_lpFMGClient->m_UserMgr.m_UserInfo;
    std::string customface(me.m_strCustomFaceName.GetString(),
                           (size_t)me.m_strCustomFaceName.GetLength());
    balloonmain::ApplyUserDataToMainPanel(
        GetClientContent(),
        me.m_uUserID,
        CString(me.m_strAccount.c_str()),
        CString(me.m_strNickName.c_str()),
        CString(me.m_strSign.c_str()),
        CIUSocket::GetInstance().GetRecordOnlineStatus(),
        (int)me.m_nFace,
        customface);
    // 真图头像（facetype==自定义图）：另给 header 设真实位图；字形头像清空走字形渲染。
    ApplySelfAvatarBitmap((int)me.m_nFace, customface, me.m_uUserID);
}

void CBalloonMainPanel::ApplySelfAvatarBitmap(int facetype, const std::string& customfaceUtf8, UINT userid)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonmain::MainPanelHeader* hdr = dynamic_cast<balloonmain::MainPanelHeader*>(
        content->FindCtrlById(balloonmain::kIdHeader));
    if (hdr == NULL)
    {
        return;
    }

    // 头像位图边长 / 圆角须与 MainPanelHeader 内 kAvatarSize(50) / kAvatarRadius(6) 一致，
    // AlphaBlend 1:1 绘制不缩放（本进程 DPI-unaware）。
    const int kSelfAvatarBmpSize   = 50;
    const int kSelfAvatarBmpRadius = 6;

    if (facetype == balloonmain::avatar_kind_image && !customfaceUtf8.empty())
    {
        //customface 即 fileid；从本地头像缓存载图（本端刚上传或上次登录已缓存）。
        std::wstring path = net::ImageTransfer::AvatarCacheFilePathForFileId(
            net::CurrentUserAccount(), customfaceUtf8);
        HBITMAP hbm = NULL;
        if (!path.empty())
        {
            hbm = balloonmain::MakeRoundedAvatarFromFile(
                path.c_str(), kSelfAvatarBmpSize, kSelfAvatarBmpRadius);
        }
        //载图失败（缓存缺失，如换机后未下载）→ 传 NULL 回退字形，第一期不做下载。
        hdr->SetAvatarBitmap(hbm);
    }
    else
    {
        //非真图头像：清掉位图，回到字形渲染。
        hdr->SetAvatarBitmap(NULL);
    }
}

void CBalloonMainPanel::BeginSignatureEdit()
{
    LOG_INFO(_T("BeginSignatureEdit 进入。"));
    if (m_lpFMGClient == NULL)
    {
        LOG_INFO(_T("BeginSignatureEdit: m_lpFMGClient 为空，返回。"));
        return;
    }

    // 取签名文字命中矩形（host 客户区坐标）。空矩形表示当前不可编辑（如未注入数据）。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        LOG_INFO(_T("BeginSignatureEdit: GetClientContent 为空，返回。"));
        return;
    }
    balloonmain::MainPanelHeader* mph =
        dynamic_cast<balloonmain::MainPanelHeader*>(content->FindCtrlById(balloonmain::kIdHeader));
    if (mph == NULL)
    {
        LOG_INFO(_T("BeginSignatureEdit: 未找到 Header 控件(kIdHeader)，返回。"));
        return;
    }
    RECT rcSign = mph->GetSignHitRect();
    LOG_INFO(_T("BeginSignatureEdit: signHit=(%d,%d,%d,%d)。"),
             rcSign.left, rcSign.top, rcSign.right, rcSign.bottom);
    if (::IsRectEmpty(&rcSign))
    {
        LOG_INFO(_T("BeginSignatureEdit: signHit 为空矩形，返回（无法定位编辑框）。"));
        return;
    }

    // host 客户区坐标 → 屏幕坐标，供弹层精确盖到签名上。
    POINT tl = { rcSign.left, rcSign.top };
    POINT br = { rcSign.right, rcSign.bottom };
    ::ClientToScreen(m_hWnd, &tl);
    ::ClientToScreen(m_hWnd, &br);
    RECT rcScreen = { tl.x, tl.y, br.x, br.y };

    CString cur = CString(m_lpFMGClient->m_UserMgr.m_UserInfo.m_strSign.c_str());

    m_signEditPopup.SetCommitCallback(&CBalloonMainPanel::OnSignCommitThunk, this);
    m_signEditPopup.OpenForSignature(rcScreen, m_hWnd, cur);
}

void CBalloonMainPanel::OnSignCommitThunk(void* userdata, const CString& newText)
{
    CBalloonMainPanel* self = (CBalloonMainPanel*)userdata;
    if (self != NULL)
    {
        self->OnSignatureCommitted(newText);
    }
}

void CBalloonMainPanel::OnSignatureCommitted(const CString& newText)
{
    if (m_lpFMGClient == NULL)
    {
        return;
    }

    CString current = CString(m_lpFMGClient->m_UserMgr.m_UserInfo.m_strSign.c_str());
    balloonmain::SignCommitDecision d =
        balloonmain::DecideSignatureCommit(newText, current, balloonmain::kSignatureMaxLen);

    if (d.action == balloonmain::kSignCommitNoChange)
    {
        return;   // 没变化：不发包，仅收起编辑（弹层已关）
    }
    if (d.action == balloonmain::kSignCommitTooLong)
    {
        // 编辑框已 SetMaxLength(127) 兜底，正常走不到这；防御性提示。
        ::MessageBox(m_hWnd, _T("个性签名不能超过 127 个字符。"),
                     _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
        return;
    }

    // 离线禁止修改（与旧版一致）。
    if (m_lpFMGClient->IsOffline())
    {
        ::MessageBox(m_hWnd, _T("您当前处于离线状态，无法修改个性签名，请上线后重试。"),
                     _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
        return;
    }

    // 乐观更新：先存快照供失败回退，再把新签名写入本地用户信息并刷新 Header。
    m_signBeforeUpdate = current;
    std::string utf8 = CStrToUtf8(d.trimmed);   // 发往服务端用 UTF-8
    m_lpFMGClient->m_UserMgr.m_UserInfo.m_strSign = d.trimmed.GetString();   // 本地缓存为 tstring(宽字符)

    const CBuddyInfo& me = m_lpFMGClient->m_UserMgr.m_UserInfo;
    balloonmain::ApplyUserDataToMainPanel(
        GetClientContent(),
        me.m_uUserID,
        CString(me.m_strAccount.c_str()),
        CString(me.m_strNickName.c_str()),
        CString(me.m_strSign.c_str()),
        CIUSocket::GetInstance().GetRecordOnlineStatus());   // 状态不变，沿用当前 presence

    // 发送 cmd_update_profile（仅签名）。应答经会话控制器 → OnUpdateProfileResult 回投。
    CIUSocket::GetInstance().SendUpdateSignature(utf8);
    LOG_INFO(_T("提交个性签名更新（乐观更新已生效，等待服务端应答）。"));
}

CBalloonChatWindow* CBalloonMainPanel::EnsureChatWindow(UINT peerUserId)
{
    if (peerUserId == 0)
    {
        return NULL;
    }

    // 查表：已有且窗口仍在 → 直接复用；对象在但窗口已被关闭 → 惰性回收重建。
    std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.find(peerUserId);
    if (it != m_chatWindows.end() && it->second != NULL)
    {
        if (it->second->IsWindow())
        {
            return it->second;
        }
        // 窗口已关闭，C++ 对象残留：销毁回收后重建。
        delete it->second;
        m_chatWindows.erase(it);
    }

    // 新建一个绑定该对端的单聊窗口。传 NULL 作 hMainDlg：单聊窗关闭只销毁
    // 自身，不退出程序。
    CBalloonChatWindow* pWin = new CBalloonChatWindow(m_lpFMGClient);
    pWin->SetPeerUserId(peerUserId);
    pWin->SetHostPanelHwnd(m_hWnd);   // 头部信息块点击经主面板打开对方资料窗

    // 从通讯录缓存取真实昵称 / 在线状态 / 个性签名传给窗口（v2 下 CUserMgr 好友列表为空、
    // 查不到）。在线状态 / 签名注入头部信息块，使其不再用 mock；后续好友改状态由
    // OnPresenceChanged 找到本窗口实时刷新头部。
    const net::ContactItem* pPeer = FindCachedContact(peerUserId);
    if (pPeer != NULL)
    {
        // 备注优先，与通讯录树 / 会话列表同一口径。
        pWin->SetPeerName(ContactDisplayName(peerUserId));
        pWin->SetPeerPresence(pPeer->m_presence);
        pWin->SetPeerSignature(Utf8ToCStr(pPeer->m_signature));
        pWin->SetPeerAvatar(pPeer->m_facetype, pPeer->m_customface);
    }

    pWin->CreateAndShow(NULL);
    if (!pWin->IsWindow())
    {
        // 创建失败：不入表，直接释放。
        delete pWin;
        return NULL;
    }

    // 把当前连接态推给新窗口。ApplyConnectionState 平时只在连接态<u>变化</u>时逐窗
    // 转发，而这个窗口是刚建的、错过了之前那次广播；不补这一下，离线状态下新开的
    // 聊天窗不会显示顶部提示条，输入框和发送按钮也不会置灰。
    pWin->ApplyConnectionState(m_connUiState);

    // 窗口没开时收到的已读推送记在位点表里，这里补给新窗口。历史消息是随后异步拉回来的，
    // 窗口的消息列表记住位点后，拉回来的每一条都会在追加时按位点判定已读与否。
    std::map<UINT, int64_t>::iterator rit = m_peerReadMsgids.find(peerUserId);
    if (rit != m_peerReadMsgids.end())
    {
        pWin->ApplyPeerReadPosition(rit->second);
    }

    m_chatWindows[peerUserId] = pWin;
    return pWin;
}

void CBalloonMainPanel::OpenSingleChat(UINT peerUserId)
{
    // 不允许和自己聊天。守卫放在这个总入口上，而不是散在各个调用点：通讯录
    // 双击、右键菜单、搜索结果、会话列表都会走到这里，将来新增的开聊入口也会，
    // 逐处判迟早会漏一个。各调用点该做的是把 UI 收拾干净（自己那份右键菜单里
    // 根本没有"发消息"），走到这里只是最后一道兜底，正常操作不该触发。
    if (IsSelfUserId(peerUserId))
    {
        LOG_WARNING("拒绝打开与自己的单聊: userid=%u。", peerUserId);
        return;
    }

    CBalloonChatWindow* pWin = EnsureChatWindow(peerUserId);
    if (pWin == NULL)
    {
        return;
    }
    // 用户主动打开：必要时从最小化还原，并前置抢焦点。
    if (pWin->IsIconic())
    {
        pWin->ShowWindow(SW_RESTORE);
    }
    ::SetForegroundWindow(pWin->m_hWnd);
}

void CBalloonMainPanel::OnContactsArrived(const net::ContactsResult& result)
{
    // 缓存联系人资料池 + 部门列表（部门列表用于算联系人职位/部门路径）。搜索结果
    // 下拉在每次输入时实时绑定 m_contacts（见 UpdateSearchDropdown），无需在此预绑。
    m_contacts    = result.m_contacts;
    //资料池整体替换，索引必须紧跟着重建（FindCachedContact 靠它定位，下标随资料池变化）
    m_contactIndex.Rebuild(m_contacts);
    //缓存整份应答，供收藏变化 / 群列表到达后原样重建这棵树（见 RebuildContactsTree）。
    //重建时会把本成员自己传回来，故要防自赋值。
    if (&result != &m_lastContactsResult)
    {
        m_lastContactsResult = result;
    }
    //「我的收藏」随通讯录一起下发。整体替换 —— 服务端那份是权威，本地乐观更新
    //期间的中间态到这里一律作废。
    m_favorites.Reset(result.m_favorites);
    LOG_INFO("收藏已灌入: 应答 %u 条, 模型 %d 条, 含群=%d。",
             (uint32_t)result.m_favorites.size(), m_favorites.Count(),
             m_favorites.HasGroupFavorite() ? 1 : 0);
    //「特别关注」同理，也随通讯录一起下发、整体替换。
    m_starred.Reset(result.m_starred);
    LOG_INFO("特别关注已灌入: 应答 %u 条, 模型 %d 条。",
             (uint32_t)result.m_starred.size(), m_starred.Count());
    m_departments = result.m_departments;

    // 部门路径查询表随通讯录一起刷新，搜索结果行的副行据它显示对方所在部门。
    // 放在这里而不是下面建树那一段里：即使树控件此刻取不到（界面尚未构建完），
    // 搜索下拉照样该能显示部门，两者没有依赖关系。
    RebuildContactDeptPaths();

    // 资料池已整体替换，安排一次搜索候选缓存的预热，免得第一次按键时才整批建立。
    // 放在这里而不是函数末尾：下面取不到树控件时会提前返回，预热与树没有依赖关系。
    ScheduleSearchWarmUp();

    // 通讯录与会话列表是两条相互独立、各自异步到达的应答。若会话先到、通讯录
    // 后到，会话行此前只能显示"用户<id>"；这里通讯录到位后用真实昵称/头像重绘
    // 一次会话树（只在已有会话时做，避免把尚未到达的会话页清成空）。本调用只
    // 触及 Messages 树，与下面的联系人树构建互不影响。
    if (!m_conversations.empty())
    {
        FillConversationsTree();
    }

    // 群列表：收藏列表随通讯录一起到达，若群列表先到，群行此前画不出收藏徽章。这里只
    // 就地给已有的群行重打徽章、不重填群树 —— 改备注、切换收藏等也会经
    // RebuildContactsTree 走到这里，整树重填会让群 Tab 的滚动位置跳回顶部。
    RefreshGroupsTreeFavoriteMarks();

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    balloonwjui::DuiTreeView* tv = dynamic_cast<balloonwjui::DuiTreeView*>(
        content->FindCtrlById(balloonmain::kIdContactsTree));
    if (tv == NULL)
    {
        LOG_WARNING(_T("Contacts TreeView(kIdContactsTree) 未找到,跳过真实通讯录填充。"));
        return;
    }

    // 用真实数据替换 mock 占位：清空旧节点、旧映射、旧头像位图（mock 占位
    // 的位图一并回收，避免重复填充时累积泄漏）。
    tv->Clear();
    m_contactNodeUserId.clear();
    m_favoriteGroupNodeId.clear();
    m_favoritesRootId = -1;            // 收藏分区随整棵树一起清掉，下面 AddFavoritesSection 重新登记
    m_favoriteSectionNodes.clear();
    // 树整棵重建，节点 id 会重新编号，两份定位索引必须一起作废重填，否则定位会跳到
    // 上一棵树的某个不相干的行上。头像位图归头像缓存所有，重建时不释放。
    m_contactNodeParent.clear();
    m_contactOrgNode.clear();

    tv->SetRowHeight(40);
    // 头像走 monogram bitmap + AlphaBlend 路径（圆形无锯齿），与 FillContactsTree 同设置。
    tv->SetIconSize(kContactAvatarSizePx);
    tv->SetIconCornerRadius(kContactAvatarSizePx / 2);
    tv->SetIconUsesAlpha(true);

    //「我的收藏」分区固定排在树顶、企业部门树之前。收藏为空时本方法什么都不做。
    AddFavoritesSection(tv);

    // 通讯录资料池含当前登录用户自己，且现在会照常列出来（自己那行由
    // AddContactLeaf 统一加"（我）"后缀），故这里不再需要单独持有 selfUserId。

    // userid → 联系人资料 指针索引（指针指向 result 内元素，仅本函数调用期间有效）。
    std::map<int64_t, const net::ContactItem*> contactById;
    for (size_t i = 0; i < result.m_contacts.size(); ++i)
    {
        contactById[result.m_contacts[i].m_userid] = &result.m_contacts[i];
    }

    // 无部门数据：回退为"企业名为根 + 联系人扁平挂其下"。
    if (result.m_departments.empty())
    {
        CString entName = result.m_enterprisename.empty()
            ? CString(_T("通讯录"))
            : Utf8ToCStr(result.m_enterprisename);
        // 根节点名字后面挂人数，与有部门数据时的公司根节点保持一致的形态。没有部门
        // 数据时"公司底下的人"就是整个资料池，故直接取其大小；含当前登录用户自己，
        // 而树里现在也会把自己列出来，两者口径一致。
        CString rootLabel;
        rootLabel.Format(_T("%s · %d 人"), (LPCTSTR)entName,
                         (int)result.m_contacts.size());
        int rootId = tv->AddRoot(rootLabel);
        tv->SetExpanded(rootId, true);
        tv->SetItemSelectable(rootId, false);
        m_contactNodeParent[rootId] = balloonmain::kTreeNodeNoParent;
        for (size_t i = 0; i < result.m_contacts.size(); ++i)
        {
            AddContactLeaf(tv, rootId, result.m_contacts[i]);
        }
        SyncPaneContentHeight(balloonmain::kIdPaneContacts,
                              balloonmain::kIdContactsTree, kContactsPaneTitleHeight);
        LOG_INFO("真实通讯录已填充(无部门,扁平): 联系人 %u 人。",
                 (uint32_t)result.m_contacts.size());
        return;
    }

    // 部门树：建"父部门id → 子部门指针表"索引与部门id集合（判定根）。
    std::set<int64_t> deptIds;
    std::map<int64_t, std::vector<const net::DepartmentItem*> > childrenByParent;
    for (size_t i = 0; i < result.m_departments.size(); ++i)
    {
        const net::DepartmentItem& d = result.m_departments[i];
        deptIds.insert(d.m_deptid);
        childrenByParent[d.m_parentdeptid].push_back(&d);
    }

    // 根部门 = parentdeptid==0 或 父部门不在集合里；按 deptorder 排序后逐棵铺开。
    std::vector<const net::DepartmentItem*> roots;
    for (size_t i = 0; i < result.m_departments.size(); ++i)
    {
        const net::DepartmentItem& d = result.m_departments[i];
        if (d.m_parentdeptid == 0 || deptIds.find(d.m_parentdeptid) == deptIds.end())
        {
            roots.push_back(&d);
        }
    }
    // 公司根部门（组织架构里代表"公司本身"的那一行）要在名字后面显示整棵公司树的
    // 去重人数，形如"明远科技有限公司 · 161 人" —— 原先页顶那行"通讯录 · N 人"已
    // 去掉，人数改由这里承担。认不出公司这一层时（存量数据尚未建公司根部门，见
    // ContactsOrgStats.h 的判定说明），各顶层部门一律不挂人数。
    const int companyIndex = balloonmain::FindCompanyRootDeptIndex(
        result.m_departments, result.m_enterprisename);
    int64_t companyDeptId = 0;
    CString companySuffix;
    if (companyIndex >= 0)
    {
        companyDeptId = result.m_departments[companyIndex].m_deptid;
        const int companyMemberCount = balloonmain::CountDeptMembersRecursive(
            result.m_departments, companyDeptId);
        companySuffix.Format(_T(" · %d 人"), companyMemberCount);
    }

    std::sort(roots.begin(), roots.end(), DeptOrderLess);
    for (size_t i = 0; i < roots.size(); ++i)
    {
        // 人数后缀只给公司那一棵，其余顶层部门（存量数据里可能有多棵）维持原样。
        LPCTSTR labelSuffix = NULL;
        if (companyIndex >= 0 && roots[i]->m_deptid == companyDeptId)
        {
            labelSuffix = (LPCTSTR)companySuffix;
        }
        AddDepartmentSubtree(tv, -1, *roots[i], childrenByParent, contactById,
                             labelSuffix);
    }

    // 已挂进部门树的成员 userid 集合，供"其他联系人 / 服务号"去重。
    std::set<int64_t> placed;
    for (size_t i = 0; i < result.m_departments.size(); ++i)
    {
        const std::vector<net::DeptMemberItem>& ms = result.m_departments[i].m_members;
        for (size_t k = 0; k < ms.size(); ++k)
        {
            placed.insert(ms[k].m_userid);
        }
    }

    // 资料池里未进任何部门的联系人：普通用户归"其他联系人"，服务号归"服务号"。
    // 自己不再被跳过——正常情况下自己已在某个部门里、会被下面的 placed 判断挡掉；
    // 万一没有部门归属，也该出现在"其他联系人"里，而不是整个人从通讯录消失。
    std::vector<const net::ContactItem*> others;
    std::vector<const net::ContactItem*> services;
    for (size_t i = 0; i < result.m_contacts.size(); ++i)
    {
        const net::ContactItem& c = result.m_contacts[i];
        if (placed.find(c.m_userid) != placed.end())
        {
            continue;   // 已在部门树里
        }
        if (c.m_usertype == 1)
        {
            services.push_back(&c);
        }
        else
        {
            others.push_back(&c);
        }
    }
    if (!others.empty())
    {
        CString label;
        label.Format(_T("其他联系人  %d"), (int)others.size());
        int r = tv->AddRoot(label);
        tv->SetExpanded(r, true);
        tv->SetItemSelectable(r, false);
        m_contactNodeParent[r] = balloonmain::kTreeNodeNoParent;
        for (size_t i = 0; i < others.size(); ++i)
        {
            AddContactLeaf(tv, r, *others[i]);
        }
    }
    if (!services.empty())
    {
        CString label;
        label.Format(_T("服务号  %d"), (int)services.size());
        int r = tv->AddRoot(label);
        tv->SetExpanded(r, true);
        tv->SetItemSelectable(r, false);
        m_contactNodeParent[r] = balloonmain::kTreeNodeNoParent;
        for (size_t i = 0; i < services.size(); ++i)
        {
            AddContactLeaf(tv, r, *services[i]);
        }
    }

    SyncPaneContentHeight(balloonmain::kIdPaneContacts,
                          balloonmain::kIdContactsTree, kContactsPaneTitleHeight);

    LOG_INFO("真实通讯录已填充(组织架构): 企业=%s, 部门 %u 个, 联系人 %u 人。",
             result.m_enterprisename.c_str(),
             (uint32_t)result.m_departments.size(),
             (uint32_t)result.m_contacts.size());
}

void CBalloonMainPanel::AddDepartmentSubtree(
    balloonwjui::DuiTreeView* tv, int parentTreeNodeId,
    const net::DepartmentItem& dept,
    const std::map<int64_t, std::vector<const net::DepartmentItem*> >& childrenByParent,
    const std::map<int64_t, const net::ContactItem*>& contactById,
    LPCTSTR labelSuffix)
{
    // 1. 部门节点本身（根用 AddRoot，子部门用 AddChild）；部门是标题节点，
    //    点击只展开/收起，不参与开聊。
    CString deptLabel = Utf8ToCStr(dept.m_name);
    if (deptLabel.IsEmpty())
    {
        deptLabel.Format(_T("部门%lld"), (long long)dept.m_deptid);
    }
    // 公司根部门在名字后面挂人数（" · 161 人"），其余部门 labelSuffix 为 NULL。
    if (labelSuffix != NULL)
    {
        deptLabel += labelSuffix;
    }
    int deptNode = (parentTreeNodeId < 0)
        ? tv->AddRoot(deptLabel)
        : tv->AddChild(parentTreeNodeId, deptLabel);
    tv->SetExpanded(deptNode, true);
    tv->SetItemSelectable(deptNode, false);
    // 记下本部门节点的父节点，供 LocateContactInTree 自下而上展开整条部门路径。
    m_contactNodeParent[deptNode] = (parentTreeNodeId < 0)
        ? balloonmain::kTreeNodeNoParent : parentTreeNodeId;

    // 2. 子部门：按 deptorder 排序后递归。
    std::map<int64_t, std::vector<const net::DepartmentItem*> >::const_iterator cit =
        childrenByParent.find(dept.m_deptid);
    if (cit != childrenByParent.end())
    {
        std::vector<const net::DepartmentItem*> kids = cit->second;
        std::sort(kids.begin(), kids.end(), DeptOrderLess);
        for (size_t i = 0; i < kids.size(); ++i)
        {
            // 子部门不挂人数后缀 —— 人数只出现在公司那一行。
            AddDepartmentSubtree(tv, deptNode, *kids[i], childrenByParent, contactById,
                                 NULL);
        }
    }

    // 3. 直属成员：按 memberorder 排序，逐个挂叶子节点。自己也在其中——用户
    //    要能在树上看见自己处在哪个部门（自己那行由 AddContactLeaf 加"（我）"）。
    std::vector<net::DeptMemberItem> mems = dept.m_members;
    std::sort(mems.begin(), mems.end(), MemberOrderLess);
    for (size_t i = 0; i < mems.size(); ++i)
    {
        std::map<int64_t, const net::ContactItem*>::const_iterator fit =
            contactById.find(mems[i].m_userid);
        if (fit != contactById.end() && fit->second != NULL)
        {
            AddContactLeaf(tv, deptNode, *fit->second);
        }
        else
        {
            // 资料池里没有该成员（异常数据）：用最小信息挂节点，仍可开聊。
            net::ContactItem tmp;
            tmp.m_userid = mems[i].m_userid;
            AddContactLeaf(tv, deptNode, tmp);
        }
    }
}

HBITMAP CBalloonMainPanel::GetPresenceBadgeBitmap(int presence)
{
    // 先归一：一切越界值都落到"离线"，这样槽位下标一定在 0..3 内，
    // 也与 GetPresenceBadgeIconKind / GetPresenceVisual 的兜底口径一致。
    const int slot = balloonmain::NormalizePresenceForMenu(presence);
    if (slot < 0 || slot >= kPresenceBadgeSlotCount)
    {
        // 归一之后不该走到这里。真走到了说明 presence_type 的取值范围变了（新增了
        // 某个状态而 NormalizePresenceForMenu 没跟上），记一条日志便于定位，不画徽标。
        LOG_ERROR("presence badge slot out of range: presence=%d, slot=%d.", presence, slot);
        return NULL;
    }

    if (m_presenceBadges[slot] == NULL)
    {
        m_presenceBadges[slot] =
            balloonmain::CreatePresenceBadgeBitmap(slot, kPresenceBadgeSizePx);
        if (m_presenceBadges[slot] == NULL)
        {
            LOG_ERROR("failed to create presence badge bitmap: presence=%d, sizePx=%d.",
                slot, kPresenceBadgeSizePx);
        }
    }

    return m_presenceBadges[slot];
}

HBITMAP CBalloonMainPanel::GetEntityAvatarBitmap(int64_t id, const CString& displayName,
                                                 int facetype, const std::string& customface,
                                                 int sizePx)
{
    balloonmain::AvatarSpec spec = balloonmain::DecodeAvatar(facetype, customface, displayName, id);

    if (spec.m_kind == balloonmain::avatar_kind_image && !customface.empty())
    {
        int64_t selfId = (m_lpFMGClient != NULL)
                         ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
        std::wstring path =
            net::ImageTransfer::AvatarCacheFilePathForFileId(net::CurrentUserAccount(), customface);
        if (!path.empty() && ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            // 本地已有图片文件：取圆形真图（圆角半径=半边长即圆，匹配树里 monogram 的圆形）。
            HBITMAP hbm = m_avatarCache.GetImage(path.c_str(), sizePx);
            if (hbm != NULL)
            {
                return hbm;
            }
        }
        // 本地没有：触发下载（完成后 OnAvatarDownloadResult 刷新本节点），先回退 monogram 占位。
        if (selfId > 0)
        {
            net::ImageTransfer::GetInstance().EnqueueAvatarDownload(
                customface, net::CurrentUserAccount());
        }
        return m_avatarCache.GetMonogram(spec.m_glyph, spec.m_color, sizePx);
    }

    // 字形 / 自动：字形头像（字形 + 底色由 DecodeAvatar 给出）。
    return m_avatarCache.GetMonogram(spec.m_glyph, spec.m_color, sizePx);
}

void CBalloonMainPanel::AddContactLeaf(balloonwjui::DuiTreeView* tv, int parentNode,
                                       const net::ContactItem& c)
{
    // 主行：备注 → 昵称 → 账号 → "用户<id>"。取名口径与会话列表、单聊窗标题
    // 收口在同一个纯函数里，免得同一个人在三处叫三个名字。
    CString display = balloonmain::ResolveContactDisplayName(Utf8ToCStr(c.m_remark),
                                                             Utf8ToCStr(c.m_nickname),
                                                             Utf8ToCStr(c.m_username),
                                                             (UINT)c.m_userid);
    // 自己那一行加"（我）"后缀。通讯录里能看到自己，是为了让用户看清自己在
    // 组织架构中的位置；不加标记的话，在一屏同事里认出哪行是自己得靠记昵称。
    // 判定放在这里而不是各调用点，是为了不漏——挂联系人叶子的地方有五处。
    if (IsSelfUserId((UINT)c.m_userid))
    {
        display += _T("（我）");
    }

    // 头像：尊重对方设置的 (facetype, customface)——字形自定义现画、真图命中缓存即画、
    // 真图未缓存回退 monogram 并触发下载。位图归头像缓存所有，这里只借用。
    HBITMAP hAvatar = GetEntityAvatarBitmap(
        c.m_userid, display, c.m_facetype, c.m_customface, kContactAvatarSizePx);
    int node;
    if (hAvatar != NULL)
    {
        node = tv->AddChild(parentNode, display, hAvatar);
    }
    else
    {
        node = tv->AddChild(parentNode, display);
    }

    // 副行：个性签名（无则不显示）。
    CString sign = Utf8ToCStr(c.m_signature);
    if (!sign.IsEmpty())
    {
        tv->SetItemSubLabel(node, sign);
    }

    // 右侧在线状态徽标：按好友真实 presence（v2 presence_type，来自 cmd_get_contacts）取，
    // 复用与状态胶囊 / 状态菜单同一套字形与配色（在线实心圆 / 离开时钟 / 忙碌横杠 /
    // 离线空心环）。位图由本类缓存持有，树只借用指针，不可在此释放。
    // 收到 cmd_presence_changed_notify(1109) 时由 OnPresenceChanged 换此节点的徽标实现实时刷新。
    tv->SetItemStatusIcon(node, GetPresenceBadgeBitmap((int)c.m_presence));

    // 「特别关注」星标：画在姓名之后。标记在别处（右键菜单）改动时由
    // RefreshStarredMarks 就地刷新，不必整树重建。
    ApplyStarredMark(tv, node, (int64_t)c.m_userid);

    // 「我的收藏」徽章：画在姓名（有星标时在星标）之后。本方法只建部门树等分组下的行，
    // 「我的收藏」分区自己建行、不经这里，所以分区里的行不会重复画徽章。收藏变化时
    // 整棵联系人树会重建（RebuildContactsTree），徽章随之重打。
    ApplyFavoriteMark(tv, node, 1, (int64_t)c.m_userid);

    // 记录"节点 → userid"，供点击开聊。
    m_contactNodeUserId[node] = (UINT)c.m_userid;

    // 另记两份索引供搜索定位（LocateContactInTree）：一份是本节点的父节点，用来自下
    // 而上把整条部门路径展开（DuiTreeView 没有公开取父节点的接口，只能建树时自己记）；
    // 一份是 userid → 本节点，用来由人反查行。「我的收藏」分区自己建行、不经本方法，
    // 所以这里记下的一定是组织架构树里的那一行。
    m_contactNodeParent[node] = parentNode;
    m_contactOrgNode[(UINT)c.m_userid] = node;
}

// 消息内容类型：窗口抖动（与 net::message_type 的 message_type_shake 对齐）。收到
// 抖动需立刻弹出聊天窗并晃动以提醒；其余消息收到时一律不弹窗。
static const int kRecvMsgTypeShake = 13;

bool CBalloonMainPanel::EnsureMsgStoreOpen()
{
    if (m_lpFMGClient == NULL)
    {
        return false;
    }
    //本地库放在用户个人目录的 ChatLog 子目录，与 v1 的 msg.db 并存：msgv2.db。
    tstring personal = m_lpFMGClient->m_UserMgr.GetPersonalFolder();
    if (personal.empty())
    {
        return false;
    }
    std::wstring dbPath = personal + _T("ChatLog\\msgv2.db");
    if (m_msgStore.IsOpen())
    {
        if (dbPath == m_msgStoreDbPath)
        {
            return true;
        }
        //账号切换：关掉旧账号的库，改开新账号的库，避免把消息落到上一个账号的库里。
        m_msgStore.Close();
        m_msgStoreDbPath.clear();
    }
    //确保 ChatLog 目录存在（其父级“个人目录”由登录流程已建好）；已存在则忽略失败。
    tstring chatLogDir = personal + _T("ChatLog");
    ::CreateDirectoryW(chatLogDir.c_str(), NULL);
    if (!m_msgStore.Open(dbPath.c_str()))
    {
        LOG_ERROR(_T("打开 v2 本地消息库失败: %s"), dbPath.c_str());
        return false;
    }
    m_msgStoreDbPath = dbPath;

    //文件本地落点登记库与消息库同目录、同样按账号隔离。它记录"某文件在本机哪个位置"，
    //使翻历史时已有的文件（自己发出的源文件、之前下载过的）不必重新下载一遍。
    //打不开不影响聊天主流程，只是退化成每次都要重新下载，故失败仅记日志、不阻断。
    std::wstring fileDbPath = personal + _T("ChatLog\\filelocal.db");
    if (!balloonhistory::CFileLocalStore::GetInstance().Open(fileDbPath.c_str()))
    {
        LOG_ERROR(_T("打开文件本地落点登记库失败: %s"), fileDbPath.c_str());
    }

    //文件夹本地落点登记库：按会话 id 记住每一次文件夹传输在本机的根目录（发送端是源
    //目录、接收端是落地目录），使传输记录条上的「打开文件夹」在事后翻记录时仍然可用。
    //与上面两个库同目录、同样按账号隔离；打不开只是退化成"记录还在、但打不开目录"，
    //不影响聊天主流程，故失败仅记日志、不阻断。
    std::wstring folderDbPath = personal + _T("ChatLog\\folderlocal.db");
    if (!balloonhistory::CFolderLocalStore::GetInstance().Open(folderDbPath.c_str()))
    {
        LOG_ERROR(_T("打开文件夹本地落点登记库失败: %s"), folderDbPath.c_str());
    }
    return true;
}

void CBalloonMainPanel::ReconcileHistoryCoverage(const net::SyncMessagesResult& result)
{
    //没有锚点就算不出本页覆盖的范围：旧服务端不回带，补齐模式回带的是 aftermsgid
    if (!result.m_hasAnchor)
    {
        return;
    }
    if (!EnsureMsgStoreOpen())
    {
        return;
    }

    balloonhistory::SyncPageInfo page;
    page.m_beforeMsgid = result.m_beforeMsgid;
    page.m_hasMore = result.m_hasmore;
    for (size_t i = 0; i < result.m_messages.size(); ++i)
    {
        if (result.m_messages[i].m_msgid > 0)
        {
            page.m_msgids.push_back(result.m_messages[i].m_msgid);
        }
    }

    const balloonhistory::PageSpan span = balloonhistory::ComputePageSpan(page);
    if (!span.m_valid)
    {
        return;
    }

    //一、对账：本页覆盖范围内本地有、服务端没返回的，已在别处被删除或清理
    std::vector<int64_t> localIds;
    if (m_msgStore.QueryMsgidsInRange(result.m_ctype, result.m_cid, span.m_lo, span.m_hi, localIds))
    {
        const std::vector<int64_t> toDelete =
            balloonhistory::ComputeReconcileDeletions(page, localIds);
        if (!toDelete.empty())
        {
            int deleted = 0;
            m_msgStore.DeleteMessages(result.m_ctype, result.m_cid, toDelete, deleted);
            LOG_INFO("History reconcile: ctype=%d, cid=%lld, span=[%lld, %lld], removed %d local rows.",
                     result.m_ctype, (long long)result.m_cid,
                     (long long)span.m_lo, (long long)span.m_hi, deleted);
        }
    }

    //二、把本页并入覆盖区间
    balloonhistory::CoverRange cur;
    m_msgStore.GetCoverage(result.m_ctype, result.m_cid, cur);
    const balloonhistory::CoverRange merged = balloonhistory::MergeCoverage(cur, page);
    if (merged.m_valid != cur.m_valid || merged.m_lo != cur.m_lo || merged.m_hi != cur.m_hi
        || merged.m_reachedOldest != cur.m_reachedOldest)
    {
        m_msgStore.SetCoverage(result.m_ctype, result.m_cid, merged);
        LOG_INFO("History coverage: ctype=%d, cid=%lld, before=%lld, [%lld, %lld]%s -> [%lld, %lld]%s.",
                 result.m_ctype, (long long)result.m_cid, (long long)result.m_beforeMsgid,
                 (long long)cur.m_lo, (long long)cur.m_hi, cur.m_reachedOldest ? " oldest" : "",
                 (long long)merged.m_lo, (long long)merged.m_hi,
                 merged.m_reachedOldest ? " oldest" : "");
    }
}

/**
 *  判断用户此刻是不是正看着某个聊天窗 —— 是的话，刚到达的消息已经当场被看到，
 *  不该再给最近会话列表记未读。
 *
 *  判据本身在 balloonmain::ShouldTreatAsReadOnArrival（纯函数、有单测，见
 *  Tests/ConversationListOpsTests.cpp 的 RD 系列），本函数只负责把 Win32 侧的
 *  窗口状态查出来喂给它，好让判据保持可测。
 *    hWnd  聊天窗句柄；NULL 表示该会话的窗口根本没打开。
 *  @return 用户正看着该窗口返回 true
 */
static bool IsUserWatchingChatWindow(HWND hWnd)
{
    const bool windowExists = (hWnd != NULL);
    const bool isWindow     = windowExists && (::IsWindow(hWnd) != FALSE);
    const bool isVisible    = isWindow && (::IsWindowVisible(hWnd) != FALSE);
    const bool isIconic     = isWindow && (::IsIconic(hWnd) != FALSE);
    const bool isForeground = isWindow && (::GetForegroundWindow() == hWnd);

    return balloonmain::ShouldTreatAsReadOnArrival(windowExists, isWindow, isVisible,
                                                   isIconic, isForeground);
}

void CBalloonMainPanel::OnRecvChatMessage(const net::MessageNotify& notify)
{
    // 预览按消息类型统一算（文本取正文，抖动/表情等取占位文案），与服务端 digest
    // 语义一致；单聊群聊共用。
    std::string digest = net::MessageProtocolClient::digestForMsgType(
        notify.m_msgtype, notify.m_text);

    // 文件夹传输记录（msgtype=18）：摘要要带上文件夹名，写成 "[文件夹] 项目资料"。
    // digestForMsgType 只按类型给占位、拿不到文件夹名（它在 content 里而不在正文里），
    // 故在这里换成邀请阶段那份现成的拼装函数 —— 邀请与记录在会话列表上是同一种东西的
    // 两个阶段，措辞必须逐字相同，两处各写一份迟早会走偏。
    if (notify.m_msgtype == message_type_folder && !notify.m_folderName.empty())
    {
        digest = balloonmain::BuildFolderInviteDigest(notify.m_folderName);
    }

    // 本端 userid —— 用于识别"自己在别的设备发出、同步回来的消息"：这类消息发送方即
    // 本人，不应在托盘闪头像提醒（否则会闪自己的头像、单击还打开和自己的会话）。
    UINT selfId = (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;

    // 是否已"关闭所有声音"（托盘菜单"关闭所有声音"与系统设置"提示音"开关写的是同一个
    // 静音位）。下面在"他人发来新消息、且其聊天窗未打开"时据此决定是否播放新消息提示音。
    bool bMuted = (m_lpFMGClient != NULL) ? (m_lpFMGClient->m_UserConfig.IsEnableMute() != FALSE) : false;

    // 发送方不在通讯录缓存里：多半是本端登录之后才进组织架构的新同事（自助注册、
    // 或管理台新建的员工）。此时消息上只能显示"用户<userid>"这种兜底名字，补拉一次
    // 通讯录就能把真名换回来。正常路径是服务端推 cmd_org_structure_notify（见
    // OnOrgStructureChanged），这里管的是那条推送没到时的补救 —— 典型是变更恰好
    // 发生在本端掉线重连期间。节流交给 m_unknownSenderPolicy，同一个陌生人只补拉
    // 一次、两次补拉之间还有冷却。
    if (notify.m_from > 0
        && (UINT)notify.m_from != selfId
        && FindCachedContact((UINT)notify.m_from) == NULL
        && m_unknownSenderPolicy.ShouldRefreshContacts(notify.m_from, (int64_t)::GetTickCount64()))
    {
        LOG_INFO("OnRecvChatMessage: sender %lld not in contacts cache, refetch contacts.",
                 (long long)notify.m_from);
        CIUSocket::GetInstance().RequestContacts();
    }

    // 落本地库：实时收到的消息（含自己在别端发出、同步回来的）按会话归属持久化，
    // 供历史消息记录窗口离线查询。仅持久化已带服务器 msgid 的消息。会话归属与本函数
    // 下方口径一致：单聊取发送方 userid（peerUserId=notify.m_from），群聊取群 id
    // （notify.m_cid）。
    // 单聊会话的对端 userid。绝大多数消息里 from 就是对端（别人发给我的那条）；但
    // <u>服务端生成的消息</u>会给双方各推一帧 —— 典型是文件夹传输记录条（msgtype=18），
    // 本端是文件夹发送方的那一帧里 from 就是自己。此时对端只能取 cid：协议约定 cid 恒为
    // 「收件方视角下的对端 userid」（见 docs/协议设计-v2.md）。不这么判的话，那一帧会被
    // 归进"自己和自己"的会话里，聊天窗与会话列表都找不到它。
    const int64_t singlePeerId =
        ((UINT)notify.m_from == selfId && notify.m_cid > 0)
        ? notify.m_cid
        : (int64_t)notify.m_from;

    if (notify.m_msgid > 0 && EnsureMsgStoreOpen())
    {
        int64_t convCid = (notify.m_ctype == 2) ? notify.m_cid : singlePeerId;
        balloonhistory::HistoryRecord rec =
            balloonhistory::RecordFromNotify(notify, notify.m_ctype, convCid);
        m_msgStore.Upsert(rec);
    }

    if (notify.m_ctype == 2)
    {
        // 群消息：cid=群 id。收到消息不弹窗——只把消息渲染进“已经打开着”的群聊窗；
        // 群窗没开就不创建、不弹出，仅靠下面 BumpConversation 在会话列表做未读提示
        //（用户点开群会话时再建窗，靠历史同步看到这条）。群聊暂无窗口抖动。
        int64_t groupId = notify.m_cid;
        bool bGroupWindowOpen = false;
        HWND hGroupWnd = NULL;   // 已打开的群聊窗句柄；未打开时为 NULL，供已读判定使用
        std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.find(groupId);
        if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            it->second->OnRecvGroupMessage(notify);
            bGroupWindowOpen = true;
            hGroupWnd = it->second->m_hWnd;
        }
        // @提醒：本条有没有 @ 到我。<u>只看 uid、不看文本</u>，所以群名片改了、群里有
        // 同名成员都不受影响。自己在别端发出、同步回来的消息不算（不该提醒自己）。
        const bool bAtMe =
            (UINT)notify.m_from != selfId
            && balloonchatwnd::IsMentioningMe(notify.m_ats, notify.m_atAll, (int64_t)selfId);

        // 被 @ 时给会话列表的预览加前缀，让用户在列表上一眼看出"这条点了我的名"。
        std::string groupDigest = digest;
        if (bAtMe)
        {
            groupDigest = "[\xE6\x9C\x89\xE4\xBA\xBA@\xE6\x88\x91] " + digest;   // "[有人@我] "（UTF-8）
        }

        // 用户此刻是不是正看着这个群窗（可见、未最小化、且在前台）。是的话这条消息
        // 已经当场被看到，不该再记未读——否则就出现"消息就在眼前，会话列表还亮着
        // 红点"。判据与单聊共用同一个纯函数，两边口径一致。
        const bool bGroupWatching = IsUserWatchingChatWindow(hGroupWnd);

        // 未读只由他人发来的消息构成：服务端把群消息推给<u>包括发送者在内</u>的全体成员，
        // 自己发的群消息回推回来时若也 +1，自己发一条就会给自己点亮一个红点。服务端生成的
        // 系统消息（有人退群、转让群主、发布公告等）同样不计，只更新会话摘要与时间。
        const bool bGroupCountAsUnread =
            net::ConversationsProtocolClient::shouldCountAsUnread((UINT)notify.m_from, selfId,
                                                                  notify.m_msgtype);

        BumpConversation(2, groupId, (UINT)notify.m_from, groupDigest,
                         notify.m_msgtype, notify.m_timestamp, notify.m_msgid,
                         bGroupCountAsUnread && !bGroupWatching);

        // 正看着：除了本条不计未读，之前累积的旧未读也一并清掉，并把已读位点上报
        // 服务端（否则换设备 / 重新登录时红点又会冒出来）。必须放在 BumpConversation
        // <u>之后</u> —— 本条的 msgid 要先落进 m_lastmsgid，上报的已读位点才包含它。
        if (bGroupWatching)
        {
            MarkConversationRead(2, groupId);
        }
        // 群窗未打开、且不是自己同步回来的消息：在托盘闪该群头像提醒（窗口已开则消息
        // 已渲染进去，无需闪）。设了"消息免打扰"的会话除外——它的未读数照常在会话列表
        // 上累加，但不闪托盘、不出声，这正是免打扰的意思。
        // 例外：被 @ 到时<u>突破免打扰</u>照常闪托盘 —— 群消息免打扰的本意是"别为每条
        // 群聊都打扰我"，而点名到我头上的那条恰恰是用户设了免打扰也想知道的。提示音
        // 仍受全局静音位约束（bMuted），这里不动。
        const bool bConvMuted = balloonmain::IsConversationMuted(m_conversations, 2, groupId);

        // 判据逐条打日志，理由同单聊分支（见下方 OnRecvChatMessage 单聊部分的注释）。
        LOG_INFO("群聊托盘闪烁判据: group=%lld, from=%u, self=%u, msgtype=%d, 计未读=%d, 群窗已开=%d, "
                 "免打扰=%d, @我=%d, 正看着=%d。",
                 (long long)groupId, (UINT)notify.m_from, selfId, notify.m_msgtype,
                 bGroupCountAsUnread ? 1 : 0, bGroupWindowOpen ? 1 : 0, bConvMuted ? 1 : 0,
                 bAtMe ? 1 : 0, bGroupWatching ? 1 : 0);

        // 不计未读的消息（自己同步回来的、系统消息）也不闪托盘、不出声
        if (!bGroupWindowOpen && bGroupCountAsUnread && (!bConvMuted || bAtMe))
        {
            PostTrayFlash(2, groupId, groupDigest);
            // 群窗未打开、非自己同步消息：闪托盘的同时异步播放新消息提示音（不阻塞 UI 线程）。
            balloonmain::PlayNotifySound(balloonmain::kNotifySoundNewMessage, bMuted);
        }
        return;
    }

    // 单聊：收件人视角下发送方 userid 即本会话对端。服务端生成、双方各推一帧的那类
    // 消息（文件夹传输记录条）在本端是发送方时 from 就是自己，此时改取 cid，
    // 判定收在上面的 singlePeerId 里，两处（落库与路由）用的是同一个值。
    UINT peerUserId = (UINT)singlePeerId;

    // 收到消息默认不弹窗：只把消息渲染进“已经打开着”的聊天窗；窗口没开就不创建、
    // 不弹出，仅靠下面 BumpConversation 在会话列表做未读提示（用户点开会话时再建窗、
    // 靠历史同步看到这条）。唯一例外是“窗口抖动”——它需要立刻弹出并晃动窗口提醒，
    // 故对抖动才 EnsureChatWindow 建窗 / 弹窗。
    CBalloonChatWindow* pWin = NULL;
    if (notify.m_msgtype == kRecvMsgTypeShake)
    {
        pWin = EnsureChatWindow(peerUserId);
    }
    else
    {
        std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.find(peerUserId);
        if (it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            pWin = it->second;
        }
    }
    if (pWin != NULL)
    {
        pWin->OnRecvMessage(notify);
    }

    // 用户此刻是不是正看着这个聊天窗，口径同群聊分支。
    const bool bWatching = IsUserWatchingChatWindow((pWin != NULL) ? pWin->m_hWnd : NULL);

    // 同群聊：多端登录时服务端会把自己发的单聊消息同步回本端，那类推送不计未读。
    const bool bCountAsUnread =
        net::ConversationsProtocolClient::shouldCountAsUnread((UINT)notify.m_from, selfId,
                                                              notify.m_msgtype);

    BumpConversation(1, (int64_t)peerUserId, (UINT)notify.m_from, digest,
                     notify.m_msgtype, notify.m_timestamp, notify.m_msgid,
                     bCountAsUnread && !bWatching);

    // 正看着：连同旧未读一并清零并上报已读位点，理由与顺序要求同群聊分支。
    if (bWatching)
    {
        MarkConversationRead(1, (int64_t)peerUserId);
    }

    // 单聊窗未打开（pWin==NULL）、非窗口抖动、且不是自己同步回来的消息：在托盘闪对端
    // 头像提醒。窗口已打开时消息已渲染进窗口、无需闪；抖动消息上面已 EnsureChatWindow
    // 弹窗，pWin 必非空、自然不闪；自己同步消息 peerUserId==selfId、不闪。设了"消息
    // 免打扰"的会话同样不闪不响（未读数仍照常累加）。
    const bool bConvMuted =
        balloonmain::IsConversationMuted(m_conversations, 1, (int64_t)peerUserId);

    // 这四项决定"闪不闪"，任何一项不满足都静默地不闪。逐条打出来 —— 排查
    // "收到消息托盘不闪"时，光看现象分不清是判据没过还是后面链路断了。
    LOG_INFO("单聊托盘闪烁判据: peer=%u, self=%u, 窗口已开=%d, 消息类型=%d(抖动=%d), "
             "免打扰=%d, 正看着=%d。",
             peerUserId, selfId, (pWin != NULL) ? 1 : 0, notify.m_msgtype,
             (int)kRecvMsgTypeShake, bConvMuted ? 1 : 0, bWatching ? 1 : 0);

    // 「不是本人发出的」要按发送方判断，不能只看对端：本人转发给别人的消息（主面板用转发
    // 应答里的消息体调本函数）与文件夹传输记录条里本端是发送方的那一帧，对端都是别人、
    // 发送方却是本人，只判对端会给本人自己发出的消息闪托盘、响提示音。
    if (pWin == NULL && notify.m_msgtype != kRecvMsgTypeShake && peerUserId != selfId
        && (UINT)notify.m_from != selfId && !bConvMuted)
    {
        PostTrayFlash(1, (int64_t)peerUserId, digest);
        // 单聊窗未打开、非窗口抖动、非自己同步消息：闪托盘的同时异步播放新消息提示音。
        balloonmain::PlayNotifySound(balloonmain::kNotifySoundNewMessage, bMuted);
    }
}

void CBalloonMainPanel::OnSendChatAck(const net::SendMessageResponse& resp)
{
    if (resp.m_code == 0)
    {
        LOG_INFO("send message ok: clientmsgid=%s, msgid=%lld.",
                 resp.m_clientmsgid.c_str(), (long long)resp.m_msgid);

        // 把服务器分配的 msgid 与消息时间回填到那条乐观气泡上 —— 没有这一步，本端
        // 发出的气泡会一直是 msgid=0 / 时间=0，右键菜单里就永远不会出现"撤回"
        // （时限判断拿不到依据）。回执只带 clientmsgid、不带 ctype/cid，而 clientmsgid
        // 全局唯一，故与 OnImageSendProgress 同法向所有已打开的聊天窗 + 群聊窗广播，
        // 命中者回填、其余安静忽略。
        for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
             it != m_chatWindows.end(); ++it)
        {
            if (it->second != NULL && it->second->IsWindow())
            {
                it->second->OnSendChatAckBackfill(resp.m_clientmsgid, resp.m_msgid,
                                                  resp.m_timestamp);
            }
        }
        for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator git = m_groupChatWindows.begin();
             git != m_groupChatWindows.end(); ++git)
        {
            if (git->second != NULL && git->second->IsWindow())
            {
                git->second->OnSendChatAckBackfill(resp.m_clientmsgid, resp.m_msgid,
                                                   resp.m_timestamp);
            }
        }
        return;
    }
    // 失败：弹框提示（对端不存在 / 被拉黑 / 不能发给自己 / 内容过长等）。单聊群聊共用本回执。
    LOG_ERROR("send message failed: code=%d, msg=%s, clientmsgid=%s.",
              resp.m_code, resp.m_msg.c_str(), resp.m_clientmsgid.c_str());
    const std::wstring tip = balloonchatwnd::SendFailureText(resp.m_code);
    ::MessageBox(m_hWnd, tip.c_str(), _T("FlamingoIM"), MB_OK | MB_ICONWARNING);
}

void CBalloonMainPanel::OnImageSendProgress(const net::ImageSendProgress& prog)
{
    // 进度报文只带 clientmsgid（无 ctype/cid），而 clientmsgid 全局唯一，故向所有
    // 已打开的单聊窗 + 群聊窗广播 —— 命中者更新其"发送中"图片气泡，其余窗口按
    // clientmsgid 找不到本行、静默忽略。
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
        {
            it->second->OnImageSendProgress(prog.m_clientmsgid, prog.m_sent, prog.m_total);
        }
    }
    for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator git = m_groupChatWindows.begin();
         git != m_groupChatWindows.end(); ++git)
    {
        if (git->second != NULL && git->second->IsWindow())
        {
            git->second->OnImageSendProgress(prog.m_clientmsgid, prog.m_sent, prog.m_total);
        }
    }
}

void CBalloonMainPanel::OnImageSendResult(const net::ImageSendResult& result)
{
    if (!result.m_ok)
    {
        LOG_ERROR("发图失败: ctype=%d, cid=%lld, code=%d, clientmsgid=%s.",
                  result.m_ctype, (long long)result.m_cid, result.m_errcode,
                  result.m_clientmsgid.c_str());
    }

    // 结果报文带 ctype/cid，可精确路由。本期只接单聊发图（ctype==1）。
    if (result.m_ctype == 1)
    {
        std::map<UINT, CBalloonChatWindow*>::iterator it =
            m_chatWindows.find((UINT)result.m_cid);
        if (it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            it->second->OnImageSendResult(result.m_ok, result.m_clientmsgid,
                                              result.m_fileid, result.m_md5);
        }
        return;
    }

    // ctype==2（群聊发图）：按 cid=群 id 找到对应群聊窗，更新其图片气泡为已发 / 失败。
    if (result.m_ctype == 2)
    {
        std::map<int64_t, CBalloonGroupChatWindow*>::iterator it =
            m_groupChatWindows.find((int64_t)result.m_cid);
        if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            it->second->OnImageSendResult(result.m_ok, result.m_clientmsgid,
                                              result.m_fileid, result.m_md5);
        }
        return;
    }

    LOG_INFO("收到未知会话类型的发图结果: ctype=%d, cid=%lld, ok=%d.",
             result.m_ctype, (long long)result.m_cid, result.m_ok ? 1 : 0);
}

void CBalloonMainPanel::OnImageDownloadResult(const net::ImageDownloadResult& result)
{
    if (!result.m_ok)
    {
        LOG_ERROR("收图下载失败: msgid=%lld, code=%d.",
                  (long long)result.m_msgid, result.m_errcode);
    }

    //下载结果只带 msgid（msgid 全局唯一），向所有已开单聊窗 + 群聊窗广播——命中那条
    //"下载中"气泡的窗口回填渲染 / 标失败，其余窗口按 msgid 找不到本行、静默忽略。
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second == NULL || !it->second->IsWindow())
            continue;
        if (result.m_ok)
            it->second->OnImageDownloaded(result.m_msgid, result.m_localpath);
        else
            it->second->OnImageDownloadFailed(result.m_msgid);
    }
    for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator git = m_groupChatWindows.begin();
         git != m_groupChatWindows.end(); ++git)
    {
        if (git->second == NULL || !git->second->IsWindow())
            continue;
        if (result.m_ok)
            git->second->OnImageDownloaded(result.m_msgid, result.m_localpath);
        else
            git->second->OnImageDownloadFailed(result.m_msgid);
    }

    //消息记录窗也可能发起过图片下载（用户双击了缩略图、或点了菜单里的「下载」）。
    //它自己按 msgid 判断这一笔是不是它要的，不是就安静忽略。
    m_historyWindow.OnImageDownloadFinished(result.m_msgid, result.m_ok,
                                            result.m_localpath);
    //聊天记录查看窗同理，各窗口按 msgid 认领自己发起的那一笔
    NotifyMergedViewersDownload(result.m_msgid, result.m_ok, result.m_localpath, false, false);
}

void CBalloonMainPanel::OnFileSendProgress(const net::FileSendProgress& prog)
{
    //进度报文只带 clientmsgid（全局唯一），向所有已开单聊窗 + 群聊窗广播——命中者更新
    //其"发送中"文件气泡进度，其余窗口按 clientmsgid 找不到本行、静默忽略。
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
        {
            it->second->OnFileSendProgress(prog.m_clientmsgid, prog.m_sent, prog.m_total);
        }
    }
    for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator git = m_groupChatWindows.begin();
         git != m_groupChatWindows.end(); ++git)
    {
        if (git->second != NULL && git->second->IsWindow())
        {
            git->second->OnFileSendProgress(prog.m_clientmsgid, prog.m_sent, prog.m_total);
        }
    }
}

void CBalloonMainPanel::OnFileSendResult(const net::FileSendResult& result)
{
    //用户主动取消不是故障，记 INFO；其余失败才记 ERROR，免得日志里真问题被淹没。
    if (result.m_cancelled)
    {
        LOG_INFO("发文件被用户取消: ctype=%d, cid=%lld, clientmsgid=%s.",
                 result.m_ctype, (long long)result.m_cid, result.m_clientmsgid.c_str());
    }
    else if (!result.m_ok)
    {
        LOG_ERROR("发文件失败: ctype=%d, cid=%lld, code=%d, clientmsgid=%s.",
                  result.m_ctype, (long long)result.m_cid, result.m_errcode,
                  result.m_clientmsgid.c_str());
    }
    else
    {
        //发送成功：把这份源文件的位置登记下来。日后翻到这条消息时，只要文件还在原处
        //就能直接打开，不必再从服务器拉一遍（重传问题的根治点之一）。
        balloonhistory::CFileLocalStore::GetInstance().Upsert(
            result.m_md5, result.m_filename, result.m_localpath);
    }

    //结果报文带 ctype/cid，可精确路由。单聊(ctype==1)按对端 userid 找单聊窗。
    if (result.m_ctype == 1)
    {
        std::map<UINT, CBalloonChatWindow*>::iterator it =
            m_chatWindows.find((UINT)result.m_cid);
        if (it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            it->second->OnFileSendResult(result.m_ok, result.m_cancelled, result.m_clientmsgid);
        }
        return;
    }

    //群聊(ctype==2)按 cid=群 id 找群聊窗。
    if (result.m_ctype == 2)
    {
        std::map<int64_t, CBalloonGroupChatWindow*>::iterator it =
            m_groupChatWindows.find((int64_t)result.m_cid);
        if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            it->second->OnFileSendResult(result.m_ok, result.m_cancelled, result.m_clientmsgid);
        }
        return;
    }

    LOG_INFO("收到未知会话类型的发文件结果: ctype=%d, cid=%lld, ok=%d.",
             result.m_ctype, (long long)result.m_cid, result.m_ok ? 1 : 0);
}

void CBalloonMainPanel::OnFileDownloadProgress(const net::FileDownloadProgress& prog)
{
    //进度只带 msgid（全局唯一），向所有已开聊天窗广播——命中那条"下载中"气泡更新进度。
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
        {
            it->second->OnFileDownloadProgress(prog.m_msgid, prog.m_received, prog.m_total);
        }
    }
    for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator git = m_groupChatWindows.begin();
         git != m_groupChatWindows.end(); ++git)
    {
        if (git->second != NULL && git->second->IsWindow())
        {
            git->second->OnFileDownloadProgress(prog.m_msgid, prog.m_received, prog.m_total);
        }
    }
    //聊天记录查看窗的文件卡片同样显示下载进度，各窗口只认自己发起的那一笔
    for (size_t i = 0; i < m_mergedViewers.size(); ++i)
    {
        if (m_mergedViewers[i] && m_mergedViewers[i]->IsWindow())
        {
            m_mergedViewers[i]->OnFileDownloadProgress(prog.m_msgid, prog.m_received, prog.m_total);
        }
    }
}

void CBalloonMainPanel::OnFileDownloadResult(const net::FileDownloadResult& result)
{
    if (!result.m_ok)
    {
        LOG_ERROR("收文件下载失败: msgid=%lld, code=%d.",
                  (long long)result.m_msgid, result.m_errcode);
    }
    else
    {
        //下载成功：登记落地位置。下次翻到这条消息时文件还在就直接打开，不必重下。
        balloonhistory::CFileLocalStore::GetInstance().Upsert(
            result.m_md5, result.m_filename, result.m_localpath);
    }

    //下载结果只带 msgid，向所有已开单聊窗 + 群聊窗广播——命中那条气泡回填渲染 / 标失败。
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second == NULL || !it->second->IsWindow())
            continue;
        if (result.m_ok)
            it->second->OnFileDownloaded(result.m_msgid, result.m_localpath);
        else
            it->second->OnFileDownloadFailed(result.m_msgid, result.m_cancelled);
    }
    for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator git = m_groupChatWindows.begin();
         git != m_groupChatWindows.end(); ++git)
    {
        if (git->second == NULL || !git->second->IsWindow())
            continue;
        if (result.m_ok)
            git->second->OnFileDownloaded(result.m_msgid, result.m_localpath);
        else
            git->second->OnFileDownloadFailed(result.m_msgid, result.m_cancelled);
    }

    //消息记录窗同理：它可能从行右键菜单发起过下载，按 msgid 认领属于自己的那一笔。
    m_historyWindow.OnFileDownloadFinished(result.m_msgid, result.m_ok,
                                           result.m_localpath, result.m_cancelled);
    //聊天记录查看窗同理，各窗口按 msgid 认领自己发起的那一笔
    NotifyMergedViewersDownload(result.m_msgid, result.m_ok, result.m_localpath, true,
                                result.m_cancelled);
}

void CBalloonMainPanel::OnFileTransferAck(const net::FileTransferAckNotify& notify)
{
    LOG_INFO("对方收取文件状态变化: cid=%lld, msgid=%lld, state=%d.",
             (long long)notify.m_cid, (long long)notify.m_msgid, notify.m_state);

    //推送只带 msgid（全局唯一），向所有已开单聊窗广播——命中本端发出的那条文件气泡
    //更新状态行。群聊窗不处理文件消息，故不必广播给它们。
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
        {
            it->second->OnFilePeerAck(notify.m_msgid, notify.m_state);
        }
    }
}

// =======================================================================
// v2 单聊文件夹传输（cmd_folder_* 1810~1817）—— 路由到对应聊天窗
//
// 除邀请推送要按发送方 userid 精确定位之外，其余一律向所有已打开的单聊窗广播：会话 id
// 全局唯一，命中的那个窗口更新自己那条气泡，其余窗口查不到本行、静默忽略。群聊窗不参与
// 文件夹传输（本期只做单聊），故不广播给它们。
//
// 本机同时可进行多笔文件夹传输（上限见 flamingo.ini 的 [app] foldermaxconcurrent），故
// <u>同一个会话窗里可能同时挂着两条以上文件夹气泡</u>。这里的广播对此天然成立：每一跳都
// 只带会话 id，聊天窗按会话 id 精确匹配到某一条气泡（ChatMessageList::GetFolderMessage /
// SetFolderMessage 对空 id 一律不匹配），故多笔并存时进度与结果不会串到别人那条上。
// 发送端在服务端分配会话 id 之前用的是本地占位 id，那个 id 同样逐笔唯一（见
// FolderTransferManager 的 MakeLocalSessionId）。
// =======================================================================

void CBalloonMainPanel::OnFolderInviteNotify(const net::FolderInviteNotify& notify)
{
    LOG_INFO("Route folder invite, session=%s, from=%lld, files=%lld, bytes=%lld.",
             notify.m_sessionId.c_str(), (long long)notify.m_fromId,
             (long long)notify.m_totalFiles, (long long)notify.m_totalSize);

    //收件人视角下邀请的发起方 userid 即本会话对端。
    const UINT peerUserId = (UINT)notify.m_fromId;

    //本端 userid 与全局静音位，两者的取法与收普通消息那条路径（OnRecvChatMessage）一致。
    const UINT selfId = (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    const bool bMuted = (m_lpFMGClient != NULL)
                        ? (m_lpFMGClient->m_UserConfig.IsEnableMute() != FALSE) : false;

    //收到邀请<u>不弹窗</u>：只把邀请气泡渲染进"已经打开着"的聊天窗；窗口没开就不建窗、
    //不弹出。此前这里无条件 EnsureChatWindow，接收方会凭空弹出一个尚未渲染任何内容的
    //聊天窗，是一次明显的打扰。窗口没开时改由下面的会话列表未读 + 托盘闪烁 + 提示音
    //提醒，用户点开会话时再由 CBalloonChatWindow 向管理器查回这条待应答邀请补出气泡。
    CBalloonChatWindow* pWin = NULL;
    std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.find(peerUserId);
    if (it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow())
    {
        pWin = it->second;
    }
    if (pWin != NULL)
    {
        pWin->OnFolderInvite(notify);
    }

    //用户此刻是不是正看着这个聊天窗，口径与收普通消息完全一致。
    const bool bWatching = IsUserWatchingChatWindow((pWin != NULL) ? pWin->m_hWnd : NULL);

    //会话列表预览形如 "[文件夹] 项目资料"，与文件消息显示 "[文件]" 同体例。
    const std::string digest = balloonmain::BuildFolderInviteDigest(notify.m_folderName);

    const bool bFromSelf = (peerUserId == selfId);
    const bool bCountAsUnread = balloonmain::ShouldCountFolderInviteAsUnread(bFromSelf, bWatching);

    //邀请<u>不是服务端消息</u>：它不进 t_chatmsg、没有 msgid，故 msgid 一律传 0。
    //BumpConversation 只在 msgid 更大时推进已读上限锚点 m_lastmsgid，传 0 即保证不会
    //污染真实消息序号 —— 与本端自己发出消息时的传法（那时 msgid 也还没回来）同理。
    //消息类型取 message_type_folder：邀请本身仍然不进服务端消息表，但传输走到终态后
    //服务端会在同一个会话里落一条这个类型的记录条，两个阶段用同一个类型，会话列表上
    //前后就不会先显示成文件、又变成文件夹。预览文字直接用上面组装好的 digest，不由
    //类型反推。
    const int64_t nowMs = (int64_t)::time(NULL) * 1000;   // 秒 → 毫秒
    BumpConversation(conversation_single, (int64_t)peerUserId, peerUserId, digest,
                     message_type_folder, nowMs, 0, bCountAsUnread);

    //正看着这个窗口：连同该会话此前累积的旧未读一并清零并上报已读位点，顺序要求同
    //收普通消息那条路径（须在 BumpConversation 之后）。
    if (bWatching)
    {
        MarkConversationRead(conversation_single, (int64_t)peerUserId);
    }

    const bool bConvMuted =
        balloonmain::IsConversationMuted(m_conversations, conversation_single, (int64_t)peerUserId);
    const bool bNotify =
        balloonmain::ShouldNotifyFolderInvite(pWin != NULL, bFromSelf, bConvMuted);

    //判据逐条打日志，理由同收普通消息那条路径：排查"收到邀请但托盘不闪"时，光看现象
    //分不清是判据没过还是后面链路断了。
    LOG_INFO("Folder invite tray flash check: peer=%u, self=%u, window open=%d, muted=%d, "
             "watching=%d, notify=%d.",
             peerUserId, selfId, (pWin != NULL) ? 1 : 0, bConvMuted ? 1 : 0,
             bWatching ? 1 : 0, bNotify ? 1 : 0);

    if (bNotify)
    {
        PostTrayFlash(conversation_single, (int64_t)peerUserId, digest);
        //与收普通消息同样异步播放新消息提示音（受全局静音位约束，不阻塞 UI 线程）。
        balloonmain::PlayNotifySound(balloonmain::kNotifySoundNewMessage, bMuted);
    }
}

void CBalloonMainPanel::OnFolderScanned(const foldertransfer::FolderScanNotice& notice)
{
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
            it->second->OnFolderScanned(notice);
    }
}

void CBalloonMainPanel::OnFolderBound(const foldertransfer::FolderBindNotice& bind)
{
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
            it->second->OnFolderBound(bind);
    }
}

void CBalloonMainPanel::OnFolderStateNotify(const net::FolderStateNotify& notify)
{
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
            it->second->OnFolderState(notify);
    }
}

void CBalloonMainPanel::OnFolderTransferStarted(const std::string& sessionId)
{
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
            it->second->OnFolderTransferStarted(sessionId);
    }
}

void CBalloonMainPanel::OnFolderProgress(const foldertransfer::FolderProgressNotice& progress)
{
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
            it->second->OnFolderProgress(progress);
    }
}

void CBalloonMainPanel::OnFolderResult(const foldertransfer::FolderResultNotice& result)
{
    LOG_INFO("Route folder result, session=%s, success=%d, errcode=%d.",
             result.m_sessionId.c_str(), result.m_success ? 1 : 0, result.m_errCode);

    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
            it->second->OnFolderResult(result);
    }
}

void CBalloonMainPanel::OnTypingNotify(const net::TypingNotify& notify)
{
    //推送里的 cid 就是"正在输入的那个人"（服务端转发时已改写成发起方 userid），
    //故可直接按它精确定位单聊窗，不必像文件状态那样广播。
    std::map<UINT, CBalloonChatWindow*>::iterator it =
        m_chatWindows.find((UINT)notify.m_cid);
    if (it == m_chatWindows.end() || it->second == NULL || !it->second->IsWindow())
    {
        //窗口没开：这条提示无处可显，直接丢弃。不必记住状态——等用户开窗时，
        //对方那句话多半已经发出来了。
        LOG_INFO("对端正在输入但聊天窗未打开，忽略: cid=%lld, state=%d.",
                 (long long)notify.m_cid, notify.m_state);
        return;
    }

    LOG_INFO("路由对端正在输入到聊天窗: cid=%lld, state=%d.",
             (long long)notify.m_cid, notify.m_state);
    it->second->OnPeerTyping(notify.m_state);
}

// =======================================================================
// 会话列表（Messages 页）—— 真实单聊会话填充与实时更新
// =======================================================================

const net::ContactItem* CBalloonMainPanel::FindCachedContact(UINT userId) const
{
    // 经索引直接定位。下标越界说明资料池被替换后漏了 Rebuild，按查不到处理并记日志，
    // 不去访问越界元素。
    const int idx = m_contactIndex.Find(userId);
    if (idx < 0)
    {
        return NULL;
    }
    if ((size_t)idx >= m_contacts.size())
    {
        LOG_ERROR("FindCachedContact: stale contact index (idx=%d, pool=%u), index not rebuilt after the pool changed.",
                  idx, (unsigned)m_contacts.size());
        return NULL;
    }
    return &m_contacts[(size_t)idx];
}

net::ContactItem* CBalloonMainPanel::FindCachedContactMutable(UINT userId)
{
    return const_cast<net::ContactItem*>(
        static_cast<const CBalloonMainPanel*>(this)->FindCachedContact(userId));
}

void CBalloonMainPanel::OnConversationsArrived(const net::ConversationsResult& result)
{
    // 单聊(ctype==1)与群聊(ctype==2)会话都保留：单聊进"消息"Tab，群聊既进"消息"Tab
    // 又作"群"Tab 的群源。cid<=0 的脏数据丢弃。
    m_conversations.clear();
    for (size_t i = 0; i < result.m_items.size(); ++i)
    {
        const net::ConversationItem& it = result.m_items[i];
        if (it.m_ctype != 1 && it.m_ctype != 2)
        {
            continue;   // 服务号等其它会话类型本轮不展示
        }
        if (it.m_cid <= 0)
        {
            continue;
        }
        m_conversations.push_back(it);

        //服务端的 digest 对文本消息就是原始正文，内联表情在里面还是 /f["<id>"] 的样子，
        //直接显示会让最近会话列表出现一行 /f["75"]。这里统一净化成 "[表情]"，与实时收到
        //消息时走的 digestForMsgType 同口径。非文本类型的 digest（"[图片]" 等）不含表情
        //标签，净化函数会原样放过，故无需按类型分支。
        std::string normalized;
        if (net::MessageProtocolClient::tryNormalizeInlineFaceText(
                m_conversations.back().m_digest, normalized))
        {
            m_conversations.back().m_digest = normalized;
        }
    }

    // 置顶优先、其余按最后消息时间倒序，与 Messages 树呈现顺序一致。
    balloonmain::SortConversations(m_conversations);

    // 群名由 cmd_get_user_groups（OnUserGroupsArrived）统一提供，这里不再逐群拉
    // 群资料；若群名缓存暂未到位，群会话行先显示占位"群聊(<id>)"，待群列表到达后
    // FillConversationsTree 会被重绘出真实群名。
    FillConversationsTree();

    LOG_INFO("真实会话列表已填充: 会话 %u 条(原始 %u 条)。",
             (uint32_t)m_conversations.size(), (uint32_t)result.m_items.size());
}

void CBalloonMainPanel::OnGroupInfoArrived(const net::GroupInfoResult& info)
{
    if (info.m_groupid <= 0)
    {
        return;
    }
    // 写群信息缓存（群名 / 成员数 / 本人角色 / 群主 id）。本人角色与群主 id 供
    // 群管理入口（群聊窗齿轮、群右键菜单）做门控。
    CString newName = Utf8ToCStr(info.m_name);

    // 仅当群名或成员数变化时才重填树 —— 主动补拉群资料只为拿角色，名字/人数多半
    // 已由群列表给出、未变，重填树会引发连环刷新，故去抖。
    std::map<int64_t, GroupBrief>::iterator cacheIt = m_groupInfo.find(info.m_groupid);
    bool needRefill = (cacheIt == m_groupInfo.end())
        || cacheIt->second.m_name != newName
        || cacheIt->second.m_membercount != info.m_membercount;

    // 群头像变了同样要重填树 —— 群主换了张群头像，本端在这条应答（或随后的
    // 1318 推送）里才知道，不重填的话列表里还挂着旧头像。
    const bool avatarChanged = (cacheIt != m_groupInfo.end())
        && (cacheIt->second.m_facetype != info.m_facetype
            || cacheIt->second.m_customface != info.m_customface);
    needRefill = needRefill || avatarChanged;

    GroupBrief brief;
    brief.m_name        = newName;
    brief.m_membercount = info.m_membercount;
    brief.m_myrole      = info.m_myrole;
    brief.m_ownerid     = info.m_ownerid;
    brief.m_facetype    = info.m_facetype;
    brief.m_customface  = info.m_customface;
    m_groupInfo[info.m_groupid] = brief;

    if (needRefill)
    {
        FillGroupsTree();
        FillConversationsTree();
    }

    // 若该群的群聊窗已打开：刷新标题为真实群名，并把本人角色推给它（决定齿轮显隐）。
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator it =
        m_groupChatWindows.find(info.m_groupid);
    if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
    {
        it->second->SetGroupName(brief.m_name);
        it->second->SetMyRole(info.m_myrole);
        //群头像也在这条应答里，标题栏 / 任务栏图标据它重建（值没变时内部会跳过）。
        it->second->SetGroupAvatar(info.m_facetype, info.m_customface);
        //头部左侧那块的人数与公告摘要同样来自这条应答。公告摘要取的是快照里的
        //标题（announcetitle）而不是正文 —— 那一行只有一行宽，标题本就是为
        //"一句话说清楚"写的。
        it->second->SetHeaderMemberCount(info.m_membercount);
        it->second->SetAnnouncementTitle(Utf8ToCStr(info.m_announcetitle));
    }

    LOG_INFO("群资料已缓存: groupid=%lld, 成员 %d 人。",
             (long long)info.m_groupid, info.m_membercount);

    // 若群资料对话框正为该群打开：暂存公告（GroupBrief 不含公告），并刷新对话框。
    // 群主显示名要等群成员(1304)到达才有；这里先刷新一次让群名 / 人数 / 公告就位。
    if (info.m_groupid == m_groupInfoDlgGroupId)
    {
        m_groupInfoDlgAnnouncement = Utf8ToCStr(info.m_announcement);
        RefreshGroupInfoDialog(info.m_groupid);
    }

    // 若该群的群管理窗正开着：把这条群资料转交给它。socket 层的应答只投给主面板，
    // 不经这一跳，群管理窗「基本信息」页的群名称 / 群号 / 公告 / 简介与「群权限」页
    // 就没有任何数据来源，一直停在构造时的占位内容上。
    CBalloonGroupManageWindow* pManageWin = GetLiveGroupManageWindow(info.m_groupid);
    if (pManageWin != NULL)
    {
        pManageWin->OnGroupInfoArrived(info);
    }
}

void CBalloonMainPanel::OnUserGroupsArrived(const net::UserGroupsResult& result)
{
    // 缓存本人加入的全部群作为"群"Tab 的群源；同时把群名/成员数写进群信息缓存，
    // 供"消息"Tab 群会话行与群聊窗标题显示真实群名。
    const int64_t selfId = (m_lpFMGClient != NULL)
        ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;

    m_myGroups = result.m_groups;
    for (size_t i = 0; i < m_myGroups.size(); ++i)
    {
        const net::UserGroupItem& g = m_myGroups[i];
        GroupBrief brief;
        brief.m_name        = Utf8ToCStr(g.m_name);
        brief.m_membercount = g.m_membercount;
        brief.m_ownerid     = g.m_ownerid;
        brief.m_facetype    = g.m_facetype;
        brief.m_customface  = g.m_customface;
        // 角色：群主可直接由 ownerid 判出；非群主须靠群资料(cmd_get_group_info)
        // 才能区分管理员 / 普通成员，这里先沿用已缓存角色，未知则置 -1。
        if (selfId != 0 && g.m_ownerid == selfId)
        {
            brief.m_myrole = kGroupRoleOwner;
        }
        else
        {
            std::map<int64_t, GroupBrief>::iterator ex = m_groupInfo.find(g.m_groupid);
            brief.m_myrole = (ex != m_groupInfo.end()) ? ex->second.m_myrole : -1;
        }
        m_groupInfo[g.m_groupid] = brief;

        // 群名已由本列表给出；但为拿到"我是否管理员"，对非群主且角色仍未知的群
        // 补拉一次群资料（cmd_get_group_info 会带回 m_myrole）。群主无需再拉。
        if (m_groupInfoRequested.count(g.m_groupid) == 0)
        {
            m_groupInfoRequested.insert(g.m_groupid);
            if (brief.m_myrole < 0)
            {
                CIUSocket::GetInstance().RequestGroupInfo(g.m_groupid);
            }
        }

        // 若该群的群聊窗已打开，顺带把标题与角色刷新（角色决定齿轮显隐）。
        std::map<int64_t, CBalloonGroupChatWindow*>::iterator wit =
            m_groupChatWindows.find(g.m_groupid);
        if (wit != m_groupChatWindows.end() && wit->second != NULL && wit->second->IsWindow())
        {
            wit->second->SetGroupName(brief.m_name);
            wit->second->SetMyRole(brief.m_myrole >= 0 ? brief.m_myrole : kGroupRoleMember);
        }
    }

    FillGroupsTree();
    FillConversationsTree();

    //群列表比通讯录晚到（实测差十几毫秒），收藏分区是在通讯录到达时建的，那会儿
    //m_myGroups 还是空的、群收藏会被当成"查不到资料"跳过。这里补建收藏分区，把群收藏
    //行补出来；只有收藏分区变了，部门树不必重建。没有群收藏就什么都不用做。
    if (m_favorites.HasGroupFavorite() && !RebuildFavoritesSection())
    {
        RebuildContactsTree();
    }

    LOG_INFO("我的群列表已填充: 群 %u 个。", (uint32_t)m_myGroups.size());

    // 群列表已整体替换，安排一次搜索候选缓存的预热（与通讯录到达时的那次会合并成一次）。
    ScheduleSearchWarmUp();
}

void CBalloonMainPanel::OnGroupMembersArrived(const net::GroupMembersResult& members)
{
    // 群主退群前选新群主：成员列表到达后剔除本人作为候选，打开「转让群主并退出」的选人对话框
    if (m_transferPickGroupId != 0 && members.m_groupid == m_transferPickGroupId)
    {
        m_transferPickGroupId = 0;
        if (members.m_code != 0)
        {
            ::MessageBox(m_hWnd, _T("未能获取群成员，请稍后重试。"), _T("FlamingoIM"),
                         MB_OK | MB_ICONWARNING);
        }
        else
        {
            const std::vector<net::ContactItem> candidates =
                balloonmain::BuildTransferCandidates(members.m_members, (int64_t)SelfUserId());
            if (candidates.empty())
            {
                ::MessageBox(m_hWnd, _T("群里没有其他成员，无法转让群主。如需退出，请解散本群。"),
                             _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                const std::vector<net::DepartmentItem> noDepartments;
                m_createGroupDlg.OpenForPick(m_hWnd, CCreateGroupDlg::kPurposeTransferThenLeave,
                                             members.m_groupid, SelfUserId(), candidates, noDepartments);
                LOG_INFO("Owner leave: member picker opened, groupid=%lld, candidates=%u.",
                         (long long)members.m_groupid, (unsigned)candidates.size());
            }
        }
    }

    // 路由到对应群聊窗，填成员侧栏并建"发送人映射"。
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator it =
        m_groupChatWindows.find(members.m_groupid);
    if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
    {
        it->second->OnGroupMembersArrived(members);

        //成员头像角上的在线状态徽标：本人取状态胶囊上的当前状态，其他人取通讯录缓存里的
        //presence；不在通讯录里的成员查不到，传"未知"，不画徽标
        const UINT selfId = SelfUserId();
        for (size_t i = 0; i < members.m_members.size(); ++i)
        {
            const int64_t uid = members.m_members[i].m_userid;
            int presence = balloonchatwnd::kMemberPresenceUnknown;
            if (selfId != 0 && uid == (int64_t)selfId)
            {
                presence = GetPresence();
            }
            else
            {
                const net::ContactItem* pc = FindCachedContact((UINT)uid);
                if (pc != NULL)
                {
                    presence = pc->m_presence;
                }
            }
            it->second->SetMemberPresence(uid, presence);
        }
    }

    // 若群资料对话框正为该群打开：从成员列表挑出群主显示名 + 我的群名片（群名片优先、
    // 空回退账号昵称），暂存后刷新对话框。群主 id 取群信息缓存里的 m_ownerid。
    if (members.m_groupid == m_groupInfoDlgGroupId)
    {
        int64_t ownerId = 0;
        std::map<int64_t, GroupBrief>::iterator bit = m_groupInfo.find(members.m_groupid);
        if (bit != m_groupInfo.end())
        {
            ownerId = bit->second.m_ownerid;
        }
        const int64_t selfId = (m_lpFMGClient != NULL)
            ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;

        m_groupInfoDlgOwnerName = Utf8ToCStr(
            balloonmain::ResolveGroupOwnerName(members.m_members, ownerId));
        m_groupInfoDlgMyCard = Utf8ToCStr(
            balloonmain::ResolveMyGroupCard(members.m_members, selfId));
        RefreshGroupInfoDialog(members.m_groupid);
    }

    // 与群资料同理：群管理窗的「成员管理」「禁言管理」两页都靠这条应答填，
    // 少了这一跳就只能显示占位成员。
    CBalloonGroupManageWindow* pManageWin = GetLiveGroupManageWindow(members.m_groupid);
    if (pManageWin != NULL)
    {
        pManageWin->OnGroupMembersArrived(members);
    }
}

void CBalloonMainPanel::OpenGroupInfo(int64_t groupId)
{
    if (groupId <= 0)
    {
        return;
    }

    // 标记当前对话框所属群，清空上一群残留的"晚到字段"缓存（公告 / 群主名 / 我的群名片）。
    m_groupInfoDlgGroupId = groupId;
    m_groupInfoDlgAnnouncement.Empty();
    m_groupInfoDlgOwnerName.Empty();
    m_groupInfoDlgMyCard.Empty();

    // 用本地缓存组初值：群名 / 成员数取群信息缓存；免打扰 / 置顶取会话缓存。
    // 公告 / 群主显示名 / 我的群名片留空，待 1303 / 1304 应答到达后由 RefreshGroupInfoDialog 补。
    balloonmain::GroupInfoViewData seed;
    seed.groupid = groupId;
    // 本人角色取自群信息缓存；还没拉到时按非群主处理，群资料应答到达后 RefreshGroupInfoDialog 再更新
    seed.isOwner = (MyRoleInGroup(groupId) == kGroupRoleOwner);
    std::map<int64_t, GroupBrief>::iterator bit = m_groupInfo.find(groupId);
    if (bit != m_groupInfo.end())
    {
        seed.groupName   = bit->second.m_name;
        seed.memberCount = bit->second.m_membercount;
    }
    for (size_t i = 0; i < m_conversations.size(); ++i)
    {
        if (m_conversations[i].m_ctype == 2 && m_conversations[i].m_cid == groupId)
        {
            seed.mute   = (m_conversations[i].m_mute != 0);
            seed.pinned = (m_conversations[i].m_pinned != 0);
            break;
        }
    }

    // 打开对话框（pHost 传 this：开关 / 退群回投本主面板更新缓存 / 清理列表）。
    m_groupInfoDlg.OpenForGroup(m_hWnd, m_lpFMGClient, this, seed);

    // 补拉群资料（公告 / 群主 id / 成员数）+ 群成员（群主显示名 / 我的群名片）。
    CIUSocket::GetInstance().RequestGroupInfo(groupId);
    CIUSocket::GetInstance().RequestGroupMembers(groupId);

    LOG_INFO("打开群资料对话框: groupid=%lld。", (long long)groupId);
}

void CBalloonMainPanel::RefreshGroupInfoDialog(int64_t groupId)
{
    // 仅当对话框正为该群打开时才刷新。
    if (groupId <= 0 || groupId != m_groupInfoDlgGroupId)
    {
        return;
    }
    if (!m_groupInfoDlg.IsWindow() || m_groupInfoDlg.GroupId() != groupId)
    {
        return;
    }

    balloonmain::GroupInfoViewData data;
    data.groupid = groupId;
    data.isOwner = (MyRoleInGroup(groupId) == kGroupRoleOwner);

    std::map<int64_t, GroupBrief>::iterator bit = m_groupInfo.find(groupId);
    if (bit != m_groupInfo.end())
    {
        data.groupName   = bit->second.m_name;
        data.memberCount = bit->second.m_membercount;
    }
    for (size_t i = 0; i < m_conversations.size(); ++i)
    {
        if (m_conversations[i].m_ctype == 2 && m_conversations[i].m_cid == groupId)
        {
            data.mute   = (m_conversations[i].m_mute != 0);
            data.pinned = (m_conversations[i].m_pinned != 0);
            break;
        }
    }
    // 晚到字段（已由 1303 / 1304 应答暂存）。
    data.announcement = m_groupInfoDlgAnnouncement;
    data.ownerName    = m_groupInfoDlgOwnerName;
    data.myCard       = m_groupInfoDlgMyCard;

    m_groupInfoDlg.ApplyData(data);
}

void CBalloonMainPanel::SetGroupConversation(int64_t groupId, bool hasMute, bool mute,
                                             bool hasPinned, bool pinned)
{
    if (groupId <= 0)
    {
        return;
    }

    // 乐观更新会话缓存：找该群会话项更新 mute / pinned（置顶变化会由
    // SetConversationPinned 顺带重排）。无消息空群不在会话列表里，两个 Set 都会返回
    // false（找不到则不强建），置顶 / 免打扰仍发服务端，下次会话同步会带回。
    bool changed = false;
    if (hasMute && balloonmain::SetConversationMuted(m_conversations, 2, groupId, mute))
    {
        changed = true;
    }
    if (hasPinned && balloonmain::SetConversationPinned(m_conversations, 2, groupId, pinned))
    {
        changed = true;
    }
    if (changed)
    {
        // 重填会话树，让"消息"Tab 即时反映新的顺序与行标记。
        FillConversationsTree();
    }

    // 发协议（应答经 cmd_set_conversation 回投本面板记日志 / 失败提示）。
    CIUSocket::GetInstance().SendSetConversation(2, groupId, hasPinned, pinned, hasMute, mute);
    LOG_INFO("会话设置(群): groupid=%lld, hasMute=%d/%d, hasPinned=%d/%d。",
             (long long)groupId, (int)hasMute, (int)mute, (int)hasPinned, (int)pinned);
}

bool CBalloonMainPanel::RequestLeaveGroup(int64_t groupId, HWND hDlgOwner)
{
    if (groupId <= 0)
    {
        return false;
    }
    const HWND hOwner = ::IsWindow(hDlgOwner) ? hDlgOwner : m_hWnd;
    const std::wstring groupName = GroupNameForNotice(groupId);

    if (m_lpFMGClient != NULL && m_lpFMGClient->IsOffline())
    {
        ::MessageBox(hOwner, _T("您当前处于离线状态，无法退出群聊，请上线后重试。"),
                     _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
        return false;
    }

    // 群主不能直接退群（服务端会拒绝）：先选新群主，转让成功后再退群
    if (balloonmain::DecideLeavePath(MyRoleInGroup(groupId)) == balloonmain::kLeavePathTransferFirst)
    {
        const std::wstring tip = balloonmain::BuildOwnerLeaveConfirmText(groupName);
        if (::MessageBox(hOwner, tip.c_str(), _T("退出群聊"),
                         MB_YESNO | MB_ICONQUESTION) != IDYES)
        {
            return false;
        }
        StartOwnerTransferPick(groupId);
        return true;
    }

    const std::wstring tip = balloonmain::BuildLeaveConfirmText(groupName);
    if (::MessageBox(hOwner, tip.c_str(), _T("退出群聊"),
                     MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
    {
        return false;
    }
    SendLeaveGroupTracked(groupId, groupName);
    return true;
}

bool CBalloonMainPanel::RequestDismissGroup(int64_t groupId, HWND hDlgOwner)
{
    if (groupId <= 0)
    {
        return false;
    }
    const HWND hOwner = ::IsWindow(hDlgOwner) ? hDlgOwner : m_hWnd;
    const std::wstring groupName = GroupNameForNotice(groupId);

    if (m_lpFMGClient != NULL && m_lpFMGClient->IsOffline())
    {
        ::MessageBox(hOwner, _T("您当前处于离线状态，无法解散群聊，请上线后重试。"),
                     _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
        return false;
    }

    const std::wstring tip = balloonmain::BuildDismissConfirmText(groupName);
    if (::MessageBox(hOwner, tip.c_str(), _T("解散群聊"),
                     MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
    {
        return false;
    }

    CIUSocket::GetInstance().SendDismissGroup(groupId);
    balloonmain::PendingGroupOp pending;
    pending.m_groupId = groupId;
    pending.m_groupName = groupName;
    m_groupExitTracker.Begin(balloonmain::kGroupExitOpDismiss, pending);
    LOG_INFO("Dismiss group request sent: groupid=%lld.", (long long)groupId);
    return true;
}

void CBalloonMainPanel::SendLeaveGroupTracked(int64_t groupId, const std::wstring& groupName)
{
    CIUSocket::GetInstance().SendLeaveGroup(groupId);
    balloonmain::PendingGroupOp pending;
    pending.m_groupId = groupId;
    pending.m_groupName = groupName;
    m_groupExitTracker.Begin(balloonmain::kGroupExitOpLeave, pending);
    LOG_INFO("Leave group request sent: groupid=%lld.", (long long)groupId);
}

void CBalloonMainPanel::StartOwnerTransferPick(int64_t groupId)
{
    // 主面板平时不缓存群成员名单，现拉一次；应答到达后在 OnGroupMembersArrived 里打开选人对话框
    m_transferPickGroupId = groupId;
    CIUSocket::GetInstance().RequestGroupMembers(groupId);
    LOG_INFO("Owner leave: fetching members to pick a new owner, groupid=%lld.", (long long)groupId);
}

void CBalloonMainPanel::ApplyGroupExitOutcome(balloonmain::GroupExitOp op,
                                              const balloonmain::GroupOpOutcome& outcome)
{
    const int64_t groupId = outcome.m_op.m_groupId;
    if (!outcome.m_succeeded)
    {
        LOG_ERROR("Group exit op failed: op=%d, groupid=%lld.", (int)op, (long long)groupId);
        ::MessageBox(m_hWnd, outcome.m_errorText.c_str(), _T("FlamingoIM"), MB_OK | MB_ICONWARNING);
        return;
    }

    switch (op)
    {
    //退群成功：提示并清理本地的群数据（随后到达的成员变动推送会发现已经清理过，不再重复提示）
    case balloonmain::kGroupExitOpLeave:
        HandleLeftGroup(groupId, balloonmain::kLeaveReasonSelf, outcome.m_op.m_groupName, true);
        break;

    //解散成功：同上，提示文字是"已解散"
    case balloonmain::kGroupExitOpDismiss:
        HandleLeftGroup(groupId, balloonmain::kLeaveReasonDismissedBySelf, outcome.m_op.m_groupName, true);
        break;

    //转让成功：本人角色的缓存由随后到达的角色变更推送刷新；"转让后退出"的流程接着发退群
    case balloonmain::kGroupExitOpTransfer:
        if (outcome.m_sendLeaveNext)
        {
            SendLeaveGroupTracked(groupId, outcome.m_op.m_groupName);
        }
        else
        {
            ShowPanelToast(balloonmain::BuildTransferDoneText(outcome.m_op.m_newOwnerName));
        }
        break;

    //未知操作：不会走到这里
    default:
        break;
    }
    LOG_INFO("Group exit op succeeded: op=%d, groupid=%lld, sendLeaveNext=%d.",
             (int)op, (long long)groupId, (int)outcome.m_sendLeaveNext);
}

void CBalloonMainPanel::HandleLeftGroup(int64_t groupId, balloonmain::GroupLeaveReason reason,
                                        const std::wstring& groupName, bool showNotice)
{
    if (groupId <= 0)
    {
        return;
    }

    // 已经清理过就不再提示：本端发起的退群 / 解散，应答先到、随后的推送又到一次
    const bool known = IsGroupKnownLocally(groupId);
    if (known && showNotice)
    {
        const std::wstring name = groupName.empty() ? GroupNameForNotice(groupId) : groupName;
        const std::wstring text = balloonmain::BuildLeaveNoticeText(reason, name);
        const bool passive = (reason == balloonmain::kLeaveReasonDismissed
                              || reason == balloonmain::kLeaveReasonKicked);
        std::map<int64_t, CBalloonGroupChatWindow*>::iterator wit = m_groupChatWindows.find(groupId);
        const bool chatOpen = (wit != m_groupChatWindows.end() && wit->second != NULL
                               && wit->second->IsWindow());
        if (passive && chatOpen)
        {
            // 聊天窗随后会被关掉：先在它上面说明原因，输入框里没发出的内容不会无声消失
            ::MessageBox(wit->second->m_hWnd, text.c_str(), _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
        }
        else
        {
            ShowPanelToast(text);
        }
    }

    CleanupLeftGroup(groupId);
    LOG_INFO("Left group handled: groupid=%lld, reason=%d, wasKnown=%d, notice=%d.",
             (long long)groupId, (int)reason, (int)known, (int)showNotice);
}

bool CBalloonMainPanel::IsGroupKnownLocally(int64_t groupId) const
{
    for (size_t i = 0; i < m_myGroups.size(); ++i)
    {
        if (m_myGroups[i].m_groupid == groupId)
        {
            return true;
        }
    }
    if (m_groupInfo.find(groupId) != m_groupInfo.end())
    {
        return true;
    }
    return balloonmain::FindConversation(m_conversations, 2, groupId) != NULL;
}

std::wstring CBalloonMainPanel::GroupNameForNotice(int64_t groupId) const
{
    std::map<int64_t, GroupBrief>::const_iterator it = m_groupInfo.find(groupId);
    if (it != m_groupInfo.end() && !it->second.m_name.IsEmpty())
    {
        return std::wstring((LPCTSTR)it->second.m_name);
    }
    return std::wstring();
}

void CBalloonMainPanel::ShowPanelToast(const std::wstring& text)
{
    if (m_pPanelToast == NULL)
    {
        LOG_WARNING("Panel toast not available, notice dropped.");
        return;
    }
    m_pPanelToast->Show(text.c_str());
}

void CBalloonMainPanel::CleanupLeftGroup(int64_t groupId)
{
    if (groupId <= 0)
    {
        return;
    }

    // 从"我的群"列表移除
    for (std::vector<net::UserGroupItem>::iterator git = m_myGroups.begin();
         git != m_myGroups.end(); )
    {
        if (git->m_groupid == groupId)
        {
            git = m_myGroups.erase(git);
        }
        else
        {
            ++git;
        }
    }
    m_groupInfo.erase(groupId);
    m_groupInfoRequested.erase(groupId);

    // 从会话列表移除该群的会话
    for (std::vector<net::ConversationItem>::iterator cit = m_conversations.begin();
         cit != m_conversations.end(); )
    {
        if (cit->m_ctype == 2 && cit->m_cid == groupId)
        {
            cit = m_conversations.erase(cit);
        }
        else
        {
            ++cit;
        }
    }

    FillGroupsTree();
    FillConversationsTree();

    // 托盘上属于该群的未读提示也要跟着清 —— 会话行都没了，闪烁还留着会让人
    // 点开托盘却找不到是哪来的消息。
    PostTrayClear(2, groupId);

    // 关闭该群已打开的群聊窗（若有）。
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator wit =
        m_groupChatWindows.find(groupId);
    if (wit != m_groupChatWindows.end() && wit->second != NULL && wit->second->IsWindow())
    {
        wit->second->DestroyWindow();
    }

    // 收藏：服务端已删掉各成员对该群的收藏行，本地的收藏标记也去掉并重建收藏分区，
    // 否则通讯录页的"我的收藏"里会留着一行，点开就是一个已经不在的群
    if (m_favorites.IsFavorite(conversation_group, groupId))
    {
        m_favorites.SetFavorite(conversation_group, groupId, false);
        if (!RebuildFavoritesSection())
        {
            RebuildContactsTree();
        }
    }

    // 本地消息记录：之后已经没有入口查看这个群的记录，留着只会让全局搜索搜出一个打不开的群；
    // 日后重新入群时再从服务端同步
    if (EnsureMsgStoreOpen())
    {
        int deleted = 0;
        const bool ok = m_msgStore.DeleteConversation(conversation_group, groupId, deleted);
        LOG_INFO("Local messages of left group deleted: groupid=%lld, rows=%d, ok=%d.",
                 (long long)groupId, deleted, (int)ok);
    }

    // 消息记录窗口正显示这个群：它的数据刚删掉，关掉
    if (m_historyWindow.IsShowingConversation(conversation_group, groupId))
    {
        m_historyWindow.DestroyWindow();
    }

    // 群资料窗口、群管理窗口正为这个群打开（被移出或群被解散时它们还开着）：一并关掉。
    // 群管理窗口对象留给下次打开时回收，与 OpenGroupManage 的处理一致
    if (m_groupInfoDlg.IsWindow() && m_groupInfoDlg.GroupId() == groupId)
    {
        m_groupInfoDlg.DestroyWindow();
    }
    if (m_groupInfoDlgGroupId == groupId)
    {
        m_groupInfoDlgGroupId = 0;
    }
    CBalloonGroupManageWindow* pManageWin = GetLiveGroupManageWindow(groupId);
    if (pManageWin != NULL)
    {
        pManageWin->DestroyWindow();
    }
}

void CBalloonMainPanel::OnGroupCardSaveResult(int code, const CString& msg)
{
    // 转交群资料对话框(失败回退我的群名片);对话框未在等应答时内部自行忽略。
    m_groupInfoDlg.OnCardSaveResult(code, msg);
}

void CBalloonMainPanel::OnGroupFilesArrived(const net::GetGroupFilesResult& result)
{
    // groupid 在应答里是字符串编码的 int64，转回数值键查打开的群聊窗。
    int64_t groupId = (int64_t)atoll(result.m_groupid.c_str());
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator it =
        m_groupChatWindows.find(groupId);
    if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
    {
        it->second->OnGroupFilesArrived(result);
    }
}

void CBalloonMainPanel::OnGroupFilesChanged(const net::GroupFilesChangedNotify& notify)
{
    int64_t groupId = (int64_t)atoll(notify.m_groupid.c_str());
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator it =
        m_groupChatWindows.find(groupId);
    if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
    {
        it->second->OnGroupFilesChanged(notify);
    }
}

void CBalloonMainPanel::OnGroupFileUploadResult(const net::GroupFileUploadResult& result)
{
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator it =
        m_groupChatWindows.find(result.m_groupid);
    if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
    {
        it->second->OnGroupFileUploaded(result);
    }
}

void CBalloonMainPanel::OnGroupFileDownloadResult(const net::GroupFileDownloadResult& result)
{
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator it =
        m_groupChatWindows.find(result.m_groupid);
    if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
    {
        it->second->OnGroupFileDownloaded(result);
    }
}

void CBalloonMainPanel::OnGroupFileUploadProgress(const net::GroupFileUploadProgress& prog)
{
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator it =
        m_groupChatWindows.find(prog.m_groupid);
    if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
    {
        it->second->OnGroupFileUploadProgress(prog);
    }
}

void CBalloonMainPanel::OnGroupFileDownloadProgress(const net::GroupFileDownloadProgress& prog)
{
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator it =
        m_groupChatWindows.find(prog.m_groupid);
    if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
    {
        it->second->OnGroupFileDownloadProgress(prog);
    }
}

void CBalloonMainPanel::OnMessagePinned(const net::MessagePinnedNotify& notify)
{
    const bool bPinned = (notify.m_pin == 1);
    LOG_INFO("v2 message pinned notify: ctype=%d, cid=%lld, msgid=%lld, pin=%d, operator=%lld.",
             notify.m_ctype, (long long)notify.m_cid,
             (long long)notify.m_msgid, notify.m_pin, (long long)notify.m_operator);

    if (!EnsureMsgStoreOpen())
    {
        LOG_ERROR(_T("处理 Pin 推送失败：本地消息库未就绪。"));
        return;
    }

    //一、把本地消息库该条的 Pin 标记落上 / 清掉。Pin 分类查的就是这个标记。
    bool bHit = false;
    m_msgStore.SetPinned(notify.m_ctype, notify.m_cid, notify.m_msgid,
                         bPinned, notify.m_pinTime, bHit);

    //二、把已经打开着的聊天窗里对应那条气泡的图钉标记置上 / 清掉。窗口没开、或该条
    //   不在当前视图里都无需处理 —— 下次翻历史时本地库里已带着标记。
    //   顺带把窗口顶部「已 Pin N 条」提示带的计数就地 ±1 —— 只为刷新一个数字再向
    //   服务端拉一趟全集不值当；这份计数是乐观的，每次重开聊天窗都会用服务端全集校准。
    const int kPinCountDelta = bPinned ? 1 : -1;
    if (notify.m_ctype == conversation_group)
    {
        std::map<int64_t, CBalloonGroupChatWindow*>::iterator it =
            m_groupChatWindows.find(notify.m_cid);
        if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            it->second->OnMessagePinned(notify.m_msgid, bPinned);
            it->second->AdjustPinnedCount(kPinCountDelta);
        }
    }
    else
    {
        std::map<UINT, CBalloonChatWindow*>::iterator it =
            m_chatWindows.find((UINT)notify.m_cid);
        if (it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            it->second->OnMessagePinned(notify.m_msgid, bPinned);
            it->second->AdjustPinnedCount(kPinCountDelta);
        }
    }

    //三、若消息记录窗正显示本会话，刷新它，让 Pin 分类即时反映这次变化。
    m_historyWindow.RefreshIfConversation(notify.m_ctype, notify.m_cid);

    //四、本地压根没有这条消息时补拉一次。这不是错误 —— 本机没同步过那段历史属正常，
    //    但"钉了却在 Pin 分类里找不到"从用户角度就是功能坏了，故这里主动把消息体要回来。
    //    不会形成循环：补拉由<u>一次推送</u>触发，补齐之后本地已有该行、不会再落空；
    //    即便服务端也返回不了那条（消息刚被删等异常），也只是多花一个来回。
    //    取消 Pin 的推送不必补拉 —— 本地没有的消息，取消它本来就没有任何可做的事。
    if (!bHit && bPinned)
    {
        LOG_INFO("v2 pin notify missed local row, request pinned messages: ctype=%d, cid=%lld, msgid=%lld.",
                 notify.m_ctype, (long long)notify.m_cid, (long long)notify.m_msgid);
        CIUSocket::GetInstance().GetPinnedMessages(notify.m_ctype, notify.m_cid);
    }

    //会话列表预览不动 —— Pin 不改变"最后一条消息是什么"。
}

void CBalloonMainPanel::ChatWindowApplyPinnedSet(const net::PinnedMessagesResult& result)
{
    //本会话的聊天窗没开就什么都不用做 —— 下次开窗时会重新拉一遍。
    if (result.m_ctype == conversation_group)
    {
        std::map<int64_t, CBalloonGroupChatWindow*>::iterator it =
            m_groupChatWindows.find(result.m_cid);
        if (it == m_groupChatWindows.end() || it->second == NULL || !it->second->IsWindow())
        {
            return;
        }
        for (size_t i = 0; i < result.m_items.size(); ++i)
        {
            it->second->OnMessagePinned(result.m_items[i].m_msg.m_msgid, true);
        }
        //顶部提示带的条数以服务端的 total 为准：本次返回可能被单次上限截断，
        //拿 m_items.size() 当条数会在 Pin 很多的会话里少报。
        it->second->SetPinnedCount((int)result.m_total);
        return;
    }

    std::map<UINT, CBalloonChatWindow*>::iterator it =
        m_chatWindows.find((UINT)result.m_cid);
    if (it == m_chatWindows.end() || it->second == NULL || !it->second->IsWindow())
    {
        return;
    }
    for (size_t i = 0; i < result.m_items.size(); ++i)
    {
        it->second->OnMessagePinned(result.m_items[i].m_msg.m_msgid, true);
    }
    it->second->SetPinnedCount((int)result.m_total);   //理由同上，以服务端 total 为准
}

void CBalloonMainPanel::OnPinnedMessagesArrived(const net::PinnedMessagesResult& result)
{
    if (result.m_code != 0)
    {
        LOG_ERROR("v2 get pinned messages failed, code=%d, ctype=%d, cid=%lld.",
                  result.m_code, result.m_ctype, (long long)result.m_cid);
        return;
    }

    if (!EnsureMsgStoreOpen())
    {
        LOG_ERROR(_T("处理被 Pin 消息全集失败：本地消息库未就绪。"));
        return;
    }

    //一、把返回的完整消息体落库，补齐本地缺的那些行。口径与 OnSyncMessagesArrived
    //    完全一致：用应答里的 ctype/cid 作归属键，不依赖每条 notify 自身的 cid。
    //    同样整批放进一个事务写入，理由见 OnSyncMessagesArrived。
    std::vector<balloonhistory::PinnedItem> items;
    std::vector<balloonhistory::HistoryRecord> recs;
    items.reserve(result.m_items.size());
    recs.reserve(result.m_items.size());
    for (size_t i = 0; i < result.m_items.size(); ++i)
    {
        const net::MessageNotify& m = result.m_items[i].m_msg;
        if (m.m_msgid <= 0)
        {
            continue;
        }
        recs.push_back(balloonhistory::RecordFromNotify(m, result.m_ctype, result.m_cid));

        balloonhistory::PinnedItem item;
        item.m_msgid = m.m_msgid;
        item.m_pinTime = result.m_items[i].m_pinTime;
        items.push_back(item);
    }
    if (!m_msgStore.UpsertBatch(recs))
    {
        LOG_ERROR("local message store: batch upsert of pinned messages failed, ctype=%d, "
                  "cid=%lld, count=%u.", result.m_ctype, (long long)result.m_cid,
                  (uint32_t)recs.size());
    }

    //二、与服务端对账，把本地多出来的 Pin 标记清掉（在别的设备上取消的、或推送因离线
    //    丢掉的）。清零的时间下界要跟着"本次有没有被截断"走：
    //      · 未截断（total 等于返回条数）：传 0，全量清零后按返回集合置位；
    //      · 已截断：服务端单次最多返回最近 PIN_FETCH_LIMIT 条，那些更老的 Pin 并不在
    //        本次返回里，全量清零会把它们<u>错杀</u>。此时取返回集合里最老一条消息的
    //        时间戳作下界，只清这个时间点之后的。
    //    这一段只有在同一会话 Pin 超过单次上限时才会走到，日常测不出来，改动时务必对照
    //    docs/消息Pin接入方案.md §4.3 与 V2S20 那条用例。
    int64_t minTimestampMs = 0;
    if (result.m_total > (int64_t)result.m_items.size())
    {
        //返回集合按消息时间倒序，最老的一条在末尾；保险起见仍取一遍最小值，
        //免得日后服务端改了排序、这里悄无声息地算错下界。
        for (size_t i = 0; i < result.m_items.size(); ++i)
        {
            const int64_t ts = result.m_items[i].m_msg.m_timestamp;
            if (ts <= 0)
            {
                continue;
            }
            if (minTimestampMs == 0 || ts < minTimestampMs)
            {
                minTimestampMs = ts;
            }
        }
        LOG_INFO("v2 pinned messages truncated, reconcile from timestamp %lld: ctype=%d, cid=%lld, returned=%d, total=%lld.",
                 (long long)minTimestampMs, result.m_ctype, (long long)result.m_cid,
                 (int)result.m_items.size(), (long long)result.m_total);
    }
    m_msgStore.ReplacePinnedSet(result.m_ctype, result.m_cid, items, minTimestampMs);

    //三、把标记打到已经打开着的聊天窗里对应的气泡上。聊天窗的历史是从网络渲染的
    //   （net::MessageNotify 里没有 Pin 信息），不走这一步的话，刚开窗时那些被钉住的
    //   老消息看不到图钉，非要等下一次推送才亮起来。
    ChatWindowApplyPinnedSet(result);

    //四、消息记录窗若正显示本会话，刷新它。
    m_historyWindow.RefreshIfConversation(result.m_ctype, result.m_cid);

    LOG_INFO("v2 pinned messages applied: ctype=%d, cid=%lld, applied=%d, total=%lld.",
             result.m_ctype, (long long)result.m_cid, (int)items.size(),
             (long long)result.m_total);
}

// ====================================================================
// 平台公告（Banner 上的「消息盒子」）
//
// 收到的公告逐条落本地库 announce.db（按账号隔离），于是离线也能回看、重新登录只需
// 增量拉取、未读数直接从库里数。铃铛角标与消息盒子窗口在 S5/S6 补。
// 详见 docs/消息盒子接入方案.md。
// ====================================================================

bool CBalloonMainPanel::EnsureAnnounceStoreOpen()
{
    if (m_lpFMGClient == NULL)
    {
        return false;
    }

    tstring personal = m_lpFMGClient->m_UserMgr.GetPersonalFolder();
    if (personal.empty())
    {
        return false;
    }

    //与历史消息库同目录（其父级“个人目录”由登录流程已建好）；已存在则忽略失败。
    tstring chatLogDir = personal + _T("ChatLog");
    ::CreateDirectoryW(chatLogDir.c_str(), NULL);

    std::wstring dbPath = personal + _T("ChatLog\\announce.db");
    //Open 本身幂等：同一路径重复调直接返回 true，路径变了（账号切换）会先关旧库再开新库，
    //故这里不必自己记住已开路径。
    if (!balloonhistory::CAnnounceStore::GetInstance().Open(dbPath.c_str()))
    {
        LOG_ERROR(_T("打开平台公告本地库失败: %s"), dbPath.c_str());
        return false;
    }
    return true;
}

void CBalloonMainPanel::RefreshAnnounceBadge()
{
    balloonmain::MainPanelHeader* header = FindMainPanelHeader(GetClientContent());
    if (header == NULL)
    {
        //InitUi 之前（窗口还没建好）会走到这里，静默返回即可 —— 建好之后
        //拉取应答一到就会再刷一次。
        return;
    }

    //未读数从本地公告库现数，不在内存里另存一份计数：公告的增删改都经过那个库，
    //从它数出来的值不会与实际显示的列表对不上。
    int unread = 0;
    if (EnsureAnnounceStoreOpen())
    {
        unread = balloonhistory::CAnnounceStore::GetInstance().GetUnreadCount();
    }

    header->SetAnnounceUnread(unread);
    header->Invalidate();
}

void CBalloonMainPanel::OpenAnnounceBox()
{
    //本地库没就绪就开不出内容来（会是个空窗），提示一下比开个空窗更有用。
    if (!EnsureAnnounceStoreOpen())
    {
        LOG_ERROR(_T("打开消息盒子失败：本地公告库未就绪。"));
        ::MessageBox(m_hWnd, _T("公告数据暂时不可用，请稍后再试。"), _T("FlamingoIM"),
                     MB_OK | MB_ICONWARNING);
        return;
    }

    //窗口自己会从本地库查数据；已开着则前置并刷新一遍。
    m_announceBoxWindow.OpenBox(this);
}

void CBalloonMainPanel::RequestAnnouncements()
{
    //本地已经有的那些不必再拉一遍：用本地最大公告ID作增量游标。本地库打不开时
    //GetMaxId 返回 0，正好退化成全量拉取——只是多传些数据，功能不受影响。
    int64_t sinceId = 0;
    if (EnsureAnnounceStoreOpen())
    {
        sinceId = balloonhistory::CAnnounceStore::GetInstance().GetMaxId();
    }

    //limit 传 0 表示采用服务端默认条数。
    CIUSocket::GetInstance().SendGetAnnouncements(sinceId, 0);
    LOG_INFO("Request announcements, sinceid=%lld.", (long long)sinceId);
}

void CBalloonMainPanel::OnAnnouncementsArrived(const net::AnnouncementsResult& result)
{
    if (result.m_code != 0)
    {
        LOG_ERROR("v2 get announcements failed, code=%d, msg=%s.",
                  result.m_code, result.m_msg.c_str());
        return;
    }

    if (!EnsureAnnounceStoreOpen())
    {
        LOG_ERROR(_T("处理平台公告列表失败：本地公告库未就绪。"));
        return;
    }

    balloonhistory::CAnnounceStore& store = balloonhistory::CAnnounceStore::GetInstance();

    //逐条落库。Upsert 冲突时只更新内容类字段，不会把本地已读 / 已撤回的标记冲掉。
    int64_t maxIdThisBatch = 0;
    for (size_t i = 0; i < result.m_items.size(); ++i)
    {
        const net::AnnouncementItem& item = result.m_items[i];

        balloonhistory::AnnounceRecord rec;
        rec.m_announcementId = item.m_announcementId;
        rec.m_title          = item.m_title;
        rec.m_body           = item.m_body;
        rec.m_severity       = item.m_severity;
        rec.m_senderName     = item.m_senderName;
        rec.m_pinned         = item.m_pinned;
        rec.m_sentTimeMs     = item.m_sentTimeMs;
        rec.m_read           = item.m_read;
        //服务端只下发还在有效期内的公告，撤回的它压根不返回，故这里一律记为未撤回。
        rec.m_recalled       = 0;
        store.Upsert(rec);

        if (item.m_announcementId > maxIdThisBatch)
        {
            maxIdThisBatch = item.m_announcementId;
        }
    }

    LOG_INFO("v2 announcements stored, count=%d, hasmore=%d, unread=%d.",
             (int)result.m_items.size(), (int)result.m_hasMore, store.GetUnreadCount());

    //落完库把 Banner 上的角标刷出来；消息盒子开着的话一并刷新它的列表。
    RefreshAnnounceBadge();
    m_announceBoxWindow.RefreshIfOpen();

    //还有更多：以本批最大的公告ID作新游标接着拉。
    //
    //这里刻意用<u>本批</u>的最大ID而不是 store.GetMaxId()：两者通常相同，但万一本地库
    //里已经存着更大的ID（比如推送先于拉取到达），拿库里的最大值当游标会把本批与那个ID
    //之间的公告整段跳过。用本批最大值则一定是逐段推进的。
    //
    //另外，本批一条都没有时不再续拉——服务端说还有更多、却一条都不给，再拉也是同样的
    //结果，继续下去就是死循环。
    if (result.m_hasMore && maxIdThisBatch > 0)
    {
        CIUSocket::GetInstance().SendGetAnnouncements(maxIdThisBatch, 0);
        LOG_INFO("Request more announcements, sinceid=%lld.", (long long)maxIdThisBatch);
    }
}

void CBalloonMainPanel::OnAnnouncementNotify(const net::AnnouncementNotify& notify)
{
    const int64_t annId = notify.m_item.m_announcementId;

    if (!EnsureAnnounceStoreOpen())
    {
        LOG_ERROR(_T("处理平台公告推送失败：本地公告库未就绪。"));
        return;
    }

    balloonhistory::CAnnounceStore& store = balloonhistory::CAnnounceStore::GetInstance();

    if (notify.m_changeType == announcement_change_recalled)
    {
        //撤回推送只带公告ID（服务端不重复传正文），故这里不要去读 m_item 的其余字段。
        //本地那一行仍留着、只打撤回标记：消息盒子不再显示它，但用户若正开着这一条，
        //窗口能据此给出「该公告已被撤回」而不是让它凭空消失。
        store.MarkRecalled(annId);
        LOG_INFO("v2 announcement recalled, id=%lld, unread=%d.",
                 (long long)annId, store.GetUnreadCount());
        //撤回的若是一条未读公告，角标要跟着减一 —— 否则会挂着一个点开也找不到的数字。
        RefreshAnnounceBadge();
        //消息盒子开着的话让它把这条移出列表；用户正看着这一条时右栏会回到占位提示。
        m_announceBoxWindow.RefreshIfOpen();
        return;
    }

    //新发布：整条落库。收到推送时本人必然还没读过，read 取服务端下发值（恒为 0）。
    balloonhistory::AnnounceRecord rec;
    rec.m_announcementId = annId;
    rec.m_title          = notify.m_item.m_title;
    rec.m_body           = notify.m_item.m_body;
    rec.m_severity       = notify.m_item.m_severity;
    rec.m_senderName     = notify.m_item.m_senderName;
    rec.m_pinned         = notify.m_item.m_pinned;
    rec.m_sentTimeMs     = notify.m_item.m_sentTimeMs;
    rec.m_read           = notify.m_item.m_read;
    rec.m_recalled       = 0;
    store.Upsert(rec);

    LOG_INFO("v2 announcement published, id=%lld, severity=%d, unread=%d.",
             (long long)annId, notify.m_item.m_severity, store.GetUnreadCount());

    //新公告到达：角标亮起来，Banner 那行文字变成「你有 N 条新公告」。
    RefreshAnnounceBadge();
    //消息盒子开着的话让它把新公告插进列表，不用关掉重开。
    m_announceBoxWindow.RefreshIfOpen();

    //在桌面右下角弹一次提示 —— 用户不盯着主面板也能第一时间看到。
    //消息盒子正开着且正显示这一条时就不弹了：用户已经在看它，再弹一个反而碍事。
    ShowAnnouncePopup(notify.m_item);
}

void CBalloonMainPanel::ShowAnnouncePopup(const net::AnnouncementItem& item)
{
    //「时间」按与消息盒子列表同一口径格式化，两处看到的时间才是一致的。
    CString timeText = balloonannounce::FormatAnnounceTime(item.m_sentTimeMs);

    m_announcePopup.ShowAnnouncement(m_hWnd,
                                     item.m_announcementId,
                                     Utf8ToCStr(item.m_title),
                                     Utf8ToCStr(item.m_body),
                                     item.m_severity,
                                     Utf8ToCStr(item.m_senderName),
                                     timeText);
}

LRESULT CBalloonMainPanel::OnOpenAnnounceBoxMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    //新公告提示窗点了「查看全部」。
    OpenAnnounceBox();
    return 0;
}

void CBalloonMainPanel::OnAnnounceMarkReadResult(const net::AnnounceMarkReadResult& result)
{
    //成功时什么都不做：已读在发起上报时就乐观写进了本地库、角标也已经减掉。
    if (result.m_code != 0)
    {
        LOG_ERROR("v2 mark announcement read failed, code=%d, msg=%s, id=%lld.",
                  result.m_code, result.m_msg.c_str(),
                  (long long)result.m_announcementId);
    }
}

void CBalloonMainPanel::OnSyncMessagesArrived(const net::SyncMessagesResult& result)
{
    // 这一页是不是消息记录窗为"往前拉更早的消息"发出的。是的话只落库、不转给聊天窗：
    // 聊天窗只靠自己的翻页标志区分首屏与翻页，一页更早的消息转过去会被当成首屏，把聊天窗
    // 清空、换成一页旧消息（见 docs/清理聊天记录方案.md §9.2）。必须在函数开头判：下面刷新
    // 消息记录窗时它会结束这一轮或发出下一轮，锚点随之变化，之后再问就认不出了。
    const bool historyOwnFetch = result.m_hasAnchor
        && m_historyWindow.IsOwnOlderFetch(result.m_ctype, result.m_cid, result.m_beforeMsgid);

    BUI_TRACE("MAINPANEL-SYNC-ARRIVED ctype=%d cid=%lld count=%u",
              result.m_ctype, (long long)result.m_cid, (unsigned)result.m_messages.size());

    //本页各项处理的具体动作。先后顺序由 balloonmain::RunSyncArrival 决定：先交给聊天窗并让它
    //立即画出来，再写本地消息库 —— 写库一页 50 条约 20 毫秒，放在前面的话聊天窗要等它写完
    //才显示内容；聊天窗的渲染不读本地库，调换顺序不影响显示结果。
    //定义成函数内的局部类：它只服务于本函数，且作为成员函数的局部类可以直接访问本类的私有成员。
    class Steps : public balloonmain::ISyncArrivalSteps
    {
    public:
        Steps(CBalloonMainPanel& panel, const net::SyncMessagesResult& result)
            : m_panel(panel)
            , m_result(result)
        {
        }

        //本页所属会话的聊天窗是否仍打开着。历史只渲染进"仍打开着"的那个窗口（请求历史的就是它）：
        //用户在应答到达前已关窗就丢弃，不能用 Ensure* 把已关的窗重新弹开
        virtual bool HasOpenChatWindow()
        {
            return ChatWindow() != NULL;
        }

        virtual void RenderToChatWindow()
        {
            if (m_result.m_ctype == 2)
            {
                CBalloonGroupChatWindow* pGroup = GroupWindow();
                if (pGroup != NULL)
                    pGroup->OnHistoryMessages(m_result);
            }
            else
            {
                CBalloonChatWindow* pChat = SingleWindow();
                if (pChat != NULL)
                    pChat->OnHistoryMessages(m_result);
            }
        }

        //单聊应答带回对方的已读位点：记下来并转给单聊窗。放在渲染历史之后，这样本批刚追加
        //进来的自己的消息也会一并按位点判定。发送方重开聊天窗、或离线期间对方读过的消息，
        //都靠这一步显示成「已读」—— 实时推送只在双方都在线时才会到达
        virtual void ApplyPeerRead()
        {
            m_panel.ApplyPeerReadPosition((UINT)m_result.m_cid, m_result.m_peerReadMsgid);
        }

        //WM_PAINT 要等消息队列空闲才会派发，紧接着的写库会把它推迟约 20 毫秒；
        //这里用 UpdateWindow 让聊天窗当场同步完成一次绘制
        virtual void PaintChatWindowNow()
        {
            HWND hWnd = ChatWindow();
            if (hWnd != NULL)
                ::UpdateWindow(hWnd);
            BUI_TRACE("MAINPANEL-SYNC-PAINTED ctype=%d cid=%lld",
                      m_result.m_ctype, (long long)m_result.m_cid);
        }

        //落本地库：把本批历史 / 同步消息持久化（无论对应窗口是否仍打开）。用顶层会话的
        //ctype/cid 作归属键，避免依赖每条 notify 自身 cid 是否回填；自己发出的消息也靠
        //这条同步链路（服务端单聊查询双向）回流落库。
        //整批放进一个事务写入（UpsertBatch）：逐条 Upsert 时每条各自提交刷盘，一页 50 条在
        //UI 线程上实测约 300 毫秒。
        virtual bool StoreBatch()
        {
            m_storeStopWatch.Restart();
            if (!m_panel.EnsureMsgStoreOpen())
                return false;

            std::vector<balloonhistory::HistoryRecord> recs;
            recs.reserve(m_result.m_messages.size());
            for (size_t i = 0; i < m_result.m_messages.size(); ++i)
            {
                const net::MessageNotify& m = m_result.m_messages[i];
                if (m.m_msgid <= 0)
                {
                    continue;
                }
                recs.push_back(balloonhistory::RecordFromNotify(m, m_result.m_ctype, m_result.m_cid));
            }
            if (!m_panel.m_msgStore.UpsertBatch(recs))
            {
                LOG_ERROR("local message store: batch upsert failed, ctype=%d, cid=%lld, count=%u.",
                          m_result.m_ctype, (long long)m_result.m_cid, (uint32_t)recs.size());
                return false;
            }
            return true;
        }

        //按本页对账并更新覆盖区间：本页覆盖范围内本地有、服务端没返回的，已在别处被删除，
        //从本地库删掉。必须放在写库之后、刷新消息记录窗之前 —— 刷新看到的才是对完账的结果
        virtual void ReconcileCoverage()
        {
            m_panel.ReconcileHistoryCoverage(m_result);
        }

        //若历史消息记录窗正显示本会话，落库后刷新它，让回填 / 新到的消息即时出现
        virtual void RefreshHistoryWindow()
        {
            m_panel.m_historyWindow.RefreshIfConversation(m_result.m_ctype, m_result.m_cid);
            BUI_TRACE("MAINPANEL-SYNC-STORED count=%u store=%.3fms",
                      (unsigned)m_result.m_messages.size(), m_storeStopWatch.ElapsedMs());
        }

    private:
        //仍打开着的群聊窗；没有或已关闭时为 NULL
        CBalloonGroupChatWindow* GroupWindow() const
        {
            std::map<int64_t, CBalloonGroupChatWindow*>::iterator it =
                m_panel.m_groupChatWindows.find(m_result.m_cid);
            if (it != m_panel.m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
                return it->second;
            return NULL;
        }

        //仍打开着的单聊窗；没有或已关闭时为 NULL
        CBalloonChatWindow* SingleWindow() const
        {
            std::map<UINT, CBalloonChatWindow*>::iterator it =
                m_panel.m_chatWindows.find((UINT)m_result.m_cid);
            if (it != m_panel.m_chatWindows.end() && it->second != NULL && it->second->IsWindow())
                return it->second;
            return NULL;
        }

        //本页所属会话的聊天窗句柄（按 ctype 取群聊窗或单聊窗）；没有打开时为 NULL
        HWND ChatWindow() const
        {
            if (m_result.m_ctype == 2)
            {
                CBalloonGroupChatWindow* pGroup = GroupWindow();
                return (pGroup != NULL) ? pGroup->m_hWnd : NULL;
            }
            CBalloonChatWindow* pChat = SingleWindow();
            return (pChat != NULL) ? pChat->m_hWnd : NULL;
        }

        //所属主面板，生命周期覆盖本对象
        CBalloonMainPanel&                m_panel;
        //本页应答，生命周期覆盖本对象
        const net::SyncMessagesResult&    m_result;
        //写库、对账、刷新消息记录窗三项合计的计时，写进 MAINPANEL-SYNC-STORED 跟踪
        PerfStopWatch                     m_storeStopWatch;
    };

    Steps steps(*this, result);
    balloonmain::SyncArrivalContext ctx;
    ctx.m_historyOwnFetch = historyOwnFetch;
    ctx.m_isSingleChat = (result.m_ctype != 2);
    balloonmain::RunSyncArrival(steps, ctx);
}

CString CBalloonMainPanel::GroupDisplayName(int64_t groupId) const
{
    std::map<int64_t, GroupBrief>::const_iterator it = m_groupInfo.find(groupId);
    if (it != m_groupInfo.end() && !it->second.m_name.IsEmpty())
    {
        return it->second.m_name;
    }
    CString name;
    name.Format(_T("群聊(%lld)"), (long long)groupId);
    return name;
}

void CBalloonMainPanel::FillConversationsTree()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonmain::MainPanelTreeView* tv = dynamic_cast<balloonmain::MainPanelTreeView*>(
        content->FindCtrlById(balloonmain::kIdMessagesTree));
    if (tv == NULL)
    {
        LOG_WARNING(_T("Messages TreeView(kIdMessagesTree) 未找到,跳过会话填充。"));
        return;
    }

    // 用真实会话替换占位：清空旧节点、旧行装饰（未读角标 / 置顶 / 免打扰标记）、
    // 旧"节点↔会话"两份映射。头像位图归头像缓存所有，重建时不释放，新节点直接复用。
    tv->Clear();
    tv->ClearAllRowDecorations();
    m_msgNodeConv.clear();
    m_convNodeId.clear();

    // 会话行高 48px：容纳头像 + 主行(标题) + 副行(最后消息)；与 mock 填充一致。
    tv->SetRowHeight(48);
    const int kMsgAvatarSizePx = 36;   // 会话头像边长（逻辑像素），略大于联系人树
    tv->SetIconSize(kMsgAvatarSizePx);
    tv->SetIconCornerRadius(kMsgAvatarSizePx / 2);
    tv->SetIconUsesAlpha(true);   // 走 AlphaBlend 路径，圆形无锯齿

    for (size_t i = 0; i < m_conversations.size(); ++i)
    {
        AddConversationNode(tv, m_conversations[i]);
    }

    // 节点填完后同步 scrollview 内容高度。Messages 页无顶部标题行，传 0。
    SyncPaneContentHeight(balloonmain::kIdPaneMessages,
                          balloonmain::kIdMessagesTree, 0);

    // 会话变化后同步刷新"消息"Tab 的未读角标（拉取/收发/打开清零都会走到这里）。
    RefreshMessagesTabBadge();
}

void CBalloonMainPanel::RefreshMessagesTabBadge()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonmain::MainPanelTab* tab = dynamic_cast<balloonmain::MainPanelTab*>(
        content->FindCtrlById(balloonmain::kIdTabMessages));
    if (tab == NULL)
    {
        return;
    }
    // 未读总数 = 各单聊会话未读之和；0 时 SetBadgeCount 自动不显示角标。
    int total = net::ConversationsProtocolClient::computeTotalUnread(m_conversations);
    tab->SetBadgeCount(total);
}

int CBalloonMainPanel::AddConversationNode(balloonmain::MainPanelTreeView* tv,
                                           const net::ConversationItem& conv)
{
    // 扁平结构 —— 每个会话作为顶层 AddRoot（无子节点，DuiTreeView 不画 ▶）。标题等
    // 内容统一由 FillConversationRow 填写，与就地刷新走同一段代码。
    const int id = tv->AddRoot(_T(""));

    FillConversationRow(tv, id, conv);

    // 记录"节点 ↔ (ctype, cid)"：正向供点击分流开聊 / 清未读，反向供收发消息时就地刷新。
    m_msgNodeConv[id] = std::make_pair(conv.m_ctype, conv.m_cid);
    m_convNodeId[balloonmain::ConvNodeKey(conv.m_ctype, conv.m_cid)] = id;
    return id;
}

void CBalloonMainPanel::FillConversationRow(balloonmain::MainPanelTreeView* tv, int id,
                                            const net::ConversationItem& conv)
{
    const int kMsgAvatarSizePx = 36;   // 与 FillConversationsTree 同口径

    // 主行标题 + 头像首字 + 底色：与"托盘闪头像"同口径，统一走 ComputeConvDisplay
    //（群名 / 昵称 / "用户<id>"；首字取显示名前 1 字；底色按 cid 稳定取色）。
    CString  title;
    CString  glyph;
    COLORREF avColor = RGB(0, 0, 0);
    ComputeConvDisplay(conv.m_ctype, conv.m_cid, title, glyph, avColor);
    tv->SetItemLabel(id, title);

    // 副行：最后一条消息预览（服务端已拼好 digest，如"[图片]"/正文）。刷新已有行时
    // 预览可能从有变无（例如最后一条被撤回后服务端给了空摘要），所以空串也要写进去。
    tv->SetItemSubLabel(id, Utf8ToCStr(conv.m_digest));

    // 右侧"时间 + 未读红点"上下两排，交 MainPanelTreeView 接管绘制：时间在上排
    // （与姓名同高）、未读红点在下排（与预览同高，>0 才画、0 不显示）。
    CString rightText = FormatConversationTime(conv.m_timestamp);
    tv->SetItemRightStacked(id, rightText, conv.m_unread);

    // 置顶 / 免打扰的行内标记：置顶行底色略深，免打扰行右下角画静音图标且未读胶囊
    // 转灰。两个状态都来自会话缓存，右键菜单改完会重填整棵树，故这里跟着一起刷新。
    tv->SetItemConversationFlags(id, conv.m_pinned != 0, conv.m_mute != 0);

    // 头像：单聊尊重对端、群聊尊重群主设置的 (facetype, customface)（字形自定义 /
    // 真图，与联系人树同一套）。两种会话各有自己的资料缓存，取到哪一份都可能为空
    // （资料还没到），那时回退 ComputeConvDisplay 的首字 + 底色。位图归头像缓存所有。
    HBITMAP hAvatar = NULL;
    if (conv.m_ctype == 1)
    {
        const net::ContactItem* pc = FindCachedContact((UINT)conv.m_cid);
        if (pc != NULL)
        {
            hAvatar = GetEntityAvatarBitmap(conv.m_cid, title,
                                            pc->m_facetype, pc->m_customface, kMsgAvatarSizePx);
        }
    }
    else
    {
        std::map<int64_t, GroupBrief>::const_iterator git = m_groupInfo.find(conv.m_cid);
        if (git != m_groupInfo.end())
        {
            hAvatar = GetEntityAvatarBitmap(conv.m_cid, title,
                                            git->second.m_facetype, git->second.m_customface,
                                            kMsgAvatarSizePx);
        }
    }
    if (hAvatar == NULL)
    {
        hAvatar = m_avatarCache.GetMonogram(glyph, avColor, kMsgAvatarSizePx);
    }
    if (hAvatar != NULL)
    {
        tv->SetItemIcon(id, hAvatar);
    }

    // 单聊会话带在线状态点：按对端好友真实 presence 着色（群会话 ctype==2 不带）。
    // 单聊的 cid 即对端 userid；在通讯录缓存 m_contacts 里查其 presence，查不到按离线灰。
    // 收到 1109 推送时由 OnPresenceChanged 重着此节点，实现会话列表状态点实时刷新。
    if (conv.m_ctype == 1)
    {
        int presence = kMyPresenceOffline;
        const net::ContactItem* pPeer = FindCachedContact((UINT)conv.m_cid);
        if (pPeer != NULL && pPeer->m_userid == conv.m_cid)
        {
            presence = pPeer->m_presence;
        }
        tv->SetItemStatusIcon(id, GetPresenceBadgeBitmap(presence));
        //单聊会话行带「特别关注」星标（群会话不带 —— 群不能被关注）。
        ApplyStarredMark(tv, id, conv.m_cid);
    }

    // 「我的收藏」徽章：单聊与群聊会话都可能被收藏，画在名字（有星标时在星标）之后。
    // 收藏状态在别处改动时由 RefreshFavoriteMarks 就地刷新。
    ApplyFavoriteMark(tv, id, conv.m_ctype, conv.m_cid);
}

bool CBalloonMainPanel::RefreshConversationRow(int ctype, int64_t cid, bool reposition)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonmain::MainPanelTreeView* tv = (content != NULL)
        ? dynamic_cast<balloonmain::MainPanelTreeView*>(content->FindCtrlById(balloonmain::kIdMessagesTree))
        : NULL;
    if (tv == NULL)
    {
        return false;
    }
    const net::ConversationItem* conv = balloonmain::FindConversation(m_conversations, ctype, cid);
    if (conv == NULL)
    {
        return false;
    }

    // 有行就刷新内容；没有行（新会话）且需要定位时追加一行，随后挪到正确位置。
    bool added = false;
    std::map<balloonmain::ConvNodeKey, int>::const_iterator it =
        m_convNodeId.find(balloonmain::ConvNodeKey(ctype, cid));
    if (it != m_convNodeId.end())
    {
        FillConversationRow(tv, it->second, *conv);
    }
    else if (reposition)
    {
        AddConversationNode(tv, *conv);
        added = true;
    }
    else
    {
        return false;
    }

    if (reposition)
    {
        int node = -1;
        int before = -1;
        if (!balloonmain::FindConversationMoveTarget(m_conversations, m_convNodeId, ctype, cid,
                                                     node, before)
            || !tv->MoveRoot(node, before))
        {
            LOG_WARNING("RefreshConversationRow: cannot place row in place (ctype=%d cid=%lld), falling back to a full rebuild.",
                        ctype, (long long)cid);
            return false;
        }
    }

    if (added)
    {
        SyncPaneContentHeight(balloonmain::kIdPaneMessages, balloonmain::kIdMessagesTree, 0);
    }
    RefreshMessagesTabBadge();
    return true;
}

void CBalloonMainPanel::ComputeConvDisplay(int ctype, int64_t cid, CString& outName,
                                           CString& outGlyph, COLORREF& outColor)
{
    // 显示名：群聊取群名缓存（占位"群聊(<id>)"）；单聊走统一的取名口径
    // （备注 → 昵称 → 账号 → "用户<id>"），与通讯录树、单聊窗标题保持一致。
    if (ctype == 2)
    {
        outName = GroupDisplayName(cid);
    }
    else
    {
        outName = ContactDisplayName((UINT)cid);   // 单聊会话 cid 即对端 userid
    }

    // 首字取显示名前 1 字；底色按 cid 稳定取色（群按群id、单聊按对端userid）。
    outGlyph = outName.Left(1);
    outColor = AvatarColorForUser(cid);

    // 单聊：若对端设了字形头像，用其选定字形 + 底色（让托盘闪烁头像也尊重对方头像）。
    // 群聊维持群名首字 + 群id 取色（群头像本期不处理）。真图头像本期仍回退字形。
    if (ctype != 2)
    {
        const net::ContactItem* pc = FindCachedContact((UINT)cid);
        if (pc != NULL)
        {
            balloonmain::AvatarSpec spec = balloonmain::DecodeAvatar(
                pc->m_facetype, pc->m_customface, outName, cid);
            outGlyph = spec.m_glyph;
            outColor = spec.m_color;
        }
    }
}

void CBalloonMainPanel::PostTrayFlash(int ctype, int64_t cid, const std::string& digestUtf8)
{
    if (!IsWindow() || cid <= 0)
    {
        // 这里返回等于"该闪却没闪"，且不留任何痕迹，排查时极难定位，必须记一笔。
        LOG_WARNING("tray flash request dropped: ctype=%d, cid=%lld.",
                    ctype, (long long)cid);
        return;
    }

    CString  name;
    CString  glyph;
    COLORREF color = RGB(0, 0, 0);
    ComputeConvDisplay(ctype, cid, name, glyph, color);

    // 堆分配载荷，PostMessage 给本窗口；托盘控制器处理后 delete。CString →
    // std::wstring：Unicode 工程下 LPCTSTR 即 const wchar_t*，可直接构造 wstring。
    TrayFlashData* pData = new TrayFlashData();
    pData->ctype       = ctype;
    pData->cid         = cid;
    pData->displayName = (LPCTSTR)name;
    pData->preview     = (LPCTSTR)Utf8ToCStr(digestUtf8);   // 最新一条消息预览（与会话列表 digest 同口径）
    pData->avatarText  = (LPCTSTR)glyph;
    pData->avatarColor = color;
    //投给本窗口自己：托盘控制器挂在本面板的消息派发链上，堆负载由它处理后 delete。
    ::PostMessage(m_hWnd, FMG_MSG_TRAY_FLASH, 0, (LPARAM)pData);

    LOG_INFO("已投递托盘闪烁请求: ctype=%d, cid=%lld, 名称=%s, 首字=%s。",
             ctype, (long long)cid, CStrToUtf8(name).c_str(),
             CStrToUtf8(glyph).c_str());
}

void CBalloonMainPanel::PostTrayClear(int ctype, int64_t cid)
{
    if (!IsWindow() || cid <= 0)
    {
        return;
    }

    // Win32 下 lParam 容不下 64 位 cid，故堆分配 long long 传递（托盘控制器处理后 delete）。
    long long* pCid = new long long(cid);
    ::PostMessage(m_hWnd, FMG_MSG_TRAY_CLEAR, (WPARAM)ctype, (LPARAM)pCid);
}

void CBalloonMainPanel::OpenConversation(int ctype, int64_t cid)
{
    if (ctype == 2)
    {
        OpenGroupChat(cid);
    }
    else
    {
        OpenSingleChat((UINT)cid);
    }
    // 清未读角标；MarkConversationRead 内部会回投 FMG_MSG_TRAY_CLEAR 使托盘停闪 / 切目标。
    MarkConversationRead(ctype, cid);
}

void CBalloonMainPanel::AppendCallTipToChat(UINT peerUserId, const CString& tip)
{
    if (peerUserId == 0 || tip.IsEmpty())
    {
        return;
    }

    //通话结果是用户关心的信息，窗口没开就建一个，不让提示丢掉。
    CBalloonChatWindow* pWin = EnsureChatWindow(peerUserId);
    if (pWin == NULL || !pWin->IsWindow())
    {
        return;
    }
    pWin->AppendSystemTip(tip);
}

void CBalloonMainPanel::OnMessageReadNotify(const net::MessageReadNotify& notify)
{
    //群聊按协议不做已读回执，服务端也不会对群聊推送本条；万一收到就记一条日志后忽略，
    //免得把群 id 当成单聊对端的 userid 记进位点表。
    if (notify.m_ctype != conversation_single)
    {
        LOG_WARNING("MainPanel::OnMessageReadNotify: ignore non-single ctype=%d, cid=%lld.",
                    notify.m_ctype, (long long)notify.m_cid);
        return;
    }
    //单聊推送里的 cid 是读消息的那个人（对端）的 userid。
    ApplyPeerReadPosition((UINT)notify.m_cid, notify.m_readmsgid);
}

void CBalloonMainPanel::ApplyPeerReadPosition(UINT peerUserId, int64_t readMsgid)
{
    if (peerUserId == 0 || readMsgid <= 0)
    {
        return;
    }

    //位点只增不减：乱序到达的旧推送、或同步应答带回的较小位点都不能把已读退回去。
    std::map<UINT, int64_t>::iterator it = m_peerReadMsgids.find(peerUserId);
    if (it != m_peerReadMsgids.end() && readMsgid <= it->second)
    {
        return;
    }
    m_peerReadMsgids[peerUserId] = readMsgid;

    //与对方的单聊窗开着就当场标记；没开着的话 EnsureChatWindow 建窗时会补给它。
    int marked = 0;
    std::map<UINT, CBalloonChatWindow*>::iterator wit = m_chatWindows.find(peerUserId);
    if (wit != m_chatWindows.end() && wit->second != NULL && wit->second->IsWindow())
    {
        marked = wit->second->ApplyPeerReadPosition(readMsgid);
    }
    LOG_INFO("MainPanel::ApplyPeerReadPosition: peer=%u, readmsgid=%lld, marked rows=%d.",
             peerUserId, (long long)readMsgid, marked);
}

void CBalloonMainPanel::OnMessageRecalled(const net::MessageRecalledNotify& notify)
{
    UINT selfId = (m_lpFMGClient != NULL)
        ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    bool bIsSelf = (selfId != 0 && (UINT)notify.m_operator == selfId);

    //撤回者昵称：本人撤回时用不上；他人撤回时从通讯录缓存取，取不到就留空，
    //BuildRecallTipText 会退化成"对方撤回了一条消息"。
    CString operatorName;
    if (!bIsSelf && notify.m_operator > 0)
    {
        const net::ContactItem* pContact = FindCachedContact((UINT)notify.m_operator);
        if (pContact != NULL)
        {
            //与其余各处同一口径的「真名（备注）」，不要在这里单独退化成只取昵称。
            operatorName = ContactDisplayName((UINT)notify.m_operator);
        }
    }
    CString tipText = balloonchat::BuildRecallTipText(bIsSelf, operatorName);

    LOG_INFO("v2 message recalled: ctype=%d, cid=%lld, msgid=%lld, operator=%lld, isSelf=%d.",
             notify.m_ctype, (long long)notify.m_cid,
             (long long)notify.m_msgid, (long long)notify.m_operator, bIsSelf ? 1 : 0);

    //一、把本地消息库该条标记为已撤回 —— Count / QueryPage 恒排除已撤回，
    //   标记后"消息记录"窗口就不会再显示它。本机没同步过这条属正常，不算失败。
    m_msgStore.MarkRecalled(notify.m_ctype, notify.m_cid, notify.m_msgid);

    //二、把已经打开着的聊天窗里对应的气泡就地变成提示行。窗口没开、或该条不在
    //   当前视图里都无需处理——下次翻历史时本地库里已排除它。
    if (notify.m_ctype == conversation_group)
    {
        std::map<int64_t, CBalloonGroupChatWindow*>::iterator it =
            m_groupChatWindows.find(notify.m_cid);
        if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            it->second->OnMessageRecalled(notify.m_msgid, tipText);
        }
    }
    else
    {
        std::map<UINT, CBalloonChatWindow*>::iterator it =
            m_chatWindows.find((UINT)notify.m_cid);
        if (it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            it->second->OnMessageRecalled(notify.m_msgid, tipText);
        }
    }

    //三、会话列表预览：只有被撤的就是该会话"最新一条"时才改文字，撤回一条更早的
    //   消息不该动预览。判据取 msgid >= m_lastmsgid —— m_lastmsgid 是已读上限锚点，
    //   本端自己发出的消息不会推进它（发出时 msgid 尚未回填、传的是 0），故不能用
    //   相等判断，否则"撤回自己刚发的最后一条"会漏改。msgid 全局自增，比锚点新
    //   即可认定是最新一条。
    for (size_t i = 0; i < m_conversations.size(); ++i)
    {
        net::ConversationItem& c = m_conversations[i];
        if (c.m_ctype != notify.m_ctype || c.m_cid != notify.m_cid)
        {
            continue;
        }
        if (notify.m_msgid >= c.m_lastmsgid)
        {
            c.m_digest = CStrToUtf8(tipText);
            //预览变成系统提示，内容类型随之改成"系统消息"，避免下次按旧类型
            //（如图片）重新渲染成"[图片]"。
            c.m_lastMsgType = message_type_system;
            FillConversationsTree();
        }
        break;
    }
}

//按时间范围清理本地库时每批删除的行数。实测（docs/清理聊天记录方案.md §15 的 LDB1）单批耗时
//主要是事务提交的落盘开销、与批大小关系不大：批改小不会降低每批的卡顿，只会拉长总耗时，故取 2000。
static const int kRangeDeleteBatchRows = 2000;

void CBalloonMainPanel::OnMessagesDeleted(const net::MessagesDeletedNotify& notify)
{
    //区间形式：作用于全部会话，行数可能很多，入队后逐批删除（见 OnRangeDeleteStep）
    if (notify.m_isRange)
    {
        LOG_INFO("Messages deleted (range): [%lld, %lld], %u range(s) already pending.",
                 (long long)notify.m_begintime, (long long)notify.m_endtime,
                 (unsigned)m_pendingRangeDeletes.size());
        m_pendingRangeDeletes.push_back(std::make_pair(notify.m_begintime, notify.m_endtime));
        if (!m_rangeDeleteRunning)
        {
            m_rangeDeleteRunning = true;
            m_rangeDeletedRows = 0;
            PostMessage(kMsgRangeDeleteStep, 0, 0);
        }
        return;
    }

    //单条形式：一、删本地库。本机没同步过这几条属正常，删 0 行不算失败
    int deleted = 0;
    if (EnsureMsgStoreOpen())
    {
        m_msgStore.DeleteMessages(notify.m_ctype, notify.m_cid, notify.m_msgids, deleted);
    }

    //二、移除已打开聊天窗里对应的气泡。窗口没开、或该条不在当前视图里都无需处理
    const std::vector<long long> ids(notify.m_msgids.begin(), notify.m_msgids.end());
    if (notify.m_ctype == conversation_group)
    {
        std::map<int64_t, CBalloonGroupChatWindow*>::iterator it =
            m_groupChatWindows.find(notify.m_cid);
        if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            it->second->OnMessagesDeleted(ids);
        }
    }
    else
    {
        std::map<UINT, CBalloonChatWindow*>::iterator it =
            m_chatWindows.find((UINT)notify.m_cid);
        if (it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            it->second->OnMessagesDeleted(ids);
        }
    }

    //三、消息记录窗若正显示这个会话，重查本地库
    m_historyWindow.RefreshIfConversation(notify.m_ctype, notify.m_cid);

    //四、重拉会话列表：删掉的可能正是会话的最后一条，预览与未读由服务端重新算最省事，
    //   也与多端保持一致
    CIUSocket::GetInstance().RequestConversations();

    LOG_INFO("Messages deleted: ctype=%d, cid=%lld, requested=%u, local rows removed=%d.",
             notify.m_ctype, (long long)notify.m_cid, (unsigned)notify.m_msgids.size(), deleted);
}

LRESULT CBalloonMainPanel::OnRangeDeleteStep(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    if (m_pendingRangeDeletes.empty())
    {
        m_rangeDeleteRunning = false;
        return 0;
    }

    const std::pair<int64_t, int64_t> range = m_pendingRangeDeletes.front();
    int deleted = 0;
    bool ok = EnsureMsgStoreOpen()
           && m_msgStore.DeleteByTimeRangeBatch(range.first, range.second,
                                                kRangeDeleteBatchRows, deleted);
    if (ok && deleted > 0)
    {
        //本批删到了行：区间里可能还有剩余，投下一条续批消息，两批之间让出 UI 线程
        m_rangeDeletedRows += deleted;
        PostMessage(kMsgRangeDeleteStep, 0, 0);
        return 0;
    }

    //区间已删完（或本地库出错，删不动就不再重试，避免无限循环）
    if (!ok)
    {
        LOG_ERROR("Range delete of local store failed, range=[%lld, %lld], removed %d rows before the error.",
                  (long long)range.first, (long long)range.second, m_rangeDeletedRows);
    }
    else
    {
        LOG_INFO("Range delete of local store done, range=[%lld, %lld], removed %d rows.",
                 (long long)range.first, (long long)range.second, m_rangeDeletedRows);
    }
    m_pendingRangeDeletes.pop_front();
    m_rangeDeletedRows = 0;

    //还有排队的区间就接着删，全部删完之后才刷新界面 —— 每删完一个区间就刷新一次没有意义
    if (!m_pendingRangeDeletes.empty())
    {
        PostMessage(kMsgRangeDeleteStep, 0, 0);
        return 0;
    }
    m_rangeDeleteRunning = false;

    //已打开的聊天窗整页重新加载：区间可能盖住列表里任意多行，逐行移除不如重载
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
        {
            it->second->ReloadLatestMessages();
        }
    }
    for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.begin();
         it != m_groupChatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
        {
            it->second->ReloadLatestMessages();
        }
    }

    //消息记录窗不论显示哪个会话都受影响；会话列表的预览、未读、甚至会话本身是否还在，
    //都由服务端重新算
    m_historyWindow.RefreshIfOpen();
    CIUSocket::GetInstance().RequestConversations();
    return 0;
}

void CBalloonMainPanel::BumpConversation(int ctype, int64_t cid, UINT fromUserId,
                                         const std::string& digestUtf8, int msgType,
                                         int64_t timestampMs, int64_t msgid, bool incUnread)
{
    if (cid <= 0)
    {
        return;
    }

    // 在 m_conversations 里按 (ctype,cid) 找该会话；存在则更新，不存在则新建。
    bool found = false;
    for (size_t i = 0; i < m_conversations.size(); ++i)
    {
        if (m_conversations[i].m_ctype == ctype && m_conversations[i].m_cid == cid)
        {
            net::ConversationItem& c = m_conversations[i];
            c.m_digest      = digestUtf8;
            c.m_lastMsgType = msgType;
            c.m_lastFrom    = fromUserId;
            c.m_timestamp   = timestampMs;
            // 推进已读上限锚点：msgid 全局自增，仅在更大时更新，避免乱序/重复推送
            // 把锚点拉回旧值。本端发出（msgid==0）时不动锚点。
            if (msgid > c.m_lastmsgid)
            {
                c.m_lastmsgid = msgid;
            }
            if (incUnread)
            {
                c.m_unread += 1;
            }
            found = true;
            break;
        }
    }
    if (!found)
    {
        net::ConversationItem c;
        c.m_ctype       = ctype;
        c.m_cid         = cid;
        c.m_unread      = incUnread ? 1 : 0;
        c.m_pinned      = 0;
        c.m_mute        = 0;
        c.m_lastFrom    = fromUserId;
        c.m_lastMsgType = msgType;
        c.m_lastmsgid   = (msgid > 0) ? msgid : 0;   // 新建会话的已读上限锚点
        c.m_digest      = digestUtf8;
        c.m_timestamp   = timestampMs;
        m_conversations.push_back(c);
        // 新冒出来的群会话若还没群名，补拉一次群资料。
        if (ctype == 2 && m_groupInfoRequested.count(cid) == 0)
        {
            m_groupInfoRequested.insert(cid);
            CIUSocket::GetInstance().RequestGroupInfo(cid);
        }
    }

    // 重排（置顶优先、其余按时间倒序），再就地刷新这一行并把它挪到对应位置。3000 个
    // 会话时整表重建要几百毫秒，挪一行只要几毫秒；树或映射不完整时退回整表重建。
    balloonmain::SortConversations(m_conversations);
    if (!RefreshConversationRow(ctype, cid, true))
    {
        FillConversationsTree();
    }
}

void CBalloonMainPanel::MarkConversationRead(int ctype, int64_t cid)
{
    if (cid <= 0)
    {
        return;
    }

    // 会话被读 —— 无条件回投"托盘清除该会话"。这是"会话已读"的统一收口（列表点开 /
    // 托盘单击 / 主动开聊都经此），让托盘控制器把它移出闪烁集合。即使本地未读本就
    // 为 0、清除一个不在集合里的会话也是安全的无操作，故不依赖下面的 changed 判定。
    PostTrayClear(ctype, cid);

    // 找到该会话：本地未读清零，并取出已读到的最大 msgid（m_lastmsgid）用于上报。
    bool changed = false;
    int64_t readmsgid = 0;
    for (size_t i = 0; i < m_conversations.size(); ++i)
    {
        if (m_conversations[i].m_ctype == ctype && m_conversations[i].m_cid == cid)
        {
            readmsgid = m_conversations[i].m_lastmsgid;
            if (m_conversations[i].m_unread != 0)
            {
                m_conversations[i].m_unread = 0;
                changed = true;
            }
            break;
        }
    }

    // 向服务端上报已读位点（cmd_mark_read），更新 t_conversation.f_readmsgid，使重新
    // 登录时该会话未读数据此重新计算而清零——这是修复"看过消息后重新登录未读仍不
    // 清零"的关键。readmsgid<=0（会话尚无已知消息）时 ReportConversationRead 内部跳过。
    // 本次登录内已向服务端报过同样或更大的位点时不再重复上报：打开一个会话时点击处理与
    // 窗口激活各调一次本函数，双击会话列表还会再执行一遍（见 ReadReportDedupe.h）。
    // 本地清未读与托盘停闪不受影响，照常执行。
    if (m_readReportDedupe.ShouldReport(ctype, cid, readmsgid))
        CIUSocket::GetInstance().ReportConversationRead(ctype, cid, readmsgid);
    else
        LOG_INFO("MarkConversationRead: skip duplicate read report, ctype=%d, cid=%lld, readmsgid=%lld.",
                 ctype, (long long)cid, (long long)readmsgid);

    // 未读数变了才刷新。顺序不受未读影响，只就地刷新这一行的未读胶囊，不挪位置；
    // 取不到那一行时退回整表重建。
    if (changed && !RefreshConversationRow(ctype, cid, false))
    {
        FillConversationsTree();
    }
}

CBalloonGroupChatWindow* CBalloonMainPanel::EnsureGroupWindow(int64_t groupId)
{
    if (groupId <= 0)
    {
        return NULL;
    }

    // 查表：已有且窗口仍在 → 复用；对象在但窗口已关 → 惰性回收重建。
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.find(groupId);
    if (it != m_groupChatWindows.end() && it->second != NULL)
    {
        if (it->second->IsWindow())
        {
            return it->second;
        }
        delete it->second;
        m_groupChatWindows.erase(it);
    }

    // 新建一个绑定该群的群聊窗口。传 NULL 作 hMainDlg：群聊窗关闭只销毁自身。
    CBalloonGroupChatWindow* pWin = new CBalloonGroupChatWindow(m_lpFMGClient);
    pWin->SetGroupId(groupId);
    pWin->SetGroupName(GroupDisplayName(groupId));
    //群头像要赶在 CreateAndShow 之前设好，标题栏 / 任务栏图标才会一次画对；
    //缓存里没有这个群时（资料还没到）留默认值，等 1303 应答再补设。
    std::map<int64_t, GroupBrief>::const_iterator avIt = m_groupInfo.find(groupId);
    if (avIt != m_groupInfo.end())
    {
        pWin->SetGroupAvatar(avIt->second.m_facetype, avIt->second.m_customface);
    }
    pWin->SetHostPanelHwnd(m_hWnd);
    pWin->SetMyRole(MyRoleInGroup(groupId));   // 决定右上角"群管理"齿轮是否显示
    pWin->CreateAndShow(NULL);
    if (!pWin->IsWindow())
    {
        delete pWin;
        return NULL;
    }

    // 同单聊窗：新建的窗口错过了之前那次连接态广播，这里补推一次，
    // 保证离线时新开的群聊窗也能看到顶部提示条、输入区也照样置灰。
    pWin->ApplyConnectionState(m_connUiState);

    m_groupChatWindows[groupId] = pWin;
    return pWin;
}

void CBalloonMainPanel::OpenGroupChat(int64_t groupId)
{
    CBalloonGroupChatWindow* pWin = EnsureGroupWindow(groupId);
    if (pWin == NULL)
    {
        return;
    }
    // 用户主动打开：必要时从最小化还原，并前置抢焦点。
    if (pWin->IsIconic())
    {
        pWin->ShowWindow(SW_RESTORE);
    }
    ::SetForegroundWindow(pWin->m_hWnd);
}

void CBalloonMainPanel::AddFavoritesSection(balloonwjui::DuiTreeView* tv)
{
    if (tv == NULL)
    {
        return;
    }

    //把收藏集合与"联系人资料池 / 我的群列表"合成可显示的行。查不到资料的收藏项
    //会被 BuildRows 跳过（好友刚被删、群刚被解散时会碰到），所以这里要看合成后的
    //行数而不是 m_favorites.Count()。
    std::vector<balloonmain::FavoriteRow> rows;
    m_favorites.BuildRows(m_contacts, m_myGroups, rows);
    LOG_INFO("收藏分区: 收藏 %d 条, 可显示 %u 行 (联系人池 %u, 群列表 %u)。",
             m_favorites.Count(), (uint32_t)rows.size(),
             (uint32_t)m_contacts.size(), (uint32_t)m_myGroups.size());
    if (rows.empty())
    {
        //一行都显示不出来时整个分区不出现 —— 下面紧跟着部门树，空的根节点很突兀。
        return;
    }

    CString rootLabel;
    rootLabel.Format(_T("我的收藏  %d"), (int)rows.size());
    int rootId = tv->AddRoot(rootLabel);
    tv->SetExpanded(rootId, true);
    //根节点是分区标题，点击只展开 / 折叠，不显示选中底色（与其它分组标题一致）。
    tv->SetItemSelectable(rootId, false);
    //登记分区根节点，RebuildFavoritesSection 据它只重建这一段
    m_favoritesRootId = rootId;

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const balloonmain::FavoriteRow& row = rows[i];

        //FavoriteRow 里的头像标识是 CString，而 GetEntityAvatarBitmap 与通讯录
        //那几处一样收 UTF-8 的 std::string（头像 fileid 本就是协议侧的字节串），
        //故在此转一道。位图归头像缓存所有，这里只借用。
        HBITMAP hAvatar = GetEntityAvatarBitmap(
            row.m_cid, row.m_displayName, row.m_facetype,
            CStrToUtf8(row.m_customface), kContactAvatarSizePx);

        int node;
        if (hAvatar != NULL)
        {
            node = tv->AddChild(rootId, row.m_displayName, hAvatar);
        }
        else
        {
            node = tv->AddChild(rootId, row.m_displayName);
        }
        m_favoriteSectionNodes.insert(node);

        if (!row.m_subLabel.IsEmpty())
        {
            tv->SetItemSubLabel(node, row.m_subLabel);
        }

        if (row.m_showStatusDot)
        {
            //联系人行画在线状态徽标，字形与配色跟部门树里的联系人行、状态胶囊三处同源。
            tv->SetItemStatusIcon(node, GetPresenceBadgeBitmap(row.m_presence));
            //收藏分区里的联系人行同样带星标 —— 同一个人在树上出现两次（收藏区一次、
            //部门树一次），两处的标记必须一致，否则用户会以为是两个不同的人。
            ApplyStarredMark(tv, node, row.m_cid);
            //登记进联系人节点表：双击开单聊、右键弹联系人菜单两条既有路径直接复用，
            //不必为收藏区另写一套。
            m_contactNodeUserId[node] = (UINT)row.m_cid;
        }
        else
        {
            //群行不画状态点（群没有"在线"这个概念），单独记一张表供开群聊 / 弹群菜单。
            m_favoriteGroupNodeId[node] = row.m_cid;
            //群名是宽字符串，必须先转成 UTF-8 再按窄字符 %s 打印：直接传宽字符串会把
            //UTF-16 的字节当成窄字符串写进日志；改用 %ls 则遇到中文时格式化失败，整条日志被丢弃
            LOG_INFO("favorites section: group row registered, node=%d, groupid=%lld, name=%s.",
                     node, (long long)row.m_cid, CStrToUtf8(row.m_displayName).c_str());
        }
    }
}

void CBalloonMainPanel::RebuildContactsTree()
{
    //还没收到过通讯录（未登录 / 正在拉取）时无从重建。
    if (m_lastContactsResult.m_contacts.empty() && m_lastContactsResult.m_departments.empty())
    {
        return;
    }

    //把收藏模型的现状写回缓存：OnContactsArrived 会用缓存里的 favorites 做 Reset，
    //不同步的话刚做的乐观更新会被服务端当初下发的旧值覆盖掉。
    m_favorites.ExportItems(m_lastContactsResult.m_favorites);
    //「特别关注」同理：不写回缓存，刚点下去的标记会被上一次应答里的旧名单覆盖。
    m_starred.ExportItems(m_lastContactsResult.m_starred);
    //联系人资料池同理：OnContactsArrived 开头就是 m_contacts = result.m_contacts。
    //登录之后对 m_contacts 的即时更新（备注、标签、在线状态、资料变更推送里的头像
    //昵称签名）都只写在 m_contacts 上，不写回快照的话，任何一次重建联系人树都会把
    //它们还原成登录时下发的旧值。2026-09-30 的「改完备注一失焦就变回原样」即由此
    //而来：SetContactRemark 先把新备注写进 m_contacts，紧接着 RefreshContactDisplayName
    //调本函数重建树，新备注随即被快照里的旧备注覆盖，资料窗又按覆盖后的缓存刷新。
    m_lastContactsResult.m_contacts = m_contacts;

    LOG_INFO("重建联系人树: 收藏 %d 条, 群列表 %u 个。",
             m_favorites.Count(), (uint32_t)m_myGroups.size());

    OnContactsArrived(m_lastContactsResult);
}

bool CBalloonMainPanel::RebuildFavoritesSection()
{
    //还没收到过通讯录时没有树可改，交给调用方决定（RebuildContactsTree 此时也什么都不做）
    if (m_lastContactsResult.m_contacts.empty() && m_lastContactsResult.m_departments.empty())
    {
        return false;
    }
    balloonwjui::DuiControl* content = GetClientContent();
    balloonwjui::DuiTreeView* tv = (content != NULL)
        ? dynamic_cast<balloonwjui::DuiTreeView*>(content->FindCtrlById(balloonmain::kIdContactsTree))
        : NULL;
    if (tv == NULL)
    {
        return false;
    }

    //1) 锚点：现在紧跟在收藏分区之后的那个根节点（没有分区时就是树里的第一个根节点）。
    //   新分区追加到末尾之后要挪回它前面，也就是树顶。分区里的行都是叶子，所以分区
    //   展开时占 1 + 行数 个可见行，折叠时只占根节点这一行
    bool wasExpanded = true;
    int anchor = -1;
    if (m_favoritesRootId != -1 && tv->GetVisibleRow(m_favoritesRootId) >= 0)
    {
        wasExpanded = tv->IsExpanded(m_favoritesRootId);
        const int afterRow = tv->GetVisibleRow(m_favoritesRootId) + 1
                           + (wasExpanded ? (int)m_favoriteSectionNodes.size() : 0);
        anchor = (afterRow < tv->GetVisibleCount()) ? tv->GetIdAtVisibleRow(afterRow) : -1;

        //2) 注销旧分区各行的映射，再删掉整个分区
        for (std::set<int>::const_iterator it = m_favoriteSectionNodes.begin();
             it != m_favoriteSectionNodes.end(); ++it)
        {
            m_contactNodeUserId.erase(*it);
            m_favoriteGroupNodeId.erase(*it);
        }
        tv->Remove(m_favoritesRootId);
    }
    else if (tv->GetVisibleCount() > 0)
    {
        anchor = tv->GetIdAtVisibleRow(0);
    }
    m_favoritesRootId = -1;
    m_favoriteSectionNodes.clear();

    //3) 追加新分区（收藏为空时不建），挪回锚点之前，恢复展开状态
    AddFavoritesSection(tv);
    if (m_favoritesRootId != -1)
    {
        tv->SetExpanded(m_favoritesRootId, wasExpanded);
        if (anchor != -1)
        {
            tv->MoveRoot(m_favoritesRootId, anchor);
        }
    }

    SyncPaneContentHeight(balloonmain::kIdPaneContacts,
                          balloonmain::kIdContactsTree, kContactsPaneTitleHeight);
    LOG_INFO("favorites section rebuilt in place: rows=%u, expanded=%d.",
             (unsigned)m_favoriteSectionNodes.size(), wasExpanded ? 1 : 0);
    return true;
}

void CBalloonMainPanel::RefreshContactFavoriteBadges(int64_t userid)
{
    balloonwjui::DuiControl* content = GetClientContent();
    balloonwjui::DuiTreeView* tv = (content != NULL)
        ? dynamic_cast<balloonwjui::DuiTreeView*>(content->FindCtrlById(balloonmain::kIdContactsTree))
        : NULL;
    if (tv == NULL || userid <= 0)
    {
        return;
    }
    //同一个人在部门树里可能有多行（主部门 + 兼属部门），m_contactOrgNode 只记了其中一行，
    //所以遍历全部联系人行。收藏分区自己的行不画徽章，跳过
    for (std::map<int, UINT>::const_iterator it = m_contactNodeUserId.begin();
         it != m_contactNodeUserId.end(); ++it)
    {
        if (it->second != (UINT)userid || m_favoriteSectionNodes.count(it->first) != 0)
        {
            continue;
        }
        ApplyFavoriteMark(tv, it->first, conversation_single, userid);
    }
}

void CBalloonMainPanel::ToggleFavorite(int ctype, int64_t cid)
{
    if (cid <= 0)
    {
        return;
    }

    //三处右键菜单的文案是按当前状态取反给出的，故这里也取反后交给按目标状态写的执行体。
    SetFavoriteState(ctype, cid, !m_favorites.IsFavorite(ctype, cid));
}

void CBalloonMainPanel::SetFavoriteState(int ctype, int64_t cid, bool favorite)
{
    if (cid <= 0)
    {
        return;
    }

    //已经是这个状态就什么都不做：资料对话框的开关在被刷新时也会走一次 SetOn，
    //不加这项判断的话，界面刷新会被当成一次用户操作，多发一条无意义的协议。
    if (m_favorites.IsFavorite(ctype, cid) == favorite)
    {
        return;
    }

    //先本地生效再发协议：点完立刻能看到收藏区的变化，不必等一个来回。
    //应答失败时按服务端回带的 ctype/cid/favorite 回滚（见 OnSetFavoriteResult）。
    m_favorites.SetFavorite(ctype, cid, favorite);
    //联系人树只有收藏分区与此人名字后面的收藏徽章变了：只重建分区、就地刷新徽章，
    //不整棵重建（5000 人时整棵重建要几百毫秒，点一下收藏会明显卡顿）。
    if (RebuildFavoritesSection())
    {
        if (ctype == conversation_single)
        {
            RefreshContactFavoriteBadges(cid);
        }
    }
    else
    {
        RebuildContactsTree();
    }
    //群列表与会话列表上的收藏徽章就地刷新，不必整树重建。
    RefreshFavoriteMarks(ctype, cid);

    CIUSocket::GetInstance().SendSetFavorite(ctype, cid, favorite);

    LOG_INFO("set favorite: ctype=%d, cid=%lld, favorite=%d.",
             ctype, (long long)cid, (int)favorite);
}

void CBalloonMainPanel::OnSetFavoriteResult(const net::SetFavoriteResult& result)
{
    if (result.m_code == 0)
    {
        //成功：乐观更新已经是对的，什么都不用做。
        return;
    }

    //失败：把这一条回滚。服务端原样回带了 ctype/cid/favorite，据此定位到底是哪一条 ——
    //用户连点几个人时会有多条请求在途，只看"最后一次操作"会回滚错对象。
    //本次请求的意图是 m_favorite，回滚就是把它设成相反值。
    const bool intended = (result.m_favorite != 0);
    m_favorites.SetFavorite(result.m_ctype, result.m_cid, !intended);
    //与 SetFavorite 同理：只重建收藏分区、就地刷新徽章
    if (RebuildFavoritesSection())
    {
        if (result.m_ctype == conversation_single)
        {
            RefreshContactFavoriteBadges(result.m_cid);
        }
    }
    else
    {
        RebuildContactsTree();
    }
    RefreshFavoriteMarks(result.m_ctype, result.m_cid);
    //资料对话框上的收藏开关此刻还停在用户拨过去的位置，跟着退回来，否则界面显示的
    //状态与实际状态不一致，而用户没有任何提示。群收藏不影响该窗口，故只处理联系人。
    if (result.m_ctype == conversation_single)
    {
        RefreshContactWindowIfShowing((UINT)result.m_cid);
    }

    LOG_ERROR(_T("收藏设置失败，已回滚: ctype=%d, cid=%lld, favorite=%d, code=%d。"),
              result.m_ctype, (long long)result.m_cid, result.m_favorite, result.m_code);

    //收藏数量超限要给用户一句明确的话，否则他只会看到"点了没反应"。
    //其余失败（不是好友 / 不在群里 / 服务器内部错误）给一句通用提示即可。
    LPCTSTR tip = _T("收藏设置失败，请稍后重试。");
    if (result.m_code == error_code_favorite_limit)
    {
        tip = _T("收藏数量已达上限，请先取消一些收藏再试。");
    }
    ::MessageBox(m_hWnd, tip, _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
}

bool CBalloonMainPanel::IsContactStarred(int64_t userid) const
{
    return m_starred.IsStarred(userid);
}

void CBalloonMainPanel::ApplyStarredMark(balloonwjui::DuiTreeView* tv, int node, int64_t userid)
{
    //星标是 MainPanelTreeView 的扩展，裸 DuiTreeView 没有这个能力。三棵树建出来的
    //都是子类（XML 里写的是 <main-treeview>），转不过去说明有人换了控件，
    //此时安静跳过而不是崩掉 —— 少一个装饰不该让整棵树填不出来。
    balloonmain::MainPanelTreeView* mtv = dynamic_cast<balloonmain::MainPanelTreeView*>(tv);
    if (mtv == NULL)
    {
        return;
    }
    mtv->SetItemStarred(node, m_starred.IsStarred(userid));
}

void CBalloonMainPanel::ApplyFavoriteMark(balloonwjui::DuiTreeView* tv, int node,
                                          int ctype, int64_t cid)
{
    //与 ApplyStarredMark 同理：徽章是 MainPanelTreeView 的扩展，转不过去就安静跳过。
    balloonmain::MainPanelTreeView* mtv = dynamic_cast<balloonmain::MainPanelTreeView*>(tv);
    if (mtv == NULL)
    {
        return;
    }
    mtv->SetItemFavorite(node, m_favorites.IsFavorite(ctype, cid));
}

void CBalloonMainPanel::RefreshFavoriteMarks(int ctype, int64_t cid)
{
    if (cid <= 0)
    {
        return;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    const bool favorite = m_favorites.IsFavorite(ctype, cid);

    //群树：只有群才在这里出现。
    const int kCtypeGroup = 2;
    if (ctype == kCtypeGroup)
    {
        RefreshGroupsTreeFavoriteMarks();
    }

    //会话树：单聊与群聊会话都可能被收藏，按 (ctype, cid) 匹配。
    balloonmain::MainPanelTreeView* mtv = dynamic_cast<balloonmain::MainPanelTreeView*>(
        content->FindCtrlById(balloonmain::kIdMessagesTree));
    if (mtv != NULL)
    {
        for (std::map<int, std::pair<int, int64_t> >::iterator it = m_msgNodeConv.begin();
             it != m_msgNodeConv.end(); ++it)
        {
            if (it->second.first == ctype && it->second.second == cid)
            {
                mtv->SetItemFavorite(it->first, favorite);
            }
        }
    }

    LOG_INFO("refresh favorite marks: ctype=%d, cid=%lld, favorite=%d.",
             ctype, (long long)cid, (int)favorite);
}

void CBalloonMainPanel::RefreshGroupsTreeFavoriteMarks()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonmain::MainPanelTreeView* gtv = dynamic_cast<balloonmain::MainPanelTreeView*>(
        content->FindCtrlById(balloonmain::kIdGroupsTree));
    if (gtv == NULL)
    {
        return;
    }

    //节点 → 群ID 的映射由 FillGroupsTree 维护；群树还没填过时映射为空，什么都不做。
    const int kCtypeGroup = 2;
    for (std::map<int, int64_t>::iterator it = m_groupNodeId.begin();
         it != m_groupNodeId.end(); ++it)
    {
        gtv->SetItemFavorite(it->first, m_favorites.IsFavorite(kCtypeGroup, it->second));
    }
}

void CBalloonMainPanel::RefreshStarredMarks(int64_t userid)
{
    if (userid <= 0)
    {
        return;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    const bool starred = m_starred.IsStarred(userid);

    //通讯录树：同一个人可能挂着两个节点（部门树里一个、收藏分区里一个），
    //故遍历整张"节点 → userid"表把命中的都改掉，不能找到一个就收工。
    balloonmain::MainPanelTreeView* ctv = dynamic_cast<balloonmain::MainPanelTreeView*>(
        content->FindCtrlById(balloonmain::kIdContactsTree));
    if (ctv != NULL)
    {
        for (std::map<int, UINT>::iterator it = m_contactNodeUserId.begin();
             it != m_contactNodeUserId.end(); ++it)
        {
            if (it->second == (UINT)userid)
            {
                ctv->SetItemStarred(it->first, starred);
            }
        }
    }

    //会话树：只有单聊会话（ctype==1）才有星标，其 cid 即对端 userid。
    balloonmain::MainPanelTreeView* mtv = dynamic_cast<balloonmain::MainPanelTreeView*>(
        content->FindCtrlById(balloonmain::kIdMessagesTree));
    if (mtv != NULL)
    {
        for (std::map<int, std::pair<int, int64_t> >::iterator it = m_msgNodeConv.begin();
             it != m_msgNodeConv.end(); ++it)
        {
            if (it->second.first == 1 && it->second.second == userid)
            {
                mtv->SetItemStarred(it->first, starred);
            }
        }
    }

    LOG_INFO("刷新特别关注星标: userid=%lld, starred=%d。",
             (long long)userid, (int)starred);
}

void CBalloonMainPanel::MaybeShowStarredPresencePopup(int64_t userid, int oldPresence,
                                                      int newPresence, UINT selfId)
{
    // 一、只对被特别关注的人弹。名单为空时这一步就挡住了，后面几项都不必判。
    if (!m_starred.IsStarred(userid))
    {
        return;
    }

    // 二、排除自己。自己的状态变化走 OnChangePresenceResult 那条路、本不该经过这里，
    //     但多设备登录时会有回声推送进来，不排除就会给自己弹"我已上线"。
    if ((UINT)userid == selfId)
    {
        return;
    }

    // 三、只认真正的上下线跳变。在线↔离开↔忙碌之间的来回切换不弹 —— 对方锁个屏就
    //     弹一次，一上午能弹十几次。两个判定共用同一套"可见在线"口径，见
    //     NotificationSound.h。
    const bool cameOnline  = balloonmain::IsBuddyJustCameOnline(oldPresence, newPresence);
    const bool wentOffline = balloonmain::IsBuddyJustWentOffline(oldPresence, newPresence);
    if (!cameOnline && !wentOffline)
    {
        return;
    }

    // 四、登录初期静默。通讯录还没回来时上面那个 oldPresence 一律取 0（离线），
    //     此刻收到的推送会被判成"刚上线"，不静默就会在登录瞬间把在线的同事全弹一遍。
    //     GetTickCount 会在约 49.7 天后回绕，用相减再比较而不是直接比大小，回绕时也对。
    if ((int)(::GetTickCount() - m_starredSilenceUntilTick) < 0)
    {
        LOG_INFO("特别关注提示被登录静默期挡下: userid=%lld, online=%d。",
                 (long long)userid, (int)cameOnline);
        return;
    }

    // 五、本人处于「勿扰」时不弹。本客户端里的勿扰就是用户自己选的在线状态
    //     presence_busy（状态菜单里的那一项，见 MainPanelStatusMenu.h），不是某个配置
    //     开关。用户既然选了勿扰，就是不想被浮窗打断，而不是只不想收消息。
    //     读 Header 里那份"用户自己选的状态"——它不受掉线影响（掉线只说明连不上，
    //     用户选的状态没变），拿连接态去判会在断线重连时误伤。
    balloonmain::MainPanelHeader* header = FindMainPanelHeader(GetClientContent());
    if (header != NULL && header->GetPresence() == presence_busy)
    {
        LOG_INFO("特别关注提示被勿扰挡下: userid=%lld, online=%d。",
                 (long long)userid, (int)cameOnline);
        return;
    }

    // 显示名取备注优先的那一份，与通讯录树 / 会话列表同一口径。
    m_starredPopup.AddPresenceEvent(m_hWnd, userid,
                                    ContactDisplayName((UINT)userid), cameOnline);
}

void CBalloonMainPanel::ToggleStarred(int64_t userid)
{
    if (userid <= 0)
    {
        return;
    }

    //四处菜单的文案是按当前状态取反给出的，故这里也取反后交给按目标状态写的执行体。
    SetStarredState(userid, !m_starred.IsStarred(userid));
}

void CBalloonMainPanel::SetStarredState(int64_t userid, bool starred)
{
    if (userid <= 0)
    {
        return;
    }

    //关注自己没有意义（自己的上下线自己清楚），服务端也会直接拒掉。四处菜单本来
    //就不会对自己出这一项，这里再兜一道，免得将来新加入口时漏判。
    if (IsSelfUserId((UINT)userid))
    {
        LOG_ERROR(_T("忽略对自己的特别关注请求: userid=%lld。"), (long long)userid);
        return;
    }

    //已经是这个状态就什么都不做，理由同 SetFavoriteState。
    if (m_starred.IsStarred(userid) == starred)
    {
        return;
    }

    //先本地生效再发协议：菜单文案下次弹出来立刻是对的，不必等一个来回。
    //应答失败时按服务端回带的 target/starred 回滚（见 OnSetStarredResult）。
    m_starred.SetStarred(userid, starred);
    RefreshStarredMarks(userid);

    CIUSocket::GetInstance().SendSetStarred(userid, starred);

    LOG_INFO("set starred: userid=%lld, starred=%d, total %d contacts.",
             (long long)userid, (int)starred, m_starred.Count());
}

void CBalloonMainPanel::OnSetStarredResult(const net::SetStarredResult& result)
{
    if (result.m_code == 0)
    {
        //成功：乐观更新已经是对的，什么都不用做。
        return;
    }

    //失败：把这一条回滚。服务端原样回带了 target/starred，据此定位到底是哪一个人 ——
    //用户连点几个人时会有多条请求在途，只看"最后一次操作"会回滚错对象。
    //本次请求的意图是 m_starred，回滚就是把它设成相反值。
    const bool intended = (result.m_starred != 0);
    m_starred.SetStarred(result.m_target, !intended);
    RefreshStarredMarks(result.m_target);
    //资料对话框上的特别关注开关跟着退回来，理由同收藏的回滚分支。
    RefreshContactWindowIfShowing((UINT)result.m_target);

    LOG_ERROR(_T("特别关注设置失败，已回滚: target=%lld, starred=%d, code=%d。"),
              (long long)result.m_target, result.m_starred, result.m_code);

    //关注人数超限要给用户一句明确的话，否则他只会看到"点了没反应"。
    //其余失败（不同企业 / 对方不存在 / 服务器内部错误）给一句通用提示即可。
    LPCTSTR tip = _T("特别关注设置失败，请稍后重试。");
    if (result.m_code == error_code_starred_limit)
    {
        tip = _T("特别关注人数已达上限，请先取消一些再试。");
    }
    ::MessageBox(m_hWnd, tip, _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
}

CString CBalloonMainPanel::ContactDisplayName(UINT peerUserId) const
{
    const net::ContactItem* c = FindCachedContact(peerUserId);
    if (c == NULL)
    {
        //不在通讯录缓存里（资料还没到 / 已离职）：给个能对上号的兜底名。
        return balloonmain::ResolveContactDisplayName(CString(), CString(), CString(), peerUserId);
    }
    return balloonmain::ResolveContactDisplayName(Utf8ToCStr(c->m_remark),
                                                  Utf8ToCStr(c->m_nickname),
                                                  Utf8ToCStr(c->m_username),
                                                  peerUserId);
}

void CBalloonMainPanel::SetContactRemark(UINT peerUserId, const CString& remark)
{
    if (peerUserId == 0)
    {
        return;
    }

    //原值取自通讯录缓存 —— 它是本端"当前生效的备注"的唯一权威来源，资料窗里显示的
    //那份只是它的一份拷贝。
    CString oldRemark;
    const net::ContactItem* pCached = FindCachedContact(peerUserId);
    if (pCached != NULL)
    {
        oldRemark = Utf8ToCStr(pCached->m_remark);
    }

    //该发、该拦还是该当没改，由纯函数判定（见 ContactRemarkModel.h）。判定与执行
    //都收在主面板：通讯录缓存、连接态与 socket 全在这里，两处各判一份日后必走岔。
    const bool online = (m_connUiState == balloonmain::kConnUiOnline);
    const balloonmain::RemarkCommitDecision decision =
        balloonmain::DecideRemarkCommit(remark, oldRemark, online,
                                        balloonmain::kContactRemarkMaxLen);

    if (decision.m_action == balloonmain::kRemarkCommitNoChange)
    {
        //一个字没改：不发包。资料窗那边已经退出编辑态，刷一次把它的显示对回缓存值
        //（顺带抹平用户多敲的首尾空白）。
        RefreshContactDisplayName(peerUserId);
        return;
    }

    if (decision.m_action == balloonmain::kRemarkCommitOffline)
    {
        //发不出去就不能让界面停在"改好了"的样子——否则用户以为成了，下次刷新又变回去。
        RefreshContactDisplayName(peerUserId);
        ::MessageBox(m_hWnd, _T("当前网络不可用，备注未能保存。"),
                     _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
        LOG_INFO("设置联系人备注被拦下（当前不可联网）: userid=%u。", peerUserId);
        return;
    }

    if (decision.m_action == balloonmain::kRemarkCommitTooLong)
    {
        //库里 f_userX_markname 是 VARCHAR(32)，放过去会被悄悄截断，宁可让用户自己删。
        RefreshContactDisplayName(peerUserId);
        CString tip;
        tip.Format(_T("备注名最多 %d 个字，请删减后再试。"), balloonmain::kContactRemarkMaxLen);
        ::MessageBox(m_hWnd, tip, _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
        LOG_INFO("设置联系人备注被拦下（超长）: userid=%u, 长度=%d。",
                 peerUserId, decision.m_trimmed.GetLength());
        return;
    }

    //先本地生效再发协议：改完立刻能在通讯录 / 会话列表 / 聊天窗标题上看到，不必等
    //一个来回。改动前的值记进回滚表，应答失败时原样写回（见 OnSetFriendRemarkResult）。
    for (size_t i = 0; i < m_contacts.size(); ++i)
    {
        if ((UINT)m_contacts[i].m_userid == peerUserId)
        {
            m_contacts[i].m_remark = CStrToUtf8(decision.m_trimmed);
            break;
        }
    }
    m_pendingRemarkRollback[peerUserId] = oldRemark;

    //三处显示名都要跟着变：通讯录树、会话列表、已打开的单聊窗标题。
    RefreshContactDisplayName(peerUserId);

    CIUSocket::GetInstance().SendSetFriendRemark((int64_t)peerUserId,
                                                 CStrToUtf8(decision.m_trimmed));

    //备注名可能含用户隐私，日志只记长度不记内容。
    LOG_INFO("设置联系人备注: userid=%u, 新备注长度=%d。",
             peerUserId, decision.m_trimmed.GetLength());
}

LRESULT CBalloonMainPanel::OnSetContactRemarkMsg(UINT /*uMsg*/, WPARAM wParam,
                                                 LPARAM lParam, BOOL& /*bHandled*/)
{
    //联系人资料窗提交了备注：lParam 为堆上的 UTF-8 原始文本（未 trim），处理后须 delete。
    std::string* pRemark = (std::string*)lParam;
    if (pRemark == NULL)
    {
        return 0;
    }
    std::unique_ptr<std::string> guard(pRemark);

    SetContactRemark((UINT)wParam, Utf8ToCStr(*pRemark));
    return 0;
}

void CBalloonMainPanel::SetContactTags(UINT peerUserId, const std::vector<std::string>& tagsUtf8)
{
    if (peerUserId == 0)
    {
        return;
    }

    //原值取自通讯录缓存 —— 它是本端"当前生效的标签"的唯一权威来源，资料窗里显示的
    //那份只是它的一份拷贝。
    std::vector<std::string> oldTags;
    const net::ContactItem* pCached = FindCachedContact(peerUserId);
    if (pCached != NULL)
    {
        oldTags = pCached->m_tags;
    }

    //一条没改：不发包。资料窗那边已经改完界面了，刷一次把它对回缓存值。
    //这里按逐字相等判断，不做归一后再比 —— 归一是服务端的规则，本端跟着猜容易走岔；
    //真发了一次内容等价的请求，服务端也是幂等处理，代价只是一个来回。
    if (tagsUtf8 == oldTags)
    {
        RefreshContactTagsDisplay(peerUserId);
        return;
    }

    //发不出去就不能让界面停在"改好了"的样子——否则用户以为成了，下次刷新又变回去。
    if (m_connUiState != balloonmain::kConnUiOnline)
    {
        RefreshContactTagsDisplay(peerUserId);
        ::MessageBox(m_hWnd, _T("当前网络不可用，标签未能保存。"),
                     _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
        LOG_INFO("设置联系人标签被拦下（当前不可联网）: userid=%u。", peerUserId);
        return;
    }

    //先本地生效再发协议：改完立刻能在资料窗上看到，不必等一个来回。改动前的列表记进
    //回滚表，供应答里 userid 缺失（老服务端）时退回（见 OnSetContactTagsResult）。
    for (size_t i = 0; i < m_contacts.size(); ++i)
    {
        if ((UINT)m_contacts[i].m_userid == peerUserId)
        {
            m_contacts[i].m_tags = tagsUtf8;
            break;
        }
    }
    m_pendingTagsRollback[peerUserId] = oldTags;

    RefreshContactTagsDisplay(peerUserId);

    CIUSocket::GetInstance().SendSetContactTags((int64_t)peerUserId, tagsUtf8);
    //标签可能含用户隐私，日志只记条数不记内容。
    LOG_INFO("设置联系人标签: userid=%u, 新标签条数=%d。", peerUserId, (int)tagsUtf8.size());
}

void CBalloonMainPanel::OnSetContactTagsResult(const net::SetContactTagsResult& result)
{
    const UINT peerUserId = (UINT)result.m_userid;

    //老服务端不回带 userid：无从判断这条应答对应哪一次请求。此时只能靠本地快照兜底，
    //而且只在失败时才退 —— 连着改几个人时"退错人"比"不退"更糟，所以宁可保守。
    if (peerUserId == 0)
    {
        LOG_ERROR("OnSetContactTagsResult: 应答未回带 userid，code=%d，无法定位到具体联系人。",
                  result.m_code);
        return;
    }

    //应答里的 m_tags 无论成败都是服务端侧的真值：成功时是本次提交归一后的最终列表
    //（服务端会去首尾空白、丢空串、忽略大小写去重，本端乐观更新算出来的那份未必与它
    //逐字相同），失败时是服务端当前实际存着的那一份。两种情形都直接覆盖本地缓存，
    //调用方不必区分是"对齐"还是"回滚"，也就不会出现两条路各写一半的中间状态。
    for (size_t i = 0; i < m_contacts.size(); ++i)
    {
        if ((UINT)m_contacts[i].m_userid == peerUserId)
        {
            m_contacts[i].m_tags = result.m_tags;
            break;
        }
    }
    m_pendingTagsRollback.erase(peerUserId);
    RefreshContactTagsDisplay(peerUserId);

    if (result.m_code == 0)
    {
        return;
    }

    //失败要让用户知道，否则他只会看到标签自己变回去了、以为程序出错。
    CString tip;
    if (result.m_code == error_code_tag_limit)
    {
        tip = _T("标签数量已达上限，未能保存。");
    }
    else
    {
        tip = _T("标签未能保存，请稍后重试。");
    }
    ::MessageBox(m_hWnd, tip, _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
    LOG_ERROR("设置联系人标签失败: userid=%u, code=%d。", peerUserId, result.m_code);
}

void CBalloonMainPanel::RefreshContactTagsDisplay(UINT peerUserId)
{
    //标签只出现在资料窗里，不上通讯录树也不上会话列表，故不必像备注名那样整棵树重建。
    RefreshContactWindowIfShowing(peerUserId);
}

LRESULT CBalloonMainPanel::OnSetContactTagsMsg(UINT /*uMsg*/, WPARAM wParam,
                                               LPARAM lParam, BOOL& /*bHandled*/)
{
    //联系人资料窗提交了整份标签列表：lParam 为堆上的 UTF-8 列表，处理后须 delete。
    std::vector<std::string>* pTags = (std::vector<std::string>*)lParam;
    if (pTags == NULL)
    {
        return 0;
    }
    std::unique_ptr<std::vector<std::string> > guard(pTags);

    SetContactTags((UINT)wParam, *pTags);
    return 0;
}

void CBalloonMainPanel::RefreshContactDisplayName(UINT peerUserId)
{
    //通讯录树与会话列表整体重建最稳妥：显示名同时影响树节点文字与会话行标题，
    //逐节点改文字要在两棵树里各找一遍，还容易漏掉收藏分区里的那一份。
    //
    //联系人树必须走 RebuildContactsTree（用缓存的 v2 通讯录应答重建组织架构树），
    //<u>不能</u>用 RefreshContacts —— 后者重建的是旧 v1 好友列表，企业版下好友列表
    //恒为空，那条路径整个是空转，改完备注树上的名字根本不会变。
    RebuildContactsTree();
    FillConversationsTree();

    //已打开的单聊窗：标题 / 标题栏 / 任务栏图标上都是这个名字。走 UpdatePeerInfo
    //而不是 SetPeerName —— 后者只在 CreateAndShow 之前有效（见 BalloonChatWindow.h），
    //窗口已经开着时调它不会刷新界面。签名与头像原样传回去，本次只改名字。
    std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.find(peerUserId);
    if (it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow())
    {
        const net::ContactItem* c = FindCachedContact(peerUserId);
        const CString      sign  = (c != NULL) ? Utf8ToCStr(c->m_signature) : CString();
        const int          ftype = (c != NULL) ? c->m_facetype : 0;
        const std::string  cface = (c != NULL) ? c->m_customface : std::string();
        it->second->UpdatePeerInfo(ContactDisplayName(peerUserId), sign, ftype, cface);
    }

    //资料窗若正开着这个人，把备注行也刷新掉。
    RefreshContactWindowIfShowing(peerUserId);
}

void CBalloonMainPanel::OnSetFriendRemarkResult(const net::SetFriendRemarkResult& result)
{
    const UINT peerUserId = (UINT)result.m_userid;

    if (result.m_code == 0)
    {
        //成功：乐观更新已经是对的，丢掉回滚快照即可。
        if (peerUserId != 0)
        {
            m_pendingRemarkRollback.erase(peerUserId);
        }
        return;
    }

    //失败：把这一条回滚。服务端原样回带了 userid，据此定位到底是哪一条 —— 用户
    //连着改几个人时会有多条请求在途，只看"最后一次操作"会把别人的备注退回去。
    //老服务端不回带 userid（m_userid 为 0）时宁可不回滚，也不要退错人。
    std::map<UINT, CString>::iterator it = m_pendingRemarkRollback.find(peerUserId);
    if (peerUserId != 0 && it != m_pendingRemarkRollback.end())
    {
        for (size_t i = 0; i < m_contacts.size(); ++i)
        {
            if ((UINT)m_contacts[i].m_userid == peerUserId)
            {
                m_contacts[i].m_remark = CStrToUtf8(it->second);
                break;
            }
        }
        m_pendingRemarkRollback.erase(it);
        RefreshContactDisplayName(peerUserId);
    }

    LOG_ERROR("设置备注失败，已回滚: userid=%u, code=%d。", peerUserId, result.m_code);

    //"这个人不在了"要给一句明确的话（对方已离职 / 账号被删，通讯录还没刷新到），
    //用户不会想到这一层，只给通用提示他会以为是网络问题反复重试。
    //注意服务端这里返回的是通用的 error_code_not_found，不是 error_code_not_friend
    //——企业版取消好友关系后，改备注不再有"是不是好友"这道校验。
    LPCTSTR tip = _T("备注保存失败，请稍后重试。");
    if (result.m_code == error_code_not_found)
    {
        tip = _T("该联系人不存在，可能已经离职。");
    }
    ::MessageBox(m_hWnd, tip, _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
}

void CBalloonMainPanel::BeginContactRemarkEdit(UINT peerUserId)
{
    //右键菜单的"修改备注"不另做输入对话框：资料窗里已经有一套完整的备注行内编辑
    //（铅笔按钮、编辑框、失焦去抖提交），直接把资料窗打开并让那一行进入编辑态，
    //两个入口的交互与校验就天然一致，不会出现两套措辞不同的提示。
    ballooncontact::ContactModel model;
    if (!BuildContactModelFromCache(peerUserId, model))
    {
        return;   // 不在通讯录缓存里
    }
    m_contactWindow.OpenForContact(m_hWnd, peerUserId, model, true);
    //把当前连接态推给它 —— 资料窗里的语音 / 视频通话要靠这个值做离线门控。
    //ApplyConnectionState 平时只在连接态变化时逐窗转发，新开的窗口收不到，故在此补一次。
    m_contactWindow.ApplyConnectionState(m_connUiState);
}

void CBalloonMainPanel::ShowContactContextMenu(UINT peerUserId)
{
    const bool isSelf = IsSelfUserId(peerUserId);

    // 免打扰的当前状态取自会话缓存。还没聊过的联系人不在会话列表里，查不到就按
    // "未免打扰"显示，用户点了照样发协议生效。右键的是自己时这一项不出现，
    // 也就无需去查。
    bool muted = false;
    if (!isSelf)
    {
        const net::ConversationItem* pConv =
            balloonmain::FindConversation(m_conversations, 1, (int64_t)peerUserId);
        if (pConv != NULL)
        {
            muted = (pConv->m_mute != 0);
        }
    }

    // 菜单结构（含"右键的是自己时只剩查看资料"这条规则）由纯函数给出，本处只
    // 负责把行模型喂给 DuiMenu 并分发命令，便于单测（见 MainPanelContactMenu.h）。
    balloonmain::ContactMenuState state;
    state.m_isSelf   = isSelf;
    state.m_muted    = muted;
    state.m_favorite = !isSelf && m_favorites.IsFavorite(1, (int64_t)peerUserId);
    state.m_starred  = !isSelf && m_starred.IsStarred((int64_t)peerUserId);

    const std::vector<balloonmain::ContactMenuEntry> model =
        balloonmain::BuildContactMenuModel(state);

    balloonwjui::DuiMenu menu;
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (model[i].m_isSeparator)
        {
            menu.AppendSeparator();
        }
        else
        {
            menu.AppendItem(model[i].m_id, model[i].m_label);
        }
    }

    // 弹出前把落点夹进显示器工作区：列表项可能靠近屏幕底/右边缘，DuiMenu 不
    // 自动翻转，不夹取会让菜单跑到桌面外（见 PopupMenuPlacement.h）。
    POINT pt = { 0, 0 };
    ::GetCursorPos(&pt);
    POINT cpos = balloonmain::ClampPopupMenuToWorkArea(pt, menu.MeasureSize());
    UINT chosen = menu.TrackPopup(cpos.x, cpos.y, m_hWnd);   // 同步阻塞

    switch (chosen)
    {
    case kCMenuSendMsg:
        OpenSingleChat(peerUserId);
        break;

    // 语音 / 视频通话：投给本窗口交通话协调器，与单聊窗头部那两个按钮走同一条路。
    case kCMenuVoice:
        StartCallWithContact(peerUserId, false);
        break;

    case kCMenuVideo:
        StartCallWithContact(peerUserId, true);
        break;

    case kCMenuViewProfile:
        // 看自己走"个人信息"窗（与点左上角头像是同一个窗口，可编辑）；看同事
        // 走联系人资料窗。不共用一个窗口是为了不出现两个不一样的"自己的资料"。
        if (isSelf)
        {
            OpenProfileDlg();
        }
        else
        {
            OpenContactProfile(peerUserId);
        }
        break;

    case kCMenuViewHistory:
        OpenHistory(1, (int64_t)peerUserId);
        break;

    case kCMenuFavorite:
        ToggleFavorite(1, (int64_t)peerUserId);
        break;

    // 特别关注：菜单文案是按当前状态取反给出的，ToggleStarred 内部同样取反，
    // 故这里直接调、不用再传目标状态。
    case kCMenuStarred:
        ToggleStarred((int64_t)peerUserId);
        break;

    case kCMenuMute:
        // 菜单文案是按当前状态取反给出的，故这里直接取反写入。
        SetConversationMuteState(1, (int64_t)peerUserId, !muted);
        break;

    case kCMenuRemark:
        // 打开资料窗并让「备注名」那一行直接进入编辑态 —— 不另做一个输入对话框，
        // 两个入口共用同一套编辑与校验，交互才一致。
        BeginContactRemarkEdit(peerUserId);
        break;

    //复制联系人名：复制列表上显示的名字（备注优先，其次昵称）
    case kCMenuCopyName:
        CopyContactName(peerUserId);
        break;

    case 0:
        break;   // 未选中（失焦 / ESC）

    // 兜底分支。本菜单的每一项现在都已接上，走到这里只可能是新增了菜单项却忘了
    // 加分发；记一条日志，不要静默丢弃（症状会是"点了没反应"，很难查）。
    default:
        LOG_ERROR("contact context menu: unhandled command id=%u, userid=%u.",
                  chosen, peerUserId);
        break;
    }
}

int CBalloonMainPanel::MyRoleInGroup(int64_t groupId) const
{
    std::map<int64_t, GroupBrief>::const_iterator it = m_groupInfo.find(groupId);
    if (it != m_groupInfo.end())
    {
        if (it->second.m_myrole >= 0)
        {
            return it->second.m_myrole;   // 群资料已带回的权威角色
        }
        // 角色未知但群主 id 已知：群主可直接判出。
        const int64_t selfId = (m_lpFMGClient != NULL)
            ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
        if (selfId != 0 && it->second.m_ownerid == selfId)
        {
            return kGroupRoleOwner;
        }
    }
    return kGroupRoleMember;   // 默认按普通成员（不显示群管理入口）
}

void CBalloonMainPanel::OpenGroupManage(int64_t groupId)
{
    if (groupId <= 0)
    {
        return;
    }

    // 门控：仅群主 / 管理员可打开。
    const int role = MyRoleInGroup(groupId);
    if (role != kGroupRoleOwner && role != kGroupRoleAdmin)
    {
        ::MessageBox(m_hWnd, _T("仅群主或管理员可管理本群。"), _T("FlamingoIM"),
                     MB_OK | MB_ICONINFORMATION);
        return;
    }

    // 同群已开 → 前置；他群已开 → 关旧开新；对象在但窗口已关 → 惰性回收。
    if (m_pGroupManageWindow != NULL)
    {
        if (m_pGroupManageWindow->IsWindow())
        {
            if (m_groupManageGroupId == groupId)
            {
                if (m_pGroupManageWindow->IsIconic())
                {
                    m_pGroupManageWindow->ShowWindow(SW_RESTORE);
                }
                ::SetForegroundWindow(m_pGroupManageWindow->m_hWnd);
                return;
            }
            m_pGroupManageWindow->DestroyWindow();
        }
        delete m_pGroupManageWindow;
        m_pGroupManageWindow = NULL;
        m_groupManageGroupId = 0;
    }

    // 新建绑定该群的群管理对话框。传 NULL 作 hMainDlg：关闭只销毁自身、不退程序。
    CBalloonGroupManageWindow* pWin = new CBalloonGroupManageWindow(m_lpFMGClient);
    pWin->SetGroupContext(groupId, GroupDisplayName(groupId), role);
    pWin->CreateAndShow(NULL);
    if (!pWin->IsWindow())
    {
        delete pWin;
        return;
    }
    m_pGroupManageWindow = pWin;

    // 让群管理窗知道宿主是谁 —— 它的成员右键菜单要借主面板打开单聊窗 / 资料窗。
    pWin->SetHostPanel(m_hWnd);

    // 窗口已建好，开始拉真实数据（群资料 / 成员 / 待审批申请三条并发发出）。
    // 必须在 CreateAndShow 之后调 —— 应答回来时要往控件树里填内容，控件树得先在。
    pWin->RequestGroupData();
    m_groupManageGroupId = groupId;
}

void CBalloonMainPanel::ShowGroupContextMenu(int64_t groupId)
{
    const int role = MyRoleInGroup(groupId);

    // 置顶状态取自群列表：它随群列表应答下发、本端切换与多端同步时都会更新，没有消息的
    // 空群也有，与群 Tab 上画出来的置顶样式同一来源。免打扰仍取自会话缓存，空群查不到
    // 就按"未免打扰"显示，用户点了照样发协议生效（下次会话同步会把状态带回来）。
    const bool pinned = balloonmain::IsUserGroupPinned(m_myGroups, groupId);
    bool muted = false;
    const net::ConversationItem* pConv =
        balloonmain::FindConversation(m_conversations, 2, groupId);
    if (pConv != NULL)
    {
        muted = (pConv->m_mute != 0);
    }

    // 组装菜单（结构对照 design/群聊右键菜单.png，行模型见 MainPanelGroupMenu.h）。本轮文字版：
    // 退出群聊暂无法标红（DuiMenu 不支持单项配色），图标待补资源；"群管理"仅群主/管理员显示。
    balloonmain::GroupMenuState state;
    state.m_canManage = (role == kGroupRoleOwner || role == kGroupRoleAdmin);
    state.m_isOwner   = (role == kGroupRoleOwner);
    state.m_pinned    = pinned;
    state.m_muted     = muted;
    state.m_favorite  = m_favorites.IsFavorite(2, groupId);
    const std::vector<balloonmain::GroupMenuEntry> rows = balloonmain::BuildGroupMenuModel(state);
    balloonwjui::DuiMenu menu;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].m_isSeparator)
        {
            menu.AppendSeparator();
        }
        else
        {
            menu.AppendItem(rows[i].m_id, rows[i].m_label);
        }
    }

    // 弹出前把落点夹进显示器工作区（DuiMenu 不自动翻转，见 PopupMenuPlacement.h）。
    POINT pt = { 0, 0 };
    ::GetCursorPos(&pt);
    POINT cpos = balloonmain::ClampPopupMenuToWorkArea(pt, menu.MeasureSize());
    UINT chosen = menu.TrackPopup(cpos.x, cpos.y, m_hWnd);   // 同步阻塞

    switch (chosen)
    {
    //进入群聊：打开群聊窗
    case balloonmain::kGroupMenuEnter:
        OpenGroupChat(groupId);
        break;

    //群管理：仅群主 / 管理员的菜单里有这一项
    case balloonmain::kGroupMenuManage:
        OpenGroupManage(groupId);
        break;

    //查看群资料
    case balloonmain::kGroupMenuViewInfo:
        OpenGroupInfo(groupId);
        break;

    //查看历史消息：打开消息记录窗口并定位到本群
    case balloonmain::kGroupMenuViewHistory:
        OpenHistory(2, groupId);
        break;

    //添加到收藏 / 取消收藏
    case balloonmain::kGroupMenuFavorite:
        ToggleFavorite(2, groupId);
        break;

    //复制群号 / 复制群名：写入剪贴板
    case balloonmain::kGroupMenuCopyGroupNo:
    case balloonmain::kGroupMenuCopyGroupName:
        CopyGroupInfoText(groupId, chosen == balloonmain::kGroupMenuCopyGroupNo);
        break;

    //置顶群聊 / 取消置顶：菜单文案是按当前状态取反给出的，故这里直接取反写入
    case balloonmain::kGroupMenuPin:
        SetConversationPinnedState(2, groupId, !pinned);
        break;

    //消息免打扰 / 取消免打扰
    case balloonmain::kGroupMenuMute:
        SetConversationMuteState(2, groupId, !muted);
        break;

    //退出群聊：群主走"先转让群主再退出"，其余成员确认后直接退群
    case balloonmain::kGroupMenuQuit:
        RequestLeaveGroup(groupId, m_hWnd);
        break;

    //解散群聊：仅群主的菜单里有这一项
    case balloonmain::kGroupMenuDismiss:
        RequestDismissGroup(groupId, m_hWnd);
        break;

    case 0:
        break;   // 未选中（失焦 / ESC）

    default:
        // 清空聊天记录待接后端。
        ::MessageBox(m_hWnd, _T("该功能正在开发中。"), _T("FlamingoIM"),
                     MB_OK | MB_ICONINFORMATION);
        break;
    }
}

void CBalloonMainPanel::ShowConversationContextMenu(int ctype, int64_t cid)
{
    if (cid <= 0)
    {
        return;
    }

    // 菜单内容取决于这条会话此刻的状态（类型 / 置顶 / 免打扰 / 未读数）。会话一定
    // 在缓存里（菜单是从会话行右键出来的），查不到则按默认状态兜底、不至于弹不出菜单。
    balloonmain::ConversationMenuState state;
    state.m_isGroup = (ctype == 2);
    const net::ConversationItem* pConv =
        balloonmain::FindConversation(m_conversations, ctype, cid);
    if (pConv != NULL)
    {
        state.m_pinned = (pConv->m_pinned != 0);
        state.m_muted  = (pConv->m_mute != 0);
        state.m_unread = pConv->m_unread;
    }
    //收藏状态与会话缓存无关，直接问收藏模型。会话的 (ctype,cid) 与收藏的 (ctype,cid)
    //是同一套口径，可以直接传。
    state.m_favorite = m_favorites.IsFavorite(ctype, cid);
    //特别关注只对人有效，群会话上这一项根本不出现（由行模型据 m_isGroup 决定），
    //这里也就只在单聊时去问一次名单。
    state.m_starred  = (ctype == 1) && m_starred.IsStarred(cid);

    balloonwjui::DuiMenu menu;

    // 各行照行模型逐条追加：分隔条走 AppendSeparator，灰显项走 AppendDisabled。
    std::vector<balloonmain::ConversationMenuEntry> rows =
        balloonmain::BuildConversationMenuModel(state);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].m_isSeparator)
        {
            menu.AppendSeparator();
        }
        else if (rows[i].m_enabled)
        {
            menu.AppendItem(rows[i].m_id, rows[i].m_label);
        }
        else
        {
            menu.AppendDisabled(rows[i].m_id, rows[i].m_label);
        }
    }

    // 弹出前把落点夹进显示器工作区（DuiMenu 不自动翻转，见 PopupMenuPlacement.h）。
    // 会话行经常靠近面板底边，不夹取的话菜单会掉到桌面外面去。
    POINT pt = { 0, 0 };
    ::GetCursorPos(&pt);
    POINT cpos = balloonmain::ClampPopupMenuToWorkArea(pt, menu.MeasureSize());
    UINT chosen = menu.TrackPopup(cpos.x, cpos.y, m_hWnd);   // 同步阻塞

    switch (chosen)
    {
    case balloonmain::kConvMenuOpen:
        // 与左键单击会话行同一套动作：开窗 + 清未读。
        OpenConversation(ctype, cid);
        break;

    case balloonmain::kConvMenuMarkRead:
        // 只清未读、不开窗（内部会上报已读位点并让托盘停闪）。
        MarkConversationRead(ctype, cid);
        break;

    case balloonmain::kConvMenuHistory:
        OpenHistory(ctype, cid);
        break;

    case balloonmain::kConvMenuProfile:
        OpenConversationProfile(ctype, cid);
        break;

    case balloonmain::kConvMenuFavorite:
        ToggleFavorite(ctype, cid);
        break;

    // 特别关注：只有单聊会话会出这一项（群会话的行模型里压根没有它），故 cid
    // 就是对端 userid，可以直接传。
    case balloonmain::kConvMenuStarred:
        ToggleStarred(cid);
        break;

    case balloonmain::kConvMenuPin:
        // 菜单文案是按当前状态取反给出的，故这里直接取反写入。
        SetConversationPinnedState(ctype, cid, !state.m_pinned);
        break;

    case balloonmain::kConvMenuMute:
        SetConversationMuteState(ctype, cid, !state.m_muted);
        break;

    case balloonmain::kConvMenuDelete:
        DeleteConversation(ctype, cid);
        break;

    //复制联系人名（单聊会话）：复制列表上显示的名字（备注优先，其次昵称）
    case balloonmain::kConvMenuCopyName:
        CopyContactName((UINT)cid);
        break;

    //复制群号 / 复制群名（群会话）
    case balloonmain::kConvMenuCopyGroupNo:
    case balloonmain::kConvMenuCopyGroupName:
        CopyGroupInfoText(cid, chosen == balloonmain::kConvMenuCopyGroupNo);
        break;

    case balloonmain::kConvMenuDeleteAll:
        DeleteAllConversations();
        break;

    case 0:
        break;   // 未选中（失焦 / ESC）

    default:
        break;   // 行模型里没有别的项，落到这里说明 id 对不上，静默忽略
    }
}

void CBalloonMainPanel::CopyContactName(UINT peerUserId)
{
    const CString name = ContactDisplayName(peerUserId);
    const bool ok = flamingo::CopyTextToClipboard((LPCTSTR)name);
    LOG_INFO("context menu: copy contact name, userid=%u, length %d, ok=%d.",
             peerUserId, name.GetLength(), (int)ok);
}

void CBalloonMainPanel::CopyGroupInfoText(int64_t groupId, bool copyNumber)
{
    const CString text = copyNumber ? CString(balloonmain::FormatGroupNumber(groupId).c_str())
                                    : GroupDisplayName(groupId);
    const bool ok = flamingo::CopyTextToClipboard((LPCTSTR)text);
    LOG_INFO("context menu: copy group %s, groupid=%lld, length %d, ok=%d.",
             copyNumber ? "number" : "name", (long long)groupId, text.GetLength(), (int)ok);
}

void CBalloonMainPanel::OpenConversationProfile(int ctype, int64_t cid)
{
    if (ctype == 2)
    {
        OpenGroupInfo(cid);
    }
    else
    {
        OpenContactProfile((UINT)cid);
    }
}

void CBalloonMainPanel::DeleteConversation(int ctype, int64_t cid)
{
    if (cid <= 0)
    {
        return;
    }

    // 二次确认：删除在服务端是不可逆的（会话被标记成已删除，只有该会话再来新消息
    // 才会重新出现），误点代价不小，故先问一句。提示语对单聊 / 群聊通用。
    CString  title;
    CString  glyph;
    COLORREF avColor = RGB(0, 0, 0);
    ComputeConvDisplay(ctype, cid, title, glyph, avColor);

    CString prompt;
    prompt.Format(_T("确定要删除会话“%s”吗？\n\n只会把它从最近会话列表移除，聊天记录不会被删除。"),
                  (LPCTSTR)title);
    if (::MessageBox(m_hWnd, prompt, _T("删除会话"),
                     MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }

    // 上报服务端（应答经 cmd_set_conversation 回投本面板记日志 / 失败提示）。
    CIUSocket::GetInstance().SendDeleteConversation(ctype, cid);

    // 乐观更新：本地移除该会话并重填列表。若服务端最终失败，下次会话同步会把它带回。
    const size_t removed = balloonmain::RemoveConversation(m_conversations, ctype, cid);

    // 被删的会话若正在托盘闪烁，要一并停掉——否则托盘上还在闪一个列表里已经没有的会话，
    // 用户点开会一脸茫然。
    PostTrayClear(ctype, cid);

    if (removed > 0)
    {
        // 重填会话树（内部顺带刷新"消息"Tab 的未读总数角标）。
        FillConversationsTree();
    }

    LOG_INFO("删除会话: ctype=%d, cid=%lld, 本地移除 %u 条。",
             ctype, (long long)cid, (uint32_t)removed);
}

void CBalloonMainPanel::DeleteAllConversations()
{
    if (m_conversations.empty())
    {
        return;   // 列表本来就是空的，没什么可删，也不必弹确认框
    }

    CString prompt;
    prompt.Format(_T("确定要删除全部 %u 个会话吗？\n\n只会清空最近会话列表，聊天记录不会被删除。"),
                  (uint32_t)m_conversations.size());
    if (::MessageBox(m_hWnd, prompt, _T("删除全部会话"),
                     MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
    {
        return;
    }

    // 先把各会话标识快照下来 —— 下面要清空 m_conversations，清完就取不到了。
    const std::vector<balloonmain::ConversationKey> keys =
        balloonmain::CollectConversationKeys(m_conversations);

    // 服务端没有"批量删除会话"的命令，只能逐条发 cmd_set_conversation{deleted:true}。
    for (size_t i = 0; i < keys.size(); ++i)
    {
        CIUSocket::GetInstance().SendDeleteConversation(keys[i].m_ctype, keys[i].m_cid);
        PostTrayClear(keys[i].m_ctype, keys[i].m_cid);
    }

    m_conversations.clear();
    FillConversationsTree();

    LOG_INFO("删除全部会话: 共 %u 条。", (uint32_t)keys.size());
}

void CBalloonMainPanel::SetConversationPinnedState(int ctype, int64_t cid, bool pinned)
{
    if (cid <= 0)
    {
        return;
    }

    // 乐观更新本地缓存。置顶会改变排序，SetConversationPinned 内部改完就重排；返回
    // false 表示状态没变（或会话不存在），那就不必重填列表。
    if (balloonmain::SetConversationPinned(m_conversations, ctype, cid, pinned))
    {
        FillConversationsTree();
    }

    // 群的置顶还要同步到群 Tab。没有消息的空群不在会话缓存里，上面那一步改不到它，
    // 群 Tab 只认群列表里的置顶状态，所以两份各改各的。
    const int kCtypeGroup = 2;
    if (ctype == kCtypeGroup && balloonmain::SetUserGroupPinned(m_myGroups, cid, pinned))
    {
        FillGroupsTree();
    }

    CIUSocket::GetInstance().SendSetConversation(ctype, cid,
                                                 /*hasPinned*/true, pinned,
                                                 /*hasMute*/false, false);
    LOG_INFO("会话置顶设置: ctype=%d, cid=%lld, pinned=%d。",
             ctype, (long long)cid, (int)pinned);
}

void CBalloonMainPanel::SetConversationMuteState(int ctype, int64_t cid, bool mute)
{
    if (cid <= 0)
    {
        return;
    }

    // 免打扰不影响排序，但会话行右下角的静音标记与未读胶囊配色要跟着变，故仍要重填。
    if (balloonmain::SetConversationMuted(m_conversations, ctype, cid, mute))
    {
        FillConversationsTree();
    }

    CIUSocket::GetInstance().SendSetConversation(ctype, cid,
                                                 /*hasPinned*/false, false,
                                                 /*hasMute*/true, mute);
    LOG_INFO("会话免打扰设置: ctype=%d, cid=%lld, mute=%d。",
             ctype, (long long)cid, (int)mute);
}

// ====================================================================
// 群管理（应答与推送的转交）
// ====================================================================
//
// 主面板本身不显示群管理内容，这一组方法的职责是把 socket 层来的消息转交给
// 群管理窗口 —— 窗口是主面板创建并持有的（m_pGroupManageWindow），而 socket
// 层的消息只投给本面板，所以必须经这一跳。窗口没开着时直接丢弃即可，
// 下次打开会重新拉一遍数据。

// 取当前打开着的、且绑定在指定群上的群管理窗口；没有则返回 NULL。
// groupId 传 0 表示"不校验群，只要窗口开着就行"。
CBalloonGroupManageWindow* CBalloonMainPanel::GetLiveGroupManageWindow(int64_t groupId)
{
    if (m_pGroupManageWindow == NULL)
    {
        return NULL;
    }
    if (!m_pGroupManageWindow->IsWindow())
    {
        return NULL;
    }
    if (groupId > 0 && m_pGroupManageWindow->GetGroupId() != groupId)
    {
        //窗口开着，但管的是别的群 —— 这条应答不是给它的
        return NULL;
    }
    return m_pGroupManageWindow;
}

// 群名变了：会话列表里那一行、以及开着的群聊窗标题都要跟着改。
//   groupId：  群ID；
//   nameUtf8： 新群名（UTF-8，来自协议层）。
void CBalloonMainPanel::UpdateGroupNameEverywhere(int64_t groupId, const std::string& nameUtf8)
{
    if (nameUtf8.empty())
    {
        return;
    }

    const CString name = Utf8ToCStr(nameUtf8);
    if (name.IsEmpty())
    {
        return;
    }

    //1) 群资料缓存。会话列表行与"群"Tab 的显示名都从这里取（net::ConversationItem
    //   本身不带名字，只带 ctype/cid），所以改这一处两棵树就都跟着变了。
    bool changed = false;
    std::map<int64_t, GroupBrief>::iterator git = m_groupInfo.find(groupId);
    if (git != m_groupInfo.end() && git->second.m_name != name)
    {
        git->second.m_name = name;
        changed = true;
    }

    //2) "我的群"列表（"群"Tab 的数据源之一）
    for (size_t i = 0; i < m_myGroups.size(); ++i)
    {
        if (m_myGroups[i].m_groupid != groupId)
            continue;
        if (m_myGroups[i].m_name != nameUtf8)
        {
            m_myGroups[i].m_name = nameUtf8;
            changed = true;
        }
        break;
    }

    if (changed)
    {
        FillGroupsTree();
        FillConversationsTree();
    }

    //3) 开着的群聊窗标题
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator wit = m_groupChatWindows.find(groupId);
    if (wit != m_groupChatWindows.end() && wit->second != NULL && wit->second->IsWindow())
    {
        wit->second->SetWindowText((LPCTSTR)name);
    }
}

void CBalloonMainPanel::OnGroupOpResult(int cmd, const net::GroupOpResult& result)
{
    //改群头像与「保存修改」共用 cmd_update_group_info(1311)，靠"修改头像对话框
    //是不是正等着这条应答"把两者分开：是的话由对话框独占处理（成功关窗、失败
    //就地提示），不再转给群管理窗 —— 否则同一次失败会弹两个提示框。
    const int kCmdUpdateGroupInfo = 1311;
    if (cmd == kCmdUpdateGroupInfo && m_modifyAvatarDlg.IsWaitingGroupAvatarResult())
    {
        net::UpdateProfileResponse resp;
        resp.code = result.m_code;
        resp.msg  = result.m_msg;
        m_modifyAvatarDlg.OnChangeAvatarResult(resp);
        LOG_INFO(_T("修改群头像应答已交给对话框处理，code=%d。"), result.m_code);
        return;
    }

    //解散与转让也可能由本面板发起（群资料窗口、群列表右键、群聊窗成员右键、群主退群的流程）：
    //先看本面板有没有登记过这种请求，有就由本面板提示结果；没有才是群管理窗发起的，照旧转交
    if (cmd == cmd_dismiss_group || cmd == cmd_transfer_owner)
    {
        const balloonmain::GroupExitOp op = (cmd == cmd_dismiss_group)
                                            ? balloonmain::kGroupExitOpDismiss
                                            : balloonmain::kGroupExitOpTransfer;
        const balloonmain::GroupOpOutcome outcome = m_groupExitTracker.OnResult(op, result.m_code);
        if (outcome.m_matched)
        {
            ApplyGroupExitOutcome(op, outcome);
            return;
        }
    }

    //写操作的应答体里没有 groupid，无从校验是哪个群的；但同一时刻只可能开着
    //一个群管理窗口，且其余操作也只能从它发起，故直接转交。
    CBalloonGroupManageWindow* pWin = GetLiveGroupManageWindow(0);
    if (pWin == NULL)
    {
        LOG_INFO(_T("收到群管理操作应答但窗口已关闭，忽略。cmd=%d, code=%d。"),
                 cmd, result.m_code);
        return;
    }
    pWin->OnGroupOpResult(cmd, result.m_code, result.m_msg);
}

void CBalloonMainPanel::OnGroupJoinRequests(const net::GroupJoinRequestsResult& result)
{
    CBalloonGroupManageWindow* pWin = GetLiveGroupManageWindow(result.m_groupid);
    if (pWin == NULL)
    {
        return;
    }
    pWin->OnJoinRequestsArrived(result);
}

void CBalloonMainPanel::OnSensitiveWords(const net::SensitiveWordsResult& result)
{
    CBalloonGroupManageWindow* pWin = GetLiveGroupManageWindow(result.m_groupid);
    if (pWin == NULL)
    {
        return;
    }
    pWin->OnSensitiveWordsArrived(result);
}

void CBalloonMainPanel::OnGroupAnnouncements(const net::GroupAnnouncementsResult& result)
{
    //群管理窗与群聊窗可能同时开着、看的还是同一个群（一个在发公告、一个在看），
    //两处都要转交 —— 谁发的请求这条应答里看不出来，按群分发即可。
    CBalloonGroupManageWindow* pManageWin = GetLiveGroupManageWindow(result.m_groupid);
    if (pManageWin != NULL)
    {
        pManageWin->OnAnnouncementsArrived(result);
    }

    std::map<int64_t, CBalloonGroupChatWindow*>::iterator itChat =
        m_groupChatWindows.find(result.m_groupid);
    if (itChat != m_groupChatWindows.end() && itChat->second != NULL
        && itChat->second->IsWindow())
    {
        itChat->second->OnAnnouncementsArrived(result);
    }
}

void CBalloonMainPanel::OnAnnouncementChanged(const net::GroupAnnouncementNotify& notify)
{
    CBalloonGroupManageWindow* pManageWin = GetLiveGroupManageWindow(notify.m_groupid);
    if (pManageWin != NULL)
    {
        pManageWin->OnAnnouncementChanged(notify);
    }

    std::map<int64_t, CBalloonGroupChatWindow*>::iterator itChat =
        m_groupChatWindows.find(notify.m_groupid);
    if (itChat != m_groupChatWindows.end() && itChat->second != NULL
        && itChat->second->IsWindow())
    {
        itChat->second->OnAnnouncementChanged(notify);

        //头部那行公告摘要要跟着变。它平时读的是群资料里的公告快照，而服务端在推
        //1326 之后还会推一条 1318（快照四列确实变了），照理会刷新到；但两条推送到达
        //之间有一小段时间，这里先按推送里那条公告就地更新，免得摘要短暂对不上。
        if (notify.m_changetype == group_announcement_change_publish)
        {
            itChat->second->SetAnnouncementTitle(
                Utf8ToCStr(notify.m_announcement.m_title));
        }
    }

    //要不要弹窗由 AnnouncePopupPolicy 判定：自己发的不弹、撤回不弹、断线重连后
    //重复推来的同一条不弹、乱序补到的更旧的一条也不弹。
    const CString configPath = GetUserConfigPath();

    balloongroupmanage::AnnouncePopupInput input;
    input.m_changetype   = notify.m_changetype;
    input.m_announceId   = notify.m_announcement.m_id;
    input.m_publisherId  = notify.m_announcement.m_publisher;
    input.m_selfUserId   = (m_lpFMGClient != NULL)
                         ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    input.m_lastPoppedId = balloongroupmanage::LoadLastPoppedAnnounceId(
        configPath, notify.m_groupid);

    if (balloongroupmanage::ShouldPopupAnnouncement(input))
    {
        //群名从群信息缓存里取；缓存里没有（刚入群还没拉过资料）时留空，
        //弹窗顶部那行就不显示群名，其余照常。
        CString groupName;
        std::map<int64_t, GroupBrief>::iterator itBrief = m_groupInfo.find(notify.m_groupid);
        if (itBrief != m_groupInfo.end())
        {
            groupName = itBrief->second.m_name;
        }

        const CString timeText = balloongroupmanage::FormatAnnouncementTime(
            notify.m_announcement.m_publishtime, NowMillisecondsForPanel());

        m_announcePopupDlg.ShowAnnouncement(
            m_hWnd, notify.m_groupid, groupName,
            Utf8ToCStr(notify.m_announcement.m_title),
            Utf8ToCStr(notify.m_announcement.m_content),
            notify.m_announcement.m_severity,
            Utf8ToCStr(notify.m_announcement.m_publishername),
            timeText);

        //弹过就记账，重启客户端后也不会把这条再弹一遍
        balloongroupmanage::SaveLastPoppedAnnounceId(
            configPath, notify.m_groupid, notify.m_announcement.m_id);
    }

    LOG_INFO("收到群公告变更推送: groupid=%lld, changetype=%d, id=%lld。",
             (long long)notify.m_groupid, notify.m_changetype,
             (long long)notify.m_announcement.m_id);
}

CString CBalloonMainPanel::GetUserConfigPath() const
{
    if (m_lpFMGClient == NULL || m_lpFMGClient->m_UserMgr.m_UserInfo.m_strAccount.empty())
    {
        return CString();
    }

    //与 CFlamingoClient::LoadUserConfig / SaveUserConfig 用同一个函数拼路径 ——
    //几处必须指同一个文件，否则记的账下次读不回来。
    const std::wstring configPath = net::UserConfigFilePath(g_szDataRoot,
                                                            m_lpFMGClient->m_UserMgr.m_UserInfo.m_strAccount);
    return CString(configPath.c_str());
}

LRESULT CBalloonMainPanel::OnOpenGroupAnnounceMsg(UINT /*uMsg*/, WPARAM wParam,
                                                  LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    const int64_t groupId = (int64_t)wParam;
    if (groupId <= 0)
    {
        return 0;
    }

    //先把群聊窗开出来（已开着则前置），再切到公告边栏
    OpenGroupChat(groupId);

    std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.find(groupId);
    if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
    {
        it->second->ActivateNoticeTab();
    }

    LOG_INFO("新公告提示窗请求查看全部: groupid=%lld。", (long long)groupId);
    return 0;
}

void CBalloonMainPanel::OnGroupMemberChanged(const net::GroupMemberChangedNotify& notify)
{
    //1) 群管理窗口开着且管的就是这个群 —— 让它重拉成员列表
    CBalloonGroupManageWindow* pWin = GetLiveGroupManageWindow(notify.m_groupid);
    if (pWin != NULL)
    {
        pWin->OnMemberChanged(notify);
    }

    //本次是否已经为刷新本人角色补拉过群资料，避免下面两处各发一次
    bool infoRequested = false;

    //2) 该群的聊天窗开着 —— 让它重拉成员边栏（@提醒 的候选、成员数标题都靠它）
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator itChat =
        m_groupChatWindows.find(notify.m_groupid);
    if (itChat != m_groupChatWindows.end() && itChat->second != NULL
        && itChat->second->IsWindow())
    {
        CIUSocket::GetInstance().RequestGroupMembers(notify.m_groupid);

        //角色变更还要补拉一次群资料：本人的角色只在 cmd_get_group_info 应答里回填
        //（成员列表应答不带 myrole），少了这一跳，别人把我提成管理员时右上角的
        //「群管理」齿轮不会出现、@所有人 的权限也不会跟着放开，得重开窗口才生效。
        //不去判断变更名单里有没有本人 —— 这是一条很小的短包，判断的收益不抵漏判的代价。
        const int kMemberChangeRole = 4;
        if (notify.m_changetype == kMemberChangeRole)
        {
            LOG_INFO(_T("群 %lld 有成员角色变更，补拉群资料以刷新本人角色。"),
                     (long long)notify.m_groupid);
            CIUSocket::GetInstance().RequestGroupInfo(notify.m_groupid);
            infoRequested = true;
        }
    }

    //2.5) 名单里有本人的角色变更（被设为 / 撤销管理员、转让群主时的新旧群主）：不管聊天窗
    //     开没开都补拉一次群资料，应答到达后 OnGroupInfoArrived 会更新本人角色的缓存
    //     （m_groupInfo[群].m_myrole）。主面板群右键菜单里的「群管理」、打开群管理窗前的权限
    //     判断、之后打开聊天窗时右上角的齿轮都按这份缓存走，不补拉就要等重新登录才更新
    if (!infoRequested
        && grouprole::ShouldRefetchMyRoleOnMemberChange(notify.m_changetype, notify.m_userids,
                                                        (int64_t)SelfUserId()))
    {
        LOG_INFO("Group %lld: my role changed, refetch group info to refresh the cached role.",
                 (long long)notify.m_groupid);
        CIUSocket::GetInstance().RequestGroupInfo(notify.m_groupid);
    }

    //3) 本人被移出本群：把该群的会话从列表里去掉，并关掉它的聊天窗。
    //   服务端在移出后仍会把这条推送发给被移出者本人（fan-out 时他还在成员表里），
    //   这是本端得知"我被踢了"的唯一途径。
    const int kMemberChangeKick  = 3;
    const int kMemberChangeLeave = 2;
    if (notify.m_changetype == kMemberChangeKick || notify.m_changetype == kMemberChangeLeave)
    {
        const int64_t myId = (m_lpFMGClient != NULL)
                             ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
        for (size_t i = 0; i < notify.m_userids.size(); ++i)
        {
            if (notify.m_userids[i] != myId || myId == 0)
                continue;

            LOG_INFO(_T("本人已离开群 %lld（changetype=%d），清理该群的会话与窗口。"),
                     (long long)notify.m_groupid, notify.m_changetype);
            HandleLeftGroup(notify.m_groupid,
                            notify.m_changetype == kMemberChangeKick ? balloonmain::kLeaveReasonKicked
                                                                     : balloonmain::kLeaveReasonSelf,
                            std::wstring(), true);
            break;
        }
    }
}

void CBalloonMainPanel::OnGroupInfoChanged(const net::GroupInfoChangedNotify& notify)
{
    const int64_t groupId = notify.m_info.m_groupid;
    const int kInfoChangeDismissed = 2;

    //群被解散：会话、聊天窗、群管理窗全都要收拾掉
    if (notify.m_changetype == kInfoChangeDismissed)
    {
        LOG_INFO(_T("群 %lld 已被解散，清理其会话与窗口。"), (long long)groupId);
        //本人是群主：解散是本人发起的（从群管理窗发起时本面板没有登记，走到这里），提示"已解散"
        const balloonmain::GroupLeaveReason reason = (MyRoleInGroup(groupId) == kGroupRoleOwner)
                                                     ? balloonmain::kLeaveReasonDismissedBySelf
                                                     : balloonmain::kLeaveReasonDismissed;
        bool shownByManageWindow = false;
        CBalloonGroupManageWindow* pWin = GetLiveGroupManageWindow(groupId);
        if (pWin != NULL)
        {
            //窗口会自行提示并销毁自己；它提示过，本面板就不再提示。对象本身留给下次打开群管理
            //时回收（OpenGroupManage 的"对象在但窗口已关"分支），不在这里置空，否则对象会泄漏
            shownByManageWindow = pWin->OnInfoChanged(notify);
        }
        HandleLeftGroup(groupId, reason, std::wstring(), !shownByManageWindow);
        return;
    }

    //普通资料变更：群名可能变了，会话列表与聊天窗标题都要跟着改
    UpdateGroupNameEverywhere(groupId, notify.m_info.m_name);

    //群头像也可能变了（谁改了群头像，全群都收到这条推送）。这是别人改完之后
    //本端能立刻看到的唯一路径 —— 缓存写回后重填两棵树，头像即时生效。
    std::map<int64_t, GroupBrief>::iterator briefIt = m_groupInfo.find(groupId);
    if (briefIt != m_groupInfo.end())
    {
        const bool avatarChanged =
            (briefIt->second.m_facetype != notify.m_info.m_facetype
             || briefIt->second.m_customface != notify.m_info.m_customface);
        if (avatarChanged)
        {
            briefIt->second.m_facetype   = notify.m_info.m_facetype;
            briefIt->second.m_customface = notify.m_info.m_customface;

            //群列表(1320)那份缓存里也存着头像两项，一并跟上 —— 它不会因为改头像
            //而重拉，留着旧值将来任何从它取头像的地方都会画错。
            for (size_t i = 0; i < m_myGroups.size(); ++i)
            {
                if (m_myGroups[i].m_groupid == groupId)
                {
                    m_myGroups[i].m_facetype   = notify.m_info.m_facetype;
                    m_myGroups[i].m_customface = notify.m_info.m_customface;
                    break;
                }
            }

            FillGroupsTree();
            FillConversationsTree();
            LOG_INFO(_T("群 %lld 的群头像已变更，列表头像已刷新。"), (long long)groupId);
        }
    }

    //开着的群聊窗：标题栏与任务栏图标跟着换（值没变时 SetGroupAvatar 内部跳过）。
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator avWit =
        m_groupChatWindows.find(groupId);
    if (avWit != m_groupChatWindows.end() && avWit->second != NULL && avWit->second->IsWindow())
    {
        avWit->second->SetGroupAvatar(notify.m_info.m_facetype, notify.m_info.m_customface);
    }

    CBalloonGroupManageWindow* pWin = GetLiveGroupManageWindow(groupId);
    if (pWin != NULL)
    {
        pWin->OnInfoChanged(notify);
    }

    //群聊窗头部那行公告摘要也跟着刷新。公告发布 / 撤回后服务端会推一条本消息
    //（t_group 上的公告快照四列确实变了），撤回最新那条时摘要要退回上一条、
    //撤光了要整行消失 —— 这条路径就是它唯一的更新来源。
    std::map<int64_t, CBalloonGroupChatWindow*>::iterator itChat =
        m_groupChatWindows.find(groupId);
    if (itChat != m_groupChatWindows.end() && itChat->second != NULL
        && itChat->second->IsWindow())
    {
        itChat->second->SetAnnouncementTitle(Utf8ToCStr(notify.m_info.m_announcetitle));
    }
}

void CBalloonMainPanel::OnJoinGroupRequestNotify(const net::GroupJoinRequestItem& item)
{
    CBalloonGroupManageWindow* pWin = GetLiveGroupManageWindow(item.m_groupid);
    if (pWin == NULL)
    {
        //窗口没开着：待处理数量下次打开时会重新拉到，这里只记一条日志。
        //（在群聊窗或会话行上显示"有待审批"的红点留待后续。）
        LOG_INFO(_T("收到加群申请推送，群管理窗未打开，groupid=%lld。"),
                 (long long)item.m_groupid);
        return;
    }
    pWin->OnJoinRequestNotify();
}

void CBalloonMainPanel::OnConversationChanged(const net::ConversationChangedNotify& notify)
{
    if (notify.m_cid <= 0)
    {
        return;
    }

    // 已删除：本地移除并停掉该会话的托盘闪烁。发起改动的那一端自己也会收到这条推送，
    // 但它在发请求时就已经移除过了，这里 RemoveConversation 返回 0、不再重绘，等于空过。
    if (notify.m_deleted != 0)
    {
        const size_t removed =
            balloonmain::RemoveConversation(m_conversations, notify.m_ctype, notify.m_cid);
        PostTrayClear(notify.m_ctype, notify.m_cid);
        if (removed > 0)
        {
            FillConversationsTree();
        }
        LOG_INFO("会话多端同步(删除): ctype=%d, cid=%lld, 本地移除 %u 条。",
                 notify.m_ctype, (long long)notify.m_cid, (uint32_t)removed);
        return;
    }

    // 未删除：按推送里的置顶 / 免打扰覆盖本地状态。两个 Set 在"本地没有这条会话"或
    // "状态本来就一样"时都返回 false —— 前者是本端还没同步到该会话（不凭空造空壳行），
    // 后者是发起端收到自己那条推送的常见情形，两种都无需重绘。
    bool changed = false;
    if (balloonmain::SetConversationPinned(m_conversations, notify.m_ctype, notify.m_cid,
                                           notify.m_pinned != 0))
    {
        changed = true;
    }
    if (balloonmain::SetConversationMuted(m_conversations, notify.m_ctype, notify.m_cid,
                                          notify.m_mute != 0))
    {
        changed = true;
    }
    if (changed)
    {
        FillConversationsTree();
        LOG_INFO("会话多端同步(置顶/免打扰): ctype=%d, cid=%lld, pinned=%d, mute=%d。",
                 notify.m_ctype, (long long)notify.m_cid, notify.m_pinned, notify.m_mute);
    }

    // 群的置顶同步到群 Tab。与会话缓存不同，群列表里总有这个群（哪怕是没有消息的空群），
    // 所以另一台设备置顶空群时，本端的群 Tab 也能跟着变。
    const int kCtypeGroup = 2;
    if (notify.m_ctype == kCtypeGroup
        && balloonmain::SetUserGroupPinned(m_myGroups, notify.m_cid, notify.m_pinned != 0))
    {
        FillGroupsTree();
        LOG_INFO("group pin synced from another device: groupid=%lld, pinned=%d.",
                 (long long)notify.m_cid, notify.m_pinned);
    }
}

void CBalloonMainPanel::OpenHistory(int ctype, int64_t cid, int initialTypeFilter)
{
    if (!EnsureMsgStoreOpen())
    {
        LOG_ERROR(_T("打开历史消息记录窗失败：本地消息库未就绪。"));
        return;
    }
    //对话名：群聊取群名，单聊取对端昵称。
    CString title;
    if (ctype == 2)
    {
        title = GroupDisplayName(cid);
    }
    else
    {
        title = ResolveSenderName(cid);
    }
    m_historyWindow.OpenForConversation(this, &m_msgStore, ctype, cid, title,
                                        initialTypeFilter);

    //按需回填：向服务端拉一次本会话最新消息（服务端单聊查询双向，含自己刚发出、还没
    //回流落库的）。应答经 OnSyncMessagesArrived 落库并刷新历史窗。
    const int kBackfillLimit = 100;   //回填条数（服务端上限 100）
    CIUSocket::GetInstance().RequestSyncMessages(ctype, cid, 0, kBackfillLimit);

    //再拉一次本会话被 Pin 的消息全集。上面那次回填只覆盖"最新 100 条"，而 Pin 的往往
    //恰恰是更早的老消息 —— 本地库里没有那一行，Pin 分类就会漏掉它。应答经
    //OnPinnedMessagesArrived 补齐消息体并与服务端对账。
    CIUSocket::GetInstance().GetPinnedMessages(ctype, cid);
}

CString CBalloonMainPanel::ResolveSenderName(int64_t userid) const
{
    //本人：取本人昵称。
    if (m_lpFMGClient != NULL
        && (UINT)userid == m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID)
    {
        CString self(m_lpFMGClient->m_UserMgr.m_UserInfo.m_strNickName.c_str());
        if (!self.IsEmpty())
        {
            return self;
        }
    }
    //联系人：与通讯录树 / 会话列表同一口径的「真名（备注）」—— 用户给谁起了备注，
    //在消息记录里看到的也该带上那个记号。口径收敛到 ResolveContactDisplayName 一处，
    //这里不再自己写一份逐级兜底，免得两处规则日后走岔。
    const net::ContactItem* pc = FindCachedContact((UINT)userid);
    if (pc != NULL)
    {
        return balloonmain::ResolveContactDisplayName(
            Utf8ToCStr(pc->m_remark), Utf8ToCStr(pc->m_nickname),
            Utf8ToCStr(pc->m_username), (UINT)userid);
    }

    //不在通讯录缓存里（已离职 / 资料还没到）：拼一个能对上号的兜底名。
    CString fallback;
    fallback.Format(_T("用户%lld"), (long long)userid);
    return fallback;
}

LRESULT CBalloonMainPanel::OnOpenHistoryMsg(UINT /*uMsg*/, WPARAM wParam,
                                            LPARAM lParam, BOOL& /*bHandled*/)
{
    //wParam 里塞了两样东西：低字节是会话类型，次低字节是"打开后要预选哪个类型胶囊"
    //（见 UserSessionData.h 里 FMG_MSG_OPEN_HISTORY 的约定）。聊天窗顶部的「已 Pin N 条」
    //提示带就靠这个次低字节把窗口直接开在 Pin 分类上，省得再加一条窗口消息。
    const int ctype = (int)(wParam & 0xFF);
    const int initialTypeFilter = (int)((wParam >> 8) & 0xFF);
    int64_t cid = 0;
    if (lParam != 0)
    {
        int64_t* pCid = (int64_t*)lParam;
        cid = *pCid;
        delete pCid;   //发送方堆分配，接收方释放（约定见 FMG_MSG_OPEN_HISTORY）
    }
    if (cid != 0)
    {
        OpenHistory(ctype, cid, initialTypeFilter);
    }
    return 0;
}

void CBalloonMainPanel::LocateMessageInChat(int ctype, int64_t cid, long long msgid)
{
    if (ctype == 2)
    {
        //群聊：打开 / 前置群聊窗，再让它定位。
        OpenGroupChat(cid);
        std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.find(cid);
        if (it != m_groupChatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            it->second->LocateMessage(msgid);
        }
    }
    else
    {
        //单聊：打开 / 前置单聊窗，再让它定位。
        OpenSingleChat((UINT)cid);
        std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.find((UINT)cid);
        if (it != m_chatWindows.end() && it->second != NULL && it->second->IsWindow())
        {
            it->second->LocateMessage(msgid);
        }
    }
}

void CBalloonMainPanel::ShowSystemMenu()
{
    // 取汉堡按钮的屏幕矩形作锚点（控件 rect 是 host 客户区坐标，转屏幕）。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiControl* menuBtn =
        content->FindCtrlById(balloonmain::kIdBottomMenu);
    if (menuBtn == NULL)
    {
        return;
    }

    // 按行模型装配 DuiMenu。图标用 CreateMenuIconImage 即时渲染，存进局部
    // icons 数组保活到 TrackPopup 返回（TrackPopup 同步阻塞，局部即可）——
    // DuiMenu 只借用图标指针，不持有所有权。
    balloonwjui::DuiMenu menu;
    std::vector<balloonmain::SystemMenuEntry> model = balloonmain::BuildSystemMenuModel();
    std::vector<std::unique_ptr<CImageEx> > icons;
    icons.reserve(model.size());

    for (size_t i = 0; i < model.size(); ++i)
    {
        const balloonmain::SystemMenuEntry& e = model[i];
        if (e.isSeparator)
        {
            menu.AppendSeparator();
            continue;
        }

        CImageEx* icon = balloonmain::CreateMenuIconImage(
            e.iconKind, kSystemMenuIconColor, kSystemMenuIconSize);
        icons.push_back(std::unique_ptr<CImageEx>(icon));   // icon 可能为 NULL，AppendItem 容忍
        menu.AppendItem(e.id, e.label, icon);
        if (!e.enabled)
        {
            menu.SetEnabled(e.id, false);
        }
    }

    // 锚定：菜单左对齐按钮左边、底边落在按钮顶边上方（菜单向上弹）。
    // DuiMenuPopup 把左上角放在传入坐标且不自动翻转，故须先用 MeasureSize
    // 拿到菜单高度，再把传入的 y 上移整个菜单高度。
    RECT  rc = menuBtn->GetRect();
    POINT tl = { rc.left, rc.top };
    ::ClientToScreen(m_hWnd, &tl);

    SIZE  menuSize = menu.MeasureSize();
    POINT desired = { tl.x, tl.y - menuSize.cy - kSystemMenuGap };

    // 再夹进显示器工作区兜底，保证整张菜单在桌面内（见 PopupMenuPlacement.h）。
    // 正常情况下菜单已在按钮上方、本就在屏内，此处只在越界时才生效。
    POINT pos = balloonmain::ClampPopupMenuToWorkArea(desired, menuSize);

    LOG_INFO("主面板系统菜单弹出：锚点(%d,%d) 菜单尺寸(%dx%d)。",
             pos.x, pos.y, (int)menuSize.cx, (int)menuSize.cy);

    UINT chosen = menu.TrackPopup(pos.x, pos.y, m_hWnd);   // 同步阻塞
    LOG_INFO("主面板系统菜单：选中项 id=%u。", chosen);

    // 按选中项分发。修改密码 / 系统设置 / 网络设置 / 关于 都转交给
    // 隐藏的 主面板现成的命令处理；个人信息走本类自己的窗口。
    switch (chosen)
    {
    case balloonmain::kSysMenuProfile:
        OpenProfileDlg();
        break;

    case balloonmain::kSysMenuChangePwd:
        OpenChangePwdDlg();
        break;

    case balloonmain::kSysMenuSettings:
        // 改为打开新版 balloonui 系统设置对话框（老 MFC CSystemSettingDlg 保留但本
        // 菜单不再指向它，与"修改密码"迁移到 balloonui 对话框同处理）。
        OpenSystemSettingDlg();
        break;

    case balloonmain::kSysMenuNetSettings:
        OpenNetSettings();
        break;

    case balloonmain::kSysMenuAbout:
        // 改为打开新版 balloonui 关于对话框（老 SkinLib CAboutDlg 保留但本菜单不再
        // 指向它，与"修改密码"/"系统设置"迁移到 balloonui 对话框同处理）。
        OpenAboutDlg();
        break;

    case balloonmain::kSysMenuLogout:
        // 登出交给本面板持有的会话控制器执行，且必须用 PostMessage：登出会重置本面板、
        // 并进入登录窗的模态循环，不能在本函数的调用栈上同步做（见
        // balloonmain::CSessionController::OnLogout 与 FMG_MSG_LOGOUT 的定义）。
        ::PostMessage(m_hWnd, FMG_MSG_LOGOUT, 0, 0);
        break;

    case balloonmain::kSysMenuAnnounce:
        // 「公告与通知」与 Banner 底部那一行是同一个去处 —— 平台公告的收件箱。
        // 两个入口指向同一个窗口，不各开一个。
        OpenAnnounceBox();
        break;

    case 0:
        break;   // 未选中（失焦 / ESC）

    default:
        break;
    }
}

void CBalloonMainPanel::OpenNetSettings(HWND hCloseTarget)
{
    // 已经开着就前置，不重复 Create —— ATL 的 CWindowImpl::Create 断言 m_hWnd 为空，
    // 对同一个成员窗口再建一次会在 Debug 下断言、Release 下丢掉旧句柄。
    if (m_netSettingsWindow.IsWindow())
    {
        m_netSettingsWindow.ShowWindow(SW_SHOW);
        ::SetForegroundWindow(m_netSettingsWindow.m_hWnd);
        return;
    }

    // 该参数决定关掉这个窗口时要不要请求退出进程：窗口在 WM_CLOSE 里往它投
    // WM_CLOSE_MAINDLG（见 CBalloonNetSettingsWindow::OnCloseMsg）。从系统菜单
    // 打开时为 NULL，关掉它只销毁它自己，与本面板开群管理窗 / 资料窗的口径一致。
    m_netSettingsWindow.CreateAndShow(hCloseTarget);
}

void CBalloonMainPanel::ShowStatusMenu()
{
    // 取状态胶囊的屏幕矩形作锚点（控件 rect 是 host 客户区坐标，转屏幕）。
    balloonmain::MainPanelHeader* header = FindMainPanelHeader(GetClientContent());
    if (header == NULL)
    {
        return;
    }
    RECT rcHit = header->GetStatusHitRect();
    if (::IsRectEmpty(&rcHit))
    {
        return;   // 胶囊尚未绘制（理论上点击必发生在绘制之后，这里只作防御）
    }

    // 按行模型装配 DuiMenu。当前所处的那一项由行模型标出（带外环的圆点 + "（当前）"
    // 后缀），当前状态取胶囊上正在显示的那个，两者必然一致。装配走共用函数
    // AppendStatusMenuItems，与登录窗状态菜单同一份代码；icons 数组保活到 TrackPopup
    // 返回（同步阻塞，局部即可）——DuiMenu 只借用图标指针、不持有所有权。
    balloonwjui::DuiMenu menu;
    std::vector<balloonmain::StatusMenuEntry> model =
        balloonmain::BuildStatusMenuModel(header->GetPresence());
    std::vector<std::unique_ptr<CImageEx> > icons;

    balloonmain::AppendStatusMenuItems(menu, icons, model, kSystemMenuIconSize);

    // 锚定：菜单左对齐胶囊左边、顶边落在胶囊底边下方（菜单向下弹）。再夹进显示器
    // 工作区兜底，保证整张菜单在桌面内（见 PopupMenuPlacement.h）。
    POINT tl = { rcHit.left, rcHit.bottom + kSystemMenuGap };
    ::ClientToScreen(m_hWnd, &tl);

    SIZE  menuSize = menu.MeasureSize();
    POINT pos = balloonmain::ClampPopupMenuToWorkArea(tl, menuSize);

    LOG_INFO("主面板状态菜单弹出：锚点(%d,%d) 菜单尺寸(%dx%d) 当前状态presence=%d。",
             pos.x, pos.y, (int)menuSize.cx, (int)menuSize.cy, header->GetPresence());

    UINT chosen = menu.TrackPopup(pos.x, pos.y, m_hWnd);   // 同步阻塞
    LOG_INFO("主面板状态菜单：选中项 id=%u。", chosen);

    if (chosen == 0)
    {
        return;   // 未选中（失焦 / ESC）
    }

    // 把菜单 id 映射回目标在线状态（v2 presence）。
    int targetPresence = -1;
    for (size_t i = 0; i < model.size(); ++i)
    {
        if (model[i].id == chosen)
        {
            targetPresence = model[i].presence;
            break;
        }
    }
    if (targetPresence < 0)
    {
        return;   // 理论上不会发生（chosen 必为模型里某项的 id）
    }

    // 目标状态与当前胶囊状态一致：无需任何动作，避免重复发包 / 无谓重连或断开。
    if (targetPresence == header->GetPresence())
    {
        return;
    }

    // 把目标 presence 投给隐藏的 主面板，由其按当前连接 / 登录态决定：
    // 离线→GoOffline；离线态选在线/离开/勿扰→GoOnline（登录带上该 presence）；
    // 在线态改状态→SendChangePresence（cmd_change_presence）。
    //
    // 胶囊的乐观更新<u>不在这里做</u>：切换在线状态还有系统托盘右键菜单这个入口，它是
    // 直接调会话控制器的 OnChangeMyPresence 的。两个入口都汇到那个函数，把更新胶囊放在
    // 那里（ApplyPresenceOptimistic）才能保证两条路表现一致。
    //lParam 是切换来源：状态菜单是用户手动选的，故传 kPresenceChangeManual。
    //会话控制器据此把"当前的离开是自动切过去的"标志清掉 —— 用户手动改过状态之后，
    //"无操作自动离开"就不该再擅自把他切回在线。
    ::PostMessage(m_hWnd, FMG_MSG_CHANGE_MY_PRESENCE, (WPARAM)targetPresence,
                  (LPARAM)kPresenceChangeManual);
}

int CBalloonMainPanel::GetPresence()
{
    balloonmain::MainPanelHeader* header = FindMainPanelHeader(GetClientContent());
    if (header == NULL)
    {
        // Header 尚未创建（理论上不会发生：能调到这里说明主面板已经显示出来了）。
        // 退回"期望在线状态"作兜底，与主面板初始化时给胶囊的取值同源。
        return CIUSocket::GetInstance().GetRecordOnlineStatus();
    }
    return header->GetPresence();
}

bool CBalloonMainPanel::CheckOnlineFor(int action)
{
    // 门禁只看会话控制器推下来的连接态，与托盘图标、状态胶囊、聊天窗同源。
    // 判定本身交给 OnlineCapability，不在本面板就地写一份。
    if (balloonmain::IsActionAllowedInState(m_connUiState, action))
    {
        return true;
    }

    LOG_INFO("MainPanel::CheckOnlineFor: blocked. action=%d, "
             "connUiState=%d (0=online 1=connecting 2=offline).",
        action, m_connUiState);
    ::MessageBox(m_hWnd, balloonmain::GetBlockedActionTip(action),
                 _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
    return false;
}

void CBalloonMainPanel::ApplyConnectionState(int state)
{
    const int prevState = m_connUiState;
    m_connUiState = state;

    // 「特别关注」提示的两件事跟着连接态走：
    //   · 断线 / 正在连接：把已经弹着的提示收掉。那批状态是断线前的旧账，连回来时
    //     服务端会重新推一遍当前状态，留着只会让人看着过期信息。
    //   · 由非在线态回到在线：重新起一个静默期。重连后服务端会把在线的同事重推一遍，
    //     与刚登录时是同一个局面，不静默就会弹一大片。
    if (state != balloonmain::kConnUiOnline)
    {
        m_starredPopup.HideAndClear();
    }
    else if (prevState != balloonmain::kConnUiOnline)
    {
        m_starredSilenceUntilTick = ::GetTickCount() + kStarredSilenceMs;
    }

    LOG_INFO("主面板应用连接态：state=%d(0=在线 1=正在连接 2=离线)，"
             "单聊窗 %u 个、群聊窗 %u 个。",
             state, (unsigned int)m_chatWindows.size(),
             (unsigned int)m_groupChatWindows.size());

    // 顶部 Banner：状态胶囊的文字与头像的灰彩都跟着连接态走。原先这里只往聊天窗
    // 转发、没管 Banner，于是掉线时托盘图标转灰了、胶囊却还写着"在线"。
    // 注意只推连接态，<u>不动</u> Header 的 m_presence —— 用户选的状态没变，状态菜单
    // 标记的"当前项"仍要按他选的那一项标。
    balloonmain::MainPanelHeader* header = FindMainPanelHeader(GetClientContent());
    if (header != NULL)
    {
        header->SetConnectionState(state);
        header->Invalidate();   // 自带 host 守卫；只重绘 Header 控件
    }

    // 逐窗转发。聊天窗的所有权在本面板，会话控制器够不着，故由这里代为分发。
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
        {
            it->second->ApplyConnectionState(state);
        }
    }

    for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.begin();
         it != m_groupChatWindows.end(); ++it)
    {
        if (it->second != NULL && it->second->IsWindow())
        {
            it->second->ApplyConnectionState(state);
        }
    }

    // 联系人资料对话框：它的快捷操作行里有语音 / 视频通话，同样要按连接态做门控。
    // 该窗口是本面板的成员对象（不是堆上的一组），开着才推。
    if (m_contactWindow.IsWindow())
    {
        m_contactWindow.ApplyConnectionState(state);
    }

    // 系统设置：「清理聊天记录」对话框按连接态决定「清理」是否可用。设置窗对象常驻，
    // 不论开着与否都转告一次，下次打开清理对话框时即按最新状态初始化。
    m_systemSettingDlg.SetServerConnected(state == balloonmain::kConnUiOnline);
}

void CBalloonMainPanel::ApplyPresenceOptimistic(int targetPresence)
{
    balloonmain::MainPanelHeader* header = FindMainPanelHeader(GetClientContent());
    if (header == NULL)
    {
        return;
    }

    // 先存改前状态供 cmd_change_presence 应答失败时回退（见 OnChangePresenceResult），
    // 再把胶囊改成目标状态并重绘 Header。
    m_presenceBeforeChange = header->GetPresence();
    header->SetPresence(targetPresence);
    header->Invalidate();   // 自带 host 守卫；只重绘 Header 控件，不触及搜索框

    LOG_INFO("主面板状态胶囊更新：%d → %d。", m_presenceBeforeChange, targetPresence);
}

void CBalloonMainPanel::ComputeContactDeptInfo(UINT userId, CString& outPosition,
                                               CString& outDeptPath)
{
    outPosition.Empty();
    outDeptPath.Empty();

    // 查 RebuildContactDeptPaths 算好的表。选部门的规则（主部门优先，否则第一个所在部门）
    // 与路径的拼法见 ContactDeptTable.h；不在任何部门的人查不到，两个出参保持为空。
    std::map<UINT, balloonmain::ContactDeptInfo>::const_iterator it = m_contactDeptInfo.find(userId);
    if (it == m_contactDeptInfo.end())
    {
        return;
    }
    outPosition = Utf8ToCStr(it->second.m_position);
    outDeptPath = Utf8ToCStr(it->second.m_deptPath);
}

bool CBalloonMainPanel::BuildContactModelFromCache(UINT peerUserId,
                                                   ballooncontact::ContactModel& outModel)
{
    // 取该联系人的缓存资料；不在通讯录缓存里返 false。
    const net::ContactItem* c = FindCachedContact(peerUserId);
    if (c == NULL)
    {
        return false;
    }

    CString position;
    CString deptPath;
    ComputeContactDeptInfo(peerUserId, position, deptPath);

    // 这里刻意只取真名、不含备注：资料窗头部要把真名与备注分开渲染（真名是大字
    // 主名、备注跟在后面），备注另经下面的 SetRemark 灌进模型。
    const CString name = balloonmain::ResolveContactRealName(
        Utf8ToCStr(c->m_nickname), Utf8ToCStr(c->m_username), peerUserId);

    // 在线状态：presence 非 0 视为在线（点绿），否则离线（不画点）。
    int status = (c->m_presence > 0) ? ballooncontact::kContactStatusOnline
                                     : ballooncontact::kContactStatusOffline;

    outModel.SetBasicInfo(name, Utf8ToCStr(c->m_username), Utf8ToCStr(c->m_address),
                          position, deptPath, Utf8ToCStr(c->m_signature), status);
    //备注名随通讯录应答一起带回来（contacts[].remark），这里灌进模型，资料窗打开
    //时那一行就显示已存的备注而不是恒为"未设置"。
    outModel.SetRemark(Utf8ToCStr(c->m_remark));
    //标签随通讯录应答一起下发（contacts[].tags），这里灌进模型，资料窗打开时那一排
    //胶囊就显示已存的标签。逐条 AddTag 而不是整份塞：AddTag 内部会 trim 并丢掉空串，
    //顺手挡住服务端万一下发的脏数据。
    outModel.ClearTags();
    for (size_t t = 0; t < c->m_tags.size(); ++t)
    {
        outModel.AddTag(Utf8ToCStr(c->m_tags[t]));
    }

    // 分组下拉：用部门路径作唯一项（无部门则"未分组"）。
    std::vector<CString> groups;
    groups.push_back(deptPath.IsEmpty() ? CString(_T("未分组")) : deptPath);
    outModel.SetGroups(groups, 0);

    //三个开关的当前状态。此前这三项从未灌过值，资料窗打开时一律显示"关"，与真实
    //状态对不上；用户拨一下也只改窗口内存、窗口一关就没了。
    //免打扰取自会话缓存：还没聊过的联系人不在会话列表里，查不到就按"未免打扰"显示，
    //用户拨开关照样发协议生效（口径与联系人右键菜单一致）。
    bool muted = false;
    const net::ConversationItem* pConv =
        balloonmain::FindConversation(m_conversations, conversation_single, (int64_t)peerUserId);
    if (pConv != NULL)
    {
        muted = (pConv->m_mute != 0);
    }
    outModel.SetSwitch(ballooncontact::kContactSwitchMute, muted);
    outModel.SetSwitch(ballooncontact::kContactSwitchFavorite,
                       m_favorites.IsFavorite(conversation_single, (int64_t)peerUserId));
    outModel.SetSwitch(ballooncontact::kContactSwitchStarred,
                       m_starred.IsStarred((int64_t)peerUserId));
    return true;
}

void CBalloonMainPanel::RefreshContactWindowIfShowing(UINT peerUserId)
{
    if (peerUserId == 0)
    {
        return;
    }
    if (!m_contactWindow.IsWindow())
    {
        return;   // 资料对话框没开着
    }
    if (m_contactWindow.GetPeerUserId() != peerUserId)
    {
        return;   // 开着的是别人的资料
    }

    ballooncontact::ContactModel model;
    if (!BuildContactModelFromCache(peerUserId, model))
    {
        return;   // 不在通讯录缓存里，无从重建
    }
    //静默刷新：不前置、不抢焦点，只把窗口内容换成最新状态。
    m_contactWindow.RefreshContactData(model);
}

void CBalloonMainPanel::RebuildContactDeptPaths()
{
    // 一次扫描全部部门，得出每个人的职务与部门路径。原先是每个人都把全部部门重扫一遍，
    // 5000 人、120 个部门时要一秒多（2026-10-01 基准实测）。
    balloonmain::BuildContactDeptTable(m_departments, m_contactDeptInfo);

    m_contactDeptPaths.clear();
    for (size_t i = 0; i < m_contacts.size(); ++i)
    {
        const UINT userId = (UINT)m_contacts[i].m_userid;
        std::map<UINT, balloonmain::ContactDeptInfo>::const_iterator it = m_contactDeptInfo.find(userId);
        //没有部门归属的人不进表 —— 搜索结果行查不到就退回展示账号与地区。
        if (it != m_contactDeptInfo.end() && !it->second.m_deptPath.empty())
        {
            m_contactDeptPaths[userId] = Utf8ToCStr(it->second.m_deptPath);
        }
    }
    LOG_INFO("contact dept path table rebuilt: %u of %u contacts have a department.",
             (uint32_t)m_contactDeptPaths.size(), (uint32_t)m_contacts.size());
}

void CBalloonMainPanel::ShowAndActivateSelf()
{
    if (!IsWindow())
    {
        return;
    }

    //最小化的窗口只有 SW_RESTORE 能还原，SW_SHOW 对它无效（表现为点了没有任何反应）；
    //隐藏到托盘的窗口则走 SW_SHOW。两种情形必须分开处理，做法与托盘菜单「打开主面板」
    //一致（见 balloonmain::CSessionController::ShowLoginOrMainPanel）。
    if (IsIconic())
    {
        ShowWindow(SW_RESTORE);
    }
    else
    {
        ShowWindow(SW_SHOW);
    }
    ::SetForegroundWindow(m_hWnd);
    ::SetFocus(m_hWnd);
}

void CBalloonMainPanel::LocateContactInTree(UINT peerUserId)
{
    if (peerUserId == 0)
    {
        return;
    }

    //定位的结果显示在主面板上，所以先把主面板还原并切到前台。入口之一是联系人资料
    //对话框的「组织架构」按钮，那个窗口点完就关闭，此时主面板可能收在托盘里或处于
    //最小化状态 —— 不先调出来的话，用户看不到任何变化，会以为这个按钮没有生效。
    //另一处入口（搜索结果行的定位按钮）本来主面板就在前台，多调一次没有副作用。
    ShowAndActivateSelf();

    std::map<UINT, int>::const_iterator itNode = m_contactOrgNode.find(peerUserId);
    if (itNode == m_contactOrgNode.end())
    {
        //树上没有这一行：通讯录尚未到达，或者这个人不属于任何部门（协议下发的资料池
        //与部门成员表是两份数据，理论上可能对不齐）。此时弹出提示说明原因，不静默返回。
        ::MessageBox(m_hWnd, _T("这位联系人不在当前的组织架构中，无法定位。"),
                     _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
        LOG_WARNING("locate contact failed: userid=%u is not in the org tree.", peerUserId);
        return;
    }
    const int targetNode = itNode->second;

    //先切到通讯录页。SwitchTab 内部会收起搜索结果下拉，避免该浮窗遮住刚定位到的那一行。
    SwitchTab(balloonmain::kIdTabContacts);

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiTreeView* tv = dynamic_cast<balloonwjui::DuiTreeView*>(
        content->FindCtrlById(balloonmain::kIdContactsTree));
    //只需要滚动位置与内容高度，用基类指针即可，不必依赖那个自绘悬浮滚动条的子类。
    balloonwjui::DuiScrollView* sv = dynamic_cast<balloonwjui::DuiScrollView*>(
        content->FindCtrlById(balloonmain::kIdPaneContacts));
    if (tv == NULL || sv == NULL)
    {
        return;
    }

    //自下而上把整条部门路径展开。用户可能手动折叠过其中某一级，不展开的话目标行
    //根本不在可见行列表里，算不出行号也滚不到。
    std::vector<int> ancestors;
    balloonmain::CollectAncestorChain(m_contactNodeParent, targetNode,
                                      balloonmain::kTreeAncestorMaxDepth, ancestors);
    for (size_t i = 0; i < ancestors.size(); ++i)
    {
        tv->SetExpanded(ancestors[i], true);
    }

    //展开之后树变高了，滚动范围要重算一遍，否则下面拿到的内容总高还是展开前的，
    //目标行在视口下方时会因为滚不动而定位不到。
    SyncPaneContentHeight(balloonmain::kIdPaneContacts,
                          balloonmain::kIdContactsTree, kContactsPaneTitleHeight);

    //把目标行滚进视口。行的纵向位置 = 页标题高度 + 容器上内边距 + 行号 × 行高。
    const int rowIndex = tv->GetVisibleRow(targetNode);
    const RECT rcView  = sv->GetRect();
    const int viewHeight = rcView.bottom - rcView.top;
    const int newPos = balloonmain::ComputeLocateScrollPos(
        rowIndex, tv->GetRowHeight(),
        kContactsPaneTitleHeight + kContactsPaneContentPadTop,
        viewHeight, sv->GetContentHeight(), sv->GetScrollPos());
    sv->SetScrollPos(newPos);

    //选中该行。notify 传 false —— 树的选中通知会被当成"用户点了这一行"从而打开
    //单聊窗，而定位只是让用户看到这个人在组织架构中的位置，不应当同时打开聊天窗口。
    tv->SetCurSel(targetNode, false);
    tv->Invalidate();

    LOG_INFO("locate contact in org tree: userid=%u, node=%d, row=%d, scroll=%d.",
             peerUserId, targetNode, rowIndex, newPos);
}

void CBalloonMainPanel::OpenContactProfile(UINT peerUserId)
{
    ballooncontact::ContactModel model;
    if (!BuildContactModelFromCache(peerUserId, model))
    {
        return;   // 不在通讯录缓存里
    }
    m_contactWindow.OpenForContact(m_hWnd, peerUserId, model);
    //把当前连接态推给它，理由同 BeginContactRemarkEdit 里的那一处。
    m_contactWindow.ApplyConnectionState(m_connUiState);
}

LRESULT CBalloonMainPanel::OnOpenContactProfileMsg(UINT /*uMsg*/, WPARAM wParam,
                                                   LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    OpenContactProfile((UINT)wParam);
    return 0;
}

LRESULT CBalloonMainPanel::OnQueryStarredMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                             LPARAM lParam, BOOL& /*bHandled*/)
{
    const int64_t* pUserId = (const int64_t*)lParam;
    if (pUserId == NULL)
    {
        return 0;
    }
    return m_starred.IsStarred(*pUserId) ? 1 : 0;
}

LRESULT CBalloonMainPanel::OnQueryContactBriefMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                  LPARAM lParam, BOOL& /*bHandled*/)
{
    balloonchatwnd::ContactBriefQuery* pQuery = (balloonchatwnd::ContactBriefQuery*)lParam;
    if (pQuery == NULL || pQuery->m_userId <= 0)
    {
        return 0;
    }
    const net::ContactItem* pContact = FindCachedContact((UINT)pQuery->m_userId);
    if (pContact == NULL)
    {
        return 0;
    }
    pQuery->m_found      = true;
    pQuery->m_nickname   = pContact->m_nickname;
    pQuery->m_facetype   = pContact->m_facetype;
    pQuery->m_customface = pContact->m_customface;
    return 1;
}

LRESULT CBalloonMainPanel::OnToggleStarredMsg(UINT /*uMsg*/, WPARAM wParam,
                                              LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    ToggleStarred((int64_t)wParam);
    return 0;
}

LRESULT CBalloonMainPanel::OnSetContactMuteMsg(UINT /*uMsg*/, WPARAM wParam,
                                               LPARAM lParam, BOOL& /*bHandled*/)
{
    //lParam 是目标状态而不是取反，理由见 SetFavoriteState 的说明。
    SetConversationMuteState(conversation_single, (int64_t)(UINT)wParam, lParam != 0);
    return 0;
}

LRESULT CBalloonMainPanel::OnSetContactFavoriteMsg(UINT /*uMsg*/, WPARAM wParam,
                                                   LPARAM lParam, BOOL& /*bHandled*/)
{
    SetFavoriteState(conversation_single, (int64_t)(UINT)wParam, lParam != 0);
    return 0;
}

LRESULT CBalloonMainPanel::OnSetContactStarredMsg(UINT /*uMsg*/, WPARAM wParam,
                                                  LPARAM lParam, BOOL& /*bHandled*/)
{
    SetStarredState((int64_t)(UINT)wParam, lParam != 0);
    return 0;
}

LRESULT CBalloonMainPanel::OnLocateContactMsg(UINT /*uMsg*/, WPARAM wParam,
                                              LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    LocateContactInTree((UINT)wParam);
    return 0;
}

LRESULT CBalloonMainPanel::OnOpenChatMsg(UINT /*uMsg*/, WPARAM wParam,
                                         LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    OpenSingleChat((UINT)wParam);
    return 0;
}

LRESULT CBalloonMainPanel::OnConvSendBumpMsg(UINT /*uMsg*/, WPARAM wParam,
                                             LPARAM lParam, BOOL& /*bHandled*/)
{
    // 单聊窗发出一条消息：把该会话冒泡到顶部并刷新预览（自己发出，不计未读）。
    // lParam 为堆上 ConvSendBumpData（含消息类型与预览文字），处理后须 delete。
    ConvSendBumpData* pBump = (ConvSendBumpData*)lParam;
    if (pBump == NULL)
    {
        return 0;
    }
    int         ctype  = pBump->ctype;
    int64_t     cid    = (int64_t)pBump->cid;
    std::string digest = pBump->digest;
    int         msgType = pBump->msgType;
    delete pBump;

    UINT selfId = (m_lpFMGClient != NULL)
        ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    // 发送时间用本地当前时刻（服务器时间在回执/下次会话同步里再校正即可）。
    int64_t nowMs = (int64_t)::time(NULL) * 1000;   // 秒 → 毫秒
    // 本端发出时服务器 msgid 暂未知（在 cmd_send_message 回执里才回来），传 0；
    // 已读上限锚点不靠自己发出的消息推进——未读只由对端消息构成。
    BumpConversation(ctype, cid, selfId, digest, msgType, nowMs, 0, false);

    // 用户在窗口里回了一条消息 —— 这是最强的"我已经看到了"信号，故顺带把该会话
    // 可能还挂着的旧未读清零。此前只有从会话列表点进去才会清，于是出现过"回都回
    // 了，列表上的红点还在"。
    MarkConversationRead(ctype, cid);
    return 0;
}

LRESULT CBalloonMainPanel::OnConvMarkReadMsg(UINT /*uMsg*/, WPARAM wParam,
                                             LPARAM lParam, BOOL& /*bHandled*/)
{
    // 聊天窗被用户切到前台：把该会话未读清零并上报已读位点。
    // lParam 为堆上 long long* cid（Win32 下 LPARAM 装不下 64 位 cid，与
    // FMG_MSG_TRAY_CLEAR 同法），处理后须 delete。
    long long* pCid = (long long*)lParam;
    if (pCid == NULL)
    {
        return 0;
    }
    const int64_t cid = (int64_t)*pCid;
    delete pCid;

    MarkConversationRead((int)wParam, cid);
    return 0;
}

LRESULT CBalloonMainPanel::OnOpenMemberPickerMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                 LPARAM lParam, BOOL& /*bHandled*/)
{
    // 群管理窗请求打开选人对话框：lParam 为堆上的请求载荷，处理后须 delete。
    balloongroupmanage::MemberPickerRequest* pReq =
        (balloongroupmanage::MemberPickerRequest*)lParam;
    if (pReq == NULL)
    {
        return 0;
    }
    std::unique_ptr<balloongroupmanage::MemberPickerRequest> guard(pReq);

    const UINT selfId = (m_lpFMGClient != NULL)
                        ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;

    std::vector<net::ContactItem> candidates;
    std::vector<net::DepartmentItem> departments;

    if (pReq->m_purpose == CCreateGroupDlg::kPurposeInvite)
    {
        // 邀请入群：候选是通讯录里还不在本群的人。群成员 id 由群管理窗传来
        // （只有它知道谁在群里），这里做差集。
        std::set<int64_t> inGroup(pReq->m_excludeUserIds.begin(),
                                  pReq->m_excludeUserIds.end());
        for (size_t i = 0; i < m_contacts.size(); ++i)
        {
            if (inGroup.count(m_contacts[i].m_userid) > 0)
                continue;
            candidates.push_back(m_contacts[i]);
        }
        // 通讯录来源的候选按部门分组显示
        departments = m_departments;
    }
    else
    {
        // 禁言 / 转让群主：候选已由群管理窗按其成员模型组好，直接用。
        // 群成员没有部门归属，departments 留空 —— 对话框会把他们全放进"未分组"。
        candidates = pReq->m_candidates;
    }

    if (candidates.empty())
    {
        ::MessageBox(m_hWnd, _T("没有可选的成员。"), _T("提示"), MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    m_createGroupDlg.OpenForPick(m_hWnd, (CCreateGroupDlg::Purpose)pReq->m_purpose,
                                 pReq->m_groupId, selfId, candidates, departments);
    LOG_INFO("打开选人对话框: purpose=%d, groupid=%lld, candidates=%u。",
             pReq->m_purpose, (long long)pReq->m_groupId, (uint32_t)candidates.size());
    return 0;
}

LRESULT CBalloonMainPanel::OnTransferGroupOwnerMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                   LPARAM lParam, BOOL& /*bHandled*/)
{
    // lParam 为堆上的转让请求载荷，处理后须 delete。确认框已由发起窗口弹过
    balloonmain::TransferOwnerRequest* pReq = (balloonmain::TransferOwnerRequest*)lParam;
    if (pReq == NULL)
    {
        return 0;
    }
    std::unique_ptr<balloonmain::TransferOwnerRequest> guard(pReq);
    if (pReq->m_groupId <= 0 || pReq->m_newOwner <= 0)
    {
        return 0;
    }

    if (m_lpFMGClient != NULL && m_lpFMGClient->IsOffline())
    {
        ::MessageBox(m_hWnd, _T("您当前处于离线状态，无法转让群主，请上线后重试。"),
                     _T("FlamingoIM"), MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    CIUSocket::GetInstance().SendTransferOwner(pReq->m_groupId, pReq->m_newOwner);
    balloonmain::PendingGroupOp pending;
    pending.m_groupId = pReq->m_groupId;
    pending.m_groupName = GroupNameForNotice(pReq->m_groupId);
    pending.m_newOwner = pReq->m_newOwner;
    pending.m_newOwnerName = pReq->m_newOwnerName;
    pending.m_thenLeave = pReq->m_thenLeave;
    m_groupExitTracker.Begin(balloonmain::kGroupExitOpTransfer, pending);
    LOG_INFO("Transfer owner request sent: groupid=%lld, newowner=%lld, thenLeave=%d.",
             (long long)pReq->m_groupId, (long long)pReq->m_newOwner, (int)pReq->m_thenLeave);
    return 0;
}

LRESULT CBalloonMainPanel::OnOpenGroupManageMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                LPARAM lParam, BOOL& /*bHandled*/)
{
    // 群聊窗齿轮请求打开群管理对话框：lParam 为堆上 int64_t* 群 id，处理后 delete。
    int64_t* pGroupId = (int64_t*)lParam;
    if (pGroupId == NULL)
    {
        return 0;
    }
    int64_t groupId = *pGroupId;
    delete pGroupId;
    OpenGroupManage(groupId);
    return 0;
}

LRESULT CBalloonMainPanel::OnOpenGroupAvatarDlgMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                   LPARAM lParam, BOOL& /*bHandled*/)
{
    // 群管理窗请求改群头像：lParam 为堆上的请求载荷，处理后须 delete。
    balloongroupmanage::GroupAvatarDlgRequest* pReq =
        (balloongroupmanage::GroupAvatarDlgRequest*)lParam;
    if (pReq == NULL)
    {
        return 0;
    }
    std::unique_ptr<balloongroupmanage::GroupAvatarDlgRequest> guard(pReq);

    if (pReq->m_groupId <= 0)
    {
        return 0;
    }

    // 上传分桶用的是登录者，不是群 —— 传错会让头像图落到别的账号的缓存目录下。
    const int64_t selfId = (m_lpFMGClient != NULL)
                           ? (int64_t)m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;

    m_modifyAvatarDlg.OpenForGroup(m_hWnd, pReq->m_groupId,
                                   Utf8ToCStr(pReq->m_groupNameUtf8),
                                   pReq->m_facetype, pReq->m_customfaceUtf8, selfId);
    LOG_INFO(_T("已打开修改群头像对话框，groupid=%lld。"), (long long)pReq->m_groupId);
    return 0;
}

LRESULT CBalloonMainPanel::OnOpenChangePwdMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                              LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 系统设置「账号安全」页"修改密码"按钮转发到此：复用面板自己持有的改密对话框，
    // 与系统菜单"修改密码"走同一实例，保证改密应答能正确回投。
    OpenChangePwdDlg();
    return 0;
}

LRESULT CBalloonMainPanel::OnRefreshSelfHeaderMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                  LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 个人信息对话框乐观保存后转发到此：按已更新的 m_UserInfo 立即刷新本人 Header
    // （昵称/签名/头像），不必等服务端应答，主面板即时反映改动。
    RefreshSelfAvatar();
    return 0;
}

LRESULT CBalloonMainPanel::OnLinkOptionChangedMsg(UINT /*uMsg*/, WPARAM wParam,
                                                  LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    const bool enabled = (wParam != 0);
    // 先更新进程级开关：随后各窗重新测量时，气泡才会按新口径决定要不要识别链接。
    balloonchatwnd::SetChatLinkOptionEnabled(enabled);

    // 再让已经打开的会话窗逐个重新测量重绘。没开窗时这两个 map 是空的，什么都不做。
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL)
        {
            it->second->RefreshLinkRendering();
        }
    }
    for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.begin();
         it != m_groupChatWindows.end(); ++it)
    {
        if (it->second != NULL)
        {
            it->second->RefreshLinkRendering();
        }
    }

    LOG_INFO("聊天链接识别开关已切换: enabled=%d, 已刷新单聊窗 %d 个、群聊窗 %d 个。",
             enabled ? 1 : 0, (int)m_chatWindows.size(), (int)m_groupChatWindows.size());
    return 0;
}

LRESULT CBalloonMainPanel::OnDefaultChatFontChangedMsg(UINT /*uMsg*/, WPARAM /*wParam*/,
                                                       LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    // 逐个已打开的会话窗处理；单独设过字体的窗口由窗口自己判断后跳过。没开窗时两个 map
    // 是空的，什么都不做。
    for (std::map<UINT, CBalloonChatWindow*>::iterator it = m_chatWindows.begin();
         it != m_chatWindows.end(); ++it)
    {
        if (it->second != NULL)
        {
            it->second->ApplyDefaultChatFontIfNotOverridden();
        }
    }
    for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator it = m_groupChatWindows.begin();
         it != m_groupChatWindows.end(); ++it)
    {
        if (it->second != NULL)
        {
            it->second->ApplyDefaultChatFontIfNotOverridden();
        }
    }

    LOG_INFO("default chat font changed: notified %d single chat windows and %d group chat windows.",
             (int)m_chatWindows.size(), (int)m_groupChatWindows.size());
    return 0;
}

void CBalloonMainPanel::StartCallWithContact(UINT peerUserId, bool video)
{
    if (peerUserId == 0)
    {
        return;
    }

    // 通话要走信令通道，离线时发起必然失败，在入口就拦住（口径与单聊窗头部的
    // 语音 / 视频按钮一致）。
    if (!CheckOnlineFor(balloonmain::kActionRealtimeCall))
    {
        return;
    }
    if (!IsWindow())
    {
        LOG_ERROR("start call failed: main panel window is gone, userid=%u.",
                  peerUserId);
        return;
    }

    // peerName 走堆上的 std::wstring，最终由通话协调器处理后 delete —— 与单聊窗
    // 那两个按钮传的是同一份载荷格式。协调器挂在本面板的消息派发链上，故投给本窗口。
    const CString peerName = ContactDisplayName(peerUserId);
    const UINT msgId = video ? FMG_MSG_CALL_START_VIDEO_INVITE : FMG_MSG_CALL_START_INVITE;
    ::PostMessage(m_hWnd, msgId, (WPARAM)peerUserId,
                  (LPARAM)new std::wstring(peerName.GetString()));

    LOG_INFO("start call from contact menu: userid=%u, video=%d.",
             peerUserId, (int)video);
}

bool CBalloonMainPanel::ComputeSearchAnchor(RECT& outAnchorScreen, int& outPanelWidth)
{
    if (!IsWindow())
    {
        return false;
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return false;
    }
    balloonwjui::DuiEdit* edit = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(balloonmain::kIdSearchBox));
    if (edit == NULL)
    {
        return false;
    }

    // 锚点 = 搜索框矩形，转屏幕坐标（下拉是顶层浮窗，按屏幕坐标定位）。
    RECT rc = edit->GetRect();
    POINT tl = { rc.left, rc.top };
    POINT br = { rc.right, rc.bottom };
    ::ClientToScreen(m_hWnd, &tl);
    ::ClientToScreen(m_hWnd, &br);
    outAnchorScreen.left   = tl.x;
    outAnchorScreen.top    = tl.y;
    outAnchorScreen.right  = br.x;
    outAnchorScreen.bottom = br.y;

    // 下拉宽 = 主面板客户区宽。
    RECT rcClient;
    ::GetClientRect(m_hWnd, &rcClient);
    outPanelWidth = rcClient.right - rcClient.left;
    return true;
}

void CBalloonMainPanel::UpdateSearchDropdown()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiEdit* edit = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(balloonmain::kIdSearchBox));
    if (edit == NULL)
    {
        return;
    }

    // 实时绑定数据源：直接借用主面板自己持有的联系人池与群列表，保证通讯录 / 群
    // 列表后到也能被搜到。selfId 有两个用途：参与"要不要包含自己"的判断，以及
    // 画头像时要知道哪一行是自己——所以想包含自己只能靠下面那个开关，不能传 0。
    UINT selfId = SelfUserId();
    // 最后一个参数是"联系人结果里包不包含自己"。传 true：通讯录树本来就会列出
    // 自己（好让用户看清自己在组织架构里的位置），搜索这边跟着放开才不别扭；
    // 命中自己的那一行会带"（我）"后缀，不会认错人。
    m_searchDropdown.SetData(&m_contacts, &m_myGroups, selfId, true);
    // 部门路径查询表：联系人结果行的副行据它显示对方所在部门，而不是账号与地区。
    // 通讯录复杂时，"这个人在哪个部门"往往比账号更能帮用户认出是不是要找的那位。
    m_searchDropdown.SetContactDeptPaths(&m_contactDeptPaths);

    RECT anchorScreen = { 0, 0, 0, 0 };
    int panelWidth = 0;
    if (!ComputeSearchAnchor(anchorScreen, panelWidth))
    {
        return;
    }

    m_searchDropdown.UpdateQuery(anchorScreen, panelWidth, edit->GetText());
}

void CBalloonMainPanel::ScheduleSearchWarmUp()
{
    //已有一条在途：通讯录与群列表先后到达时只预热一次，那一次会按处理时的最新数据 Sync
    if (m_searchWarmUpPending || !IsWindow())
    {
        return;
    }
    if (PostMessage(kMsgSearchWarmUp, 0, 0))
    {
        m_searchWarmUpPending = true;
    }
}

LRESULT CBalloonMainPanel::OnSearchWarmUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    m_searchWarmUpPending = false;

    //投递的消息排在 WM_PAINT 之前处理，直接预热会让刚填好的列表晚几百毫秒才画出来。
    //先把待绘制的内容画完，再做耗时的预热。
    ::UpdateWindow(m_hWnd);

    //与 UpdateSearchDropdown 绑定给下拉的是同一份资料池与群列表，预热结果在按键时可直接复用
    m_searchDropdown.WarmUpCandidates(m_contacts, m_myGroups);
    return 0;
}

void CBalloonMainPanel::SyncSearchDropdownPos()
{
    RECT anchorScreen = { 0, 0, 0, 0 };
    int panelWidth = 0;
    if (!ComputeSearchAnchor(anchorScreen, panelWidth))
    {
        return;
    }

    m_searchDropdown.FollowAnchor(anchorScreen, panelWidth);
}

void CBalloonMainPanel::FocusSearchBox()
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiEdit* edit = dynamic_cast<balloonwjui::DuiEdit*>(
        content->FindCtrlById(balloonmain::kIdSearchBox));
    if (edit == NULL)
    {
        return;
    }
    // 把焦点交给搜索框，用户即可直接输入。无窗口控件的 SetFocus 一步到位：
    // 既把 DUI 焦点指到本控件，又让主面板窗口把 Win32 键盘焦点拿到手。
    edit->SetFocus();
}

void CBalloonMainPanel::OpenCreateGroupDlg()
{
    // 建群要向服务器发建群请求，离线时做不了，在入口就拦住并给提示。
    if (!CheckOnlineFor(balloonmain::kActionAccountOp))
    {
        return;
    }

    UINT selfId = (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    // 把通讯录缓存（联系人 + 部门）交给对话框构建可勾选成员列表。
    m_createGroupDlg.OpenForCreate(m_hWnd, selfId, m_contacts, m_departments);
}

void CBalloonMainPanel::OnCreateGroupResult(const net::CreateGroupResponse& resp)
{
    // 转交对话框提示结果（成功关闭并提示，失败提示错误）。
    m_createGroupDlg.OnCreateGroupResult(resp);

    // 建群成功后立刻重拉一次"我的群列表"，让新群马上出现在"群"Tab（否则要等
    // 下次登录才刷新）。
    if (resp.m_code == 0)
    {
        CIUSocket::GetInstance().RequestUserGroups();
    }
}

void CBalloonMainPanel::OpenChangePwdDlg()
{
    // 改密码要向服务器发请求，离线时做不了，在入口就拦住并给提示。
    if (!CheckOnlineFor(balloonmain::kActionAccountOp))
    {
        return;
    }

    // 修改密码对话框自包含（不需要通讯录数据），创建并显示即可。
    m_changePwdDlg.OpenForChange(m_hWnd);
}

void CBalloonMainPanel::OpenSystemSettingDlg()
{
    // 系统设置对话框自包含（本期仅本地配置），创建并显示即可。
    // 唯一的例外是「自动登录」开关：它的状态存在会话控制器持有的历史账号列表里，
    // 设置窗读不到，须在打开前传进去（见 docs/自动登录方案.md §4）。
    m_systemSettingDlg.SetAutoLoginSwitchState(m_sessionController.IsAutoLoginEnabledForCurrentAccount());
    // 「清理聊天记录」要连着服务器才能用，同样在打开前把当前连接态传进去。
    m_systemSettingDlg.SetServerConnected(m_connUiState == balloonmain::kConnUiOnline);
    m_systemSettingDlg.OpenSettings(m_hWnd);
}

void CBalloonMainPanel::OpenAboutDlg()
{
    // 关于对话框自包含（纯展示 + 几个外链 / 占位按钮），创建并显示即可。
    m_aboutDlg.OpenAbout(m_hWnd);
}

void CBalloonMainPanel::OnChangePwdResult(const net::ChangePasswordResponse& resp)
{
    // 转交改密对话框提示结果（成功关闭、旧密码错 / 失败提示）。
    m_changePwdDlg.OnChangePwdResult(resp);
}

void CBalloonMainPanel::OnChangeAvatarResult(const net::UpdateProfileResponse& resp)
{
    // 先交修改头像对话框：成功则关闭，失败就地提示。
    m_modifyAvatarDlg.OnChangeAvatarResult(resp);

    if (resp.code != 0)
    {
        return;
    }

    // 改的是群头像时不能动登录用户自己的那份缓存。正常情况下走不到这里
    // （群头像的应答走 cmd_update_group_info，由 OnGroupOpResult 分流），
    // 这一道是防止日后改动把两条路串了。
    if (m_modifyAvatarDlg.GetTargetKind() != CBalloonModifyAvatarDlg::target_user)
    {
        return;
    }

    // 成功：把对话框实际发出的 (facetype, customface) 写回本端缓存（m_nFace 存
    // facetype，m_strCustomFaceName 这个 CStringA 按字节存 UTF-8 customface），
    // 再按新值刷新 Banner 头像，使主面板自己的头像即时更新。
    if (m_lpFMGClient != NULL)
    {
        CBuddyInfo& me = m_lpFMGClient->m_UserMgr.m_UserInfo;
        me.m_nFace = (UINT)m_modifyAvatarDlg.GetAppliedFacetype();
        const std::string& cf = m_modifyAvatarDlg.GetAppliedCustomface();
        me.m_strCustomFaceName = CStringA(cf.c_str(), (int)cf.size());
    }
    RefreshSelfAvatar();
    LOG_INFO(_T("修改头像成功，已刷新本端 Banner 头像。"));
}

void CBalloonMainPanel::OnAvatarUploadResult(const net::AvatarUploadResult& result)
{
    // 转交修改头像对话框：成功则它接着发 cmd_change_avatar(facetype=2,customface=fileid)，
    // 失败就地提示。本端头像最终刷新仍由随后的 cmd_change_avatar 应答触发。
    m_modifyAvatarDlg.OnAvatarUploadResult(result);
}

void CBalloonMainPanel::OnAvatarDownloadResult(const net::AvatarDownloadResult& result)
{
    if (!result.m_ok || result.m_fileid.empty())
    {
        return;   //下载失败：相关节点维持 monogram 占位，不刷新。
    }
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 联系人树：node→userid，找到引用该 fileid 真图头像的节点，从（刚下好的）缓存重画刷新。
    balloonwjui::DuiTreeView* ctv = dynamic_cast<balloonwjui::DuiTreeView*>(
        content->FindCtrlById(balloonmain::kIdContactsTree));
    if (ctv != NULL)
    {
        bool any = false;
        for (std::map<int, UINT>::iterator it = m_contactNodeUserId.begin();
             it != m_contactNodeUserId.end(); ++it)
        {
            const net::ContactItem* pc = FindCachedContact(it->second);
            if (pc == NULL || pc->m_facetype != balloonmain::avatar_kind_image
                || pc->m_customface != result.m_fileid)
            {
                continue;
            }
            CString name = Utf8ToCStr(pc->m_nickname);
            if (name.IsEmpty())
            {
                name = Utf8ToCStr(pc->m_username);
            }
            HBITMAP hbm = GetEntityAvatarBitmap(pc->m_userid, name,
                                                pc->m_facetype, pc->m_customface, kContactAvatarSizePx);
            if (hbm != NULL)
            {
                ctv->SetItemIcon(it->first, hbm);   //位图归头像缓存所有，这里只借用
                any = true;
            }
        }
        if (any)
        {
            ctv->Invalidate();
        }
    }

    // 消息树（单聊会话）：node→(ctype,cid)，ctype==1 查缓存联系人命中 fileid → 刷新。
    balloonwjui::DuiTreeView* mtv = dynamic_cast<balloonwjui::DuiTreeView*>(
        content->FindCtrlById(balloonmain::kIdMessagesTree));
    if (mtv != NULL)
    {
        const int kMsgAvatarSizePx = 36;   // 须与 FillConversationsTree 内一致
        bool any = false;
        for (std::map<int, std::pair<int, int64_t> >::iterator it = m_msgNodeConv.begin();
             it != m_msgNodeConv.end(); ++it)
        {
            //单聊查联系人缓存、群聊查群缓存，两边都可能还没到（那就跳过，
            //下次重填树时自然会带上真图）。
            const int64_t entityId = it->second.second;
            int         facetype = 0;
            std::string customface;
            CString     name;

            if (it->second.first == 1)
            {
                const net::ContactItem* pc = FindCachedContact((UINT)entityId);
                if (pc == NULL)
                {
                    continue;
                }
                facetype   = pc->m_facetype;
                customface = pc->m_customface;
                name       = Utf8ToCStr(pc->m_nickname);
                if (name.IsEmpty())
                {
                    name = Utf8ToCStr(pc->m_username);
                }
            }
            else
            {
                std::map<int64_t, GroupBrief>::const_iterator bit = m_groupInfo.find(entityId);
                if (bit == m_groupInfo.end())
                {
                    continue;
                }
                facetype   = bit->second.m_facetype;
                customface = bit->second.m_customface;
                name       = GroupDisplayName(entityId);
            }

            //只刷引用了这张刚下好的图的节点。
            if (facetype != balloonmain::avatar_kind_image || customface != result.m_fileid)
            {
                continue;
            }

            HBITMAP hbm = GetEntityAvatarBitmap(entityId, name, facetype, customface,
                                                kMsgAvatarSizePx);
            if (hbm != NULL)
            {
                mtv->SetItemIcon(it->first, hbm);   //位图归头像缓存所有，这里只借用
                any = true;
            }
        }
        if (any)
        {
            mtv->Invalidate();
        }
    }

    // 群 Tab 树：node→groupid，找到引用该 fileid 群头像的节点，从缓存重画刷新。
    balloonwjui::DuiTreeView* gtv = dynamic_cast<balloonwjui::DuiTreeView*>(
        content->FindCtrlById(balloonmain::kIdGroupsTree));
    if (gtv != NULL)
    {
        const int kGroupAvatarSizePx = 32;   // 须与 FillGroupsTree 内一致
        bool any = false;
        for (std::map<int, int64_t>::iterator it = m_groupNodeId.begin();
             it != m_groupNodeId.end(); ++it)
        {
            std::map<int64_t, GroupBrief>::const_iterator bit = m_groupInfo.find(it->second);
            if (bit == m_groupInfo.end())
            {
                continue;
            }
            if (bit->second.m_facetype != balloonmain::avatar_kind_image
                || bit->second.m_customface != result.m_fileid)
            {
                continue;
            }
            HBITMAP hbm = GetEntityAvatarBitmap(it->second, GroupDisplayName(it->second),
                                                bit->second.m_facetype, bit->second.m_customface,
                                                kGroupAvatarSizePx);
            if (hbm != NULL)
            {
                gtv->SetItemIcon(it->first, hbm);   //位图归头像缓存所有，这里只借用
                any = true;
            }
        }
        if (any)
        {
            gtv->Invalidate();
        }
    }

    // 开着的群管理窗：Header 与基本信息页那两处群头像同样可能在等这张图。
    if (m_pGroupManageWindow != NULL && m_pGroupManageWindow->IsWindow())
    {
        m_pGroupManageWindow->OnAvatarDownloaded(result.m_fileid);
    }

    // 打开着的聊天窗（单聊 + 群聊）：标题栏图标 / 气泡 / 成员列表里引用该 fileid 真图头像
    // 的位置，让其按刚下好的缓存重画。窗口各自判断是否与本窗口相关，不相关即 no-op。
    for (std::map<UINT, CBalloonChatWindow*>::iterator cit = m_chatWindows.begin();
         cit != m_chatWindows.end(); ++cit)
    {
        if (cit->second != NULL && cit->second->IsWindow())
        {
            cit->second->OnAvatarDownloaded(result.m_fileid);
        }
    }
    for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator git = m_groupChatWindows.begin();
         git != m_groupChatWindows.end(); ++git)
    {
        if (git->second != NULL && git->second->IsWindow())
        {
            git->second->OnAvatarDownloaded(result.m_fileid);
        }
    }
}

void CBalloonMainPanel::OnUpdateProfileResult(const net::UpdateProfileResponse& resp)
{
    // 个人信息对话框发起的"保存资料"有未决应答时,交给它处理(它持快照,负责回退 /
    // 提示 / 切回查看态),随后用已成最终态的 m_UserInfo 刷新主面板 Header(昵称 /
    // 签名 / 头像可能变)。与下方签名原地编辑路径互斥。
    if (m_profileDlg.HasPendingSave())
    {
        m_profileDlg.OnSaveResult(resp);
        // OnSaveResult 已把 m_UserInfo 置成最终态(成功=新值,失败=回退旧值),这里按它
        // 刷新本人 Header。用 RefreshSelfAvatar()(全量、含头像)而非 5 参 ApplyUserData,
        // 避免把自定义字形头像误重置成自动头像。
        RefreshSelfAvatar();
        return;
    }

    if (resp.code == 0)
    {
        LOG_INFO(_T("个性签名更新成功。"));
        return;   // 乐观更新已生效，无需额外动作
    }

    // 失败：回退到提交前的签名并刷新 Header，提示用户。
    LOG_ERROR("update signature failed, code=%d, msg=%s.", resp.code, resp.msg.c_str());
    if (m_lpFMGClient != NULL)
    {
        m_lpFMGClient->m_UserMgr.m_UserInfo.m_strSign = m_signBeforeUpdate.GetString();
        const CBuddyInfo& me = m_lpFMGClient->m_UserMgr.m_UserInfo;
        balloonmain::ApplyUserDataToMainPanel(
            GetClientContent(),
            me.m_uUserID,
            CString(me.m_strAccount.c_str()),
            CString(me.m_strNickName.c_str()),
            CString(me.m_strSign.c_str()),
            CIUSocket::GetInstance().GetRecordOnlineStatus());   // 状态不变，沿用当前 presence
    }
    ::MessageBox(m_hWnd, _T("修改个性签名失败，请稍后重试。"),
                 _T("FlamingoIM"), MB_OK | MB_ICONWARNING);
}

void CBalloonMainPanel::OnChangePresenceResult(const net::ChangePresenceResponse& resp)
{
    if (resp.code == 0)
    {
        LOG_INFO("change presence ok, code=0.");
        return;   // 乐观更新已生效，无需额外动作
    }

    // 失败：把状态胶囊回退到改之前的状态并重绘 Header，并把"期望在线状态"一并回退
    // （避免后续断线重连沿用到这个没改成功的状态），最后提示用户。
    LOG_ERROR("change presence failed, code=%d, msg=%s.", resp.code, resp.msg.c_str());
    balloonmain::MainPanelHeader* header = FindMainPanelHeader(GetClientContent());
    if (header != NULL)
    {
        header->SetPresence(m_presenceBeforeChange);
        header->Invalidate();
    }
    CIUSocket::GetInstance().SetRecordOnlineStatus(m_presenceBeforeChange);
    ::MessageBox(m_hWnd, _T("修改在线状态失败，请稍后重试。"),
                 _T("FlamingoIM"), MB_OK | MB_ICONWARNING);
}

void CBalloonMainPanel::OnPresenceChanged(int64_t userid, int presence)
{
    // 1) 更新通讯录缓存里该 userid 的 presence —— 资料窗下次打开、或其它读 m_contacts 处即取新值。
    //    同时记录变化前的旧 presence，用于下面判定该好友是否"刚上线"。通讯录里查不到该好友
    //    时按离线（presence_offline=0）处理 —— 这样首次出现为在线也能算上线。
    int oldPresence = 0;
    net::ContactItem* pCached = FindCachedContactMutable((UINT)userid);
    if (pCached != NULL && pCached->m_userid == userid)
    {
        oldPresence = pCached->m_presence;
        pCached->m_presence = presence;
    }

    // 1.5) 好友从离线跳变为可见在线（上线）时，异步播放上线提示音。放在下面取面板
    //      控件的若干 early-return 之前，保证即便主面板客户区暂不可用也照常出声。自己的状态
    //      变化走 OnChangePresenceResult 路径、不经此函数，这里再排除一次自身 id 以防多设备回声误响。
    UINT selfId = (m_lpFMGClient != NULL) ? m_lpFMGClient->m_UserMgr.m_UserInfo.m_uUserID : 0;
    if ((UINT)userid != selfId && balloonmain::IsBuddyJustCameOnline(oldPresence, presence))
    {
        bool bMuted = (m_lpFMGClient != NULL) ? (m_lpFMGClient->m_UserConfig.IsEnableMute() != FALSE) : false;
        balloonmain::PlayNotifySound(balloonmain::kNotifySoundBuddyOnline, bMuted);
    }

    // 1.6) 「特别关注」的桌面提示：被关注的人上线 / 下线时，右下角弹一条。
    //      与上面的上线提示音同一处，但判定条件更严 —— 提示音只在上线时响一声，
    //      而这个弹窗会挡住屏幕右下角，故抑制条件要齐全，缺一条都会变成骚扰。
    MaybeShowStarredPresencePopup(userid, oldPresence, presence, selfId);

    // 2) 单聊窗：若与该好友的单聊窗正开着，刷新其头部状态胶囊。聊天窗是独立窗口、不依赖
    //    面板客户区，故放在下面取面板控件（可能早退）之前先做，确保不被跳过。
    std::map<UINT, CBalloonChatWindow*>::iterator wit = m_chatWindows.find((UINT)userid);
    if (wit != m_chatWindows.end() && wit->second != NULL && wit->second->IsWindow())
    {
        wit->second->SetPeerPresence(presence);
    }

    // 2.5) 群聊窗：已打开的群聊窗成员侧栏里该好友头像角上的状态徽标跟着变。群聊窗按 userid
    //      自己判断侧栏里有没有这个人，不在时只记录。同样放在下面可能早退的地方之前。
    //      本人的状态变化不走这里（见上面 1.5 的说明），由 NotifyMyPresenceChanged 负责
    if ((UINT)userid != selfId)
    {
        for (std::map<int64_t, CBalloonGroupChatWindow*>::iterator git = m_groupChatWindows.begin();
             git != m_groupChatWindows.end(); ++git)
        {
            if (git->second != NULL && git->second->IsWindow())
            {
                git->second->SetMemberPresence(userid, presence);
            }
        }
    }

    // 3) 联系人树上对应节点重着色 + 刷新。同一好友在树上只挂一个叶子，遍历"节点→userid"
    //    映射命中即按新 presence 重着状态点；改完之后整树重绘一次即可，不需要局部刷新。
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }
    balloonwjui::DuiTreeView* tv = dynamic_cast<balloonwjui::DuiTreeView*>(
        content->FindCtrlById(balloonmain::kIdContactsTree));
    if (tv == NULL)
    {
        return;
    }

    HBITMAP hBadge = GetPresenceBadgeBitmap(presence);
    bool changed = false;
    for (std::map<int, UINT>::iterator it = m_contactNodeUserId.begin();
         it != m_contactNodeUserId.end(); ++it)
    {
        if (it->second == (UINT)userid)
        {
            tv->SetItemStatusIcon(it->first, hBadge);
            changed = true;
        }
    }
    if (changed)
    {
        tv->Invalidate();
    }

    // 4) "消息"会话列表里若有与该好友的单聊会话，同步重着其状态点（群会话 ctype!=1 跳过）。
    //    m_msgNodeConv: 节点 → (ctype, cid)；单聊 cid 即对端 userid。
    balloonmain::MainPanelTreeView* mtv = dynamic_cast<balloonmain::MainPanelTreeView*>(
        content->FindCtrlById(balloonmain::kIdMessagesTree));
    if (mtv != NULL)
    {
        bool convChanged = false;
        for (std::map<int, std::pair<int, int64_t> >::iterator it = m_msgNodeConv.begin();
             it != m_msgNodeConv.end(); ++it)
        {
            if (it->second.first == 1 && it->second.second == userid)
            {
                mtv->SetItemStatusIcon(it->first, hBadge);
                convChanged = true;
            }
        }
        if (convChanged)
        {
            mtv->Invalidate();
        }
    }
}

void CBalloonMainPanel::OnUserInfoChanged(const net::UserInfoChangedNotify& notify)
{
    if (notify.m_userid <= 0)
    {
        return;
    }

    // 关键路径日志:确认推送到达本端并进入处理(内容长度即可判断昵称/签名是否带过来)。
    LOG_INFO("OnUserInfoChanged: userid=%lld, nicklen=%d, signlen=%d, facetype=%d.",
             (long long)notify.m_userid, (int)notify.m_nickname.size(),
             (int)notify.m_signature.size(), notify.m_facetype);

    // 1) 更新通讯录缓存里该 userid 的资料字段（头像 / 昵称 / 签名，供后续各处渲染取新值）。
    bool cacheHit = false;
    for (size_t i = 0; i < m_contacts.size(); ++i)
    {
        if (m_contacts[i].m_userid == notify.m_userid)
        {
            m_contacts[i].m_facetype   = notify.m_facetype;
            m_contacts[i].m_customface = notify.m_customface;
            if (!notify.m_nickname.empty())
            {
                m_contacts[i].m_nickname = notify.m_nickname;
            }
            // 签名允许为空（用户清空），服务端推送的是全量 profile，故无条件覆盖。
            m_contacts[i].m_signature = notify.m_signature;
            cacheHit = true;
            break;
        }
    }
    if (!cacheHit)
    {
        // 该 userid 不在通讯录缓存里 —— 后面各处刷新都会落空。多为"还没拉到/不是好友"。
        LOG_INFO("OnUserInfoChanged: userid=%lld NOT in contacts cache(size=%d), skip UI refresh.",
                 (long long)notify.m_userid, (int)m_contacts.size());
    }

    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 2) 联系人树：重画该 userid 节点头像（字形即时生效；真图未缓存则 GetEntityAvatarBitmap
    //    内部触发下载，下载完成再经 OnAvatarDownloadResult 刷新）。
    balloonwjui::DuiTreeView* ctv = dynamic_cast<balloonwjui::DuiTreeView*>(
        content->FindCtrlById(balloonmain::kIdContactsTree));
    int contactMatched = 0;
    if (ctv != NULL)
    {
        bool any = false;
        for (std::map<int, UINT>::iterator it = m_contactNodeUserId.begin();
             it != m_contactNodeUserId.end(); ++it)
        {
            if (it->second != (UINT)notify.m_userid)
            {
                continue;
            }
            const net::ContactItem* pc = FindCachedContact(it->second);
            if (pc == NULL)
            {
                continue;
            }
            // 真名（不含备注）—— 头像首字取的是姓，带上"（备注）"会取错字。
            const CString realName = balloonmain::ResolveContactRealName(
                Utf8ToCStr(pc->m_nickname), Utf8ToCStr(pc->m_username), (UINT)pc->m_userid);
            // 显示名文字同步更新（昵称变了即时反映）。必须走 ContactDisplayName 取
            // 「真名（备注）」，不能直接写昵称 —— 早先这里写死取昵称，于是每收到一条
            // 资料变更推送，用户刚设的备注就被从树上冲掉，表现为"备注设了又没了"。
            ctv->SetItemLabel(it->first, ContactDisplayName((UINT)pc->m_userid));
            // 副行个性签名同步更新（与 AddContactLeaf 一致：联系人节点副标签 = 签名）。
            ctv->SetItemSubLabel(it->first, Utf8ToCStr(pc->m_signature));
            HBITMAP hbm = GetEntityAvatarBitmap(pc->m_userid, realName,
                                                pc->m_facetype, pc->m_customface, kContactAvatarSizePx);
            if (hbm != NULL)
            {
                ctv->SetItemIcon(it->first, hbm);   //位图归头像缓存所有，这里只借用
            }
            any = true;
            ++contactMatched;
        }
        if (any)
        {
            ctv->Invalidate();
        }
    }
    LOG_INFO("OnUserInfoChanged: contacts tree=%s, matched %d node(s) for userid=%lld (nodeMap=%d).",
             (ctv != NULL ? "found" : "MISSING"), contactMatched,
             (long long)notify.m_userid, (int)m_contactNodeUserId.size());

    // 3) 单聊会话节点（cid==该 userid）同步重画头像。
    balloonwjui::DuiTreeView* mtv = dynamic_cast<balloonwjui::DuiTreeView*>(
        content->FindCtrlById(balloonmain::kIdMessagesTree));
    if (mtv != NULL)
    {
        const int kMsgAvatarSizePx = 36;   // 须与 FillConversationsTree 内一致
        bool any = false;
        for (std::map<int, std::pair<int, int64_t> >::iterator it = m_msgNodeConv.begin();
             it != m_msgNodeConv.end(); ++it)
        {
            if (it->second.first != 1 || it->second.second != notify.m_userid)
            {
                continue;
            }
            const net::ContactItem* pc = FindCachedContact((UINT)notify.m_userid);
            // 真名（不含备注）供头像首字用；带备注的显示名供会话行文字用。
            CString name;
            CString display;
            int facetype = balloonmain::avatar_kind_auto;
            std::string customface;
            if (pc != NULL)
            {
                name = balloonmain::ResolveContactRealName(
                    Utf8ToCStr(pc->m_nickname), Utf8ToCStr(pc->m_username), (UINT)pc->m_userid);
                display    = ContactDisplayName((UINT)notify.m_userid);
                facetype   = pc->m_facetype;
                customface = pc->m_customface;
            }
            // 会话名文字同步更新（与单聊窗标题一致）。同上，这里也必须带备注，
            // 否则一条资料变更推送就会把会话列表里的备注冲掉。
            if (!display.IsEmpty())
            {
                mtv->SetItemLabel(it->first, display);
            }
            HBITMAP hbm = GetEntityAvatarBitmap(notify.m_userid, name, facetype, customface, kMsgAvatarSizePx);
            if (hbm != NULL)
            {
                mtv->SetItemIcon(it->first, hbm);   //位图归头像缓存所有，这里只借用
            }
            any = true;
        }
        if (any)
        {
            mtv->Invalidate();
        }
    }

    // 4) 好友资料窗：正展示该联系人时后台静默刷新（不前置、不抢焦点）。
    if (m_contactWindow.IsWindow() &&
        m_contactWindow.GetPeerUserId() == (UINT)notify.m_userid)
    {
        ballooncontact::ContactModel model;
        if (BuildContactModelFromCache((UINT)notify.m_userid, model))
        {
            m_contactWindow.RefreshContactData(model);
        }
    }

    // 5) 单聊窗口：开着则即时更新对端昵称 / 签名 / 头像（标题 / 标题栏 / 任务栏图标 / 头部签名）。
    std::map<UINT, CBalloonChatWindow*>::iterator cit =
        m_chatWindows.find((UINT)notify.m_userid);
    if (cit != m_chatWindows.end() && cit->second != NULL)
    {
        // 标题上的名字走 ContactDisplayName 取「真名（备注）」，不能直接用推送里的
        // 昵称 —— 那样每来一条资料变更推送就把聊天窗标题上的备注冲掉。此刻缓存里的
        // 昵称已由本函数前半段更新过，取出来的是新值。
        CString newName = ContactDisplayName((UINT)notify.m_userid);
        CString newSign = Utf8ToCStr(notify.m_signature);
        cit->second->UpdatePeerInfo(newName, newSign, notify.m_facetype, notify.m_customface);
    }
}

void CBalloonMainPanel::OnOrgStructureChanged(const net::OrgStructureChangedNotify& notify)
{
    // 关键路径日志：这条推送是"新同事出现在通讯录里"的唯一触发点，出问题时要能
    // 从日志上直接看出它到底有没有到达 UI 层（对照 IUSocket 那条 "Recv v2
    // org-structure-changed notify"，两条都在才说明四层管道通了）。
    LOG_INFO("OnOrgStructureChanged: changetype=%d, deptid=%lld, refetch contacts.",
             notify.m_changetype, (long long)notify.m_deptid);

    // 整份重拉。应答经 FMG_MSG_V2_CONTACTS 回到 OnContactsArrived，那里会重建
    // 联系人树、m_contacts 缓存与部门列表，与登录时走的是同一条路径。
    CIUSocket::GetInstance().RequestContacts();
}

void CBalloonMainPanel::ResetUnknownSenderPolicy()
{
    m_unknownSenderPolicy.Reset();
}

void CBalloonMainPanel::ResetHistoryCoverage()
{
    //先确保打开的是本次登录账号的库：切换账号后 EnsureMsgStoreOpen 会关旧库、开新库，
    //清的必须是新账号的覆盖区间
    if (!EnsureMsgStoreOpen())
    {
        LOG_ERROR("Reset history coverage skipped: local message store not ready.");
        return;
    }
    if (!m_msgStore.ClearAllCoverage())
    {
        LOG_ERROR("Reset history coverage failed.");
        return;
    }
    LOG_INFO("History coverage reset after login.");
}

void CBalloonMainPanel::ResetReadReportDedupe()
{
    m_readReportDedupe.Reset();
    LOG_INFO("Read report dedupe reset.");
}

void CBalloonMainPanel::OnSearchPick(void* userdata, UINT peerUserId)
{
    CBalloonMainPanel* self = (CBalloonMainPanel*)userdata;
    if (self == NULL)
    {
        return;
    }

    // 搜索结果里现在也能命中自己。与通讯录树双击同一口径：聊天不允许，就打开
    // 自己的资料，而不是点了没反应。
    if (self->IsSelfUserId(peerUserId))
    {
        LOG_INFO("搜索结果选中的是自己，打开个人信息窗。");
        self->OpenProfileDlg();
        return;
    }

    self->OpenSingleChat(peerUserId);
}

void CBalloonMainPanel::OnSearchPickGroup(void* userdata, int64_t groupId)
{
    CBalloonMainPanel* self = (CBalloonMainPanel*)userdata;
    if (self != NULL)
    {
        self->OpenGroupChat(groupId);
    }
}

void CBalloonMainPanel::OnSearchLocate(void* userdata, UINT peerUserId)
{
    CBalloonMainPanel* self = (CBalloonMainPanel*)userdata;
    if (self == NULL)
    {
        return;
    }

    //本回调是在结果行的通知调用栈里被同步调到的，此刻那一批行控件还活着。
    //LocateContactInTree 里只会调下拉的 HideDropdown（仅隐藏窗口、不重建控件树），
    //<u>不要</u>在这条路径上去调 UpdateQuery —— 那会重建结果列表、把正在派发通知的
    //那个行控件当场析构掉。
    self->LocateContactInTree(peerUserId);
}

void CBalloonMainPanel::ToggleSection(balloonmain::MainPanelSectionHeader* sec)
{
    if (sec == NULL)
    {
        return;
    }
    sec->SetExpanded(!sec->IsExpanded());
    const bool show = sec->IsExpanded();

    // 分组标题的父控件即所在的列表面板（vbox）。
    balloonwjui::DuiControl* pane = sec->GetParent();
    if (pane == NULL)
    {
        return;
    }

    // 定位 sec 在面板子控件中的下标。
    const std::vector<std::unique_ptr<balloonwjui::DuiControl> >& kids =
        pane->Children();
    size_t secIdx = 0;
    bool found = false;
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (kids[i].get() == sec)
        {
            secIdx = i;
            found = true;
            break;
        }
    }
    if (!found)
    {
        return;
    }

    // sec 之后、下一个分组标题之前的所有控件即本分组的列表项 —— 随展开
    // 态显隐。
    for (size_t i = secIdx + 1; i < kids.size(); ++i)
    {
        if (dynamic_cast<balloonmain::MainPanelSectionHeader*>(kids[i].get()) != NULL)
        {
            break;
        }
        kids[i]->SetVisible(show);
    }

    // 面板内子控件显隐变了 —— 强制其所在滚动视口重新布局：滚动视口会
    // 重新测量内容高度、重排内容 vbox、刷新滚动范围，再整窗重绘。
    balloonwjui::DuiControl* scrollView = pane->GetParent();
    if (scrollView != NULL)
    {
        scrollView->ForceLayout(scrollView->GetRect());
    }
    else
    {
        pane->ForceLayout(pane->GetRect());
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

void CBalloonMainPanel::SwitchTab(UINT tabId)
{
    balloonwjui::DuiControl* content = GetClientContent();
    if (content == NULL)
    {
        return;
    }

    // 记下当前 Tab，供底部"建群/查找"按钮按页分发。
    m_currentTabId = tabId;

    // 切 Tab 收起搜索结果下拉（避免它继续盖在新页内容上）。
    m_searchDropdown.HideDropdown();

    // 三个 Tab 与其内容面板的对应关系。
    struct TabPanePair
    {
        UINT tabId;
        UINT paneId;
    };
    const TabPanePair kPairs[3] =
    {
        { balloonmain::kIdTabMessages, balloonmain::kIdPaneMessages },
        { balloonmain::kIdTabContacts, balloonmain::kIdPaneContacts },
        { balloonmain::kIdTabGroups,   balloonmain::kIdPaneGroups   },
    };

    // 选中态：被点的 Tab 高亮，对应内容面板显示，其余隐藏。
    for (int i = 0; i < 3; ++i)
    {
        balloonmain::MainPanelTab* tab = dynamic_cast<balloonmain::MainPanelTab*>(
            content->FindCtrlById(kPairs[i].tabId));
        if (tab != NULL)
        {
            tab->SetActive(kPairs[i].tabId == tabId);
        }
        balloonwjui::DuiControl* pane = content->FindCtrlById(kPairs[i].paneId);
        if (pane != NULL)
        {
            const bool bActive = (kPairs[i].tabId == tabId);
            pane->SetVisible(bActive);

            // 被切走的面板：滚动条直接收掉，不留着在后台慢慢淡出 —— 它已经
            // 看不见了，还占着 pulse 定时器没有意义；下次切回来也该是干净的
            // 隐藏态，而不是残留上一次的半透明滑块。
            if (!bActive)
            {
                balloonmain::MainPanelScrollView* sv =
                    dynamic_cast<balloonmain::MainPanelScrollView*>(pane);
                if (sv != NULL)
                {
                    sv->HideScrollBarNow();
                }
            }
        }
    }

    // 底部主操作按钮文字：群页"建群"，其余"查找"。
    balloonmain::MainPanelActionButton* action =
        dynamic_cast<balloonmain::MainPanelActionButton*>(
            content->FindCtrlById(balloonmain::kIdBottomAction));
    if (action != NULL)
    {
        action->SetText(tabId == balloonmain::kIdTabGroups
                        ? _T("建群") : _T("查找"));
    }

    // 内容面板显隐变了 —— 对"内容区容器"（三个面板的父 vbox）强制重新
    // 布局：它会按新的显隐重新分配空间，当前面板随之撑满内容区。
    // 注意不能只 ForceLayout 客户区根 —— 根的 Layout 用 SetRect 向下派发，
    // 内容区容器的矩形本身没变，会被 SetRect 的 EqualRect 短路、不再向下
    // 重排，于是面板停留在初始的三等分高度。
    balloonwjui::DuiControl* anyPane =
        content->FindCtrlById(balloonmain::kIdPaneContacts);
    balloonwjui::DuiControl* contentArea =
        (anyPane != NULL) ? anyPane->GetParent() : NULL;
    if (contentArea != NULL)
    {
        contentArea->ForceLayout(contentArea->GetRect());
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}
