/**
 *  群通话的通话窗，GroupCallWindow.h
 *
 *  接通后显示的主窗口（docs/群音视频通话方案.md 6.1）。自上而下：
 *    - 宫格（balloonui DuiGrid）：每格是一张深色卡片（DuiVBox），开着摄像头的人显示画面（CallVideoCanvas），没开的人显示
 *      头像（DuiAvatar，没有图片时画姓名缩写）；卡片底部写名字与状态（「我」「已静音」「等待接听」「连接中断」）；正在
 *      说话的人卡片描一圈绿边。1 人整幅、2 人左右两格、3~4 人 2×2、5~6 人 2×3、7~9 人 3×3（GroupCallLayout）；
 *    - 头像小条：宫格放不下的人（第 10 人起）排成一行小头像；
 *    - 状态行：正在说话的人或临时提示（如「你已被管理员静音」）；
 *    - 底部按钮：麦克风、摄像头、邀请成员、成员管理（有权限时）、挂断，左侧显示通话时长、右侧显示人数。
 *  全部由 balloonui 现成控件拼成，画面控件复用一对一通话的 CallVideoCanvas，没有新的自绘控件。
 *
 *  窗口不持有通话状态：控制器在房间快照、本人设备状态或说话人变化时调 Update，定时调 RefreshFrames 取画面。按钮经
 *  kMsgGroupCallWindowCommand 投给控制器；关窗等同挂断。
 *
 *  2026-10-05 起（docs/音视频通话优化方案.md 第 5 项）：头像经宿主取真实头像（照片或与主面板相同的字形），开着摄像头但
 *  还没有画面的格子也先显示头像；标题栏加图钉，固定后置顶、不能移动和改大小、不能最小化和最大化，再点一下解除。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（即在 .cpp 里先 #include "stdafx.h"）。
 *
 *  balloonwj@qq.com   2026-10-04
 */
#pragma once

#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   // ProgramWindowIcons（标题栏与任务栏的程序图标）

#include <stdint.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "GroupCallLayout.h"
#include "GroupCallMessages.h"
#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiControl.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../Common/MediaAvatar.h"   // CMediaAvatarSource：格子与小条里的成员头像
#include "../Share/ShareBarPopupGuard.h"   // CShareBarPopupGuard：成员管理菜单开着时窗口被关闭（2026-10-07）

namespace balloonwjui { class DuiAvatar; class DuiButton; class DuiGrid; class DuiHBox; class DuiLabel; class DuiVBox; }
namespace call { class CallVideoCanvas; }

namespace groupcall {

//通话窗取画面的来源（群通话控制器实现，转给媒体会话）
class IGroupCallFrameSource
{
public:
    virtual ~IGroupCallFrameSource()
    {
    }

    /**
     *  取某一路摄像头最近一帧（BGRA，自上而下，行跨度 = 宽 × 4）。只在 UI 线程调用。
     *  @param seat   座位号（self 为 true 时忽略）
     *  @param self   是否取本人摄像头的预览
     *  @param serial [出参] 帧序号
     *  @return 已有画面返回 true
     */
    virtual bool GetTileFrame(int seat, bool self, std::vector<uint8_t>& bgra, int& width, int& height,
                              uint64_t& serial) = 0;
};

//控制器交给通话窗的显示内容
struct GroupCallWindowView
{
    std::wstring                    m_title;          //标题栏文字（MakeGroupCallTitle 的结果）
    std::vector<GroupCallTile>      m_grid;           //宫格里的格子
    std::vector<GroupCallTile>      m_strip;          //头像小条里的人
    std::map<int64_t, std::wstring> m_names;          //userid → 显示名
    std::set<int>                   m_speakingSeats;  //正在说话的座位号
    bool                            m_micOn;          //本人麦克风是否开着
    bool                            m_cameraOn;       //本人摄像头是否开着
    bool                            m_canManage;      //本人能否静音、移出别人（发起人、群主、管理员），决定「成员管理」按钮显隐
    bool                            m_allMuted;       //房间是否处于全员静音：成员管理菜单里「全员静音」据此打勾
    bool                            m_micLocked;      //本人麦克风是否被全员静音锁住（没有管理权）：为 true 时麦克风按钮不可用
    int                             m_joinedCount;    //已加入人数
    std::wstring                    m_status;         //状态行文字（正在说话的人或临时提示）；为空时不显示

