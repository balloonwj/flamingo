/**
 *  帖子群窗口 CThreadWindow（docs/帖子群方案.md 4.6，布局以第 0 期样稿 tmp/threadgroup/preview.html 为准）。
 *
 *  布局：
 *    - 标题栏：程序图标（UI 约定第 10 条）、群名；
 *    - 头部：群头像（字形）、群名与「帖子群」标签、成员数；右侧「成员」（滑出群成员面板）、「群资料」（经宿主打开现有的
 *      群资料窗口）与「群管理」（只有群主与管理员有，经宿主打开现有的群管理窗口，docs/帖子群方案.md §15.1 第 3 条）；
 *    - 左栏（380 宽）：「发帖」按钮、「最新动态 / 最新发布」排序切换、帖子卡片列表；本人不能发言时「发帖」置灰并在列表上方说明原因；
 *    - 右栏：选中帖子的详情、楼主补充与留言区，底部是留言框。
 *  第 2b-1 步只搭出窗口、头部与两栏的骨架，帖子列表与详情在之后几步填进来。
 *
 *  生命周期：模块经工厂新建本对象，Open 时才创建窗口；用户关闭窗口时销毁 HWND 并通知模块（OnWindowClosed），对象由模块稍后
 *  释放；宿主关闭（退群、退出登录）时模块调 Close，此时不再通知模块。窗口激活、失去激活、最小化、还原时把「正在看」的变化告诉模块。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   //ProgramWindowIcons：标题栏与任务栏的程序图标
#include "../Host/ThreadWindowApi.h"
#include "../Core/ThreadSession.h"
#include "ThreadBitmapCache.h"
#include "ThreadComposeWindow.h"
#include "ThreadDetailPane.h"   // DetailActionSlot
#include "ThreadFacePicker.h"
#include "ThreadMemberPicker.h"
#include "ThreadPostCard.h"

namespace balloonwjui {
class DuiAvatar;
class DuiButton;
class DuiControl;
class DuiHBox;
class DuiLabel;
class DuiToast;
class DuiVBox;
}

namespace threadgroup {

class CThreadFeedList;
class CThreadDetailPane;
class CThreadComposer;
struct DetailViewData;

//窗口投给自己、留到当前控件通知或绘制处理完再做的事（开发约定文档的 WM_APP 占用表登记为 0x182）：
//wParam 为 ThreadWindowDeferred，lParam 随动作而定
const UINT kMsgThreadWindowDeferred = WM_APP + 0x182;

//kMsgThreadWindowDeferred 的 wParam
enum ThreadWindowDeferred
{
    thread_defer_select_post  = 1,  //单击了左栏的一张卡片：lParam 为卡片序号，选中那条帖子
    thread_defer_load_more    = 2,  //左栏滚到了接近底部：拉下一页
    thread_defer_refresh_feed = 3,  //有缩略图下载完成：重建一次左栏与右栏（几张图先后到达时只重建一次）
    thread_defer_relayout     = 4,  //窗口大小变了：右栏的详情重新排版（富文本的高度随宽度变化）
    thread_defer_detail_click = 5,  //单击了右栏留言区的按钮：lParam 为控件 id（动作会重建详情，按钮不能在自己的通知里被销毁）
    thread_defer_send_comment = 6,  //留言框按了 Enter 或点了「发送」：发出留言或回复
    thread_defer_detail_button= 7,  //单击了详情里的「编辑」「置顶」「删除」、补充的「删除」或留言框的「取消」：lParam 为控件 id
    thread_defer_face_picked  = 8,  //表情浮层里选中了一个表情：lParam 为表情编号，用它回应「＋」所属的目标
    thread_defer_comment_mention = 9, //留言框的成员浮层里选中了一个人：lParam 为堆上的 int64_t 用户 ID，处理后 delete
    thread_defer_member_menu  = 10, //点了详情里正文、补充、留言或回复中的 @某人（左键或右键）：lParam 为堆上的
                                    //ThreadMemberMenuRequest，处理后 delete；等富文本的鼠标事件处理完再弹成员菜单（UI 约定第 13 条）
};

//thread_defer_member_menu 的负载：点了谁、在哪里点的
struct ThreadMemberMenuRequest
{
    int64_t m_userId;   //被 @ 的人
    POINT   m_screen;   //点击位置（屏幕坐标），成员菜单的锚点

    ThreadMemberMenuRequest()
        : m_userId(0)
    {
        m_screen.x = 0;
        m_screen.y = 0;
    }
};

/**
 *  CThreadWindow —— 一个帖子群的窗口（非模态顶层窗口）。只在界面线程上使用。
 */
