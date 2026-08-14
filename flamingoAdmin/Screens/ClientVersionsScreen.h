/**
 *  flamingoAdmin —— 客户端版本界面 ClientVersionsScreen。
 *
 *  平台超管在这里发布 PC 客户端的升级版本：升级包由运维手工放进服务器的升级包
 *  目录（控制台<u>不负责上传</u>），本界面把目录里的候选包连同大小与 MD5 列出来
 *  供选择，管理员只需填版本号、最低版本与更新说明。
 *
 *  与 AuditScreen 同构：页头两个按钮是真实的子按钮控件，其余（提示条、表格、
 *  行内操作）全部自绘 —— 表格不是 balloonui 提供的控件，本工程六个界面一律
 *  这样处理，这里沿用同一形态以免多出一套写法。
 *
 *  数据流：本控件只负责展示与交互。点「刷新」「新建版本」由子按钮自己上报；
 *  行内操作（发布 / 撤回 / 回滚 / 编辑）自绘并自行命中，命中后把动作暂存进
 *  m_pending 再经 WM_DUI_NOTIFY 上报，AdminFrame 收到后调 GetPendingAction
 *  取出来决定发哪个请求，应答回来再经 SetVersions 回填本控件。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#pragma once

#include "DuiControl.h"

#include <vector>

#include "Util/AdminApi.h"   // ClientVersionItem

namespace balloonwjui { class DuiButton; }

namespace flamingoadmin {

class OverlayScrollbar;   // 表格区的悬浮 auto-hide 滚动条子控件

// 行内操作的种类。GetPendingAction 用它告诉 AdminFrame 管理员点了什么。
enum class CvRowAction
{
    None,       // 没有待处理动作
    Publish,    // 发布（草稿 → 已发布）
    Disable,    // 撤回（已发布 → 草稿态，不再下发）
    Edit,       // 编辑：改最低版本与更新说明
};

// 一次待处理的行内操作。
struct CvPendingAction
{
    CvRowAction m_action;  // 动作种类
    long long   m_id;      // 目标记录 ID

    CvPendingAction()
        : m_action(CvRowAction::None), m_id(0)
    {
    }
};

// 客户端版本界面。
class ClientVersionsScreen : public balloonwjui::DuiControl
{
public:
    ClientVersionsScreen();

    // AdminFrame 在 clientversion/list.do 应答后调用，回填表格。
    //   list：版本记录列表，已按发布时间倒序。
    void SetVersions(const std::vector<adminapi::ClientVersionItem>& list);

    // AdminFrame 在 clientversion/scanpackages.do 应答后调用：把服务端的升级包
    // 目录显示在提示条上，管理员据此知道该把 zip 放到哪儿去。
    //   dir：服务端升级包目录的绝对路径。
    void SetPackageDir(const CString& dir);

    // 取出并<u>清空</u>待处理的行内操作。AdminFrame 收到本控件的 DUIN_CLICK
    // 后调用一次；返回 m_action 为 None 表示没有待处理动作。
    CvPendingAction TakePendingAction();

    // 当前选中行对应的记录；界面点「编辑」时 AdminFrame 用它预填弹窗。
    // 没有选中行时返回的记录 m_id 为 0。
    const adminapi::ClientVersionItem& GetSelected() const;

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;
    bool OnSetCursor(POINT pt) override;

private:
    // 一个可点击命中区。
    struct Hit
    {
        RECT        m_rc;      // 命中矩形（host 客户区坐标）
        CvRowAction m_action;  // 该区域对应的动作
        long long   m_id;      // 目标记录 ID
    };

    // 重算表格区的滚动上限，并把当前滚动位置夹回合法区间。
    void ClampScroll();

    // 绘制表格（表头 + 数据行 + 空态）。绘制过程中顺带重建 m_hits。
    //   hdc：目标 DC。
    //   rc：表格区矩形。
    void PaintTable(HDC hdc, const RECT& rc);

private:
    std::vector<adminapi::ClientVersionItem> m_list;    //当前展示的版本记录
    CString                                  m_dir;     //服务端升级包目录，提示条上显示
    CvPendingAction                          m_pending; //待 AdminFrame 取走的行内操作
    int                                      m_selected; //选中行下标；-1 表示无选中
    adminapi::ClientVersionItem              m_empty;   //GetSelected 在无选中时返回它

    std::vector<Hit>  m_hits;       //本轮绘制建立的命中区
    RECT              m_tableRc;    //表格区矩形，滚轮命中判定用
    int               m_scroll;     //表格区纵向滚动偏移（像素）
    int               m_scrollMax;  //纵向滚动上限（像素）
    OverlayScrollbar* m_scrollbar;  //表格区悬浮滚动条；所有权在子控件列表
    balloonwjui::DuiButton* m_btnRefresh;  //页头「刷新」按钮；所有权在子控件列表
    balloonwjui::DuiButton* m_btnNew;      //页头「新建版本」按钮；所有权在子控件列表
};

} // namespace flamingoadmin
