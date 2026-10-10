/**
 *  群通话的通话窗，GroupCallWindow.cpp
 *  布局与职责见 GroupCallWindow.h。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#include "stdafx.h"

#include "GroupCallWindow.h"

#include <memory>
#include "../Call/CallVideoCanvas.h"
#include "../../Source/Common/Log/IULog.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiAvatar.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiButton.h"
#include "../../../third-party/balloonui/balloonui/Controls/Basic/DuiLabel.h"
#include "../../../third-party/balloonui/balloonui/Controls/Layout/DuiLayout.h"
#include "../../../third-party/balloonui/balloonui/Controls/List/DuiMenu.h"
#include "../../Source/Common/I18n/I18n.h"   // TR / TRF：界面文字的多语言查找
#include "../Common/WindowPin.h"             // 标题栏图钉的图标、提示与固定期间的拦截判定

namespace groupcall {

namespace {

//整窗的默认尺寸与最小尺寸、标题栏高（逻辑像素）；尺寸含标题栏（开发约定文档「UI 约定」第 7 条）
const int kWindowW = 960;
const int kWindowH = 640;
const int kMinWindowW = 640;
const int kMinWindowH = 480;
const int kTitleBarH = 36;
//宫格的内边距与格子间距、卡片圆角与内边距、说话描边宽
const int kGridPadding = 8;
const int kGridGap = 6;
const int kCardRadius = 6;
const int kCardPadding = 4;
const float kSpeakingBorderW = 2.0f;
//卡片里头像的边长、名字行的高
const int kAvatarSize = 72;
const int kNameRowH = 22;
//头像小条的高、小头像的边长与间距
const int kStripH = 56;
const int kStripAvatar = 40;
const int kStripGap = 8;
//状态行高、底部按钮栏高、按钮宽与高、时长与人数标签宽
const int kStatusH = 24;
const int kBottomH = 60;
const int kButtonW = 96;
const int kButtonH = 34;
const int kSideLabelW = 120;
const int kBottomGap = 12;
//颜色：宫格底色、卡片底色、说话描边、名字与状态行文字、底部栏底色与文字
const COLORREF kStageBg = RGB(0x1F, 0x23, 0x29);
const COLORREF kCardBg = RGB(0x2B, 0x30, 0x38);
const COLORREF kSpeakingBorder = RGB(0x3B, 0xB2, 0x73);
const COLORREF kNameColor = RGB(0xE8, 0xEA, 0xED);
const COLORREF kStatusColor = RGB(0xB0, 0xB6, 0xC0);
const COLORREF kBottomBg = RGB(0x17, 0x1A, 0x1F);
const COLORREF kBottomText = RGB(0xC8, 0xCC, 0xD2);
//「成员管理」菜单的命令号：全员静音，以及每个成员的静音、移出（加上成员在 m_menuTargets 里的下标）
const UINT kMenuMuteAll = 100;
const UINT kMenuMuteBase = 1000;
const UINT kMenuRemoveBase = 2000;
const UINT kMenuSubBase = 3000;

//等宽的弹性空白
std::unique_ptr<balloonwjui::DuiControl> spacer()
{
    return std::unique_ptr<balloonwjui::DuiControl>(new balloonwjui::DuiControl());
}

std::unique_ptr<balloonwjui::DuiButton> makeButton(UINT id, const wchar_t* text,
                                                   balloonwjui::DuiButton::Variant variant)
{
    std::unique_ptr<balloonwjui::DuiButton> btn(new balloonwjui::DuiButton());
    btn->SetCtrlId(id);
    btn->SetText(text);
    btn->SetVariant(variant);
    return btn;
}

} // namespace

GroupCallWindowView::GroupCallWindowView()
    : m_micOn(false)
    , m_cameraOn(false)
    , m_canManage(false)
    , m_allMuted(false)
    , m_micLocked(false)
    , m_joinedCount(0)
{
}

CGroupCallWindow::TileRefs::TileRefs()
    : m_card(NULL)
    , m_canvas(NULL)
    , m_avatar(NULL)
    , m_nameLabel(NULL)
    , m_lastSerial(0)
{
}

CGroupCallWindow::CGroupCallWindow()
    : m_notifyWnd(NULL)
    , m_source(NULL)
    , m_built(false)
    , m_gridHolder(NULL)
    , m_grid(NULL)
    , m_strip(NULL)
    , m_statusLabel(NULL)
    , m_durationLabel(NULL)
    , m_countLabel(NULL)
    , m_micBtn(NULL)
    , m_cameraBtn(NULL)
    , m_manageBtn(NULL)
    , m_pendingTarget(0)
    , m_pinned(false)
    , m_pinIconId(0)
    , m_pinIconNormal(NULL)
    , m_pinIconActive(NULL)
{
}

CGroupCallWindow::~CGroupCallWindow()
{
    Close();
    //图钉位图由本对象持有（AddCaptionIcon 只存裸指针），窗口已销毁，此处释放
    if (m_pinIconNormal != NULL)
        ::DeleteObject(m_pinIconNormal);
    if (m_pinIconActive != NULL)
        ::DeleteObject(m_pinIconActive);
}

void CGroupCallWindow::SetHost(flamingomedia::IMediaHost* host)
{
    m_avatars.SetHost(host);
}

void CGroupCallWindow::Open(HWND owner, HWND notifyWnd, IGroupCallFrameSource* source, const GroupCallWindowView& view)
{
    m_notifyWnd = notifyWnd;
    m_source = source;
    m_view = view;
    if (!IsWindow())
    {
        //进任务栏、能最小化：通话可能持续很久，用户要能找回它
        const DWORD exStyle = WS_EX_APPWINDOW;
        if (Create(NULL, CWindow::rcDefault, view.m_title.c_str(), WS_POPUP | WS_MINIMIZEBOX, exStyle) == NULL)
        {
            LOG_ERROR("groupcall: create call window failed.");
            return;
        }
        SetTitleBarHeight(kTitleBarH);
        SetButtons(true, true, true);
        SetResizable(true);
        SetMinSize(kMinWindowW, kMinWindowH);
        SetIcon(m_icons.TitleBitmap(balloonmain::kProgramTitleIconSize, balloonmain::kOpaqueTitleBarColor));
        m_icons.ApplyTaskIcons(m_hWnd);
        m_built = false;
        //新窗口的标题栏是新建的：图钉从未固定开始
        m_pinned = false;
        m_pinIconId = 0;
        EnsurePinCaptionIcon();
        ResizeClient(kWindowW, kWindowH);
        //在主面板所在显示器的工作区里居中
        HMONITOR monitor = ::MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (::GetMonitorInfo(monitor, &mi))
        {
            RECT rc;
            GetWindowRect(&rc);
            const int w = rc.right - rc.left;
            const int h = rc.bottom - rc.top;
            const int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
            const int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
            SetWindowPos(NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    else if (m_popup.TakePendingDestroy())
    {
        //成员管理菜单弹出期间被关闭、等待销毁的窗口：直接复用并取消推迟的销毁（否则 m_hWnd 不为空，再 Create 会触发
        //atlwin.h 的断言）；骨架由下面按 m_built 重建，图钉按新窗口复位
        ResetPinForReuse();
        LOG_INFO("groupcall window reused while its manage menu is closing.");
    }
    SetTitle(view.m_title.c_str());
    if (!m_built)
        BuildSkeleton();
    RebuildGrid();
    RebuildStrip();
    ApplyTexts();
    ShowWindow(SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    LOG_INFO("groupcall window opened, tiles=%u.", static_cast<unsigned>(view.m_grid.size()));
}

void CGroupCallWindow::Update(const GroupCallWindowView& view)
{
    if (!IsWindow() || !m_built)
        return;
    const bool gridChanged = !SameGridStructure(m_view.m_grid, view.m_grid);
    const bool stripChanged = !SameGridStructure(m_view.m_strip, view.m_strip);
    m_view = view;
    SetTitle(view.m_title.c_str());
    if (gridChanged)
        RebuildGrid();
    else
    {
        //结构相同：只换每格对应的人的状态（麦克风、在线），画面控件保留
        for (size_t i = 0; i < m_tiles.size() && i < view.m_grid.size(); ++i)
            m_tiles[i].m_tile = view.m_grid[i];
    }
    if (stripChanged)
        RebuildStrip();
    ApplyTexts();
}

void CGroupCallWindow::SetDuration(const std::wstring& text)
{
    if (!IsWindow() || m_durationLabel == NULL)
        return;
    m_durationLabel->SetText(text.c_str());
    m_durationLabel->Invalidate();
}

void CGroupCallWindow::RefreshFrames()
{
    if (!IsWindow() || m_source == NULL || ::IsIconic(m_hWnd))
        return;
    for (size_t i = 0; i < m_tiles.size(); ++i)
    {
        TileRefs& t = m_tiles[i];
        if (t.m_canvas == NULL)
            continue;
        int width = 0;
        int height = 0;
        uint64_t serial = 0;
        if (!m_source->GetTileFrame(t.m_tile.m_seat, t.m_tile.m_self, m_frameBuf, width, height, serial))
            continue;
        if (serial == t.m_lastSerial || width <= 0 || height <= 0 || m_frameBuf.empty())
            continue;
        t.m_lastSerial = serial;
        t.m_canvas->SetFrame(&m_frameBuf[0], width, height);
        t.m_canvas->Invalidate();
    }
}

void CGroupCallWindow::RefreshAvatars()
{
    if (!IsWindow())
        return;
    //就地换位图；位图按内容缓存，旧位图仍由 m_avatars 持有，不会在重画前失效
    for (size_t i = 0; i < m_tiles.size(); ++i)
    {
        TileRefs& t = m_tiles[i];
        if (t.m_avatar != NULL)
        {
            m_avatars.ApplyUser(t.m_avatar, t.m_tile.m_userId, kAvatarSize);
            t.m_avatar->Invalidate();
        }
        if (t.m_canvas != NULL)
        {
            t.m_canvas->SetAvatar(m_avatars.GetUserBitmap(t.m_tile.m_userId, kAvatarSize));
            t.m_canvas->Invalidate();
        }
    }
    for (size_t i = 0; i < m_stripAvatars.size() && i < m_view.m_strip.size(); ++i)
    {
        m_avatars.ApplyUser(m_stripAvatars[i], m_view.m_strip[i].m_userId, kStripAvatar);
        m_stripAvatars[i]->Invalidate();
    }
}

void CGroupCallWindow::Close()
{
    m_notifyWnd = NULL;
    m_source = NULL;
    m_tiles.clear();
    m_stripAvatars.clear();
    m_grid = NULL;
    m_gridHolder = NULL;
    m_strip = NULL;
    m_statusLabel = NULL;
    m_durationLabel = NULL;
    m_countLabel = NULL;
    m_micBtn = NULL;
    m_cameraBtn = NULL;
    m_manageBtn = NULL;
    m_built = false;
    if (!IsWindow())
    {
        m_pinIconId = 0;
        m_pinned = false;
        return;
    }
    //成员管理菜单正在弹出：菜单的嵌套消息循环还在本窗口的消息处理里，此时销毁窗口，ATL 要等那条消息返回才清 m_hWnd，
    //菜单也不随之关闭。改为先关菜单、只隐藏窗口，等 TrackPopup 返回后由 OnManageMenuMsg 销毁（图钉随窗口保留）
    if (screenshare::ShareBarShouldDeferDestroy(m_popup.IsActive()))
    {
        m_popup.Close();
        m_popup.SetPendingDestroy();
        ShowWindow(SW_HIDE);
        LOG_INFO("groupcall window closed while its manage menu is open, destroy deferred.");
        return;
    }
    //标题栏随窗口销毁，图钉与固定态一并失效
    m_pinIconId = 0;
    m_pinned = false;
    DestroyWindow();
}

bool CGroupCallWindow::IsShowing() const
{
    return const_cast<CGroupCallWindow*>(this)->IsWindow() != FALSE;
}

bool CGroupCallWindow::IsMinimizedNow() const
{
    return IsShowing() && ::IsIconic(m_hWnd) != FALSE;
}

int64_t CGroupCallWindow::TakePendingTarget()
{
    const int64_t target = m_pendingTarget;
    m_pendingTarget = 0;
    return target;
}

void CGroupCallWindow::BuildSkeleton()
{
    std::unique_ptr<balloonwjui::DuiVBox> root(new balloonwjui::DuiVBox());
    root->SetBgColor(kStageBg);

    //宫格的父容器：重建宫格时只换它的唯一子控件
    std::unique_ptr<balloonwjui::DuiVBox> holder(new balloonwjui::DuiVBox());
    m_gridHolder = holder.get();
    root->AddChild(std::move(holder), balloonwjui::DuiLayout::Hint().Weight(1));

    std::unique_ptr<balloonwjui::DuiHBox> strip(new balloonwjui::DuiHBox());
    strip->SetPadding(kGridPadding, 0, kGridPadding, 0);
    strip->SetGap(kStripGap);
    m_strip = strip.get();
    root->AddChild(std::move(strip), balloonwjui::DuiLayout::Hint().Fixed(kStripH));

    std::unique_ptr<balloonwjui::DuiLabel> status(new balloonwjui::DuiLabel());
    status->SetTextColor(kStatusColor);
    status->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    m_statusLabel = status.get();
    root->AddChild(std::move(status), balloonwjui::DuiLayout::Hint().Fixed(kStatusH));

    std::unique_ptr<balloonwjui::DuiVBox> bottomBg(new balloonwjui::DuiVBox());
    bottomBg->SetBgColor(kBottomBg);
    std::unique_ptr<balloonwjui::DuiHBox> bottom(new balloonwjui::DuiHBox());
    bottom->SetPadding(kGridPadding * 2, (kBottomH - kButtonH) / 2, kGridPadding * 2, (kBottomH - kButtonH) / 2);
    bottom->SetGap(kBottomGap);
    std::unique_ptr<balloonwjui::DuiLabel> duration(new balloonwjui::DuiLabel());
    duration->SetTextColor(kBottomText);
    duration->SetTextAlign(DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    m_durationLabel = duration.get();
    bottom->AddChild(std::move(duration), balloonwjui::DuiLayout::Hint().Fixed(kSideLabelW));
    bottom->AddChild(spacer(), balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiButton> mic = makeButton(kIdMic, TR(L"关闭麦克风"),
                                                             balloonwjui::DuiButton::Variant::Default);
    m_micBtn = mic.get();
    bottom->AddChild(std::move(mic), balloonwjui::DuiLayout::Hint().Fixed(kButtonW));
    std::unique_ptr<balloonwjui::DuiButton> camera = makeButton(kIdCamera, TR(L"打开摄像头"),
                                                                balloonwjui::DuiButton::Variant::Default);
    m_cameraBtn = camera.get();
    bottom->AddChild(std::move(camera), balloonwjui::DuiLayout::Hint().Fixed(kButtonW));
    bottom->AddChild(makeButton(kIdInvite, TR(L"邀请成员"), balloonwjui::DuiButton::Variant::Default),
                     balloonwjui::DuiLayout::Hint().Fixed(kButtonW));
    std::unique_ptr<balloonwjui::DuiButton> manage = makeButton(kIdManage, TR(L"成员管理"),
                                                                balloonwjui::DuiButton::Variant::Default);
    m_manageBtn = manage.get();
    bottom->AddChild(std::move(manage), balloonwjui::DuiLayout::Hint().Fixed(kButtonW));
    bottom->AddChild(makeButton(kIdHangup, TR(L"挂断"), balloonwjui::DuiButton::Variant::Danger),
                     balloonwjui::DuiLayout::Hint().Fixed(kButtonW));
    bottom->AddChild(spacer(), balloonwjui::DuiLayout::Hint().Weight(1));
    std::unique_ptr<balloonwjui::DuiLabel> count(new balloonwjui::DuiLabel());
    count->SetTextColor(kBottomText);
    count->SetTextAlign(DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    m_countLabel = count.get();
    bottom->AddChild(std::move(count), balloonwjui::DuiLayout::Hint().Fixed(kSideLabelW));
    bottomBg->AddChild(std::move(bottom), balloonwjui::DuiLayout::Hint().Weight(1));
    root->AddChild(std::move(bottomBg), balloonwjui::DuiLayout::Hint().Fixed(kBottomH));

    SetClientContent(std::move(root));
    m_built = true;
}

void CGroupCallWindow::RebuildGrid()
{
    if (m_gridHolder == NULL)
        return;
    if (m_grid != NULL)
    {
        m_gridHolder->RemoveChild(m_grid);
        m_grid = NULL;
    }
    m_tiles.clear();

    int rows = 1;
    int cols = 1;
    GridShapeFor(static_cast<int>(m_view.m_grid.size()), rows, cols);
    std::unique_ptr<balloonwjui::DuiGrid> grid(new balloonwjui::DuiGrid());
    grid->SetGrid(rows, cols);
    grid->SetPadding(kGridPadding);
    grid->SetGap(kGridGap);
    for (size_t i = 0; i < m_view.m_grid.size(); ++i)
    {
        const GroupCallTile& tile = m_view.m_grid[i];
        TileRefs refs;
        refs.m_tile = tile;
        std::unique_ptr<balloonwjui::DuiVBox> card(new balloonwjui::DuiVBox());
        card->SetBgColor(kCardBg);
        card->SetCornerRadius(kCardRadius);
        card->SetPadding(kCardPadding);
        refs.m_card = card.get();
        if (tile.m_camera && !tile.m_ringing)
        {
            std::unique_ptr<call::CallVideoCanvas> canvas(new call::CallVideoCanvas());
            //还没收到第一帧时先显示这个人的头像
            canvas->SetAvatar(m_avatars.GetUserBitmap(tile.m_userId, kAvatarSize));
            refs.m_canvas = canvas.get();
            card->AddChild(std::move(canvas), balloonwjui::DuiLayout::Hint().Weight(1));
        }
        else
        {
            //没开摄像头：头像居中（照片或与主面板相同的字形，docs/音视频通话优化方案.md 第 5 项）
            card->AddChild(spacer(), balloonwjui::DuiLayout::Hint().Weight(1));
            std::unique_ptr<balloonwjui::DuiHBox> avatarRow(new balloonwjui::DuiHBox());
            avatarRow->AddChild(spacer(), balloonwjui::DuiLayout::Hint().Weight(1));
            std::unique_ptr<balloonwjui::DuiAvatar> avatar = m_avatars.MakeUserAvatar(tile.m_userId, kAvatarSize);
            refs.m_avatar = avatar.get();
            avatarRow->AddChild(std::move(avatar), balloonwjui::DuiLayout::Hint().Fixed(kAvatarSize));
            avatarRow->AddChild(spacer(), balloonwjui::DuiLayout::Hint().Weight(1));
            card->AddChild(std::move(avatarRow), balloonwjui::DuiLayout::Hint().Fixed(kAvatarSize));
            card->AddChild(spacer(), balloonwjui::DuiLayout::Hint().Weight(1));
        }
        std::unique_ptr<balloonwjui::DuiLabel> name(new balloonwjui::DuiLabel());
        name->SetTextColor(kNameColor);
        name->SetTextAlign(DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        refs.m_nameLabel = name.get();
        card->AddChild(std::move(name), balloonwjui::DuiLayout::Hint().Fixed(kNameRowH));
        grid->AddChild(std::move(card), balloonwjui::DuiLayout::Hint());
        m_tiles.push_back(refs);
    }
    m_grid = grid.get();
    m_gridHolder->AddChild(std::move(grid), balloonwjui::DuiLayout::Hint().Weight(1));
    m_gridHolder->ForceLayout(m_gridHolder->GetRect());
    m_gridHolder->Invalidate();
}

void CGroupCallWindow::RebuildStrip()
{
    if (m_strip == NULL)
        return;
    //逐个移除旧的小头像（每次移除第一个，直到没有子控件）
    while (!m_strip->Children().empty())
        m_strip->RemoveChild(m_strip->Children().front().get());
    m_stripAvatars.clear();
    for (size_t i = 0; i < m_view.m_strip.size(); ++i)
    {
        std::unique_ptr<balloonwjui::DuiAvatar> avatar = m_avatars.MakeUserAvatar(m_view.m_strip[i].m_userId,
                                                                                  kStripAvatar);
        m_stripAvatars.push_back(avatar.get());
        m_strip->AddChild(std::move(avatar), balloonwjui::DuiLayout::Hint().Fixed(kStripAvatar, kStripAvatar)
                                                 .AlignC(balloonwjui::DuiLayout::AlignCenter));
    }
    const bool show = !m_view.m_strip.empty();
    if (m_strip->IsVisible() != show)
    {
        m_strip->SetVisible(show);
        RelayoutParent(m_strip);
    }
    else
    {
        m_strip->ForceLayout(m_strip->GetRect());
        m_strip->Invalidate();
    }
}

void CGroupCallWindow::ApplyTexts()
{
    for (size_t i = 0; i < m_tiles.size(); ++i)
    {
        TileRefs& t = m_tiles[i];
        if (t.m_nameLabel != NULL)
        {
            t.m_nameLabel->SetText(TileCaption(t.m_tile).c_str());
            t.m_nameLabel->Invalidate();
        }
        if (t.m_card != NULL)
        {
            //正在说话：描一圈绿边
            const bool speaking = !t.m_tile.m_ringing && t.m_tile.m_mic &&
                                  m_view.m_speakingSeats.find(t.m_tile.m_seat) != m_view.m_speakingSeats.end();
            t.m_card->SetBorderColor(speaking ? kSpeakingBorder : CLR_INVALID);
            t.m_card->SetBorderWidth(kSpeakingBorderW);
            t.m_card->Invalidate();
        }
    }
    if (m_statusLabel != NULL)
    {
        m_statusLabel->SetText(m_view.m_status.c_str());
        m_statusLabel->Invalidate();
    }
    if (m_countLabel != NULL)
    {
        m_countLabel->SetText(TRF(L"{0} 人通话中", m_view.m_joinedCount).c_str());
        m_countLabel->Invalidate();
    }
    if (m_micBtn != NULL)
    {
        m_micBtn->SetText(m_view.m_micOn ? TR(L"关闭麦克风") : TR(L"打开麦克风"));
        //全员静音期间没有管理权的人不能打开麦克风：按钮置灰（状态行另写「已开启全员静音」）
        m_micBtn->SetEnabled(!m_view.m_micLocked);
        m_micBtn->Invalidate();
    }
    if (m_cameraBtn != NULL)
    {
        m_cameraBtn->SetText(m_view.m_cameraOn ? TR(L"关闭摄像头") : TR(L"打开摄像头"));
        m_cameraBtn->Invalidate();
    }
    if (m_manageBtn != NULL && m_manageBtn->IsVisible() != m_view.m_canManage)
    {
        m_manageBtn->SetVisible(m_view.m_canManage);
        RelayoutParent(m_manageBtn);
    }
}

std::wstring CGroupCallWindow::TileCaption(const GroupCallTile& tile) const
{
    std::wstring name = tile.m_self ? std::wstring(TR(L"我")) : NameOf(tile.m_userId);
    if (tile.m_ringing)
        return TRF(L"{0} · 等待接听", name);
    if (!tile.m_online)
        return TRF(L"{0} · 连接中断", name);
    if (!tile.m_mic)
        return TRF(L"{0} · 已静音", name);
    return name;
}

std::wstring CGroupCallWindow::NameOf(int64_t userId) const
{
    std::map<int64_t, std::wstring>::const_iterator it = m_view.m_names.find(userId);
    if (it != m_view.m_names.end() && !it->second.empty())
        return it->second;
    return TR(L"成员");
}

void CGroupCallWindow::Send(int command)
{
    if (m_notifyWnd != NULL && ::IsWindow(m_notifyWnd))
        ::PostMessage(m_notifyWnd, kMsgGroupCallWindowCommand, static_cast<WPARAM>(command), 0);
}

void CGroupCallWindow::RelayoutParent(balloonwjui::DuiControl* ctrl)
{
    balloonwjui::DuiControl* parent = (ctrl != NULL) ? ctrl->GetParent() : NULL;
    if (parent != NULL)
    {
        parent->ForceLayout(parent->GetRect());
        parent->Invalidate();
    }
}

LRESULT CGroupCallWindow::OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    const balloonwjui::DuiNotify* n = reinterpret_cast<const balloonwjui::DuiNotify*>(lParam);
    //标题栏图钉：caption icon 的通知不带有意义的 ctrlId，靠 extra 里回带的 captionId 区分（开发约定文档「UI 约定」第 4 条）
    if (n != NULL && n->code == static_cast<UINT>(balloonwjui::DuiFrameWindow::DUIFW_CAPTION_ICON_CLICK)
        && m_pinIconId != 0 && static_cast<int>(n->extra) == m_pinIconId)
    {
        TogglePinned();
        return 0;
    }
    if (n == NULL || n->code != static_cast<UINT>(balloonwjui::DUIN_CLICK))
    {
        bHandled = FALSE;
        return 0;
    }
    switch (n->ctrlId)
    {
    //麦克风开关
    case kIdMic:
        Send(gc_window_toggle_mic);
        return 0;

    //摄像头开关
    case kIdCamera:
        Send(gc_window_toggle_camera);
        return 0;

    //邀请成员：控制器请宿主弹选人框
    case kIdInvite:
        Send(gc_window_invite);
        return 0;

    //成员管理：等按钮通知处理完再弹菜单（菜单是模态的）
    case kIdManage:
        PostMessage(kMsgGroupCallWindowMenu, 0, 0);
        return 0;

    //挂断
    case kIdHangup:
        Send(gc_window_hangup);
        return 0;

    //其它控件的点击：交给基类
    default:
        bHandled = FALSE;
        return 0;
    }
}

LRESULT CGroupCallWindow::OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //关窗等同挂断：窗口由控制器在收尾时关闭
    Send(gc_window_hangup);
    return 0;
}

LRESULT CGroupCallWindow::OnSysCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)lParam;
    if (m_pinned && flamingomedia::IsSysCommandBlockedWhenPinned(wParam))
        return 0;
    bHandled = FALSE;
    return 0;
}

LRESULT CGroupCallWindow::OnWindowPosChangingMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    if (m_pinned)
        flamingomedia::KeepWindowPosWhenPinned(reinterpret_cast<WINDOWPOS*>(lParam));
    //基类也要处理（最小尺寸等），交回消息链
    bHandled = FALSE;
    return 0;
}

void CGroupCallWindow::EnsurePinCaptionIcon()
{
    //两种状态的图钉位图各生成一次，之后复用
    if (m_pinIconNormal == NULL)
        m_pinIconNormal = flamingomedia::MakePinCaptionIcon(false);
    if (m_pinIconActive == NULL)
        m_pinIconActive = flamingomedia::MakePinCaptionIcon(true);
    HBITMAP icon = m_pinned ? m_pinIconActive : m_pinIconNormal;
    if (icon == NULL)
        return;
    //caption icon 的图标与提示在添加时确定，状态变化时先移除旧的再按新状态添加；本窗口只有这一个 caption icon
    if (m_pinIconId != 0)
    {
        RemoveCaptionIcon(m_pinIconId);
        m_pinIconId = 0;
    }
    m_pinIconId = AddCaptionIcon(icon, flamingomedia::PinCaptionTooltip(m_pinned));
}

void CGroupCallWindow::TogglePinned()
{
    if (!IsWindow())
        return;
    //最大化时固定：先还原成普通大小再固定，否则固定后既不能还原也不能改大小
    if (!m_pinned && ::IsZoomed(m_hWnd))
        ShowWindow(SW_RESTORE);
    m_pinned = !m_pinned;
    const bool topmostNow = flamingomedia::ApplyWindowTopmost(m_hWnd, m_pinned);
    //固定期间没有最小化与最大化按钮、不能拖边框改大小；拖标题栏、双击标题栏等由 OnSysCommandMsg / OnWindowPosChangingMsg 拦下
    SetButtons(!m_pinned, !m_pinned, true);
    SetResizable(!m_pinned);
    EnsurePinCaptionIcon();
    LOG_INFO("groupcall window toggle pinned, pinned=%d, topmost now=%d.", m_pinned ? 1 : 0, topmostNow ? 1 : 0);
}

void CGroupCallWindow::ResetPinForReuse()
{
    if (!IsWindow())
        return;
    if (m_pinned)
    {
        m_pinned = false;
        flamingomedia::ApplyWindowTopmost(m_hWnd, false);
        SetButtons(true, true, true);
        SetResizable(true);
    }
    //EnsurePinCaptionIcon 先移除旧图钉，再按未固定的状态装上
    EnsurePinCaptionIcon();
}

LRESULT CGroupCallWindow::OnManageMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
    (void)uMsg;
    (void)wParam;
    (void)lParam;
    (void)bHandled;
    //已经有菜单开着、窗口已隐藏（等待销毁或通话已结束）：不再弹
    if (!m_view.m_canManage || m_popup.IsActive() || m_popup.IsPendingDestroy() || !IsWindowVisible())
        return 0;
    //菜单：全员静音（可勾选，勾上表示房间处于全员静音，再点一次解除）；每个其他人一个子菜单（已加入的人可静音、移出，
    //振铃中的人只能取消邀请）
    balloonwjui::DuiMenu menu;
    menu.AppendChecked(kMenuMuteAll, TR(L"全员静音"), m_view.m_allMuted);
    m_menuTargets.clear();
    std::vector<std::unique_ptr<balloonwjui::DuiMenu> > subMenus;
    std::vector<GroupCallTile> everyone = m_view.m_grid;
    everyone.insert(everyone.end(), m_view.m_strip.begin(), m_view.m_strip.end());
    for (size_t i = 0; i < everyone.size(); ++i)
    {
        const GroupCallTile& t = everyone[i];
        if (t.m_self)
            continue;
        if (m_menuTargets.empty())
            menu.AppendSeparator();
        const UINT index = static_cast<UINT>(m_menuTargets.size());
        m_menuTargets.push_back(t.m_userId);
        std::unique_ptr<balloonwjui::DuiMenu> sub(new balloonwjui::DuiMenu());
        if (!t.m_ringing)
            sub->AppendItem(kMenuMuteBase + index, TR(L"静音"));
        sub->AppendItem(kMenuRemoveBase + index, t.m_ringing ? TR(L"取消邀请") : TR(L"移出通话"));
        menu.AppendSubMenu(kMenuSubBase + index, NameOf(t.m_userId).c_str(), sub.get());
        subMenus.push_back(std::move(sub));
    }
    RECT rc = { 0, 0, 0, 0 };
    if (m_manageBtn != NULL)
        rc = m_manageBtn->GetRect();
    POINT pt = { rc.left, rc.top };
    ClientToScreen(&pt);
    //菜单向上弹：先量好高度，左上角放在按钮上方
    const SIZE size = menu.MeasureSize();
    //记下正在弹出的菜单与子菜单：期间通话窗被关闭时据此关掉它们
    m_popup.BeginMenu(&menu, subMenus);
    const UINT chosen = menu.TrackPopup(pt.x, pt.y - size.cy, m_hWnd);
    m_popup.End();
    //菜单期间控制器关闭了通话窗：现在才销毁窗口（本条消息是投递来的，返回后 ATL 即清空 m_hWnd）
    if (m_popup.TakePendingDestroy())
    {
        m_pinIconId = 0;
        m_pinned = false;
        LOG_INFO("groupcall window destroyed after its manage menu closed.");
        DestroyWindow();
        return 0;
    }
    //菜单期间通话窗被关闭过（通话已结束，可能又开始了下一场）或通知窗口没了：丢弃这次选择
    if (!screenshare::ShareBarShouldDeliverChoice(chosen, m_popup.WasInterrupted(),
                                                  m_notifyWnd != NULL && ::IsWindow(m_notifyWnd)))
        return 0;
    if (chosen == kMenuMuteAll)
    {
        //按弹出菜单时的状态切换：原本没勾选就开启，已勾选就解除。勾选状态等服务端推回快照后随之刷新
        Send(m_view.m_allMuted ? gc_window_unmute_all : gc_window_mute_all);
    }
    else if (chosen >= kMenuMuteBase && chosen < kMenuMuteBase + m_menuTargets.size())
    {
        m_pendingTarget = m_menuTargets[chosen - kMenuMuteBase];
        Send(gc_window_mute_member);
    }
    else if (chosen >= kMenuRemoveBase && chosen < kMenuRemoveBase + m_menuTargets.size())
    {
        m_pendingTarget = m_menuTargets[chosen - kMenuRemoveBase];
        Send(gc_window_remove_member);
    }
    return 0;
}

} // namespace groupcall
