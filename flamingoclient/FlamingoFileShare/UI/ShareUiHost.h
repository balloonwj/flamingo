/**
 *  文件共享 DLL 内部窗口之间互相打开的接口，ShareUiHost.h
 *
 *  浏览窗口、发起共享对话框、调整成员窗口都归 CFileShareModule 持有（每个共享 / 会话至多一个）。本会话共享列表浮层与
 *  「文件共享」窗口要打开它们时不直接持有这些窗口，而是经本接口请模块打开，模块负责查找已打开的那一个并前置。
 *  由 CFileShareModule 实现，只在 UI 线程上调用。
 *
 *  典型用法（浮层里点「打开」）：
 *      m_ui->UiOpenShare(m_owner, entry.m_shareId);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>

namespace fileshare {

struct ShareListEntry;

/**
 *  图片预览的来源（发起预览的浏览窗口）。看图窗里按 ← / → 时，模块把翻页转给它。只在 UI 线程上调用。
 */
class IShareImagePreviewSource
{
public:
    virtual ~IShareImagePreviewSource()
    {
    }

    /**
     *  翻到同一目录的上一张或下一张图片。
     *  @param delta -1 上一张，1 下一张
     */
    virtual void OnPreviewNavigate(int delta) = 0;
};

/**
 *  IShareUiHost —— DLL 内部打开各窗口的入口（由模块实现）。
 */
class IShareUiHost
{
public:
    virtual ~IShareUiHost()
    {
    }

    /**
     *  打开某个会话的发起共享对话框（已打开时前置）。
     *  @param owner                 所有者窗口，可为 NULL
     *  @param ctype                 会话类型
     *  @param cid                   会话 id（本人视角）
     *  @param openConversationAfter 发起成功后是否经宿主打开（前置）这个会话的聊天窗：从「文件共享」窗口发起时为 true，
     *                               卡片就在那个聊天窗里；从聊天窗的浮层发起时为 false，聊天窗本来就在眼前
     */
    virtual void UiOpenCreateShare(HWND owner, int ctype, int64_t cid, bool openConversationAfter) = 0;

    /**
     *  打开「文件共享」窗口并记住发起它的会话（2026-10-03 起，聊天窗浮层的「管理全部共享」）：之后在窗口里点「发起共享」
     *  直接在这个会话里发起，不弹选人窗口、不新建群。窗口已打开时改为记住这个会话并前置窗口。
     *  @param owner 所有者窗口（聊天窗），可为 NULL
     *  @param ctype 会话类型：1 单聊 2 群聊
     *  @param cid   会话 id（本人视角）
     *  @return 窗口已显示返回 true
     */
    virtual bool UiOpenShareCenter(HWND owner, int ctype, int64_t cid) = 0;

    /**
     *  打开某个共享的浏览窗口（已打开时前置）。
     *  @param owner   用来居中对齐的参照窗口，可为 NULL
     *  @param shareId 共享 id
     */
    virtual void UiOpenShare(HWND owner, int64_t shareId) = 0;

    /**
     *  打开某个共享的调整成员窗口（已打开时前置）。
     *  @param owner 所有者窗口，可为 NULL
     *  @param share 要调整的共享（「我的共享」列表里的一项，带全部成员与权限）
     */
    virtual void UiEditMembers(HWND owner, const ShareListEntry& share) = 0;

    /**
     *  用主程序的看图窗显示一张预览图（第 2 期第 4 步）。
     *  @param owner  看图窗的拥有者窗口，可为 NULL
     *  @param path   预览图的本机路径
     *  @param source 预览的来源（不拥有，可为 NULL）：看图窗里按 ← / → 时转给它；来源销毁前须调 UiPreviewSourceGone
     *  @return 已交给宿主返回 true
     */
    virtual bool UiOpenImage(HWND owner, const std::wstring& path, IShareImagePreviewSource* source) = 0;

    /**
     *  预览的来源要销毁了：此后不再把翻页转给它。
     *  @param source 来源
     */
    virtual void UiPreviewSourceGone(IShareImagePreviewSource* source) = 0;

    /**
     *  主动发起共享（「文件共享」窗口的「发起共享」，2026-10-03 起）：请宿主弹出选人窗口选择共享对象，选好后打开
     *  发起共享对话框（按选好的对象发起）。发起共享对话框已打开时只前置它。
     *  @param owner 发起的窗口（「文件共享」窗口），可为 NULL
     *  @return 选人窗口已弹出（或发起共享对话框已前置）返回 true；宿主弹不出选人窗口（离线、通讯录还没加载）返回 false
     */
    virtual bool UiStartDirectShare(HWND owner) = 0;
};

} // namespace fileshare
