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
#include "ContactMatch.h"
#include "../IULog.h"
#include "../EncodeUtil.h"
#include "../net/ContactsProtocolClient.h"     // net::ContactItem
#include "../net/GroupProtocolClient.h"        // net::UserGroupItem
#include "../BalloonMain/MainPanelListItem.h"  // 结果行（含命中高亮）
#include "../BalloonMain/GroupRowText.h"       // BuildGroupRowTexts（群结果行的"群名+人数 / 群号"文案）
#include "../BalloonMain/AvatarSpec.h"         // DecodeAvatar：按 (facetype,customface) 定该画什么
#include "../BalloonMain/AvatarImageUtil.h"    // MakeRoundedAvatarFromFile：自定义图头像位图
#include "../BalloonMain/ContactRemarkModel.h" // ResolveContactDisplayName：备注优先的统一取名口径
#include "../net/ImageTransfer.h"
#include "../net/CurrentUserAccount.h"   // net::CurrentUserAccount（本地缓存目录按登录账号分）              // 头像真图的本地缓存路径与下载
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"      // DuiVBox / DuiHBox / Hint
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiScrollBar.h"   // DuiScrollView

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

// 标签栏内边距与标签间距。
static const int kTabBarPadLeft = 10;
static const int kTabBarGap     = 6;

// 各标签固定宽（逻辑像素）。
static const int kTabWidthAll      = 48;
static const int kTabWidthContacts = 72;
static const int kTabWidthGroups   = 56;
static const int kTabWidthMessages = 48;
static const int kTabWidthFiles    = 48;

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
    , m_built(false)
    , m_filter(kFilterAll)
    , m_panelWidth(0)
    , m_anchorLeft(0)
    , m_anchorBottom(0)
    , m_scroll(NULL)
    , m_tabAll(NULL)
    , m_tabContacts(NULL)
    , m_tabGroups(NULL)
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

