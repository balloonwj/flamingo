/**
 *  文件共享的右下角通知弹窗，ShareNoticePopup.h
 *
 *  第 4 期（docs/文件共享方案.md 第 14 节）：一个窗口类，两个实例——分享者的「文件共享动态」（别人在本人的共享上完成了下载、
 *  上传、重命名、删除、移动）与访问者的「文件共享传输」（本人的传输结束）。由 CShareNoticeCenter 持有与摆放，本类只负责画：
 *    - 每一条两行：第一行是标题与右端的时间，第二行是说明（失败为红字）与右端的操作链接（DuiLabel 的链接模式）；
 *    - 最多显示 kShareNoticeVisibleRows 条，更多的在底部写「还有 N 条」；分享者的弹窗底部另有「查看全部」链接；
 *    - 窗口高度随条数变化（HeightFor），宽度固定；各尺寸按弹窗所在显示器的 DPI 换算（2026-10-07 起，字体随 DPI 放大，
 *      窗口不放大时文字放不下）；不自动关闭，点标题栏的关闭按钮才关闭；
 *    - 访问者下载成功那一条的「已保存到 …」放不下时省略路径中间（DT_PATH_ELLIPSIS），其余说明省略末尾。
 *  形态与录制完成提示一致（FlamingoMedia/Record/RecordDoneToast）：无所有者的置顶 WS_POPUP（UI 约定第 11 条：2026-10-14 起
 *  有无所有者，控件通知都发回本窗口，WS_POPUP 可以照常使用）、WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW、显示时不抢焦点，
 *  标题栏左侧有程序图标（UI 约定第 10 条）。
 *  点了链接后不在控件通知里直接执行，而是投给自己一条 kMsgNoticeLink，等通知处理完再交给监听者：执行的动作可能重建本窗口
 *  的内容，不能在被点的控件的通知还没返回时销毁它。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法（CShareNoticeCenter 内部）：
 *      m_popup.SetKind(share_notice_popup_owner, this);
 *      m_popup.ShowEntries(m_list.Visible(), m_list.OverflowCount());
 *      balloonmain::MoveWindowToWorkAreaCorner(m_popup.m_hWnd, ...);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   //ProgramWindowIcons：标题栏的程序图标

#include <stdint.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../Engine/ShareNoticeModel.h"

namespace balloonwjui {
class DuiLabel;
}

namespace fileshare {

//两种弹窗
enum share_notice_popup_kind
{
    share_notice_popup_owner   = 1,   //「文件共享动态」：分享者收到别人操作其共享的通知，底部有「查看全部」
    share_notice_popup_visitor = 2,   //「文件共享传输」：访问者本人的传输结束通知
};

/**
 *  弹窗的监听者（CShareNoticeCenter 实现）。回调都在 UI 线程上、在控件通知处理完之后。
 */
class IShareNoticePopupListener
{
public:
    virtual ~IShareNoticePopupListener()
    {
    }

    /**
     *  点了某一条的操作链接。
     *  @param kind    哪个弹窗，share_notice_popup_kind
     *  @param entryId 条目编号（CShareNoticeList 分配的）
     *  @param action  动作，share_notice_action
     */
    virtual void OnNoticeLink(int kind, uint32_t entryId, int action) = 0;

    /**
     *  点了底部的「查看全部」（只有分享者的弹窗有）。
     *  @param kind 哪个弹窗
     */
    virtual void OnNoticeViewAll(int kind) = 0;

    /**
     *  用户点标题栏的关闭按钮关闭了弹窗（Hide 引起的关闭不回调）。
     *  @param kind 哪个弹窗
     */
    virtual void OnNoticePopupClosed(int kind) = 0;
};

/**
 *  CShareNoticePopup —— 文件共享的右下角通知弹窗。只在 UI 线程使用。
 */
class CShareNoticePopup : public balloonwjui::DuiFrameWindow
{
public:
    //点了链接之后投给自己的消息：wParam 为条目编号（「查看全部」为 0），lParam 为动作（「查看全部」为 0）。
    //窗口私有消息走 WM_APP 段（UI 约定第 6 条），偏移 0x160 已登记在开发约定文档
    static const UINT kMsgNoticeLink = WM_APP + 0x160;

    CShareNoticePopup();
    ~CShareNoticePopup();

    /**
     *  设置弹窗的种类与监听者（建窗之前调用一次）。
     *  @param kind     share_notice_popup_kind
     *  @param listener 监听者（不拥有，须在本窗口销毁之前一直有效），可为 NULL
     */
    void SetKind(int kind, IShareNoticePopupListener* listener);

