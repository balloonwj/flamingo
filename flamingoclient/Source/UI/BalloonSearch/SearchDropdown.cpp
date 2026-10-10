/**
 *  SearchDropdown.cpp —— 主面板搜索结果下拉实现。
 *  balloonwj@qq.com   2026-06-04
 */
#include "stdafx.h"
#include "SearchDropdown.h"

#include <memory>
#include <string>
#include <vector>

#include "SearchFilterTab.h"
#include "SearchResultScrollView.h"   // 结果滚动区：聊天窗样式的细滑块
#include "ContactMatch.h"
#include "GlobalSearchText.h"                    // 「消息」「文件」标签结果行的文案与时间文字
#include "../BalloonHistory/V2MsgSearchWorker.h" // 本地库后台查询的请求种类与结果结构
#include "../BalloonHistory/HistoryView.h"       // FormatFileSize：文件结果行的大小文字
#include "../BalloonHistory/HistoryLocalFile.h"  // ResolveRecordLocalPath：文件结果在本机的路径
#include <shellapi.h>                            // ShellExecute：在本机打开文件或文件夹
#include "../../Common/Graphics/ShellTypeIcon.h" // 文件结果行行首的系统类型图标
#include "../../Common/Log/IULog.h"
#include "../../Common/Util/EncodeUtil.h"
#include "../../Common/Util/PerfStopWatch.h"      // 预热搜索候选缓存的耗时记入日志
#include "../../net/ContactsProtocolClient.h"     // net::ContactItem
#include "../../net/GroupProtocolClient.h"        // net::UserGroupItem
#include "../../net/Protocol.h"                   // conversation_single / conversation_group
#include "../../net/MemoProtocolClient.h"         // net::MemoItem（「备忘」一段）
#include "../BalloonMain/MainPanelListItem.h"  // 结果行（含命中高亮）
#include "../BalloonMain/GroupRowText.h"       // BuildGroupRowTexts（群结果行的"群名+人数 / 群号"文案）
#include "../BalloonMain/AvatarSpec.h"         // DecodeAvatar：按 (facetype,customface) 定该画什么
#include "../BalloonMain/AvatarImageUtil.h"    // MakeRoundedAvatarFromFile：自定义图头像位图
#include "../../net/ImageTransfer.h"
#include "../../net/CurrentUserAccount.h"   // net::CurrentUserAccount（本地缓存目录按登录账号分）              // 头像真图的本地缓存路径与下载
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"      // DuiVBox / DuiHBox / Hint
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"   // 「查看更多」文字链按钮
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView
#include "../../Common/I18n/I18n.h"   // TR：界面文字的多语言查找

// ---- 文件作用域常量 -----------------------------------------------------

// 各行高度（逻辑像素）。
static const int kTabRowHeight       = 38;   // 顶部筛选标签栏高
static const int kResultRowHeight    = 54;   // 单条结果行高
static const int kResultRowStride    = kResultRowHeight + 1;  // 行高 + 结果 vbox gap(1)
static const int kSectionHeaderHeight = 24;  // "联系人"/"群组"分组标题高
static const int kNoResultHintHeight = 44;   // "无匹配结果"提示高

// 结果区内容最大高度（超出则滚动）：约 2 个分组标题 + 6 行。
static const int kMaxContentHeight =
    2 * kSectionHeaderHeight + 6 * kResultRowStride;   // 378
// 结果区底部留白，避免最后一行贴边。
static const int kBottomPad = 6;

// 标签栏内边距与标签间距。标签按文字宽度排版（Hint().Auto()，见 SearchFilterTab::GetDesiredSize），
// 每个标签自带左右空白，间距再留 4 像素，相邻两段文字之间约 12 像素。
static const int kTabBarPadLeft = 10;
static const int kTabBarGap     = 4;


// 延迟查询（本地聊天记录、本地文件、群文件）：停止输入这么久（毫秒）之后才查。取值与
// 消息记录窗搜索框的延迟查询相同（BalloonHistoryWindow.cpp 的 kSearchDebounceMs），两处
// 打字手感一致。
static const UINT kDelayedSearchMs = 250;
// 延迟查询所用定时器的编号（本窗口私有，DuiHost 与各控件都不用 WM_TIMER）。
static const UINT_PTR kDelayedSearchTimerId = 1;
// 「消息」标签最多列出多少个会话。更多的命中请用户输入更具体的词，或到消息记录窗里查。
static const int kMessageHitLimit = 50;
// 「聊天文件」一段最多列出多少条。
static const int kFileHitLimit = 50;
// 「群文件」一段请服务端最多返回多少条（服务端上限 GROUP_FILE_SEARCH_LIMIT_MAX 为 50）。
static const int kGroupFileHitLimit = 30;
// 文件结果行行首系统图标的边长（像素），与联系人行的头像同尺寸，各行的文字左边对齐。
static const int kFileIconPx = balloonmain::MainPanelListItem::kAvatarSizePx;

// 以下界面文字是文件作用域常量，在语言确定之前就已初始化，只能用 TR_NOOP 标记，到使用处
// 再调 TR（开发约定文档「UI 约定」第 8 条）。
// 「消息」标签分组标题。只查本机已保存的记录、不向服务端查询，标题里写明，免得用户以为
// 搜不到的旧消息就是不存在。
static const wchar_t* const kSectionMessagesText = TR_NOOP(L"聊天记录（仅搜索本机已保存的记录）");
// 「文件」标签两段的分组标题。聊天文件同样只查本机已保存的记录。
static const wchar_t* const kSectionChatFilesText  = TR_NOOP(L"聊天文件（仅搜索本机已保存的记录）");
static const wchar_t* const kSectionGroupFilesText = TR_NOOP(L"群文件");
// 本地查询还在等待延迟查询时，「消息」「文件」标签下显示的提示。
static const wchar_t* const kSearchingHintText = TR_NOOP(L"正在搜索…");
// 分组内的矮行（提示文字如「群文件需联网搜索」、「查看更多」按钮）一行的高度（逻辑像素）。
static const int kSectionHintHeight = 32;
static const int kSectionHintStride = kSectionHintHeight + 1;   // 矮行高 + 结果 vbox gap(1)

// 「全部」标签里聊天记录、文件两段各最多列几条，更多的点段末「查看更多」到对应标签里看。
static const size_t kAllTabSectionMax = 3;
// 「全部」标签里文件那一段的标题（本地聊天文件与群文件合在一起）。
static const wchar_t* const kSectionFilesText = TR_NOOP(L"文件");
// 「全部」标签两段末尾「查看更多」按钮的文字。
static const wchar_t* const kMoreMessagesText = TR_NOOP(L"查看更多聊天记录");
static const wchar_t* const kMoreFilesText    = TR_NOOP(L"查看更多文件");
// 「消息」标签结果末尾那个按钮的文字：打开聊天记录管理器按同一个关键字跨会话搜索。下拉最多
// 列 kMessageHitLimit 个会话、每个会话只汇总成一行，管理器里能看到全部会话的每一条命中。
static const wchar_t* const kOpenMessageManagerText = TR_NOOP(L"在聊天记录管理器中查看全部");
// 「消息」标签末尾服务器搜索那部分的文字：按钮、两段的分组标题、本机与服务器都没有命中时的提示。
static const wchar_t* const kSearchServerText          = TR_NOOP(L"搜索服务器上的聊天记录");
static const wchar_t* const kSectionServerMessagesText = TR_NOOP(L"服务器上的聊天记录");
static const wchar_t* const kSectionAnnouncementsText  = TR_NOOP(L"群公告");
static const wchar_t* const kNoLocalMessagesText       = TR_NOOP(L"本机没有匹配的聊天记录");
static const wchar_t* const kNoServerMessagesText      = TR_NOOP(L"服务器上没有匹配的聊天记录");
// 「部门」一段：分组标题、部门行头像的字与底色（与通讯录树里的部门行无关，只用来和人、群区分开）、
// 顶级部门副行的文字。
static const wchar_t* const kSectionDepartmentsText = TR_NOOP(L"部门");
static const wchar_t* const kDeptAvatarGlyph        = TR_NOOP(L"部");
static const COLORREF       kDeptAvatarColor        = RGB(0x5B, 0x8F, 0xF9);
static const wchar_t* const kTopDepartmentText      = TR_NOOP(L"顶级部门");
// 「备忘」一段：分组标题、备忘行头像的字与底色、个人备忘副行的归属文字、群名取不到时的归属文字，
// 以及最多列几条（「备忘」没有自己的标签，只在「全部」里出现，列得太多会把后面的聊天记录挤下去）。
static const wchar_t* const kSectionMemosText  = TR_NOOP(L"备忘");
static const wchar_t* const kMemoAvatarGlyph   = TR_NOOP(L"备");
static const COLORREF       kMemoAvatarColor   = RGB(0xF5, 0xA6, 0x23);
static const wchar_t* const kPersonalMemoText  = TR_NOOP(L"个人备忘");
static const wchar_t* const kGroupMemoFallback = TR_NOOP(L"群聊");
static const size_t kMemoHitLimit = 5;
// 服务器搜索最多要几个会话、几条群公告（服务端上限分别为 50、30）。
static const int kServerSearchLimit       = 20;
static const int kServerAnnouncementLimit = 10;
// 分组标题与「查看更多」按钮的左缩进（像素），与上方标签栏的文字左边对齐（kTabBarPadLeft）。
// 不缩进时这两种文字紧贴下拉的左边框（2026-10-03 真实界面核对时发现）。
static const int kSectionIndent = kTabBarPadLeft;

// 「最近搜索」列表：标题行的文字、右侧「清空」按钮的文字与宽度（像素，按中英文两种文字中
// 较宽的那个留出余量；DuiButton 不按文字计算宽度，只能给定值）。
static const wchar_t* const kSectionRecentText = TR_NOOP(L"最近搜索");
static const wchar_t* const kClearRecentText   = TR_NOOP(L"清空");
static const int kClearRecentWidth = 56;
// kMsgDeferredRecentAction 的 wParam 取这个值表示点的是「清空」，其余取值是关键字的下标。
static const WPARAM kRecentClearIndex = (WPARAM)-1;

// 下拉内筛选标签控件 id（9100 段，避开主面板 200/210-212/220-222/230/231/240）。
static const UINT kIdTabAll      = 9101;
static const UINT kIdTabContacts = 9102;
static const UINT kIdTabGroups   = 9103;
static const UINT kIdTabMessages = 9104;
static const UINT kIdTabFiles    = 9105;

// 卡片底色 / 描边色 / 分组标题文字色。
static const COLORREF kCardBg      = RGB(0xFF, 0xFF, 0xFF);
static const COLORREF kCardBorder  = RGB(0xDD, 0xDD, 0xDD);
static const COLORREF kSectionColor = RGB(0x9A, 0x9A, 0x9A);
// 结果行悬停底色 —— 比 MainPanelListItem 默认的极浅灰更明显（浅蓝灰），让鼠标
// 移到结果上时有清晰反馈。
static const COLORREF kRowHoverFill = RGB(0xE9, 0xEC, 0xF1);

// ---- 编码与配色辅助 -----------------------------------------------------

// UTF-8 → std::wstring。
static std::wstring Utf8ToWStr(const std::string& utf8)
{
    if (utf8.empty())
    {
        return std::wstring();
    }
    std::vector<wchar_t> buf(utf8.size() + 1, 0);
    EncodeUtil::Utf8ToUnicode(utf8.c_str(), buf.data(), (int)buf.size());
    return std::wstring(buf.data());
}

// UTF-8 → CString。
static CString Utf8ToCStr(const std::string& utf8)
{
    std::wstring w = Utf8ToWStr(utf8);
    return CString(w.c_str());
}

// 给一行搜索结果装上头像：尊重对方 / 该群设定的 (facetype, customface)——
// 自定义图命中本地缓存就贴真图，字形头像画其选定的字与底色，自动头像回落
// 「名字首字 + 按 id 取色」。与联系人树、会话列表、群列表同一套解码（DecodeAvatar），
// 故同一个人在各处的头像长得一样。
//
//   item：       待装配的结果行（本函数不持有它）。
//   entityId：   联系人 userid 或群 groupid，自动头像据它取底色。
//   displayName：行上显示的名字，自动头像取其首字。
//   facetype / customfaceUtf8：对方 / 该群存的头像两项。
//   selfId：     当前登录用户 userid，真图缓存按它分桶；为 0（未登录）时不碰真图。
static void ApplyRowAvatar(balloonmain::MainPanelListItem* item, int64_t entityId,
                           const CString& displayName, int facetype,
                           const std::string& customfaceUtf8, int64_t selfId)
{
    balloonmain::AvatarSpec spec = balloonmain::DecodeAvatar(
        facetype, customfaceUtf8, displayName, entityId);

    if (spec.m_kind == balloonmain::avatar_kind_image && !customfaceUtf8.empty() && selfId > 0)
    {
        const std::wstring path =
            net::ImageTransfer::AvatarCacheFilePathForFileId(net::CurrentUserAccount(),
                                                             customfaceUtf8);
        if (!path.empty() && ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            HBITMAP hbm = balloonmain::MakeRoundedAvatarFromFile(
                path.c_str(), balloonmain::MainPanelListItem::kAvatarSizePx,
                balloonmain::MainPanelListItem::kAvatarRadiusPx);
            if (hbm != NULL)
            {
                //位图交给行控件持有，行销毁时一并释放。
                item->SetOwnedAvatarBitmap(hbm);
                //字形仍设上：真图万一画不出来（文件被删等）还有个兜底。
                item->SetAvatar(spec.m_glyph, spec.m_color);
                return;
            }
        }

        //缓存没命中：这一次先画字形占位，同时把图拉下来 —— 下拉是随手开随手关的
        //浮窗，每次搜索都整批重建行，不值得为一张头像单独回刷，下次打开就是真图了。
        net::ImageTransfer::GetInstance().EnqueueAvatarDownload(customfaceUtf8,
                                                                net::CurrentUserAccount());
    }

    item->SetAvatar(spec.m_glyph, spec.m_color);
}

