/**
 *  「文件共享」窗口，ShareCenterWindow.h
 *
 *  系统菜单「文件共享」的落点（docs/文件共享方案.md 7.4「共享窗口」）。第 1 期第 2 步先做成占位窗口验证第二份 balloonui，
 *  第 17 步做成正式窗口：
 *    - 顶部两个页签「我的共享」「共享给我的」与「刷新」按钮；打开窗口、点刷新、窗口重新被激活、取消共享成功之后都会重新拉取
 *      两份列表（2205，不按会话筛选）；
 *    - 左侧列表每行两行字：名称；「我的共享」为显示路径 · 所在会话 · 成员数 · 状态（位于另一台电脑、本机缺少记录的加标注），
 *      「共享给我的」为某某的共享 · 所在会话 · 我的权限 · 状态；
 *    - 右侧详情：选中一行时显示名称、操作按钮（在本机打开 / 打开、调整成员、取消共享、定位到会话，按 ShareCenterModel 显隐），
 *      「我的共享」再列出成员与权限、正在访问的人（每两秒刷新一次）、最近操作记录；「共享给我的」列出分享者、我的权限与状态。
 *  调整成员、打开浏览窗口经 IShareUiHost 交给模块；取消共享前确认，经 CShareService::CancelShare 发 2202；定位到会话经宿主
 *  OpenConversation；所在会话的名字与成员、访问者的显示名经宿主取得。
 *
 *  控件全部用 balloonui 现成的：列表是 DuiVirtualList 加画行回调，详情是 DuiScrollView 装若干 DuiLabel，操作按钮是
 *  DuiButton。操作按钮显隐之后对按钮所在的行重排（UI 约定第 5 条）。
 *  外观（2026-10-03 按用户要求美化，见 docs/文件共享方案.md 7.4）：窗口浅灰底，左侧列表与右侧详情各放在一张白色圆角卡片里
 *  （DuiVBox 的底色与圆角）；顶栏左边是两个胶囊页签（选中的品牌蓝实心、另一个白底灰边，文字带共享个数），右边是搜索框、
 *  品牌蓝实心的「发起共享」与白底灰边的「刷新」，按钮宽度按字体量出。
 *  详情里的成员、正在访问的人与「共享给我的」的分享者（2026-10-03 起）每人一行：DuiAvatar 头像（宿主给的，与主面板联系人列表
 *  同一口径，见 ShareAvatar.h），名字在上，权限、正在做什么或「分享者」在下。
 *
 *  同一时刻只存在一个实例（由 CFileShareModule 持有）。已打开时再打开，只把它前置。关闭即销毁 HWND，对象本身保留。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法：
 *      CShareCenterWindow window;
 *      window.Open(ownerHwnd, &service, host, uiHost);   // 显示或前置
 *      window.Close();                                   // 销毁窗口（退出登录、模块关闭时）
 *
 *  balloonwj@qq.com   2026-10-02（第 17 步 2026-10-03 改为正式窗口）
 */
#pragma once

#include <stdint.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   //ProgramWindowIcons：标题栏与任务栏的程序图标
#include "../FileShareApi.h"
#include "../Engine/ShareService.h"   //IShareListObserver / IShareManageObserver
#include "ShareUiHost.h"
#include "ShareAvatar.h"   //CShareAvatarSource：详情里成员、访问者、分享者的头像

namespace balloonwjui {
class DuiHBox;
class DuiLabel;
class DuiButton;
class DuiVBox;
class DuiScrollView;
class DuiVirtualList;
class DuiSearchBox;
class DuiAvatar;
class DuiToast;
}

namespace fileshare {

//「文件共享」窗口的两个页签
enum share_center_tab
{
    share_center_tab_mine   = 0,   //我的共享
    share_center_tab_shared = 1,   //共享给我的
};

/**
 *  CShareCenterWindow —— 「文件共享」窗口（非模态顶层窗口）。只在 UI 线程使用。
 */
class CShareCenterWindow : public balloonwjui::DuiFrameWindow, public IShareListObserver, public IShareManageObserver
{
public:
    CShareCenterWindow();
    ~CShareCenterWindow();

