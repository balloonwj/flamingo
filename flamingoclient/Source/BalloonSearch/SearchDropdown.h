/**
 *  SearchDropdown.h
 *
 *  主面板搜索框下方的"搜索结果下拉"。本控件<u>不</u>自带输入框 —— 用户在主面板
 *  既有的搜索框里输入，本下拉只负责把命中结果紧贴搜索框下方展示。
 *
 *  窗口形态：顶层无边框浮窗（WS_POPUP），窗口类带 CS_DROPSHADOW 系统投影，使它
 *  像下拉/菜单那样明显浮于内容之上（白底卡片在浅色内容上靠投影区分层次）。建窗
 *  时加 WS_EX_NOACTIVATE，故点击下拉<u>不抢</u>主面板的激活与键盘焦点：用户在
 *  主面板搜索框里能连续输入，下拉只是浮层。宿主能力复用 balloonui DuiHost（以
 *  SubclassWindow 方式接管自建的 HWND）。
 *
 *  内部结构：顶部分类筛选标签栏（全部 / 联系人 / 群组 / 消息 / 文件，后两置灰
 *  占位）+ 结果滚动区（命中的联系人 / 群组行，命中昵称/群名时黄底高亮）。匹配
 *  复用 contactsearch::Match（子串 + 拼音首字母，大小写不敏感）。
 *
 *  通知路由：建窗时 owner 传 NULL，故 DuiHost::SendNotify 的目标 GetParent 为空、
 *  自路由回本窗口，结果行 / 标签点击在本类 OnChildNotify 内自行处理（命中行 →
 *  回调对端 / 群 id 并隐藏；标签 → 切筛选刷新）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-06-04
 */
#pragma once

#include "../../../third-party/balloonui/balloonui/DuiHost.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"

#include <stdint.h>
#include <vector>
#include <map>

namespace net { struct ContactItem; struct UserGroupItem; }
namespace balloonwjui { class DuiControl; class DuiScrollView; }
namespace balloonsearch { class SearchFilterTab; }

/**
 *  CSearchDropdown —— 主面板搜索结果下拉（WS_CHILD 子窗口）。
 */
class CSearchDropdown : public balloonwjui::DuiHost
{
public:
    // 选中某联系人结果时的回调；peerUserId 为对端 userid。
    typedef void (*PickContactCallback)(void* userdata, UINT peerUserId);
    // 选中某群组结果时的回调；groupId 为群 id。
    typedef void (*PickGroupCallback)(void* userdata, int64_t groupId);

    CSearchDropdown();
    ~CSearchDropdown();

    /**
     *  创建本下拉浮窗（幂等，仅首次真正创建）。须在调用方 HWND 就绪后调。
     *    owner：归属窗口（主面板）HWND，仅用于记录 / 屏幕换算基准；窗口本身以
     *           无 owner 的顶层 WS_POPUP 创建（让通知自路由回本窗口）。
     *  @return 创建成功（或已创建）返回 true；失败返回 false。
     */
    bool EnsureCreated(HWND owner);

    /**
     *  设置搜索数据源与当前登录用户（均借用，不持有；调用方须保证生命周期覆盖
     *  本下拉使用期。一般直接传主面板自己持有的 m_contacts / m_myGroups）。
     *    contacts：联系人资料池指针。
     *    groups：本人加入的群列表指针。
     *    selfUserId：当前登录用户 userid。<u>注意它有两个用途</u>：一是按
     *                includeSelf 决定要不要把自己从联系人结果里滤掉，二是画头像
     *                时要用（ApplyRowAvatar 需要知道哪一行是自己）。所以想让
     *                结果包含自己时，<u>不能</u>把本参数传 0，要走 includeSelf。
     *    includeSelf：联系人结果里要不要包含自己。通讯录树已经会列出自己（好让
     *                用户看清自己在组织架构中的位置），搜索跟着放开才不别扭；
     *                命中自己的那一行显示名会加"（我）"后缀。
     */
    void SetData(const std::vector<net::ContactItem>* contacts,
                 const std::vector<net::UserGroupItem>* groups,
                 UINT selfUserId,
                 bool includeSelf);

    /**
     *  设置选中结果回调。
     *    contactCb：点联系人结果时触发。
     *    groupCb：点群组结果时触发。
     *    userdata：回调透传的用户数据（一般是主面板 this）。
     */
    void SetPickCallbacks(PickContactCallback contactCb,
                          PickGroupCallback groupCb, void* userdata);

    /**
     *  设新查询词并据它在搜索框下方展示 / 刷新结果。查询词去首尾空白后为空则
     *  隐藏下拉（"空内容不显示"）。
     *    anchorScreenRect：搜索框矩形（<u>屏幕坐标</u>，调用方对 searchBox->GetRect()
     *                      做 ClientToScreen 后传入）。下拉左上角落在其左下角。
     *    panelWidth：主面板客户区宽（下拉与主面板同宽，逻辑像素）。
     *    query：当前查询词（UTF-16）。
     */
    void UpdateQuery(const RECT& anchorScreenRect, int panelWidth, LPCTSTR query);