// 去 CString 首尾空白。
static CString TrimCopy(const CString& s)
{
    CString t = s;
    t.Trim();
    return t;
}

// =======================================================================
// 构造 / 析构
// =======================================================================

CSearchDropdown::CSearchDropdown()
    : m_contacts(NULL)
    , m_groups(NULL)
    , m_selfUserId(0)
    , m_includeSelf(false)
    , m_contactCb(NULL)
    , m_groupCb(NULL)
    , m_pickUd(NULL)
    , m_locateCb(NULL)      // 宿主未提供定位能力时，结果行不显示定位按钮
    , m_deptPaths(NULL)     // 没有部门信息时，联系人副行退回展示账号与地区
    , m_built(false)
    , m_filter(kFilterAll)
    , m_panelWidth(0)
    , m_anchorLeft(0)
    , m_anchorBottom(0)
    , m_scroll(NULL)
    , m_tabAll(NULL)
    , m_tabContacts(NULL)
    , m_tabGroups(NULL)
    , m_tabMessages(NULL)
    , m_tabFiles(NULL)
    , m_tabRow(NULL)
    , m_moreMessagesBtn(NULL)
    , m_moreFilesBtn(NULL)
    , m_submitMsgCb(NULL)          // 宿主未提供本地聊天记录查询时，「消息」标签置灰
    , m_pickMsgCb(NULL)
    , m_submitFileCb(NULL)         // 宿主未提供文件查询时，「文件」标签置灰
    , m_requestGroupFileCb(NULL)
    , m_pickFileCb(NULL)
    , m_pickGroupFileCb(NULL)
    , m_msgSeq(0)
    , m_fileSeq(0)
    , m_searchPending(false)
    , m_hOwner(NULL)
    , m_memos(NULL)                // 宿主未提供备忘簿时不搜备忘
    , m_pickMemoCb(NULL)
    , m_departments(NULL)          // 宿主未提供部门列表时不搜部门
    , m_locateDeptCb(NULL)
    , m_requestServerCb(NULL)      // 宿主未提供时不显示「搜索服务器上的聊天记录」
    , m_pickServerMsgCb(NULL)
    , m_pickAnnCb(NULL)
    , m_serverSearchBtn(NULL)
    , m_openMsgManagerCb(NULL)     // 宿主未提供时「消息」标签底部不显示管理器入口
    , m_openMsgManagerBtn(NULL)
    , m_recordRecentCb(NULL)       // 宿主未提供时不记录最近搜索
    , m_pickRecentCb(NULL)
    , m_clearRecentCb(NULL)
    , m_recentMode(false)
    , m_clearRecentBtn(NULL)
{
}

CSearchDropdown::~CSearchDropdown()
{
    // 控件树由 DuiHost 基类析构释放；本类只持借用指针。
}

void CSearchDropdown::SetData(const std::vector<net::ContactItem>* contacts,
                              const std::vector<net::UserGroupItem>* groups,
                              UINT selfUserId,
                              bool includeSelf)
{
    m_contacts    = contacts;
    m_groups      = groups;
    m_selfUserId  = selfUserId;
    m_includeSelf = includeSelf;
}

void CSearchDropdown::SetPickCallbacks(PickContactCallback contactCb,
                                       PickGroupCallback groupCb, void* userdata)
{
    m_contactCb = contactCb;
    m_groupCb   = groupCb;
    m_pickUd    = userdata;
}

void CSearchDropdown::SetLocateCallback(LocateContactCallback locateCb)
{
    m_locateCb = locateCb;
}

void CSearchDropdown::SetContactDeptPaths(const std::map<UINT, CString>* deptPaths)
{
    m_deptPaths = deptPaths;
}

void CSearchDropdown::SetMessageCallbacks(SubmitLocalSearchCallback submitCb, PickMessageCallback pickCb)
{
    m_submitMsgCb = submitCb;
    m_pickMsgCb   = pickCb;

    //查询来源变了，已有结果作废。
    ResetDelayedResults();
    UpdateTabAvailability();
}

void CSearchDropdown::SetMessageManagerCallback(OpenMessageManagerCallback managerCb)
{
    m_openMsgManagerCb = managerCb;
}

void CSearchDropdown::SetDepartments(const std::vector<net::DepartmentItem>* departments,
                                     LocateDepartmentCallback locateCb)
{
    m_departments  = departments;
    m_locateDeptCb = locateCb;
}

void CSearchDropdown::SetMemos(const std::vector<net::MemoItem>* memos, PickMemoCallback pickCb)
{
    m_memos      = memos;
    m_pickMemoCb = pickCb;
}

void CSearchDropdown::SetServerSearchCallbacks(RequestServerSearchCallback requestCb,
                                               PickServerMessageCallback pickMsgCb,
                                               PickAnnouncementCallback pickAnnCb)
{
    m_requestServerCb = requestCb;
    m_pickServerMsgCb = pickMsgCb;
    m_pickAnnCb       = pickAnnCb;
}

void CSearchDropdown::OnServerSearchResult(const net::GlobalSearchResult& result)
{
    const bool success = (result.m_code == 0);
    if (!m_serverState.OnResponse(result.m_keyword, success))
    {
        //用户已经接着输入、或下拉已收起：这是一次过期的应答。关键字只记字节数。
        LOG_INFO("search: discard stale server search result (keyword %u bytes, code=%d).",
                 (unsigned)result.m_keyword.size(), result.m_code);
        return;
    }
    if (success)
    {
        m_serverConvs = result.m_conversations;
        m_serverAnns  = result.m_announcements;
    }
    else
    {
        m_serverConvs.clear();
        m_serverAnns.clear();
    }
    LOG_INFO("search: server search result accepted, code=%d, conversations=%u, announcements=%u.",
             result.m_code, (unsigned)result.m_conversations.size(), (unsigned)result.m_announcements.size());
    if (IsShown())
    {
        RefreshResults();
    }
}

LRESULT CSearchDropdown::OnDeferredServerSearch(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/,
                                                BOOL& /*bHandled*/)
{
    //消息排队期间下拉可能已经收起，或换成了最近搜索，这时不再发请求
    if (!IsShown() || m_recentMode || m_requestServerCb == NULL || m_query.IsEmpty())
    {
        return 0;
    }
    const std::string keywordUtf8 = EncodeUtil::UnicodeToUtf8(std::wstring((LPCWSTR)m_query));
    if (m_requestServerCb(m_pickUd, keywordUtf8, kServerSearchLimit, kServerAnnouncementLimit))
    {
        m_serverState.OnRequestSent(keywordUtf8);
    }
    else
    {
        m_serverState.OnOffline(keywordUtf8);
    }
    LOG_INFO("search: server search requested (keyword %u bytes).", (unsigned)keywordUtf8.size());
    RefreshResults();
    return 0;
}

void CSearchDropdown::AppendHintRow(balloonwjui::DuiVBox& content, LPCTSTR text)
{
    std::unique_ptr<balloonwjui::DuiLabel> hint(new balloonwjui::DuiLabel());
    hint->SetText(text);
    hint->SetTextColor(kSectionColor);
    hint->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    content.AddChild(std::move(hint), balloonwjui::DuiLayout::Hint().Fixed(kSectionHintHeight));
}

void CSearchDropdown::AppendServerResults(balloonwjui::DuiVBox& content, const ConversationIndex& index,
                                          const std::string& queryUtf8, int& sections, int& rows, int& smallRows)
{
    const balloonsearch::GroupFileSearchStatus status = m_serverState.StatusFor(queryUtf8);
    //还没搜：放按钮
    if (status == balloonsearch::kGroupFileIdle)
    {
        m_serverSearchBtn = AppendMoreButton(content, TR(kSearchServerText));
        ++smallRows;
        return;
    }
    //在等应答、没联网、失败：分组标题加一行提示
    AppendSectionHeader(content, TR(kSectionServerMessagesText));
    ++sections;
    if (status != balloonsearch::kGroupFileDone)
    {
        AppendHintRow(content, balloonsearch::ServerSearchStatusHint(status).c_str());
        ++smallRows;
        return;
    }

    //有结果：服务器上的聊天记录（按会话汇总，行的版式与本机聊天记录相同）
    const std::wstring keyword((LPCWSTR)m_query);
    int convRows = 0;
    for (size_t i = 0; i < m_serverConvs.size(); ++i)
    {
        const net::GlobalSearchConversation& hit = m_serverConvs[i];
        CString     name;
        int         facetype = 0;
        std::string customface;
        //会话列表、通讯录与群列表里都查不到的会话（已离职的同事等）不列出，点进去也打不开
        if (!ResolveConversation(index, hit.m_ctype, hit.m_cid, name, facetype, customface))
        {
            continue;
        }
        const balloonsearch::HighlightedText sub =
            balloonsearch::BuildMessageHitSubText(hit.m_hitCount, Utf8ToWStr(hit.m_latestText), keyword);
        const std::wstring timeText = balloonsearch::FormatHitTimeFromUnixMs(hit.m_latestTime);
        std::unique_ptr<balloonmain::MainPanelListItem> item(new balloonmain::MainPanelListItem());
        ApplyRowAvatar(item.get(), hit.m_cid, name, facetype, customface, (int64_t)m_selfUserId);
        item->SetName(name);
        item->SetRightText(timeText.c_str());
        item->SetSubText(sub.m_text.c_str());
        item->SetSubHighlight(sub.m_hiStart, sub.m_hiLen);
        item->SetHoverFill(kRowHoverFill);
        m_rowServerConv[item.get()] = i;
        content.AddChild(std::move(item), balloonwjui::DuiLayout::Hint().Fixed(kResultRowHeight));
        ++convRows;
    }
    rows += convRows;
    if (convRows == 0)
    {
        AppendHintRow(content, TR(kNoServerMessagesText));
        ++smallRows;
    }

    //群公告：标题当主行（标出关键字），副行是群名与正文摘录
    int annRows = 0;
    for (size_t i = 0; i < m_serverAnns.size(); ++i)
    {
        const net::GlobalSearchAnnouncement& ann = m_serverAnns[i];
        CString     groupName;
        int         facetype = 0;
        std::string customface;
        if (!ResolveConversation(index, ::conversation_group, ann.m_groupid, groupName, facetype, customface))
        {
            continue;
        }
        if (annRows == 0)
        {
            AppendSectionHeader(content, TR(kSectionAnnouncementsText));
            ++sections;
        }
        const std::wstring title = Utf8ToWStr(ann.m_title);
        const int titleHit = balloonsearch::FindKeywordNoCase(title, keyword);
        std::wstring subText = std::wstring((LPCWSTR)groupName);
        const std::wstring excerpt = Utf8ToWStr(ann.m_excerpt);
        if (!excerpt.empty())
        {
            subText += L" \x00B7 ";
            subText += excerpt;
        }
        const std::wstring timeText = balloonsearch::FormatHitTimeFromUnixMs(ann.m_publishTime);
        std::unique_ptr<balloonmain::MainPanelListItem> item(new balloonmain::MainPanelListItem());
        ApplyRowAvatar(item.get(), ann.m_groupid, groupName, facetype, customface, (int64_t)m_selfUserId);
        item->SetName(title.c_str());
        if (titleHit >= 0)
        {
            item->SetNameHighlight(titleHit, (int)keyword.size());
        }
        item->SetRightText(timeText.c_str());
        item->SetSubText(subText.c_str());
        item->SetHoverFill(kRowHoverFill);
        m_rowServerAnn[item.get()] = i;
        content.AddChild(std::move(item), balloonwjui::DuiLayout::Hint().Fixed(kResultRowHeight));
        ++annRows;
    }
    rows += annRows;
}

void CSearchDropdown::SetFileCallbacks(SubmitLocalSearchCallback submitCb, RequestGroupFilesCallback groupCb,
                                       PickFileMessageCallback pickFileCb, PickGroupFileCallback pickGroupFileCb)
{
    m_submitFileCb       = submitCb;
    m_requestGroupFileCb = groupCb;
    m_pickFileCb         = pickFileCb;
    m_pickGroupFileCb    = pickGroupFileCb;

    //查询来源变了，已有结果作废。
    ResetDelayedResults();
    UpdateTabAvailability();
}

void CSearchDropdown::UpdateTabAvailability()
{
    const bool messagesAvailable = (m_submitMsgCb != NULL);
    const bool filesAvailable    = (m_submitFileCb != NULL || m_requestGroupFileCb != NULL);
    if (m_tabMessages != NULL)
    {
        m_tabMessages->SetGrayed(!messagesAvailable);
    }
    if (m_tabFiles != NULL)
    {
        m_tabFiles->SetGrayed(!filesAvailable);
    }

    //当前选中的标签不可用了：退回「全部」，免得停在一个点不动的标签上。
    const bool lostCurrent = (m_filter == kFilterMessages && !messagesAvailable)
                          || (m_filter == kFilterFiles && !filesAvailable);
    if (lostCurrent)
    {
        m_filter = kFilterAll;
        if (m_tabAll != NULL)
        {
            m_tabAll->SetActive(true);
        }
        if (m_tabMessages != NULL)
        {
            m_tabMessages->SetActive(false);
        }
        if (m_tabFiles != NULL)
        {
            m_tabFiles->SetActive(false);
        }
    }
}

