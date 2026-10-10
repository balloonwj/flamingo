/**
 *  在共享内选一个目标文件夹的小窗口（「移动到…」），ShareFolderPickDialog.h
 *
 *  浏览窗口右键菜单的「移动到…」用它选目标目录（docs/文件共享方案.md 7.4，第 2 期第 2 步）：
 *  顶部是「上一级」按钮与当前路径，中间列出当前目录下的子文件夹（双击进入），底部是状态文字与「移动到这里」「取消」。
 *  目录内容经 IShareFolderSource 向分享者请求（浏览窗口转给它的访问状态机），结果由浏览窗口经 OnListed 交回；
 *  不合法的目标（移到自己或自己的下级）由分享者判定并回 808，这里不重复判断。
 *
 *  非模态、带所有者：打开期间把所有者窗口置为不可操作，关闭时恢复。控件全部用 balloonui 现成的
 *  （DuiLabel / DuiButton / DuiVirtualList / DuiVBox / DuiHBox）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法（浏览窗口右键菜单的「移动到…」）：
 *      m_folderPick.Open(m_hWnd, this, this, m_currentPath, rootName);
 *      ...（浏览窗口收到列目录结果时）if (m_folderPick.IsOpen()) m_folderPick.OnListed(path, code, entries);
 *
 *  balloonwj@qq.com   2026-10-03
 */
#pragma once

#include <string>
#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "../../../third-party/balloonui/balloonui/DuiNotify.h"
#include "../../Source/UI/BalloonMain/TitleBarIcon.h"   //ProgramWindowIcons：标题栏与任务栏的程序图标
#include "../Engine/ShareControlFraming.h"   //ShareEntry

namespace balloonwjui {
class DuiLabel;
class DuiButton;
class DuiVirtualList;
}

namespace fileshare {

/**
 *  目录内容的来源，由打开窗口的一方实现（浏览窗口转给访问状态机）。
 */
class IShareFolderSource
{
public:
    virtual ~IShareFolderSource()
    {
    }

    /**
     *  请列一个目录；结果经 CShareFolderPickDialog::OnListed 交回。
     *  @param path 目录的相对路径（共享根为空串）
     */
    virtual void RequestFolderList(const std::string& path) = 0;
};

/**
 *  选择结果的监听者。回调在 UI 线程上。
 */
class IShareFolderPickListener
{
public:
    virtual ~IShareFolderPickListener()
    {
    }

    /**
     *  用户点了「移动到这里」。回调时窗口已隐藏、所有者已恢复可操作。
     *  @param dir 选中的目录（相对路径，共享根为空串）
     */
    virtual void OnShareFolderPicked(const std::string& dir) = 0;

    /**
     *  窗口关闭了（无论选没选）。打开方据此恢复自己的列目录。
     */
    virtual void OnShareFolderPickClosed() = 0;
};

/**
 *  CShareFolderPickDialog —— 在共享内选目标文件夹的小窗口。只在 UI 线程使用。
 */
class CShareFolderPickDialog : public balloonwjui::DuiFrameWindow
{
public:
    CShareFolderPickDialog();
    ~CShareFolderPickDialog();

    /**
     *  打开窗口并列出 startPath 下的子文件夹；已打开时先关闭旧的。
     *  @param owner     所有者窗口（打开期间被置为不可操作），可为 NULL；不转移所有权
     *  @param source    目录内容的来源（不拥有，须在本窗口关闭之前一直有效），不可为 NULL
     *  @param listener  监听者（不拥有，同上），不可为 NULL
     *  @param startPath 起始目录（相对路径）
     *  @param rootName  共享根的显示名（路径栏用）
     *  @return 窗口已显示返回 true
     */
    bool Open(HWND owner, IShareFolderSource* source, IShareFolderPickListener* listener, const std::string& startPath,
              const std::wstring& rootName);

    /**
     *  关闭窗口并恢复所有者；打开着时回调 OnShareFolderPickClosed。窗口未创建时调用是安全的。
     */
    void Close();

    //窗口是否打开着
    bool IsOpen() const;

    /**
     *  打开方收到列目录结果时交给本窗口。路径与最近一次请求的不一致时忽略（用户已经换了目录）。
     *  @param path    目录
     *  @param code    结果码：0 成功，否则为错误码
     *  @param entries 目录内容（含文件，本窗口只取子文件夹）
     */
    void OnListed(const std::string& path, int code, const std::vector<ShareEntry>& entries);

    //当前显示的目录（测试用）
    const std::string& GetCurrentPath() const;

    //当前列出的子文件夹个数（测试用）
    size_t GetFolderCount() const;

    //进入第 index 个子文件夹（与双击相同，测试也直接调用）
    void EnterFolder(size_t index);

    //回到上一级（与点「上一级」相同）
    void GoUp();

    /**
     *  与点「移动到这里」相同：关闭窗口并把当前目录交给监听者（测试也直接调用）。
     *  @return 已回调返回 true；正在加载时返回 false
     */
    bool ConfirmHere();

    BEGIN_MSG_MAP(CShareFolderPickDialog)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    //WM_CLOSE：点标题栏的关闭按钮或「取消」（经 PostMessage）
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //WM_DUI_NOTIFY：三个按钮的点击
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //构建客户区控件树
    void BuildContent();

    //请来源列一个目录，状态栏显示「正在加载」
    void Request(const std::string& path);

    //刷新路径文字与「上一级」按钮
    void UpdatePathBar();

    //设置状态文字
    void SetStatus(const std::wstring& text);

    //恢复所有者窗口为可操作并把它带到前台
    void RestoreOwner();

    //DuiVirtualList 的画行回调与单击 / 双击回调（user 为本窗口）
    static void PaintRowThunk(void* user, HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);
    static void RowClickThunk(void* user, int rowIndex, bool isDoubleClick);

    //画一行：目录图标与名称
    void PaintRow(HDC hdc, int rowIndex, const RECT& rowRect, bool selected, bool hover);

private:
    HWND                        m_owner;          //所有者窗口；打开期间被置为不可操作；没有时为 NULL
    IShareFolderSource*         m_source;         //目录内容的来源（不拥有）
    IShareFolderPickListener*   m_listener;       //监听者（不拥有）
    std::wstring                m_rootName;       //共享根的显示名
    std::string                 m_currentPath;    //当前显示的目录
    std::string                 m_requestedPath;  //最近一次请求的目录
    bool                        m_loading;        //是否在等列目录结果
    std::vector<ShareEntry>     m_folders;        //当前目录下的子文件夹，按名称排序

    //以下控件归窗口的控件树所有，窗口创建后有效、销毁后失效
    balloonwjui::DuiButton*      m_upButton;      //「上一级」
    balloonwjui::DuiLabel*       m_pathLabel;     //当前路径
    balloonwjui::DuiVirtualList* m_list;          //子文件夹列表
    balloonwjui::DuiLabel*       m_status;        //状态文字
    balloonmain::ProgramWindowIcons m_programIcons;  //标题栏与任务栏 / Alt-Tab 的程序图标，本对象持有，跨关闭再打开复用
};

} // namespace fileshare