bool CSearchDropdown::EnsureCreated(HWND /*owner*/)
{
    if (m_hWnd != NULL)
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
        LOG_WARNING(_T("搜索下拉浮窗创建失败。"));
        return false;
    }

    // 用 DuiHost 接管这个 HWND（SubclassWindow 模式）：之后所有消息走 DuiHost
    // 的 WndProc → 本类消息映射。随后 SetRoot 会 AttachToHost 并按客户区布局。
    if (!SubclassWindow(hWnd))
    {
        ::DestroyWindow(hWnd);
        LOG_WARNING(_T("搜索下拉浮窗 SubclassWindow 失败。"));
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

    // ① 分类筛选标签栏：全部 / 联系人 / 群组 / 消息 / 文件（后两置灰占位）。
    {
        std::unique_ptr<balloonwjui::DuiHBox> row(new balloonwjui::DuiHBox());
        row->SetGap(kTabBarGap);
        row->SetPadding(kTabBarPadLeft, 0, 0, 0);   // 仅左内距（l,t,r,b）

        std::unique_ptr<balloonsearch::SearchFilterTab> tabAll(new balloonsearch::SearchFilterTab());
        tabAll->SetCtrlId(kIdTabAll);
        tabAll->SetLabel(_T("全部"));
        tabAll->SetActive(true);
        m_tabAll = tabAll.get();
        row->AddChild(std::move(tabAll), balloonwjui::DuiLayout::Hint().Fixed(kTabWidthAll));

        std::unique_ptr<balloonsearch::SearchFilterTab> tabContacts(new balloonsearch::SearchFilterTab());
        tabContacts->SetCtrlId(kIdTabContacts);
        tabContacts->SetLabel(_T("联系人"));
        tabContacts->SetCount(false, 0);
        m_tabContacts = tabContacts.get();
        row->AddChild(std::move(tabContacts), balloonwjui::DuiLayout::Hint().Fixed(kTabWidthContacts));

        std::unique_ptr<balloonsearch::SearchFilterTab> tabGroups(new balloonsearch::SearchFilterTab());
        tabGroups->SetCtrlId(kIdTabGroups);
        tabGroups->SetLabel(_T("群组"));
        tabGroups->SetCount(false, 0);
        m_tabGroups = tabGroups.get();
        row->AddChild(std::move(tabGroups), balloonwjui::DuiLayout::Hint().Fixed(kTabWidthGroups));

        std::unique_ptr<balloonsearch::SearchFilterTab> tabMessages(new balloonsearch::SearchFilterTab());
        tabMessages->SetCtrlId(kIdTabMessages);
        tabMessages->SetLabel(_T("消息"));
        tabMessages->SetGrayed(true);
        row->AddChild(std::move(tabMessages), balloonwjui::DuiLayout::Hint().Fixed(kTabWidthMessages));

        std::unique_ptr<balloonsearch::SearchFilterTab> tabFiles(new balloonsearch::SearchFilterTab());
        tabFiles->SetCtrlId(kIdTabFiles);
        tabFiles->SetLabel(_T("文件"));
        tabFiles->SetGrayed(true);
        row->AddChild(std::move(tabFiles), balloonwjui::DuiLayout::Hint().Fixed(kTabWidthFiles));

        // 尾部弹簧：把标签推到左侧。
        std::unique_ptr<balloonwjui::DuiLabel> spacer(new balloonwjui::DuiLabel());
        row->AddChild(std::move(spacer), balloonwjui::DuiLayout::Hint().Weight(1));

        root->AddChild(std::move(row), balloonwjui::DuiLayout::Hint().Fixed(kTabRowHeight));
    }

    // ② 结果滚动区（内容自动高度，初始空）。
    {
        std::unique_ptr<balloonwjui::DuiScrollView> scroll(new balloonwjui::DuiScrollView());
        scroll->SetAutoContentHeight(true);
        std::unique_ptr<balloonwjui::DuiVBox> content(new balloonwjui::DuiVBox());
        content->SetGap(1);
        scroll->SetContent(std::move(content));
        m_scroll = scroll.get();
        root->AddChild(std::move(scroll), balloonwjui::DuiLayout::Hint().Weight(1));
    }

    SetRoot(std::move(root));
    m_built = true;
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

    RefreshResults();
}