void CSearchDropdown::ResetDelayedResults()
{
    if (m_searchPending && IsWindow())
    {
        KillTimer(kDelayedSearchTimerId);
    }
    m_searchPending = false;
    m_msgHits.clear();
    m_msgHitsQuery.Empty();
    m_fileHits.clear();
    m_fileHitLocalPaths.clear();
    m_fileHitsQuery.Empty();
    //序号清零：在途查询的结果回来时对不上号，直接丢弃。
    m_msgSeq = 0;
    m_msgSeqQuery.Empty();
    m_fileSeq = 0;
    m_fileSeqQuery.Empty();
    m_lastSearchedQuery.Empty();
    m_groupFileHits.clear();
    m_groupFileState.Reset();
    //服务器搜索的结果同样作废：下次打开要用户再点一次按钮
    m_serverState.Reset();
    m_serverConvs.clear();
    m_serverAnns.clear();
}

void CSearchDropdown::OnGroupFileSearchResult(const net::SearchGroupFilesResult& result)
{
    const bool success = (result.m_code == 0);
    if (!m_groupFileState.OnResponse(result.m_keyword, success))
    {
        //用户已经接着输入、或下拉已收起：这是一次过期的应答。关键字只记字节数。
        LOG_INFO("search: discard stale group-file result (keyword %u bytes, code=%d).",
                 (unsigned)result.m_keyword.size(), result.m_code);
        return;
    }

    if (success)
    {
        m_groupFileHits = result.m_files;
    }
    else
    {
        m_groupFileHits.clear();
    }
    LOG_INFO("search: group-file result accepted, code=%d, count=%u, hasmore=%d.",
             result.m_code, (unsigned)result.m_files.size(), result.m_hasmore ? 1 : 0);

    //只在下拉仍然展示时刷新：RefreshResults 会把窗口显示出来。
    if (IsShown())
    {
        RefreshResults();
    }
}

void CSearchDropdown::WarmUpCandidates(const std::vector<net::ContactItem>& contacts,
                                       const std::vector<net::UserGroupItem>& groups)
{
    PerfStopWatch sw;
    const int rebuiltContacts = m_candidates.SyncContacts(contacts);
    const int rebuiltGroups   = m_candidates.SyncGroups(groups);
    LOG_INFO("search: candidate cache warm-up rebuilt %d contacts, %d groups in %.1f ms.",
             rebuiltContacts, rebuiltGroups, sw.ElapsedMs());
}

void CSearchDropdown::ClearCandidates()
{
    m_candidates.Clear();
}

// =======================================================================
// 创建 / 控件树构建
// =======================================================================

// 下拉浮窗的窗口类名（带 CS_DROPSHADOW 投影；DuiHost 的 __DuiHost__ 类无投影，
// 故另注册一个）。
static const TCHAR* kDropdownClassName = _T("__FlamingoSearchDropdown__");

// 注册下拉浮窗窗口类（幂等）。类风格带 CS_DROPSHADOW（系统投影，让浮层明显浮
// 起来）+ CS_SAVEBITS（弹出时保存底图，收起后免重绘底层）。窗口过程用
// DefWindowProc 占位 —— 真正的消息处理由随后的 SubclassWindow 接管。
static void EnsureDropdownClassRegistered()
{
    static bool s_registered = false;
    if (s_registered)
    {
        return;
    }
    WNDCLASSEX wc;
    ::ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DROPSHADOW | CS_SAVEBITS;
    wc.lpfnWndProc   = ::DefWindowProc;
    wc.hInstance     = ::GetModuleHandle(NULL);
    wc.hCursor       = ::LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kDropdownClassName;
    ::RegisterClassEx(&wc);
    s_registered = true;
}

bool CSearchDropdown::EnsureCreated(HWND owner)
{
    m_hOwner = owner;
    if (IsWindow())
    {
        return true;   // 已创建
    }

    EnsureDropdownClassRegistered();

    // 顶层无边框浮窗：WS_POPUP；WS_EX_TOOLWINDOW 不进任务栏；WS_EX_NOACTIVATE
    // 点击不抢激活（搜索框焦点不丢）。owner 传 NULL —— 让 DuiHost::SendNotify 的
    // GetParent 为空、通知自路由回本窗口。初始 0 尺寸、不可见，UpdateQuery 时定位。
    HWND hWnd = ::CreateWindowEx(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kDropdownClassName, NULL,
        WS_POPUP,
        0, 0, 0, 0,
        NULL, NULL, ::GetModuleHandle(NULL), NULL);
    if (hWnd == NULL)
    {
        LOG_WARNING("search dropdown: failed to create the popup window.");
        return false;
    }

    // 用 DuiHost 接管这个 HWND（SubclassWindow 模式）：之后所有消息走 DuiHost
    // 的 WndProc → 本类消息映射。随后 SetRoot 会 AttachToHost 并按客户区布局。
    if (!SubclassWindow(hWnd))
    {
        ::DestroyWindow(hWnd);
        LOG_WARNING("search dropdown: SubclassWindow failed.");
        return false;
    }

    BuildContentOnce();
    return true;
}

void CSearchDropdown::BuildContentOnce()
{
    if (m_built)
    {
        return;
    }

    // 根：白底卡片（1px 浅灰描边），自上而下 标签栏 / 结果滚动区。
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetPadding(0);
    root->SetGap(0);
    root->SetBgColor(kCardBg);
    root->SetBorderColor(kCardBorder);
    root->SetBorderWidth(1.0f);

    // ① 分类筛选标签栏：全部 / 联系人 / 群组 / 消息 / 文件（「消息」「文件」在宿主提供了
    //    相应回调时可用，否则置灰，见 UpdateTabAvailability）。
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kTabBarGap);
        row->SetPadding(kTabBarPadLeft, 0, 0, 0);   // 仅左内距（l,t,r,b）

        std::unique_ptr<balloonsearch::SearchFilterTab> tabAll(new balloonsearch::SearchFilterTab());
        tabAll->SetCtrlId(kIdTabAll);
        tabAll->SetLabel(TR(L"全部"));
        tabAll->SetActive(true);
        m_tabAll = tabAll.get();
        row->AddChild(std::move(tabAll), balloonwjui::DuiLayout::Hint().Auto());

        std::unique_ptr<balloonsearch::SearchFilterTab> tabContacts(new balloonsearch::SearchFilterTab());
        tabContacts->SetCtrlId(kIdTabContacts);
        tabContacts->SetLabel(TR(L"联系人"));
        tabContacts->SetCount(false, 0);
        m_tabContacts = tabContacts.get();
        row->AddChild(std::move(tabContacts), balloonwjui::DuiLayout::Hint().Auto());

        std::unique_ptr<balloonsearch::SearchFilterTab> tabGroups(new balloonsearch::SearchFilterTab());
        tabGroups->SetCtrlId(kIdTabGroups);
        tabGroups->SetLabel(TR(L"群组"));
        tabGroups->SetCount(false, 0);
        m_tabGroups = tabGroups.get();
        row->AddChild(std::move(tabGroups), balloonwjui::DuiLayout::Hint().Auto());

        std::unique_ptr<balloonsearch::SearchFilterTab> tabMessages(new balloonsearch::SearchFilterTab());
        tabMessages->SetCtrlId(kIdTabMessages);
        tabMessages->SetLabel(TR(L"消息"));
        tabMessages->SetCount(false, 0);
        m_tabMessages = tabMessages.get();
        row->AddChild(std::move(tabMessages), balloonwjui::DuiLayout::Hint().Auto());

        std::unique_ptr<balloonsearch::SearchFilterTab> tabFiles(new balloonsearch::SearchFilterTab());
        tabFiles->SetCtrlId(kIdTabFiles);
        tabFiles->SetLabel(TR(L"文件"));
        tabFiles->SetCount(false, 0);
        m_tabFiles = tabFiles.get();
        row->AddChild(std::move(tabFiles), balloonwjui::DuiLayout::Hint().Auto());

        // 尾部弹簧：把标签推到左侧。
        std::unique_ptr<balloonwjui::DuiLabel> spacer(new balloonwjui::DuiLabel());
        row->AddChild(std::move(spacer), balloonwjui::DuiLayout::Hint().Weight(1));

        m_tabRow = row.get();
        root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kTabRowHeight));
    }

    // ② 结果滚动区（内容自动高度，初始空）。滚动条与聊天窗消息区相同：超出时一直显示的细滑块，
    //    浮在内容上、不占宽度，可拖动（SearchResultScrollView）。
    {
        std::unique_ptr<balloonsearch::SearchResultScrollView> scroll(new balloonsearch::SearchResultScrollView());
        scroll->SetAutoContentHeight(true);
        std::unique_ptr<balloonwjui::DuiVBox> content(new balloonwjui::DuiVBox());
        content->SetGap(1);
        scroll->SetContent(std::move(content));
        m_scroll = scroll.get();
        root->AddChild(std::move(scroll), balloonwjui::DuiLayout::Hint().Weight(1));
    }

    SetRoot(std::move(root));
    m_built = true;

    //宿主可能在建窗之前就设好了查询回调，这里按当前回调补一次「消息」「文件」标签的可用状态。
    UpdateTabAvailability();
}

// =======================================================================
// 展示 / 刷新
// =======================================================================

void CSearchDropdown::UpdateQuery(const RECT& anchorScreenRect, int panelWidth, LPCTSTR query)
{
    m_query        = TrimCopy(CString(query != NULL ? query : _T("")));
    m_panelWidth   = panelWidth;
    m_anchorLeft   = anchorScreenRect.left;
    m_anchorBottom = anchorScreenRect.bottom;

    // 空查询：不展示结果（"空内容不显示"）。
    if (m_query.IsEmpty())
    {
        HideDropdown();
        return;
    }

    // 有了查询词就从「最近搜索」切回搜索结果：恢复分类筛选标签栏。
    SetRecentMode(false);

    // 联系人与群组立即刷新；聊天记录、文件等停止输入一小段时间后再查。
    ScheduleDelayedSearch();
    RefreshResults();
}

bool CSearchDropdown::HasDelayedSearch() const
{
    return m_submitMsgCb != NULL || m_submitFileCb != NULL || m_requestGroupFileCb != NULL;
}

void CSearchDropdown::ScheduleDelayedSearch()
{
    if (!HasDelayedSearch() || m_query.IsEmpty() || m_query == m_lastSearchedQuery)
    {
        //没有查询能力、查询词为空，或当前查询词已经发起过：不需要再查，停掉在途的计时。
        if (m_searchPending && IsWindow())
        {
            KillTimer(kDelayedSearchTimerId);
        }
        m_searchPending = false;
        return;
    }

    if (!IsWindow())
    {
        //窗口还没建好时既无法计时、也收不到后台查询的结果：本次不查（只会在宿主建窗失败的
        //异常路径上出现）。
        return;
    }

    //SetTimer 用同一个编号会重新开始计时，所以连续输入时只在最后一次按键之后查一次。
    SetTimer(kDelayedSearchTimerId, kDelayedSearchMs, NULL);
    m_searchPending = true;
}

void CSearchDropdown::RunDelayedSearch()
{
    m_lastSearchedQuery = m_query;
    if (m_query.IsEmpty() || !IsWindow())
    {
        return;
    }

    const std::string keywordUtf8 = EncodeUtil::UnicodeToUtf8(std::wstring((LPCWSTR)m_query));

    //本地聊天记录与本地文件类消息：提交给后台线程，结果之后经 kMsgLocalSearchDone 投回。
    //上一个关键字的结果先留着照常显示，新结果回来再整批替换。提交失败（本地库打不开）时
    //按"查完、没有命中"处理，免得一直显示"正在搜索"。
    if (m_submitMsgCb != NULL)
    {
        m_msgSeq = m_submitMsgCb(m_pickUd, balloonhistory::kV2SearchConversations, keywordUtf8,
                                 kMessageHitLimit, m_hWnd, kMsgLocalSearchDone);
        m_msgSeqQuery = m_query;
        if (m_msgSeq == 0)
        {
            m_msgHits.clear();
            m_msgHitsQuery = m_query;
            LOG_WARNING("search: failed to submit local message search.");
        }
    }
    if (m_submitFileCb != NULL)
    {
        m_fileSeq = m_submitFileCb(m_pickUd, balloonhistory::kV2SearchFileMessages, keywordUtf8,
                                   kFileHitLimit, m_hWnd, kMsgLocalSearchDone);
        m_fileSeqQuery = m_query;
        if (m_fileSeq == 0)
        {
            m_fileHits.clear();
            m_fileHitsQuery = m_query;
            LOG_WARNING("search: failed to submit local file search.");
        }
    }

    //群文件：请宿主向服务端发请求，应答之后经 OnGroupFileSearchResult 回来。没连上服务器时
    //请求发不出去，记下状态以便显示「群文件需联网搜索」，并清掉上一个关键字的结果。
    if (m_requestGroupFileCb != NULL)
    {
        if (m_requestGroupFileCb(m_pickUd, keywordUtf8, kGroupFileHitLimit))
        {
            m_groupFileState.OnRequestSent(keywordUtf8);
        }
        else
        {
            m_groupFileState.OnOffline(keywordUtf8);
            m_groupFileHits.clear();
            LOG_INFO("search: group-file search skipped, not connected.");
        }
    }
}

bool CSearchDropdown::MessageResultsReady() const
{
    return !m_query.IsEmpty() && m_query == m_msgHitsQuery;
}

bool CSearchDropdown::FileResultsReady() const
{
    return !m_query.IsEmpty() && m_query == m_fileHitsQuery;
}