    GroupCallWindowView();
};

//群通话的通话窗
class CGroupCallWindow : public balloonwjui::DuiFrameWindow
{
public:
    CGroupCallWindow();
    ~CGroupCallWindow();

    /**
     *  登记宿主：成员头像经它取得。群通话控制器在 Init 时调用。
     *  @param host 宿主（借用，不拥有；须比本对象的使用期长）；为 NULL 时头像画问号
     */
    void SetHost(flamingomedia::IMediaHost* host);

    /**
     *  打开通话窗（已打开时前置并刷新内容）。窗口在主窗口所在显示器的工作区里居中。
     *  @param owner     居中参照的窗口（主面板）；可为 NULL
     *  @param notifyWnd 按钮命令的接收窗口（群通话控制器的消息窗口），不拥有
     *  @param source    画面来源，不拥有，须在 Close 之前一直有效
     *  @param view      显示内容
     */
    void Open(HWND owner, HWND notifyWnd, IGroupCallFrameSource* source, const GroupCallWindowView& view);

    /**
     *  刷新显示内容：宫格结构（人、摄像头开关）变了就重建宫格，否则只改名字、状态、说话描边与按钮文字。
     *  窗口没有打开时什么都不做。
     */
    void Update(const GroupCallWindowView& view);

    //刷新底部的通话时长文字
    void SetDuration(const std::wstring& text);

    //从画面来源取各路最新画面，有新画面的格子重画（控制器的定时器上调用，约每秒 15 次）
    void RefreshFrames();

    //关闭通话窗，不发任何命令；可重复调用
    void Close();

    //有用户的头像变了：宫格与小条里没开摄像头的人重新取头像。窗口没开时什么也不做
    void RefreshAvatars();

    //通话窗是否开着
    bool IsShowing() const;

    //通话窗是否最小化（最小化时控制器不再订阅画面）
    bool IsMinimizedNow() const;

    /**
     *  取出最近一次「成员管理」菜单选中的目标 userid（控制器收到静音某人、移出某人命令时调用），取出后清零。
     *  @return 目标 userid；没有时为 0
     */
    int64_t TakePendingTarget();

