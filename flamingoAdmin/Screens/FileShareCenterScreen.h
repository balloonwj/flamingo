/**
 *  flamingoAdmin —— 侧栏「文件共享」对应的页面 FileShareCenterScreen：「文件共享」「共享审计」两个页签的外层页面。
 *
 *  2026-10-08 起「共享审计」不再是侧栏里单独的一项，而是并入本页成为第二个页签。本页自上而下：
 *    · 页头：标题「文件共享」，副标题随页签变化；右侧依次为页签切换「文件共享 | 共享审计」（ui::SegControl，kIdFsTabs，
 *      与敏感词页的「词库 | 拦截记录」同一样式）与「刷新」（kIdFsRefresh，只刷新当前页签）。页签放在页头右侧、不另占
 *      一行，是为了在最小窗口下给两个页签的表格留出至少三行的高度；
 *    · 当前页签的内容：FileSharesScreen 或 FileShareAuditScreen（两者都不再自带页头），另一个隐藏。
 *  每次进入本页或切换页签时调用当前页签的 OnEnter（按角色显示企业筛选并刷新列表，筛选条件保留）。
 *  页签在本次登录内保持上次看的那一个；换了登录身份后回到第一个页签。
 *
 *  本页不直接接触网络与对话框：两个页签的请求出口、界面请求与防抖定时器所需的主窗口句柄都经本页转发给它们；
 *  它们的控件通知、定时器与接口应答仍由 AdminFrame 直接交给 SharesPage() / AuditPage()。
 *  单元测试见 Tests/FileShareScreensTests.cpp 的 FSC 系列。
 *
 *  典型用法（AdminFrame 内）：
 *      std::unique_ptr<FileShareCenterScreen> center(new FileShareCenterScreen());
 *      center->SetHostWindow(m_hWnd);
 *      center->SetRequestSink(&AdminFrame::SensitiveRequestThunk, this);
 *      center->SetUiRequestHandler(&AdminFrame::FileShareUiRequestThunk, this);
 *      //侧栏点「文件共享」时
 *      center->OnEnter();
 *      //页签切换（kIdFsTabs 的 DUIN_VALUECHANGED）
 *      center->OnTabChanged(static_cast<int>(n->extra));
 *
 *  balloonwj@qq.com   2026-10-08
 */
#pragma once

#include "Controls/Layout/DuiLayout.h"
#include "Screens/FileShareUi.h"

namespace flamingoadmin {

namespace ui { class PageHeader; class SegControl; }

class FileSharesScreen;
class FileShareAuditScreen;

// 文件共享页：页头（含页签切换与「刷新」）+ 当前页签的内容（「文件共享」或「共享审计」）。
class FileShareCenterScreen : public balloonwjui::DuiVBox
{
public:
    // 页签下标
    enum Tab
    {
        kTabShares = 0,   // 「文件共享」页签：查看共享与成员、强制取消
        kTabAudit  = 1,   // 「共享审计」页签：文件共享的审计记录
        kTabCount  = 2,   // 页签个数
    };

    // 构造时建好页头、页签行与两个页签的内容，此时不发请求；初始为「文件共享」页签。
    FileShareCenterScreen();

    // 注入主窗口句柄（两个页签挂输入防抖定时器用），转交两个页签。hwnd 为空时不防抖、立即查询。
    void SetHostWindow(HWND hwnd);

    // 设置两个页签的请求出口与界面请求处理函数，含义见 FileShareUi.h。user 所有权归调用方。
    void SetRequestSink(fsui::RequestSinkFn fn, void* user);
    void SetUiRequestHandler(fsui::UiRequestFn fn, void* user);

    // 每次切换到本页时由 AdminFrame 调用：换了登录身份时回到第一个页签，然后进入当前页签（刷新其列表）。
    void OnEnter();

    // 切到指定页签并进入它（刷新其列表）。AdminFrame 打开「共享审计」页签时直接调用。
    //   tab：页签下标，取值见 Tab；越界时不做任何事。
    // 副作用：同步页签控件的选中段、页头副标题与两个页签的显隐，并重新排版本页。
    void SelectTab(int tab);

    // 页签控件上点了另一段（kIdFsTabs 的 DUIN_VALUECHANGED）：与 SelectTab 相同。
    //   index：页签下标，取值见 Tab；越界时不做任何事。
    void OnTabChanged(int index);

    // 页头「刷新」（kIdFsRefresh 的 DUIN_CLICK）：按当前条件重新拉取当前页签的数据。
    void Refresh();

    // ---- 只读访问（AdminFrame 与单元测试用）----

    // 当前页签下标，取值见 Tab。
    int ActiveTab() const { return m_activeTab; }

    // 两个页签的内容页（借用，所有权在本页的控件树中）。
    FileSharesScreen* SharesPage() const { return m_shares; }
    FileShareAuditScreen* AuditPage() const { return m_audit; }

    // 页头当前的副标题（单元测试核对它随页签变化）。
    CString Subtitle() const { return m_subtitle; }

    // 某个页签对应的页头副标题（静态函数，便于单元测试）；tab 越界时返回空串。
    static LPCTSTR SubtitleFor(int tab);

private:
    // 按 m_activeTab 同步页签控件、页头副标题与两个页签的显隐，并对本页重新排版（SetVisible 本身不触发排版）。
    void ApplyTab();

    // 进入当前页签（调用其 OnEnter）。
    void EnterActiveTab();

    ui::PageHeader*        m_header;        // 页头（借用，所有权在控件树中）
    ui::SegControl*        m_tabs;          // 页签切换控件（借用，所有权在控件树中）
    FileSharesScreen*      m_shares;        // 「文件共享」页签的内容（借用，所有权在控件树中）
    FileShareAuditScreen*  m_audit;         // 「共享审计」页签的内容（借用，所有权在控件树中）
    int                    m_activeTab;     // 当前页签下标，取值见 Tab；本次登录内保持
    CString                m_subtitle;      // 页头当前的副标题
    CString                m_sessionToken;  // 上次进入本页时的登录令牌，用来发现换了登录身份
};

} // namespace flamingoadmin