LRESULT CSearchDropdown::OnLocalSearchDone(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
    balloonhistory::V2SearchResponse* resp = (balloonhistory::V2SearchResponse*)lParam;
    if (resp == NULL)
    {
        return 0;
    }
    const uint32_t seq = (uint32_t)wParam;

    //只采纳最近一次提交的那个序号：用户接着输入后，旧关键字的结果回来时序号已经对不上；
    //收起下拉时序号已清零，迟到的结果同样丢弃。
    bool accepted = false;
    if (resp->m_kind == balloonhistory::kV2SearchConversations && seq != 0 && seq == m_msgSeq)
    {
        m_msgHits.swap(resp->m_conversations);
        if (!resp->m_ok)
        {
            m_msgHits.clear();
        }
        m_msgHitsQuery = m_msgSeqQuery;
        m_msgSeq = 0;
        accepted = true;
    }
    else if (resp->m_kind == balloonhistory::kV2SearchFileMessages && seq != 0 && seq == m_fileSeq)
    {
        m_fileHits.swap(resp->m_records);
        if (!resp->m_ok)
        {
            m_fileHits.clear();
        }
        //每条结果在本机的路径：悬停按钮据它决定显示与否、点了打开哪里。
        m_fileHitLocalPaths.assign(m_fileHits.size(), std::wstring());
        for (size_t i = 0; i < m_fileHits.size(); ++i)
        {
            balloonhistory::ResolveRecordLocalPath(m_fileHits[i], m_fileHitLocalPaths[i]);
        }
        m_fileHitsQuery = m_fileSeqQuery;
        m_fileSeq = 0;
        accepted = true;
    }
    LOG_INFO("search: local result seq %u kind %d %s (%.1f ms in background).",
             seq, resp->m_kind, accepted ? "accepted" : "discarded as stale", resp->m_elapsedMs);
    delete resp;

    //只在下拉仍然展示时刷新：RefreshResults 会把窗口显示出来，收起之后不能因为一次迟到的
    //结果又弹出来。
    if (accepted && IsShown())
    {
        RefreshResults();
    }
    return 0;
}

LRESULT CSearchDropdown::OnDestroyWindow(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{
    //窗口销毁后，已投进消息队列的后台查询结果无人接收，在这里取出释放。
    const int drained = balloonhistory::CV2MsgSearchWorker::DrainPendingResponses(m_hWnd, kMsgLocalSearchDone);
    if (drained > 0)
    {
        LOG_INFO("search: %d pending local results released on destroy.", drained);
    }
    m_msgSeq  = 0;
    m_fileSeq = 0;
    bHandled = FALSE;   //交给 DuiHost 继续处理 WM_DESTROY
    return 0;
}

LRESULT CSearchDropdown::OnDeferredSwitchFilter(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/,
                                                BOOL& /*bHandled*/)
{
    //消息排队期间下拉可能已经收起（例如焦点离开），收起后不再切换，免得把窗口重新显示出来。
    if (IsShown())
    {
        SwitchFilter((int)wParam);
    }
    return 0;
}

LRESULT CSearchDropdown::OnDeferredRecentAction(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/,
                                                BOOL& /*bHandled*/)
{
    //消息排队期间下拉可能已经收起，或用户已经开始输入、换成了搜索结果，这时不再处理。
    if (!IsShown() || !m_recentMode)
    {
        return 0;
    }
    if (wParam == kRecentClearIndex)
    {
        LOG_INFO("search: clear %u recent keywords.", (unsigned)m_recentKeywords.size());
        HideDropdown();
        if (m_clearRecentCb != NULL)
        {
            m_clearRecentCb(m_pickUd);
        }
        return 0;
    }
    const size_t index = (size_t)wParam;
    if (index >= m_recentKeywords.size())
    {
        return 0;
    }
    //先复制出来：回调里宿主填入搜索框，内容变化会立即走 UpdateQuery，下拉不再处于最近搜索模式。
    const std::wstring keyword = m_recentKeywords[index];
    LOG_INFO("search: pick recent keyword %u of %u.", (unsigned)index, (unsigned)m_recentKeywords.size());
    if (m_pickRecentCb != NULL)
    {
        m_pickRecentCb(m_pickUd, keyword);
    }
    else
    {
        HideDropdown();
    }
    return 0;
}

LRESULT CSearchDropdown::OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    if (wParam != kDelayedSearchTimerId)
    {
        bHandled = FALSE;
        return 0;
    }

    KillTimer(kDelayedSearchTimerId);
    m_searchPending = false;
    RunDelayedSearch();

    //只在下拉仍然展示时刷新：RefreshResults 会把窗口显示出来，收起之后不能因为
    //一次迟到的查询又弹出来。
    if (IsShown())
    {
        RefreshResults();
    }
    return 0;
}