class CThreadWindow : public balloonwjui::DuiFrameWindow, public IThreadWindow, public IThreadSessionView
{
public:
    /**
     *  @param groupId 帖子群
     *  @param session 会话控制器（借用，模块保证比本对象存活久）
     *  @param owner   模块（借用）
     */
    CThreadWindow(int64_t groupId, CThreadSession* session, IThreadWindowOwner* owner);
    virtual ~CThreadWindow();

    //======== IThreadWindow（语义见 Host/ThreadWindowApi.h） ========
    virtual bool Open(int64_t postId) override;
    virtual void Close() override;
    virtual bool IsWatching() const override;
    virtual IThreadSessionView* View() override;
    virtual void OnGroupChanged() override;
    virtual void OnGroupMembers(const std::vector<ThreadMember>& members) override;
    virtual void OnMemberPanelClosed() override;
    virtual void OnImageUploaded(uint32_t requestId, bool ok, const ThreadImage& image, const std::wstring& localPath) override;
    virtual void OnImageReady(uint32_t requestId, bool ok, const std::wstring& localPath) override;
    virtual void OnAvatarsUpdated() override;

    //======== IThreadSessionView（语义见 Core/ThreadSession.h） ========
    virtual void OnFeedChanged() override;
    virtual void OnDetailChanged() override;
    virtual void OnActionDone(int cmd, int code, int64_t id) override;
    virtual void OnReactorsLoaded(int targetType, int64_t targetId, int reaction, const std::vector<int64_t>& users,
                                  int total) override;

    //以下供测试使用
    //头部显示的群名
    std::wstring GetHeaderName() const;
    //头部显示的成员数文字
    std::wstring GetHeaderSub() const;
    //「发帖」按钮是否可用
    bool IsPublishEnabled() const;
    //当前排序（ThreadSort）
    int GetSort() const;
    //左栏列出的卡片张数
    int GetCardCount() const;

