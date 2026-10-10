/**
 *  flamingoAdmin —— 新建客户端版本弹窗 ClientVersionModal。
 *
 *  「客户端版本」界面点「新建版本」时弹出。升级包由运维手工放进服务器的升级包
 *  目录，控制台<u>不负责上传</u>，故本弹窗没有文件选择与上传进度 —— 它做的是：
 *    · 列出服务端扫描到的候选包（文件名 / 大小 / 是不是 zip / 有没有被引用），
 *      单选一个；不是 zip 的项标灰且点不中，那多半是放错了文件；
 *    · 填版本号、最低版本号、更新说明；
 *    · 存为草稿或直接发布。
 *  包的 MD5 与字节数由服务端算好随候选包一起下发，<u>不需要人工输入</u>。
 *
 *  实现与其它弹窗一致：半透明遮罩 + 居中卡片，常驻 RootContainer 弹窗层，
 *  开 / 关只切显隐；点遮罩不关闭。三个输入框是 DuiEdit，随本弹窗一起参与
 *  绘制、受弹窗矩形裁剪；候选包列表为手写自绘可滚动区，复用 OverlayScrollbar
 *  悬浮滚动条（见开发约定文档
 *  「UI 约定」）。各按钮经自己的 ctrlId 上报 AdminFrame。
 *
 *  balloonwj@qq.com   2026-08-11
 */
#pragma once

#include "DuiControl.h"

#include <vector>

#include "Util/AdminApi.h"   // UpdatePackageItem / ScanPackagesResult

namespace balloonwjui { class DuiEdit; }
namespace balloonwjui { class DuiButton; }

namespace flamingoadmin {

class OverlayScrollbar;   // 候选包列表的悬浮滚动条子控件

// 新建客户端版本弹窗。
class ClientVersionModal : public balloonwjui::DuiControl
{
public:
    ClientVersionModal();

    // 切换到<u>编辑态</u>：只改最低版本与更新说明，包与版本号锁定不可改。
    // 版本号与升级包一旦发布就可能已被客户端下载，原地改会让同一个版本号对应
    // 两份内容，排查时无从分辨；要换包请新建一条记录。
    //   item：要编辑的那条版本记录。
    void SetEditTarget(const adminapi::ClientVersionItem& item);

    // 当前是否处于编辑态（false 为新建态）。
    bool IsEditing() const { return m_editingId != 0; }

    // 编辑态下正在编辑的记录 ID；新建态返回 0。
    long long GetEditingId() const { return m_editingId; }

    // 编辑态下这条记录的版本号（最低版本不得高于它）；新建态返回空串。
    CString GetEditingVersion() const { return m_editingVersion; }

    // 装入一次扫描结果并切回<u>新建态</u>，同时清空表单（每次打开前调用）。
    // 默认选中第一个「是 zip 且尚未被引用」的候选包 —— 那多半就是刚放进去的
    // 新包，省掉一次点击。
    //   scan：clientversion/scanpackages.do 的解析结果。
    void SetPackages(const adminapi::ScanPackagesResult& scan);

    // 校验必填项：选中了一个可用的候选包，且版本号非空。
    bool Validate() const;

    // 选中候选包的 MD5；未选中时返回空串。
    CString GetSelectedMd5() const;

    // 版本号输入框的内容（首尾空白已去掉）。
    CString GetVersion() const;

    // 最低版本号输入框的内容（首尾空白已去掉）；允许为空，表示不设门槛。
    CString GetMinVersion() const;

    // 更新说明输入框的内容。
    CString GetReleaseNote() const;

    //「强制升级」是否勾选。勾上之后客户端的升级窗不给"稍后"出口、也不放行登录。
    bool IsForced() const;

    void Layout(const RECT& rcAvail) override;
    void OnPaint(HDC hdc, const RECT& rcDirty) override;
    bool OnLButtonUp(POINT pt, UINT mkFlags) override;
    bool OnMouseWheel(POINT pt, short zDelta, UINT mkFlags) override;

private:
    // 重算候选包列表的滚动上限，并把当前滚动位置夹回合法区间。
    void ClampScroll();

    // 绘制候选包列表；绘制过程中顺带重建 m_rowHits。
    //   hdc：目标 DC；rc：列表区矩形。
    void PaintPackageList(HDC hdc, const RECT& rc);

private:
    std::vector<adminapi::UpdatePackageItem> m_packages;  //候选包列表
    CString m_dir;       //服务端升级包目录，卡片上提示用
    int     m_selected;  //选中的候选包下标；-1 表示未选

    long long m_editingId;   //编辑态下的目标记录 ID；0 表示新建态
    CString   m_editingVersion;  //编辑态下目标记录的版本号，校验最低版本时用；新建态为空
    CString   m_editInfo;    //编辑态下展示的只读信息行（版本号 · 平台 · 包名）

    std::vector<RECT> m_rowHits;   //候选包各行的命中矩形，与 m_packages 同序
    RECT    m_panelRect;  //卡片矩形
    RECT    m_listRc;     //候选包列表区矩形
    int     m_scroll;     //列表纵向滚动偏移（像素）
    int     m_scrollMax;  //纵向滚动上限（像素）

    balloonwjui::DuiEdit* m_editVersion;  //版本号输入框；所有权在子控件列表
    balloonwjui::DuiEdit* m_editMinVer;   //最低版本号输入框；所有权在子控件列表
    balloonwjui::DuiEdit* m_editNote;     //更新说明输入框；所有权在子控件列表
    balloonwjui::DuiButton*   m_chkForced;    //「强制升级」复选框；所有权在子控件列表
    balloonwjui::DuiButton*   m_btnClose;     //右上角关闭按钮
    balloonwjui::DuiButton*   m_btnRescan;    //「重新扫描」按钮
    balloonwjui::DuiButton*   m_btnCancel;    //「取消」按钮
    balloonwjui::DuiButton*   m_btnDraft;     //「存为草稿」按钮
    balloonwjui::DuiButton*   m_btnPublish;   //「直接发布」按钮
    OverlayScrollbar*         m_scrollbar;    //候选包列表的悬浮滚动条
};

} // namespace flamingoadmin