int CSearchDropdown::RefreshResults()
{
    if (m_scroll == NULL)
    {
        return 0;
    }

    m_rowContact.clear();
    m_rowGroup.clear();
    m_rowMessage.clear();
    m_rowFile.clear();
    m_rowGroupFile.clear();
    m_rowServerConv.clear();
    m_rowServerAnn.clear();
    m_serverSearchBtn = NULL;
    m_rowDept.clear();
    m_rowMemo.clear();

    const bool wantContacts = (m_filter == kFilterAll || m_filter == kFilterContacts);
    const bool wantGroups   = (m_filter == kFilterAll || m_filter == kFilterGroups);
    const bool wantMessages = (m_filter == kFilterMessages);
    const bool wantFiles    = (m_filter == kFilterFiles);
    const bool wantAll      = (m_filter == kFilterAll);

    // 先把两类命中各自收集出来（计数与渲染分离：标签计数总是反映各类真实命中数，
    // 渲染则按当前筛选只放该类）。
    std::vector<std::unique_ptr<balloonmain::MainPanelListItem> > contactRows;
    std::vector<UINT> contactIds;
    std::vector<std::unique_ptr<balloonmain::MainPanelListItem> > groupRows;
    std::vector<int64_t> groupIds;

    // 让候选缓存与数据源保持一致：只重算与数据源不一致的条目。打字过程中数据通常
    // 不变，这一步只做比对；通讯录、群列表刚加载完或有人改了名时才会真正重算。
    const int rebuiltContacts = (m_contacts != NULL) ? m_candidates.SyncContacts(*m_contacts) : 0;
    const int rebuiltGroups   = (m_groups != NULL) ? m_candidates.SyncGroups(*m_groups) : 0;
    if (rebuiltContacts > 0 || rebuiltGroups > 0)
    {
        LOG_INFO("search: candidate cache rebuilt %d contacts, %d groups.",
                 rebuiltContacts, rebuiltGroups);
    }

    // 查询词只预处理一次，在全部候选上复用。
    const contactsearch::PreparedQuery query = contactsearch::PrepareQuery((LPCWSTR)m_query);

    // —— 联系人匹配 ——
    if (m_contacts != NULL)
    {
        for (size_t i = 0; i < m_contacts->size(); ++i)
        {
            const net::ContactItem& c = (*m_contacts)[i];
            const bool isSelf = ((UINT)c.m_userid == m_selfUserId);
            if (isSelf && !m_includeSelf)
            {
                continue;   // 调用方要求不搜自己
            }

            // 第 i 条候选对应资料池的第 i 个联系人（刚 Sync 过）。
            const balloonsearch::ContactCandidate& cand = m_candidates.ContactAt(i);

            // 先按"看到的名字"匹配 —— 高亮区间是相对主行名给出的，用它才对得上。
            contactsearch::MatchResult mr = contactsearch::MatchPrepared(
                query, cand.m_shownName, cand.m_accountLower, cand.m_regionLower);
            if (!mr.m_matched && cand.m_nickDiffers)
            {
                // 设了备注的人，用原始昵称也该能搜到：用户记得的往往还是对方的真名。
                // 这一轮只用来决定"要不要显示这一行"，命中区间落在昵称上、与主行名
                // 对不上，故不取它的高亮，只把行放出来。
                contactsearch::MatchResult byNick = contactsearch::MatchPrepared(
                    query, cand.m_nickName, cand.m_accountLower, cand.m_regionLower);
                if (byNick.m_matched)
                {
                    mr.m_matched     = true;
                    mr.m_field       = byNick.m_field;
                    mr.m_nameHiStart = -1;
                    mr.m_nameHiLen   = 0;
                }
            }
            if (!mr.m_matched)
            {
                continue;
            }

            // 主行名：备注 → 昵称 → 账号 → 用户<id>，缓存里已按 ResolveContactDisplayName
            // 算好，与通讯录树 / 会话列表同一口径，同一个人不会在搜索结果里换个名字。
            CString display = cand.m_display;
            // 与通讯录树同一口径：自己那一行加"（我）"后缀，便于一眼区分。
            if (isSelf)
            {
                display += TR(L"（我）");
            }

            // 副行：优先显示这个人在组织架构里的位置（形如"技术中心 / 研发部 / 前端组"）——
            // 同名同姓的人靠部门一眼就能分开，比账号有辨识度得多。宿主没给部门表、
            // 表里查不到这个人、或路径为空串时，退回原来的"账号 xxx · 地区"
            //（地区为空则省略）。
            CString sub;
            bool haveDeptPath = false;
            if (m_deptPaths != NULL)
            {
                std::map<UINT, CString>::const_iterator itd =
                    m_deptPaths->find((UINT)c.m_userid);
                if (itd != m_deptPaths->end() && !itd->second.IsEmpty())
                {
                    sub          = itd->second;
                    haveDeptPath = true;
                }
            }
            if (!haveDeptPath)
            {
                sub.Format(TR(L"账号 %s"), (LPCTSTR)Utf8ToCStr(c.m_username));
                if (!c.m_address.empty())
                {
                    sub += _T(" · ");
                    sub += Utf8ToCStr(c.m_address);
                }
            }

            std::unique_ptr<balloonmain::MainPanelListItem> item(new balloonmain::MainPanelListItem());
            ApplyRowAvatar(item.get(), (int64_t)c.m_userid, display,
                           c.m_facetype, c.m_customface, (int64_t)m_selfUserId);
            item->SetName(display);
            item->SetSubText(sub);
            item->SetHoverFill(kRowHoverFill);
            // 宿主提供了定位能力时，联系人行才长出右侧那枚悬停态的定位按钮。
            // 群组行一律不调用本方法 —— 通讯录树里没有群，定位无从谈起。
            if (m_locateCb != NULL)
            {
                item->SetLocateButtonVisible(true);
            }
            if (mr.m_nameHiStart >= 0 && mr.m_nameHiLen > 0)
            {
                item->SetNameHighlight(mr.m_nameHiStart, mr.m_nameHiLen);
            }

            contactRows.push_back(std::move(item));
            contactIds.push_back((UINT)c.m_userid);
        }
    }

    // —— 部门匹配（部门名当昵称，规则同群组）——
    std::vector<std::unique_ptr<balloonmain::MainPanelListItem> > deptRows;
    std::vector<int64_t> deptIds;
    if (m_departments != NULL && m_locateDeptCb != NULL && wantContacts)
    {
        m_candidates.SyncDepartments(*m_departments);
        //上级路径要用的两张表：部门 id → 上级、部门 id → 名字
        std::map<int64_t, int64_t> parentOf;
        std::map<int64_t, std::wstring> nameOf;
        for (size_t i = 0; i < m_candidates.DepartmentCount(); ++i)
        {
            const balloonsearch::DepartmentCandidate& cand = m_candidates.DepartmentAt(i);
            parentOf[cand.m_deptid] = cand.m_parentid;
            nameOf[cand.m_deptid] = Utf8ToWStr(cand.m_name);
        }
        const std::wstring noField;   // 部门没有账号与地区，两项都按空串匹配
        for (size_t i = 0; i < m_candidates.DepartmentCount(); ++i)
        {
            const balloonsearch::DepartmentCandidate& cand = m_candidates.DepartmentAt(i);
            contactsearch::MatchResult mr = contactsearch::MatchPrepared(query, cand.m_preparedName, noField, noField);
            if (!mr.m_matched)
            {
                continue;
            }
            const std::wstring path = balloonsearch::BuildDepartmentParentPath(cand.m_deptid, parentOf, nameOf);
            std::unique_ptr<balloonmain::MainPanelListItem> item(new balloonmain::MainPanelListItem());
            item->SetAvatar(TR(kDeptAvatarGlyph), kDeptAvatarColor);
            item->SetName(nameOf[cand.m_deptid].c_str());
            item->SetSubText(path.empty() ? TR(kTopDepartmentText) : path.c_str());
            item->SetHoverFill(kRowHoverFill);
            if (mr.m_nameHiStart >= 0 && mr.m_nameHiLen > 0)
            {
                item->SetNameHighlight(mr.m_nameHiStart, mr.m_nameHiLen);
            }
            deptRows.push_back(std::move(item));
            deptIds.push_back(cand.m_deptid);
        }
    }

    // —— 群组匹配（群名当昵称，账号 / 地区留空，复用同一套匹配规则）——
    if (m_groups != NULL)
    {
        const std::wstring noField;   // 群没有账号与地区，两项都按空串匹配
        for (size_t i = 0; i < m_groups->size(); ++i)
        {
            const net::UserGroupItem& g = (*m_groups)[i];
            // 第 i 条群候选对应群列表的第 i 个群（刚 Sync 过）。
            contactsearch::MatchResult mr = contactsearch::MatchPrepared(
                query, m_candidates.GroupAt(i).m_preparedName, noField, noField);
            if (!mr.m_matched)
            {
                continue;
            }

            // 行版式与主面板"群"Tab 的列表保持一致：第一行群名 + 成员数，第二行群号。
            // 三段文案统一由 BuildGroupRowTexts 拼（含空群名的占位回落），免得两处
            // 各写一份格式串、口径慢慢漂移。副行不再写"群 ·" —— 这一段结果上方已经
            // 有一条"群组"分组标题，重复标注是多余的。
            balloonmain::GroupRowTexts texts = balloonmain::BuildGroupRowTexts(
                Utf8ToCStr(g.m_name), g.m_membercount, (__int64)g.m_groupid);

            std::unique_ptr<balloonmain::MainPanelListItem> item(new balloonmain::MainPanelListItem());
            ApplyRowAvatar(item.get(), g.m_groupid, texts.m_name,
                           g.m_facetype, g.m_customface, (int64_t)m_selfUserId);
            item->SetName(texts.m_name);
            item->SetRightText(texts.m_memberText);
            item->SetSubText(texts.m_groupNoText);
            item->SetHoverFill(kRowHoverFill);
            if (mr.m_nameHiStart >= 0 && mr.m_nameHiLen > 0)
            {
                item->SetNameHighlight(mr.m_nameHiStart, mr.m_nameHiLen);
            }

            groupRows.push_back(std::move(item));
            groupIds.push_back(g.m_groupid);
        }
    }

    // —— 延迟查询的结果：本地聊天记录、本地文件、群文件 ——
    // 新结果回来之前，先沿用上一个查询词的结果：用户在「消息」「文件」标签里接着打字时，
    // 列表保持原样、新结果回来后整批替换，而不是每按一个键就缩成一行"正在搜索"再撑开。
    // 新的查询词在旧结果里找不到时，那一行只是不标黄（见 MakeHitSnippet）。
    // 不在对应标签时也要生成，标签上的计数是去掉已退出的群之后的条数，只有把行做出来
    // 才知道。每类最多几十行，开销可以忽略。
    const bool msgReady  = MessageResultsReady();
    const bool fileReady = FileResultsReady();
    const ConversationIndex convIndex = BuildConversationIndex();

    std::vector<std::unique_ptr<balloonmain::MainPanelListItem> > messageRows;
    std::vector<size_t> messageHitIndexes;
    std::vector<std::unique_ptr<balloonmain::MainPanelListItem> > fileRows;
    std::vector<size_t> fileHitIndexes;
    if (!m_msgHitsQuery.IsEmpty())
    {
        BuildMessageRows(convIndex, messageRows, messageHitIndexes);
    }
    if (!m_fileHitsQuery.IsEmpty())
    {
        BuildFileRows(convIndex, fileRows, fileHitIndexes);
    }
    std::vector<std::unique_ptr<balloonmain::MainPanelListItem> > groupFileRows;
    std::vector<size_t> groupFileHitIndexes;
    BuildGroupFileRows(convIndex, groupFileRows, groupFileHitIndexes);

    // 群文件请求针对当前查询词的状态：等待应答、没连上服务器、失败时，「群文件」一段显示提示。
    const std::string queryUtf8 = EncodeUtil::UnicodeToUtf8(std::wstring((LPCWSTR)m_query));
    const balloonsearch::GroupFileSearchStatus groupFileStatus =
        (m_requestGroupFileCb != NULL) ? m_groupFileState.StatusFor(queryUtf8) : balloonsearch::kGroupFileIdle;

    const int contactMatch   = (int)contactRows.size();
    const int groupMatch     = (int)groupRows.size();
    const int messageMatch   = (int)messageRows.size();
    const int fileMatch      = (int)fileRows.size();
    const int groupFileMatch = (int)groupFileRows.size();

    // —— 按当前筛选渲染 ——
    std::unique_ptr<balloonwjui::DuiVBox> content(new balloonwjui::DuiVBox());
    content->SetGap(1);

    int renderedSections = 0;
    int renderedRows     = 0;
    int renderedSmallRows = 0;

    if (wantContacts && contactMatch > 0)
    {
        AppendSectionHeader(*content, TR(L"联系人"));
        for (size_t i = 0; i < contactRows.size(); ++i)
        {
            balloonmain::MainPanelListItem* raw = contactRows[i].get();
            m_rowContact[raw] = contactIds[i];
            content->AddChild(std::move(contactRows[i]),
                              balloonwjui::DuiLayout::Hint().Fixed(kResultRowHeight));
        }
        ++renderedSections;
        renderedRows += contactMatch;
    }

    if (wantContacts && !deptRows.empty())
    {
        AppendSectionHeader(*content, TR(kSectionDepartmentsText));
        for (size_t i = 0; i < deptRows.size(); ++i)
        {
            balloonmain::MainPanelListItem* raw = deptRows[i].get();
            m_rowDept[raw] = deptIds[i];
            content->AddChild(std::move(deptRows[i]),
                              balloonwjui::DuiLayout::Hint().Fixed(kResultRowHeight));
        }
        ++renderedSections;
        renderedRows += (int)deptIds.size();
    }

    if (wantGroups && groupMatch > 0)
    {
        AppendSectionHeader(*content, TR(L"群组"));
        for (size_t i = 0; i < groupRows.size(); ++i)
        {
            balloonmain::MainPanelListItem* raw = groupRows[i].get();
            m_rowGroup[raw] = groupIds[i];
            content->AddChild(std::move(groupRows[i]),
                              balloonwjui::DuiLayout::Hint().Fixed(kResultRowHeight));
        }
        ++renderedSections;
        renderedRows += groupMatch;
    }

    m_openMsgManagerBtn = NULL;
    if (wantMessages && messageMatch > 0)
    {
        AppendSectionHeader(*content, TR(kSectionMessagesText));
        for (size_t i = 0; i < messageRows.size(); ++i)
        {
            balloonmain::MainPanelListItem* raw = messageRows[i].get();
            m_rowMessage[raw] = messageHitIndexes[i];
            content->AddChild(std::move(messageRows[i]),
                              balloonwjui::DuiLayout::Hint().Fixed(kResultRowHeight));
        }
        ++renderedSections;
        renderedRows += messageMatch;
        //末尾：到聊天记录管理器里看全部会话的每一条命中。
        if (m_openMsgManagerCb != NULL)
        {
            m_openMsgManagerBtn = AppendMoreButton(*content, TR(kOpenMessageManagerText));
            ++renderedSmallRows;
        }
    }
    //本机结果之后：搜索服务器上的聊天记录。本机的结果回来之前不放（届时整页显示「正在搜索」）；
    //本机没有命中时先说一句，免得用户只看到一个按钮
    if (wantMessages && m_requestServerCb != NULL && (msgReady || messageMatch > 0))
    {
        if (messageMatch == 0)
        {
            AppendHintRow(*content, TR(kNoLocalMessagesText));
            ++renderedSmallRows;
        }
        AppendServerResults(*content, convIndex, queryUtf8, renderedSections, renderedRows, renderedSmallRows);
    }

    if (wantFiles && fileMatch > 0)
    {
        AppendSectionHeader(*content, TR(kSectionChatFilesText));
        for (size_t i = 0; i < fileRows.size(); ++i)
        {
            balloonmain::MainPanelListItem* raw = fileRows[i].get();
            m_rowFile[raw] = fileHitIndexes[i];
            content->AddChild(std::move(fileRows[i]),
                              balloonwjui::DuiLayout::Hint().Fixed(kResultRowHeight));
        }
        ++renderedSections;
        renderedRows += fileMatch;
    }

    if (wantFiles)
    {
        // 「群文件」一段：有结果就列结果；没有结果但有提示（等待应答、没连上服务器、失败）
        // 时列一行提示；查完确实没有时整段省略。本地这一段也还没到点时不单独提示，
        // 统一由下面的"正在搜索"兜底。
        const std::wstring hintText = balloonsearch::GroupFileStatusHint(groupFileStatus);
        if (groupFileMatch > 0)
        {
            AppendSectionHeader(*content, TR(kSectionGroupFilesText));
            for (size_t i = 0; i < groupFileRows.size(); ++i)
            {
                balloonmain::MainPanelListItem* raw = groupFileRows[i].get();
                m_rowGroupFile[raw] = groupFileHitIndexes[i];
                content->AddChild(std::move(groupFileRows[i]),
                                  balloonwjui::DuiLayout::Hint().Fixed(kResultRowHeight));
            }
            ++renderedSections;
            renderedRows += groupFileMatch;
        }
        else if (!hintText.empty() && (fileReady || renderedRows > 0))
        {
            AppendSectionHeader(*content, TR(kSectionGroupFilesText));
            std::unique_ptr<balloonwjui::DuiLabel> hint(new balloonwjui::DuiLabel());
            hint->SetText(hintText.c_str());
            hint->SetTextColor(kSectionColor);
            hint->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            content->AddChild(std::move(hint),
                              balloonwjui::DuiLayout::Hint().Fixed(kSectionHintHeight));
            ++renderedSections;
            ++renderedSmallRows;
        }
    }

    // 「全部」标签：群组之后列出命中的备忘（标题或备注含关键字），最多 kMemoHitLimit 条
    if (wantAll && m_memos != NULL && m_pickMemoCb != NULL)
    {
        const std::wstring keyword((LPCWSTR)m_query);
        SYSTEMTIME nowLocal;
        ::GetLocalTime(&nowLocal);
        int memoRows = 0;
        for (size_t i = 0; i < m_memos->size() && (size_t)memoRows < kMemoHitLimit; ++i)
        {
            const net::MemoItem& memo = (*m_memos)[i];
            //归属：群备忘取群名（群列表里查不到时写「群聊」），个人备忘写「个人备忘」
            std::wstring owner = TR(kPersonalMemoText);
            if (memo.m_ctype == net::kMemoCtypeGroup)
            {
                CString groupName;
                int facetype = 0;
                std::string customface;
                owner = ResolveConversation(convIndex, ::conversation_group, memo.m_groupid, groupName, facetype, customface)
                      ? std::wstring((LPCWSTR)groupName) : std::wstring(TR(kGroupMemoFallback));
            }
            const balloonsearch::MemoHitTexts texts = balloonsearch::BuildMemoHitTexts(
                Utf8ToWStr(memo.m_title), Utf8ToWStr(memo.m_note), owner, keyword);
            if (!texts.m_matched)
            {
                continue;
            }
            if (memoRows == 0)
            {
                AppendSectionHeader(*content, TR(kSectionMemosText));
                ++renderedSections;
            }
            std::unique_ptr<balloonmain::MainPanelListItem> item(new balloonmain::MainPanelListItem());
            item->SetAvatar(TR(kMemoAvatarGlyph), kMemoAvatarColor);
            item->SetName(texts.m_name.m_text.c_str());
            item->SetNameHighlight(texts.m_name.m_hiStart, texts.m_name.m_hiLen);
            item->SetSubText(texts.m_sub.m_text.c_str());
            item->SetSubHighlight(texts.m_sub.m_hiStart, texts.m_sub.m_hiLen);
            SYSTEMTIME startLocal;
            if (balloonsearch::MemoStartToSystemTime(memo.m_start, startLocal))
            {
                item->SetRightText(balloonsearch::FormatHitTime(startLocal, nowLocal).c_str());
            }
            item->SetHoverFill(kRowHoverFill);
            m_rowMemo[item.get()] = memo.m_memoid;
            content->AddChild(std::move(item), balloonwjui::DuiLayout::Hint().Fixed(kResultRowHeight));
            ++memoRows;
        }
        renderedRows += memoRows;
    }

    // 「全部」标签：联系人、群组之后，聊天记录与文件各列最多 kAllTabSectionMax 条，超出时
    // 段末给一个「查看更多」按钮，点它切到对应标签看完整结果。文件这一段先列本地的聊天文件、
    // 再列群文件，合起来取前几条；群文件的提示文字（如「群文件需联网搜索」）只在「文件」
    // 标签里显示，这里不重复。
    m_moreMessagesBtn = NULL;
    m_moreFilesBtn    = NULL;
    if (wantAll && messageMatch > 0)
    {
        const balloonsearch::AllTabSectionPick pick =
            balloonsearch::PickAllTabSection(messageRows.size(), 0, kAllTabSectionMax);
        AppendSectionHeader(*content, TR(kSectionMessagesText));
        for (size_t i = 0; i < pick.m_firstShown; ++i)
        {
            balloonmain::MainPanelListItem* raw = messageRows[i].get();
            m_rowMessage[raw] = messageHitIndexes[i];
            content->AddChild(std::move(messageRows[i]),
                              balloonwjui::DuiLayout::Hint().Fixed(kResultRowHeight));
        }
        ++renderedSections;
        renderedRows += (int)pick.m_firstShown;
        if (pick.m_showMore)
        {
            m_moreMessagesBtn = AppendMoreButton(*content, TR(kMoreMessagesText));
            ++renderedSmallRows;
        }
    }
    if (wantAll && (fileMatch > 0 || groupFileMatch > 0))
    {
        const balloonsearch::AllTabSectionPick pick =
            balloonsearch::PickAllTabSection(fileRows.size(), groupFileRows.size(), kAllTabSectionMax);
        AppendSectionHeader(*content, TR(kSectionFilesText));
        for (size_t i = 0; i < pick.m_firstShown; ++i)
        {
            balloonmain::MainPanelListItem* raw = fileRows[i].get();
            m_rowFile[raw] = fileHitIndexes[i];
            content->AddChild(std::move(fileRows[i]),
                              balloonwjui::DuiLayout::Hint().Fixed(kResultRowHeight));
        }
        for (size_t i = 0; i < pick.m_secondShown; ++i)
        {
            balloonmain::MainPanelListItem* raw = groupFileRows[i].get();
            m_rowGroupFile[raw] = groupFileHitIndexes[i];
            content->AddChild(std::move(groupFileRows[i]),
                              balloonwjui::DuiLayout::Hint().Fixed(kResultRowHeight));
        }
        ++renderedSections;
        renderedRows += (int)(pick.m_firstShown + pick.m_secondShown);
        if (pick.m_showMore)
        {
            m_moreFilesBtn = AppendMoreButton(*content, TR(kMoreFilesText));
            ++renderedSmallRows;
        }
    }

    if (renderedRows == 0 && renderedSmallRows == 0)
    {
        // 当前筛选下无命中：给一句居中灰色提示。「消息」「文件」「全部」标签的本地查询结果还没
        // 回来、又没有上一次的结果可沿用时，说"正在搜索"而不是"无匹配结果"，免得用户以为
        // 已经查过了（「全部」里的聊天记录与文件也要等结果回来才有）。
        bool searching = false;
        if (wantMessages)
        {
            searching = !msgReady;
        }
        else if (wantFiles)
        {
            searching = !fileReady;
        }
        else if (wantAll && HasDelayedSearch())
        {
            searching = !(msgReady && fileReady);
        }
        std::unique_ptr<balloonwjui::DuiLabel> hint(new balloonwjui::DuiLabel());
        hint->SetText(searching ? TR(kSearchingHintText) : TR(L"无匹配结果"));
        hint->SetTextColor(kSectionColor);
        hint->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        content->AddChild(std::move(hint),
                          balloonwjui::DuiLayout::Hint().Fixed(kNoResultHintHeight));
    }

    // 按显示顺序记下全部结果行，供上下方向键移动选中；行换了一批，选中状态清零。
    m_selectableRows.clear();
    for (size_t i = 0; i < content->Children().size(); ++i)
    {
        balloonmain::MainPanelListItem* row =
            dynamic_cast<balloonmain::MainPanelListItem*>(content->Children()[i].get());
        if (row != NULL)
        {
            m_selectableRows.push_back(row);
        }
    }
    m_selection.Reset((int)m_selectableRows.size());

    m_scroll->SetContent(std::move(content));

    // —— 更新标签计数（非空查询才显示数字）——
    // 聊天记录与本地文件各自在后台查询结果回来后计入，群文件在应答成功到达后计入；「全部」
    // 是各类之和（聊天记录按会话计数，其余按条计数，与各自标签上的数字一致）。规则见
    // ComputeSearchTabCounts。
    const bool showCount = !m_query.IsEmpty();
    const balloonsearch::SearchTabCounts counts = balloonsearch::ComputeSearchTabCounts(
        contactMatch, groupMatch, messageMatch, fileMatch, groupFileMatch, msgReady, fileReady,
        groupFileStatus == balloonsearch::kGroupFileDone);
    if (m_tabAll != NULL)
    {
        m_tabAll->SetCount(showCount, counts.m_all);
    }
    if (m_tabContacts != NULL)
    {
        m_tabContacts->SetCount(showCount, contactMatch);
    }
    if (m_tabGroups != NULL)
    {
        m_tabGroups->SetCount(showCount, groupMatch);
    }
    if (m_tabMessages != NULL)
    {
        m_tabMessages->SetCount(showCount && counts.m_showMessages, counts.m_messages);
    }
    if (m_tabFiles != NULL)
    {
        m_tabFiles->SetCount(showCount && counts.m_showFiles, counts.m_files);
    }
    // 标签按文字宽度排版，计数位数变了宽度也跟着变：对标签那一行重新排版才生效。
    // 必须用 ForceLayout —— 行本身的矩形没变，SetRect 会直接返回（开发约定文档 UI 约定第 5 条）。
    if (m_tabRow != NULL)
    {
        m_tabRow->ForceLayout(m_tabRow->GetRect());
    }

    // —— 按内容算高度并在搜索框下方（屏幕坐标）展示 ——
    int h = ComputeHeight(renderedSections, renderedRows, renderedSmallRows);
    if (IsWindow())
    {
        // HWND_TOPMOST：可靠地浮在仍处于前台的主面板之上；SWP_NOACTIVATE 不抢激活。
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, m_anchorLeft, m_anchorBottom, m_panelWidth, h,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }

    return renderedRows;
}