    /**
     *  显示这些条目（还没建窗时先建窗；已显示时替换内容），并按条数调整窗口高度。不挪位置，摆放由调用方负责；
     *  新建的窗口在调用方摆好之前是隐藏的，摆好后调 ShowNoActivate。
     *  @param entries  要显示的条目，新的在前（CShareNoticeList::Visible 的结果）；为空时等同 Hide
     *  @param overflow 没显示出来的条数（底部的「还有 N 条」）
     *  @return 窗口有内容返回 true；建窗失败或没有条目返回 false
     */
    bool ShowEntries(const std::vector<ShareNoticeEntry>& entries, size_t overflow);

    //显示（不激活、不抢焦点）；窗口未创建时调用无效果
    void ShowNoActivate();

    //关闭并销毁窗口，不回调监听者；可重复调用
    void Hide();

    //窗口是否显示着
    bool IsShowing() const;

    /**
     *  一个弹窗显示 rows 条时的整窗高度（含标题栏）。各段尺寸分别按 dpi 换算后相加。
     *  @param rows   显示的条数
     *  @param footer 是否有底部一行（「还有 N 条」或「查看全部」）
     *  @param dpi    弹窗所在显示器的 DPI（96 为 100% 缩放；不大于 0 时按 96）
     *  @return 整窗高度，物理像素
     */
    static int HeightFor(size_t rows, bool footer, int dpi);

    /**
     *  整窗宽度，两种弹窗相同。
     *  @param dpi 弹窗所在显示器的 DPI（96 为 100% 缩放；不大于 0 时按 96）
     *  @return 整窗宽度，物理像素（100% 缩放下为 480）
     */
    static int PopupWidth(int dpi);

    //最近一次 ShowEntries 时取到的 DPI（鼠标所在显示器，弹窗随后摆到这块显示器上）；还没显示过时为 96
    int GetDpi() const;

    //以下供单元测试读取界面内容
    size_t GetRowCount() const;
    std::wstring GetRowTitle(size_t row) const;
    std::wstring GetRowDetail(size_t row) const;

    /**
     *  某一条说明的 DrawText 标志（单元测试据此检查省略方式）。
     *  @param row 第几条（0 为最上面）
     *  @return DT_* 组合；越界时返回 0
     */
    DWORD GetRowDetailAlign(size_t row) const;
    std::vector<std::wstring> GetRowLinkTexts(size_t row) const;
    std::wstring GetFooterText() const;
    bool HasViewAllLink() const;

    /**
     *  某一条某个链接的控件 id（单元测试据此投递点击通知）。
     *  @param row  第几条（0 为最上面）
     *  @param link 第几个链接（0 为最左）
     *  @return 控件 id；越界时返回 0
     */
    UINT GetLinkCtrlId(size_t row, size_t link) const;

    //「查看全部」的控件 id
    static UINT ViewAllCtrlId();

    BEGIN_MSG_MAP(CShareNoticePopup)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        MESSAGE_HANDLER(kMsgNoticeLink, OnNoticeLinkMsg)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //WM_DUI_NOTIFY：链接的点击，查到对应的条目与动作后投给自己 kMsgNoticeLink
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //kMsgNoticeLink：控件通知处理完之后再交给监听者
    LRESULT OnNoticeLinkMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_CLOSE：点了标题栏的关闭按钮，关闭并告诉监听者
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //建窗（标题、标题栏、程序图标）；已建时什么也不做
    bool EnsureCreated();

    //按条目重建客户区控件
    void BuildContent(const std::vector<ShareNoticeEntry>& entries, size_t overflow);

    //建一个链接标签并量出它需要的宽度
    std::unique_ptr<balloonwjui::DuiLabel> MakeLink(const std::wstring& text, UINT ctrlId, int& width) const;

    //一条显示出来的内容（测试读取用，与控件同步）
    struct RowView
    {
        std::wstring              m_title;   //第一行
        std::wstring              m_detail;  //第二行的说明
        DWORD                     m_detailAlign;   //第二行说明的 DrawText 标志（含省略方式）
        std::vector<std::wstring> m_links;   //链接文字
        std::vector<UINT>         m_ids;     //链接的控件 id，与 m_links 一一对应

        RowView()
            : m_detailAlign(0)
        {
        }
    };

    int                          m_kind;       //弹窗种类，share_notice_popup_kind
    IShareNoticePopupListener*   m_listener;   //监听者（不拥有），可为 NULL
    //链接控件 id → (条目编号, 动作)；每次重建内容时重填
    std::vector<std::pair<UINT, std::pair<uint32_t, int> > > m_linkTargets;
    std::vector<RowView>         m_rows;       //显示出来的各条，最上面的在前
    std::wstring                 m_footer;     //底部的「还有 N 条」；没有时为空
    bool                         m_viewAll;    //底部是否有「查看全部」
    int                          m_dpi;        //各尺寸换算所用的 DPI：每次 ShowEntries 时取鼠标所在显示器的 DPI（弹窗随后由
                                               //调用方摆到这块显示器的右下角）；构造时为 96
    balloonmain::ProgramWindowIcons m_icons;   //标题栏的程序图标，本对象持有；窗口先于它销毁
};

} // namespace fileshare