    BEGIN_MSG_MAP(CGroupCallWindow)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(kMsgGroupCallWindowMenu, OnManageMenuMsg)
        //固定期间拦下移动、改大小、最小化、最大化（须排在 CHAIN_MSG_MAP 之前，见 Common/WindowPin.h）
        MESSAGE_HANDLER(WM_SYSCOMMAND, OnSysCommandMsg)
        MESSAGE_HANDLER(WM_WINDOWPOSCHANGING, OnWindowPosChangingMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //控件 id
    enum
    {
        kIdMic     = 20,   //麦克风按钮
        kIdCamera  = 21,   //摄像头按钮
        kIdInvite  = 22,   //邀请成员按钮
        kIdManage  = 23,   //成员管理按钮（第 6 期）
        kIdHangup  = 24,   //挂断按钮
    };

    //宫格里一格的控件与画面状态
    struct TileRefs
    {
        GroupCallTile           m_tile;         //这一格对应的人
        balloonwjui::DuiVBox*   m_card;         //卡片（归控件树所有）
        call::CallVideoCanvas*  m_canvas;       //画面控件；没开摄像头时为 NULL
        balloonwjui::DuiAvatar* m_avatar;       //头像控件；开着摄像头时为 NULL（头像画在画面控件里）
        balloonwjui::DuiLabel*  m_nameLabel;    //名字与状态
        uint64_t                m_lastSerial;   //最近一次显示的帧序号

        TileRefs();
    };

    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnManageMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //WM_SYSCOMMAND：固定期间拦下移动、改大小、最小化、最大化、还原，其余交给基类
    LRESULT OnSysCommandMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //WM_WINDOWPOSCHANGING：固定期间保住窗口位置与大小（Aero Snap 等不经 WM_SYSCOMMAND 的路径），其余交给基类
    LRESULT OnWindowPosChangingMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //在标题栏关闭按钮左侧装 / 更新图钉图标，按 m_pinned 取未固定 / 已固定的图标与提示
    void EnsurePinCaptionIcon();
    //切换固定态（点图钉时调用）：固定时置顶、不能拖动和改大小、隐藏最小化与最大化按钮，解除时恢复
    void TogglePinned();
    //复用一个在菜单弹出期间被关闭、尚未销毁的窗口时调用：解除固定、移除旧图钉再重新装上，与新建的窗口一致
    void ResetPinForReuse();

    //建一次窗口的骨架（宫格容器、头像小条、状态行、底部按钮）
    void BuildSkeleton();
    //按 m_view 重建宫格
    void RebuildGrid();
    //按 m_view 重建头像小条
    void RebuildStrip();
    //按 m_view 改名字、状态、说话描边与按钮文字（不重建控件）
    void ApplyTexts();
    //某一格名字行的文字：名字加状态
    std::wstring TileCaption(const GroupCallTile& tile) const;
    //显示名；查不到时写「成员」
    std::wstring NameOf(int64_t userId) const;
    //把按钮命令投给控制器
    void Send(int command);
    //显隐变化后对父容器重排（开发约定文档「UI 约定」第 5 条）
    static void RelayoutParent(balloonwjui::DuiControl* ctrl);

    HWND                            m_notifyWnd;      //命令的接收窗口（不拥有）
    IGroupCallFrameSource*          m_source;         //画面来源（不拥有）
    GroupCallWindowView             m_view;           //当前显示的内容
    bool                            m_built;          //骨架是否已建好
    balloonwjui::DuiVBox*           m_gridHolder;     //宫格的父容器（归控件树所有）
    balloonwjui::DuiGrid*           m_grid;           //宫格（归控件树所有）；重建时换新
    balloonwjui::DuiHBox*           m_strip;          //头像小条（归控件树所有）
    balloonwjui::DuiLabel*          m_statusLabel;    //状态行
    balloonwjui::DuiLabel*          m_durationLabel;  //通话时长
    balloonwjui::DuiLabel*          m_countLabel;     //人数
    balloonwjui::DuiButton*         m_micBtn;         //麦克风按钮
    balloonwjui::DuiButton*         m_cameraBtn;      //摄像头按钮
    balloonwjui::DuiButton*         m_manageBtn;      //成员管理按钮
    std::vector<TileRefs>           m_tiles;          //宫格里各格的控件，顺序同 m_view.m_grid
    std::vector<int64_t>            m_menuTargets;    //成员管理菜单里各成员的 userid，下标即菜单项序号
    int64_t                         m_pendingTarget;  //最近一次菜单选中的目标；控制器取走后清零
    std::vector<uint8_t>            m_frameBuf;       //取画面用的缓冲（只在 UI 线程使用）
    balloonmain::ProgramWindowIcons m_icons;          //标题栏与任务栏的程序图标，本对象持有；窗口先于它销毁
    std::vector<balloonwjui::DuiAvatar*> m_stripAvatars; //头像小条里的头像控件（归控件树所有），顺序同 m_view.m_strip
    bool                            m_pinned;         //是否已固定（置顶，不能移动、改大小、最小化、最大化）；新建窗口时为 false
    int                             m_pinIconId;      //标题栏图钉的 caption icon id（AddCaptionIcon 返回）；0 表示未装
    //成员管理菜单的弹出状态（2026-10-07）：菜单开着时通话结束、控制器调 Close，先关菜单、只隐藏窗口，等 TrackPopup 返回后
    //再销毁；期间又调 Open 时直接复用这个窗口。原因同屏幕共享控制条（docs/屏幕共享与录制方案.md 13.1，用例 MMD1、MMD3）
    screenshare::CShareBarPopupGuard m_popup;
    HBITMAP                         m_pinIconNormal;  //未固定时的图钉位图（本对象持有，析构时 DeleteObject）
    HBITMAP                         m_pinIconActive;  //已固定时的图钉位图（本对象持有，析构时 DeleteObject）
    //成员头像的位图（归它所有）。声明在全部控件指针之后：析构函数里先关窗，借用位图的控件都已不存在
    flamingomedia::CMediaAvatarSource m_avatars;
};

} // namespace groupcall