void CSearchDropdown::AppendSectionHeader(balloonwjui::DuiVBox& content, LPCTSTR text)
{
    std::unique_ptr<balloonwjui::DuiLabel> header(new balloonwjui::DuiLabel());
    header->SetText(text);
    header->SetTextColor(kSectionColor);
    // 带说明的长标题（如「聊天记录（仅搜索本机已保存的记录）」）在窄面板上放不下时以省略号结尾。
    header->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    content.AddChild(std::move(header),
                     balloonwjui::DuiLayout::Hint().Fixed(kSectionHeaderHeight).Margin(kSectionIndent, 0, 0, 0));
}

balloonwjui::DuiControl* CSearchDropdown::AppendMoreButton(balloonwjui::DuiVBox& content, LPCTSTR text)
{
    // 文字链样式（无底无边、悬停浅底），左对齐、与分组标题文字左边对齐。
    std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
    btn->SetVariant(balloonwjui::DuiButton::Variant::Text);
    btn->SetText(text);
    btn->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    balloonwjui::DuiControl* raw = btn.get();
    content.AddChild(std::move(btn),
                     balloonwjui::DuiLayout::Hint().Fixed(kSectionHintHeight).Margin(kSectionIndent, 0, 0, 0));
    return raw;
}

void CSearchDropdown::SwitchFilter(int filter)
{
    m_filter = filter;
    if (m_tabAll != NULL)
    {
        m_tabAll->SetActive(filter == kFilterAll);
    }
    if (m_tabContacts != NULL)
    {
        m_tabContacts->SetActive(filter == kFilterContacts);
    }
    if (m_tabGroups != NULL)
    {
        m_tabGroups->SetActive(filter == kFilterGroups);
    }
    if (m_tabMessages != NULL)
    {
        m_tabMessages->SetActive(filter == kFilterMessages);
    }
    if (m_tabFiles != NULL)
    {
        m_tabFiles->SetActive(filter == kFilterFiles);
    }
    RefreshResults();
}

CSearchDropdown::ConversationIndex CSearchDropdown::BuildConversationIndex() const
{
    // 会话 id → 资料池 / 群列表下标。每条结果都线性查找的话，通讯录大时要把资料池
    // 扫几十遍，先各建一次索引。
    ConversationIndex index;
    if (m_contacts != NULL)
    {
        for (size_t i = 0; i < m_contacts->size(); ++i)
        {
            index.m_contacts[(int64_t)(*m_contacts)[i].m_userid] = i;
        }
    }
    if (m_groups != NULL)
    {
        for (size_t i = 0; i < m_groups->size(); ++i)
        {
            index.m_groups[(*m_groups)[i].m_groupid] = i;
        }
    }
    return index;
}

bool CSearchDropdown::ResolveConversation(const ConversationIndex& index, int ctype, int64_t cid,
                                          CString& outName, int& outFacetype, std::string& outCustomface)
{
    outName.Empty();
    outFacetype = 0;
    outCustomface.clear();

    if (ctype == ::conversation_group)
    {
        std::map<int64_t, size_t>::const_iterator itg = index.m_groups.find(cid);
        if (itg == index.m_groups.end())
        {
            // 已退出或已解散的群：本地库里还留着它的记录，但群列表里已经没有它，
            // 点进去也无法正常打开，不展示。
            return false;
        }
        const net::UserGroupItem& g = (*m_groups)[itg->second];
        outName       = balloonmain::BuildGroupRowTexts(Utf8ToCStr(g.m_name), g.m_membercount,
                                                        (__int64)g.m_groupid).m_name;
        outFacetype   = g.m_facetype;
        outCustomface = g.m_customface;
        return true;
    }

    if (ctype == ::conversation_single)
    {
        std::map<int64_t, size_t>::const_iterator itc = index.m_contacts.find(cid);
        if (itc != index.m_contacts.end())
        {
            // 名字取搜索候选缓存里算好的显示名（备注 → 昵称 → 账号），与联系人结果行
            // 同一口径。缓存在 RefreshResults 开头刚 Sync 过，与资料池按下标对应。
            const net::ContactItem& c = (*m_contacts)[itc->second];
            outName       = m_candidates.ContactAt(itc->second).m_display;
            outFacetype   = c.m_facetype;
            outCustomface = c.m_customface;
        }
        else
        {
            // 通讯录里没有这个人（例如已离职）：沿用各处统一的占位名。
            outName.Format(TR(L"用户%lld"), (long long)cid);
        }
        return true;
    }

    // 服务号等其它会话类型没有对应的消息记录窗入口，不展示。
    return false;
}

void CSearchDropdown::BuildMessageRows(const ConversationIndex& index,
                                       std::vector<std::unique_ptr<balloonmain::MainPanelListItem> >& rows,
                                       std::vector<size_t>& hitIndexes)
{
    const std::wstring keyword((LPCWSTR)m_query);
    for (size_t h = 0; h < m_msgHits.size(); ++h)
    {
        const balloonhistory::ConversationHit& hit = m_msgHits[h];

        CString     name;
        int         facetype = 0;
        std::string customface;
        if (!ResolveConversation(index, hit.m_ctype, hit.m_cid, name, facetype, customface))
        {
            continue;
        }

        const balloonsearch::HighlightedText sub =
            balloonsearch::BuildMessageHitSubText(hit.m_hitCount, Utf8ToWStr(hit.m_latestText), keyword);
        const std::wstring timeText = balloonsearch::FormatHitTimeFromUnixMs(hit.m_latestTimestamp);

        std::unique_ptr<balloonmain::MainPanelListItem> item(new balloonmain::MainPanelListItem());
        ApplyRowAvatar(item.get(), hit.m_cid, name, facetype, customface, (int64_t)m_selfUserId);
        item->SetName(name);
        item->SetRightText(timeText.c_str());
        item->SetSubText(sub.m_text.c_str());
        item->SetSubHighlight(sub.m_hiStart, sub.m_hiLen);
        item->SetHoverFill(kRowHoverFill);

        rows.push_back(std::move(item));
        hitIndexes.push_back(h);
    }
}

// 给文件结果行装上行首图标：本机按类型取的系统图标（磁盘 / 文件夹 / 按扩展名的文件图标）。
// 取不到图标时退回一个灰底的「文件」字形占位，行的版式不变。
//   item：待装配的结果行（本函数不持有它）。
//   iconKind：shellicon::ItemKind 取值。name：文件名，iconKind 为文件时据其扩展名取图标。
static void ApplyFileIcon(balloonmain::MainPanelListItem* item, int iconKind, const std::wstring& name)
{
    // 取不到系统图标时字形占位的底色（中性灰，与未知类型的文件色块同一色系）。
    static const COLORREF kFileGlyphFallbackColor = RGB(0xB8, 0xB5, 0xB1);

    HBITMAP hbm = shellicon::CreateItemIconBitmap(iconKind, name, kFileIconPx);
    if (hbm != NULL)
    {
        //位图交给行控件持有，行销毁时一并释放。
        item->SetOwnedAvatarBitmap(hbm);
        return;
    }
    item->SetAvatar(TR(L"文"), kFileGlyphFallbackColor);
}

void CSearchDropdown::BuildFileRows(const ConversationIndex& index,
                                    std::vector<std::unique_ptr<balloonmain::MainPanelListItem> >& rows,
                                    std::vector<size_t>& hitIndexes)
{
    const std::wstring keyword((LPCWSTR)m_query);
    for (size_t h = 0; h < m_fileHits.size(); ++h)
    {
        const balloonhistory::HistoryRecord& rec = m_fileHits[h];

        CString     convName;
        int         facetype = 0;
        std::string customface;
        if (!ResolveConversation(index, rec.m_ctype, rec.m_cid, convName, facetype, customface))
        {
            continue;
        }

        const balloonsearch::LocalFileHitInfo info =
            balloonsearch::DescribeLocalFileRecord(rec.m_msgtype, rec.m_content, rec.m_searchText);
        const std::wstring name     = Utf8ToWStr(info.m_nameUtf8);
        const std::wstring sizeText = Utf8ToWStr(balloonhistory::FormatFileSize(info.m_sizeBytes));
        const balloonsearch::FileHitTexts texts = balloonsearch::BuildFileHitTexts(
            info.m_kind, name, std::wstring((LPCWSTR)convName), sizeText, keyword);
        const std::wstring timeText = balloonsearch::FormatHitTimeFromUnixMs(rec.m_timestamp);

        std::unique_ptr<balloonmain::MainPanelListItem> item(new balloonmain::MainPanelListItem());
        ApplyFileIcon(item.get(), info.m_iconKind, name);
        item->SetName(texts.m_name.m_text.c_str());
        item->SetNameHighlight(texts.m_name.m_hiStart, texts.m_name.m_hiLen);
        item->SetRightText(timeText.c_str());
        item->SetSubText(texts.m_sub.c_str());
        item->SetHoverFill(kRowHoverFill);

        //本机找得到文件或文件夹时，悬停显示「打开文件」「打开所在文件夹」按钮。
        const bool hasLocal = (h < m_fileHitLocalPaths.size() && !m_fileHitLocalPaths[h].empty());
        const balloonsearch::FileHitActions actions = balloonsearch::PickFileHitActions(info.m_kind, hasLocal);
        std::vector<int> buttons;
        if (actions.m_openFile)
        {
            buttons.push_back(balloonmain::MainPanelListItem::kHoverActionOpenFile);
        }
        if (actions.m_openFolder)
        {
            buttons.push_back(balloonmain::MainPanelListItem::kHoverActionOpenFolder);
        }
        item->SetHoverActions(buttons);

        rows.push_back(std::move(item));
        hitIndexes.push_back(h);
    }
}