    /**
     *  显示窗口并拉取两份列表；已显示时只前置并重新拉取。
     *  @param owner   用来居中对齐的参照窗口，可为 NULL；不转移所有权
     *  @param service 业务总控（不拥有，须在本窗口关闭之前一直有效），不可为 NULL
     *  @param host    宿主（取显示名与会话名、定位到会话；不拥有），不可为 NULL
     *  @param ui      打开其它窗口的入口（模块，不拥有），不可为 NULL
     *  @return 窗口已显示返回 true；参数非法或创建失败返回 false（已写日志）
     */
    bool Open(HWND owner, CShareService* service, IFileShareHost* host, IShareUiHost* ui);

    /**
     *  销毁窗口：不再接收列表与取消的结果。窗口未创建时调用是安全的。
     */
    void Close();

    //IShareListObserver：一份列表到达
    virtual void OnShareListResult(uint32_t requestId, int scope, int code, const std::vector<ShareListEntry>& shares) override;

    //IShareManageObserver：取消共享的结果
    virtual void OnShareManageResult(const ShareManageResult& result) override;

    /**
     *  切换页签（界面上点页签时调用，测试直接调用）。
     *  @param tab share_center_tab 之一
     */
    void SetTab(int tab);

    /**
     *  选中当前页签里的一行（界面上单击时调用，测试直接调用），刷新右侧详情。
     *  @param row 行号；越界时清除选择
     */
    void SelectRow(int row);

    /**
     *  对选中的行执行一项操作（按钮点击时调用，测试直接调用）。
     *  @param action    share_center_action 之一
     *  @param confirmed 取消共享时为真表示不再弹确认框（视为已确认）；按钮传假
     *  @return 已执行（或已发出取消请求）返回 true；没有选中行、该行不能做这项操作、用户在确认框里放弃返回 false
     */
    bool DoAction(int action, bool confirmed);

    /**
     *  「发起共享」（按钮点击时调用，测试直接调用）。窗口是从聊天窗打开的（记住了会话，见 SetLaunchConversation）时，直接打开
     *  那个会话的发起共享对话框，不弹选人窗口、不建群，发起成功后前置那个聊天窗；否则经 IShareUiHost 请宿主弹出选人窗口，
     *  选好的人随后进入发起共享对话框，弹不出选人窗口时弹出浮层提示说明原因。
     *  @return 发起共享对话框或选人窗口已弹出返回 true
     */
    bool StartDirectShare();

    /**
     *  记住打开本窗口的会话（2026-10-03 起）：从聊天窗浮层的「管理全部共享」打开时为那个会话，从系统菜单、主面板打开时
     *  传 0 清除。窗口已打开时同样生效，以最近一次为准。
     *  @param ctype 会话类型：1 单聊 2 群聊；cid 为 0 时忽略
     *  @param cid   会话 id（本人视角）；0 表示不属于任何会话
     */
    void SetLaunchConversation(int ctype, int64_t cid);

    //记住的会话 id；没有时为 0（测试用）
    int64_t GetLaunchCid() const;

    /**
     *  按关键字过滤当前页签的列表（搜索框文字变化时调用，测试直接调用）：名称、路径、所在会话、分享者任何一项包含关键字的
     *  共享才列出（见 ShareCenterModel::MatchesFilter）。选中的共享仍在结果里时保持选中，否则清除选择。
     *  @param keyword 关键字；空串列出全部
     */
    void SetFilter(const std::wstring& keyword);

    //当前列出的行数（过滤之后，测试用）
    size_t GetVisibleCount() const;

    //重新拉取两份列表
    void Refresh();

    /**
     *  切到「我的共享」、清空搜索关键字并选中一个共享（第 4 期起，分享者操作通知里的「查看」）。列表里已有它时立即选中，
     *  列表还在加载（刚打开窗口）时等「我的共享」列表到达后选中；列表里没有它时只切换页签。
     *  @param shareId 共享 id，大于 0
     */
    void FocusShare(int64_t shareId);

