/**
 *  帖子群窗口的表情选择浮层 CThreadFacePicker（docs/帖子群方案.md 4.4：「＋」打开现有的表情选择浮层，用 143 个表情中的
 *  任意一个回应）。
 *
 *  做法与主程序的 CFaceImagePickerPopup（Source/UI/BalloonChatWnd/FaceImagePickerPopup）相同：子类化 balloonui 的
 *  DuiPopupHost（无边框浮层，自带 ESC / 失焦自动关闭），里面放 DuiEmojiPanel 承载表情格子，鼠标停在格子上显示表情名。
 *  不直接编译主程序那份，是因为它从 CFaceList 读表情库，会把用了 #import 的 XmlDocument.h 带进 DLL；本类改从模块
 *  拿到的表情库（宿主经 IThreadGroupHost::GetFaceInfo 给的编号、名字与图片路径）建格子。
 *
 *  选中一个表情后经回调交出表情编号；浮层本身不关闭，由窗口在这次点击处理完之后调用 Hide（不在面板自己的点击回调里关）。
 *
 *  本文件遵循 balloonui 头文件约定：包含它之前调用方须已引入 ATL/WTL（本工程的 stdafx.h）。
 *
 *  典型用法：
 *      m_facePicker.SetPickCallback(&CThreadWindow::FacePickedThunk, this);
 *      m_facePicker.OpenAt(buttonScreenRect, faceEntries);
 *
 *  balloonwj@qq.com   2026-10-07
 */
#pragma once

#include <vector>

#include "../../../third-party/balloonui/balloonui/Controls/Feedback/DuiPopupHost.h"
#include "../Host/ThreadWindowApi.h"   // ThreadFaceEntry

namespace balloonwjui {
class DuiEmojiPanel;
}

namespace threadgroup {

/**
 *  CThreadFacePicker —— 表情选择浮层。只在界面线程上使用。
 */
class CThreadFacePicker : public balloonwjui::DuiPopupHost
{
public:
    //选中表情时的回调：user 为登记时传入的指针，faceId 为表情编号
    typedef void (*PickFn)(void* user, int faceId);

    CThreadFacePicker();
    ~CThreadFacePicker();

    //登记选中表情时的回调（fn 为 NULL 时取消）
    void SetPickCallback(PickFn fn, void* user);

    /**
     *  在锚点矩形附近打开浮层（优先向上弹，越界时自动翻边）。第一次打开时按表情库建格子。
     *  @param anchorScreen 锚点矩形（屏幕坐标，一般是「＋」按钮）
     *  @param faces        表情库（只在第一次打开时读取）
     */
    void OpenAt(const RECT& anchorScreen, const std::vector<ThreadFaceEntry>& faces);

    BEGIN_MSG_MAP(CThreadFacePicker)
        MESSAGE_HANDLER(WM_MOUSEMOVE, OnPanelMouseMove)
        MESSAGE_HANDLER(WM_MOUSELEAVE, OnPanelMouseLeave)
        CHAIN_MSG_MAP(balloonwjui::DuiPopupHost)
    END_MSG_MAP()

private:
    //鼠标在浮层里移动：按命中的格子显示表情名（DuiEmojiPanel 是一个控件里有很多格子，格子之间移动不会触发宿主的提示派发）
    LRESULT OnPanelMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    //鼠标离开浮层：只重置「上次提示的格子」
    LRESULT OnPanelMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

    //第一次打开时建格子：逐个把表情图片解码成位图交给 DuiEmojiPanel
    void BuildOnce(const std::vector<ThreadFaceEntry>& faces);

    //DuiEmojiPanel 的选中回调（user 为本对象，index 为格子序号）
    static void OnPanelPick(void* user, LPCTSTR sequence, int index);

private:
    PickFn                      m_pickFn;     //选中回调
    void*                       m_pickUser;   //回调的参数（窗口，不拥有）
    bool                        m_built;      //格子是否已建好
    balloonwjui::DuiEmojiPanel* m_panel;      //表情格子面板（归浮层的控件树所有）
    std::vector<int>            m_faceIds;    //各格子对应的表情编号（与格子同序）
    std::vector<std::wstring>   m_faceNames;  //各格子对应的表情名（提示用）
    std::vector<HBITMAP>        m_bitmaps;    //各格子的表情位图（本对象持有，析构时释放）
    int                         m_lastTipIdx; //上次显示提示的格子；-1 表示没有
};

} // namespace threadgroup