void CSearchDropdown::OpenFileHitLocally(size_t hitIndex, bool folderButton)
{
    if (hitIndex >= m_fileHits.size() || hitIndex >= m_fileHitLocalPaths.size())
    {
        return;
    }
    //先取出再收起下拉：HideDropdown 会清空 m_fileHits 与 m_fileHitLocalPaths。
    const std::wstring path = m_fileHitLocalPaths[hitIndex];
    const balloonsearch::LocalFileHitInfo info = balloonsearch::DescribeLocalFileRecord(
        m_fileHits[hitIndex].m_msgtype, m_fileHits[hitIndex].m_content, m_fileHits[hitIndex].m_searchText);
    const balloonsearch::FileHitActions actions = balloonsearch::PickFileHitActions(info.m_kind, !path.empty());
    RecordCurrentQuery();
    HideDropdown();
    if (path.empty())
    {
        return;
    }
    LOG_INFO("search: open local %s for file hit %u (kind %d).",
             folderButton ? "folder" : "file", (unsigned)hitIndex, info.m_kind);

    //ShellExecute 返回值 <= 32 表示失败（没有关联程序、文件刚被删掉等）。
    const INT_PTR kShellExecOkThreshold = 32;
    if (folderButton && actions.m_folderIsTarget)
    {
        //文件夹传输记录：打开那个文件夹本身。路径带引号，含空格的路径才不会被拆成多个参数。
        CString param;
        param.Format(_T("\"%s\""), path.c_str());
        ::ShellExecute(m_hOwner, _T("open"), _T("explorer.exe"), param, NULL, SW_SHOWNORMAL);
    }
    else if (folderButton)
    {
        //文件：打开所在文件夹并选中它。路径同样必须带引号。
        CString param;
        param.Format(_T("/select,\"%s\""), path.c_str());
        ::ShellExecute(m_hOwner, _T("open"), _T("explorer.exe"), param, NULL, SW_SHOWNORMAL);
    }
    else
    {
        HINSTANCE hr = ::ShellExecute(m_hOwner, _T("open"), path.c_str(), NULL, NULL, SW_SHOWNORMAL);
        if ((INT_PTR)hr <= kShellExecOkThreshold)
        {
            LOG_WARNING("search: ShellExecute failed (%d) when opening a local file.", (int)(INT_PTR)hr);
            ::MessageBox(m_hOwner, TR(L"打开文件失败，可能没有关联的打开方式。"),
                         TR(L"搜索"), MB_OK | MB_ICONWARNING);
        }
    }
}

void CSearchDropdown::BuildGroupFileRows(const ConversationIndex& index,
                                         std::vector<std::unique_ptr<balloonmain::MainPanelListItem> >& rows,
                                         std::vector<size_t>& hitIndexes)
{
    const std::wstring keyword((LPCWSTR)m_query);
    for (size_t h = 0; h < m_groupFileHits.size(); ++h)
    {
        const net::SearchedGroupFileItem& hit = m_groupFileHits[h];
        const int64_t groupId = ::_strtoi64(hit.m_groupid.c_str(), NULL, 10);

        // 群名与聊天文件同一口径；服务端只搜本人所在的群，群列表里却查不到时（群列表还没
        // 刷新）同样不展示，点进去也打不开那个群。
        CString     groupName;
        int         facetype = 0;
        std::string customface;
        if (groupId <= 0
            || !ResolveConversation(index, ::conversation_group, groupId, groupName, facetype, customface))
        {
            continue;
        }

        const std::wstring name     = Utf8ToWStr(hit.m_entry.m_name);
        const std::wstring sizeText = Utf8ToWStr(balloonhistory::FormatFileSize(hit.m_entry.m_size));
        const balloonsearch::FileHitTexts texts = balloonsearch::BuildFileHitTexts(
            balloonsearch::kFileHitGroupFile, name, std::wstring((LPCWSTR)groupName), sizeText, keyword);
        const std::wstring timeText = balloonsearch::FormatHitTimeFromUnixMs(hit.m_entry.m_uploadtime);

        std::unique_ptr<balloonmain::MainPanelListItem> item(new balloonmain::MainPanelListItem());
        ApplyFileIcon(item.get(), shellicon::kItemFile, name);
        item->SetName(texts.m_name.m_text.c_str());
        item->SetNameHighlight(texts.m_name.m_hiStart, texts.m_name.m_hiLen);
        item->SetRightText(timeText.c_str());
        item->SetSubText(texts.m_sub.c_str());
        item->SetHoverFill(kRowHoverFill);

        rows.push_back(std::move(item));
        hitIndexes.push_back(h);
    }
}

int CSearchDropdown::ComputeHeight(int sections, int rows, int smallRows) const
{
    int content = sections * kSectionHeaderHeight + rows * kResultRowStride
                + smallRows * kSectionHintStride;
    if (content == 0)
    {
        content = kNoResultHintHeight;   // 无匹配提示
    }
    if (content > kMaxContentHeight)
    {
        content = kMaxContentHeight;
    }
    return kTabRowHeight + content + kBottomPad;
}

void CSearchDropdown::FollowAnchor(const RECT& anchorScreenRect, int panelWidth)
{
    // 先把新锚点记下来：即便此刻不可见（用户还没输入），下次 RefreshResults 展示时
    // 也会用这份最新坐标，不会闪一下旧位置。
    m_anchorLeft   = anchorScreenRect.left;
    m_anchorBottom = anchorScreenRect.bottom;
    m_panelWidth   = panelWidth;

    if (m_hWnd == NULL || !::IsWindowVisible(m_hWnd))
    {
        return;
    }

    // 结果内容没动，高度沿用当前窗口高度，只改左上角与宽度。仍指定 HWND_TOPMOST +
    // SWP_NOACTIVATE，口径与 RefreshResults 里的展示一致（浮在主面板之上、不抢激活）。
    RECT rcWnd;
    ::GetWindowRect(m_hWnd, &rcWnd);
    const int height = rcWnd.bottom - rcWnd.top;

    ::SetWindowPos(m_hWnd, HWND_TOPMOST, m_anchorLeft, m_anchorBottom, m_panelWidth, height,
                   SWP_NOACTIVATE);
}

void CSearchDropdown::HideDropdown()
{
    //停掉在途的延迟查询，并让已有的查询结果作废：下拉收起期间可能又收到了新消息、群里
    //又传了新文件，下次打开时一律重新查询；之后迟到的群文件应答也一并丢弃。
    ResetDelayedResults();

    if (m_hWnd != NULL && ::IsWindowVisible(m_hWnd))
    {
        ::ShowWindow(m_hWnd, SW_HIDE);
    }
    //下次显示时默认是搜索结果，标签栏要先恢复。
    SetRecentMode(false);
}

bool CSearchDropdown::IsShown() const
{
    return m_hWnd != NULL && ::IsWindowVisible(m_hWnd) != FALSE;
}

// =======================================================================
// 最近搜索
// =======================================================================

void CSearchDropdown::SetRecentCallbacks(RecordRecentCallback recordCb, PickRecentCallback pickCb,
                                         ClearRecentCallback clearCb)
{
    m_recordRecentCb = recordCb;
    m_pickRecentCb   = pickCb;
    m_clearRecentCb  = clearCb;
}

bool CSearchDropdown::IsShowingRecent() const
{
    return m_recentMode && IsShown();
}

void CSearchDropdown::RecordCurrentQuery()
{
    if (m_recordRecentCb != NULL && !m_query.IsEmpty())
    {
        m_recordRecentCb(m_pickUd, std::wstring((LPCWSTR)m_query));
    }
}

void CSearchDropdown::SetRecentMode(bool recent)
{
    if (!recent)
    {
        //离开最近搜索：关键字行与「清空」按钮随下一次刷新被替换，先把指向它们的记录清掉。
        m_rowRecent.clear();
        m_clearRecentBtn = NULL;
    }
    if (m_recentMode == recent)
    {
        return;
    }
    m_recentMode = recent;
    if (m_tabRow == NULL)
    {
        return;
    }
    m_tabRow->SetVisible(!recent);
    //SetVisible 只置标志位、不重新排版；标签栏是根容器的直接子控件，对根容器 ForceLayout
    //才能让结果滚动区占用或让出标签栏的位置（开发约定文档「UI 约定」第 5 条）。
    balloonwjui::DuiControl* root = GetRoot();
    if (root != NULL)
    {
        root->ForceLayout(root->GetRect());
    }
}