    /**
     *  跟随宿主窗口移动 / 改变大小 —— 只重新摆放浮窗的位置与宽度，<u>不</u>重算搜索
     *  结果。本下拉是独立顶层浮窗、不是主面板的子窗口，宿主移动时系统不会带着它走，
     *  故主面板收到 WM_MOVE / WM_SIZE 时须调本方法把它拉回搜索框下方，否则拖动主面板
     *  会出现下拉留在原地、与主面板分离的现象。
     *  之所以不走 UpdateQuery：拖动过程中 WM_MOVE 高频到达，每次重算命中列表既无必要
     *  又会卡顿；结果没变，高度也不必变，沿用当前窗口高度即可。
     *    anchorScreenRect：搜索框矩形（<u>屏幕坐标</u>），口径同 UpdateQuery。
     *    panelWidth：主面板客户区宽（逻辑像素），下拉宽 = 它。
     *  下拉当前未创建或不可见时只记下新锚点，等下次展示时自然生效。
     */
    void FollowAnchor(const RECT& anchorScreenRect, int panelWidth);

    /**
     *  隐藏下拉（不销毁；下次 UpdateQuery 再显示）。
     */
    void HideDropdown();

    /**
     *  下拉当前是否可见。
     */
    bool IsShown() const;

    /**
     *  取下拉浮窗的 HWND（未创建返回 NULL）。供调用方做"光标是否落在下拉上"之类
     *  的命中判断（如失焦收起时排除点在下拉上的情形）。
     */
    HWND GetHwnd() const { return m_hWnd; }

    BEGIN_MSG_MAP(CSearchDropdown)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnChildNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiHost)
    END_MSG_MAP()

private:
    // 当前分类筛选。
    enum FilterKind
    {
        kFilterAll      = 0,   // 全部（联系人 + 群组）
        kFilterContacts = 1,   // 仅联系人
        kFilterGroups   = 2,   // 仅群组
    };

    // 自路由的 DUI 通知处理：结果行点击 → 回调对端 / 群 id 并隐藏；筛选标签点击 →
    // 切换筛选并刷新；其余通知放行（bHandled=FALSE 交回 DuiHost 链）。
    LRESULT OnChildNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // 首次构建控件树（筛选标签栏 + 结果滚动区）。
    void BuildContentOnce();

    // 按当前查询词 + 筛选重建结果列表，并刷新各标签计数；返回命中总数。
    int RefreshResults();

    // 按命中联系人数 / 群组数算下拉应有高度（逻辑像素，含标签栏与上限封顶）。
    int ComputeHeight(int contactRows, int groupRows, bool anyQuery) const;

private:
    const std::vector<net::ContactItem>*   m_contacts;   // 联系人数据源，借用
    const std::vector<net::UserGroupItem>* m_groups;     // 群列表数据源，借用
    UINT                m_selfUserId;                     // 当前登录用户 userid（既用于过滤判断，也用于画头像）
    bool                m_includeSelf;                    // 联系人结果里是否包含自己；false 时把自己滤掉
    PickContactCallback m_contactCb;                     // 选中联系人回调
    PickGroupCallback   m_groupCb;                       // 选中群组回调
    void*               m_pickUd;                        // 回调用户数据

    bool    m_built;          // 控件树是否已构建
    int     m_filter;         // 当前分类筛选（FilterKind）
    CString m_query;          // 当前查询词（已去首尾空白）

    int     m_panelWidth;     // 最近一次展示用的主面板宽（下拉宽 = 它）
    int     m_anchorLeft;     // 最近一次锚点左边 x（屏幕坐标，下拉左上角 x）
    int     m_anchorBottom;   // 最近一次锚点底部 y（屏幕坐标，下拉左上角 y）

    balloonwjui::DuiScrollView*     m_scroll;        // 结果滚动区（借用，树持有）
    balloonsearch::SearchFilterTab* m_tabAll;        // "全部"标签（借用）
    balloonsearch::SearchFilterTab* m_tabContacts;   // "联系人"标签（借用，更新计数）
    balloonsearch::SearchFilterTab* m_tabGroups;     // "群组"标签（借用，更新计数）

    // 结果行 → 选中目标。命中联系人写 m_rowContact，命中群组写 m_rowGroup；
    // 两者键互斥（一行只属于其一）。每次 RefreshResults 重建前清空。
    std::map<balloonwjui::DuiControl*, UINT>    m_rowContact;  // 行 → 对端 userid
    std::map<balloonwjui::DuiControl*, int64_t> m_rowGroup;    // 行 → 群 id
};