    /**
     *  宿主通知有用户的头像变了（2026-10-03 起）：窗口打开着时重建右侧详情，详情里成员、访问者、分享者的头像重新向宿主取。
     *  窗口没打开时不做任何事。
     */
    void RefreshAvatars();

    //以下供测试使用
    int GetTab() const;
    size_t GetRowCount(int tab) const;
    const ShareListEntry& GetRow(int tab, size_t row) const;
    int GetSelectedRow() const;
    int GetActionMask() const;
    std::vector<std::wstring> GetDetailLines() const;
    //最近一次浮层提示的文字（提示淡出后仍保留）
    std::wstring GetToastText() const;
    //两份列表是否还有没到达的
    bool IsLoading() const;
    //右侧详情的控件已经重建过几次（每次 BuildDetailControls 加 1）
    int GetDetailBuildCount() const;

    BEGIN_MSG_MAP(CShareCenterWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(WM_TIMER, OnTimerMsg)
        MESSAGE_HANDLER(WM_ACTIVATE, OnActivateMsg)
        MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChangedMsg)
        MESSAGE_HANDLER(WM_SIZE, OnSizeMsg)
        //窗口挪动后记下还原态的位置，关窗时保存（2026-10-07 起）
        MESSAGE_HANDLER(WM_MOVE, OnMoveMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //WM_CLOSE：点标题栏关闭按钮或按 Alt+F4 时销毁窗口
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DUI_NOTIFY：页签切换、按钮点击
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_TIMER：「我的共享」页签选中一行时每隔一段时间刷新「正在访问的人」
    LRESULT OnTimerMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_ACTIVATE：窗口重新被激活时重新拉取列表（别的窗口里调整了成员、取消了共享之后回来能看到最新状态）
    LRESULT OnActivateMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DPICHANGED：窗口挪到另一块 DPI 不同的显示器时，按新 DPI 的字体重算顶栏各按钮的宽度，再交给基类按新 DPI 重新布局
    LRESULT OnDpiChangedMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_SIZE：窗口宽度变化时重定详情卡片的宽度（见 UpdateDetailCardWidth），记下还原态的矩形，再交给基类按新尺寸重新布局
    LRESULT OnSizeMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_MOVE：窗口处于还原态时记下当前矩形（见 RememberNormalRect），不拦截消息
    LRESULT OnMoveMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //窗口既没有最小化也没有最大化时，把当前窗口矩形记进 m_lastNormalRect，关窗时据此保存位置与大小
    void RememberNormalRect();

    /**
     *  新建窗口后按上次保存的位置与大小摆放窗口；没有保存过时在 anchor 所在显示器的工作区里按默认大小居中
     *  （2026-10-07 起；此前以 owner 为中心居中，owner 是贴在屏幕右侧的窄长主面板时窗口会被挤到屏幕右缘）。
     *    anchor：确定「当前屏幕」的窗口（打开本窗口的主面板或聊天窗），可为 NULL。
     *  @return 上次关闭时处于最大化、应当以最大化显示时返回 true
     */
    bool ApplySavedPlacement(HWND anchor);

    //关闭窗口前把还原态的位置、大小与是否最大化写进本人个人目录下的 fileshare_ui.ini；从没有处于过还原态时不写
    void SavePlacement();

    //按窗口当前宽度定详情卡片的宽度：不比默认宽度宽时为 m_minDetailWidth，更宽时再加上多出部分的一半
    void UpdateDetailCardWidth();

    //构建客户区控件树。每次创建窗口后调用一次
    void BuildContent();

    /**
     *  按字体定各按钮与详情卡片的宽度：顶栏两个胶囊页签、「发起共享」「刷新」与详情的操作按钮正好放下各自的文字；
     *  详情卡片平时是设计宽度，按钮最多的一组（在本机打开、调整成员、取消共享）放不下时（DPI 很高、字体很大）加宽到正好放下。
     *  窗口尺寸不随 DPI 放大，所以卡片不按 DPI 等比放大，免得窗口拉到最小时左侧列表被挤得太窄。
     *  @param font 按钮文字的字体（不拥有）
     */
    void UpdateButtonWidths(HFONT font);

    //刷新两个胶囊页签：选中的用品牌蓝实心样式，另一个用白底灰边样式；文字带上各自的共享个数（列表到达之前不带）
    void UpdateTabButtons();

    //按当前页签的数据刷新列表行数与选择
    void RefreshList();

    //按当前页签与搜索关键字重算列出的行（m_visible），并按 shareid 保持选择
    void RebuildVisible();

    //列表为空时画在列表中央的提示：正在加载、还没有共享、或没有匹配的结果（列表控件的空列表绘制回调）
    static void PaintEmptyThunk(void* user, HDC hdc, const RECT& listRect);
    void PaintEmpty(HDC hdc, const RECT& listRect);

    //按选中的行重建右侧详情：头部（图标、名称、路径、状态胶囊）、操作按钮与下面的各小节
    void RebuildDetail();

    //刷新详情头部：选中共享的类型图标、名称、路径与状态胶囊；没有选中时只显示提示文字
    void UpdateDetailHeader(const ShareListEntry* e);

    //按 m_detailRows 建出详情滚动区里的控件
    void BuildDetailControls();

    //以下几个追加右侧详情的一行；text 为整行的文字（GetDetailLines 返回它，测试据此核对内容）
    //只在头部显示的一行（路径）：滚动区里不画
    void AddHeaderOnly(const std::wstring& text);
    //两列的信息行：左列 label、右列 value；link 为真时右边带「定位到会话」链接
    void AddField(const std::wstring& label, const std::wstring& value, bool link, const std::wstring& text);
    //小节标题
    void AddSection(const std::wstring& title);
    //小节里没有内容时的弱色提示
    void AddEmpty(const std::wstring& text);
    //人的一行：头像、名字；名字下面是 chips（权限胶囊）或 sub（文字）；progress 为 0~100 时右边画传输进度条，-1 不画
    void AddPerson(int64_t userId, const std::wstring& name, const std::wstring& sub, const std::vector<std::wstring>& chips,
                   int progress, const std::wstring& text);
    //最近操作的一行：左列时间、右列内容，failed 为真时红字
    void AddActivity(const std::wstring& time, const std::wstring& content, bool failed, const std::wstring& text);

    //当前页签里列出的行（按搜索关键字过滤之后），行号与列表控件、m_selected 一致
    const std::vector<ShareListEntry>& Rows() const;

    //所在会话的名字（按会话缓存，宿主取不到时为空串）
    std::wstring ConversationName(int ctype, int64_t cid);

    //一名用户的显示名（经宿主）
    std::wstring UserName(int64_t userId);

    //在本机打开共享的文件夹（文件共享时在资源管理器里选中那个文件）
    bool OpenLocal(int64_t shareId);

    /**
     *  在顶栏下方弹出一条浮层提示，过一会儿自动淡出（2026-10-03 起取代底部提示行）。
     *  @param text    提示文字；为空时立即隐藏正在显示的提示
     *  @param isError 为 true 时红底、多显示一秒，否则深灰底
     */
    void ShowToast(const std::wstring& text, bool isError);

    //DuiVirtualList 的画行回调与单击回调（user 为本窗口）
    static void PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);
    static void RowClickThunk(void* user, int rowIndex, bool isDoubleClick);
    void PaintRow(HDC hdc, int rowIndex, const RECT& rowRect);

private:
    CShareService*              m_service;      //业务总控（不拥有）；窗口打开期间有效
    IFileShareHost*             m_host;         //宿主（不拥有）
    IShareUiHost*               m_ui;           //打开其它窗口的入口（不拥有）
    int                         m_tab;          //当前页签，share_center_tab
    int                         m_selected;     //当前页签里选中的行；没有选中时为 -1
    std::vector<ShareListEntry> m_mine;         //「我的共享」
    std::vector<ShareListEntry> m_shared;       //「共享给我的」
    std::vector<ShareListEntry> m_visible;      //当前页签里按搜索关键字过滤后列出的行（Rows() 返回它，行号与列表控件一致）
    std::wstring                m_filter;       //搜索框里的关键字；空串列出全部
    uint32_t                    m_mineSeq;      //「我的共享」请求编号；已有结果时为 0
    uint32_t                    m_sharedSeq;    //「共享给我的」请求编号；已有结果时为 0
    uint32_t                    m_cancelId;     //正在等待结果的取消请求编号；没有时为 0
    int64_t                     m_lastRefreshMs;//最近一次拉取列表的时间（GetTickCount64），窗口重新激活时避免过于频繁
    bool                        m_modal;        //是否正在显示确认框（期间的激活变化不触发刷新）
    int64_t                     m_focusShareId; //等「我的共享」列表到达后要选中的共享（FocusShare）；没有时为 0
    int                         m_launchCtype;  //打开本窗口的会话类型（从聊天窗打开时）；没有时为 0
    int64_t                     m_launchCid;    //打开本窗口的会话 id（本人视角）；从系统菜单、主面板打开时为 0，「发起共享」先选人
    std::map<std::pair<int, int64_t>, std::wstring> m_convNames;   //所在会话的名字缓存：(会话类型, 会话 id) → 名字
    //右侧详情的一行的种类（2026-10-03 美化起）
    enum DetailRowKind
    {
        detail_row_header   = 0,   //只在详情头部显示的内容（路径）：滚动区里不画，只供 GetDetailLines 读取
        detail_row_field    = 1,   //两列的信息行：左列灰色标签、右列深色值；所在会话一行右边带「定位到会话」链接
        detail_row_section  = 2,   //小节标题（加粗）与下方的细分隔线
        detail_row_empty    = 3,   //小节里没有内容时的弱色提示
        detail_row_person   = 4,   //人的一行（成员、访问者、分享者）：头像、名字，名字下面是权限胶囊或说明文字
        detail_row_activity = 5,   //最近操作的一行：左列时间、右列内容，失败的红字
    };
    //右侧详情的一行
    struct DetailRow
    {
        int          m_kind;       //种类，DetailRowKind
        std::wstring m_text;       //整行的文字（GetDetailLines 返回它，如「类型：文件夹」「张三：上传」）
        std::wstring m_label;      //信息行的标签；最近操作的时间
        std::wstring m_value;      //信息行的值；最近操作的内容（不含时间）
        bool         m_link;       //信息行右边是否带「定位到会话」链接
        bool         m_failed;     //最近操作是否失败（红字）
        int64_t      m_userId;     //人的 userid；其余种类为 0
        std::wstring m_name;       //人的显示名（第一行）
        std::wstring m_sub;        //人的说明文字（第二行，没有胶囊时显示）：正在做什么或「分享者」
        std::vector<std::wstring> m_chips;   //人的权限胶囊（第二行，成员用）；为空时显示 m_sub
        int          m_progress;   //访问者的传输进度（0~100）；没有进度时为 -1