void CSearchDropdown::ShowRecent(const RECT& anchorScreenRect, int panelWidth,
                                 const std::vector<std::wstring>& keywords)
{
    m_panelWidth   = panelWidth;
    m_anchorLeft   = anchorScreenRect.left;
    m_anchorBottom = anchorScreenRect.bottom;
    if (keywords.empty() || m_scroll == NULL)
    {
        HideDropdown();
        return;
    }

    //从搜索结果切过来时：停掉在途的延迟查询、作废旧结果，免得迟到的结果把列表换回搜索结果。
    ResetDelayedResults();
    m_query.Empty();
    m_rowContact.clear();
    m_rowGroup.clear();
    m_rowMessage.clear();
    m_rowFile.clear();
    m_rowGroupFile.clear();
    m_moreMessagesBtn = NULL;
    m_moreFilesBtn    = NULL;
    m_openMsgManagerBtn = NULL;
    m_serverSearchBtn = NULL;
    m_rowServerConv.clear();
    m_rowServerAnn.clear();
    m_rowDept.clear();
    m_rowMemo.clear();
    m_selectableRows.clear();
    m_selection.Reset(0);
    SetRecentMode(true);
    m_rowRecent.clear();
    m_recentKeywords = keywords;

    std::unique_ptr<balloonwjui::DuiVBox> content(new balloonwjui::DuiVBox());
    content->SetGap(1);

    //标题行：左边「最近搜索」，右边「清空」。
    {
        std::unique_ptr<balloonwjui::DuiHBox> header(new balloonwjui::DuiHBox());
        header->SetPadding(kSectionIndent, 0, kSectionIndent, 0);
        std::unique_ptr<balloonwjui::DuiLabel> title(new balloonwjui::DuiLabel());
        title->SetText(TR(kSectionRecentText));
        title->SetTextColor(kSectionColor);
        title->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        header->AddChild(std::move(title), balloonwjui::DuiLayout::Hint().Weight(1));
        std::unique_ptr<balloonwjui::DuiButton> clearBtn(new balloonwjui::DuiButton());
        clearBtn->SetVariant(balloonwjui::DuiButton::Variant::Text);
        clearBtn->SetText(TR(kClearRecentText));
        m_clearRecentBtn = clearBtn.get();
        header->AddChild(std::move(clearBtn), balloonwjui::DuiLayout::Hint().Fixed(kClearRecentWidth));
        content->AddChild(std::move(header), balloonwjui::DuiLayout::Hint().Fixed(kSectionHeaderHeight));
    }

    //关键字行：文字链样式，左边与标题文字对齐，过长时以省略号结尾。
    for (size_t i = 0; i < m_recentKeywords.size(); ++i)
    {
        std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
        btn->SetVariant(balloonwjui::DuiButton::Variant::Text);
        btn->SetText(m_recentKeywords[i].c_str());
        btn->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        m_rowRecent[btn.get()] = i;
        content->AddChild(std::move(btn),
                          balloonwjui::DuiLayout::Hint().Fixed(kSectionHintHeight).Margin(kSectionIndent, 0, 0, 0));
    }
    m_scroll->SetContent(std::move(content));

    //没有标签栏，高度 = 标题行 + 各关键字行 + 底部留白。
    int contentHeight = kSectionHeaderHeight + (int)m_recentKeywords.size() * kSectionHintStride;
    if (contentHeight > kMaxContentHeight)
    {
        contentHeight = kMaxContentHeight;
    }
    const int h = contentHeight + kBottomPad;
    if (IsWindow())
    {
        //口径同 RefreshResults：浮在主面板之上、不抢激活。
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, m_anchorLeft, m_anchorBottom, m_panelWidth, h,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

bool CSearchDropdown::HandleNavigationKey(UINT vk, bool ctrl, bool shift)
{
    //最近搜索列表只支持鼠标点击；这时搜索框为空，方向键与回车交给搜索框也没有效果。
    if (!IsShown() || m_recentMode)
    {
        return false;
    }

    switch (vk)
    {
    //下方向键：选中下一行（没有选中时选中第一行）
    case VK_DOWN:
        if (m_selection.MoveDown())
        {
            ApplyKeySelection();
        }
        //即使已在末行也吞掉：单行搜索框里上下键没有别的用处，交给它只会挪动光标
        return true;
    //上方向键：选中上一行，第一行再往上取消选中
    case VK_UP:
        if (m_selection.MoveUp())
        {
            ApplyKeySelection();
        }
        return true;
    //回车：打开选中行（没有选中时打开第一行），与鼠标点击该行相同
    case VK_RETURN:
    {
        const int idx = m_selection.ActivationIndex();
        if (idx < 0 || idx >= (int)m_selectableRows.size())
        {
            return false;
        }
        LOG_INFO("search: activate row %d of %u by keyboard.", idx, (unsigned)m_selectableRows.size());
        return ActivateRow(m_selectableRows[idx]);
    }
    //Ctrl+Tab / Ctrl+Shift+Tab：循环切换筛选标签，跳过置灰的；不带 Ctrl 的 Tab 不管，交还宿主
    case VK_TAB:
    {
        if (!ctrl)
        {
            return false;
        }
        //标签下标与 FilterKind 取值一一对应：全部、联系人、群组、消息、文件
        const int kTabCount = 5;
        const bool available[kTabCount] =
        {
            true,
            true,
            true,
            m_tabMessages != NULL && !m_tabMessages->IsGrayed(),
            m_tabFiles != NULL && !m_tabFiles->IsGrayed(),
        };
        const int next = balloonsearch::NextAvailableTab(m_filter, shift ? -1 : 1, available, kTabCount);
        if (next != m_filter)
        {
            SwitchFilter(next);
        }
        return true;
    }
    //其它按键不处理，交还搜索框
    default:
        return false;
    }
}

void CSearchDropdown::ApplyKeySelection()
{
    const int current = m_selection.Current();
    for (size_t i = 0; i < m_selectableRows.size(); ++i)
    {
        m_selectableRows[i]->SetKeySelected((int)i == current);
    }
    if (current >= 0 && current < (int)m_selectableRows.size())
    {
        ScrollRowIntoView(m_selectableRows[current]);
    }
}

void CSearchDropdown::ScrollRowIntoView(balloonwjui::DuiControl* row)
{
    if (row == NULL || m_scroll == NULL)
    {
        return;
    }
    //结果列表按滚动位置平移摆放，行的矩形与视口在同一坐标系：比较上下边界即可。
    const RECT& rcRow  = row->GetRect();
    const RECT& rcView = m_scroll->GetRect();
    const int pos = m_scroll->GetScrollPos();
    if (rcRow.bottom > rcView.bottom)
    {
        m_scroll->SetScrollPos(pos + (rcRow.bottom - rcView.bottom));
    }
    else if (rcRow.top < rcView.top)
    {
        m_scroll->SetScrollPos(pos - (rcView.top - rcRow.top));
    }
    if (IsWindow())
    {
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }
}

bool CSearchDropdown::ActivateRow(balloonwjui::DuiControl* pCtrl)
{
    if (pCtrl == NULL)
    {
        return false;
    }
    std::map<balloonwjui::DuiControl*, UINT>::iterator itc = m_rowContact.find(pCtrl);
    if (itc != m_rowContact.end())
    {
        UINT peerUserId = itc->second;
        RecordCurrentQuery();
        HideDropdown();
        if (m_contactCb != NULL)
        {
            m_contactCb(m_pickUd, peerUserId);
        }
        return true;
    }
    std::map<balloonwjui::DuiControl*, int64_t>::iterator itg = m_rowGroup.find(pCtrl);
    if (itg != m_rowGroup.end())
    {
        int64_t groupId = itg->second;
        RecordCurrentQuery();
        HideDropdown();
        if (m_groupCb != NULL)
        {
            m_groupCb(m_pickUd, groupId);
        }
        return true;
    }
    std::map<balloonwjui::DuiControl*, size_t>::iterator itm = m_rowMessage.find(pCtrl);
    if (itm != m_rowMessage.end() && itm->second < m_msgHits.size())
    {
        // 先把会话与查询词取出来再收起下拉：HideDropdown 会清空 m_msgHits。
        const int         ctype       = m_msgHits[itm->second].m_ctype;
        const int64_t     cid         = m_msgHits[itm->second].m_cid;
        const std::string keywordUtf8 = EncodeUtil::UnicodeToUtf8(std::wstring((LPCWSTR)m_query));
        RecordCurrentQuery();
        HideDropdown();
        if (m_pickMsgCb != NULL)
        {
            m_pickMsgCb(m_pickUd, ctype, cid, keywordUtf8);
        }
        return true;
    }
    std::map<balloonwjui::DuiControl*, size_t>::iterator itf = m_rowFile.find(pCtrl);
    if (itf != m_rowFile.end() && itf->second < m_fileHits.size())
    {
        // 同上，先取出再收起下拉（HideDropdown 会清空 m_fileHits）。
        const balloonhistory::HistoryRecord& rec = m_fileHits[itf->second];
        const int         ctype       = rec.m_ctype;
        const int64_t     cid         = rec.m_cid;
        const int64_t     msgid       = rec.m_msgid;
        const int         msgtype     = rec.m_msgtype;
        const std::string keywordUtf8 = EncodeUtil::UnicodeToUtf8(std::wstring((LPCWSTR)m_query));
        RecordCurrentQuery();
        HideDropdown();
        if (m_pickFileCb != NULL)
        {
            m_pickFileCb(m_pickUd, ctype, cid, msgid, msgtype, keywordUtf8);
        }
        return true;
    }
    std::map<balloonwjui::DuiControl*, int64_t>::iterator itmm = m_rowMemo.find(pCtrl);
    if (itmm != m_rowMemo.end())
    {
        const int64_t memoid = itmm->second;
        RecordCurrentQuery();
        HideDropdown();
        if (m_pickMemoCb != NULL)
        {
            m_pickMemoCb(m_pickUd, memoid);
        }
        return true;
    }
    std::map<balloonwjui::DuiControl*, int64_t>::iterator itd = m_rowDept.find(pCtrl);
    if (itd != m_rowDept.end())
    {
        const int64_t deptid = itd->second;
        RecordCurrentQuery();
        HideDropdown();
        if (m_locateDeptCb != NULL)
        {
            m_locateDeptCb(m_pickUd, deptid);
        }
        return true;
    }
    std::map<balloonwjui::DuiControl*, size_t>::iterator itsc = m_rowServerConv.find(pCtrl);
    if (itsc != m_rowServerConv.end() && itsc->second < m_serverConvs.size())
    {
        // 先取出再收起下拉（HideDropdown 会清空 m_serverConvs）。
        const net::GlobalSearchConversation hit = m_serverConvs[itsc->second];
        const std::string keywordUtf8 = EncodeUtil::UnicodeToUtf8(std::wstring((LPCWSTR)m_query));
        RecordCurrentQuery();
        HideDropdown();
        if (m_pickServerMsgCb != NULL)
        {
            m_pickServerMsgCb(m_pickUd, hit.m_ctype, hit.m_cid, hit.m_latestTime, keywordUtf8);
        }
        return true;
    }
    std::map<balloonwjui::DuiControl*, size_t>::iterator itsa = m_rowServerAnn.find(pCtrl);
    if (itsa != m_rowServerAnn.end() && itsa->second < m_serverAnns.size())
    {
        const int64_t groupId = m_serverAnns[itsa->second].m_groupid;
        const int64_t annId   = m_serverAnns[itsa->second].m_id;
        RecordCurrentQuery();
        HideDropdown();
        if (m_pickAnnCb != NULL)
        {
            m_pickAnnCb(m_pickUd, groupId, annId);
        }
        return true;
    }
    std::map<balloonwjui::DuiControl*, size_t>::iterator itgf = m_rowGroupFile.find(pCtrl);
    if (itgf != m_rowGroupFile.end() && itgf->second < m_groupFileHits.size())
    {
        // 同上，先取出再收起下拉（HideDropdown 会清空 m_groupFileHits）。
        const net::SearchedGroupFileItem& hit = m_groupFileHits[itgf->second];
        const int64_t     groupId = ::_strtoi64(hit.m_groupid.c_str(), NULL, 10);
        const std::string entryId = hit.m_entry.m_entryid;
        RecordCurrentQuery();
        HideDropdown();
        if (m_pickGroupFileCb != NULL)
        {
            m_pickGroupFileCb(m_pickUd, groupId, entryId);
        }
        return true;
    }
    return false;
}

// =======================================================================
// 通知处理（自路由：owner 为空，DuiHost::SendNotify 回到本窗口）
// =======================================================================

LRESULT CSearchDropdown::OnChildNotify(UINT /*uMsg*/, WPARAM /*wParam*/,
                                       LPARAM lParam, BOOL& bHandled)
{
    const balloonwjui::DuiNotify* n =
        reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    if (n == NULL || n->pCtrl == NULL)
    {
        bHandled = FALSE;
        return 0;
    }
    balloonwjui::DuiControl* pCtrl = static_cast<balloonwjui::DuiControl*>(n->pCtrl);

    // —— 联系人结果行右侧的「定位」按钮 → 回调宿主去通讯录树里定位，并隐藏下拉 ——
    //
    // 仓库约定（开发约定文档「UI 约定」第 4 条）要求：判定自定义通知码时必须连触发控件
    // 一起判，且分支内没匹配上时不能无条件 return —— 否则排在后面的控件发来的同值
    // 通知会被本分支一并截住而得不到处理，症状是"点了没反应"且无任何日志。本下拉的
    // 结果行没有设 ctrlId（恒为 0），无法按编号区分，等价的区分手段是"pCtrl 能否在
    // m_rowContact 里查到"：查得到才是本下拉的联系人结果行；查不到就让流程继续往下
    // 走，不 return。
    if (n->code == (UINT)balloonmain::MainPanelListItem::DUIN_LISTITEM_LOCATE)
    {
        std::map<balloonwjui::DuiControl*, UINT>::iterator itl = m_rowContact.find(pCtrl);
        if (itl != m_rowContact.end())
        {
            UINT peerUserId = itl->second;
            RecordCurrentQuery();
            HideDropdown();
            if (m_locateCb != NULL)
            {
                m_locateCb(m_pickUd, peerUserId);
            }
            return 0;
        }
    }

    // —— 聊天文件结果行右侧的「打开文件」「打开所在文件夹」按钮 → 在本机打开 ——
    // 与上面的定位按钮同理，只认本下拉「聊天文件」一段的行，查不到就继续往下走。
    if (n->code == (UINT)balloonmain::MainPanelListItem::DUIN_LISTITEM_OPEN_FILE
        || n->code == (UINT)balloonmain::MainPanelListItem::DUIN_LISTITEM_OPEN_FOLDER)
    {
        std::map<balloonwjui::DuiControl*, size_t>::iterator itf = m_rowFile.find(pCtrl);
        if (itf != m_rowFile.end())
        {
            OpenFileHitLocally(itf->second,
                               n->code == (UINT)balloonmain::MainPanelListItem::DUIN_LISTITEM_OPEN_FOLDER);
            return 0;
        }
    }

    // 只处理单击；其余通知放行。
    if (n->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }

    // —— 结果行单击 → 回调并隐藏（与键盘回车共用 ActivateRow）——
    if (ActivateRow(pCtrl))
    {
        return 0;
    }

    // —— 「搜索服务器上的聊天记录」→ 延后发请求（发出后会重建列表、销毁这个按钮）——
    if (pCtrl != NULL && pCtrl == m_serverSearchBtn)
    {
        PostMessage(kMsgDeferredServerSearch, 0, 0);
        return 0;
    }

    // —— 「消息」标签底部「在聊天记录管理器中查看全部」→ 回调宿主打开管理器 ——
    // 收起下拉只是隐藏窗口、不销毁控件，可以在按钮的通知里直接处理。
    if (pCtrl != NULL && pCtrl == m_openMsgManagerBtn)
    {
        const std::wstring keyword((LPCWSTR)m_query);
        RecordCurrentQuery();
        HideDropdown();
        if (m_openMsgManagerCb != NULL && !keyword.empty())
        {
            m_openMsgManagerCb(m_pickUd, keyword);
        }
        return 0;
    }

    // —— 「最近搜索」列表里的关键字与「清空」→ 延后回调宿主 ——
    // 填入关键字会立即重建列表、销毁被点的按钮，理由与下面的「查看更多」相同，也要延后处理。
    if (m_recentMode)
    {
        std::map<balloonwjui::DuiControl*, size_t>::iterator itr = m_rowRecent.find(pCtrl);
        if (itr != m_rowRecent.end())
        {
            PostMessage(kMsgDeferredRecentAction, (WPARAM)itr->second, 0);
            return 0;
        }
        if (pCtrl == m_clearRecentBtn)
        {
            PostMessage(kMsgDeferredRecentAction, kRecentClearIndex, 0);
            return 0;
        }
    }

    // —— 「全部」标签段末的「查看更多」→ 切到对应标签看完整结果 ——
    // 不能在这里直接切换：本通知是按钮在自己的鼠标处理函数里同步发出的，切换会重建结果
    // 列表、销毁这个按钮，而按钮的处理函数返回后还要访问自身成员。投递一条消息，等这次
    // 鼠标事件处理完再切换。
    if (pCtrl != NULL && pCtrl == m_moreMessagesBtn)
    {
        PostMessage(kMsgDeferredSwitchFilter, (WPARAM)kFilterMessages, 0);
        return 0;
    }
    if (pCtrl != NULL && pCtrl == m_moreFilesBtn)
    {
        PostMessage(kMsgDeferredSwitchFilter, (WPARAM)kFilterFiles, 0);
        return 0;
    }

    // —— 筛选标签单击 → 切换筛选并刷新（全部 / 联系人 / 群组，以及可用时的「消息」「文件」）——
    int newFilter = -1;
    if (pCtrl == m_tabAll)
    {
        newFilter = kFilterAll;
    }
    else if (pCtrl == m_tabContacts)
    {
        newFilter = kFilterContacts;
    }
    else if (pCtrl == m_tabGroups)
    {
        newFilter = kFilterGroups;
    }
    else if (pCtrl == m_tabMessages && m_tabMessages != NULL && !m_tabMessages->IsGrayed())
    {
        newFilter = kFilterMessages;
    }
    else if (pCtrl == m_tabFiles && m_tabFiles != NULL && !m_tabFiles->IsGrayed())
    {
        newFilter = kFilterFiles;
    }
    if (newFilter >= 0)
    {
        SwitchFilter(newFilter);
        return 0;
    }

    bHandled = FALSE;
    return 0;
}
