/**
 *  TagEditPopup.h
 *
 *  联系人资料对话框「标签」区的输入弹层 —— 新增一个标签、或就地修改某一枚
 *  已有标签时弹出的单行输入框。子类化 balloonui 的 DuiPopupHost（无边框
 *  WS_POPUP 浮层，自带 ESC / 失焦自动关闭），内部承载一个单行 DuiEditHost。
 *
 *  提交时机：
 *    · 回车        → 提交（吞掉回车，避免单行 EDIT 的提示音）。
 *    · 失焦 / 点别处 → 也提交（DuiPopupHost 失焦自动关闭，关闭原因 LostFocus）。
 *    · ESC         → 取消，不提交。
 *
 *  本弹层只负责"把输入框里的文本回吐给调用方"，不做任何业务判定 —— 该不该
 *  追加、是不是重复、要不要发包，一律由调用方跑 ballooncontact::DecideTagAdd /
 *  DecideTagEdit 决定。超长则从源头挡住：内部输入框 SetMaxLength 到
 *  kContactTagMaxLen，用户根本敲不进第 17 个字符。
 *
 *  做法与主面板的 BalloonMain/SignatureEditPopup 同一套路；之所以另写一份而
 *  不是把那一份泛化，是因为两者的默认尺寸、限长与"打开时盖在哪个矩形上"的
 *  口径都不同，硬合并会让参数列表变得比两份实现还长。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL。
 *
 *  balloonwj@qq.com   2026-08-14
 */

#pragma once

#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiPopupHost.h"

namespace balloonwjui { class DuiEditHost; }

namespace ballooncontact {

/**
 *  CTagEditPopup —— 标签输入弹层。
 */
class CTagEditPopup : public balloonwjui::DuiPopupHost
{
public:
    // 提交回调：回车 / 失焦提交时触发一次。
    //   userdata：注册时传入的不透明指针，本类只透传、不解释。
    //   text：输入框当前文本（<u>未</u>去首尾空白、<u>未</u>做任何判定）。
    typedef void (*CommitCallback)(void* userdata, const CString& text);

    CTagEditPopup();
    ~CTagEditPopup();

    /**
     *  注册提交回调。
     *    cb：回调函数指针；nullptr 表示取消注册。
     *    userdata：回调时原样带回的不透明指针；本类不持有其生命周期，
     *              调用方须保证它活得比本弹层久（一般传宿主窗口 this）。
     */
    void SetCommitCallback(CommitCallback cb, void* userdata);

    /**
     *  在指定的屏幕矩形处打开输入框。
     *
     *  弹层左上角对齐 anchorScreen 的左上角，尺寸取"anchorScreen 的尺寸"与
     *  内置最小尺寸中较大的那个 —— 于是修改标签时输入框正好盖住那枚胶囊，
     *  新增标签时正好盖住「+ 添加标签」按钮，视觉上就是"原地变成输入框"。
     *
     *    anchorScreen：要盖住的目标矩形（屏幕坐标）。调用方把控件的 host
     *                  客户区矩形 ClientToScreen 之后传进来。
     *    owner：owner 窗口句柄（资料对话框），让弹层随父窗口销毁、不进任务栏。
     *    currentText：带入输入框的初始文本并全选（新增时传空串）；nullptr 视为空串。
     */
    void OpenAt(const RECT& anchorScreen, HWND owner, LPCTSTR currentText);

    // 供内部 EDIT 的子类化过程回调：回车提交。外部不要直接调。
    void OnEditEnter();

    // 供内部 EDIT 的子类化过程回调：ESC 取消。外部不要直接调。
    void OnEditEscape();

    BEGIN_MSG_MAP(CTagEditPopup)
        CHAIN_MSG_MAP(balloonwjui::DuiPopupHost)
    END_MSG_MAP()

private:
    // 首次打开时构建一次内容树（一个单行 DuiEditHost）。重复调用无副作用。
    void BuildContentOnce();

    // DuiPopupHost dismiss 回调的转接函数（C 风格回调没有 this）。
    static void OnDismissThunk(void* userdata, balloonwjui::DuiPopupHost::Reason r);

    // dismiss 的实际处理：失焦关闭视为提交；ESC / 程序化关闭不提交。
    void OnDismiss(balloonwjui::DuiPopupHost::Reason r);

    // 读输入框文本并回吐给调用方。幂等（m_handled 守卫），避免"回车提交后
    // 弹层隐藏又触发一次失焦"导致提交两次。
    void DoCommit();

private:
    balloonwjui::DuiEditHost* m_edit;        // 单行输入框（借用指针，所有权在内容树里）
    bool           m_built;                  // 内容树是否已构建（只建一次）
    bool           m_editCreated;            // 内部真 Win32 EDIT 是否已创建（只建一次）
    bool           m_keyHooked;              // 是否已给 EDIT 挂上回车 / ESC 的子类化过程（只挂一次）
    bool           m_handled;                // 本次打开是否已提交或已取消，防重复提交
    CommitCallback m_commitCb;               // 提交回调；NULL 表示未注册
    void*          m_commitUd;               // 提交回调的用户数据，本类只透传
};

} // namespace ballooncontact