    BEGIN_MSG_MAP(CThreadWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroyMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivateMsg)
        MESSAGE_HANDLER(WM_SIZE, OnSizeMsg)
        MESSAGE_HANDLER(kMsgThreadWindowDeferred, OnDeferredMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //WM_CLOSE：点标题栏关闭按钮或按 Alt+F4 时销毁窗口
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DESTROY：用户关闭时通知模块（宿主关闭时不通知），不拦截消息
    LRESULT OnDestroyMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DUI_NOTIFY：头部与左栏的按钮
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_ACTIVATE：激活与失去激活时更新「正在看」，不拦截消息
    LRESULT OnActivateMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_SIZE：最小化与还原时更新「正在看」，不拦截消息
    LRESULT OnSizeMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //kMsgThreadWindowDeferred：执行投给自己的动作（选中帖子、拉下一页）
    LRESULT OnDeferredMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //左栏滚到接近底部时 CThreadFeedList 的回调：只投递消息，等绘制结束再拉下一页
    static void NearBottomThunk(void* user);

    //按会话里的帖子列表重建左栏：没加载完或没有帖子时显示提示文字，否则显示卡片列表
    void RebuildFeed();

    //准备一张卡片的内容：作者、头像、时间、截好的正文、缩略图、互动标签、留言数
    PostCardData BuildCardData(HDC hdc, const ThreadPost& post, int innerWidth);

    //表情编号对应的表情图片路径；表情库里没有时为空串
    std::wstring FacePath(int faceId);

    /**
     *  一张图片的缩略图位图：已下载到本机时读成位图；还没下载时向宿主请求下载（同一张图只请求一次），先返回 NULL 画占位底色。
     *  @param image 图片
     *  @param sizePx 位图边长，单位：像素
     *  @return 位图（归 m_bitmaps）；还没下载或下载失败时为 NULL
     */
    HBITMAP ThumbBitmap(const ThreadImage& image, int sizePx);

    //有缩略图下载完成：投一条消息，等这次消息处理完再重建左栏（已经投过、还没处理时不再投）
    void PostFeedRefresh();

    //按会话里的帖子详情重建右栏：没有选中帖子时显示提示文字，否则显示详情
    void RebuildDetail();

    //准备右栏要显示的内容：作者、权限、正文、图片、互动与补充
    void BuildDetailData(DetailViewData& data);

    //作者的显示名与头像（经宿主；照片在本机时读成位图）
    void AuthorInfo(int64_t userId, int avatarPx, std::wstring& name, std::wstring& glyph, COLORREF& color, HBITMAP& bitmap);

    /**
     *  互动汇总换成标签（「赞」在最前，表情读成小图标）。
     *  @param ensureLike 没人点过赞时也放一个「赞」标签（详情里它就是点赞按钮）
     */
    std::vector<PostCardChip> BuildChips(const std::vector<ThreadReaction>& reactions, bool ensureLike);

    //一个目标当前的互动汇总（帖子或留言 / 回复）；找不到时返回 NULL
    const std::vector<ThreadReaction>* ReactionsOf(int targetType, int64_t targetId);

    //切换本人对一个目标的一种互动：没加过就加上，加过就取消（点击立即更新，失败时会话恢复原状）
    void ToggleReaction(int targetType, int64_t targetId, int reaction);

    //表情浮层的选中回调（user 为本窗口）：投递消息，等浮层的点击处理完再回应
    static void FacePickedThunk(void* user, int faceId);

    /**
     *  留言框里刚输入了一个用来 @ 人的「@」：在光标处弹出成员浮层；还没有成员名单时先向宿主请求（docs/帖子群方案.md
     *  14.1 的 P6）。
     *  @param atPos       那个「@」在输入框里的字符位置（选中后替换它）
     *  @param caretScreen 光标那一行底边的屏幕坐标
     */
    void OpenCommentMemberPicker(int atPos, const POINT& caretScreen);

    //留言框成员浮层的选中回调（user 为本窗口）：投递消息，等浮层的通知处理完再插入
    static void CommentMemberPickedThunk(void* user, int64_t userId);

    //详情里点了 @某人的回调（user 为本窗口，docs/帖子群方案.md §15.1 第 4 条）：投递 thread_defer_member_menu，
    //等富文本的鼠标事件处理完再经宿主弹成员菜单（菜单里的操作可能让详情重建）
    static void AtClickThunk(void* user, int64_t userId, POINT screenPt);

    //成员名单还没拉到、也没有在拉时向宿主请求一次（结果经 OnGroupMembers 回来）
    void EnsureMembersRequested();

    //留言框成员浮层的条目：去掉本人，群主、管理员在前
    std::vector<MemberPickEntry> CommentMemberEntries() const;

    //「成员」：群成员面板显示着就收起，否则请宿主在窗口右侧显示（docs/帖子群方案.md 14.3）
    void ToggleMemberPanel();

    //群成员面板此刻应在的位置（客户区坐标）
    RECT MemberPanelRect() const;

    //鼠标进入一个互动标签：记下它，向服务端拉这种互动的名单（结果到了在 OnReactorsLoaded 里显示提示）
    void OnChipEnter(balloonwjui::DuiControl* chip, UINT ctrlId);

    //鼠标离开互动标签，或标签要随详情重建而销毁：关掉提示并撤销登记
    void ClearChipHover();

    //单击了详情里的第 index 张图：已下载的几张交给宿主的看图窗口，从被点的那张开始
    void OpenDetailImage(size_t index);

    //一个用户的显示名（经宿主）
    std::wstring UserName(int64_t userId);

    //执行右栏留言区按钮的动作（延后到点击通知处理完之后）：拉更多回复、加载更多留言、回复、删除
    void HandleDetailClick(UINT ctrlId);

    //执行详情里「编辑」「置顶」「删除」、补充的「删除」与留言框「取消」的动作（延后到点击通知处理完之后）
    void HandleDetailButton(UINT ctrlId);

    //发出留言框里的留言或回复：先本地校验，不合法时在留言框里说明原因
    void SendComment();

    /**
     *  删除前弹确认框。
     *  @param text 确认的问题
     *  @return 用户点了「确定」返回 true
     */
    bool ConfirmDelete(const std::wstring& text);

    //在窗口顶部弹一条浮层提示，过一会儿自动淡出
    void ShowToast(const std::wstring& text, bool isError);

    /**
     *  打开发帖窗口。
     *  @param mode 模式，取值见 ComposeMode：发新帖；编辑或追加补充时针对右栏当前的帖子
     */
    void OpenCompose(int mode);

    //留言框的显隐与高度跟着详情走：选中了帖子（且没被删除）才显示；「回复」提示行显隐后高度跟着变
    void UpdateComposer();

    //建窗口后取正文字体、建标题字体（加粗）；窗口销毁后释放标题字体
    void CreateFonts();
    void DestroyFonts();

    //构建客户区控件树。每次创建窗口后调用一次
    void BuildContent();

    //建头部：群头像、群名与「帖子群」标签、成员数、「成员」「群资料」「群管理」
    std::unique_ptr<balloonwjui::DuiHBox> BuildHeader();

    //建左栏：「发帖」、排序切换、不能发言的说明、帖子列表区
    std::unique_ptr<balloonwjui::DuiVBox> BuildLeftColumn();

    //建右栏：详情区（第 2c 步起填内容）
    std::unique_ptr<balloonwjui::DuiVBox> BuildRightColumn();

    //按宿主给的群概况刷新标题、头部与「发帖」是否可用
    void RefreshGroupBrief();

    //刷新两个排序按钮的样式（选中的为主色实心）
    void RefreshSortButtons();

    //按控件当前的字体定各按钮与「帖子群」标签的宽度（正好放下文字）；控件挂到窗口上之后才取得到窗口 DPI 下的字体
    void UpdateWidths();

    //按本人角色显示或隐藏头部的「群管理」（CanManageGroup）；显隐变了时对头部那一行强制重排（UI 约定第 5 条）
    void ApplyGroupManageButton();

    //「正在看」可能变了：与上次告诉模块的值不同时通知模块
    void NotifyWatchChanged();

    //在 owner 所在显示器（没有时在鼠标所在显示器）的工作区里按默认大小居中摆放窗口
    void CenterOnWorkArea();

private:
    int64_t              m_groupId;       //帖子群
    CThreadSession*      m_session;       //会话控制器（借用）
    IThreadWindowOwner*  m_owner;         //模块（借用）
    bool                 m_closingByHost; //正在由宿主关闭（Close）：此时销毁窗口不通知模块
    bool                 m_watching;      //最近一次告诉模块的「正在看」状态
    bool                 m_canSpeak;      //本人此刻能否发帖、留言（RefreshGroupBrief 更新）
    std::wstring         m_groupName;     //群名（宿主给的，取不到时为空串）
    balloonmain::ProgramWindowIcons m_programIcons;   //标题栏与任务栏的程序图标，本对象持有，跨关闭再打开复用
    CThreadBitmapCache   m_bitmaps;       //头像照片、表情、缩略图的位图（卡片里的控件借用），窗口销毁后释放
    HFONT                m_bodyFont;      //卡片正文与名字的字体（借用 balloonui 按窗口 DPI 给的默认字体）；窗口创建后有效
    HFONT                m_titleFont;     //卡片标题与名字的字体（本对象创建的加粗字体），窗口销毁后释放
    HFONT                m_detailTitleFont;//详情标题的字体（本对象创建的加大加粗字体），窗口销毁后释放
    int                  m_myRole;        //本人在群里的角色（ThreadGroupRole，RefreshGroupBrief 更新）；不知道时按普通成员
    int                  m_atAllScope;    //群设置「谁可以 @ 全体」（ThreadAtAllScope，RefreshGroupBrief 更新）：留言框的 @ 列表
                                          //据此决定有没有「所有人」（docs/帖子群方案.md §15.1 第 1 条）
    bool                 m_loadMorePosted;//已投递「拉下一页」的消息、还没处理（避免每次绘制都投一条）
    bool                 m_refreshPosted; //已投递「重建左栏」的消息、还没处理（几张缩略图先后到达时合并成一次）
    std::map<std::string, std::wstring> m_imagePaths;    //已有结果的图片：fileid → 本机路径（下载失败为空串，不再重试）
    std::map<uint32_t, std::string>     m_imageRequests; //正在下载的图片：请求号 → fileid

    //以下控件归窗口的控件树所有，窗口创建后有效、销毁后失效
    balloonwjui::DuiHBox*    m_headerRow;      //头部那一行（右侧按钮宽度或显隐变化时对它改 hint、重排）
    balloonwjui::DuiHBox*    m_headerNameRow;  //头部群名与「帖子群」标签所在的行（标签宽度变化时对它改 hint）
    balloonwjui::DuiVBox*    m_headerTag;      //头部「帖子群」标签
    balloonwjui::DuiAvatar*  m_headerAvatar;   //头部的群头像（群名首字的字形头像）
    balloonwjui::DuiLabel*   m_headerName;     //头部的群名
    balloonwjui::DuiLabel*   m_headerSub;      //头部群名下面的成员数
    balloonwjui::DuiButton*  m_btnMembers;     //头部「成员」
    balloonwjui::DuiButton*  m_btnGroupInfo;   //头部「群资料」：所有成员都有
    balloonwjui::DuiButton*  m_btnGroupManage; //头部「群管理」：只有群主与管理员看得到（ApplyGroupManageButton）
    balloonwjui::DuiButton*  m_btnPublish;     //左栏「发帖」
    balloonwjui::DuiButton*  m_btnSortActivity;//左栏排序「最新动态」
    balloonwjui::DuiButton*  m_btnSortPublish; //左栏排序「最新发布」
    balloonwjui::DuiHBox*    m_leftToolbar;    //左栏顶部那一行（按钮宽度变化时对它改 hint）
    balloonwjui::DuiVBox*    m_leftColumn;     //左栏（「不能发言」说明行显隐后对它重排，UI 约定第 5 条）
    balloonwjui::DuiVBox*    m_muteBar;        //左栏「不能发言」的说明行
    balloonwjui::DuiLabel*   m_muteText;       //说明行里的文字
    balloonwjui::DuiLabel*   m_feedHint;       //帖子列表区的提示文字：正在加载、还没有帖子（有帖子时隐藏）
    CThreadFeedList*         m_feedList;       //帖子卡片列表（没有帖子时隐藏）
    balloonwjui::DuiLabel*   m_detailHint;     //右栏的提示文字：没有选中帖子时显示
    balloonwjui::DuiVBox*    m_rightColumn;    //右栏（提示文字与详情显隐互换后对它重排）
    CThreadDetailPane*       m_detailPane;     //右栏的帖子详情：选中帖子时显示
    CThreadComposer*         m_composer;       //右栏底部的留言框：选中帖子时显示
    balloonwjui::DuiControl* m_composerLine;   //留言框上方的分隔线（与留言框一起显隐）
    balloonwjui::DuiToast*   m_toast;          //窗口顶部的浮层提示（操作失败的原因）
    bool                     m_commentSending; //有一条留言或回复正在发送（应答回来之前不再发）
    CThreadFacePicker        m_facePicker;     //表情选择浮层（「＋」打开），本对象持有
    int                      m_pickTargetType; //表情浮层是为哪个目标打开的：目标类型（ThreadTargetType）
    int64_t                  m_pickTargetId;   //表情浮层是为哪个目标打开的：目标 ID
    RECT                     m_lastClickRect;  //最近一次单击的详情按钮的屏幕矩形（「＋」据此定表情浮层的位置）
    balloonwjui::DuiControl* m_hoverChip;      //鼠标正停着的互动标签（详情重建前清掉）；没有为 NULL
    DetailActionSlot         m_hoverSlot;      //m_hoverChip 登记的内容（目标与互动编号）
    CThreadComposeWindow     m_compose;        //发帖窗口（发帖、编辑帖子、追加补充共用），本对象持有，用到时才建窗口
    CThreadMemberBook        m_members;        //群成员名单（留言框的 @ 用）；群资料变化时作废，下次用到时重拉
    bool                     m_membersPending; //已向宿主请求成员名单、还没有结果
    CThreadMemberPicker      m_commentPicker;  //留言框的成员浮层，本对象持有
    int                      m_commentAtPos;   //成员浮层是为留言框里哪个位置的「@」打开的；没有为 -1
    bool                     m_memberPanelShown; //群成员面板是否显示着（面板在 exe 里，宿主经 OnMemberPanelClosed 告知被用户关掉）
};

} // namespace threadgroup
