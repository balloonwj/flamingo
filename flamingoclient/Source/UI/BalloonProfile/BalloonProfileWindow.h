/**
 *  BalloonProfileWindow.h
 *
 *  个人信息对话框 —— 基于 balloonui，对照 design/个人信息.png（查看态）
 *  与 个人信息02.png（编辑态）。窗口展示当前登录用户的头像 / 姓名 / 账号 /
 *  在线状态及昵称 / 个性签名 / 性别 / 生日 / 所在地 / 邮箱六个字段。
 *
 *  对话框有"查看 / 编辑"两态：
 *    · 查看态 —— 字段为只读文本，底部"关闭 + 编辑资料"。
 *    · 编辑态 —— 字段为文本框 / 下拉框，底部"取消 + 保存"。
 *  "编辑资料"进入编辑态；"取消" / "保存"回到查看态。"保存"把编辑态控件
 *  内容回写 ProfileModel（当前仅内存暂存，提交服务器另议）。
 *
 *  调试期由 CMainDlg 的 --profile 入口直接弹出；接入主面板菜单另议。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL
 *  （Flamingo 侧由 stdafx.h 保证）。
 *
 *  balloonwj@qq.com   2026-05-22
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Window/DuiFrameWindow.h"
#include "ProfileModel.h"

class CFlamingoClient;

/**
 *  CBalloonProfileWindow
 *
 *  个人信息对话框窗口。继承 balloonwjui::DuiFrameWindow —— 由它提供无
 *  边框自绘窗体与标题栏。固定尺寸、不可缩放，标题栏只保留关闭按钮。
 *  查看 / 编辑两态布局高度不同，切换时整体重建客户区并 ResizeClient。
 */
class CBalloonProfileWindow : public balloonwjui::DuiFrameWindow
{
public:
    /**
     *  构造函数。
     *    pFMGClient：所属客户端对象（消息中枢），本类只引用、不持有。
     */
    explicit CBalloonProfileWindow(CFlamingoClient* pFMGClient);
    ~CBalloonProfileWindow();

    /**
     *  创建并显示个人信息对话框（初始为查看态）。
     *    hMainDlg：CMainDlg 的窗口句柄（隐藏的消息中枢）；关闭本窗口时
     *              据此通知 CMainDlg。
     */
    void CreateAndShow(HWND hMainDlg);

    BEGIN_MSG_MAP(CBalloonProfileWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnCloseMsg)
        MESSAGE_HANDLER(WM_DUI_NOTIFY, OnDuiNotify)
        CHAIN_MSG_MAP(balloonwjui::DuiFrameWindow)
    END_MSG_MAP()

private:
    // 按当前 m_editMode 重建客户区控件树、接好输入框、并按模式同步窗口
    // 尺寸。窗口创建及每次模式切换都走这里。
    void RebuildUi();

    // 把 m_model 的字段值灌进当前模式的客户区控件（查看态填只读文本，
    // 编辑态填文本框 / 下拉框）。
    void LoadModelIntoUi();

    // 把编辑态客户区控件里用户输入的内容收集回 m_model。仅编辑态有意义。
    void CommitUiToModel();

    // 切换查看 / 编辑模式：toEditMode=true 进入编辑态，false 回到查看态。
    // 内部重建客户区并刷新窗口尺寸。
    //   toEditMode：目标模式。
    void SwitchMode(bool toEditMode);

    // "保存"按钮：把编辑态内容提交回 m_model，再切回查看态。
    void DoSave();

    // WM_CLOSE：调试入口下关闭即结束程序，转交 CMainDlg。
    LRESULT OnCloseMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    // WM_DUI_NOTIFY：处理头像点击 / 状态胶囊切换 / 底部按钮点击。
    LRESULT OnDuiNotify(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    CFlamingoClient* m_lpFMGClient;   // 所属客户端（消息中枢），只引用不持有
    HWND             m_hMainDlg;      // CMainDlg 句柄（隐藏的消息中枢）
    bool             m_editMode;      // 当前是否处于编辑态（false=查看态）
    balloonprofile::ProfileModel m_model;   // 个人信息数据模型
};