int CSearchDropdown::RefreshResults()
{
    if (m_scroll == NULL)
    {
        return 0;
    }

    m_rowContact.clear();
    m_rowGroup.clear();

    std::wstring qWide = (LPCWSTR)m_query;
    const bool wantContacts = (m_filter == kFilterAll || m_filter == kFilterContacts);
    const bool wantGroups   = (m_filter == kFilterAll || m_filter == kFilterGroups);

    // 先把两类命中各自收集出来（计数与渲染分离：标签计数总是反映各类真实命中数，
    // 渲染则按当前筛选只放该类）。
    std::vector<std::unique_ptr<balloonmain::MainPanelListItem> > contactRows;
    std::vector<UINT> contactIds;
    std::vector<std::unique_ptr<balloonmain::MainPanelListItem> > groupRows;
    std::vector<int64_t> groupIds;

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

            // 主行名：备注 → 昵称 → 账号 → 用户<id>。与通讯录树 / 会话列表收口在
            // 同一个纯函数上，同一个人不会在搜索结果里换个名字。
            CString display = balloonmain::ResolveContactDisplayName(Utf8ToCStr(c.m_remark),
                                                                     Utf8ToCStr(c.m_nickname),
                                                                     Utf8ToCStr(c.m_username),
                                                                     (UINT)c.m_userid);

            std::wstring shown = (LPCTSTR)display;             // 用户看到的那个名字
            std::wstring nick  = Utf8ToWStr(c.m_nickname);     // 原始昵称
            std::wstring acc   = Utf8ToWStr(c.m_username);
            std::wstring reg   = Utf8ToWStr(c.m_address);

            // 先按"看到的名字"匹配 —— 高亮区间是相对主行名给出的，用它才对得上。
            contactsearch::MatchResult mr = contactsearch::Match(qWide, shown, acc, reg);
            if (!mr.m_matched && shown != nick)
            {
                // 设了备注的人，用原始昵称也该能搜到：用户记得的往往还是对方的真名。
                // 这一轮只用来决定"要不要显示这一行"，命中区间落在昵称上、与主行名
                // 对不上，故不取它的高亮，只把行放出来。
                contactsearch::MatchResult byNick = contactsearch::Match(qWide, nick, acc, reg);
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
            // 与通讯录树同一口径：自己那一行加"（我）"后缀，便于一眼区分。
            if (isSelf)
            {
                display += _T("（我）");
            }

            // 副行："账号 xxx · 地区"（地区为空则省略）。
            CString sub;
            sub.Format(_T("账号 %s"), (LPCTSTR)Utf8ToCStr(c.m_username));
            if (!c.m_address.empty())
            {
                sub += _T(" · ");
                sub += Utf8ToCStr(c.m_address);
            }

            std::unique_ptr<balloonmain::MainPanelListItem> item(new balloonmain::MainPanelListItem());
            ApplyRowAvatar(item.get(), (int64_t)c.m_userid, display,
                           c.m_facetype, c.m_customface, (int64_t)m_selfUserId);
            item->SetName(display);
            item->SetSubText(sub);
            item->SetHoverFill(kRowHoverFill);
            if (mr.m_nameHiStart >= 0 && mr.m_nameHiLen > 0)
            {
                item->SetNameHighlight(mr.m_nameHiStart, mr.m_nameHiLen);
            }

            contactRows.push_back(std::move(item));
            contactIds.push_back((UINT)c.m_userid);
        }
    }

    // —— 群组匹配（群名当昵称，账号 / 地区留空，复用同一 Match）——
    if (m_groups != NULL)
    {
        for (size_t i = 0; i < m_groups->size(); ++i)
        {
            const net::UserGroupItem& g = (*m_groups)[i];
            std::wstring name = Utf8ToWStr(g.m_name);
            contactsearch::MatchResult mr =
                contactsearch::Match(qWide, name, std::wstring(), std::wstring());
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

    const int contactMatch = (int)contactRows.size();
    const int groupMatch    = (int)groupRows.size();

    // —— 按当前筛选渲染 ——
    std::unique_ptr<balloonwjui::DuiVBox> content(new balloonwjui::DuiVBox());
    content->SetGap(1);

    int renderedContacts = 0;
    int renderedGroups   = 0;

    if (wantContacts && contactMatch > 0)
    {
        std::unique_ptr<balloonwjui::DuiLabel> header(new balloonwjui::DuiLabel());
        header->SetText(_T("联系人"));
        header->SetTextColor(kSectionColor);
        header->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        content->AddChild(std::move(header),
                          balloonwjui::DuiLayout::Hint().Fixed(kSectionHeaderHeight));

        for (size_t i = 0; i < contactRows.size(); ++i)
        {
            balloonmain::MainPanelListItem* raw = contactRows[i].get();
            m_rowContact[raw] = contactIds[i];
            content->AddChild(std::move(contactRows[i]),
                              balloonwjui::DuiLayout::Hint().Fixed(kResultRowHeight));
        }
        renderedContacts = contactMatch;
    }

    if (wantGroups && groupMatch > 0)
    {
        std::unique_ptr<balloonwjui::DuiLabel> header(new balloonwjui::DuiLabel());
        header->SetText(_T("群组"));
        header->SetTextColor(kSectionColor);
        header->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        content->AddChild(std::move(header),
                          balloonwjui::DuiLayout::Hint().Fixed(kSectionHeaderHeight));

        for (size_t i = 0; i < groupRows.size(); ++i)
        {
            balloonmain::MainPanelListItem* raw = groupRows[i].get();
            m_rowGroup[raw] = groupIds[i];
            content->AddChild(std::move(groupRows[i]),
                              balloonwjui::DuiLayout::Hint().Fixed(kResultRowHeight));
        }
        renderedGroups = groupMatch;
    }

    if (renderedContacts == 0 && renderedGroups == 0)
    {
        // 当前筛选下无命中：给一句居中灰色提示。
        std::unique_ptr<balloonwjui::DuiLabel> hint(new balloonwjui::DuiLabel());
        hint->SetText(_T("无匹配结果"));
        hint->SetTextColor(kSectionColor);
        hint->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        content->AddChild(std::move(hint),
                          balloonwjui::DuiLayout::Hint().Fixed(kNoResultHintHeight));
    }

    m_scroll->SetContent(std::move(content));

    // —— 更新标签计数（非空查询才显示数字）——
    const bool showCount = !m_query.IsEmpty();
    if (m_tabAll != NULL)
    {
        m_tabAll->SetCount(showCount, contactMatch + groupMatch);
    }
    if (m_tabContacts != NULL)
    {
        m_tabContacts->SetCount(showCount, contactMatch);
    }
    if (m_tabGroups != NULL)
    {
        m_tabGroups->SetCount(showCount, groupMatch);
    }

    // —— 按内容算高度并在搜索框下方（屏幕坐标）展示 ——
    int h = ComputeHeight(renderedContacts, renderedGroups, true);
    if (m_hWnd != NULL)
    {
        // HWND_TOPMOST：可靠地浮在仍处于前台的主面板之上；SWP_NOACTIVATE 不抢激活。
        ::SetWindowPos(m_hWnd, HWND_TOPMOST, m_anchorLeft, m_anchorBottom, m_panelWidth, h,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ::InvalidateRect(m_hWnd, NULL, FALSE);
    }

    return renderedContacts + renderedGroups;
}

int CSearchDropdown::ComputeHeight(int contactRows, int groupRows, bool /*anyQuery*/) const
{
    int content = 0;
    if (contactRows > 0)
    {
        content += kSectionHeaderHeight + contactRows * kResultRowStride;
    }
    if (groupRows > 0)
    {
        content += kSectionHeaderHeight + groupRows * kResultRowStride;
    }
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
    if (m_hWnd != NULL && ::IsWindowVisible(m_hWnd))
    {
        ::ShowWindow(m_hWnd, SW_HIDE);
    }
}

bool CSearchDropdown::IsShown() const
{
    return m_hWnd != NULL && ::IsWindowVisible(m_hWnd) != FALSE;
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

    // 只处理单击；其余通知放行。
    if (n->code != balloonwjui::DUIN_CLICK)
    {
        bHandled = FALSE;
        return 0;
    }

    // —— 结果行单击 → 回调并隐藏 ——
    std::map<balloonwjui::DuiControl*, UINT>::iterator itc = m_rowContact.find(pCtrl);
    if (itc != m_rowContact.end())
    {
        UINT peerUserId = itc->second;
        HideDropdown();
        if (m_contactCb != NULL)
        {
            m_contactCb(m_pickUd, peerUserId);
        }
        return 0;
    }
    std::map<balloonwjui::DuiControl*, int64_t>::iterator itg = m_rowGroup.find(pCtrl);
    if (itg != m_rowGroup.end())
    {
        int64_t groupId = itg->second;
        HideDropdown();
        if (m_groupCb != NULL)
        {
            m_groupCb(m_pickUd, groupId);
        }
        return 0;
    }

    // —— 筛选标签单击 → 切换筛选并刷新（仅 全部 / 联系人 / 群组 可点）——
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
    if (newFilter >= 0)
    {
        m_filter = newFilter;
        if (m_tabAll != NULL)
        {
            m_tabAll->SetActive(newFilter == kFilterAll);
        }
        if (m_tabContacts != NULL)
        {
            m_tabContacts->SetActive(newFilter == kFilterContacts);
        }
        if (m_tabGroups != NULL)
        {
            m_tabGroups->SetActive(newFilter == kFilterGroups);
        }
        RefreshResults();
        return 0;
    }

    bHandled = FALSE;
    return 0;
}