        DetailRow()
            : m_kind(detail_row_empty)
            , m_link(false)
            , m_failed(false)
            , m_userId(0)
            , m_progress(-1)
        {
        }
    };
    std::vector<DetailRow>      m_detailRows;   //右侧详情的各行，按显示顺序；RebuildDetail 时重填
    int64_t                     m_detailShareId; //右侧详情当前显示的共享 id，没有选中时为 0；换了共享时详情滚回顶部，同一个共享定时刷新时保持滚动位置
    int                         m_minDetailWidth; //详情卡片的最小宽度，单位：像素：设计宽度与按钮最多的一组所需宽度中较大的那个；UpdateButtonWidths 计算
    int                         m_detailBuildCount; //BuildDetailControls 执行过的次数，只供测试判断详情有没有重建
    std::wstring                m_liveSignature; //「我的共享」详情里正在访问的人与最近操作的内容摘要；定时刷新时摘要不变就不重建详情；RebuildDetail 更新
    HBITMAP                     m_headerIconBmp; //详情头部的类型图标位图（本对象持有，换图标与关闭窗口时释放）

    //以下控件归窗口的控件树所有，窗口创建后有效、销毁后失效
    balloonwjui::DuiHBox*        m_topBar;       //顶栏（胶囊页签、搜索框与两个按钮所在的行；按钮宽度随字体变化时对它改 hint）
    balloonwjui::DuiButton*      m_tabMine;      //胶囊页签「我的共享」（2026-10-03 起取代 DuiTab）
    balloonwjui::DuiButton*      m_tabShared;    //胶囊页签「共享给我的」
    balloonwjui::DuiSearchBox*   m_search;       //顶栏的搜索框：按名称、路径（只有「我的共享」有）、所在会话、分享者过滤当前页签的列表，提示文字随页签切换
    balloonwjui::DuiButton*      m_btnNewShare;  //顶栏「发起共享」（品牌蓝实心）
    balloonwjui::DuiButton*      m_btnRefresh;   //顶栏「刷新」（白底灰边）
    balloonwjui::DuiVirtualList* m_list;         //左侧列表
    balloonwjui::DuiHBox*        m_body;         //左侧列表卡片与右侧详情卡片所在的行（详情卡片宽度随 DPI 变化时对它改 hint）
    balloonwjui::DuiVBox*        m_detailCard;   //右侧详情卡片
    balloonwjui::DuiHBox*        m_detailHeader; //详情头部的一行（图标、名称与路径、状态胶囊；显隐变化后对它重排）
    balloonwjui::DuiAvatar*      m_detailIcon;   //详情头部的类型图标（借用 m_headerIconBmp）
    balloonwjui::DuiLabel*       m_detailTitle;  //详情头部的名称（加粗大字）；没有选中时显示提示
    balloonwjui::DuiLabel*       m_detailPath;   //详情头部名称下面的路径（弱色，中间省略）
    balloonwjui::DuiVBox*        m_detailChip;   //详情头部右端的状态胶囊（圆角底色）
    balloonwjui::DuiLabel*       m_detailChipText; //状态胶囊里的文字
    balloonwjui::DuiHBox*        m_actionRow;    //操作按钮所在的行（按钮显隐后对它重排）
    balloonwjui::DuiButton*      m_btnOpenLocal; //「在本机打开」（品牌蓝实心，我的共享的主操作）
    balloonwjui::DuiButton*      m_btnOpen;      //「打开」（品牌蓝实心，共享给我的主操作）
    balloonwjui::DuiButton*      m_btnEdit;      //「调整成员」（白底灰边）
    balloonwjui::DuiButton*      m_btnCancel;    //「取消共享」（红色实心，放在最右）。「定位到会话」改为信息表里的链接
    balloonwjui::DuiScrollView*  m_detailScroll; //详情的滚动视口
    balloonwjui::DuiToast*       m_toast;        //顶栏下方的浮层提示（归内容根容器所有）
    std::wstring                 m_toastText;    //最近一次浮层提示的文字，供测试读取
    balloonmain::ProgramWindowIcons m_programIcons;  //标题栏与任务栏 / Alt-Tab 的程序图标，本对象持有，跨关闭再打开复用
    CShareAvatarSource           m_avatars;      //详情里头像的来源与位图（位图归它所有，头像控件借用）；关闭窗口后释放位图
    std::wstring                 m_placementIni; //保存窗口位置的配置文件全路径（<个人目录>ChatLog\fileshare_ui.ini），新建窗口时取得；取不到时为空，不保存
    RECT                         m_lastNormalRect; //窗口最近一次处于还原态时的矩形（屏幕坐标、物理像素），关窗时保存
    bool                         m_hasNormalRect;  //m_lastNormalRect 是否有效：本次打开后窗口处于过还原态才为 true
};

} // namespace fileshare
